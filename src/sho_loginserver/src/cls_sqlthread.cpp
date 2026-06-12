
#include "stdAFX.h"

#include "CLS_Account.h"
#include "CLS_Client.h"
#include "CLS_Server.h"
#include "CLS_SqlTHREAD.h"
#include "blockLIST.h"

#include "rose/util/sha256.h"

using namespace Rose::Database;
using namespace Rose::Network;
using namespace Rose::Util;

IMPLEMENT_INSTANCE(CLS_SqlTHREAD)

const uint32_t USERNAME_MAX = 30;

CLS_SqlTHREAD::CLS_SqlTHREAD(): CSqlTHREAD(true), minimum_access_level(1) {}

__fastcall CLS_SqlTHREAD::~CLS_SqlTHREAD() {
    this->Free();
}

bool
CLS_SqlTHREAD::Add_LogOutUSER(CLS_Account* pCAccount) {
    m_csUserLIST.Lock();
    m_AddUserLIST.AppendNode(pCAccount->m_pListNODE);
    m_csUserLIST.Unlock();

    m_pEVENT->SetEvent();

    return true;
}

void
CLS_SqlTHREAD::Execute() {
    CDLList<tagQueryDATA>::tagNODE* pSqlNODE;
    classDLLNODE<CLS_Account*>* pUsrNODE;

    while (TRUE) {
        if (!this->Terminated) {
            m_pEVENT->WaitFor(INFINITE);
        } else {
            int iReaminCNT;
            this->m_CS.Lock();
            iReaminCNT = m_AddPACKET.GetNodeCount();
            this->m_CS.Unlock();

            if (iReaminCNT <= 0)
                break;
        }

        this->m_CS.Lock();
        m_RunPACKET.AppendNodeList(&m_AddPACKET);
        m_AddPACKET.Init();
        this->m_CS.Unlock();
        m_pEVENT->ResetEvent();

        for (pSqlNODE = m_RunPACKET.GetHeadNode(); pSqlNODE;) {
            if (Run_SqlPACKET(&pSqlNODE->m_VALUE))
                pSqlNODE = this->Del_SqlPACKET(pSqlNODE);
            else
                pSqlNODE = m_RunPACKET.GetNextNode(pSqlNODE);
        }

        m_csUserLIST.Lock();
        m_RunUserLIST.AppendNodeList(&m_AddUserLIST);
        m_AddUserLIST.Init();
        m_csUserLIST.Unlock();

        for (pUsrNODE = m_RunUserLIST.GetHeadNode(); pUsrNODE;
             pUsrNODE = m_RunUserLIST.GetHeadNode()) {
            m_RunUserLIST.DeleteNode(pUsrNODE->DATA->m_pListNODE);
            // 모든 CLS_Account()는 여기서 삭제된다...
            g_pListJOIN->Delete_ACCOUNT(pUsrNODE->DATA);
        }

        this->tick();
    }
}

bool
CLS_SqlTHREAD::Run_SqlPACKET(tagQueryDATA* pSqlPACKET) {
    return true;
}

void
CLS_SqlTHREAD::handle_queued_packet(QueuedPacket& p) {
    Packets::PacketType packet_type = p.packet.packet_data()->data_type();
    switch (packet_type) {
        case Packets::PacketType::LoginRequest: {
            if (!this->handle_login_req(p)) {
                CLS_Client* pClient = (CLS_Client*)g_pListCLIENT->GetSOCKET(p.socket_id);
                if (pClient) {
                    pClient->m_nProcSTEP = CLIENT_STEP_LOGIN_WAIT;
                }
                return;
            }
        }
    }
}

bool
CLS_SqlTHREAD::handle_login_req(QueuedPacket& p) {
    const Packets::LoginRequest* req = p.packet.packet_data()->data_as_LoginRequest();

    if (!req) {
        return false;
    }

    if (!req->username() || !req->password()) {
        return false;
    }

    if (req->username()->size() == 0 || req->username()->size() > USERNAME_MAX) {
        LOG_DEBUG("Username was string was empty or too long: {}", req->username()->c_str());
        g_pListCLIENT->Send_lsv_LOGIN_REPLY(p.socket_id, RESULT_LOGIN_REPLY_NOT_FOUND_ACCOUNT);
        return false;
    }

    if (req->password()->size() == 0) {
        return false;
    }

    const char* stmt = "SELECT password, salt, access_level FROM account WHERE email=$1";

    QueryResult res = this->db.query(stmt, {req->username()->c_str()});
    if (!res.is_ok()) {
        LOG_ERROR("Failed to query account info for username: ({})", req->username()->c_str());
        LOG_ERROR(this->db.last_error_message());
        g_pListCLIENT->Send_lsv_LOGIN_REPLY(p.socket_id, RESULT_LOGIN_REPLY_FAILED);
        return false;
    }

    if (res.data.size() == 0) {
        g_pListCLIENT->Send_lsv_LOGIN_REPLY(p.socket_id, RESULT_LOGIN_REPLY_NOT_FOUND_ACCOUNT);
        return false;
    }

    std::string password = res.get_string(0, 0);
    std::string salt = res.get_string(0, 1);
    int access_level = res.get_int32(0, 2);

    if (access_level < this->minimum_access_level) {
        g_pListCLIENT->Send_lsv_LOGIN_REPLY(p.socket_id, RESULT_LOGIN_REPLY_NO_RIGHT_TO_CONNECT);
        return false;
    }

    const std::string password_hash = sha256(req->password()->str() + salt);

    if (password != password_hash) {
        g_pListCLIENT->Send_lsv_LOGIN_REPLY(p.socket_id, RESULT_LOGIN_REPLY_INVALID_PASSWORD);
        return false;
    }

    CLS_Account* account = g_pListJOIN->Search_ACCOUNT((char*)req->username()->c_str());
    if (account) {
        DWORD dwCurTime = ::timeGetTime();
        if (dwCurTime - account->m_dwDisconnectTryTIME >= 2 * 60 * 1000) {
            account->m_dwDisconnectTryTIME = dwCurTime;
            if (account->m_pWorldServer) {
                account->m_pWorldServer->Send_str_PACKET(WLS_KICK_ACCOUNT,
                    (char*)req->username()->c_str());
            }
        }
        g_pListCLIENT->Send_lsv_LOGIN_REPLY(p.socket_id, RESULT_LOGIN_REPLY_ALREADY_LOGGEDIN);
        return false;
    }

    CLS_Client* pClient = (CLS_Client*)g_pListCLIENT->GetSOCKET(p.socket_id);
    if (!pClient) {
        return false;
    }

    DWORD current_time = classTIME::GetCurrentAbsSecond();
    if (access_level < this->minimum_access_level && g_pListCLIENT->IsMaxiumUSER()) {
        g_pListCLIENT->Send_lsv_LOGIN_REPLY(p.socket_id, RESULT_LOGIN_REPLY_TOO_MANY_USER);
        return false;
    }

    pClient->Set_ACCOUNT((char*)req->username()->c_str(), (DWORD*)req->password()->c_str());

    // TODO: This should be the last previous login time not the current
    pClient->m_dwLastLoginTIME = current_time;

    // TODO: What the hell is this.
    ::FillMemory(pClient->m_pJuMinNO, 8, '7');
    pClient->m_pJuMinNO[8] = '\0';

    pClient->m_dwRIGHT = access_level;
    if (0 == pClient->m_dwLastLoginTIME) {
        pClient->m_dwLastLoginTIME = current_time;
    }

    DWORD dwPayFlags = (PLAY_FLAG_KOREA_DEFAULT | PLAY_FLAG_EXTRA_STOCK | PLAY_FLAG_EXTRA_CHAR);

    pClient->Send_lsv_LOGIN_REPLY(RESULT_LOGIN_REPLY_OK, dwPayFlags);
    pClient->m_nProcSTEP = CLIENT_STEP_LOGEDIN;
    pClient->m_dwPayFLAG = dwPayFlags;

    return true;
}