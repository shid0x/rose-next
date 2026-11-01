#include "stdafx.h"

#include "ProcessMemory.h"

#include "rose/common/log.h"

#include <psapi.h>
#pragma comment(lib, "psapi.lib")

namespace {

const unsigned int kLogIntervalMs = 60 * 1000;
const unsigned int kHudIntervalMs = 1000;

unsigned int g_uiPeakVaMb = 0;
DWORD g_dwLastLog = 0;
int g_iLastLoggedZone = -1;

Rose::Client::ProcessMemory g_HudSample = {};
DWORD g_dwLastHudSample = 0;

inline unsigned int
ToMb(unsigned long long ullBytes) {
    return (unsigned int)(ullBytes / (1024 * 1024));
}

} // namespace

namespace Rose {
namespace Client {

void
SampleProcessMemory(ProcessMemory& out, bool bWalk) {
    ::ZeroMemory(&out, sizeof(out));

    MEMORYSTATUSEX ms;
    ::ZeroMemory(&ms, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (::GlobalMemoryStatusEx(&ms)) {
        out.va_total_mb = ToMb(ms.ullTotalVirtual);
        out.va_used_mb = ToMb(ms.ullTotalVirtual - ms.ullAvailVirtual);
    }

    PROCESS_MEMORY_COUNTERS_EX pmc;
    ::ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        out.private_mb = ToMb(pmc.PrivateUsage);
        out.working_set_mb = ToMb(pmc.WorkingSetSize);
    }

    if (out.va_used_mb > g_uiPeakVaMb) {
        g_uiPeakVaMb = out.va_used_mb;
    }

    if (!bWalk) {
        return;
    }

    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    const UINT_PTR ulEnd = (UINT_PTR)si.lpMaximumApplicationAddress;

    unsigned long long ullImage = 0, ullMapped = 0, ullPrivate = 0, ullLargestFree = 0;
    MEMORY_BASIC_INFORMATION mbi;
    // Unsigned throughout: with /LARGEADDRESSAWARE the walk crosses 0x80000000.
    for (UINT_PTR ulAt = (UINT_PTR)si.lpMinimumApplicationAddress; ulAt < ulEnd;) {
        if (::VirtualQuery((LPCVOID)ulAt, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.RegionSize == 0) {
            break;
        }
        if (mbi.State == MEM_FREE) {
            if (mbi.RegionSize > ullLargestFree) {
                ullLargestFree = mbi.RegionSize;
            }
        }
        else if (mbi.Type == MEM_IMAGE) {
            ullImage += mbi.RegionSize;
        }
        else if (mbi.Type == MEM_MAPPED) {
            ullMapped += mbi.RegionSize;
        }
        else {
            ullPrivate += mbi.RegionSize;
        }

        const UINT_PTR ulNext = (UINT_PTR)mbi.BaseAddress + mbi.RegionSize;
        if (ulNext <= ulAt) {
            break; // wrapped past the top of the address space
        }
        ulAt = ulNext;
    }

    out.image_mb = ToMb(ullImage);
    out.mapped_mb = ToMb(ullMapped);
    out.private_va_mb = ToMb(ullPrivate);
    out.largest_free_mb = ToMb(ullLargestFree);
}

void
LogProcessMemory(const char* szReason, int iZoneNo) {
    ProcessMemory m;
    SampleProcessMemory(m, true);

    LOG_INFO("Memory ({}, zone {}): address space {}/{} MB used (image {}, mapped {}, private {}; "
             "largest free block {} MB), commit {} MB, working set {} MB, session peak {} MB",
        szReason,
        iZoneNo,
        m.va_used_mb,
        m.va_total_mb,
        m.image_mb,
        m.mapped_mb,
        m.private_va_mb,
        m.largest_free_mb,
        m.private_mb,
        m.working_set_mb,
        g_uiPeakVaMb);

    g_dwLastLog = ::GetTickCount();
}

void
TickProcessMemoryLog(int iZoneNo) {
    if (iZoneNo != g_iLastLoggedZone) {
        g_iLastLoggedZone = iZoneNo;
        LogProcessMemory("zone", iZoneNo);
        return;
    }
    if (::GetTickCount() - g_dwLastLog >= kLogIntervalMs) {
        LogProcessMemory("periodic", iZoneNo);
    }
}

const ProcessMemory&
GetHudProcessMemory(unsigned int* pPeakVaMb) {
    const DWORD dwNow = ::GetTickCount();
    if (g_dwLastHudSample == 0 || dwNow - g_dwLastHudSample >= kHudIntervalMs) {
        SampleProcessMemory(g_HudSample, false);
        g_dwLastHudSample = dwNow;
    }
    if (pPeakVaMb) {
        *pPeakVaMb = g_uiPeakVaMb;
    }
    return g_HudSample;
}

} // namespace Client
} // namespace Rose
