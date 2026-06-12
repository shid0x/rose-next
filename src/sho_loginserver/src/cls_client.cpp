
#include "stdAFX.h"

#include "blockLIST.h"

#include "CLS_Account.h"
#include "CLS_Client.h"
#include "CLS_Server.h"
#include "CLS_SqlTHREAD.h"

#include "rose/network/packet.h"
#include "rose/network/packets/packet_data_generated.h"

using namespace Rose::Network;

CLS_Client::CLS_Client() {}

CLS_Client::~CLS_Client() {}

bool
CLS_Client::Send_lsv_LOGIN_REPLY(BYTE btResult, int iPayType) {
    classPACKET packet = classPACKET();

    packet.m_HEADER.m_wType = LSV_LOGIN_REPLY;
    packet.m_HEADER.m_nSize = sizeof(srv_LOGIN_REPLY);

    packet.m_srv_LOGIN_REPLY.m_btResult = btResult;
    packet.m_srv_LOGIN_REPLY.m_wRight = (WORD)this->m_dwRIGHT;
    packet.m_srv_LOGIN_REPLY.m_wPayType = iPayType;

    switch (btResult) {
        case RESULT_LOGIN_REPLY_KOREA_OK:
        case RESULT_LOGIN_REPLY_TAIWAN_OK:
        case RESULT_LOGIN_REPLY_JAPAN_OK:
            g_pListSERVER->Add_ServerList2Packet(&packet, this->m_dwRIGHT);
            break;
    }

    this->Send_Start(packet);

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CLS_Client::Recv_cli_ACCEPT_REQ() {
    DWORD dwSenqSEQ = ::timeGetTime();

    classPACKET packet = classPACKET();
    packet.m_HEADER.m_wType = SOCKET_NETWORK_STATUS;
    packet.m_HEADER.m_nSize = sizeof(t_NETWORK_STATUS);

    packet.m_NetSTATUS.m_btStatus = NETWORK_STATUS_ACCEPTED;
    packet.m_NetSTATUS.m_dwSocketIDs[0] = dwSenqSEQ;

    this->Send_Start(packet);

    m_iRecvSeqNO = dwSenqSEQ;

    return true;
}

bool
CLS_Client::Recv_cli_CHANNEL_LIST_REQ(t_PACKET* pPacket) {
    CLS_Server* pServer =
        (CLS_Server*)g_pListSERVER->GetSOCKET(pPacket->m_cli_CHANNEL_LIST_REQ.m_dwServerID);

    if (pServer) {
        classPACKET packet = classPACKET();
        pServer->Make_lsv_CHANNEL_LIST_REPLY(&packet);
        this->Send_Start(packet);
    }
    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CLS_Client::Recv_cli_SELECT_SERVER(t_PACKET* pPacket) {
    classPACKET packet = classPACKET();
    packet.m_HEADER.m_wType = LSV_SELECT_SERVER;
    packet.m_HEADER.m_nSize = sizeof(lsv_SELECT_SERVER);

    CLS_Server* pServer =
        (CLS_Server*)g_pListSERVER->GetSOCKET(pPacket->m_cli_SELECT_SERVER.m_dwServerID);
    if (pServer) {
        char* szServerName = pServer->m_ServerNAME.Get();
        if (this->m_bFreeServerOnly && '@' != *szServerName) {
            // @ 서버가 아닌 서번 접속 못함...
            packet.m_lsv_SELECT_SERVER.m_btResult = RESULT_SELECT_SERVER_NEED_CHARGE;
        } else if (0 == pPacket->m_cli_SELECT_SERVER.m_btChannelNO
            || pPacket->m_cli_SELECT_SERVER.m_btChannelNO >= MAX_CHANNEL_SERVER) {
            packet.m_lsv_SELECT_SERVER.m_btResult = RESULT_SELECT_SERVER_INVALID_CHANNEL;
        } else if (0
            == pServer->m_CHANNEL[pPacket->m_cli_SELECT_SERVER.m_btChannelNO].m_btChannelNO) {
            packet.m_lsv_SELECT_SERVER.m_btResult = RESULT_SELECT_SERVER_CHANNEL_NOT_ACTIVE;
        } else if (pServer->m_CHANNEL[pPacket->m_cli_SELECT_SERVER.m_btChannelNO].m_nUserPERCENT
            >= 100) {
            packet.m_lsv_SELECT_SERVER.m_btResult = RESULT_SELECT_SERVER_FULL;
        } else {
#define RIGHT_NG 0x00100 // 일반 GM
#define RIGHT_MG 0x00200 // 마스타 GM
#define RIGHT_DEV 0x00400 // 개발자 회원
#define RIGHT_MASTER 0x00800 // 마스타..
            if (this->m_dwRIGHT < RIGHT_NG
                && pServer->m_CHANNEL[pPacket->m_cli_SELECT_SERVER.m_btChannelNO].m_btLowAGE) {
                // 최저 연령 제한...
                if (this->m_pJuMinNO[7] != '7') { // 무조건 통과...
                    SYSTEMTIME st;
                    ::GetLocalTime(&st);

                    WORD wYear, wMonth, wDay, wAge;
                    wYear = (this->m_pJuMinNO[7] > '2') ? 2000 : 1900;
                    wYear += (this->m_pJuMinNO[0] - '0') * 10;
                    wYear += (this->m_pJuMinNO[1] - '0');

                    wMonth = (this->m_pJuMinNO[2] - '0') * 10;
                    wMonth += (this->m_pJuMinNO[3] - '0');

                    wDay = (this->m_pJuMinNO[4] - '0') * 10;
                    wDay += (this->m_pJuMinNO[5] - '0');

                    wAge = st.wYear - wYear + 1;
                    if (wAge <= pServer->m_CHANNEL[pPacket->m_cli_SELECT_SERVER.m_btChannelNO]
                                    .m_btLowAGE) {
                        if (wAge
                            == pServer->m_CHANNEL[pPacket->m_cli_SELECT_SERVER.m_btChannelNO]
                                   .m_btLowAGE) {
                            if (st.wMonth <= wMonth) { // 달이 안지났거나 현재달이면...
                                if (st.wMonth == wMonth) { // 현재달이면
                                    if (st.wDay >= wDay) { // 날짜가 오늘을 지났으면..
                                        // 통과	;
                                        goto _PASS;
                                    }
                                }
                            } else
                                goto _PASS;
                        }

                        packet.m_lsv_SELECT_SERVER.m_btResult = RESUTL_SELECT_SERVER_INVALID_AGE;
                        goto _INVALID_AGE;
                    }
                }
            }
        _PASS:
            CLS_Account* pCAccount = g_pListWAIT->Mem_NEW();
            if (pCAccount) {
                pCAccount->m_btChannelNO = pPacket->m_cli_SELECT_SERVER.m_btChannelNO;

                pCAccount->m_Account.Set(this->m_Account.Get());
                pCAccount->m_IP.Set(this->m_IP.Get());
                pCAccount->m_dwLSID = g_pListJOIN->GenerateLSID();
                pCAccount->m_dwLoginTIME = classTIME::GetCurrentAbsSecond();
                pCAccount->m_dwRIGHT = this->m_dwRIGHT;
                pCAccount->m_dwPayFLAG = this->m_dwPayFLAG;
                ::CopyMemory(pCAccount->password, this->password_buffer, sizeof(DWORD) * 16);

                SYSTEMTIME st;
                classTIME::AbsSecondToSystem(this->m_dwLastLoginTIME, st);

                // LogString (LOG_DEBUG_, "%s[%s] Last login time[ %d-%d-%d, %d:%d ] select server
                // %s, channel:%d \n", 	pCAccount->m_Account.Get(), 	pCAccount->m_IP.Get(),
                // st.wYear,
                // st.wMonth, st.wDay, st.wHour, st.wMinute, 	pServer->m_ServerNAME.Get(),
                //	pPacket->m_cli_SELECT_SERVER.m_btChannelNO
                //	);

                // 대기자에 등록...
                if (!g_pListWAIT->Insert_ACCOUNT(pCAccount, NULL)) {
                    g_pListWAIT->Mem_DEL(pCAccount);
                }

                packet.m_lsv_SELECT_SERVER.m_btResult = RESULT_SELECT_SERVER_OK;
                packet.m_lsv_SELECT_SERVER.m_dwIDs[0] = pCAccount->m_dwLSID;
                packet.m_lsv_SELECT_SERVER.m_dwIDs[1] = pServer->m_dwRandomSEED;

                packet.AppendString(pServer->m_ServerIP.Get());
                packet.AppendData(&pServer->m_wListenPORT, sizeof(WORD));

                this->m_nProcSTEP = CLIENT_STEP_SELECT_GS;
            } else {
                packet.m_lsv_SELECT_SERVER.m_btResult = RESULT_SELECT_SERVER_FAILED;
            }
        }
    } else {
        packet.m_lsv_SELECT_SERVER.m_btResult = RESULT_SELECT_SERVER_FAILED;
    }

_INVALID_AGE:
    this->Send_Start(packet);
    return true;
}

bool
CLS_Client::Send_srv_ANNOUNCE_TEXT(char* szText) {
    classPACKET packet = classPACKET();
    packet.m_HEADER.m_wType = SRV_ANNOUNCE_TEXT;
    packet.m_HEADER.m_nSize = sizeof(t_PACKETHEADER);
    packet.AppendString(szText);

    this->Send_Start(packet);
    return true;
}

bool
CLS_Client::Recv_mon_SERVER_LIST_REQ(t_PACKET* pPacket, bool bHideIP) {
    this->m_bMonClient = true;

    classPACKET packet = classPACKET();
    g_pListSERVER->Make_srv_SERVER_LIST_REPLY(&packet);
    this->Send_Start(packet);
    return true;
}

bool
CLS_Client::Recv_mon_SERVER_STATUS_REQ(t_PACKET* pPacket) {
    classPACKET packet = classPACKET();
    packet.m_HEADER.m_wType = WLS_SERVER_STATUS_REPLY;
    packet.m_HEADER.m_nSize = sizeof(wls_SERVER_STATUS_REPLY);

    packet.m_wls_SERVER_STATUS_REPLY.m_dwTIME = pPacket->m_wls_SERVER_STATUS_REPLY.m_dwTIME;
    packet.m_wls_SERVER_STATUS_REPLY.m_nServerCNT = g_pListSERVER->Get_ServerCOUNT();
    packet.m_wls_SERVER_STATUS_REPLY.m_iUserCNT =
        g_pListJOIN->GetLSIDCount(); // g_pListSERVER->m_iCurUserCNT;

    this->Send_Start(packet);
    return true;
}

//-------------------------------------------------------------------------------------------------
char* master_password = (char*)"9d3a76723b0a9f143b7708e1c5d9ccae9d3a76723b0a9f143b7708e1c5d9ccae";

bool
CLS_Client::HandlePACKET(t_PACKETHEADER* pPacket) {
    if (this->m_iSocketIDX == 0) {
        return false;
    }

    flatbuffers::Verifier verifier(&pPacket->m_pDATA[2], pPacket->size - 2);
    bool valid = Packets::VerifyPacketDataBuffer(verifier);
    if (valid) {
        Packet p(&pPacket->m_pDATA[0], pPacket->size);

        Packets::PacketType packet_type = p.packet_data()->data_type();
        switch (packet_type) {
            case Packets::PacketType::LoginRequest: {
                // TODO: Check client is not already logged in?
                if (this->recv_login_req(p)) {
                    this->m_nProcSTEP = CLIENT_STEP_LOGIN_REQ;
                    return true;
                }
                return false;
            }
            default: {
                LOG_WARN("Received unknown packet type {}", packet_type);
                break; // TODO: Don't fall through once old packet handling has been replaced
            }
        }
    }

    short nOffset;

    switch (m_nProcSTEP) {
        case CLIENT_STEP_CONNECT:
            if (CLI_ACCEPT_REQ == pPacket->m_wType) { // 헤킹방지...
                this->m_bVerified = true;
                m_nProcSTEP = CLIENT_STEP_LOGIN_WAIT;
                return Recv_cli_ACCEPT_REQ();
            }
            return true;
        case CLIENT_STEP_LOGIN_WAIT:
            switch (pPacket->m_wType) { // 마구 클릭 방지...
                case MON_SERVER_LIST_REQ: {
                    if (pPacket->m_nSize != sizeof(t_PACKETHEADER) + 64)
                        return false;
                    // 비번 체크 md5 password...
                    DWORD* pPass = (DWORD*)&pPacket->m_pDATA[sizeof(t_PACKETHEADER)];
                    DWORD* pMaster = (DWORD*)master_password;
                    bool bHideIP = true;
                    for (short nI = 0; nI < 16; nI++) {
                        if (pPass[nI] != pMaster[nI]) {
                            for (nI = 0; nI < 16; nI++) {
                                if (pPass[nI] != SHO_LS::GetInstance()->password_buffer[nI])
                                    return false;
                            }
                            bHideIP = false;
                            this->m_bAdmClient = false;
                            break;
                        }
                        this->m_bAdmClient = true;
                    }

                    return Recv_mon_SERVER_LIST_REQ((t_PACKET*)pPacket, bHideIP);
                }

                case MON_SERVER_STATUS_REQ: {
                    if (!this->m_bMonClient || !this->m_bAdmClient)
                        return false;
                    return Recv_mon_SERVER_STATUS_REQ((t_PACKET*)pPacket);
                }

                case MON_SERVER_ANNOUNCE: {
                    if (!this->m_bMonClient || !this->m_bAdmClient)
                        return false;

                    nOffset = sizeof(t_PACKETHEADER);
                    char* szMsg = Packet_GetStringPtr((t_PACKET*)pPacket, nOffset);
                    return Send_srv_ANNOUNCE_TEXT(szMsg);
                }
            }
            return true;
        case CLIENT_STEP_LOGIN_REQ: // 마구 클릭한 경우...
            // if ( CLI_LOGIN_REQ == pPacket->m_wType ) {
            //	return true;
            //}
            return true;
        case CLIENT_STEP_LOGEDIN:
            switch (pPacket->m_wType) {
                case CLI_SELECT_SERVER:
                    return Recv_cli_SELECT_SERVER((t_PACKET*)pPacket);
                case CLI_CHANNEL_LIST_REQ:
                    return Recv_cli_CHANNEL_LIST_REQ((t_PACKET*)pPacket);
            }
            return true;
    }

    g_LOG.CS_ODS(0xffff,
        "** ERROR[ %d ] : Invalid packet type: 0x%x, Size: %d ",
        m_nProcSTEP,
        pPacket->m_wType,
        pPacket->m_nSize);

    // 잘못된패킷이 올경우....
    if (CLIENT_STEP_LOGEDIN == m_nProcSTEP) {
        return false;
    }

    DWORD dwCurTIME = classTIME::GetCurrentAbsSecond();
    g_LOG.CS_ODS(0xffff,
        "Invalied packet[ %s ] Type:0x%x, Size:%d",
        this->m_IP.Get(),
        pPacket->m_wType,
        pPacket->m_nSize);

    return false;
}

bool
CLS_Client::recv_login_req(Packet& p) {
    const Packets::LoginRequest* req = p.packet_data()->data_as_LoginRequest();
    g_pThreadSQL->queue_packet(this->m_iSocketIDX, p);
    return true;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CLS_ListCLIENT::CLS_ListCLIENT(UINT uiInitDataCNT, UINT uiIncDataCNT):
    IOCPSocketSERVER((char*)"CLS_ClientSOCKET", 1, 2, false), CDataPOOL<CLS_Client>(
                                                                  (char*)"CClientPOOL",
                                                                  uiInitDataCNT,
                                                                  uiIncDataCNT),
    m_CS(4000) {
    ;
}

void
CLS_ListCLIENT::InitClientSOCKET(iocpSOCKET* pCLIENT) {
    //	::WSAIoctl( pCLIENT->m_Socket, SIO_KEEPALIVE_VALS, &m_KeepAlive, sizeof( m_KeepAlive ), 0,
    // 0, &m_dwTMP, NULL, NULL );
    BOOL bOptVal = TRUE;
    int bOptLen = sizeof(BOOL);
    int iOptVal;
    int iOptLen = sizeof(int);

    ///* lib_server에서.... 기능이 삭제 됐기때문에...추가.
    if (setsockopt(pCLIENT->m_Socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&bOptVal, bOptLen)
        == SOCKET_ERROR) {
        assert("error:: Set SO_KEEPALIVE: ON");
    }
    //*/
    if (getsockopt(pCLIENT->m_Socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&iOptVal, &iOptLen)
        == SOCKET_ERROR) {
        assert("error:: Get SO_KEEPALIVE: ON");
    }
}

bool
CLS_ListCLIENT::Send_lsv_LOGIN_REPLY(int iSocketIDX, BYTE btResult, int iPayType) {
    CLS_Client* pClient = (CLS_Client*)this->GetSOCKET(iSocketIDX);
    if (NULL == pClient)
        return false;

    return pClient->Send_lsv_LOGIN_REPLY(btResult, iPayType);
}
