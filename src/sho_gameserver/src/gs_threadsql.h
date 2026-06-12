#pragma once

#include "CSqlTHREAD.h"

#pragma pack(push, 1)
struct tagBasicETC {
    tPOINTF m_PosSTART;
    BYTE m_btCharRACE;
    short m_nZoneNO;

    tPOINTF m_PosREVIVE;
    short m_nReviveZoneNO;

    tagPartITEM m_PartITEM[MAX_BODY_PART];
    tagPartITEM m_RideITEM[MAX_RIDING_PART];
    tagPartITEM costume[MAX_BODY_PART];

    BYTE m_btCharSlotNO;

    void Init() {
        m_btCharSlotNO = 0;
        m_PosSTART.x = 0;
        m_PosSTART.y = 0;
        m_nZoneNO = -1;

        ::ZeroMemory(m_PartITEM, sizeof(tagPartITEM) * MAX_BODY_PART);
        ::ZeroMemory(m_RideITEM, sizeof(tagPartITEM) * MAX_RIDING_PART);
        ::ZeroMemory(costume, sizeof(tagPartITEM) * MAX_BODY_PART);
    }
    void SetPartITEM(short nPartIdx, tagITEM& sITEM) {
        m_PartITEM[nPartIdx].m_nItemNo = sITEM.GetItemNO();
        m_PartITEM[nPartIdx].m_nGEM_OP = sITEM.GetGemNO();
        m_PartITEM[nPartIdx].m_cGrade = sITEM.GetGrade();
        m_PartITEM[nPartIdx].m_bHasSocket = sITEM.m_bHasSocket;
    }
};
#pragma pack(pop)

struct tagSqlUSER {
    BYTE m_btLogOutMODE;
    classUSER* m_pUSER;
};

class GS_CThreadSQL: public CSqlTHREAD {
private:
    bool m_bWaiting;

private:
    CCriticalSection m_csUserLIST;
    classDLLIST<tagSqlUSER> m_AddUserLIST;
    classDLLIST<tagSqlUSER> m_RunUserLIST;

    DWORD m_dwCurTIME;

    // inherit virtual function from CSqlTHREAD...
    bool Run_SqlPACKET(tagQueryDATA* pSqlPACKET);

    bool Proc_cli_SELECT_CHAR(tagQueryDATA* pSqlPACKET);
    bool Proc_cli_BANK_LIST_REQ(tagQueryDATA* pSqlPACKET);

    void Execute();

public:
    GS_CThreadSQL();

    void Set_EVENT() { m_pEVENT->SetEvent(); }

    bool Add_SqlPacketWithACCOUNT(classUSER* pUSER, t_PACKET* pPacket);
    bool Add_SqlPacketWithAVATAR(classUSER* pUSER, t_PACKET* pPacket);
    bool Add_BackUpUSER(classUSER* pUSER, BYTE btLogOutMODE = 0);

    bool IsWaiting() { return m_bWaiting; }
    int WaitUserCNT() { return m_AddUserLIST.GetNodeCount(); }

    bool UpdateUserRECORD(classUSER* pUSER);
};
extern GS_CThreadSQL* g_pThreadSQL;
