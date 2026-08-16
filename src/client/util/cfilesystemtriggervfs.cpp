#include "stdafx.h"

#include "CFileSystemTriggerVFS.h"
#include <assert.h>

CFileSystemTriggerVFS::CFileSystemTriggerVFS(void): m_pFileHandle(NULL), m_hVFile(NULL) {
    m_pData = NULL;
    m_iSize = 0;

    m_pReadBuf = NULL;
    m_lBufStart = -1;
    m_iBufValid = 0;
    m_lLogicalPos = 0;
};

CFileSystemTriggerVFS::~CFileSystemTriggerVFS(void) {
    delete[] m_pReadBuf;
    m_pReadBuf = NULL;
}

void
CFileSystemTriggerVFS::InvalidateReadBuffer() {
    m_lBufStart = -1;
    m_iBufValid = 0;
}

long
CFileSystemTriggerVFS::ClampToFile(long lPos) {
    if (lPos < 0) {
        return 0;
    }
    const long lSize = (long)::vfgetsize(m_pFileHandle);
    return (lPos > lSize) ? lSize : lPos;
}

bool
CFileSystemTriggerVFS::RefillReadBuffer() {
    if (m_pReadBuf == NULL) {
        m_pReadBuf = new unsigned char[kReadBufSize];
        if (m_pReadBuf == NULL) {
            return false;
        }
    }

    ::vfseek(m_pFileHandle, m_lLogicalPos, SEEK_SET);
    const int iGot = (int)::vfread(m_pReadBuf, 1, kReadBufSize, m_pFileHandle);
    if (iGot <= 0) {
        InvalidateReadBuffer();
        return false;
    }

    m_lBufStart = m_lLogicalPos;
    m_iBufValid = iGot;
    return true;
}

bool
CFileSystemTriggerVFS::OpenFile(const char* fname, int iMode) {
    if (m_hVFile == NULL)
        return false;

    VFileHandle* pFileHandle = ::VOpenFile(fname, m_hVFile);

    /// File open error
    if (pFileHandle == NULL) {
        m_strFileName = std::string("");
        return false;
    }

    SetVFSHandle(pFileHandle);

    m_strFileName = std::string(fname);

    if (m_pData != NULL) {
        delete m_pData;
        m_pData = NULL;
    }

    m_iSize = 0;

    /// These objects are pooled and reused across files (CVFSManager), so stale
    /// buffer state from the previous file would be read as this one's content.
    /// VOpenFile starts a fresh handle at offset 0.
    InvalidateReadBuffer();
    m_lLogicalPos = 0;

    return true;
}

void
CFileSystemTriggerVFS::CloseFile() {
    if (m_pFileHandle) {
        ::VCloseFile(m_pFileHandle);
        m_pFileHandle = NULL;
    }

    m_strFileName = std::string("");

    InvalidateReadBuffer();
    m_lLogicalPos = 0;
}

void
CFileSystemTriggerVFS::ReleaseData() {
    if (m_pData != NULL) {
        delete[] m_pData;
        m_pData = NULL;
    }
}

////////////////////////////////////////////////////////////
// Overload function
int
CFileSystemTriggerVFS::Read(void* lpBuf, unsigned int nCount) {
    assert(lpBuf && "Read failed from FileStream, output buffer is null");
    assert(m_pFileHandle && "Read failed from FileStream, file description is null");
    if (m_pFileHandle == NULL || lpBuf == NULL) {
        return 0;
    }

    /// Zero-fill up front, exactly as the unbuffered version did: a short read
    /// at EOF must leave the tail of the caller's buffer zeroed, and several
    /// parsers rely on that for name buffers.
    memset(lpBuf, 0, sizeof(char) * nCount);

    if (nCount == 0) {
        return 0;
    }

    unsigned char* pDst = (unsigned char*)lpBuf;

    /// Reads at or above the buffer size would gain nothing from staging and
    /// would evict a useful buffer, so they go straight through.
    if (nCount >= (unsigned int)kReadBufSize) {
        InvalidateReadBuffer();
        ::vfseek(m_pFileHandle, m_lLogicalPos, SEEK_SET);
        const int iGot = (int)::vfread(pDst, 1, nCount, m_pFileHandle);
        if (iGot > 0) {
            m_lLogicalPos += iGot;
        }
        return iGot;
    }

    unsigned int nRemaining = nCount;
    unsigned int nTotal = 0;

    while (nRemaining > 0) {
        long lOffInBuf = (m_lBufStart < 0) ? -1 : (m_lLogicalPos - m_lBufStart);

        if (lOffInBuf < 0 || lOffInBuf >= (long)m_iBufValid) {
            if (!RefillReadBuffer()) {
                break; // EOF, or allocation failure -- short read, tail stays zeroed
            }
            lOffInBuf = m_lLogicalPos - m_lBufStart;
            if (lOffInBuf < 0 || lOffInBuf >= (long)m_iBufValid) {
                break;
            }
        }

        const unsigned int nAvail = (unsigned int)((long)m_iBufValid - lOffInBuf);
        const unsigned int nTake = (nRemaining < nAvail) ? nRemaining : nAvail;

        memcpy(pDst, m_pReadBuf + lOffInBuf, nTake);

        pDst += nTake;
        nRemaining -= nTake;
        nTotal += nTake;
        m_lLogicalPos += nTake;
    }

    return (int)nTotal;
}

void
CFileSystemTriggerVFS::Write(const void* lpBuf, unsigned int nCount) {
    return;
    /*
    assert( lpBuf && "Write failed from FileStream, input buffer is null" );
    assert( m_pFileHandle && "Write failed from FileStream, file description is null" );

    if( m_pFileHandle != NULL && lpBuf != NULL )
        ::vfwrite( lpBuf, 1, nCount, m_pFileHandle );
    */
}

/// Seeks move the logical position only. The read buffer is deliberately NOT
/// invalidated: Read() re-checks whether the new position falls inside the
/// buffered window, so a seek that stays within it costs nothing. That is the
/// common case in the .IFO lump walk, which does Tell() -> Seek(lump offset) ->
/// read -> Seek(back) for every lump.
///
/// vfseek clamps to the file bounds and reports success even for an out-of-range
/// request, so the clamp is mirrored here rather than failing.
bool
CFileSystemTriggerVFS::Seek(long lOff, unsigned int nFrom) {
    assert(m_pFileHandle && "Seek failed from FileStream, file description is null");
    if (m_pFileHandle == NULL)
        return false;

    switch (nFrom) {
        case FILE_POS_SET:
            m_lLogicalPos = ClampToFile(lOff);
            break;
        case FILE_POS_CUR:
            m_lLogicalPos = ClampToFile(m_lLogicalPos + lOff);
            break;
        case FILE_POS_END:
            m_lLogicalPos = ClampToFile((long)::vfgetsize(m_pFileHandle) + lOff);
            break;
        default:
            return false; // unknown origin: matches the old iResult == 1 path
    }

    return true;
}

long
CFileSystemTriggerVFS::Tell() {
    /// Must be the logical position, not vftell(): after a refill the underlying
    /// handle sits up to kReadBufSize bytes ahead of where the caller thinks it
    /// is, and the .IFO lump walk seeks back to values it got from here.
    return m_lLogicalPos;
}

bool
CFileSystemTriggerVFS::IsEOF() {
    if (m_pFileHandle == NULL) {
        return true;
    }
    return m_lLogicalPos >= (long)::vfgetsize(m_pFileHandle);
}

int
CFileSystemTriggerVFS::ReadStringByNullLength() {
    assert(
        (m_pFileHandle != NULL) && "Read string failed from FileStream, file description is null");

    if (m_pFileHandle == NULL)
        return 0;

    long lFilePtr = this->Tell();

    char cOneChar;
    Read(&cOneChar, sizeof(char));

    int j = 0;
    while (cOneChar != '\0') {
        j++;
        Read(&cOneChar, sizeof(char));
    }

    this->Seek(lFilePtr, FILE_POS_SET);

    return j;
}

int
CFileSystemTriggerVFS::ReadStringByNull(char* lpBuf) {
    assert((lpBuf != NULL) && "Read string failed from FileStream, Output buffer is null");
    assert(
        (m_pFileHandle != NULL) && "Read string failed from FileStream, file description is null");

    char cOneChar;
    Read(&cOneChar, sizeof(char));

    int j = 0;
    while (cOneChar != '\0') {
        lpBuf[j++] = cOneChar;
        Read(&cOneChar, sizeof(char));
    }
    lpBuf[j] = '\0';

    return j;
}

void
CFileSystemTriggerVFS::WriteStringByNull(const char* pStr) {
    assert((pStr != NULL) && "Write string to FileStream failed , Input string is null");
    assert(m_pFileHandle && "Write string to FileStream failed, file description is null");

    if (pStr == NULL)
        return;

    int j = 0;
    while (pStr[j] != '\0') {
        Write(&(pStr[j]), sizeof(char));
        j++;
    }
    Write(&(pStr[j]), sizeof(char));
}

int
CFileSystemTriggerVFS::ReadPascalStringLength() {
    assert(m_pFileHandle && "Write string to FileStream failed, file description is null");

    if (m_pFileHandle == NULL)
        return 0;

    long lFilePtr = this->Tell();

    int iLength;
    BYTE btLength;
    ReadByte(&btLength);

    /// 한바이트를 사용하는가? 두바이트를 사용하는가?

    /// 두바이트를 사용한다.
    if (btLength & 0x80) {
        BYTE btSecondByte = 0;
        ReadByte(&btSecondByte);

        int iSecondLength = btSecondByte;
        int iFirstLength = btLength;

        iLength = (iSecondLength << 7) | (iFirstLength - 0x80);
    } else {
        iLength = btLength;
    }

    this->Seek(lFilePtr, FILE_POS_SET);

    return iLength;
}

int
CFileSystemTriggerVFS::ReadPascalString(char* lpBuf, int iBufferLength) {
    assert((lpBuf != NULL) && "Write string to FileStream failed , Input string is null");
    assert(m_pFileHandle && "Write string to FileStream failed, file description is null");

    if (lpBuf == NULL)
        return 0;

    int iLength;
    BYTE btLength;
    ReadByte(&btLength);

    /// 한바이트를 사용하는가? 두바이트를 사용하는가?

    /// 두바이트를 사용한다.
    if (btLength & 0x80) {
        BYTE btSecondByte = 0;
        ReadByte(&btSecondByte);

        int iSecondLength = btSecondByte;
        int iFirstLength = btLength;

        iLength = (iSecondLength << 7) | (iFirstLength - 0x80);
    } else {
        iLength = btLength;
    }

    if (iLength > iBufferLength) {
        assert(
            0 && "Input buffer is not enough to fill data that readed from file( Pascal string )");
        return 0;
    }

    Read(lpBuf, iLength);
    lpBuf[iLength] = '\0';

    return iLength;
}

void
CFileSystemTriggerVFS::WritePascalString(const char* pStr) {
    assert((pStr != NULL) && "Write string to FileStream failed , Input string is null");
    assert(m_pFileHandle && "Write string to FileStream failed, file description is null");

    if (pStr == NULL)
        return;

    short nLength = strlen(pStr);

    /// 두바이트로 길이를 표현한다.
    if (nLength > 0x7F) {
        BYTE btFirstLength = (nLength & 0x1111111) | 0x10000000;
        WriteByte(&btFirstLength, "First length");

        BYTE btSecondLength = nLength >> 7;
        WriteByte(&btSecondLength, "Second Length");
    }

    Write(pStr, nLength);
}

////////////////////////////////////////////////////////////

bool
CFileSystemTriggerVFS::ReadToMemory() {
    if (m_pFileHandle == NULL) {
        m_strFileName = std::string("");
        return false;
    }

    ReleaseData();

    m_iSize = GetSize();
    if (m_iSize == 0) {
        CloseFile();
        return false;
    }

    m_pData = new unsigned char[m_iSize + 1]; // so 0 size file will be saved
    memset(m_pData, 0, m_iSize + 1);
    Read(m_pData, m_iSize);

    return true;
}

int
CFileSystemTriggerVFS::GetSize() {
    if (m_pFileHandle == NULL || m_strFileName.empty())
        return 0;

    if (m_iSize)
        return m_iSize;

    return ::vfgetsize(m_pFileHandle);
}

bool
CFileSystemTriggerVFS::IsExist(const char* pFileName) {
    return ::VFileExists(m_hVFile, pFileName);
}

///////////////////////////////////////////////////////////////////////

// Specific read method
int
CFileSystemTriggerVFS::ReadFloat(float* pValue) {
    return Read(pValue, sizeof(float));
}

int
CFileSystemTriggerVFS::ReadFloat2(float* lpBuf) {
    return Read(lpBuf, sizeof(float) * 2);
}

int
CFileSystemTriggerVFS::ReadFloat2(float* x, float* y) {
    int iSize = 0;
    iSize += Read(x, sizeof(float));
    iSize += Read(y, sizeof(float));

    return iSize;
}

int
CFileSystemTriggerVFS::ReadFloat3(float* lpBuf) {
    return Read(lpBuf, sizeof(float) * 3);
}

int
CFileSystemTriggerVFS::ReadFloat3(float* x, float* y, float* z) {
    int iSize = 0;
    iSize += Read(x, sizeof(float));
    iSize += Read(y, sizeof(float));
    iSize += Read(z, sizeof(float));

    return iSize;
}

int
CFileSystemTriggerVFS::ReadFloat4(float* lpBuf) {
    return Read(lpBuf, sizeof(float) * 4);
}

int
CFileSystemTriggerVFS::ReadFloat4(float* x, float* y, float* z, float* w) {
    int iSize = 0;
    iSize += Read(x, sizeof(float));
    iSize += Read(y, sizeof(float));
    iSize += Read(z, sizeof(float));
    iSize += Read(w, sizeof(float));

    return iSize;
}

int
CFileSystemTriggerVFS::ReadChar(char* pValue) {
    return Read(pValue, sizeof(char));
}

int
CFileSystemTriggerVFS::ReadByte(unsigned char* pValue) {
    return Read(pValue, sizeof(unsigned char));
}

int
CFileSystemTriggerVFS::ReadBool(bool* pValue) {
    return Read(pValue, sizeof(bool));
}

int
CFileSystemTriggerVFS::ReadInt16(short* pValue) {
    return Read(pValue, sizeof(short));
}

int
CFileSystemTriggerVFS::ReadInt32(int* pValue) {
    return Read(pValue, sizeof(int));
}

int
CFileSystemTriggerVFS::ReadInt64(__int64* pValue) {
    return Read(pValue, sizeof(__int64));
}

int
CFileSystemTriggerVFS::ReadUInt16(unsigned short* pValue) {
    return Read(pValue, sizeof(unsigned short));
}

int
CFileSystemTriggerVFS::ReadUInt32(unsigned int* pValue) {
    return Read(pValue, sizeof(unsigned int));
}

int
CFileSystemTriggerVFS::ReadUInt64(unsigned __int64* pValue) {
    return Read(pValue, sizeof(unsigned __int64));
}

// Specific write method
void
CFileSystemTriggerVFS::WriteFloat(float* pValue, const char* strValueName) {
    Write(pValue, sizeof(float));
}

void
CFileSystemTriggerVFS::WriteFloat2(float* lpBuf, const char* strValueName) {
    Write(lpBuf, sizeof(float) * 2);
}

void
CFileSystemTriggerVFS::WriteFloat2(float* x, float* y, const char* strValueName) {
    Write(x, sizeof(float));
    Write(y, sizeof(float));
}

void
CFileSystemTriggerVFS::WriteFloat3(float* lpBuf, const char* strValueName) {
    Write(lpBuf, sizeof(float) * 3);
}

void
CFileSystemTriggerVFS::WriteFloat3(float* x, float* y, float* z, const char* strValueName) {
    Write(x, sizeof(float));
    Write(y, sizeof(float));
    Write(z, sizeof(float));
}

void
CFileSystemTriggerVFS::WriteFloat4(float* lpBuf, const char* strValueName) {
    Write(lpBuf, sizeof(float) * 4);
}

void
CFileSystemTriggerVFS::WriteFloat4(float* x,
    float* y,
    float* z,
    float* w,
    const char* strValueName) {
    Write(x, sizeof(float));
    Write(y, sizeof(float));
    Write(z, sizeof(float));
    Write(w, sizeof(float));
}

void
CFileSystemTriggerVFS::WriteChar(char* pValue, const char* strValueName) {
    Write(pValue, sizeof(char));
}

void
CFileSystemTriggerVFS::WriteByte(unsigned char* pValue, const char* strValueName) {
    Write(pValue, sizeof(unsigned char));
}

void
CFileSystemTriggerVFS::WriteBool(bool* pValue, const char* strValueName) {
    Write(pValue, sizeof(bool));
}

void
CFileSystemTriggerVFS::WriteInt16(short* pValue, const char* strValueName) {
    Write(pValue, sizeof(short));
}

void
CFileSystemTriggerVFS::WriteInt32(int* pValue, const char* strValueName) {
    Write(pValue, sizeof(int));
}

void
CFileSystemTriggerVFS::WriteInt64(__int64* pValue, const char* strValueName) {
    Write(pValue, sizeof(__int64));
}

void
CFileSystemTriggerVFS::WriteUInt16(unsigned short* pValue, const char* strValueName) {
    Write(pValue, sizeof(unsigned short));
}

void
CFileSystemTriggerVFS::WriteUInt32(unsigned int* pValue, const char* strValueName) {
    Write(pValue, sizeof(unsigned int));
}

void
CFileSystemTriggerVFS::WriteUInt64(unsigned __int64* pValue, const char* strValueName) {
    Write(pValue, sizeof(unsigned __int64));
}
