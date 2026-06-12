
#include <windows.h>

#include "CSqlTHREAD.h"

#include "classUTIL.h"

#include "rose/common/log.h"
#include "rose/common/server_config.h"
#include "rose/network/packet.h"

#include <queue>

CSqlTHREAD::CSqlTHREAD(bool bCreateSuspended): classTHREAD(bCreateSuspended), m_CS(4000) {
    m_pEVENT = new classEVENT(NULL, false, false, NULL);
}
CSqlTHREAD::~CSqlTHREAD() {
    this->Free();

    SAFE_DELETE(m_pEVENT);
}

void
CSqlTHREAD::Free() {
    this->Terminate();
    do {
        this->m_pEVENT->SetEvent();
    } while (!IsFinished());
}

CDLList<tagQueryDATA>::tagNODE*
CSqlTHREAD::Del_SqlPACKET(CDLList<tagQueryDATA>::tagNODE* pDelNODE) {
    CDLList<tagQueryDATA>::tagNODE* pNextNODE;

    pNextNODE = m_RunPACKET.GetNextNode(pDelNODE);

    SAFE_DELETE_ARRAY(pDelNODE->m_VALUE.m_pDATA);
    m_RunPACKET.DeleteNFree(pDelNODE);

    return pNextNODE;
}

bool
CSqlTHREAD::Add_SqlPACKET(int iTAG, char* szName, BYTE* pDATA, int iDataSize) {
    CDLList<tagQueryDATA>::tagNODE* pNewNODE;

    pNewNODE = new CDLList<tagQueryDATA>::tagNODE;
    if (!pNewNODE) {
        // out of memory !!!
        return false;
    }

    pNewNODE->m_VALUE.m_iTAG = iTAG;
    pNewNODE->m_VALUE.m_Name.Set(szName);
    pNewNODE->m_VALUE.m_iDataLEN = iDataSize;
    pNewNODE->m_VALUE.m_pDATA = new BYTE[iDataSize];
    if (!pNewNODE->m_VALUE.m_pDATA) {
        // out of memory !!!
        SAFE_DELETE(pNewNODE);
        return false;
    }

    ::CopyMemory(pNewNODE->m_VALUE.m_pDATA, pDATA, iDataSize);

    m_CS.Lock();
    m_AddPACKET.AppendNode(pNewNODE);
    m_CS.Unlock();
    m_pEVENT->SetEvent();

    return true;
}

void
CSqlTHREAD::tick() {
    std::lock_guard<std::mutex> lock(this->queue_mutex);
    while (!this->packet_queue.empty()) {
        this->handle_queued_packet(this->packet_queue.front());
        this->packet_queue.pop();
    }
}

void
CSqlTHREAD::queue_packet(int32_t socket_id,
    const Rose::Network::Packet& p) {

    const std::lock_guard<std::mutex> lock(this->queue_mutex);
    this->packet_queue.push({socket_id, p});
    m_pEVENT->SetEvent();
}
