#include "stdafx.h"

#include "CrashHandler.h"
#include "ProcessMemory.h"

#include "rose/common/log.h"

#include <ctype.h>
#include <dbghelp.h>
#include <string.h>

namespace {

//--------------------------------------------------------------------------------
/// Everything below runs inside a process that has already failed, so it obeys
/// crash-handler rules rather than ordinary ones:
///
///  - No CRT. `wsprintfA` (user32) and `CreateFileA`/`WriteFile` do not touch the
///    CRT heap, which may be the very thing that is corrupt. That costs us %f and
///    a 1024-byte output cap; neither matters here, and we are 32-bit so %08X
///    covers every pointer.
///  - No LoadLibrary at fault time. A crash can happen while the loader lock is
///    held, and loading dbghelp there deadlocks instead of producing a dump. It is
///    resolved once, at install time, and cached.
///  - No re-entry. A fault inside the handler must not recurse; the first thread in
///    wins and any later one is passed straight to the default handler.
//--------------------------------------------------------------------------------

typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(HANDLE hProcess,
    DWORD ProcessId,
    HANDLE hFile,
    MINIDUMP_TYPE DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

HMODULE g_hDbgHelp = NULL;
MiniDumpWriteDumpFn g_pfnMiniDumpWriteDump = NULL;
MINIDUMP_TYPE g_eDumpType = MiniDumpNormal;
LONG g_lHandlerEntered = 0;
LPTOP_LEVEL_EXCEPTION_FILTER g_pPreviousFilter = NULL;

/// Address space held back for the dump writer. An out-of-memory crash
/// (std::bad_alloc) arrives with the address space full, and MiniDumpWriteDump
/// allocates as it works: on 2026-11-01 all three such crashes left a 0-byte .dmp.
/// Reserved only, never committed, so it costs address space and no RAM; released
/// at fault time, right before the dump is written.
const SIZE_T kEmergencyReserveBytes = 32 * 1024 * 1024;
void* g_pEmergencyReserve = NULL;

void
AppendText(char* szBuffer, int iCapacity, int* piLength, const char* szText) {
    for (int iC = 0; szText[iC] && *piLength < iCapacity - 1; ++iC) {
        szBuffer[(*piLength)++] = szText[iC];
    }
    szBuffer[*piLength] = '\0';
}

/// Which module owns this address, and where inside it? "znzin.dll+0x0001A2B4" is
/// the single most useful line in the file: it points at the guilty binary without
/// anyone opening the dump, and the offset feeds straight into a `ln` in WinDbg.
void
DescribeAddress(const void* pAddress, char* szOut, int iCapacity) {
    HMODULE hModule = NULL;
    szOut[0] = '\0';

    if (!::GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)pAddress,
            &hModule)
        || hModule == NULL) {
        ::wsprintfA(szOut, "<no module> (0x%08X)", (unsigned int)(UINT_PTR)pAddress);
        return;
    }

    char szPath[MAX_PATH] = {0};
    if (0 == ::GetModuleFileNameA(hModule, szPath, MAX_PATH)) {
        ::lstrcpynA(szPath, "<unknown>", MAX_PATH);
    }

    const char* szName = szPath;
    for (const char* pC = szPath; *pC; ++pC) {
        if (*pC == '\\' || *pC == '/') {
            szName = pC + 1;
        }
    }

    const UINT_PTR ulRVA = (UINT_PTR)pAddress - (UINT_PTR)hModule;
    if (iCapacity > 64) {
        ::wsprintfA(szOut, "%s+0x%08X (0x%08X)", szName, (unsigned int)ulRVA,
            (unsigned int)(UINT_PTR)pAddress);
    }
}

const char*
ExceptionName(DWORD dwCode) {
    switch (dwCode) {
        case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
        case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
        case 0xE06D7363: return "C++ exception (unhandled throw)";
        default: return "unknown";
    }
}

/// The decorated name of a thrown C++ type, e.g. ".?AVbad_alloc@std@@".
///
/// An unhandled throw otherwise reads as 0xE06D7363 and nothing else. On x86 the
/// record's third parameter is the compiler's _ThrowInfo, which leads through a
/// catchable-type array to a TypeDescriptor whose name is the decorated type --
/// absolute pointers all the way (x64 stores image-relative offsets instead).
/// Every hop is read-checked: this runs in a process that has already failed.
bool
ThrownTypeName(const EXCEPTION_RECORD* pRecord, char* szOut, int iCapacity) {
    szOut[0] = '\0';
#if defined(_M_IX86)
    if (pRecord->ExceptionCode != 0xE06D7363 || pRecord->NumberParameters < 3) {
        return false;
    }
    // _ThrowInfo { attributes, pmfnUnwind, pForwardCompat, pCatchableTypeArray }
    const DWORD* pThrowInfo = (const DWORD*)pRecord->ExceptionInformation[2];
    if (pThrowInfo == NULL || ::IsBadReadPtr(pThrowInfo, 4 * sizeof(DWORD))) {
        return false;
    }
    // _CatchableTypeArray { nCatchableTypes, arrayOfCatchableTypes[] }; [0] is the thrown type
    const DWORD* pTypeArray = (const DWORD*)pThrowInfo[3];
    if (pTypeArray == NULL || ::IsBadReadPtr(pTypeArray, 2 * sizeof(DWORD)) || pTypeArray[0] == 0) {
        return false;
    }
    // _CatchableType { properties, pType, ... }
    const DWORD* pCatchable = (const DWORD*)pTypeArray[1];
    if (pCatchable == NULL || ::IsBadReadPtr(pCatchable, 2 * sizeof(DWORD))) {
        return false;
    }
    // TypeDescriptor { pVFTable, spare, name[] }
    const char* pTypeDescriptor = (const char*)pCatchable[1];
    if (pTypeDescriptor == NULL || ::IsBadReadPtr(pTypeDescriptor, 2 * sizeof(DWORD) + 1)) {
        return false;
    }
    const char* szName = pTypeDescriptor + 2 * sizeof(DWORD);
    if (::IsBadStringPtrA(szName, (UINT_PTR)iCapacity)) {
        return false;
    }
    ::lstrcpynA(szOut, szName, iCapacity);
    return szOut[0] != '\0';
#else
    (void)pRecord;
    (void)iCapacity;
    return false;
#endif
}

void
WriteSummaryFile(const char* szPath, EXCEPTION_POINTERS* pExceptionInfo, const char* szDumpName) {
    HANDLE hFile = ::CreateFileA(
        szPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }

    char szBuffer[4096];
    char szLine[1024];
    char szWhere[512];
    int iLength = 0;
    szBuffer[0] = '\0';

    const EXCEPTION_RECORD* pRecord = pExceptionInfo->ExceptionRecord;

    AppendText(szBuffer, sizeof(szBuffer), &iLength, "rose-next client crash\r\n");
    AppendText(szBuffer, sizeof(szBuffer), &iLength, "======================\r\n\r\n");

    // Both clocks, deliberately. client.log timestamps are UTC while the file
    // system (and this file's name) are local, so a single clock here would make
    // lining the two up guesswork -- it is a two-hour offset in summer.
    SYSTEMTIME stLocal, stUtc;
    ::GetLocalTime(&stLocal);
    ::GetSystemTime(&stUtc);
    ::wsprintfA(szLine,
        "local time : %04d-%02d-%02d %02d:%02d:%02d\r\nUTC time   : %04d-%02d-%02d %02d:%02d:%02d  (matches client.log)\r\n\r\n",
        stLocal.wYear, stLocal.wMonth, stLocal.wDay, stLocal.wHour, stLocal.wMinute, stLocal.wSecond,
        stUtc.wYear, stUtc.wMonth, stUtc.wDay, stUtc.wHour, stUtc.wMinute, stUtc.wSecond);
    AppendText(szBuffer, sizeof(szBuffer), &iLength, szLine);

    ::wsprintfA(szLine, "exception  : 0x%08X  %s\r\n", (unsigned int)pRecord->ExceptionCode,
        ExceptionName(pRecord->ExceptionCode));
    AppendText(szBuffer, sizeof(szBuffer), &iLength, szLine);

    if (ThrownTypeName(pRecord, szWhere, 256)) {
        ::wsprintfA(szLine, "thrown type: %s\r\n", szWhere);
        AppendText(szBuffer, sizeof(szBuffer), &iLength, szLine);
    }

    DescribeAddress(pRecord->ExceptionAddress, szWhere, sizeof(szWhere));
    ::wsprintfA(szLine, "faulted at : %s\r\n", szWhere);
    AppendText(szBuffer, sizeof(szBuffer), &iLength, szLine);

    // For an AV the two parameters say read vs write and the address touched --
    // "wrote to 0x00000000" versus "read from 0xFEEEFEEE" (freed heap) usually
    // names the bug class before the dump is even opened.
    if (pRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRecord->NumberParameters >= 2) {
        const ULONG_PTR ulOperation = pRecord->ExceptionInformation[0];
        const ULONG_PTR ulAddress = pRecord->ExceptionInformation[1];
        const char* szOperation = (ulOperation == 0)   ? "read from"
                                  : (ulOperation == 1) ? "wrote to"
                                  : (ulOperation == 8) ? "executed (DEP)"
                                                       : "accessed";
        ::wsprintfA(szLine, "access     : %s 0x%08X\r\n", szOperation, (unsigned int)ulAddress);
        AppendText(szBuffer, sizeof(szBuffer), &iLength, szLine);
    }

    ::wsprintfA(szLine, "thread     : %u\r\n", (unsigned int)::GetCurrentThreadId());
    AppendText(szBuffer, sizeof(szBuffer), &iLength, szLine);

    // Taken before the emergency reserve is released, so it is the state that
    // failed. The 2026-11-01 testers' std::bad_alloc crashes left 0-byte dumps --
    // MiniDumpWriteDump needs memory too -- and nothing said the address space was
    // full; this line says so on its own.
    Rose::Client::ProcessMemory stMem;
    Rose::Client::SampleProcessMemory(stMem, true);
    ::wsprintfA(szLine,
        "memory     : address space %u/%u MB used (image %u, mapped %u, private %u), "
        "largest free block %u MB, commit %u MB\r\n\r\n",
        stMem.va_used_mb, stMem.va_total_mb, stMem.image_mb, stMem.mapped_mb, stMem.private_va_mb,
        stMem.largest_free_mb, stMem.private_mb);
    AppendText(szBuffer, sizeof(szBuffer), &iLength, szLine);

#if defined(_M_IX86)
    const CONTEXT* pContext = pExceptionInfo->ContextRecord;
    ::wsprintfA(szLine,
        "eip=%08X esp=%08X ebp=%08X\r\neax=%08X ebx=%08X ecx=%08X edx=%08X\r\nesi=%08X edi=%08X\r\n\r\n",
        pContext->Eip, pContext->Esp, pContext->Ebp, pContext->Eax, pContext->Ebx, pContext->Ecx,
        pContext->Edx, pContext->Esi, pContext->Edi);
    AppendText(szBuffer, sizeof(szBuffer), &iLength, szLine);

    // Return addresses off the raw stack. This is not a real unwind -- it is every
    // stack slot that looks like it points into a loaded module -- so expect stale
    // frames mixed in with live ones. It costs nothing and it means the .txt alone
    // often names the area, which matters when the dump is on someone else's disk.
    AppendText(szBuffer, sizeof(szBuffer), &iLength,
        "possible return addresses on the stack (unverified, may include stale frames):\r\n");

    const UINT_PTR* pStack = (const UINT_PTR*)pContext->Esp;
    int iFound = 0;
    for (int iSlot = 0; iSlot < 1024 && iFound < 24; ++iSlot) {
        if (::IsBadReadPtr(pStack + iSlot, sizeof(UINT_PTR))) {
            break;
        }

        const UINT_PTR ulCandidate = pStack[iSlot];
        HMODULE hOwner = NULL;
        if (ulCandidate < 0x10000
            || !::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)ulCandidate,
                &hOwner)) {
            continue;
        }

        DescribeAddress((const void*)ulCandidate, szWhere, sizeof(szWhere));
        ::wsprintfA(szLine, "  [esp+0x%04X] %s\r\n", (unsigned int)(iSlot * sizeof(UINT_PTR)), szWhere);
        AppendText(szBuffer, sizeof(szBuffer), &iLength, szLine);
        ++iFound;
    }
#endif

    AppendText(szBuffer, sizeof(szBuffer), &iLength, "\r\nminidump   : ");
    AppendText(szBuffer, sizeof(szBuffer), &iLength, szDumpName);
    AppendText(szBuffer, sizeof(szBuffer), &iLength,
        "\r\n\r\nOpen the dump with symbols from bin\\release (rosenext.pdb / znzin.pdb must\r\n"
        "match the deployed binaries -- scripts\\debug-client-crash.ps1 -VerifyOnly checks that).\r\n");

    DWORD dwWritten = 0;
    ::WriteFile(hFile, szBuffer, (DWORD)iLength, &dwWritten, NULL);
    ::CloseHandle(hFile);
}

/// Fatal codes that never reach an unhandled-exception filter.
///
/// `SetUnhandledExceptionFilter` is only consulted when an exception unwinds out
/// of every SEH frame. The fail-fast path does not unwind: the raiser terminates
/// the process on the spot, so these arrive nowhere and a real crash leaves no
/// crash-*.txt, no crash-*.dmp, and a log that simply stops.
///
/// That is not hypothetical -- it is how the 2026-09-12 Karkia double-free
/// presented. The heap detected the second free of a bone-effect array, raised
/// STATUS_HEAP_CORRUPTION, and the installed handler never ran. The only evidence
/// left was a truncated client.log.
///
/// A vectored handler runs *before* SEH, which is what makes it able to see these
/// -- and also why it must be extremely selective. It is not a general net: an
/// ordinary access violation that some frame goes on to handle, and every C++
/// throw (0xE06D7363), also pass through here, so anything not on this list is
/// handed straight back.
bool
IsFailFastCode(DWORD dwCode) {
    switch (dwCode) {
        case 0xC0000374: // STATUS_HEAP_CORRUPTION
        case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN (__fastfail, /GS)
        case 0xC0000429: // STATUS_FATAL_APP_EXIT
            return true;
        default:
            return false;
    }
}

LONG WINAPI
CrashFilter(EXCEPTION_POINTERS* pExceptionInfo);

LONG CALLBACK
FailFastVectoredHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    if (pExceptionInfo == NULL || pExceptionInfo->ExceptionRecord == NULL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (!IsFailFastCode(pExceptionInfo->ExceptionRecord->ExceptionCode)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Write the same dump the filter would have, then let the OS proceed with the
    // termination it was always going to perform.
    CrashFilter(pExceptionInfo);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI
CrashFilter(EXCEPTION_POINTERS* pExceptionInfo) {
    // A fault inside the handler must not recurse. First thread in wins; anyone
    // else goes straight to the default handler rather than racing us to the file.
    if (::InterlockedExchange(&g_lHandlerEntered, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    SYSTEMTIME st;
    ::GetLocalTime(&st);

    char szStamp[32];
    ::wsprintfA(szStamp, "crash-%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
        st.wMinute, st.wSecond);

    char szDumpName[64];
    char szTextName[64];
    ::wsprintfA(szDumpName, "%s.dmp", szStamp);
    ::wsprintfA(szTextName, "%s.txt", szStamp);

    // Relative paths: the client's CWD is the game directory (it resolves
    // rose.vfs / data.idx / rose-next.ini from there), so these land beside
    // client.log and error.txt -- and beside the crash-*.log cdb writes.
    WriteSummaryFile(szTextName, pExceptionInfo, szDumpName);

    if (g_pEmergencyReserve != NULL) {
        ::VirtualFree(g_pEmergencyReserve, 0, MEM_RELEASE);
        g_pEmergencyReserve = NULL;
    }

    if (g_pfnMiniDumpWriteDump != NULL) {
        HANDLE hFile = ::CreateFileA(szDumpName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION stInfo;
            stInfo.ThreadId = ::GetCurrentThreadId();
            stInfo.ExceptionPointers = pExceptionInfo;
            stInfo.ClientPointers = FALSE;

            // Dumping a process from inside its own exception filter is not what
            // MSDN recommends (a helper process cannot be affected by the fault),
            // but for an access violation it works, and a second process is a lot
            // of machinery to keep alive for a bug we may see once a week.
            g_pfnMiniDumpWriteDump(
                ::GetCurrentProcess(), ::GetCurrentProcessId(), hFile, g_eDumpType, &stInfo, NULL, NULL);
            ::CloseHandle(hFile);
        }
    }

    // Hand back to whatever was installed before us so a debugger still sees the
    // fault: under cdb (scripts/debug-client-crash.ps1) we now write the dump and
    // cdb still breaks in, which is strictly better than either alone.
    if (g_pPreviousFilter != NULL) {
        return g_pPreviousFilter(pExceptionInfo);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

MINIDUMP_TYPE
ResolveDumpType(bool* pbEnabled) {
    char szMode[32] = {0};
    ::GetPrivateProfileStringA("LOG", "CRASHDUMP", "normal", szMode, sizeof(szMode), "./rose-next.ini");

    char szEnv[32] = {0};
    if (::GetEnvironmentVariableA("ROSE_CRASHDUMP", szEnv, sizeof(szEnv)) != 0) {
        ::lstrcpynA(szMode, szEnv, sizeof(szMode));
    }

    for (int iC = 0; szMode[iC]; ++iC) {
        szMode[iC] = (char)::tolower((unsigned char)szMode[iC]);
    }

    *pbEnabled = (0 != ::strcmp(szMode, "off")) && (0 != ::strcmp(szMode, "0"));

    // MiniDumpWithFullMemoryInfo in both: the region list (base, size, state,
    // type), not the memory itself -- a few hundred KB that make `!address
    // -summary` work, so a dump says how full the address space was.
    if (0 == ::strcmp(szMode, "full")) {
        return (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo | MiniDumpWithHandleData
            | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData | MiniDumpWithThreadInfo);
    }

    // Default. Stacks plus globals and thread state -- enough to name the faulting
    // code and read most of what it was working on, at tens of MB rather than the
    // whole 32-bit address space.
    return (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithFullMemoryInfo | MiniDumpWithHandleData
        | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData | MiniDumpWithThreadInfo);
}

} // namespace

namespace Rose {
namespace Client {

void
InstallCrashHandler(void) {
    bool bEnabled = true;
    g_eDumpType = ResolveDumpType(&bEnabled);

    if (!bEnabled) {
        LOG_INFO("Crash dumps disabled ([LOG] CRASHDUMP=off)");
        return;
    }

    // Resolved now, never at fault time -- see the note at the top of the file.
    g_hDbgHelp = ::LoadLibraryA("dbghelp.dll");
    if (g_hDbgHelp != NULL) {
        g_pfnMiniDumpWriteDump =
            (MiniDumpWriteDumpFn)::GetProcAddress(g_hDbgHelp, "MiniDumpWriteDump");
    }

    g_pEmergencyReserve = ::VirtualAlloc(NULL, kEmergencyReserveBytes, MEM_RESERVE, PAGE_NOACCESS);

    g_pPreviousFilter = ::SetUnhandledExceptionFilter(CrashFilter);

    // First in the vectored chain, so a fail-fast is seen before anything can
    // swallow it. It filters hard on the exception code -- see IsFailFastCode.
    ::AddVectoredExceptionHandler(1, FailFastVectoredHandler);

    if (g_pfnMiniDumpWriteDump == NULL) {
        LOG_WARN("Crash handler installed, but dbghelp.dll!MiniDumpWriteDump did not resolve -- "
                 "a crash will still write crash-*.txt, but no minidump");
    } else {
        LOG_INFO("Crash handler installed: crash-*.dmp + crash-*.txt in the game directory "
                 "([LOG] CRASHDUMP = normal | full | off, ROSE_CRASHDUMP overrides)");
    }
}

} // namespace Client
} // namespace Rose
