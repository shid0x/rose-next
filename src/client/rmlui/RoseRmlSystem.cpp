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
