#include "stdafx.h"

#include "string_util.h"
#include "util/localizing.h"
#include "misc/gameutil.h"

namespace Rose {
namespace Util {

int
MulityByte2WideString(const char* pszMultyByte, std::wstring& wstrWide) {
    assert(pszMultyByte);
    if (pszMultyByte == NULL)
        return 0;

    int iStrLen = strlen(pszMultyByte);
    wchar_t* pWideString = new wchar_t[iStrLen + 1];
    ZeroMemory(pWideString, sizeof(wchar_t) * (iStrLen + 1));

    unsigned uiCodePage = CLocalizing::GetSingleton().GetCurrentCodePageNO();

    int iRet =
        MultiByteToWideChar(CP_ACP, 0, pszMultyByte, iStrLen, (LPWSTR)pWideString, iStrLen + 1);

    wstrWide.assign(pWideString);

    delete[] pWideString;
    return iRet;
}

int
MulityByte2WideString(const char* pszMultyByte, wchar_t* pwszWide, int iBufSize) {
    assert(pszMultyByte);
    if (pszMultyByte == NULL)
        return 0;

    ZeroMemory(pwszWide, sizeof(wchar_t) * iBufSize);

    unsigned uiCodePage = CLocalizing::GetSingleton().GetCurrentCodePageNO();

    int iStrLen = strlen(pszMultyByte);

    int iRet = MultiByteToWideChar(CP_ACP, 0, pszMultyByte, iStrLen, (LPWSTR)pwszWide, iBufSize);

    return iRet;
}

int
Wide2MultiByteString(std::wstring& wstrWide, std::string& strMultyByte) {
    int iBufSize = wstrWide.length() * 3 + 1;

    char* pszBuf = new char[iBufSize];

    ZeroMemory(pszBuf, sizeof(char) * iBufSize);

    unsigned uiCodePage = CLocalizing::GetSingleton().GetCurrentCodePageNO();

    BOOL bChangedDefaultChar;
    int iRet = WideCharToMultiByte(CP_ACP,
        0,
        (LPCWSTR)wstrWide.c_str(),
        wstrWide.length(),
        pszBuf,
        iBufSize,
        ".",
        &bChangedDefaultChar);

    strMultyByte = pszBuf;

    delete[] pszBuf;
    return iRet;
}

} // Namespace Util
} // Namespace Rose