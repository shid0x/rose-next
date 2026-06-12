#include "stdAFX.h"

#include "CChatROOM.h"
#include "CThreadGUILD.h"
#include "CThreadMSGR.h"
#include "CWS_Client.h"
#include "CWS_Server.h"
#include "SHO_WS_LIB.h"
#include "WS_SocketLSV.h"
#include "WS_ThreadSQL.h"
#include "WS_ZoneLIST.h"

#include "rose/network/packet.h"
#include "rose/network/packets/packet_data_generated.h"

using namespace Rose::Network;

CWS_Client::CWS_Client() {
    m_pNodeChatROOM = new CDLList<CWS_Client*>::tagNODE;
    m_pNodeChatROOM->m_VALUE = this;
}
CWS_Client::~CWS_Client() {
    SAFE_DELETE(m_pNodeChatROOM);
}

bool
CWS_Client::IsHacking(const char* szDesc, const char* szFile, int iLine) {
    if (this->m_dwRIGHT == RIGHT_MASTER)
        return true;

    g_LOG.CS_ODS(0xfff,
        "IsHacking[ID:%s, IP:%s Char:%s] %s() : %s, %d \n",
        this->Get_ACCOUNT(),
        this->m_IP.Get(),
        this->Get_NAME(),
        szDesc,
        szFile,
        iLine);

    this->CloseSocket();
    ::sndPlaySound("Connect.WAV", SND_ASYNC);

    return false;
}

CWS_Server*
CWS_Client::GetSERVER() {
    if (this->m_pAccount)
        return g_pListSERVER->Get_ChannelSERVER(this->m_pAccount->m_btChannelNO);
    return NULL;
}

bool
CWS_Client::Send_wsv_MOVE_SERVER(short nZoneNO) {
    this->m_btChannelNO = this->m_pAccount->m_btChannelNO;
    CWS_Server* pServer = g_pListSERVER->Get_ChannelSERVER(this->m_btChannelNO);
    if (NULL == pServer) {
        // TODO:: 채널 서버가 동작 안하고 있다는 메세지 전송...
        g_LOG.CS_ODS(0xffff,
            "invalid CHANNEL[ %d ]: %d, %s \n",
            this->m_btChannelNO,
            this->m_iSocketIDX,
            this->Get_ACCOUNT());
        this->CloseSocket();
        return false;
    }

    // LogString( LOG_DEBUG_, "moveCHANNEL[ %d ]: %d, %s \n", btChannelNO, this->m_iSocketIDX,
    // this->Get_ACCOUNT() );

    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = WSV_MOVE_SERVER;
    pCPacket.m_HEADER.m_nSize = sizeof(wsv_MOVE_SERVER);

    pCPacket.m_wsv_MOVE_SERVER.m_wPortNO = pServer->m_wListenPORT;
    pCPacket.m_wsv_MOVE_SERVER.m_dwIDs[0] = this->Get_WSID();
    pCPacket.m_wsv_MOVE_SERVER.m_dwIDs[1] = pServer->m_dwRandomSEED;

    pCPacket.AppendString(pServer->m_ServerIP.Get());

    this->SendPacket(pCPacket);
    return true;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Send_srv_ERROR(WORD wErrCODE) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = SRV_ERROR;
    pCPacket.m_HEADER.m_nSize = sizeof(srv_ERROR);

    this->SendPacket(pCPacket);
    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Send_wsv_CHATROOM(BYTE btCMD, WORD wUserID, char* szSTR) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = WSV_CHATROOM;
    pCPacket.m_tag_CHAT_HEADER.m_btCMD = btCMD;

    if (wUserID) {
        pCPacket.m_HEADER.m_nSize = sizeof(wsv_CHAT_ROOM_USER);
        pCPacket.m_wsv_CHAT_ROOM_USER.m_wUserID = wUserID;
    } else
        pCPacket.m_HEADER.m_nSize = sizeof(tag_CHAT_HEADER);

    if (szSTR)
        pCPacket.AppendString(szSTR);

    this->SendPacket(pCPacket);
    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Send_gsv_GM_COMMAND(char* szAccount, BYTE btCMD, WORD wBlockTIME) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = GSV_GM_COMMAND;
    pCPacket.m_HEADER.m_nSize = sizeof(gsv_GM_COMMAND);
    pCPacket.m_gsv_GM_COMMAND.m_btCMD = btCMD;
    pCPacket.m_gsv_GM_COMMAND.m_wBlockTIME = wBlockTIME;

    pCPacket.AppendString(szAccount);

    this->SendPacket(pCPacket);
    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Send_srv_JOIN_SERVER_REPLY(BYTE btResult, DWORD dwRecvSeqNO, DWORD dwPayFlag) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = SRV_JOIN_SERVER_REPLY;
    pCPacket.m_HEADER.m_nSize = sizeof(srv_JOIN_SERVER_REPLY);
    pCPacket.m_srv_JOIN_SERVER_REPLY.m_btResult = btResult;
    pCPacket.m_srv_JOIN_SERVER_REPLY.m_dwID = dwRecvSeqNO;
    pCPacket.m_srv_JOIN_SERVER_REPLY.m_dwPayFLAG = dwPayFlag;

    this->SendPacket(pCPacket);
    return true;
}

//-------------------------------------------------------------------------------------------------
void
CWS_Client::Send_wsv_CHAR_CHANGE() {
    this->Send_HEADER(WSV_CHAR_CHANGE);

    if (this->m_pAccount) {
        m_pAccount->Sub_LoginBIT(BIT_LOGIN_GS);
    }

    g_pUserLIST->Del_CHAR(this);

    //	this->SetWaitZoneNO( 0 );				// Send_wsv_CHAR_CHANGE
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Recv_cli_JOIN_SERVER_REQ(t_PACKET* pPacket) {
    ::CopyMemory(this->password_buffer,
        pPacket->m_cli_JOIN_SERVER_REQ.password,
        sizeof(DWORD) * 16);
    g_pSockLSV->Send_zws_CONFIRM_ACCOUNT_REQ(this->Get_WSID(), pPacket);
    return true;
}

bool
CWS_Client::Recv_cli_DELETE_CHAR(t_PACKET* pPacket) {
    short nOffset = sizeof(cli_DELETE_CHAR);

    char* pCharName = Packet_GetStringPtr(pPacket, nOffset);
    if (!pCharName || !this->Get_ACCOUNT()) {
        return false;
    }

    //	g_pSockLOG->When_DeleteCHAR( this, pCharName );

    return g_pThreadSQL->Add_SqlPacketWithACCOUNT(this, pPacket);
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Recv_cli_JOIN_ZONE(t_PACKET* pPacket) {
    return false;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Recv_cli_MEMO(t_PACKET* pPacket) {
    return g_pThreadSQL->Add_SqlPacketWithAVATAR(this, (t_PACKET*)pPacket);
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Send_tag_MCMD_HEADER(BYTE btCMD, char* szStr) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = WSV_MESSENGER;
    pCPacket.m_HEADER.m_nSize = sizeof(tag_MCMD_HEADER);
    pCPacket.m_tag_MCMD_HEADER.m_btCMD = btCMD;
    if (szStr) {
        pCPacket.AppendString(szStr);
    }

    this->SendPacket(pCPacket);
    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Recv_cli_MCMD_APPEND_REQ(t_PACKET* pPacket) {
    char* szName = pPacket->m_cli_MCMD_APPEND_REQ.m_szName;
    CWS_Client* pDestUSER = g_pUserLIST->Find_CHAR(pPacket->m_cli_MCMD_APPEND_REQ.m_szName);
    if (NULL == pDestUSER)
        return this->Send_tag_MCMD_HEADER(MSGR_CMD_NOT_FOUND, szName);

    if (this == pDestUSER) { // 자신이 자신을 추가...
        return true;
    }

    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = WSV_MESSENGER;
    pCPacket.m_HEADER.m_nSize = sizeof(wsv_MCMD_APPEND_REQ);

    pCPacket.m_wsv_MCMD_APPEND_REQ.m_btCMD = MSGR_CMD_APPEND_REQ;
    pCPacket.m_wsv_MCMD_APPEND_REQ.m_wUserIDX = this->m_iSocketIDX;
    pCPacket.AppendString(this->Get_NAME());

    pDestUSER->SendPacket(pCPacket);
    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Recv_cli_MESSENGER(t_PACKET* pPacket) {
    switch (pPacket->m_tag_MCMD_HEADER.m_btCMD) {
        case MSGR_CMD_APPEND_REQ: // 상대방에 요청
            return this->Recv_cli_MCMD_APPEND_REQ(pPacket);

        case MSGR_CMD_APPEND_REJECT: // 거부
        {
            classUSER* pDestUSER =
                (classUSER*)g_pUserLIST->GetSOCKET(pPacket->m_cli_MCMD_APPEND_REPLY.m_wUserIDX);
            if (pDestUSER) {
                short nOffset = sizeof(cli_MCMD_APPEND_REPLY);
                char* szName = Packet_GetStringPtr(pPacket, nOffset);
                if (szName && !_strcmpi(szName, pDestUSER->Get_NAME()))
                    pDestUSER->Send_tag_MCMD_HEADER(MSGR_CMD_APPEND_REJECT, this->Get_NAME());
            }
            return true;
        }

        case MSGR_CMD_APPEND_ACCEPT: // 쌍방에 추가..
            g_pThreadMSGR->Add_MessengerCMD(this->Get_NAME(),
                MSGR_CMD_APPEND_ACCEPT,
                pPacket,
                this->m_iSocketIDX);
            return true;

        default:
            g_pThreadMSGR->Add_MessengerCMD(this->Get_NAME(),
                pPacket->m_tag_MCMD_HEADER.m_btCMD,
                pPacket,
                this->m_iSocketIDX);
            return true;
    }

    return false;
}
bool
CWS_Client::Recv_cli_MESSENGER_CHAT(t_PACKET* pPacket) {
    if (pPacket->m_HEADER.m_nSize <= 1 + sizeof(cli_MESSENGER_CHAT)) {
        return true;
    }

    g_pThreadMSGR->Add_MessengerCMD(this->Get_NAME(), 0x0ff, pPacket, this->m_iSocketIDX);

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Send_gsv_WHISPER(char* szFromAccount, char* szMessage) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = GSV_WHISPER;
    pCPacket.m_HEADER.m_nSize = sizeof(gsv_WHISPER);
    pCPacket.AppendString(szFromAccount);
    pCPacket.AppendString(szMessage);

    this->SendPacket(pCPacket);
    return true;
}

bool
CWS_Client::Recv_cli_WHISPER(t_PACKET* pPacket) {
    char *szDest, *szMsg;
    short nOffset = sizeof(cli_WHISPER);

    szDest = Packet_GetStringPtr(pPacket, nOffset);
    szMsg = Packet_GetStringPtr(pPacket, nOffset);

    if (NULL == szMsg || 0 == szMsg[0]) {
        return true;
    }

    if (szDest && szMsg) {
        CWS_Client* pUser = g_pUserLIST->Find_CHAR(szDest);
        if (pUser) {
            pUser->Send_gsv_WHISPER(this->Get_NAME(), szMsg);
        } else {
            this->Send_gsv_WHISPER(szDest, NULL);
        }
    }

    return true;
}

bool
CWS_Client::Send_wsv_CLAN_COMMAND(BYTE btCMD, ...) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = WSV_CLAN_COMMAND;
    pCPacket.m_HEADER.m_nSize = sizeof(wsv_CLAN_COMMAND);

    pCPacket.m_wsv_CLAN_COMMAND.m_btRESULT = btCMD;

    va_list va;
    va_start(va, btCMD);
    {
        short nDataLen;
        BYTE* pDataPtr;
        while ((nDataLen = va_arg(va, short)) != NULL) {
            pDataPtr = va_arg(va, BYTE*);
            pCPacket.AppendData(pDataPtr, nDataLen);
        }
    }
    va_end(va);

    this->SendPacket(pCPacket);
    return true;
}

bool
CWS_Client::Recv_cli_CLAN_COMMAND(t_PACKET* pPacket) {
    if (GCMD_CREATE == pPacket->m_cli_CLAN_COMMAND.m_btCMD) {
        return false;
    }

    // Check for Guild Force Join hack.
    tagCLAN_CMD* pGuildCMD = (tagCLAN_CMD*)pPacket;
    CWS_Client* pUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
    if (pUSER != NULL && this->Get_HashACCOUNT() != pUSER->Get_HashACCOUNT()) {
        return IS_HACKING(this, "Recv_cli_CLAN_COMMAND");
    }

    return g_pThreadGUILD->Add_ClanCMD(pPacket->m_cli_CLAN_COMMAND.m_btCMD,
        this->m_iSocketIDX,
        pPacket);
}

//-------------------------------------------------------------------------------------------------
short
CWS_Client::GuildCMD(char* szCMD) {
    char *pArg1, *pArg2;
    char* pDelimiters = (char*)" ";

    CStrVAR tmpStr;
    CStrVAR* pStrVAR = &tmpStr;

    pArg1 = pStrVAR->GetTokenFirst(szCMD, pDelimiters);
    if (NULL == pArg1)
        return 0;

    classPACKET pCPacket = classPACKET();

    if (!_strcmpi(pArg1,
            "invite")) { //길드초대, /ginvite <플레이어> - 길드에 해당 플레이어 초대하기
        pArg2 = pStrVAR->GetTokenNext(pDelimiters);
        if (pArg2) {
            pCPacket.m_HEADER.m_wType = CLI_CLAN_COMMAND;
            pCPacket.m_HEADER.m_nSize = sizeof(cli_CLAN_COMMAND);
            pCPacket.m_cli_CLAN_CREATE.m_btCMD = GCMD_INVITE;

            pCPacket.AppendString(pArg2);
            this->Recv_cli_CLAN_COMMAND((t_PACKET*)(pCPacket.m_pDATA));
        }
    } else if (!_strcmpi(pArg1,
                   "remove")) { //길드추방, /gremove <플레이어> - 길드에서 해당 플레이어 추방하기
        pArg2 = pStrVAR->GetTokenNext(pDelimiters);
        if (pArg2) {
            pCPacket.m_HEADER.m_wType = CLI_CLAN_COMMAND;
            pCPacket.m_HEADER.m_nSize = sizeof(cli_CLAN_COMMAND);
            pCPacket.m_cli_CLAN_CREATE.m_btCMD = GCMD_REMOVE;

            pCPacket.AppendString(pArg2);
            this->Recv_cli_CLAN_COMMAND((t_PACKET*)(pCPacket.m_pDATA));
        }
    } else if (!_strcmpi(pArg1,
                   "promote")) { //길드승급, /gpromote <플레이어> - 해당 플레이어 길드 등급 올리기
        pArg2 = pStrVAR->GetTokenNext(pDelimiters);
        if (pArg2) {
            pCPacket.m_HEADER.m_wType = CLI_CLAN_COMMAND;
            pCPacket.m_HEADER.m_nSize = sizeof(cli_CLAN_COMMAND);
            pCPacket.m_cli_CLAN_CREATE.m_btCMD = GCMD_PROMOTE;

            pCPacket.AppendString(pArg2);
            this->Recv_cli_CLAN_COMMAND((t_PACKET*)(pCPacket.m_pDATA));
        }
    } else if (!_strcmpi(pArg1,
                   "demote")) { //길드강등, /gdemote <플레이어> - 해당 플레이어 길드 등급 내리기
        pArg2 = pStrVAR->GetTokenNext(pDelimiters);
        if (pArg2) {
            pCPacket.m_HEADER.m_wType = CLI_CLAN_COMMAND;
            pCPacket.m_HEADER.m_nSize = sizeof(cli_CLAN_COMMAND);
            pCPacket.m_cli_CLAN_CREATE.m_btCMD = GCMD_DEMOTE;

            pCPacket.AppendString(pArg2);
            this->Recv_cli_CLAN_COMMAND((t_PACKET*)(pCPacket.m_pDATA));
        }
    } else if (!_strcmpi(pArg1, "slogan")) {
        pArg2 = pStrVAR->GetTokenNext(pDelimiters);
        if (pArg2) {
            pCPacket.m_HEADER.m_wType = CLI_CLAN_COMMAND;
            pCPacket.m_HEADER.m_nSize = sizeof(cli_CLAN_COMMAND);
            pCPacket.m_cli_CLAN_CREATE.m_btCMD = GCMD_SLOGAN;

            pCPacket.AppendString(pArg2);
            this->Recv_cli_CLAN_COMMAND((t_PACKET*)(pCPacket.m_pDATA));
        }
    } else if (!_strcmpi(pArg1, "motd")) { //길드공지, /gmotd <할말> - 오늘의 길드 메시지 정하기
        pArg2 = pStrVAR->GetTokenNext(pDelimiters);
        if (pArg2) {
            pCPacket.m_HEADER.m_wType = CLI_CLAN_COMMAND;
            pCPacket.m_HEADER.m_nSize = sizeof(cli_CLAN_COMMAND);
            pCPacket.m_cli_CLAN_CREATE.m_btCMD = GCMD_MOTD;

            pCPacket.AppendString(pArg2);
            this->Recv_cli_CLAN_COMMAND((t_PACKET*)(pCPacket.m_pDATA));
        }
    } else if (!_strcmpi(pArg1, "quit")) { //길드탈퇴, /gquit - 길드에서 탈퇴하기
        pCPacket.m_HEADER.m_wType = CLI_CLAN_COMMAND;
        pCPacket.m_HEADER.m_nSize = sizeof(cli_CLAN_COMMAND);
        pCPacket.m_cli_CLAN_CREATE.m_btCMD = GCMD_QUIT;
        this->Recv_cli_CLAN_COMMAND((t_PACKET*)(pCPacket.m_pDATA));
    } else if (!_strcmpi(pArg1, "roster")) { //길드목록, /groster - 전체 길드원 목록 보기
        pCPacket.m_HEADER.m_wType = CLI_CLAN_COMMAND;
        pCPacket.m_HEADER.m_nSize = sizeof(cli_CLAN_COMMAND);
        pCPacket.m_cli_CLAN_CREATE.m_btCMD = GCMD_ROSTER;

        this->Recv_cli_CLAN_COMMAND((t_PACKET*)(pCPacket.m_pDATA));
    } else if (!_strcmpi(pArg1, "leader")) { //길드위임, /gleader <플레이어> - 다른 플레이어에게
                                             //길드장 위임하기 (길드장 전용)
        pArg2 = pStrVAR->GetTokenNext(pDelimiters);
        if (pArg2) {
            pCPacket.m_HEADER.m_wType = CLI_CLAN_COMMAND;
            pCPacket.m_HEADER.m_nSize = sizeof(cli_CLAN_COMMAND);
            pCPacket.m_cli_CLAN_CREATE.m_btCMD = GCMD_LEADER;

            pCPacket.AppendString(pArg2);
            this->Recv_cli_CLAN_COMMAND((t_PACKET*)(pCPacket.m_pDATA));
        }
    }

    return 0;
}

bool
CWS_Client::Recv_cli_GUILD_CHAT(t_PACKET* pPacket) {
    if (this->GetClanID()) {
        short nOffset = sizeof(cli_CLAN_CHAT);
        char* szMsg = Packet_GetStringPtr(pPacket, nOffset);
        if (!strncmp(szMsg, "/clan ", 5)) {
            this->GuildCMD(&szMsg[5]);
            return true;
        }

        g_pThreadGUILD->Add_ClanCMD(GCMD_CHAT, this->GetClanID(), pPacket, this->Get_NAME());
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Recv_cli_CLANMARK_SET(t_PACKET* pPacket) {
    if (this->GetClanID()) {
        g_pThreadGUILD->Add_ClanCMD(GCMD_CLANMARK_SET, this->m_iSocketIDX, pPacket);
    }

    return true;
}
bool
CWS_Client::Recv_cli_CLANMARK_REQ(t_PACKET* pPacket) {
    g_pThreadGUILD->Add_ClanCMD(GCMD_CLANMARK_GET, this->m_iSocketIDX, pPacket);
    return true;
}
bool
CWS_Client::Recv_cli_CLANMARK_REG_TIME(t_PACKET* pPacket) {
    g_pThreadGUILD->Add_ClanCMD(GCMD_CLANMARK_REGTIME, this->m_iSocketIDX, pPacket);
    return true;
}

bool
CWS_Client::Send_wsv_CLANMARK_REPLY(DWORD dwClanID,
    WORD wMarkCRC,
    char* pMarkData,
    short nDataLen) {
    if (!pMarkData) {
        return false;
    }

    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = WSV_CLANMARK_REPLY;
    pCPacket.m_HEADER.m_nSize = sizeof(wsv_CLANMARK_REPLY);

    pCPacket.m_wsv_CLANMARK_REPLY.m_dwClanID = dwClanID;
    pCPacket.m_wsv_CLANMARK_REPLY.m_wMarkCRC16 = wMarkCRC;
    pCPacket.AppendData(pMarkData, nDataLen);

    this->SendPacket(pCPacket);
    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::Recv_mon_SERVER_LIST_REQ(t_PACKET* pPacket) {
    classPACKET pCPacket = classPACKET();

    g_pListSERVER->Make_srv_SERVER_LIST_REPLY(&pCPacket);

    this->SendPacket(pCPacket);
    return true;
}
bool
CWS_Client::Recv_mon_SERVER_STATUS_REQ(t_PACKET* pPacket) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = WLS_SERVER_STATUS_REPLY;
    pCPacket.m_HEADER.m_nSize = sizeof(wls_SERVER_STATUS_REPLY);

    pCPacket.m_wls_SERVER_STATUS_REPLY.m_dwTIME = pPacket->m_wls_SERVER_STATUS_REPLY.m_dwTIME;
    pCPacket.m_wls_SERVER_STATUS_REPLY.m_nServerCNT = g_pListSERVER->Get_ChannelCOUNT();
    pCPacket.m_wls_SERVER_STATUS_REPLY.m_iUserCNT = g_pUserLIST->Get_AccountCNT();

    this->SendPacket(pCPacket);
    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_Client::HandlePACKET(t_PACKETHEADER* pPacket) {
    if (this->m_iSocketIDX == 0) {
        return false;
    }

    flatbuffers::Verifier verifier(&pPacket->m_pDATA[2], pPacket->size - 2);
    bool valid = Packets::VerifyPacketDataBuffer(verifier);
    if (valid) {
        Packet p(&pPacket->m_pDATA[0], pPacket->size);

        Packets::PacketType packet_type = p.packet_data()->data_type();
        switch (packet_type) {
            case Packets::PacketType::CharacterCreateRequest: {
                return this->recv_char_create_req(p);
            }
            default: {
                LOG_WARN("Received unknown packet type {}", packet_type);
                break; // TODO: Don't fall through once old packet handling has been replaced
            }
        }
    }

    switch (pPacket->m_wType) {
        case MON_SERVER_LIST_REQ:
            this->m_bVerified = true;
            return Recv_mon_SERVER_LIST_REQ((t_PACKET*)pPacket);
        case MON_SERVER_STATUS_REQ:
            return Recv_mon_SERVER_STATUS_REQ((t_PACKET*)pPacket);
        case MON_SERVER_ANNOUNCE:
            return true;

        case CLI_LOGOUT_REQ:
            // Recv_cli_LOGOUT_REQ( pPacket );
            // 짤리도록..
            return false;
        case CLI_JOIN_SERVER_REQ:
            if (this->m_HashCHAR)
                return false;
            this->m_bVerified = true;
            return Recv_cli_JOIN_SERVER_REQ((t_PACKET*)pPacket);
        case CLI_DELETE_CHAR:
            if (this->m_HashCHAR)
                return false;
            return Recv_cli_DELETE_CHAR((t_PACKET*)pPacket);

        case CLI_SELECT_CHAR:
        case CLI_CHAR_LIST:
            if (this->m_HashCHAR)
                return false;
            return g_pThreadSQL->Add_SqlPacketWithACCOUNT(this, (t_PACKET*)pPacket);

        case CLI_JOIN_ZONE:
            // 이 패킷을 받은 이후에는 게임 플레이용 패킷이다...
            return Recv_cli_JOIN_ZONE((t_PACKET*)pPacket);

        case CLI_CHATROOM: {
            if (!(this->m_dwPayFLAG & PLAY_FLAG_COMMUNITY)) {
                switch (((t_PACKET*)pPacket)->m_tag_CHAT_HEADER.m_btCMD) {
                    case CHAT_REQ_MAKE:
                        this->Send_wsv_CHATROOM(CHAT_REPLY_MAKE_FAILED, 0, NULL);
                        break;

                    case CHAT_REQ_JOIN:
                        this->Send_wsv_CHATROOM(CHAT_REPLY_FULL_USERS, 0, NULL);
                        break;
                }
                return true;
            }
            return g_pChatROOMs->Recv_cli_CHATROOM(this, (t_PACKET*)pPacket);
        }
        case CLI_CHATROOM_MSG: {
            if (!(this->m_dwPayFLAG & PLAY_FLAG_COMMUNITY))
                return true;
            return g_pChatROOMs->Recv_cli_CHATROOM_MSG(this, (t_PACKET*)pPacket);
        }

        case CLI_MEMO: // 쪽지 보내자...
        {
            if (!(this->m_dwPayFLAG & PLAY_FLAG_COMMUNITY))
                return true;
            return Recv_cli_MEMO((t_PACKET*)pPacket);
        }

        case CLI_MESSENGER:
            return Recv_cli_MESSENGER((t_PACKET*)pPacket);
        case CLI_MESSENGER_CHAT: {
            if (!(this->m_dwPayFLAG & PLAY_FLAG_COMMUNITY))
                return true;
            return Recv_cli_MESSENGER_CHAT((t_PACKET*)pPacket);
        }

        case CLI_WHISPER: {
            // if ( !(this->m_dwPayFLAG & PLAY_FLAG_COMMUNITY ) )
            //	return true;
            return Recv_cli_WHISPER((t_PACKET*)pPacket);
        }

        case CLI_CLAN_COMMAND:
            return Recv_cli_CLAN_COMMAND((t_PACKET*)pPacket);

        case CLI_CLAN_CHAT: {
            if (!(this->m_dwPayFLAG & PLAY_FLAG_BATTLE))
                return true;
            return Recv_cli_GUILD_CHAT((t_PACKET*)pPacket);
        }

        case CLI_CLANMARK_SET:
            return Recv_cli_CLANMARK_SET((t_PACKET*)pPacket);
        case CLI_CLANMARK_REQ:
            return Recv_cli_CLANMARK_REQ((t_PACKET*)pPacket);

        case CLI_CLANMARK_REG_TIME:
            return Recv_cli_CLANMARK_REG_TIME((t_PACKET*)pPacket);
            /*
                    case SRV_ERROR :
                        return true;
            */
    }

    g_LOG.CS_ODS(0xffff,
        "** ERROR:: CLS_Server: Invalid packet type: 0x%x, Size: %d ",
        pPacket->m_wType,
        pPacket->m_nSize);

    //	return IS_HACKING( this, "HandlePACKET" );
    return false;
}

bool
CWS_Client::recv_char_create_req(Rose::Network::Packet& p) {
    if (this->m_HashCHAR) {
        return false;
    }
    if (SHO_WS::GetInstance()->IsBlockedCreateCHAR()) {
        g_pUserLIST->Send_wsv_CREATE_CHAR(this->m_iSocketIDX, RESULT_CREATE_CHAR_BLOCKED);
        return true;
    }

    const Packets::CharacterCreateRequest* req = p.packet_data()->data_as_CharacterCreateRequest();
    g_pThreadSQL->queue_packet(this->m_iSocketIDX, p);
    return true;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void
CWS_ListCLIENT::ClosedClientSOCKET(iocpSOCKET* pSOCKET) {
    CWS_Client* pClient = (CWS_Client*)pSOCKET;

    if (pClient->m_pAccount) {
        pClient->LockSOCKET();
        {
            if (0 == this->Del_ACCOUNT(pClient->Get_ACCOUNT(), BIT_LOGIN_WS))
                pClient->m_HashACCOUNT = 0;
        }
        pClient->UnlockSOCKET();
    }

    this->FreeClientSOCKET(pSOCKET);
    // 소켓이 삭제됐다.. 알아서 메모리 해제할것...
}

//-------------------------------------------------------------------------------------------------
bool
CWS_ListCLIENT::SendPacketToSocketIDX(int iClientSocketIDX, const classPACKET& pCPacket) {
    // 반드시 pCPacket은 1 유저에게 보내는 패킷이어야 한다.
    CWS_Client* pFindUSER = (CWS_Client*)this->GetSOCKET(iClientSocketIDX);
    if (pFindUSER) {
        if (!pFindUSER->SendPacket(pCPacket)) {
            // 보내기 실패...
        }

        return true;
    }

    return false;
}

//-------------------------------------------------------------------------------------------------
void
CWS_ListCLIENT::Send_wsv_CREATE_CHAR(int iSocketIDX, BYTE btResult, BYTE btIsPlatinumCHAR) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = WSV_CREATE_CHAR;
    pCPacket.m_HEADER.m_nSize = sizeof(wsv_CREATE_CHAR);
    pCPacket.m_wsv_CREATE_CHAR.m_btResult = btResult;
    pCPacket.m_wsv_CREATE_CHAR.m_btIsPlatinumCHAR = btIsPlatinumCHAR;

    SendPacketToSocketIDX(iSocketIDX, pCPacket);
}

//-------------------------------------------------------------------------------------------------
void
CWS_ListCLIENT::Send_wsv_MEMO(int iSocketIDX, BYTE btTYPE, short nRecvCNT) {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = WSV_MEMO;
    pCPacket.m_wsv_MEMO.m_btTYPE = btTYPE;

    if (nRecvCNT >= 0) {
        pCPacket.m_HEADER.m_nSize = sizeof(wsv_MEMO) + sizeof(short);
        pCPacket.m_wsv_MEMO.m_nRecvCNT[0] = nRecvCNT;
    } else
        pCPacket.m_HEADER.m_nSize = sizeof(wsv_MEMO);

    this->SendPacketToSocketIDX(iSocketIDX, pCPacket);
}

//-------------------------------------------------------------------------------------------------
bool
CWS_ListCLIENT::Add_ACCOUNT(int iSocketIDX, t_PACKET* pRecvPket, char* szAccount) {
    CWS_Client* pUSER = (CWS_Client*)this->GetSOCKET(iSocketIDX);
    if (NULL == pUSER) {
        // 그새 접속이 끊겼는가 ???
        g_pSockLSV->Send_zws_SUB_ACCOUNT(pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_dwLSID,
            szAccount);
        return false;
    }

    DWORD dwRecvSeqNO = 0xffffffff, dwPayFlag = 0;
    if (RESULT_CONFIRM_ACCOUNT_OK == pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_btResult) {
        t_HASHKEY HashACCOUNT;
        HashACCOUNT = CStr::GetHASH(szAccount);
        if (Check_DupACCOUNT(szAccount, HashACCOUNT)) {
            // 로그인 서버가 혹시 뻣었을 경우...중복 접속을 로그인서버가 막지 못할경우를 대비해서
            // 월드에서 중복 접속을 확인... 아이템 복사가 일어 날수 있기때문에...
            // 다른 월드로 접속할경우는 ????.
            g_pSockLSV->Send_zws_SUB_ACCOUNT(pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_dwLSID,
                szAccount);
            return pUSER->Send_srv_JOIN_SERVER_REPLY(RESULT_CONFIRM_ACCOUNT_ALREADY_LOGGEDIN,
                dwRecvSeqNO,
                0);
        }

        pUSER->m_dwRIGHT = pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_dwRIGHT;
        pUSER->m_dwPayFLAG = pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_dwPayFLAG;
        dwPayFlag = pUSER->m_dwPayFLAG;

        dwRecvSeqNO = ::timeGetTime();
        pUSER->Set_RecvSeqNO(dwRecvSeqNO);

        CWS_Account* pCAccount = new CWS_Account;

        pCAccount->m_btChannelNO = pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_btChannelNO;

        pUSER->m_pAccount = pCAccount;
        pCAccount->Add_WSBit(pUSER,
            szAccount,
            pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_dwLSID,
            pUSER->m_iSocketIDX,
            pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_dwLoginTIME);

        m_csHashACCOUNT.Lock();
        pUSER->m_HashACCOUNT = HashACCOUNT;
        m_pHashACCOUNT->Insert(pUSER->m_HashACCOUNT, pCAccount);
        m_csHashACCOUNT.Unlock();

    } else {
        char password[65];
        ::CopyMemory(password, pUSER->password_buffer, 64);
        password[64] = 0;
        g_LOG.CS_ODS(0xffff,
            "LS Return RESULT_CONFIRM_ACCOUNT_INVALID_PASSWORD: LSID:%d, %s\n",
            pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_dwLSID,
            password);
    }

    return pUSER->Send_srv_JOIN_SERVER_REPLY(pRecvPket->m_wls_CONFIRM_ACCOUNT_REPLY.m_btResult,
        dwRecvSeqNO,
        dwPayFlag);
}

//-------------------------------------------------------------------------------------------------
BYTE
CWS_ListCLIENT::Del_ACCOUNT(char* szAccount, BYTE btDelLoginBIT, CWS_Server* pClosingServer) {
    t_HASHKEY HashKEY = CStr::GetHASH(szAccount);

    tagHASH<CWS_Account*>* pHashNode;
    CWS_Account* pAccount;
    BYTE btRamainBIT = BIT_LOGIN_WS | BIT_LOGIN_GS;

    m_csHashACCOUNT.Lock();
    {
        pHashNode = m_pHashACCOUNT->Search(HashKEY);
        pAccount = pHashNode ? pHashNode->m_DATA : NULL;
        while (pAccount) {
            if (!_strcmpi(szAccount, pAccount->Get_ACCOUNT())) {
                if (pAccount->m_pCLIENT) {
                    this->Del_CHAR(pAccount->m_pCLIENT);
                }

                switch (pAccount->Sub_LoginBIT(btDelLoginBIT)) {
                    case BIT_LOGIN_WS: // 채널 서버에서 계정이 삭제됐다 - 케릭터가 삭제됐다.
                        // 소켓 종료...
                        if (pAccount->m_pCLIENT) {
                            pAccount->m_pCLIENT->CloseSocket();
                            CWS_Server* pCServer;

                            // pCServer = pClosingServer ? pClosingServer :
                            // g_pListSERVER->Get_ChannelSERVER( pAccount->m_btChannelNO );
                            pCServer = pClosingServer
                                ? pClosingServer
                                : g_pListSERVER->Get_ChannelSERVER(pAccount->m_btChannelNO);
                            if (pCServer) {
                                pCServer->SubChannelCLIENT(pAccount->m_pCLIENT);
                            }
                        } else {
                            LOG_ERROR(
                                "****ERROR:: NULL == pAccount->m_pCLIENT[ {}, Cur:{:#x}, Sub:{:#x} ] ",
                                szAccount,
                                pAccount->m_btLoginBIT,
                                btDelLoginBIT);
                        }
                        btRamainBIT = BIT_LOGIN_WS;
                        break;

                    case BIT_LOGIN_GS: // 소켓이 종료됐다.
                    {
                        // 채널 서버에 계정 삭제 통보...
                        CWS_Server* pCServer =
                            g_pListSERVER->Get_ChannelSERVER(pAccount->m_btChannelNO);
                        if (pCServer) {
                            pCServer->Send_str_PACKET(WLS_KICK_ACCOUNT,
                                szAccount); // GS에 계정 삭제하라고 통보....
                            pCServer->SubChannelCLIENT(pAccount->m_pCLIENT);
                            pAccount->m_pCLIENT = NULL;
                            // 여기서 리턴되면 pAccount->m_pCLIENT는 메모리가 해제된다.
                            btRamainBIT = BIT_LOGIN_GS;
                            break;
                        }
                        // 채널 서버가 없어 졌다...	계정 바로 삭제...
                    }

                    case 0: // 계정 삭제...
                        if (!m_pHashACCOUNT->Delete(HashKEY, pAccount))
                            g_LOG.CS_ODS(0xffff,
                                "ERROR *** Sub_ACCOUNT( %s ) not found, AccountCNT:%d \n",
                                pAccount->Get_ACCOUNT(),
                                m_pHashACCOUNT->GetCount());
                        else {
                            // classTIME::GetCurrentAbsSecond ()
                            // pAccount->m_dwLoginTIME;
                            LOG_ERROR("{} Account, Sub_ACCOUNT({})",
                                m_pHashACCOUNT->GetCount(),
                                pAccount->Get_ACCOUNT());
                        }

                        g_pSockLSV->Send_zws_SUB_ACCOUNT(pAccount->m_dwLSID,
                            pAccount->Get_ACCOUNT()); // LS에 삭제 전송.

                        SAFE_DELETE(pAccount);
                        btRamainBIT = 0;
                        break;
                }
                m_csHashACCOUNT.Unlock();

                return btRamainBIT;
            }

            pHashNode = m_pHashACCOUNT->SearchContinue(pHashNode, HashKEY);
            pAccount = pHashNode ? pHashNode->m_DATA : NULL;
        }

        // 로그인 서버에 삭제 요청한 계정인데...없을경우...
        g_pSockLSV->Send_zws_SUB_ACCOUNT(0, szAccount); // LS에 삭제 전송.
    }
    m_csHashACCOUNT.Unlock();

    return btRamainBIT;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_ListCLIENT::Check_DupACCOUNT(char* szAccount, t_HASHKEY HashKEY) {
    tagHASH<CWS_Account*>* pHashNode;

    m_csHashACCOUNT.Lock();
    {
        pHashNode = m_pHashACCOUNT->Search(HashKEY);
        while (pHashNode) {
            if (!_strcmpi(szAccount, pHashNode->m_DATA->Get_ACCOUNT())) {
                // Find !!! 짤라랏 !!!
                m_csHashACCOUNT.Unlock();
                if (pHashNode->m_DATA->m_pCLIENT) {
                    pHashNode->m_DATA->m_pCLIENT->CloseSocket();
                }
                return true;
            }

            pHashNode = m_pHashACCOUNT->SearchContinue(pHashNode, HashKEY);
        }
    }
    m_csHashACCOUNT.Unlock();

    return false;
}

//-------------------------------------------------------------------------------------------------
CWS_Client*
CWS_ListCLIENT::Find_ACCOUNT(char* szAccount) {
    t_HASHKEY HashKEY = CStr::GetHASH(szAccount);
    tagHASH<CWS_Account*>* pHashNode;

    m_csHashACCOUNT.Lock();
    {
        pHashNode = m_pHashACCOUNT->Search(HashKEY);
        while (pHashNode) {
            if (!_strcmpi(szAccount, pHashNode->m_DATA->Get_ACCOUNT())) {
                // Find !!!
                m_csHashACCOUNT.Unlock();
                return pHashNode->m_DATA->m_pCLIENT;
            }

            pHashNode = m_pHashACCOUNT->SearchContinue(pHashNode, HashKEY);
        }
    }
    m_csHashACCOUNT.Unlock();

    return NULL;
}

//-------------------------------------------------------------------------------------------------
bool
CWS_ListCLIENT::Add_CHAR(CWS_Client* pCLIENT, char* szCharName) {
    pCLIENT->m_Name.Set(szCharName);
    pCLIENT->m_HashCHAR = CStr::GetHASH(pCLIENT->Get_NAME());

    g_pThreadMSGR->Add_MessengerCMD(szCharName,
        MSGR_CMD_LOGIN,
        NULL,
        pCLIENT->m_iSocketIDX,
        pCLIENT->m_dwDBID);

    m_csHashCHAR.Lock();
    m_pHashCHAR->Insert(pCLIENT->m_HashCHAR, pCLIENT);
    m_csHashCHAR.Unlock();

    return true;
}

void
CWS_ListCLIENT::Del_CHAR(CWS_Client* pCLIENT) {
    g_pChatROOMs->LeftUSER(pCLIENT);

    // 동시접근 불가 함수 !!!!
    m_csHashCHAR.Lock();
    if (0 == pCLIENT->m_HashCHAR) {
        m_csHashCHAR.Unlock();
        return;
    }
    if (!m_pHashCHAR->Delete(pCLIENT->m_HashCHAR, pCLIENT)) {
        g_LOG.CS_ODS(0xffff,
            "ERROR *** Sub_CHAR( %s : %s) not found \n",
            pCLIENT->Get_ACCOUNT(),
            pCLIENT->Get_IP());
    }
    pCLIENT->m_HashCHAR = 0;
    m_csHashCHAR.Unlock();

    g_pThreadMSGR->Add_MessengerCMD(pCLIENT->Get_NAME(), MSGR_CMD_LOGOUT, NULL, 0);
}

CWS_Client*
CWS_ListCLIENT::Find_CHAR(char* szAvatar) {
    t_HASHKEY HashKEY = CStr::GetHASH(szAvatar);
    tagHASH<CWS_Client*>* pHashNode;

    m_csHashCHAR.Lock();
    {
        pHashNode = m_pHashCHAR->Search(HashKEY);
        while (pHashNode) {
            if (!_strcmpi(szAvatar, pHashNode->m_DATA->Get_NAME())) {
                // Find !!!
                m_csHashCHAR.Unlock();
                return pHashNode->m_DATA;
            }

            pHashNode = m_pHashCHAR->SearchContinue(pHashNode, HashKEY);
        }
    }
    m_csHashCHAR.Unlock();

    return NULL;
}

//-------------------------------------------------------------------------------------------------
void
CWS_ListCLIENT::Send_wls_ACCOUNT_LIST() {
    classPACKET* pCPacket = Packet_AllocNLock();

    pCPacket->m_HEADER.m_wType = WLS_ACCOUNT_LIST;
    pCPacket->m_HEADER.m_nSize = sizeof(wls_ACCOUNT_LIST);
    pCPacket->m_wls_ACCOUNT_LIST.m_nAccountCNT = 0;

    tagHASH<CWS_Account*>* pAccNODE;
    tag_WLS_ACCOUNT sInfo;

    m_csHashACCOUNT.Lock();
    for (int iL = 0; iL < m_pHashACCOUNT->GetTableCount(); iL++) {
        pAccNODE = m_pHashACCOUNT->GetEntryNode(iL);
        while (pAccNODE) {
            sInfo.m_dwLoginTIME = pAccNODE->m_DATA->m_dwLoginTIME;
            sInfo.m_dwLSID = pAccNODE->m_DATA->m_dwLSID;
            pCPacket->AppendData(&sInfo, sizeof(tag_WLS_ACCOUNT));
            pCPacket->AppendString(pAccNODE->m_DATA->m_Account.Get());
            pCPacket->m_wls_ACCOUNT_LIST.m_nAccountCNT++;

            if (pCPacket->m_HEADER.m_nSize >= MAX_PACKET_SIZE - 50) {
                g_pSockLSV->m_SockLSV.Packet_Register2SendQ(pCPacket);
                Packet_ReleaseNUnlock(pCPacket);

                pCPacket = Packet_AllocNLock();
                pCPacket->m_HEADER.m_wType = WLS_ACCOUNT_LIST;
                pCPacket->m_HEADER.m_nSize = sizeof(wls_ACCOUNT_LIST);
                pCPacket->m_wls_ACCOUNT_LIST.m_nAccountCNT = 0;
            }

            pAccNODE = pAccNODE->m_NEXT;
        }
    }
    m_csHashACCOUNT.Unlock();

    if (0 != pCPacket->m_wls_ACCOUNT_LIST.m_nAccountCNT) {
        g_pSockLSV->m_SockLSV.Packet_Register2SendQ(pCPacket);
    }

    Packet_ReleaseNUnlock(pCPacket);
}

//-------------------------------------------------------------------------------------------------
#define SOCKET_KEEP_ALIVE_TIME (5 * 60 * 1000) // 5분
void
CWS_ListCLIENT::Check_SocketALIVE() {
    classPACKET pCPacket = classPACKET();

    pCPacket.m_HEADER.m_wType = SRV_ERROR;
    pCPacket.m_HEADER.m_nSize = sizeof(gsv_ERROR);
    pCPacket.m_gsv_ERROR.m_wErrorCODE = 0;

    DWORD dwCurTIME = ::timeGetTime();
    tagHASH<CWS_Account*>* pAccNODE;

    m_csHashACCOUNT.Lock();
    for (int iL = 0; iL < m_pHashACCOUNT->GetTableCount(); iL++) {
        pAccNODE = m_pHashACCOUNT->GetEntryNode(iL);
        while (pAccNODE) {
            if (pAccNODE->m_DATA && pAccNODE->m_DATA->m_pCLIENT) {
                // 마지막으로 패킷을 보낸후 30초가 넘었으면...
                if (dwCurTIME - pAccNODE->m_DATA->m_pCLIENT->Get_CheckTIME() >= 30 * 1000) {
                    pAccNODE->m_DATA->m_pCLIENT->SendPacket(pCPacket);
                    // 5분동안 암것도 보내지 못했으면... 짤러~~~~
                    if (dwCurTIME - SOCKET_KEEP_ALIVE_TIME
                        > pAccNODE->m_DATA->m_pCLIENT->Get_CheckTIME())
                        pAccNODE->m_DATA->m_pCLIENT->CloseSocket();
                }
            }

            pAccNODE = pAccNODE->m_NEXT;
        }
    }
    m_csHashACCOUNT.Unlock();
}

CWS_Client*
CWS_ListCLIENT::find_client(size_t socket_id) {
    iocpSOCKET* sock = this->GetSOCKET(socket_id);
    if (sock) {
        return static_cast<CWS_Client*>(sock);
    }
    return nullptr;
}