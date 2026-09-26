#ifndef _ROSE_RML_TEXT_H_
#define _ROSE_RML_TEXT_H_

/**
 * Game text -> RmlUi text.
 *
 * The game's strings ( STL names and descriptions, chat ) are in the Windows
 * ANSI code page -- the classic UI draws them with DrawTextA -- while RmlUi
 * reads UTF-8. Plain ASCII is the same in both and passes through untouched;
 * anything else ( an accented letter, a curly quote ) is converted, or it
 * would draw as garbage.
 */

#include <RmlUi/Core/Types.h>

#include <windows.h>

namespace RoseRmlText {

inline Rml::String
FromGame(const char* pszText) {
    if (pszText == NULL || pszText[0] == '\0')
        return Rml::String();

    bool bAscii = true;
    for (const unsigned char* p = (const unsigned char*)pszText; *p; ++p) {
        if (*p >= 0x80) {
            bAscii = false;
            break;
        }
    }
    if (bAscii)
        return Rml::String(pszText);

    const int iWide = MultiByteToWideChar(CP_ACP, 0, pszText, -1, NULL, 0);
    if (iWide <= 0)
        return Rml::String(pszText);
    std::wstring wide((size_t)iWide, L'\0');
    MultiByteToWideChar(CP_ACP, 0, pszText, -1, &wide[0], iWide);

    const int iUtf8 = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, NULL, 0, NULL, NULL);
    if (iUtf8 <= 0)
        return Rml::String(pszText);
    Rml::String out((size_t)iUtf8, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &out[0], iUtf8, NULL, NULL);
    out.resize((size_t)iUtf8 - 1); /// drop the terminator the count included
    return out;
}

/// Text for RML markup: the markup characters escaped ( names built into a
/// data-rml string ). UTF-8 in, UTF-8 out.
inline Rml::String
Escape(const Rml::String& strText) {
    Rml::String out;
    out.reserve(strText.size() + 8);
    for (size_t i = 0; i < strText.size(); ++i) {
        switch (strText[i]) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += strText[i]; break;
        }
    }
    return out;
}

} // namespace RoseRmlText

#endif /// _ROSE_RML_TEXT_H_
