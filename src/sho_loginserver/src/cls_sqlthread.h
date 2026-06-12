#pragma once

#include "CLS_Account.h"
#include "CSqlTHREAD.h"

class CLS_SqlTHREAD: public CSqlTHREAD {
public:
    void Set_EVENT() { m_pEVENT->SetEvent(); }
    bool Add_LogOutUSER(CLS_Account* pCAccount);

    DECLARE_INSTANCE(CLS_SqlTHREAD)

public:
    int minimum_access_level;

public:
    void handle_queued_packet(QueuedPacket& p);

    bool handle_login_req(QueuedPacket& p);

private:
    CCriticalSection m_csUserLIST;
    classDLLIST<CLS_Account*> m_AddUserLIST;
    classDLLIST<CLS_Account*> m_RunUserLIST;

    // inherit virtual function from CSqlTHREAD...
    bool Run_SqlPACKET(tagQueryDATA* pSqlPACKET);

    void Execute();

    CLS_SqlTHREAD();
    ~CLS_SqlTHREAD();
};
extern CLS_SqlTHREAD* g_pThreadSQL;