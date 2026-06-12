#include "stdAFX.h"

#include "rose/database/database.h"

using namespace Rose::Database;

#if defined(__SHO_WS)

    #ifdef __SHO_WS
        #include "CWS_Client.h"
    #endif

    #include "CThreadMSGR.h"
    //#include "classLOG.h"
    #include "rose/io/stb.h"

//-------------------------------------------------------------------------------------------------
CThreadMSGR::CThreadMSGR(UINT uiInitDataCNT, UINT uiIncDataCNT):
    CSqlTHREAD(true), /* m_csListCMD( 4000 ) */
    m_Pools((char*)"CMessengerPOOL", uiInitDataCNT, uiIncDataCNT),
    m_HashMSGR(1024 * 3) {}

void
CThreadMSGR::Check_FRIENDS() {
    /*
        int iCharID=0, iFriendCNT, iTotCNT=0;
        BYTE *pLIST;
        while( true )
        {
            if ( !this->db->QuerySQL( "SELECT TOP 1 * FROM tblWS_FRIEND WHERE intCharID > %d
       ORDER BY intCharID ASC", iCharID ) ) { g_LOG.CS_ODS(LOG_NORMAL, "Query ERROR:: %s \n",
       this->db->GetERROR() ); return;
            }

            if ( !this->db->GetNextRECORD() )
                break;

            {
                iCharID = this->db->GetInteger( 0 );
                iFriendCNT = this->db->GetInteger( 1 );
                pLIST = this->db->GetDataPTR( 2 );

                char *pName;
                tagFriend_H *pFrH;
                tagMyFriend  FR;
                for (int iC=0; iC<iFriendCNT; iC++) {
                    pFrH = (tagFriend_H*)pLIST;
                    pName = (char*)( pLIST+sizeof(tagFriend_H) );

                    _ASSERT( iCharID != pFrH->m_dwDBID );

                    FR.m_dwDBID = pFrH->m_dwDBID;
                    FR.m_btSTATUS = pFrH->m_btSTATUS;
                    FR.m_Name.Set( pName );

                    // check name & dbid...
                    if ( !this->db->QuerySQL( "SELECT txtNAME FROM tblGS_AVATAR WHERE
       intCharID=%d", FR.m_dwDBID ) ) { g_LOG.CS_ODS(LOG_NORMAL, "Query ERROR:: %s \n",
       this->db->GetERROR() ); } else if ( this->db->GetNextRECORD() ) { pName =
       this->db->GetStrPTR( 0 ); if ( 0 == pFrH->m_dwDBID || pFrH->m_dwDBID > 31793 ) {
                            LogString (0xffff, "Invalid DBID: %d, %s \n", FR.m_dwDBID,
       FR.m_Name.Get() ); } else if ( strcmpi(pName, FR.m_Name.Get() ) ) { LogString (0xffff, "Name
       mismatch %d, %s/%s \n", pFrH->m_dwDBID, pName, FR.m_Name.Get() );
                        }
                    }

                    pLIST += ( sizeof(tagFriend_H)+strlen(pName)+1 );
                }

                iTotCNT ++;
            }
        }
        LogString (0xffff, ">>>>>>>>>>>>> Totcal friend record %d \n", iTotCNT );
    */
}

//-------------------------------------------------------------------------------------------------
struct tagRU {
    CStrVAR m_Char;
    int m_iCnt;
};

void
CThreadMSGR::Check_ItemCNT(int iItemType, int iItemNo) {}

//-------------------------------------------------------------------------------------------------
void
CThreadMSGR::Execute() {
    CDLList<tagMSGR_CMD>::tagNODE* pCmdNODE;

    //	this->Check_FRIENDS ();
    //	this->Check_ItemCNT (ITEM_TYPE_NATURAL, 401);

    while (TRUE) {
        if (!this->Terminated) {
            m_pEVENT->WaitFor(INFINITE);
        } else {
            int iReaminCNT;
            this->m_CS.Lock();
            iReaminCNT = m_WaitCMD.GetNodeCount();
            this->m_CS.Unlock();

            if (iReaminCNT <= 0)
                break;
        }

        this->m_CS.Lock();
        m_ProcCMD.AppendNodeList(&m_WaitCMD);
        m_WaitCMD.Init();
        m_pEVENT->ResetEvent();
        this->m_CS.Unlock();

        for (pCmdNODE = m_ProcCMD.GetHeadNode(); pCmdNODE;) {
            this->Run_MessengerPACKET(&pCmdNODE->m_VALUE);

            SAFE_DELETE_ARRAY(pCmdNODE->m_VALUE.m_pPacket);
            m_ProcCMD.DeleteNFree(pCmdNODE);
            pCmdNODE = m_ProcCMD.GetHeadNode();
        }
    }

    int iCnt = m_AddPACKET.GetNodeCount();
    _ASSERT(iCnt == 0);
}

//-------------------------------------------------------------------------------------------------
void
CThreadMSGR::Add_MessengerCMD(char* szCharName,
    BYTE btCMD,
    t_PACKET* pPacket,
    int iSocketIDX,
    DWORD dwDBID) {
    CDLList<tagMSGR_CMD>::tagNODE* pNewNODE;

    pNewNODE = new CDLList<tagMSGR_CMD>::tagNODE;

    pNewNODE->m_VALUE.m_iSocketIDX = iSocketIDX;
    pNewNODE->m_VALUE.m_dwDBID = dwDBID;
    pNewNODE->m_VALUE.m_btCMD = btCMD;
    pNewNODE->m_VALUE.m_Name.Set(szCharName);
    if (pPacket) {
        pNewNODE->m_VALUE.m_pPacket = (t_PACKET*)new BYTE[pPacket->m_HEADER.m_nSize];
        ::CopyMemory(pNewNODE->m_VALUE.m_pPacket, pPacket, pPacket->m_HEADER.m_nSize);
    } else
        pNewNODE->m_VALUE.m_pPacket = NULL;

    this->m_CS.Lock();
    m_WaitCMD.AppendNode(pNewNODE);
    this->m_CS.Unlock();

    this->Set_EVENT();
}

//-------------------------------------------------------------------------------------------------
CMessenger*
CThreadMSGR::SearchMSGR(char* szCharName) {
    t_HASHKEY HashKEY = CStr::GetHASH(szCharName);

    tagHASH<CMessenger*>* pHashNode = m_HashMSGR.Search(HashKEY);
    while (pHashNode) {
        if (!_strcmpi(szCharName, pHashNode->m_DATA->m_Name.Get())) {
            return pHashNode->m_DATA;
        }
        pHashNode = m_HashMSGR.SearchContinue(pHashNode, HashKEY);
    }
    return NULL;
}

bool
CThreadMSGR::Run_MessengerPACKET(tagMSGR_CMD* pMsgCMD) {
    switch (pMsgCMD->m_btCMD) {
        case MSGR_CMD_LOGIN:
            this->LogIN(pMsgCMD);
            return true;
        case MSGR_CMD_LOGOUT: {
            CMessenger* pMSGR = this->SearchMSGR(pMsgCMD->m_Name.Get());
            if (pMSGR)
                this->LogOUT(pMSGR);
            return true;
        }

        case MSGR_CMD_APPEND_ACCEPT: {
            CWS_Client* pRequestUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(
                pMsgCMD->m_pPacket->m_cli_MCMD_APPEND_REPLY.m_wUserIDX);
            short nOffset = sizeof(cli_MCMD_APPEND_REPLY);
            char* szName = Packet_GetStringPtr(pMsgCMD->m_pPacket, nOffset);

            if (!pRequestUSER || !szName || _strcmpi(szName, pRequestUSER->Get_NAME())) {
                return false;
            }

            CMessenger* pMSGR1 = this->SearchMSGR(pMsgCMD->m_Name.Get());
            if (!pMSGR1 || pMSGR1->Get_FriendCNT() >= MAX_FRIEND_COUNT) {
                return false;
            }

            CMessenger* pMSGR2 = this->SearchMSGR(pRequestUSER->Get_NAME());
            if (!pMSGR2 || pMSGR2->Get_FriendCNT() >= MAX_FRIEND_COUNT
                || pMSGR1->m_dwDBID == pMSGR2->m_dwDBID) {
                return false;
            }

            this->add_friend(pMSGR1->Get_DBID(), pMSGR2->Get_DBID());
            pMSGR1->MSGR_Add(pMSGR2);
            pMSGR2->MSGR_Add(pMSGR1);
            return true;
        }

        case MSGR_CMD_DELETE: {
            CMessenger* pMSGR = this->SearchMSGR(pMsgCMD->m_Name.Get());
            if (!pMSGR) {
                return false;
            }

            DWORD id = pMsgCMD->m_pPacket->m_cli_MCMD_TAG.m_dwUserTAG;
            this->del_friend(pMSGR->Get_DBID(), id);
            pMSGR->MSGR_Del(id);
            return true;
        }

        case MSGR_CMD_REFUSE: {
            CMessenger* pMSGR = this->SearchMSGR(pMsgCMD->m_Name.Get());
            if (pMSGR)
                pMSGR->MSGR_Ban(pMsgCMD->m_pPacket->m_cli_MCMD_TAG.m_dwUserTAG);
            return true;
        }

        case MSGR_CMD_CHANGE_STATUS: {
            // ������ ����... ��ϵ� ��� ģ������...
            CMessenger* pMSGR = this->SearchMSGR(pMsgCMD->m_Name.Get());
            if (pMSGR)
                pMSGR->MSGR_Status2ALL(pMsgCMD->m_pPacket->m_cli_MCMD_STATUS_REQ.m_btStatus);
            break;
        }

        case 0x0ff: {
            CMessenger* pMSGR = this->SearchMSGR(pMsgCMD->m_Name.Get());
            if (pMSGR && FRIEND_STATUS_REFUSE_MSG != pMSGR->m_btMsgrSTATUS)
                pMSGR->MSGR_Msg(pMsgCMD->m_pPacket);
            return true;
        }
    }

    return false;
}

bool
CThreadMSGR::LogIN(tagMSGR_CMD* pCMD) {
    QueryResult res =
        this->db.query("SELECT character.id, character.name FROM character, friends "
                          "WHERE friends.friend_id = character.id AND friends.character_id=$1",
            {std::to_string(pCMD->m_dwDBID)});

    if (!res.is_ok()) {
        LOG_ERROR("Failed to query friends list for character {}: {}",
            pCMD->m_Name.Get(),
            res.error_message());
        return false;
    }

    CMessenger* pMSGR;
    pMSGR = this->SearchMSGR(pCMD->m_Name.Get());
    if (pMSGR) {
        return true;
    }

    pMSGR = this->AllocMEM();
    if (!pMSGR) {
        return false;
    }

    pMSGR->Init(pCMD->m_Name.Get(), pCMD->m_dwDBID, pCMD->m_iSocketIDX);

    std::vector<MessengerFriend> friends;
    for (size_t i = 0; i < res.row_count; ++i) {
        MessengerFriend my_friend;
        my_friend.id = res.get_int32(i, 0);
        my_friend.name = res.get_string(i, 1);
        friends.push_back(my_friend);
    }

    if (friends.size() > 0) {
        pMSGR->MSGR_LogIN(friends);
    }

    pMSGR->m_btMsgrSTATUS = FRIEND_STATUS_ONLINE;

    t_HASHKEY HashKEY = CStr::GetHASH(pCMD->m_Name.Get());
    this->m_HashMSGR.Insert(HashKEY, pMSGR);

    return true;
}

void
CThreadMSGR::LogOUT(CMessenger* pMSGR) {
    pMSGR->MSGR_LogOUT();

    t_HASHKEY HashKEY = CStr::GetHASH(pMSGR->m_Name.Get());
    this->m_HashMSGR.Delete(HashKEY, pMSGR);
    this->FreeMEM(pMSGR);
}

bool
CThreadMSGR::add_friend(uint32_t friend1_id, uint32_t friend2_id) {
    QueryResult res = this->db.query("INSERT INTO friends (character_id, friend_id) "
                                        "VALUES ($1, $2), ($2, $1) "
                                        "ON CONFLICT DO NOTHING;",
        {std::to_string(friend1_id), std::to_string(friend2_id)});

    if (!res.is_ok()) {
        LOG_ERROR("Failed to add friends for character ids '{}' and '{}': {}",
            friend1_id,
            friend2_id,
            res.error_message());
        return false;
    }

    return true;
}

bool
CThreadMSGR::del_friend(uint32_t friend1_id, uint32_t friend2_id) {
    QueryResult res = this->db.query("DELETE FROM friends WHERE (character_id=$1 AND "
                                        "friend_id=$2) OR (friend_id=$1 AND character_id=$2)",
        {std::to_string(friend1_id), std::to_string(friend2_id)});

    if (!res.is_ok()) {
        LOG_ERROR("Failed to delete friends character ids '{}' and '{}': {}",
            friend1_id,
            friend2_id,
            res.error_message());
        return false;
    }
    return true;
}

bool
CMessenger::SendPacket(int iClientSocketIDX, DWORD dwDBID, classPACKET* packet) {
    // Temporary until all packets are refactored
    classPACKET pCPacket = *packet;

    CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(iClientSocketIDX);
    if (pFindUSER && pFindUSER->m_dwDBID == dwDBID) {
        if (!pFindUSER->SendPacket(pCPacket)) {
            // ������ ����...
        }
        return true;
    }

    return false;
}

void
CMessenger::MSGR_LogIN(const std::vector<MessengerFriend>& friends) {
    classPACKET* pToFR = Packet_AllocNLock();
    classPACKET* pToME = Packet_AllocNLock();

    pToFR->m_HEADER.m_wType = WSV_MESSENGER;
    pToFR->m_HEADER.m_nSize = sizeof(wsv_MCMD_STATUS_REPLY);

    pToFR->m_wsv_MCMD_STATUS_REPLY.m_btCMD = MSGR_CMD_CHANGE_STATUS;
    pToFR->m_wsv_MCMD_STATUS_REPLY.m_dwUserTAG = this->Get_DBID();
    pToFR->m_wsv_MCMD_STATUS_REPLY.m_btStatus = FRIEND_STATUS_ONLINE;

    pToME->m_HEADER.m_wType = WSV_MESSENGER;
    pToME->m_HEADER.m_nSize = sizeof(wsv_MCMD_LIST);

    pToME->m_wsv_MCMD_LIST.m_btCMD = MSGR_CMD_LIST;
    pToME->m_wsv_MCMD_LIST.m_btFriendCNT = friends.size();

    for (const MessengerFriend& friend_data: friends) {
        char* friend_name = const_cast<char*>(friend_data.name.c_str());

        int friend_status = FRIEND_STATUS_OFFLINE;
        CMessenger* friend_messenger = g_pThreadMSGR->SearchMSGR(friend_name);
        if (friend_messenger) {
            friend_status = FRIEND_STATUS_ONLINE;
            friend_messenger->MSGR_OnOffLine(pToFR, this, this->Get_DBID(), FRIEND_STATUS_ONLINE);
        }

        CDLList<tagMyFriend>::tagNODE* pNODE = m_ListFRIEND.AllocNAppend();
        pNODE->m_VALUE.m_btSTATUS = friend_status;
        pNODE->m_VALUE.m_dwDBID = friend_data.id;
        pNODE->m_VALUE.m_pMSGR = friend_messenger;
        pNODE->m_VALUE.m_Name.Set(friend_name);

        tagFriend_H sFrStatus;
        sFrStatus.m_dwDBID = friend_data.id;
        sFrStatus.m_btSTATUS = friend_status;

        pToME->AppendData(&sFrStatus, sizeof(tagFriend_H));
        pToME->AppendString(friend_name);
        if (pToME->m_HEADER.m_nSize > MAX_PACKET_SIZE - 30) {
            this->m_bFriendUPDATE = true;
            break;
        }
    }

    this->SendPacket(this->m_iOwnerSocetIDX, this->m_dwDBID, pToME);

    Packet_ReleaseNUnlock(pToME);
    Packet_ReleaseNUnlock(pToFR);
}

void
CMessenger::MSGR_LogOUT() {
    classPACKET* pCPacket = Packet_AllocNLock();

    pCPacket->m_HEADER.m_wType = WSV_MESSENGER;
    pCPacket->m_HEADER.m_nSize = sizeof(wsv_MCMD_STATUS_REPLY);

    pCPacket->m_wsv_MCMD_STATUS_REPLY.m_btCMD = MSGR_CMD_CHANGE_STATUS;
    pCPacket->m_wsv_MCMD_STATUS_REPLY.m_dwUserTAG = this->Get_DBID();
    pCPacket->m_wsv_MCMD_STATUS_REPLY.m_btStatus = FRIEND_STATUS_OFFLINE;

    CDLList<tagMyFriend>::tagNODE* pNODE = m_ListFRIEND.GetHeadNode();
    while (pNODE) {
        CMessenger* pFindMSGR = g_pThreadMSGR->SearchMSGR(pNODE->m_VALUE.m_Name.Get());
        if (pFindMSGR) {
            pFindMSGR->MSGR_OnOffLine(pCPacket, NULL, this->Get_DBID(), FRIEND_STATUS_OFFLINE);
        }
        m_ListFRIEND.DeleteNFree(pNODE);
        pNODE = m_ListFRIEND.GetHeadNode();
    }

    Packet_ReleaseNUnlock(pCPacket);
    return;
}

bool
CMessenger::MSGR_Add(CMessenger* pFriend) {
    CDLList<tagMyFriend>::tagNODE* pNODE = m_ListFRIEND.GetHeadNode();
    while (pNODE) {
        if (pNODE->m_VALUE.m_dwDBID == pFriend->Get_DBID()) {
            pNODE->m_VALUE.m_btSTATUS = pFriend->m_btMsgrSTATUS;
            pNODE->m_VALUE.m_pMSGR = pFriend;
            this->MSGR_Status2ONE(pFriend->Get_DBID(), pFriend->m_btMsgrSTATUS, pFriend);
            return true;
        }
        pNODE = pNODE->GetNext();
    }

    CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pFriend->m_iOwnerSocetIDX);
    if (pFindUSER && pFindUSER->m_dwDBID == pFriend->m_dwDBID) {
        this->m_bFriendUPDATE = true;
        pNODE = m_ListFRIEND.AllocNAppend();

        pNODE->m_VALUE.m_btSTATUS = pFriend->m_btMsgrSTATUS;
        pNODE->m_VALUE.m_dwDBID = pFriend->Get_DBID();
        pNODE->m_VALUE.m_pMSGR = pFriend;
        pNODE->m_VALUE.m_Name.Set(pFriend->m_Name.Get());

        classPACKET* pCPacket = Packet_AllocNLock();

        pCPacket->m_HEADER.m_wType = WSV_MESSENGER;
        pCPacket->m_HEADER.m_nSize = sizeof(wsv_MCMD_APPEND_ACCEPT);

        pCPacket->m_wsv_MCMD_APPEND_ACCEPT.m_btCMD = MSGR_CMD_APPEND_ACCEPT;
        pCPacket->m_wsv_MCMD_APPEND_ACCEPT.m_dwUserTAG = pNODE->m_VALUE.m_dwDBID;
        pCPacket->m_wsv_MCMD_APPEND_ACCEPT.m_btStatus = pNODE->m_VALUE.m_btSTATUS;
        pCPacket->AppendString(pNODE->m_VALUE.m_Name.Get());

        this->SendPacket(this->m_iOwnerSocetIDX, this->m_dwDBID, pCPacket);

        Packet_ReleaseNUnlock(pCPacket);
        return true;
    }
    return false;
}

void
CMessenger::MSGR_Del(DWORD dwDBID) {
    CDLList<tagMyFriend>::tagNODE* pNODE;

    pNODE = m_ListFRIEND.GetHeadNode();
    while (pNODE) {
        if (dwDBID == pNODE->m_VALUE.m_dwDBID) {
            // ����Ŭ���̾�Ʈ �˾Ƽ� �����϶��...
            if (pNODE->m_VALUE.m_pMSGR) {
                // "����� �����Ȼ��·� �����ϰ� �ݵ�� m_pFRIEND = NULL�� ������...... " )
                pNODE->m_VALUE.m_pMSGR->MSGR_Status2ONE(this->Get_DBID(),
                    FRIEND_STATUS_DELETED,
                    NULL); // CMessenger::MSGR_Del
            }

            m_ListFRIEND.DeleteNFree(pNODE);
            this->m_bFriendUPDATE = true;
            break;
        }
        pNODE = pNODE->GetNext();
    }
}

//-------------------------------------------------------------------------------------------------
BYTE
CMessenger::MSGR_OnOffLine(classPACKET* pCPacket,
    CMessenger* pFriend,
    DWORD dwDBID,
    BYTE btNewStatus) {
    CDLList<tagMyFriend>::tagNODE* pNODE;

    pNODE = m_ListFRIEND.GetHeadNode();
    while (pNODE) {
        if (dwDBID == pNODE->m_VALUE.m_dwDBID) {
            pNODE->m_VALUE.m_pMSGR = pFriend;

            if (!(FRIEND_STATUS_REFUSED & pNODE->m_VALUE.m_btSTATUS)) {
                // ���� �ź��߰ų�, ���� ������ ���̾ƴϸ�...
                pNODE->m_VALUE.m_btSTATUS = btNewStatus;
                this->SendPacket(this->m_iOwnerSocetIDX, this->m_dwDBID, pCPacket);
                return this->m_btMsgrSTATUS;
            }

            return FRIEND_STATUS_OFFLINE;
        }
        pNODE = pNODE->GetNext();
    }

    return FRIEND_STATUS_DELETED;
}

//-------------------------------------------------------------------------------------------------
void
CMessenger::MSGR_Status2ONE(DWORD dwDBID, BYTE btNewStatus, CMessenger* pMessenger) {
    // ����Ͽ��� dwDBID�� ã�� btNewStatus���·� �ٲ۴�.
    // dwDBID�� ���� �����Ѱ��̶��... �� ����� m_pFRIEND�� NULL !!!�� �����ؾ���.
    CDLList<tagMyFriend>::tagNODE* pNODE;

    pNODE = m_ListFRIEND.GetHeadNode();
    while (pNODE) {
        if (dwDBID == pNODE->m_VALUE.m_dwDBID) { // 04. 10. 11 : 0x0013bfe����...
            pNODE->m_VALUE.m_pMSGR = pMessenger; // ���� �����Ҷ��� pMessenger = NULL�� ��

            if (!(FRIEND_STATUS_REFUSED & pNODE->m_VALUE.m_btSTATUS)) {
                // ���� �ź��� ���°ų�, ���� ���� ������ ���°� �ƴϸ�??? (���� ������ ���� ������
                // ����-������ ���)
                this->m_bFriendUPDATE =
                    (FRIEND_STATUS_REFUSED & (pNODE->m_VALUE.m_btSTATUS | btNewStatus)) ? true
                                                                                        : false;
                pNODE->m_VALUE.m_btSTATUS = btNewStatus;

                classPACKET* pCPacket = Packet_AllocNLock();

                pCPacket->m_HEADER.m_wType = WSV_MESSENGER;
                pCPacket->m_HEADER.m_nSize = sizeof(wsv_MCMD_STATUS_REPLY);

                pCPacket->m_wsv_MCMD_STATUS_REPLY.m_btCMD = MSGR_CMD_CHANGE_STATUS;
                pCPacket->m_wsv_MCMD_STATUS_REPLY.m_dwUserTAG = dwDBID;
                pCPacket->m_wsv_MCMD_STATUS_REPLY.m_btStatus = btNewStatus;

                this->SendPacket(this->m_iOwnerSocetIDX, this->m_dwDBID, pCPacket);

                Packet_ReleaseNUnlock(pCPacket);
            }
            break;
        }
        pNODE = pNODE->GetNext();
    }
}

void
CMessenger::MSGR_Msg(t_PACKET* pPacket) {
    CDLList<tagMyFriend>::tagNODE* pNODE;

    pNODE = m_ListFRIEND.GetHeadNode();
    while (pNODE) {
        if (pNODE->m_VALUE.m_dwDBID == pPacket->m_cli_MESSENGER_CHAT.m_dwUserTAG) {
            if (pNODE->m_VALUE.m_pMSGR) {
                classPACKET* pCPacket = Packet_AllocNLock();
                if (pCPacket) {
                    pCPacket->m_HEADER.m_wType = WSV_MESSENGER_CHAT;
                    pCPacket->m_HEADER.m_nSize = sizeof(wsv_MESSENGER_CHAT);
                    pCPacket->m_wsv_MESSENGER_CHAT.m_dwUserTAG = this->m_dwDBID;
                    pCPacket->AppendString(pPacket->m_cli_MESSENGER_CHAT.m_szMSG);

                    this->SendPacket(pNODE->m_VALUE.m_pMSGR->m_iOwnerSocetIDX,
                        pNODE->m_VALUE.m_pMSGR->m_dwDBID,
                        pCPacket);

                    Packet_ReleaseNUnlock(pCPacket);
                }
            }
            break;
        }

        pNODE = pNODE->GetNext();
    }
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void
CMessenger::MSGR_Ban(DWORD dwDBID) {
    /*	CDLList< tagMyFriend >::tagNODE *pNODE;

        pNODE = m_ListFRIEND.GetHeadNode ();
        while( pNODE ) {
            if ( dwDBID == pNODE->m_VALUE.m_dwDBID ) {
                // �̳��� �޼��� ����..
                if ( FRIEND_STATUS_DELETED != pNODE->m_VALUE.m_btSTATUS ) {
                    if ( FRIEND_STATUS_REFUSED & pNODE->m_VALUE.m_btSTATUS ) {
                        if ( pNODE->m_VALUE.m_pMSGR )
                            pNODE->m_VALUE.m_btSTATUS = pNODE->m_VALUE.m_pMSGR->m_btMsgrSTATUS;
                        else
                            pNODE->m_VALUE.m_btSTATUS = FRIEND_STATUS_OFFLINE;
                    } else
                        pNODE->m_VALUE.m_btSTATUS = FRIEND_STATUS_REFUSED;
                }

                #pragma COMPILE_TIME_MSG ( " �ٲ� ���� ����??? " )

                break;
            }
            pNODE = pNODE->GetNext ();
        } */
}

void
CMessenger::MSGR_Status2ALL(BYTE btNewStatus) {
    this->m_btMsgrSTATUS = btNewStatus;

    classPACKET* pCPacket = Packet_AllocNLock();

    pCPacket->m_HEADER.m_wType = WSV_MESSENGER;
    pCPacket->m_HEADER.m_nSize = sizeof(wsv_MCMD_STATUS_REPLY);

    pCPacket->m_wsv_MCMD_STATUS_REPLY.m_btCMD = MSGR_CMD_CHANGE_STATUS;
    pCPacket->m_wsv_MCMD_STATUS_REPLY.m_dwUserTAG = this->Get_DBID();
    pCPacket->m_wsv_MCMD_STATUS_REPLY.m_btStatus = btNewStatus;

    CDLList<tagMyFriend>::tagNODE* pNODE;

    pNODE = m_ListFRIEND.GetHeadNode();
    while (pNODE) {
        if (pNODE->m_VALUE.m_pMSGR) {
            this->SendPacket(pNODE->m_VALUE.m_pMSGR->m_iOwnerSocetIDX,
                pNODE->m_VALUE.m_pMSGR->m_dwDBID,
                pCPacket);
        }
        pNODE = pNODE->GetNext();
    }

    Packet_ReleaseNUnlock(pCPacket);
}

//-------------------------------------------------------------------------------------------------
#endif
