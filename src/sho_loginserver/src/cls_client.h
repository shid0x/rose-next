#ifndef __CLS_CLIENT_H
#define __CLS_CLIENT_H
#include "SHO_LS_LIB.h"
//-------------------------------------------------------------------------------------------------

#define CLIENT_STEP_CONNECT 0
#define CLIENT_STEP_LOGIN_WAIT 1
#define CLIENT_STEP_LOGIN_REQ 2
#define CLIENT_STEP_LOGEDIN 3
#define CLIENT_STEP_SELECT_GS 4

//-------------------------------------------------------------------------------------------------
#define __VIEW_ACCOUNT

#include "rose/network/packet.h"

class CLS_Client: public iocpSOCKET {
public:
    // New handlers
    bool recv_login_req(Rose::Network::Packet& p);

private:
    // Legacy handlers
    bool Recv_cli_ACCEPT_REQ();
    bool Recv_cli_CHANNEL_LIST_REQ(t_PACKET* pPacket);
    bool Recv_cli_SELECT_SERVER(t_PACKET* pPacket);

    bool Recv_mon_SERVER_LIST_REQ(t_PACKET* pPacket, bool bHideIP);
    bool Recv_mon_SERVER_STATUS_REQ(t_PACKET* pPacket);

public:
    bool Send_srv_ANNOUNCE_TEXT(char* szText);

public:
    CStrVAR m_Account; /// 계정
    DWORD password_buffer[16]; /// 비번
    DWORD m_dwLastLoginTIME; /// 마지막 접속시간

    DWORD m_dwRIGHT; /// 개발자/GM등... 게임내에서 치트또는 접속 권한
    DWORD m_dwPayFLAG; /// 과금 설정된 플레그

    short m_nProcSTEP;
    char m_pJuMinNO[9]; /// 주민번호 앞자리~뒷자리 1번째
    bool m_bFreeServerOnly; /// 프리서버에만 접속 가능하지 여부...

    DWORD m_dwConnTIME; /// 접속시간
    bool m_bMonClient; /// 모니터링 클라이언트인가 ??
    bool m_bAdmClient; /// Raven - Server Admin Tool

    CDLList<CLS_Client*>::tagNODE* m_pConnNODE;

#ifdef __VIEW_ACCOUNT
    void* m_pCliListITEM;
#endif

    CLS_Client();
    ~CLS_Client();

    void Init() {
        m_bFreeServerOnly = false;
        m_iRecvSeqNO = 0;
        m_pConnNODE = NULL;

        m_bMonClient = false;
        m_bAdmClient = false;

        // m_pListITEM = NULL;
        m_nProcSTEP = CLIENT_STEP_CONNECT;
        m_dwRIGHT = 0;
        m_dwPayFLAG = 0;

        m_dwConnTIME = classTIME::GetCurrentAbsSecond();

#ifdef __VIEW_ACCOUNT
        m_pCliListITEM = NULL;
#endif
    }
    void Free() {
        // _ASSERT( 0 == this->m_iSocketIDX );	어떤경운지.... IROSE에서 발생...
        this->Clear_LIST();
    }

    void Set_ACCOUNT(char* username, DWORD* password) {
        m_Account.Set(username);
        ::CopyMemory(this->password_buffer, password, sizeof(DWORD) * 16);
    }

    bool Send_lsv_LOGIN_REPLY(BYTE btResult, int iPayType);

private:
    //----------------------- virtual function inherit from iocpSOCKET
    bool HandlePACKET(t_PACKETHEADER* pPacket);

    int m_iRecvSeqNO;
};

//-------------------------------------------------------------------------------------------------
/// 접속된 전체 클라이언트 리스트
class CLS_ListCLIENT: public IOCPSocketSERVER, public CDataPOOL<CLS_Client> {
public:
    int max_users;
    CCriticalSection m_CS;
    CDLList<CLS_Client*> m_ConnLIST;

public:
    // Worker Thread 갯수 = CPU * 2 + 2
    CLS_ListCLIENT(UINT uiInitDataCNT, UINT uiIncDataCNT);
    ~CLS_ListCLIENT() { ; }

    bool Send_lsv_LOGIN_REPLY(int iSocketIDX, BYTE btResult, int iPayType = 0);

    void Delete_IdleSOCKET() {
        DWORD dwConnTIME = classTIME::GetCurrentAbsSecond();

        CDLList<CLS_Client*>::tagNODE* pNode;
        this->m_CS.Lock();
        pNode = m_ConnLIST.GetHeadNode();
        while (pNode) {
            switch (pNode->m_VALUE->m_nProcSTEP) {
                case CLIENT_STEP_CONNECT:
                    // 접속하자 마자 보내는 CLI_ACCEPT_REQ를 60초 안에 안보낸넘 짤러~~~
                    if (dwConnTIME - pNode->m_VALUE->m_dwConnTIME > 60) {
                        pNode->m_VALUE->CloseSocket();
                    }
                    break;
                case CLIENT_STEP_LOGEDIN: // 로그인 된 계정...
                    if (!pNode->m_VALUE->m_bMonClient
                        && dwConnTIME - pNode->m_VALUE->m_dwConnTIME > 10 * 60) {
                        // 10분동안 게임으로 이동안한 계정 계정...
                        pNode->m_VALUE->CloseSocket();
                        // g_LOG.CS_ODS (LOG_NORMAL, "10 Min TimeOut[%s]\n",
                        // pNode->m_VALUE->Get_IP() );
                    }
                    break;
                default:
                    if (!pNode->m_VALUE->m_bMonClient
                        && dwConnTIME - pNode->m_VALUE->m_dwConnTIME > 5 * 60) {
                        // 5분동안 로그인 안된 계정...
                        pNode->m_VALUE->CloseSocket();
                        // g_LOG.CS_ODS (LOG_NORMAL, "5 Min TimeOut[%s]\n", pNode->m_VALUE->Get_IP()
                        // );
                    }
                    break;
            }
            pNode = pNode->GetNext();
        }
        this->m_CS.Unlock();
    }
    //-------------------------------------------------------------------------------------------------
    /// Inherited from IOCPSocketSERVER
    iocpSOCKET* AllocClientSOCKET() {
        // 메모리할당
        CLS_Client* pSOCKET = this->Pool_Alloc();
        if (pSOCKET) {
            pSOCKET->Init();

            this->m_CS.Lock();
            pSOCKET->m_pConnNODE = m_ConnLIST.AllocNAppend(pSOCKET);
            this->m_CS.Unlock();
        }
        return (iocpSOCKET*)pSOCKET;
    }
    void InitClientSOCKET(iocpSOCKET* pCLIENT);
    void FreeClientSOCKET(iocpSOCKET* pSOCKET) {
        CLS_Client* pClient = (CLS_Client*)pSOCKET;

        this->m_CS.Lock();
        m_ConnLIST.DeleteNFree(pClient->m_pConnNODE);
        this->m_CS.Unlock();

        pClient->Free();
        this->Pool_Free(pClient);
    }
    void ClosedClientSOCKET(iocpSOCKET* pSOCKET) {
        this->FreeClientSOCKET(pSOCKET);
        // 소켓이 삭제됐다.. 알아서 메모리 해제할것...
    }

    bool IsMaxiumUSER() {
        if (this->max_users && this->GetUsedSocketCNT() > this->max_users) {
            return true;
        }

        return false;
    }
};
extern CLS_ListCLIENT* g_pListCLIENT;

//-------------------------------------------------------------------------------------------------
#endif