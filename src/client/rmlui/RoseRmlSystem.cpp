#include "stdafx.h"

#include "RoseRmlSystem.h"

#include <windows.h>

#include "rose/common/log.h"

RoseRmlSystem::RoseRmlSystem(): m_dFrequency(1000.0), m_llStart(0) {
    LARGE_INTEGER freq, now;
    if (QueryPerformanceFrequency(&freq) && freq.QuadPart != 0)
        m_dFrequency = (double)freq.QuadPart;
    QueryPerformanceCounter(&now);
    m_llStart = now.QuadPart;
}

double
RoseRmlSystem::GetElapsedTime() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - m_llStart) / m_dFrequency;
}

namespace {

/// True for a path that should be handed to the loader verbatim rather than
/// resolved relative to the referencing document.
bool
IsGameAbsolutePath(const Rml::String& path) {
    if (path.empty())
        return false;

    /// A real absolute path or a drive letter.
    if (path[0] == '/' || path[0] == '\\')
        return true;
    if (path.size() > 1 && path[1] == ':')
        return true;

    /// The game data root, in whatever case the author used. The client itself
    /// is inconsistent about this ("3DData\\Control\\Res\\Ui.TSI" vs
    /// "3ddata/..."), and the VFS is case-insensitive, so accept any spelling.
    const char* kRoot = "3ddata";
    const size_t nRoot = 6;
    if (path.size() < nRoot)
        return false;
    for (size_t i = 0; i < nRoot; ++i) {
        if (tolower((unsigned char)path[i]) != kRoot[i])
            return false;
    }
    return true;
}

} // namespace

void
RoseRmlSystem::JoinPath(Rml::String& translated_path,
    const Rml::String& document_path,
    const Rml::String& path) {
    if (IsGameAbsolutePath(path)) {
        translated_path = path;
        return;
    }

    Rml::SystemInterface::JoinPath(translated_path, document_path, path);
}

bool
RoseRmlSystem::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    switch (type) {
        case Rml::Log::LT_ALWAYS:
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            LOG_ERROR("[rmlui] {}", message.c_str());
            break;
        case Rml::Log::LT_WARNING:
            LOG_WARN("[rmlui] {}", message.c_str());
            break;
        default:
            LOG_INFO("[rmlui] {}", message.c_str());
            break;
    }

    /// false would abort on asserts; keep the client running so a bad document
    /// is a visible log line rather than a crash.
    return true;
}

void
RoseRmlSystem::SetClipboardText(const Rml::String& text) {
    if (!OpenClipboard(NULL))
        return;

    EmptyClipboard();

    const size_t nBytes = text.size() + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, nBytes);
    if (hMem != NULL) {
        void* pDst = GlobalLock(hMem);
        if (pDst != NULL) {
            memcpy(pDst, text.c_str(), nBytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }

    CloseClipboard();
}

void
RoseRmlSystem::GetClipboardText(Rml::String& text) {
    text.clear();

    if (!OpenClipboard(NULL))
        return;

    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData != NULL) {
        const char* pSrc = (const char*)GlobalLock(hData);
        if (pSrc != NULL) {
            text = pSrc;
            GlobalUnlock(hData);
        }
    }

    CloseClipboard();
}
