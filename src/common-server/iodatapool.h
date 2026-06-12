/*
    $Header: /7HeartsOnline/LIB_Server/ioDataPOOL.h 1     04-03-25 11:20a Icarus $
*/
#ifndef __IODATA_POOL_H
#define __IODATA_POOL_H

#include "DLLIST.h"
#include "LIB_Util.h"
#include "classPACKET.h"

#include "CDataPOOL.h"

enum class IOMode {
    Read,
    Write,
};

struct tagIO_DATA {
    /// Overlapped data from the IO socket
    OVERLAPPED overlapped;

    /// The current operation for this data (read/write)
    IOMode mode;

    /// Bytes read from/sent to the socket
    uint32_t bytes;

    /// Socket data as a packet
    classPACKET packet;

    // TODO: Remove these node references
    classDLLNODE<tagIO_DATA>* node;
};

typedef classDLLNODE<tagIO_DATA> IODATANODE;
typedef classDLLNODE<tagIO_DATA>* LPIODATANODE;

class CPoolSENDIO: public CDataPOOL<IODATANODE> {
    DECLARE_INSTANCE2(CPoolSENDIO)
public:
    static CPoolSENDIO* Instance(UINT uiInitDataCNT, UINT uiIncDataCNT) {
        if (!m_pCPoolSENDIO) {
            m_pCPoolSENDIO = new CPoolSENDIO(uiInitDataCNT, uiIncDataCNT);
        }
        return m_pCPoolSENDIO;
    }

private:
    CPoolSENDIO(UINT uiInitDataCNT, UINT uiIncDataCNT);

public:
    inline void InitData(LPIODATANODE pData) {
        ::ZeroMemory(&pData->DATA.overlapped, sizeof(OVERLAPPED));
        pData->DATA.bytes = 0;

        pData->DATA.node = pData;
        pData->DATA.mode = IOMode::Write;
    }
};

inline classPACKET*
Packet_AllocOnly() {
    return new classPACKET();
}

inline classPACKET*
Packet_AllocNLock() {
    return new classPACKET();
}

inline void
Packet_ReleaseNUnlock(classPACKET* pCPacket) {
    delete pCPacket;
}

inline void
Packet_DecRefCount(classPACKET* pCPacket) {
    if (pCPacket <= 0) {
        delete pCPacket;
    }
}

#endif
