#include "stdAFX.h"
#include <fstream>

#ifdef __SHO_WS
    #include "CWS_Client.h"
    #include "CWS_Server.h"
#else
    #include "GS_ListUSER.h"
    #include "GS_SocketLSV.h"
    #include "LIB_gsMAIN.h"
#endif

#include "CThreadGUILD.h"
//#include "classLOG.h"
#include "IO_Skill.h"

#include "rose/database/database.h"

using namespace Rose;
using namespace Rose::Common;
using namespace Rose::Database;

const int CClan::s_iUserLimit[MAX_CLAN_LEVEL + 1] = {15, 15, 20, 25, 30, 36, 43, 50};
// const int CClan::s_iPosLimit [MAX_CLAN_LEVEL+1] = { 1, 7, 15, 20, 25, 32, 40, 50 };

//-------------------------------------------------------------------------------------------------
CClan::CClan(): CCriticalSection(4000) {
    m_dwClanID = 0;
}
CClan::~CClan() {
    this->Free();
}

DWORD
CClan::GetCurAbsSEC() {
    return classTIME::GetCurrentAbsSecond();
}

//-------------------------------------------------------------------------------------------------
// 클랜 해체...
void
CClan::Disband() {
    CWS_Client* pUSER;
    CDLList<CClanUSER>::tagNODE* pNode;
    this->Lock();
    {
        pNode = m_ListUSER.GetHeadNode();
        while (pNode) {
            if (pNode->m_VALUE.m_iConnSockIDX) {
                pUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pNode->m_VALUE.m_iConnSockIDX);
                if (pUSER && pUSER->GetClanID() == this->m_dwClanID) {
                    pUSER->ClanINIT(0); // 클렌 삭제..
                    pUSER->Send_wsv_CLAN_COMMAND(RESULT_CLAN_DESTROYED, NULL);
#ifdef __SHO_WS
                    // GS에 통보
                    g_pListSERVER->Send_zws_DEL_USER_CLAN(pUSER);
#endif
                }
            }

            pNode = pNode->GetNext();
        }
    }
    this->Unlock();
}

//-------------------------------------------------------------------------------------------------
bool
CClan::Send_MemberSTATUS(CClanUSER* pClanUser, BYTE btResult) {
    classPACKET* pCPacket = Packet_AllocNLock();
    if (!pCPacket)
        return false;

    tag_CLAN_MEMBER sMember;

    sMember.m_btChannelNO = pClanUser->m_btChannelNo;
    sMember.m_btClanPOS = pClanUser->m_iPosition;
    sMember.m_iClanCONTRIBUTE = pClanUser->m_iContribute;
    sMember.m_nLEVEL = pClanUser->m_nLevel;
    sMember.m_nJOB = pClanUser->m_nJob;

    pCPacket->m_HEADER.m_wType = WSV_CLAN_COMMAND;
    pCPacket->m_HEADER.m_nSize = sizeof(wsv_CLAN_COMMAND);
    pCPacket->m_wsv_CLAN_COMMAND.m_btRESULT = btResult; // RESULT_CLAN_MEMBER_STATUS;

    pCPacket->AppendData(&sMember, sizeof(tag_CLAN_MEMBER));
    pCPacket->AppendString(pClanUser->m_Name.Get());

    this->SendPacketToMEMBER(pCPacket);

    Packet_ReleaseNUnlock(pCPacket);
    return true;
}
//-------------------------------------------------------------------------------------------------
// 클랜 유저가 로그인 했다.
bool
CClan::LogIn_ClanUSER(char* szCharName, int iSenderSockIDX, int iContribute) {
    t_HASHKEY HashName = ::StrToHashKey(szCharName);

    CDLList<CClanUSER>::tagNODE* pNode;
    this->Lock();
    {
        pNode = m_ListUSER.GetHeadNode();
        while (pNode) {
            if (HashName == pNode->m_VALUE.m_HashName
                && !_strcmpi(szCharName, pNode->m_VALUE.m_Name.Get())) {
                // Find..
                CWS_Client* pUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(iSenderSockIDX);
                if (pUSER) {
                    pNode->m_VALUE.m_iContribute = iContribute; // 전체 통보 보다 앞서게...
#ifdef __SHO_WS
                    pNode->m_VALUE.m_btChannelNo = pUSER->Get_ChannelNO();
#else
                    pNode->m_VALUE.m_btChannelNo = (BYTE)::Get_ServerChannelNO();
#endif
                    // 로그인된 전체 클랜원에게 로그인 했다 통보...
                    this->Send_MemberSTATUS(&pNode->m_VALUE, RESULT_CLAN_MEMBER_LOGIN);

                    pNode->m_VALUE.m_iConnSockIDX = pUSER->m_iSocketIDX;

                    pUSER->SetClanCONTRIBUTE(iContribute);
                    pUSER->SetClanPOS(pNode->m_VALUE.m_iPosition);

                    pUSER->SetClanID(this->m_dwClanID);
                    pUSER->SetClanMARK(this->m_dwClanMARK);

                    pUSER->SetClanLEVEL(this->m_nClanLEVEL);
                    pUSER->SetClanSCORE(this->m_iClanSCORE);

                    pUSER->SetClanUserCNT(this->m_ListUSER.GetNodeCount());
                    pUSER->SetClanMONEY(this->m_biClanMONEY);
                    pUSER->SetClanRATE(this->m_iClanRATE);

                    ::CopyMemory(pUSER->m_CLAN.m_ClanBIN.m_pDATA,
                        this->m_ClanBIN.m_pDATA,
                        sizeof(tagClanBIN));

                    pUSER->SetClanNAME(this->m_Name.Get());

                    this->Send_SetCLAN(RESULT_CLAN_MY_DATA, pUSER);

                    this->Unlock();
                    return true;
                }
                break;
            }
            pNode = pNode->GetNext();
        }
    }
    this->Unlock();
    return false;
}
// 길드 유저가 로그아웃 했다.
bool
CClan::LogOut_ClanUSER(char* szCharName) {
    t_HASHKEY HashName = ::StrToHashKey(szCharName);

    CDLList<CClanUSER>::tagNODE* pNode;
    this->Lock();
    {
        pNode = m_ListUSER.GetHeadNode();
        while (pNode) {
            if (HashName == pNode->m_VALUE.m_HashName
                && !_strcmpi(szCharName, pNode->m_VALUE.m_Name.Get())) {
                // Find..
                pNode->m_VALUE.m_iConnSockIDX = 0;
                pNode->m_VALUE.m_btChannelNo = 0xff;

                // 로그인된 전체 클랜원에게 로그아웃 했다 통보...
                this->Send_MemberSTATUS(&pNode->m_VALUE, RESULT_CLAN_MEMBER_LOGOUT);

                this->Unlock();
                return true;
            }
            pNode = pNode->GetNext();
        }
    }
    this->Unlock();
    return false;
}

//-------------------------------------------------------------------------------------------------
BYTE
CClan::FindClanSKILL(short nSkillNo1, short nSkillNo2) {
#ifdef MAX_CLAN_SKILL_SLOT
    for (short nI = 0; nI < MAX_CLAN_SKILL_SLOT; nI++) {
        if (this->m_ClanBIN.m_SKILL[nI].m_nSkillIDX >= nSkillNo1
            && this->m_ClanBIN.m_SKILL[nI].m_nSkillIDX <= nSkillNo2) {
            if (SKILL_NEED_LEVELUPPOINT(this->m_ClanBIN.m_SKILL[nI].m_nSkillIDX)) {
                // 만료 날짜 체크...
                if (this->GetCurAbsSEC() >= this->m_ClanBIN.m_SKILL[nI].m_dwExpiredAbsSEC) {
                    this->m_ClanBIN.m_SKILL[nI].m_nSkillIDX = 0;
                    this->m_ClanBIN.m_SKILL[nI].m_dwExpiredAbsSEC = 0;
                    return MAX_CLAN_SKILL_SLOT;
                }
            }
            return (BYTE)nI;
        }
    }
    return MAX_CLAN_SKILL_SLOT;
#else
    return 0;
#endif
}
bool
CClan::AddClanSKILL(short nSkillNo) {
#ifdef MAX_CLAN_SKILL_SLOT
    if (MAX_CLAN_SKILL_SLOT == this->FindClanSKILL(nSkillNo, nSkillNo)) {
        // 이미등록된 스킬이 아니다...
        tagClanBIN ClanBIN;
        ::CopyMemory(&ClanBIN, &this->m_ClanBIN, sizeof(tagClanBIN));

        for (short nI = 0; nI < MAX_CLAN_SKILL_SLOT; nI++) {
            if (0 == ClanBIN.m_SKILL[nI].m_nSkillIDX) {
                ClanBIN.m_SKILL[nI].m_nSkillIDX = nSkillNo;
                // 현재시간 + 입력된 수치 * 10분까지 사용 가능..
                if (SKILL_NEED_LEVELUPPOINT(nSkillNo)) {
                    ClanBIN.m_SKILL[nI].m_dwExpiredAbsSEC =
                        this->GetCurAbsSEC() + SKILL_NEED_LEVELUPPOINT(nSkillNo) * 10 * 60;
                } else {
                    ClanBIN.m_SKILL[nI].m_dwExpiredAbsSEC = 0;
                }

                // 월드섭이나 개인섭에서만 이함수가 호출된다...
                if (!g_pThreadGUILD->Query_UpdateClanBINARY(this->m_dwClanID,
                        (BYTE*)&ClanBIN,
                        sizeof(tagClanBIN))) {
                    return false;
                }

                this->m_ClanBIN.m_SKILL[nI] = ClanBIN.m_SKILL[nI];
                this->Send_UpdateInfo();
                return true;
            }
        }
    }
#endif

    return false;
}
bool
CClan::DelClanSKILL(short nSkillNo) {
#ifdef MAX_CLAN_SKILL_SLOT
    tagClanBIN ClanBIN;
    ::CopyMemory(&ClanBIN, &this->m_ClanBIN, sizeof(tagClanBIN));

    for (short nI = 0; nI < MAX_CLAN_SKILL_SLOT; nI++) {
        if (ClanBIN.m_SKILL[nI].m_nSkillIDX == nSkillNo) {
            ClanBIN.m_SKILL[nI].m_nSkillIDX = 0;
            ClanBIN.m_SKILL[nI].m_dwExpiredAbsSEC = 0;

            // 월드섭이나 개인섭에서만 이함수가 호출된다...
            if (!g_pThreadGUILD->Query_UpdateClanBINARY(this->m_dwClanID,
                    (BYTE*)&ClanBIN,
                    sizeof(tagClanBIN))) {
                return false;
            }

            this->m_ClanBIN.m_SKILL[nI] = ClanBIN.m_SKILL[nI];
            this->Send_UpdateInfo();

            return true;
        }
    }
#endif
    return false;
}

void
CClan::SetJOBnLEV(t_HASHKEY HashCHAR, char* szCharName, short nJob, short nLev) {
    t_HASHKEY HashName = ::StrToHashKey(szCharName);

    CDLList<CClanUSER>::tagNODE* pNode;
    this->Lock();
    {
        pNode = m_ListUSER.GetHeadNode();
        while (pNode) {
            if (HashName == pNode->m_VALUE.m_HashName
                && !_strcmpi(szCharName, pNode->m_VALUE.m_Name.Get())) {
                // Find..
                pNode->m_VALUE.m_nLevel = nLev;
                pNode->m_VALUE.m_nJob = nJob;

                classPACKET* pCPacket = Packet_AllocNLock();
                if (pCPacket) {
                    pCPacket->m_HEADER.m_wType = WSV_CLAN_COMMAND;
                    pCPacket->m_HEADER.m_nSize = sizeof(wsv_CLAN_MEMBER_JOBnLEV);
                    pCPacket->m_wsv_CLAN_MEMBER_JOBnLEV.m_btRESULT = RESULT_CLAN_MEMBER_JOBnLEV;

                    pCPacket->m_wsv_CLAN_MEMBER_JOBnLEV.m_nLEVEL = nLev;
                    pCPacket->m_wsv_CLAN_MEMBER_JOBnLEV.m_nJOB = nJob;
                    pCPacket->AppendString(szCharName);

                    this->SendPacketToMEMBER(pCPacket);

                    Packet_ReleaseNUnlock(pCPacket);
                }

                this->Unlock();
                return;
            }
            pNode = pNode->GetNext();
        }
    }
    this->Unlock();
}

//-------------------------------------------------------------------------------------------------
// bool CClan::Send_AdjCLAN ()
//{
////RESULT_CLAN_INFO xxx
//}

bool
CClan::Send_SetCLAN(BYTE btCMD, CWS_Client* pMember) {
#ifdef __SHO_WS
    // GS에 사용자 클랜 설정 통보...
    g_pListSERVER->Send_zws_SET_USER_CLAN(pMember);
#endif

    if (this->m_Motd.Get()) {
        if (this->m_Desc.Get()) {
            pMember->Send_wsv_CLAN_COMMAND(btCMD,
                sizeof(tag_MY_CLAN),
                &pMember->m_CLAN,
                this->m_Name.BuffLength() + 1,
                this->m_Name.Get(),
                this->m_Desc.BuffLength() + 1,
                this->m_Desc.Get(),
                this->m_Motd.BuffLength() + 1,
                this->m_Motd.Get(),
                NULL);
        } else {
            pMember->Send_wsv_CLAN_COMMAND(btCMD,
                sizeof(tag_MY_CLAN),
                &pMember->m_CLAN,
                this->m_Name.BuffLength() + 1,
                this->m_Name.Get(),
                1,
                " ",
                this->m_Motd.BuffLength() + 1,
                this->m_Motd.Get(),
                NULL);
        }
    } else {
        if (this->m_Desc.Get()) {
            pMember->Send_wsv_CLAN_COMMAND(btCMD,
                sizeof(tag_MY_CLAN),
                &pMember->m_CLAN,
                this->m_Name.BuffLength() + 1,
                this->m_Name.Get(),
                this->m_Desc.BuffLength() + 1,
                this->m_Desc.Get(),
                NULL);
        } else {
            pMember->Send_wsv_CLAN_COMMAND(btCMD,
                sizeof(tag_MY_CLAN),
                &pMember->m_CLAN,
                this->m_Name.BuffLength() + 1,
                this->m_Name.Get(),
                1,
                " ",
                NULL);
        }
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
// 길드 유저 추가...
bool
CClan::Insert_MEMBER(BYTE btResult, CWS_Client* pMember, int iClanPos, char* szMaster) {
    CDLList<CClanUSER>::tagNODE* pNode;

    this->Lock();
    {
        if (m_ListUSER.GetNodeCount() < this->GetUserLimitCNT()) {
            if (g_pThreadGUILD->Query_InsertClanMember(pMember->Get_NAME(),
                    this->m_dwClanID,
                    iClanPos)) {

                pNode = m_ListUSER.AllocNAppend();

                pNode->m_VALUE.m_Name.Set(pMember->Get_NAME());
                pNode->m_VALUE.m_HashName = ::StrToHashKey(pNode->m_VALUE.m_Name.Get());
                pNode->m_VALUE.m_iPosition = iClanPos;
                pNode->m_VALUE.m_iConnSockIDX = pMember->m_iSocketIDX;
#ifdef __SHO_WS
                pNode->m_VALUE.m_btChannelNo = pMember->Get_ChannelNO();
#else
                pNode->m_VALUE.m_btChannelNo = (BYTE)::Get_ServerChannelNO();
#endif

                this->m_nPosCNT[iClanPos]++; // 신규 멤버 추가

                pMember->ClanINIT(this->m_dwClanID, iClanPos);
                pMember->SetClanMARK(this->m_dwClanMARK);
                pMember->SetClanNAME(this->m_Name.Get());

                pMember->SetClanLEVEL(this->m_nClanLEVEL);
                pMember->SetClanSCORE(this->m_iClanSCORE);

                pMember->SetClanUserCNT(this->m_ListUSER.GetNodeCount());
                pMember->SetClanMONEY(this->m_biClanMONEY);
                pMember->SetClanRATE(this->m_iClanRATE);

                ::CopyMemory(pMember->m_CLAN.m_ClanBIN.m_pDATA,
                    this->m_ClanBIN.m_pDATA,
                    sizeof(tagClanBIN));

                this->Send_SetCLAN(btResult, pMember);

                if (szMaster) {
                    this->Send_wsv_CLAN_COMMAND(RESULT_CLAN_JOIN_MEMBER,
                        pMember->m_Name.BuffLength() + 1,
                        pMember->Get_NAME(),
                        strlen(szMaster) + 1,
                        szMaster,
                        NULL);
                } // else 창설됨과 동시에 추가 되는 창설자

                this->Unlock();
                return true;
            }
        }
    }
    this->Unlock();
    return false;
}

// 길드 유저 삭제...
bool
CClan::Delete_MEMBER(t_HASHKEY HashCommander, char* szCharName, BYTE btCMD, char* szKicker) {
    // 1. 접속해 있으면 길드 삭제 -> GS통보
    // 2. 길드원 수 감소.
    // 3. 길드 멤버 DB 삭제.
    t_HASHKEY HashName = ::StrToHashKey(szCharName);
    CDLList<CClanUSER>::tagNODE* pNode;
    this->Lock();
    {
        pNode = m_ListUSER.GetHeadNode();
        while (pNode) {
            if (HashName == pNode->m_VALUE.m_HashName
                && !_strcmpi(szCharName, pNode->m_VALUE.m_Name.Get())) {
                if (HashName == HashCommander) {
                    // 자기 자신은 안돼 !!!!
                    this->Unlock();
                    return false;
                }

                // 동료 길드원에게 통보~~~
                if (szKicker) {
                    this->Send_wsv_CLAN_COMMAND(btCMD,
                        strlen(szCharName) + 1,
                        szCharName,
                        strlen(szKicker) + 1,
                        szKicker,
                        NULL);
                } else {
                    this->Send_wsv_CLAN_COMMAND(btCMD, strlen(szCharName) + 1, szCharName, NULL);
                }

                // Find..
                // szCharName이 접속된 상태라면 GS에 통보 및 클랜 삭제통보
                CWS_Client* pUSER = g_pUserLIST->Find_CHAR(szCharName);
                if (pUSER) {
                    // 보내고 초기화...
                    pUSER->ClanINIT();
#ifdef __SHO_WS
                    // GS에 통보
                    g_pListSERVER->Send_zws_DEL_USER_CLAN(pUSER);
#endif
                }

                if (g_pThreadGUILD->Query_DeleteClanMember(szCharName)) {
                    this->m_nPosCNT[pNode->m_VALUE.m_iPosition]--;

                    m_ListUSER.DeleteNFree(pNode);
                }
                this->Unlock();
                return true;
            }
            pNode = pNode->GetNext();
        }
    }
    this->Unlock();
    return false;
}

//-------------------------------------------------------------------------------------------------
bool
CClan::Adjust_MEMBER(t_HASHKEY HashCommander,
    char* szCharName,
    int iAdjContr,
    int iAdjPos,
    char* szMaster,
    int iMaxPos,
    int iMinPos,
    bool bCheckPosLimit) {
    t_HASHKEY HashName = ::StrToHashKey(szCharName);
    bool bResult = false;

    CDLList<CClanUSER>::tagNODE* pNode;
    this->Lock();
    {
        pNode = m_ListUSER.GetHeadNode();
        while (pNode) {
            if (HashName == pNode->m_VALUE.m_HashName
                && !_strcmpi(szCharName, pNode->m_VALUE.m_Name.Get())) {
                if (HashName == HashCommander) {
                    // 자기 자신은 안돼 !!!!
                    goto _JUMP_RETURN;
                }
                if (iMinPos && pNode->m_VALUE.m_iPosition < iMinPos) {
                    // 최하 자격은 iMinPos되야함~~~
                    goto _JUMP_RETURN;
                }

                // Find..
                if (pNode->m_VALUE.m_iPosition + iAdjPos > GPOS_MASTER)
                    iAdjPos = GPOS_MASTER - pNode->m_VALUE.m_iPosition;
                if (iAdjPos > iMaxPos) {
                    goto _JUMP_RETURN;
                }

                if (pNode->m_VALUE.m_iPosition + iAdjPos >= 0) {
                    if (bCheckPosLimit) {
                        // 등급별 최대 인원 체크...
                        int iNewPos = pNode->m_VALUE.m_iPosition + iAdjPos;
                        if (this->m_nPosCNT[iNewPos] >= this->GetPositionLimitCNT(iNewPos)) {
                            goto _JUMP_RETURN;
                        }
                    }

                    if (g_pThreadGUILD->Query_AdjustClanMember(szCharName, iAdjContr, iAdjPos)) {
                        this->m_nPosCNT[pNode->m_VALUE.m_iPosition]--; // 직위 이동

                        pNode->m_VALUE.m_iContribute += iAdjContr;
                        pNode->m_VALUE.m_iPosition += iAdjPos;
                        bResult = true;

                        this->m_nPosCNT[pNode->m_VALUE.m_iPosition]++; // 직위 이동

                        // 캡틴 이상으로 승격됐으면.. 전길드원에 통보
                        this->Send_MemberSTATUS(&pNode->m_VALUE, RESULT_CLAN_MEMBER_POSITION);

                        // szCharName이 접속된 상태라면
                        // TODO:: GS에 통보 및 클랜 삭제통보
                        CWS_Client* pUSER = g_pUserLIST->Find_CHAR(szCharName);
                        if (pUSER) {
                            pUSER->SetClanPOS(pNode->m_VALUE.m_iPosition);
                            BYTE btPos = (BYTE)pNode->m_VALUE.m_iPosition;
                            pUSER->Send_wsv_CLAN_COMMAND(RESULT_CLAN_POSITION,
                                strlen(szMaster) + 1,
                                (BYTE*)szMaster,
                                sizeof(BYTE),
                                &btPos,
                                NULL);
                        }
                    }
                }
                this->Unlock();
            _JUMP_RETURN:
                return bResult;
            }
            pNode = pNode->GetNext();
        }
    }
    this->Unlock();
    return false;
}
//-------------------------------------------------------------------------------------------------
// 길드 유저 갱신...
bool
CClan::Update_MEMBER() {
    return true;
}

//-------------------------------------------------------------------------------------------------
void
CClan::Send_UpdateInfo() {
    tag_MY_CLAN sInfo;

    sInfo.m_biClanMONEY = this->m_biClanMONEY;
    sInfo.m_btClanLEVEL = (BYTE)this->m_nClanLEVEL;
    sInfo.m_dwClanID = this->m_dwClanID;
    sInfo.m_dwClanMARK = this->m_dwClanMARK;
    sInfo.m_iClanRATE = this->m_iClanRATE;
    sInfo.m_iClanSCORE = this->m_iClanSCORE;
    ::CopyMemory(sInfo.m_ClanBIN.m_pDATA, this->m_ClanBIN.m_pDATA, sizeof(tagClanBIN));

    //	sInfo.m_nMemberCNT	= 0;
    //	sInfo.m_btClanPOS	= xxx;
    //	sInfo.m_iClanCONT	= xxx;

    // 전체 클랜 멤버에 전송...
    this->Send_wsv_CLAN_COMMAND(RESULT_CLAN_INFO, sizeof(tag_MY_CLAN), &sInfo, NULL);

    // 각 채널서버에 갱신된 내용을 전달해야해~~~~
    CDLList<CClanUSER>::tagNODE* pNODE;
    CWS_Client* pUSER;
    this->Lock();
    {
        /*
        struct tag_CLAN_ID {
            DWORD	m_dwClanID;
            union {
                DWORD	m_dwClanMARK;
                WORD	m_wClanMARK[2];
            } ;
            BYTE	m_btClanLEVEL;		// 클랜 레벨
            BYTE	m_btClanPOS;		// 클랜에서의 직위
        } ;
        struct tag_MY_CLAN : public tag_CLAN_ID {
            int		m_iClanSCORE;		// 클랜 포인트
            int		m_iClanRATE;		// 창고효율
            __int64	m_biClanMONEY;
            short	m_nMemberCNT;		// 멤버 수
            int		m_iClanCONT;		// 클랜 기여도
            // char m_szClanName[];
            // char m_szClanDESC[];
            // char m_szClanMOTD[];
        } ;
        */
        pNODE = m_ListUSER.GetHeadNode();
        while (pNODE) {
            if (pNODE->m_VALUE.m_iConnSockIDX) {
                pUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pNODE->m_VALUE.m_iConnSockIDX);
                if (pUSER && this->m_dwClanID == pUSER->GetClanID()) {
                    pUSER->SetClanMONEY(sInfo.m_biClanMONEY);
                    pUSER->SetClanLEVEL(sInfo.m_btClanLEVEL);
                    pUSER->SetClanID(sInfo.m_dwClanID);
                    pUSER->SetClanMARK(sInfo.m_dwClanMARK);
                    pUSER->SetClanRATE(sInfo.m_iClanRATE);
                    pUSER->SetClanSCORE(sInfo.m_iClanSCORE);
                    ::CopyMemory(pUSER->m_CLAN.m_ClanBIN.m_pDATA,
                        this->m_ClanBIN.m_pDATA,
                        sizeof(tagClanBIN));

                    this->Send_SetCLAN(RESULT_CLAN_MY_DATA, pUSER);
                }
            }
            pNODE = pNODE->GetNext();
        }
    }
    this->Unlock();
    /*
        classPACKET *pCPacket = Packet_AllocNLock ();
        if ( !pCPacket )
            return;

        pCPacket->m_HEADER.m_wType = WSV_CLAN_COMMAND;
        pCPacket->m_HEADER.m_nSize = sizeof( wsv_CLAN_COMMAND );
        pCPacket->m_wsv_CLAN_COMMAND.m_btRESULT = RESULT_CLAN_INFO;

        tag_CLAN_INFO sInfo;

        sInfo.m_nClanLEVEL	= this->m_nClanLEVEL;
        sInfo.m_iClanSCORE	= this->m_iClanSCORE;
        sInfo.m_iClanRATE	= this->m_iClanRATE;
        sInfo.m_biClanMONEY = this->m_biClanMONEY;

        pCPacket->AppendData( &sInfo, sizeof( tag_CLAN_INFO ) );
        pCPacket->AppendString( this->m_Name.Get() );
        pCPacket->AppendString( this->m_Desc.Get() );

        //CClan *pAllied;
        //for (int iC=0; iC<MAX_ALLIED_CLANS; iC++) {
        //	if ( this->m_dwAlliedClanID[ iC ] ) {
        //		pAllied = g_pThreadGUILD->Find_CLAN( this->m_dwAlliedClanID[ iC ] );
        //	}
        //}

        g_pUserLIST->SendPacketToSocketIDX( iSenderSockIDX, pCPacket );
        Packet_ReleaseNUnlock( pCPacket );
    */
}

void
CClan::SendRoster(int iSenderSockIDX) {
    classPACKET pCPacket;
    pCPacket.m_HEADER.m_wType = WSV_CLAN_COMMAND;
    pCPacket.m_HEADER.m_nSize = sizeof(wsv_CLAN_COMMAND);
    pCPacket.m_wsv_CLAN_COMMAND.m_btRESULT = RESULT_CLAN_ROSTER;

    CDLList<CClanUSER>::tagNODE* pNODE;
    tag_CLAN_MEMBER sMember;
    this->Lock();
    {
        pNODE = m_ListUSER.GetHeadNode();
        while (pNODE) {
            sMember.m_btClanPOS = pNODE->m_VALUE.m_iPosition;
            sMember.m_iClanCONTRIBUTE = pNODE->m_VALUE.m_iContribute;
            sMember.m_btChannelNO = pNODE->m_VALUE.m_btChannelNo;
            sMember.m_nLEVEL = pNODE->m_VALUE.m_nLevel;
            sMember.m_nJOB = pNODE->m_VALUE.m_nJob;

            pCPacket.AppendData(&sMember, sizeof(tag_CLAN_MEMBER));
            pCPacket.AppendString(pNODE->m_VALUE.m_Name.Get());

            if (pCPacket.m_HEADER.m_nSize >= MAX_PACKET_SIZE - 34) {
                g_pUserLIST->SendPacketToSocketIDX(iSenderSockIDX, pCPacket);

                pCPacket = classPACKET();
                pCPacket.m_HEADER.m_wType = WSV_CLAN_COMMAND;
                pCPacket.m_HEADER.m_nSize = sizeof(wsv_CLAN_COMMAND);
                pCPacket.m_wsv_CLAN_COMMAND.m_btRESULT = RESULT_CLAN_ROSTER;
            }
            pNODE = pNODE->GetNext();
        }
    }
    this->Unlock();

    if (pCPacket.m_HEADER.m_nSize > sizeof(wsv_CLAN_COMMAND)) {
        g_pUserLIST->SendPacketToSocketIDX(iSenderSockIDX, pCPacket);
    };
}

//-------------------------------------------------------------------------------------------------
void
CClan::SendPacketToMEMBER(classPACKET* packet) {
    // Temporary until all packets are refactored
    classPACKET pCPacket = *packet;

    CDLList<CClanUSER>::tagNODE* pNODE;
    CWS_Client* pUSER;
    this->Lock();
    {
        pNODE = m_ListUSER.GetHeadNode();
        while (pNODE) {
            if (pNODE->m_VALUE.m_iConnSockIDX) {
                pUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pNODE->m_VALUE.m_iConnSockIDX);
                if (pUSER && this->m_dwClanID == pUSER->GetClanID())
                    pUSER->SendPacket(pCPacket);
            }
            pNODE = pNODE->GetNext();
        }
    }
    this->Unlock();
}

// param :: CMD, Len, Ptr
bool
CClan::Send_wsv_CLAN_COMMAND(BYTE btCMD, ...) {
    classPACKET* pCPacket = Packet_AllocNLock();
    if (!pCPacket)
        return false;

    pCPacket->m_HEADER.m_wType = WSV_CLAN_COMMAND;
    pCPacket->m_HEADER.m_nSize = sizeof(wsv_CLAN_COMMAND);

    pCPacket->m_wsv_CLAN_COMMAND.m_btRESULT = btCMD;

    va_list va;
    va_start(va, btCMD);
    {
        short nDataLen;
        BYTE* pDataPtr;
        while ((nDataLen = va_arg(va, short)) != NULL) {
            pDataPtr = va_arg(va, BYTE*);
            pCPacket->AppendData(pDataPtr, nDataLen);
        }
    }
    va_end(va);

    this->SendPacketToMEMBER(pCPacket);

    Packet_ReleaseNUnlock(pCPacket);
    return true;
}

bool
CClan::SetClanMARK(BYTE* pMarkData, short nDataLen, WORD wMarkCRC16, CWS_Client* pReqUSER) {
    // WORD wCRC = classCRC::DataCRC16( pMarkData, nDataLen );
    //	클랜 마크를 디비에 기록....
    WORD wResult = g_pThreadGUILD->Query_UpdateClanMARK(this, wMarkCRC16, pMarkData, nDataLen);
    if (wResult) {
        classPACKET pCPacket;
        pCPacket.m_HEADER.m_wType = WSV_CLANMARK_REPLY;
        pCPacket.m_HEADER.m_nSize = sizeof(wsv_CLANMARK_REPLY);

        pCPacket.m_wsv_CLANMARK_REPLY.m_dwClanID = 0;
        pCPacket.m_wsv_CLANMARK_REPLY.m_wMarkCRC16 = wResult;

        pReqUSER->SendPacket(pCPacket);
        return false;
    }

    classPACKET pCPacket;

    this->m_wClanMARK[0] = 0;
    this->m_wClanMARK[1] = wMarkCRC16;

    this->m_nClanMarkLEN = nDataLen;
    ::CopyMemory(this->m_pClanMARK, pMarkData, nDataLen);

    pCPacket.m_HEADER.m_wType = WSV_CLANMARK_REPLY;
    pCPacket.m_HEADER.m_nSize = sizeof(wsv_CLANMARK_REPLY);

    pCPacket.m_wsv_CLANMARK_REPLY.m_dwClanID = this->m_dwClanID;
    pCPacket.m_wsv_CLANMARK_REPLY.m_wMarkCRC16 = wMarkCRC16;
    pCPacket.AppendData(pMarkData, nDataLen);

    // 클랜의 모든 멤버에게...
    CDLList<CClanUSER>::tagNODE* pNODE;
    CWS_Client* pUSER;
    this->Lock();
    {
        pNODE = m_ListUSER.GetHeadNode();
        while (pNODE) {
            if (pNODE->m_VALUE.m_iConnSockIDX) {
                pUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pNODE->m_VALUE.m_iConnSockIDX);
                if (pUSER && this->m_dwClanID == pUSER->GetClanID()) {
                    pUSER->SetClanMARK(m_dwClanMARK);
                    pUSER->SendPacket(pCPacket);
#ifdef __SHO_WS
                    // 마크변경된 클랜주변의 사용자들을 위해서... GS에 마크ID통보=>CLI에 전송=>WS에
                    // 요청
                    g_pListSERVER->Send_zws_SET_USER_CLAN(pUSER);
#endif
                }
            }
            pNODE = pNODE->GetNext();
        }
    }
    this->Unlock();

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CClan::Send_wsv_CLANMARK_REG_TIME(CWS_Client* pMember) {
    classPACKET pCPacket;
    pCPacket.m_HEADER.m_wType = WSV_CLANMARK_REG_TIME;
    pCPacket.m_HEADER.m_nSize = sizeof(wsv_CLANMARK_REG_TIME);

    pCPacket.m_wsv_CLANMARK_REG_TIME.m_wYear = this->m_RegTIME.m_wYear;
    pCPacket.m_wsv_CLANMARK_REG_TIME.m_btMon = this->m_RegTIME.m_btMon;
    pCPacket.m_wsv_CLANMARK_REG_TIME.m_btDay = this->m_RegTIME.m_btDay;
    pCPacket.m_wsv_CLANMARK_REG_TIME.m_btHour = this->m_RegTIME.m_btHour;
    pCPacket.m_wsv_CLANMARK_REG_TIME.m_btMin = this->m_RegTIME.m_btMin;
    pCPacket.m_wsv_CLANMARK_REG_TIME.m_btSec = this->m_RegTIME.m_btSec;

    pMember->SendPacket(pCPacket);
    return true;
}

CThreadGUILD::CThreadGUILD(UINT uiInitDataCNT, UINT uiIncDataCNT):
    CSqlTHREAD(true), m_Pools((char*)"CGuildPOOL", uiInitDataCNT, uiIncDataCNT) {
    m_pHashCLAN = new classHASH<CClan*>(1024 * 2);
}

CThreadGUILD::~CThreadGUILD() {
    SAFE_DELETE(m_pHashCLAN);
}

void
CThreadGUILD::Execute() {
    CDLList<tagCLAN_CMD>::tagNODE* pCmdNODE;

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
            this->Run_GuildPACKET(&pCmdNODE->m_VALUE);

            SAFE_DELETE_ARRAY(pCmdNODE->m_VALUE.m_pPacket);
            m_ProcCMD.DeleteNFree(pCmdNODE);
            pCmdNODE = m_ProcCMD.GetHeadNode();
        }
    }

    int iCnt = m_AddPACKET.GetNodeCount();
    _ASSERT(iCnt == 0);
}

//-------------------------------------------------------------------------------------------------
bool
CThreadGUILD::Add_ClanCMD(BYTE btCMD, int iSocketIDX, t_PACKET* pPacket, char* szCharName) {
    CDLList<tagCLAN_CMD>::tagNODE* pNewNODE;

    pNewNODE = new CDLList<tagCLAN_CMD>::tagNODE;

    pNewNODE->m_VALUE.m_btCMD = btCMD;
    pNewNODE->m_VALUE.m_iSocketIDX = iSocketIDX;
    if (pPacket) {
        // 2005.05.10 패킷 구조가 틀린것이 있는데... pPacket->m_cli_CLAN_COMMAND.m_btCMD ??????
        switch (btCMD) {
            case GCMD_CLANMARK_SET:
            case GCMD_CLANMARK_GET:
            case GCMD_CLANMARK_REGTIME:
                break;
            default:
                if (pPacket->m_cli_CLAN_COMMAND.m_btCMD >= GCMD_LOGOUT) {
                    if (NULL == szCharName) {
                        delete pNewNODE;
                        return false;
                    }
                    pNewNODE->m_VALUE.m_CharNAME.Set(szCharName);
                } else if (szCharName) {
                    pNewNODE->m_VALUE.m_CharNAME.Set(szCharName);
                }
                break;
        }

        pNewNODE->m_VALUE.m_pPacket = (t_PACKET*)new BYTE[pPacket->m_HEADER.m_nSize];
        ::CopyMemory(pNewNODE->m_VALUE.m_pPacket, pPacket, pPacket->m_HEADER.m_nSize);
    } else {
        pNewNODE->m_VALUE.m_CharNAME.Set(szCharName);
        pNewNODE->m_VALUE.m_pPacket = NULL;
    }

    this->m_CS.Lock();
    m_WaitCMD.AppendNode(pNewNODE);
    this->m_CS.Unlock();

    this->Set_EVENT();

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CThreadGUILD::Run_GuildPACKET(tagCLAN_CMD* pGuildCMD) {
    short nOffset = sizeof(cli_CLAN_COMMAND);
    switch (pGuildCMD->m_btCMD) {
        case GCMD_CLANMARK_SET: {
            CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
            if (pFindUSER->GetClanID() && pFindUSER->GetClanPOS() >= GPOS_MASTER) {
                CClan* pClan = this->Find_CLAN(pFindUSER->GetClanID());
                if (pClan) {
                    short nMarkLen =
                        pGuildCMD->m_pPacket->m_HEADER.m_nSize - sizeof(cli_CLANMARK_SET);
                    BYTE* pMark = &pGuildCMD->m_pPacket->m_pDATA[sizeof(cli_CLANMARK_SET)];
                    pClan->SetClanMARK(pMark,
                        nMarkLen,
                        pGuildCMD->m_pPacket->m_cli_CLANMARK_SET.m_wMarkCRC16,
                        pFindUSER);
                }
            }
            break;
        }
        case GCMD_CLANMARK_GET: {
            CClan* pClan = this->Find_CLAN(pGuildCMD->m_pPacket->m_cli_CLANMARK_REQ.m_dwClanID);
            if (pClan) {
                CWS_Client* pFindUSER =
                    (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
                if (pFindUSER) {
                    pFindUSER->Send_wsv_CLANMARK_REPLY(pClan->m_dwClanID,
                        pClan->m_wClanMARK[1],
                        pClan->m_pClanMARK,
                        pClan->m_nClanMarkLEN);
                    return true;
                }
            }
            break;
        }
        case GCMD_CLANMARK_REGTIME: {
            CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
            if (pFindUSER->GetClanID()) {
                CClan* pClan = this->Find_CLAN(pFindUSER->GetClanID());
                if (pClan) {
                    return pClan->Send_wsv_CLANMARK_REG_TIME(pFindUSER);
                }
            }
            break;
        }
        case GCMD_ADJ_VAR: {
#ifdef __SHO_WS
            CClan* pClan = this->Find_CLAN(pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_dwClanID);
            if (pClan) {
                switch (pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_btVarType) {
                    case CLVAR_INC_LEV:
                        pClan->m_nClanLEVEL = pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_iAdjValue;
                        pClan->Send_UpdateInfo();
                        break;
                    case CLVAR_ADD_SCORE:
                        pClan->m_iClanSCORE = pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_iAdjValue;
                        pClan->Send_UpdateInfo();
                        break;

                    case CLVAR_ADD_ZULY:
                        pClan->m_biClanMONEY = pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_biResult;
                        pClan->Send_UpdateInfo();
                        break;
                    case CLVAR_ADD_SKILL: {
                        CClan* pClan =
                            this->Find_CLAN(pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_dwClanID);
                        if (pClan) {
                            pClan->AddClanSKILL(
                                pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_iAdjValue);
                        }
                        break;
                    }
                    case CLVAR_DEL_SKILL: {
                        CClan* pClan =
                            this->Find_CLAN(pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_dwClanID);
                        if (pClan) {
                            pClan->DelClanSKILL(
                                pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_iAdjValue);
                        }
                        break;
                    }
                }
            }
#else
            switch (pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_btVarType) {
                case CLVAR_INC_LEV:
                    if (!this->update_clan_data("level",
                            pGuildCMD->m_pPacket)) // CLVAR_INC_LEV, 1 ) )
                        return true;
                    break;
                case CLVAR_ADD_SCORE:
                    if (!this->update_clan_data("points",
                            pGuildCMD
                                ->m_pPacket)) // CLVAR_ADD_SCORE,
                                              // pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_iAdjValue
                                              // ) )
                        return true;
                    break;
                case CLVAR_ADD_ZULY:
                    if (!this->update_clan_data("money",
                            pGuildCMD
                                ->m_pPacket)) // CLVAR_ADD_ZULY,
                                              // pGuildCMD->m_pPacket->m_gsv_ADJ_CLAN_VAR.m_iAdjValue
                                              // ) )
                        return true;
                    break;
            }
            g_pSockLSV->Send_gsv_ADJ_CLAN_VAR(pGuildCMD->m_pPacket);
#endif
            return true;
        }
        case GCMD_CHAT: // 클랜 챗
        {
            char* szMsg;
            short nOffset = sizeof(cli_CLAN_CHAT);

            szMsg = Packet_GetStringPtr(pGuildCMD->m_pPacket, nOffset);
            if (szMsg) {
                CClan* pClan = this->Find_CLAN(pGuildCMD->m_iSocketIDX);
                if (pClan) {
                    classPACKET* pCPacket = Packet_AllocNLock();
                    if (!pCPacket)
                        return false;

                    pCPacket->m_HEADER.m_wType = WSV_CLAN_CHAT;
                    pCPacket->m_HEADER.m_nSize = sizeof(wsv_CLAN_CHAT);
                    pCPacket->AppendString(pGuildCMD->m_CharNAME.Get());
                    pCPacket->AppendString(szMsg);

                    pClan->SendPacketToMEMBER(pCPacket);

                    Packet_ReleaseNUnlock(pCPacket);
                }
            }
            return true;
        }
        case GCMD_LOGIN: // 사용자 로그인
        {
            this->Query_LoginClanMember(pGuildCMD->m_CharNAME.Get(), pGuildCMD->m_iSocketIDX);
            return true;
        }
        case GCMD_LOGOUT: // 사용자 로그아웃
        {
            CClan* pClan = this->Find_CLAN((DWORD)pGuildCMD->m_iSocketIDX);
            if (pClan)
                pClan->LogOut_ClanUSER(pGuildCMD->m_CharNAME.Get());
            return true;
        }
        case GCMD_CREATE: // 길드 생성 :: GS에서 처리
        {
#ifndef __SHO_WS
            CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
            if (pFindUSER && 0 == pFindUSER->GetClanID()) {
                CClan* pClan =
                    this->Query_CreateCLAN(pGuildCMD->m_iSocketIDX, pGuildCMD->m_pPacket);
                if (pClan) {
                    if (pClan->Insert_MEMBER(RESULT_CLAN_CREATE_OK, pFindUSER, GPOS_MASTER)) {
                        if (!pFindUSER->CheckClanCreateCondition(
                                1)) { // 클랜 생성 시도시 생성된 조건 검증...
                            // 접속 종료된 넘...
                            this->Query_DeleteCLAN(pClan->m_Name.Get());
                            pFindUSER->UnlockSOCKET();
                            return true;
                        }

                        g_pSockLSV->Send_zws_CREATE_CLAN(pFindUSER->m_dwWSID,
                            pFindUSER->m_HashCHAR);
                        return true;
                    } else {
                        this->Query_DeleteCLAN(pClan->m_Name.Get());
                        pFindUSER->Send_wsv_CLAN_COMMAND(RESULT_CLAN_CREATE_NO_RIGHT, NULL);
                    }
                } else {
                    // 실패...
                    pFindUSER->Send_wsv_CLAN_COMMAND(RESULT_CLAN_CREATE_DUP_NAME, NULL);
                }

                pFindUSER->CheckClanCreateCondition(2); // 클랜 생성 실패 :: 감소된 조건 복원...
            }
#endif
            return true;
        }
        case GCMD_DISBAND: // 길드 해체
        {
            CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
            if (pFindUSER->GetClanID() && pFindUSER->GetClanPOS() >= GPOS_MASTER) {
                CClan* pClan = this->Find_CLAN(pFindUSER->GetClanID());
                if (pClan && pClan->m_ListUSER.GetNodeCount() < 2) {
                    if (this->Query_DeleteCLAN(pClan->m_Name.Get())) {
                    } else {
                        pFindUSER->Send_wsv_CLAN_COMMAND(RESULT_CLAN_DESTROY_FAILED, NULL);
                    }
                }
            }
            return true;
        }
        case GCMD_INVITE_REPLY_YES: // 길드 초대 승락
        case GCMD_INVITE_REPLY_NO: // 길드 초대 거부
        {
            short nOffset = sizeof(cli_CLAN_COMMAND);
            char* pOwnerName = Packet_GetStringPtr(pGuildCMD->m_pPacket, nOffset);
            CWS_Client* pOwner = g_pUserLIST->Find_CHAR(pOwnerName);
            if (pOwner && pOwner->GetClanPOS() >= GPOS_SUB_MASTER) {
                CWS_Client* pReplyer = (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
                if (pReplyer && 0 == pReplyer->GetClanID()) {
                    if (pGuildCMD->m_btCMD == GCMD_INVITE_REPLY_YES) {
                        CClan* pClan = this->Find_CLAN(pOwner->GetClanID());
                        if (pClan) {
                            if (!pClan->Insert_MEMBER(RESULT_CLAN_JOIN_OK,
                                    pReplyer,
                                    GPOS_JUNIOR,
                                    pOwner->Get_NAME())) {
                                pOwner->Send_wsv_CLAN_COMMAND(RESULT_CLAN_JOIN_MEMBER_FULL, NULL);
                            }
                        }
                    } else
                        pOwner->Send_wsv_CLAN_COMMAND(GCMD_INVITE_REPLY_NO,
                            pReplyer->m_Name.BuffLength() + 1,
                            pReplyer->Get_NAME(),
                            NULL);
                }
            }
            return true;
        }
        case GCMD_REMOVE: // 길드 추방
        case GCMD_PROMOTE: // 길드 승급
        case GCMD_DEMOTE: // 길드 강등
        case GCMD_LEADER: // 길드 위임
        case GCMD_MOTD: // 길드 공지
        case GCMD_SLOGAN: // 길드 슬로건
        case GCMD_INVITE:
        case GCMD_MEMBER_JOBnLEV: {
            short nOffset = sizeof(cli_CLAN_COMMAND);
            char* pAddStr = Packet_GetStringPtr(pGuildCMD->m_pPacket, nOffset);
            if (pAddStr && pAddStr[0]) {
                CWS_Client* pClanOwner =
                    (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
                if (pClanOwner && pClanOwner->GetClanID()) {
                    CClan* pClan = this->Find_CLAN(pClanOwner->GetClanID());
                    if (pClan) {
                        switch (pGuildCMD->m_btCMD) {
                            case GCMD_INVITE: // 길드 초대
                                if (pClanOwner->GetClanPOS() >= GPOS_SUB_MASTER) {
                                    CWS_Client* pDestUSER = g_pUserLIST->Find_CHAR(pAddStr);
                                    if (pDestUSER) {
                                        if (0 == pDestUSER->GetClanID()) {
                                            pDestUSER->Send_wsv_CLAN_COMMAND(GCMD_INVITE_REQ,
                                                pClanOwner->m_Name.BuffLength() + 1,
                                                pClanOwner->Get_NAME(),
                                                NULL);
                                        } else {
                                            pClanOwner->Send_wsv_CLAN_COMMAND(
                                                RESULT_CLAN_JOIN_HAS_CLAN,
                                                strlen(pAddStr) + 1,
                                                pAddStr,
                                                NULL);
                                        }
                                    } else
                                        pClanOwner->Send_wsv_CLAN_COMMAND(RESULT_CLAN_JOIN_FAILED,
                                            strlen(pAddStr) + 1,
                                            pAddStr,
                                            NULL);
                                }
                                break;
                            case GCMD_REMOVE: // 길드 추방
                                if (pClanOwner->GetClanPOS() >= GPOS_SUB_MASTER) {
                                    // 자신은 안됨
                                    if (pClan->Delete_MEMBER(pClanOwner->m_HashCHAR,
                                            pAddStr,
                                            RESULT_CLAN_KICK,
                                            pClanOwner->Get_NAME())) {
                                    }
                                }
                                break;
                            case GCMD_PROMOTE: // 길드 승급
                                if (pClanOwner->GetClanPOS() >= GPOS_MASTER) {
                                    // 자신은 안됨.
                                    pClan->Adjust_MEMBER(pClanOwner->m_HashCHAR,
                                        pAddStr,
                                        0,
                                        1,
                                        pClanOwner->Get_NAME(),
                                        GPOS_SUB_MASTER,
                                        0,
                                        true);
                                }
                                break;
                            case GCMD_DEMOTE: // 길드 강등
                                if (pClanOwner->GetClanPOS() >= GPOS_MASTER) {
                                    // 자신은 안됨
                                    pClan->Adjust_MEMBER(pClanOwner->m_HashCHAR,
                                        pAddStr,
                                        0,
                                        -1,
                                        pClanOwner->Get_NAME(),
                                        GPOS_SUB_MASTER);
                                }
                                break;
                            case GCMD_LEADER: // 길드 위임
                                if (pClanOwner->GetClanPOS() >= GPOS_MASTER) {
                                    if (pClan->Adjust_MEMBER(pClanOwner->m_HashCHAR,
                                            pAddStr,
                                            0,
                                            GPOS_MASTER,
                                            pClanOwner->Get_NAME(),
                                            GPOS_MASTER,
                                            GPOS_COMMANDER)) {
                                        // 위임 로그...
                                        pClan->Adjust_MEMBER(0,
                                            pClanOwner->Get_NAME(),
                                            0,
                                            GPOS_COMMANDER - GPOS_MASTER,
                                            pClanOwner->Get_NAME(),
                                            GPOS_COMMANDER);
                                    }
                                }
                                break;
                            case GCMD_MOTD: // 길드 공지
                                if (pClanOwner->GetClanPOS() >= GPOS_MASTER) {
                                    if (this->Query_UpdateClanMOTD(pClan->m_dwClanID, pAddStr)) {
                                        pClan->m_Motd.Set(pAddStr);
                                        if (pAddStr && pAddStr[0]) {
                                            if (pClan->m_Motd.BuffLength() < 368) {
                                                pClan->Send_wsv_CLAN_COMMAND(RESULT_CLAN_MOTD,
                                                    pClan->m_Motd.BuffLength() + 1,
                                                    pClan->m_Motd.Get(),
                                                    NULL);
                                            }
                                        }
                                    }
                                }
                                break;
                            case GCMD_SLOGAN: // 길드 슬로건
                                if (pClanOwner->GetClanPOS() >= GPOS_MASTER) {
                                    if (this->Query_UpdateClanSLOGAN(pClan->m_dwClanID, pAddStr)) {
                                        pClan->m_Desc.Set(pAddStr);
                                        if (pAddStr && pAddStr[0]) {
                                            if (pClan->m_Desc.BuffLength() < 128) {
                                                pClan->Send_wsv_CLAN_COMMAND(RESULT_CLAN_SLOGAN,
                                                    pClan->m_Desc.BuffLength() + 1,
                                                    pClan->m_Desc.Get(),
                                                    NULL);
                                            }
                                        }
                                    }
                                }
                                break;
                            case GCMD_MEMBER_JOBnLEV:
                                pClan->SetJOBnLEV(pClanOwner->m_HashCHAR,
                                    pClanOwner->Get_NAME(),
                                    pGuildCMD->m_pPacket->m_cli_CLAN_MEMBER_JOBnLEV.m_nJOB,
                                    pGuildCMD->m_pPacket->m_cli_CLAN_MEMBER_JOBnLEV.m_nLEVEL);
                                break;
                        }
                    }
                }
            }
            return true;
        }
        case GCMD_QUIT: // 길드 탈퇴
        {
            // 1. 길드원 인가 ?
            CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
            if (pFindUSER && pFindUSER->GetClanID() && pFindUSER->GetClanPOS() < GPOS_MASTER) {
                CClan* pClan = this->Find_CLAN(pFindUSER->GetClanID());
                if (pClan) {
                    if (pClan->Delete_MEMBER(0, pFindUSER->Get_NAME(), RESULT_CLAN_QUIT)) {
                    }
                }
            }
            return true;
        }
        case GCMD_ROSTER: // 길드 목록
        {
            CWS_Client* pFindUSER = (CWS_Client*)g_pUserLIST->GetSOCKET(pGuildCMD->m_iSocketIDX);
            if (pFindUSER && pFindUSER->GetClanID()) {
                CClan* pClan = this->Find_CLAN(pFindUSER->GetClanID());
                if (pClan) {
                    pClan->SendRoster(pGuildCMD->m_iSocketIDX);
                }
            }
        }
    }

    return false;
}

//-------------------------------------------------------------------------------------------------
CClan*
CThreadGUILD::Find_CLAN(DWORD dwClanID) {
    tagHASH<CClan*>* pHashNode;

    pHashNode = m_pHashCLAN->Search(dwClanID);
    CClan* pClan = pHashNode ? pHashNode->m_DATA : NULL;
    while (pClan) {
        if (dwClanID == pClan->m_dwClanID) {
            // Find !!!
            break;
        }

        pHashNode = m_pHashCLAN->SearchContinue(pHashNode, dwClanID);
        pClan = pHashNode ? pHashNode->m_DATA : NULL;
    }

    return pClan;
}

//-------------------------------------------------------------------------------------------------
CClan*
CThreadGUILD::load_clan(int clan_id) {
    const char* clan_stmt = "SELECT name, description, motd, level, points, money, marker_front, marker_back, marker_crc FROM clan WHERE id=$1";
    QueryResult clan_res = this->db.query(clan_stmt, {std::to_string(clan_id)});
    if (!clan_res.is_ok()) {
        LOG_ERROR("Failed to load clan with id: {} {}", clan_id, this->db.last_error_message());
        return NULL;
    }

    const std::string clan_name = clan_res.get_string(0, 0);
    const std::string clan_description = clan_res.get_string(0, 1);
    const std::string clan_motd = clan_res.get_string(0, 2);
    const int clan_level = clan_res.get_int32(0, 3);
    const int clan_points = clan_res.get_int64(0, 4);
    const int clan_money = clan_res.get_int64(0, 5);
    const int clan_marker_front = clan_res.get_int32(0, 6);
    const int clan_marker_back = clan_res.get_int32(0, 7);
    const int clan_marker_crc = clan_res.get_int32(0, 8);
    
    // TODO: add missing fields
    const int clan_allies_id = 0;
    const int clan_rate = 100;
    int clan_marker_length = 0;
    char* clan_marker = NULL;
    BYTE* clan_skills = NULL;

    if (clan_marker_crc != NULL) {
        // Load custom clan marker
        std::ifstream file;
        file.open(this->get_clan_marker_filename(clan_id), std::ifstream::binary);
        
        if (file) {
            file.seekg(0, file.end);
            int clan_marker_length = file.tellg();
            file.seekg(0, file.beg);

            clan_marker = new char[clan_marker_length];
            file.read(clan_marker, clan_marker_length);
        } else {
            LOG_ERROR("Unable to load custom marker for clan with id: {}", clan_id);
        }

        file.close();
    }

    CClan* clan = m_Pools.Pool_Alloc();
    if (clan == NULL) {
        LOG_ERROR("Unable to assign clan to data pool");
        return NULL;
    }

    clan->Init(const_cast<char*>(clan_name.c_str()),
        const_cast<char*>(clan_description.c_str()),
        clan_id,
        clan_marker_back,
        clan_marker_front,
        clan_skills,
        clan_marker_crc,
        clan_marker_length,
        clan_marker);

    clan->m_nClanLEVEL = clan_level;
    clan->m_iClanSCORE = clan_points;
    clan->m_dwAlliedGroupID = clan_allies_id;
    clan->m_iClanRATE = clan_rate;
    clan->m_biClanMONEY = clan_money;

    clan->m_RegTIME.m_wYear = 0;
    clan->m_RegTIME.m_btMon = 0;
    clan->m_RegTIME.m_btDay = 0;
    clan->m_RegTIME.m_btHour = 0;
    clan->m_RegTIME.m_btMin = 0;
    clan->m_RegTIME.m_btSec = 0;

    const char* clan_member_stmt = "SELECT character.name, clan_member.rank, clan_member.points FROM clan_member INNER JOIN character ON character.id = clan_member.character_id WHERE clan_id=$1";
    QueryResult clan_member_res = this->db.query(clan_member_stmt, {std::to_string(clan_id)});
    if (!clan_member_res.is_ok()) {
        clan->Free();
        m_Pools.Pool_Free(clan);
        LOG_ERROR("Failed to load clan members from clan with name: {} {}",
            clan_name,
            this->db.last_error_message());
        return NULL;
    }

    CDLList<CClanUSER>::tagNODE* clan_member;
    std::string char_name;
    int char_rank = 0;
    int char_contribution = 0;

    for (size_t row_idx = 0; row_idx < clan_member_res.row_count; ++row_idx) {
        char_name = clan_member_res.get_string(row_idx, 0);
        char_rank = clan_member_res.get_int32(row_idx, 1);
        char_contribution = clan_member_res.get_int32(row_idx, 1);

        clan_member = clan->m_ListUSER.AllocNAppend();
        if (!clan_member) {
            break;
        }

        if (char_rank > GPOS_MASTER) {
            char_rank = GPOS_MASTER;
        }

        clan->m_nPosCNT[char_rank]++;

        clan_member->m_VALUE.m_Name.Set(const_cast<char*>(char_name.c_str()));
        clan_member->m_VALUE.m_HashName = ::StrToHashKey(char_name.c_str());
        clan_member->m_VALUE.m_iPosition = char_rank;
        clan_member->m_VALUE.m_iContribute = char_contribution;
        clan_member->m_VALUE.m_btChannelNo = 0xff;
        clan_member->m_VALUE.m_nJob = 0;
        clan_member->m_VALUE.m_nLevel = 0;
    }

    if (!clan->m_ListUSER.GetNodeCount()) {
        clan->Free();
        m_Pools.Pool_Free(clan);
        return NULL;
    }

    m_pHashCLAN->Insert(clan_id, clan);

    return clan;
}

//-------------------------------------------------------------------------------------------------
CClan*
CThreadGUILD::Query_CreateCLAN(int socket_id, t_PACKET* packet) {
    if (!packet) {
        return nullptr;
    }

    short offset = sizeof(cli_CLAN_CREATE);
    std::string clan_name = Packet_GetStringPtr(packet, offset);
    std::string clan_description = Packet_GetStringPtr(packet, offset);
    const int clan_marker_back = packet->m_cli_CLAN_CREATE.m_wMarkIDX[0];
    const int clan_marker_front = packet->m_cli_CLAN_CREATE.m_wMarkIDX[1];

    if (clan_name.empty()) {
        return nullptr;
    }

    const char* count_stmt = "SELECT COUNT(*) FROM clan WHERE name=$1";
    QueryResult count_res = this->db.query(count_stmt, {clan_name});
    if (!count_res.is_ok()) {
        LOG_ERROR("Failed to count clans with name {}: {}",
            clan_name.c_str(),
            count_res.error_message());
        return nullptr;
    }

    if (count_res.get_int32(0, 0) != 0) {
        LOG_WARN("Attempted to create duplicate clan with name: {}", clan_name.c_str());
        return nullptr;
    }

    const char* insert_stmt = "INSERT INTO clan (name, description, marker_front, marker_back) VALUES ($1, $2, $3, $4) RETURNING id";
    QueryResult insert_res = this->db.query(insert_stmt, {clan_name, clan_description, std::to_string(clan_marker_front), std::to_string(clan_marker_back)});
    if (!insert_res.is_ok()) {
        LOG_ERROR("Failed to insert clan with name {}: {}",
            clan_name.c_str(),
            insert_res.error_message());
        return nullptr;
    }

    int64_t clan_id = insert_res.get_int64(0, 0);
    CClan* pClan = m_Pools.Pool_Alloc();
    pClan->Init((char*)clan_name.c_str(),
        (char*)clan_description.c_str(),
        clan_id,
        (WORD)clan_marker_back,
        (WORD)clan_marker_front,
        NULL,
        0,
        0,
        NULL);
    m_pHashCLAN->Insert(clan_id, pClan);
    return pClan;
}

bool
CThreadGUILD::Query_DeleteCLAN(char* szClanName) {
    std::string clan_name(szClanName);
    if (clan_name.empty()) {
        return false;
    }

    const char* delete_stmt = "DELETE FROM clan WHERE name=$1 RETURNING id";
    QueryResult delete_res = this->db.query(delete_stmt, {clan_name});
    if (!delete_res.is_ok()) {
        LOG_ERROR("Failed to delete clan with name {}: {}",
            clan_name.c_str(),
            delete_res.error_message());
        return false;
    }

    const int clan_id = delete_res.get_int32(0, 0);
    if (delete_res.row_count != 1 || clan_id == 0) {
        LOG_WARN("Attempted to delete non-existant clan with name: {}", clan_name.c_str());
        return false;
    }

    CClan* pClan = this->Find_CLAN(clan_id);
    if (pClan) {
        pClan->Disband();

        m_pHashCLAN->Delete(clan_id, pClan);
        pClan->Free();
        m_Pools.Pool_Free(pClan);
    }

    return true;
}

bool
CThreadGUILD::Query_InsertClanMember(char* szCharName, DWORD clan_id, int rank) {
    std::string char_name(szCharName);
    if (char_name.empty()) {
        return false;
    }

    const char* insert_stmt = "INSERT INTO clan_member (character_id, clan_id, rank)"
                              "VALUES ((SELECT id FROM character WHERE name=$1), $2, $3)";

    QueryResult insert_res =
        this->db.query(insert_stmt, {char_name, std::to_string(clan_id), std::to_string(rank)});

    if (!insert_res.is_ok()) {
        LOG_ERROR("Failed to insert character '{}' into clan with id '{}': {}",
            char_name.c_str(),
            clan_id,
            insert_res.error_message());
        return false;
    }

    return true;
}

bool
CThreadGUILD::Query_DeleteClanMember(char* szCharName) {
    std::string char_name(szCharName);
    if (char_name.empty()) {
        return false;
    }

    const char* delete_stmt = "DELETE FROM clan_member WHERE character_id=(SELECT id FROM character WHERE name=$1)";
    QueryResult delete_res = this->db.query(delete_stmt, {char_name});
    if (!delete_res.is_ok()) {
        LOG_ERROR("Failed to delete clan member with name {}: {}",
            char_name.c_str(),
            delete_res.error_message());
        return false;
    }

    return true;
}

bool
CThreadGUILD::Query_AdjustClanMember(char* szCharName, int iAdjContr, int iAdjPos) {
    std::string char_name(szCharName);
    if (char_name.empty()) {
        return false;
    }

    QueryResult res =
        this->db.query("UPDATE clan_member SET points=points+$1, rank=rank+$2 WHERE id=(SELECT id FROM character WHERE name=$3)",
            {std::to_string(iAdjContr), std::to_string(iAdjPos), char_name});
    if (!res.is_ok()) {
        LOG_ERROR("Failed to update data for clan member with name: {} points: {} rank: {}",
            char_name,
            iAdjContr,
            iAdjPos);
        LOG_ERROR(this->db.last_error_message());
        return false;
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CThreadGUILD::Query_UpdateClanMOTD(DWORD dwClanID, char* szMessage) {
    std::string motd(szMessage);
    if (motd.empty()) {
        return false;
    }

    QueryResult res = this->db.query("UPDATE clan SET motd=$1 WHERE id=$2", {motd, std::to_string(dwClanID)});
    if (!res.is_ok()) {
        LOG_ERROR("Failed to update motd for clan with id: {} {}", dwClanID, motd);
        LOG_ERROR(this->db.last_error_message());
        return false;
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CThreadGUILD::Query_UpdateClanSLOGAN(DWORD dwClanID, char* szMessage) {
    /* TODO: RAM: Port to postgres
    long iResultSP = -99;
    SDWORD cbSize1 = SQL_NTS;

    this->db->SetParam_long(1, iResultSP, cbSize1);
    if (this->db->QuerySQL((char*)"{?=call ws_ClanSLOGAN(%d,\'%s\')}", dwClanID, szMessage)) {
        while (this->db->GetMoreRESULT()) {
            ;
        }
        switch (iResultSP) {
            case 0: // 성공
                g_LOG.CS_ODS(0xffff, "update clan desc : %d / %s\n", dwClanID, szMessage);
                return true;
            // case -1 :	// ????
            //	g_LOG.CS_ODS( 0xffff, "not clan user name :  %s \n", szCharName);
            //	break;
            case -2: // 디비 오류
                g_LOG.CS_ODS(0xffff, "update clan user failed : %d / %s\n", dwClanID, szMessage);
                break;
            default:
                assert("invalid ws_ClanSLOGAN SP retrun value" && 0);
        }
    } else {
        // 디비 SP 오류...
    }
    */
    return false;
}

bool
CThreadGUILD::Query_LoginClanMember(char* char_name, int socket_id) {
    const CWS_Client* user = g_pUserLIST->Find_CHAR(char_name);
    if (!user) {
        return false;
    }

    const char* stmt = "SELECT clan_id, points FROM clan_member WHERE character_id=$1";
    QueryResult res = this->db.query(stmt, {std::to_string(user->m_dwDBID)});
    if (!res.is_ok()) {
        LOG_ERROR("Failed to select clan for character with name {}", char_name);
        LOG_ERROR(this->db.last_error_message());
        return false;
    }

    const int clan_id = res.get_int32(0, 0);
    const int char_contribution = res.get_int32(0, 1);

    CClan* clan = this->Find_CLAN(clan_id);
    if (clan == NULL) {
        clan = this->load_clan(clan_id);
        if (clan == NULL) {
            return false;
        }
    }

    return clan->LogIn_ClanUSER(char_name, socket_id, char_contribution);
}

bool
CThreadGUILD::update_clan_data(const std::string& column_name, t_PACKET* packet) {
    const int clan_id = packet->m_gsv_ADJ_CLAN_VAR.m_dwClanID;
    const int value = packet->m_gsv_ADJ_CLAN_VAR.m_iAdjValue;

    if (packet->m_gsv_ADJ_CLAN_VAR.m_btVarType == CLVAR_INC_LEV) {
        CClan* clan = this->Find_CLAN(packet->m_gsv_ADJ_CLAN_VAR.m_dwClanID);
        if (!clan || (clan->m_nClanLEVEL + value) > MAX_CLAN_LEVEL) {
            return false;
        }
    }

    std::string stmt =
        fmt::format("UPDATE clan SET {0}={0}+$1 WHERE id=$2 RETURNING {0}", column_name);

    QueryResult res = this->db.query(stmt, {std::to_string(value), std::to_string(clan_id)});
    if (!res.is_ok()) {
        LOG_ERROR("Failed to update data for clan with id {} {} {}", clan_id, column_name, value);
        LOG_ERROR(this->db.last_error_message());
        return false;
    }

    switch (packet->m_gsv_ADJ_CLAN_VAR.m_btVarType) {
        case CLVAR_INC_LEV:
        case CLVAR_ADD_SCORE:
            packet->m_gsv_ADJ_CLAN_VAR.m_iAdjValue = res.get_int32(0, 0);
            break;
        case CLVAR_ADD_ZULY:
            packet->m_gsv_ADJ_CLAN_VAR.m_biResult = res.get_int64(0, 0);
            break;
        default:
            return false;
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CThreadGUILD::Query_UpdateClanBINARY(DWORD dwClanID, BYTE* pDATA, unsigned int uiSize) {
    /* TODO: RAM: Port to postgres
    long iResultSP = -99;
    SDWORD cbSize1 = SQL_NTS;

    this->db->SetParam_long(1, iResultSP, cbSize1);
    this->db->BindPARAM(2, (BYTE*)pDATA, uiSize);

    if (this->db->QuerySQL((char*)"{?=call ws_ClanBinUPDATE(%d,?)}", dwClanID)) {
        while (this->db->GetMoreRESULT()) {
            ;
        }
        switch (iResultSP) {
            case 1: // 성공
                g_LOG.CS_ODS(0xffff, "update clan binary : %d \n", dwClanID);
                return true;
            // case -1 :	// ????
            //	g_LOG.CS_ODS( 0xffff, "not clan user name :  %s \n", szCharName);
            //	break;
            case 0: // 디비 오류
                g_LOG.CS_ODS(0xffff, "update clan binary failed : %d \n", dwClanID);
                break;
            default:
                assert("invalid ws_ClanBinUPDATE SP retrun value" && 0);
        }
    } else {
        // 디비 SP 오류...
        g_LOG.CS_ODS(0xffff, "Query ERROR :: ws_ClanBinUPDATE( %d ) \n", dwClanID);
    }
    */
    return false;
}

//-------------------------------------------------------------------------------------------------
WORD
CThreadGUILD::Query_UpdateClanMARK(CClan* pClan, WORD wMarkCRC, BYTE* pDATA, unsigned int uiSize) {
    if (!pClan || !wMarkCRC || !pDATA || uiSize == 0) {
        return RESULT_CLANMARK_SP_ERROR;
    }

    if (uiSize > 1024) {
        return RESULT_CLANMARK_TOO_LARGE_ERROR;
    }

    std::ofstream clan_marker;
    clan_marker.open(this->get_clan_marker_filename(pClan->m_dwClanID), std::ofstream::binary);

    if (!clan_marker) {
        clan_marker.close();
        LOG_ERROR("Failed to open custom marker for clan with id: {}", pClan->m_dwClanID);
        return RESULT_CLANMARK_SP_ERROR;
    }

    const char* buffer = reinterpret_cast<const char*>(pDATA);
    clan_marker.write(buffer, uiSize);

    if (clan_marker.fail()) {
        clan_marker.close();
        LOG_ERROR("Failed to save custom marker for clan with id: {}", pClan->m_dwClanID);
        return RESULT_CLANMARK_SP_ERROR;
    }

    clan_marker.close();

    QueryResult res = this->db.query("UPDATE clan SET marker_crc=$1 WHERE id=$2",
        {std::to_string(wMarkCRC), std::to_string(pClan->m_dwClanID)});
    if (!res.is_ok()) {
        LOG_ERROR("Failed to update custom marker for clan with id: {} {}",
            pClan->m_dwClanID,
            this->db.last_error_message());
        return RESULT_CLANMARK_DB_ERROR;
    }

    return 0;
}

//-------------------------------------------------------------------------------------------------
// #endif