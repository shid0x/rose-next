#ifndef STRING_UTIL_H
#define STRING_UTIL_H
#pragma once

namespace Rose {
namespace Util {

int MulityByte2WideString(const char* pszMultyByte, std::wstring& wstrWide);
int MulityByte2WideString(const char* pszMultyByte, wchar_t* pwszWide, int iBufSize);

int Wide2MultiByteString(std::wstring& wstrWide, std::string& strMultyByte);

} // Namespace Util
} // Namespace Rose

#endif // STRING_UTIL_H