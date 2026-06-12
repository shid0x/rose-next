
#include <crtdbg.h>
#include <tchar.h>
#include <windows.h>

#include "classUTIL.h"
#include "ioDataPOOL.h"
#include "rose/common/log.h"

char*
classPACKET::GetStringPtr(short* pOffset) {
    char* pStr;

    if (*pOffset >= this->m_HEADER.m_nSize) {
        return NULL;
    }

    pStr = (char*)&m_pDATA[*pOffset];
    *pOffset += (static_cast<short>(strlen(pStr)) + 1);

    return pStr;
}

BYTE*
classPACKET::GetDataPtr(short* pOffset, short nSize) {
    BYTE* pData;

    if (*pOffset + nSize > this->m_HEADER.m_nSize) {
        return NULL;
    }

    pData = (BYTE*)&m_pDATA[*pOffset];
    *pOffset += nSize;

    return (BYTE*)pData;
}

bool
classPACKET::AppendString(char* pStr) {
    short nLen, nSize;

    nLen = (pStr) ? static_cast<short>(strlen(pStr)) + 1 : 1;
    nSize = this->m_HEADER.m_nSize;

    if (nSize + nLen >= MAX_PACKET_SIZE) {
        OutputDebugString("Error !! Packet_AppendString.. Too Big Packet\n");
        return false;
    }

    if (nLen > 1) {
        ::CopyMemory(m_pDATA + nSize, pStr, nLen - 1);
    }

    m_pDATA[nSize + nLen - 1] = '\0';
    this->m_HEADER.m_nSize += nLen;

    return true;
}

bool
classPACKET::AppendData(void* pData, short nLen) {
    short nSize;

    if (!pData || !nLen) {
        OutputDebugString("Error !! Packet_AppendData.. Source Data is NULL or Length is 0 \n");
        return false;
    }

    nSize = this->m_HEADER.m_nSize;
    if (nSize + nLen >= MAX_PACKET_SIZE) {
        OutputDebugString("Error !! Packet_AppendData.. Too Big Packet\n");
        return false;
    }

    ::CopyMemory(m_pDATA + nSize, pData, nLen);

    this->m_HEADER.m_nSize += nLen;

    return true;
}
