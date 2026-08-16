#ifndef _CFileSystemTriggerVFS_
#define _CFileSystemTriggerVFS_

#include "CFileSystem.h"
#include "triggervfs/triggervfs.h"

class CFileSystemTriggerVFS: public CFileSystem {
private:
    VFileHandle* m_pFileHandle;
    VHANDLE m_hVFile;

    std::string m_strFileName;
    unsigned char* m_pData;

    int m_iSize;

    /// Sequential read-ahead buffer.
    ///
    /// Every typed reader on this class (ReadFloat, ReadInt32, ReadByte, ...)
    /// funnels into Read(), and Read() used to issue one ::vfread per call --
    /// which is a memset plus an fseek+fread round trip through the CRT, lock
    /// included, for as little as one byte. The terrain loader reads scalar by
    /// scalar: a 19 KB .HIM heightfield is ~4,900 of those calls, measured at
    /// ~650 ns each, i.e. 3.2 ms of pure call overhead per map chunk. .TIL and
    /// the lightmap .lit files have the same shape.
    ///
    /// Buffering here rather than at each call site fixes every reader at once
    /// and needs no change to any parser. m_lLogicalPos -- not the underlying
    /// handle -- is the authoritative file position; Read/Seek/Tell/IsEOF all go
    /// through it, and the underlying handle is only repositioned on a refill.
    /// 32 KB covers every map file in a single refill.
    enum { kReadBufSize = 32 * 1024 };
    unsigned char* m_pReadBuf; ///< lazily allocated on first buffered read
    long m_lBufStart; ///< file offset of m_pReadBuf[0]; -1 when invalid
    int m_iBufValid; ///< valid bytes in m_pReadBuf
    long m_lLogicalPos; ///< authoritative position, what Tell() reports

    void InvalidateReadBuffer();
    bool RefillReadBuffer();
    /// vfseek clamps to [0, size] and reports success; mirror that so Tell()
    /// after an over-seek keeps matching the old behaviour.
    long ClampToFile(long lPos);

public:
    void SetVFS(VHANDLE hVFile) { m_hVFile = hVFile; };

public:
    CFileSystemTriggerVFS(void);
    ~CFileSystemTriggerVFS(void);

    virtual bool OpenFile(const char* pFileName, int iOpenType = OPEN_READ_BIN);
    virtual void CloseFile();

    virtual bool ReadToMemory();
    virtual void ReleaseData();
    virtual unsigned char* GetData() { return m_pData; };
    ;

    virtual int GetSize();
    virtual bool IsExist(const char* pFileName);

private:
    virtual void SetVFSHandle(VFileHandle* pFileHandle) { m_pFileHandle = pFileHandle; };

public:
    virtual int Read(void* lpBuf, unsigned int nCount);
    virtual void Write(const void* lpBuf, unsigned int nCount);
    virtual bool Seek(long lOff, unsigned int nFrom);
    virtual long Tell();
    virtual bool IsEOF();

    virtual int ReadStringByNullLength();
    virtual int ReadStringByNull(char* lpBuf);
    virtual void WriteStringByNull(const char* pStr);

    virtual int ReadPascalStringLength();
    virtual int ReadPascalString(char* lpBuf, int iBufferLength);
    virtual void WritePascalString(const char* pStr);

    // Specific read method
    virtual int ReadFloat(float* pValue);
    virtual int ReadFloat2(float* lpBuf);
    virtual int ReadFloat2(float* x, float* y);
    virtual int ReadFloat3(float* lpBuf);
    virtual int ReadFloat3(float* x, float* y, float* z);
    virtual int ReadFloat4(float* lpBuf);
    virtual int ReadFloat4(float* x, float* y, float* z, float* w);

    virtual int ReadChar(char* pValue);
    virtual int ReadByte(unsigned char* pValue);
    virtual int ReadBool(bool* pValue);

    virtual int ReadInt16(short* pValue);
    virtual int ReadInt32(int* pValue);
    virtual int ReadInt64(__int64* pValue);

    virtual int ReadUInt16(unsigned short* pValue);
    virtual int ReadUInt32(unsigned int* pValue);
    virtual int ReadUInt64(unsigned __int64* pValue);

    // Specific write method
    virtual void WriteFloat(float* pValue, const char* strValueName = NULL);
    virtual void WriteFloat2(float* lpBuf, const char* strValueName = NULL);
    virtual void WriteFloat2(float* x, float* y, const char* strValueName = NULL);
    virtual void WriteFloat3(float* lpBuf, const char* strValueName = NULL);
    virtual void WriteFloat3(float* x, float* y, float* z, const char* strValueName = NULL);
    virtual void WriteFloat4(float* lpBuf, const char* strValueName = NULL);
    virtual void
    WriteFloat4(float* x, float* y, float* z, float* w, const char* strValueName = NULL);

    virtual void WriteChar(char* pValue, const char* strValueName = NULL);
    virtual void WriteByte(unsigned char* pByte, const char* strValueName = NULL);
    virtual void WriteBool(bool* pValue, const char* strValueName = NULL);

    virtual void WriteInt16(short* pValue, const char* strValueName = NULL);
    virtual void WriteInt32(int* pValue, const char* strValueName = NULL);
    virtual void WriteInt64(__int64* pValue, const char* strValueName = NULL);

    virtual void WriteUInt16(unsigned short* pValue, const char* strValueName = NULL);
    virtual void WriteUInt32(unsigned int* pValue, const char* strValueName = NULL);
    virtual void WriteUInt64(unsigned __int64* pValue, const char* strValueName = NULL);
};

#endif //_CFileSystemTriggerVFS_