#pragma once

#include "classSYNCOBJ.h"
#include "classTHREAD.h"
#include "util/classstr.h"
#include "CDLList.h"
#include "PacketHEADER.h"

#include "rose/database/database.h"
#include "rose/network/packet.h"

#include <queue>
#include <mutex>
#include <string>

struct tagQueryDATA {
    int m_iTAG;
    CStrVAR m_Name;
    int m_iDataLEN;
    union {
        BYTE* m_pDATA;
        t_PACKETHEADER* m_pPacket;
    };
};

/// A network packet that needs to be processed by the sql thread
struct QueuedPacket {
    int32_t socket_id;
    Rose::Network::Packet packet;
};

class CSqlTHREAD: public classTHREAD {
public:
    Rose::Database::Client db;

    std::mutex queue_mutex;
    std::queue<QueuedPacket> packet_queue;

protected:
    classEVENT* m_pEVENT;
    CCriticalSection m_CS;

    CDLList<tagQueryDATA> m_RunPACKET;
    CDLList<tagQueryDATA> m_AddPACKET;

    CDLList<tagQueryDATA>::tagNODE* Del_SqlPACKET(CDLList<tagQueryDATA>::tagNODE* pDelNODE);
    virtual bool Run_SqlPACKET(tagQueryDATA* pQueryDATA) = 0;

public:
    CSqlTHREAD(bool bCreateSuspended);
    virtual ~CSqlTHREAD();

    //	virtual void Execute() = 0;

    void Free();

    bool Add_SqlPACKET(int iTAG, char* szName, BYTE* pDATA, int iDataSize);

    void tick();
    void queue_packet(int32_t socket_id,
        const Rose::Network::Packet& p);

    virtual void handle_queued_packet(QueuedPacket& p){};
};
