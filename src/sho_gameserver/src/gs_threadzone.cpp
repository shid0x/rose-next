#include "stdafx.h"

#include "LIB_gsMAIN.h"

#include "GS_ListUSER.h"
#include "GS_Party.h"
#include "GS_ThreadSQL.h"
#include "ZoneLIST.h"

//-------------------------------------------------------------------------------------------------

#define SECTOR_UPDATE_ALL 0x00
#define SECTOR_UPDATE_LEFT 0x01
#define SECTOR_UPDATE_RIGHT 0x02
#define SECTOR_UPDATE_UP 0x04
#define SECTOR_UPDATE_DOWN 0x08
#define SECTOR_UPDATE_LU (SECTOR_UPDATE_LEFT | SECTOR_UPDATE_UP) // 0x05
#define SECTOR_UPDATE_RU (SECTOR_UPDATE_RIGHT | SECTOR_UPDATE_UP) // 0x06
#define SECTOR_UPDATE_LD (SECTOR_UPDATE_LEFT | SECTOR_UPDATE_DOWN) // 0x09
#define SECTOR_UPDATE_RD (SECTOR_UPDATE_RIGHT | SECTOR_UPDATE_DOWN) // 0x0a		10

static POINTS s_SecAdjPos[9] = {
    {-1, -1},
    {0, -1},
    {1, -1}, // 0 ~ 2
    {-1, 0},
    {0, 0},
    {1, 0}, // 3 ~ 5
    {-1, 1},
    {0, 1},
    {1, 1} // 6 ~ 8
};

static char s_AddSecIdx[11][10] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, -1}, // 0x00		all
    {0, 3, 6, -1, 0, 0, 0, 0, 0, 0}, // 0x01		left
    {2, 5, 8, -1, 0, 0, 0, 0, 0, 0}, // 0x02		right
    {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 0x03
    {0, 1, 2, -1, 0, 0, 0, 0, 0, 0}, // 0x04		up
    {0, 3, 6, 1, 2, -1, 0, 0, 0, 0}, // 0x05		left & up
    {2, 5, 8, 0, 1, -1, 0, 0, 0, 0}, // 0x06		right & up
    {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 0x07
    {6, 7, 8, -1, 0, 0, 0, 0, 0, 0}, // 0x08		down
    {0, 3, 6, 7, 8, -1, 0, 0, 0, 0}, // 0x09		left & down
    {2, 5, 8, 6, 7, -1, 0, 0, 0, 0} // 0x0a		right & down
};

static char s_SubSecIdx[11][10] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, -1}, // 0x00		all
    {2, 5, 8, -1, 0, 0, 0, 0, 0, 0}, // 0x01		right
    {0, 3, 6, -1, 0, 0, 0, 0, 0, 0}, // 0x02		left
    {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 0x03
    {6, 7, 8, -1, 0, 0, 0, 0, 0, 0}, // 0x04		down
    {2, 5, 8, 6, 7, -1, 0, 0, 0, 0}, // 0x05		right & down
    {0, 3, 6, 7, 8, -1, 0, 0, 0, 0}, // 0x06		left & down
    {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 0x07
    {0, 1, 2, -1, 0, 0, 0, 0, 0, 0}, // 0x08		up
    {2, 5, 8, 0, 1, -1, 0, 0, 0, 0}, // 0x09		right & up
    {0, 3, 6, 1, 2, -1, 0, 0, 0, 0} // 0x0a		left & up
};

//-------------------------------------------------------------------------------------------------
CZoneTHREAD::CZoneTHREAD(bool CreateSuspended):
    CZoneFILE(CreateSuspended), m_csZoneObjWAIT(8000), m_csPacketLIST(4000),
    m_csZoneMobCALLED(4000) {
    m_pListITEM = NULL;
    m_lMaxUSER = 0;
    m_lUserCNT = 0;
    m_bEndExecuteFunc = true;
    m_bEnableREGEN = true;

    m_dwGlobalFLAGS = 0;

    m_ppSECTOR = NULL;
    //  m_ppMAP    = NULL;

    m_pEvent = new classEVENT(NULL, false, false, NULL);

    this->SetPriority(THREAD_PRIORITY_NORMAL);
}
__fastcall CZoneTHREAD::~CZoneTHREAD() {
    if (!IsFinished() || !m_bEndExecuteFunc)
        this->Free();

    this->FreeZONE();
    // LogString( 0xffff, "Free Sectors: %d \n", CZoneSECTOR::GetSectorCount () );

    delete m_pEvent;
}

#define _FRAME_PER_SECOND 10
#define _DELAY_PER_FRAME (1000 / _FRAME_PER_SECOND)

void
CZoneTHREAD::Execute() {
    CGameOBJ* pDelOBJ;
    classDLLNODE<CGameOBJ*>*pObjNODE, *pNextNODE;
    classDLLNODE<CRegenPOINT*>* pRegenNODE;
    classSLLNODE<classPACKET*>* pPacketNODE;
    classSLLNODE<tagCalledMOB>* pMobNODE;

    DWORD dwPassTIME, dwCurTIME;

    if (!m_Timer.Start()) {
        LOG_ERROR("Could not sart the CZoneThread timer");
        this->Terminate();
    } else {
        m_Timer.GetValue();
    }

    m_dwTimeGetTIME = ::timeGetTime();
    m_bEndExecuteFunc = false;

#ifdef __NPC_TEST
    m_bEnableREGEN = false;
#endif

    while (!this->Terminated) {
        dwCurTIME = m_Timer.Reset();

        //---------- ¸®Á¨ ¿µ¿ª Ã³¸®...
        if (m_bEnableREGEN) {
            pRegenNODE = m_RegenLIST.GetHeadNode();
            while (pRegenNODE) {
                pRegenNODE->DATA->Proc(dwCurTIME);
                pRegenNODE = m_RegenLIST.GetNextNode(pRegenNODE);
            }
        }

        // Á×À»¶§ ¼ÒÈ¯µÈ ¸÷À» ÀÏÁ¤½Ã°£ÈÄ¿¡ ¸®Á¨½ÃÅ°µµ·Ï...
        m_csZoneMobCALLED.Lock();
        pMobNODE = m_MobCALLED.GetHeadNode();
        while (pMobNODE) {
            if (this->GetTimeGetTIME() < pMobNODE->DATA.m_dwActionTIME) {
                break;
            }
            this->RegenMOB(pMobNODE->DATA.m_fCenterX,
                pMobNODE->DATA.m_fCenterY,
                pMobNODE->DATA.m_iRange,
                pMobNODE->DATA.m_iCharNO,
                pMobNODE->DATA.m_iCount,
                pMobNODE->DATA.m_iTeamNO);
            m_MobCALLED.DeleteHeadNFree();
            pMobNODE = m_MobCALLED.GetHeadNode();
        }
        m_csZoneMobCALLED.Unlock();

        //---------- @ÁÖÀÇ!! ¸®Á¨ º¸´Ù µÚ¿¡ ¿Í¾ß µÈ´Ù.. Á¸¿¡ µé¾î¿Ã ´ë±âÀÚ¸¦ ¸ñ·Ï¿¡ Ãß°¡...
        m_csZoneObjWAIT.Lock();
        pObjNODE = m_ObjWAIT.GetHeadNode();
        while (pObjNODE) {
            m_ObjWAIT.DeleteNode(pObjNODE);
            m_ObjLIST.AppendNode(pObjNODE);
            this->AddObjectToSector(pObjNODE->DATA, SECTOR_UPDATE_ALL);
            pObjNODE = m_ObjWAIT.GetHeadNode();
        }
        m_csZoneObjWAIT.Unlock();

        //---------- °æÁ¦ µ¥ÀÌÅ¸ Ã³¸®...
        if (m_Economy.Proc(dwCurTIME)) {
            this->Send_EconomyDATA();
        }

        //---------- ÀüÃ¼ Àü¼Û ÆÐÅ¶...
        m_csPacketLIST.Lock();
        if (m_ToSendPacketLIST.GetNodeCount()) {
            m_SendingPacketLIST.AppendNodeList(&m_ToSendPacketLIST);
            m_ToSendPacketLIST.Init();
        }
        m_csPacketLIST.Unlock();

        //---------- ½ÇÁ¦ Á¸ÀÇ ¿ÜÀûÀÌ Ã³¸®°¡ ³¡³µÀ¸¸é ÇÁ·¹ÀÓ´ë±â...
        dwPassTIME = m_Timer.GetValue();
        // dwWorldPassTIME += dwPassTIME;
        if (dwPassTIME < _DELAY_PER_FRAME) {
            if (m_pEvent->WaitFor(_DELAY_PER_FRAME - dwPassTIME) != WAIT_TIMEOUT)
                break;
        }

        //---------- MOB, NPC, CNST, ITEM ... include AVATAR !
        m_dwTimeGetTIME = ::timeGetTime();
        m_dwCurrentTIME = m_Timer.GetCurrentTIME(); // Update current TIME !!!
        // assert( m_Timer.GetPassTIME() > 0 );

        m_dwCurAbsSEC = classTIME::GetCurrentAbsSecond();

        pObjNODE = m_ObjLIST.GetHeadNode();
        while (pObjNODE) {
            pNextNODE = m_ObjLIST.GetNextNode(pObjNODE);

            if (!pObjNODE->DATA->Proc()) {
                pDelOBJ = pObjNODE->DATA;

                if (pDelOBJ->IsUSER()) {
                    this->Dec_UserCNT();

                    // if ( ( (classUSER*)pDelOBJ )->GetPARTY() ) {
                    //	// ºñÁ¤»ó Á¾·á...
                    //	( (classUSER*)pDelOBJ )->m_pPartyBUFF->OnDisconnect( (classUSER*)pDelOBJ );
                    //}

                    this->Sub_DIRECT(pDelOBJ); // CZoneTHREAD::Execute
                    g_pUserLIST->DeleteUSER((classUSER*)pDelOBJ, LOGOUT_MODE_LEFT);
                } else {
                    this->Sub_DIRECT(pDelOBJ); // CZoneTHREAD::Execute
                    SAFE_DELETE(pDelOBJ);
                }
            } else {
                // Á¸ ÀüÃ¼¿¡ Àü¼ÛÇÒ ÆÐÅ¶ÀÌ ÀÖÀ¸¸é...
                pPacketNODE = m_SendingPacketLIST.GetHeadNode();
                while (pPacketNODE) {
                    pObjNODE->DATA->SendPacket(pPacketNODE->DATA);

                    pPacketNODE = m_SendingPacketLIST.GetNextNode(pPacketNODE);
                }
            }
            pObjNODE = pNextNODE;
        }

        //---------- Á¸¿¡ ¼öÇàµÉ Æ®¸®°Å...
        if (m_TriggerLIST.GetNodeCount())
            this->Proc_ZoneTRIGGER();

        pPacketNODE = m_SendingPacketLIST.GetHeadNode();
        while (pPacketNODE) {
            // iRefCNT¸¸ °¨¼Ò ½ÃÄÑ »èÁ¦°¡´ÉÇÏ¸é »èÁ¦ ÇÑ´Ù.
            ::Packet_DecRefCount(pPacketNODE->DATA);

            m_SendingPacketLIST.DeleteHead();
            SAFE_DELETE(pPacketNODE);

            pPacketNODE = m_SendingPacketLIST.GetHeadNode();
        }
    }

    int iTotObjInZONE = m_ObjLIST.GetNodeCount();
    this->DeleteZoneOBJ();

    this->m_bEndExecuteFunc = true;
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::DeleteZoneOBJ() {
    CGameOBJ* pDelOBJ;
    classDLLNODE<CGameOBJ*>*pObjNODE, *pNextNODE;

    // ¾²·¹µå Á¾·á½Ã...
    // Á¸ÀÇ ¸ðµç °´Ã¼¸¦ »èÁ¦ÇÑ´Ù.
    for (pObjNODE = m_ObjLIST.GetHeadNode(); pObjNODE;) {
        pNextNODE = m_ObjLIST.GetNextNode(pObjNODE);

        pDelOBJ = pObjNODE->DATA;
        this->Sub_DIRECT(pDelOBJ); // CZoneTHREAD::Execute
        if (pDelOBJ->IsUSER()) {
            g_pUserLIST->DeleteUSER((classUSER*)pDelOBJ, LOGOUT_MODE_LEFT); // Á¸ ¾²·¹µå Á¾·á½Ã...
        } else {
            SAFE_DELETE(pDelOBJ);
        }

        pObjNODE = pNextNODE;
    }

    // Á¸¿¡ Âü°¡ÇÏ±âÀ§ÇØ ´ë±âÁßÀÎ °´Ã¼ »èÁ¦...
    m_csZoneObjWAIT.Lock();
    pObjNODE = m_ObjWAIT.GetHeadNode();
    while (pObjNODE) {
        m_ObjWAIT.DeleteNode(pObjNODE);

        pDelOBJ = pObjNODE->DATA;

        g_pObjMGR->DelData(pDelOBJ->m_iIndex);
        pDelOBJ->m_iIndex = 0;
        pDelOBJ->SetZONE(NULL);

        if (pDelOBJ->IsUSER()) {
            // Á¸¿¡¼­ ºüÁø »ç¿ëÀÚ ¸®½ºÆ®¿¡ µî·Ï...
            g_pUserLIST->Add_NullZONE(&pDelOBJ->m_ZoneNODE); // Á¸ ¾²·¹µå Á¾·á½Ã...
            g_pUserLIST->DeleteUSER((classUSER*)pDelOBJ, LOGOUT_MODE_LEFT); // Á¸ ¾²·¹µå Á¾·á½Ã...
        } else {
            SAFE_DELETE(pDelOBJ);
        }

        pObjNODE = m_ObjWAIT.GetHeadNode();
    }
    m_csZoneObjWAIT.Unlock();

    // ¸®Á¨ Æ÷ÀÎÆ®´Â Á¸ °´Á¦º¸´Ù µÚ¿¡ »èÁ¦µÇ¾ß ÇÑ´Ù.
    classDLLNODE<CRegenPOINT*>* pRegenNODE;
    while (pRegenNODE = m_RegenLIST.GetHeadNode()) {
        SAFE_DELETE(pRegenNODE->DATA);
        m_RegenLIST.DeleteNFree(pRegenNODE);

        pRegenNODE = m_RegenLIST.GetHeadNode();
    }
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::Proc_ZoneTRIGGER() {
    classDLLNODE<CGameOBJ*>* pObjNODE;
    classSLLNODE<tagZoneTRIGGER>* pQstNODE;

    m_csTriggerLIST.Lock();
    {
        pQstNODE = m_TriggerLIST.GetHeadNode();
        while (pQstNODE) {
            pObjNODE = m_ObjLIST.GetHeadNode();
            while (pObjNODE) {
                if (pObjNODE->DATA->IsUSER()) {
                    if (0 == pQstNODE->DATA.m_nTeamNO
                        || ((classUSER*)pObjNODE->DATA)->Get_TeamNO() == pQstNODE->DATA.m_nTeamNO) {
                        // Æ®¸®°Å ½ÇÇà.
                        // Äù½ºÆ®¿¡¼­ ¿öÇÁ ÇÒ°æ¿ì »ç¿ëÀÚ¿¡°Ô ÆÐÅ¶ º¸³»¼­ ¿öÇÁ ¿äÃ»ÇÏµµ·Ï µÇ¾î ÀÖ´Ù~~
                        ((classUSER*)pObjNODE->DATA)->Do_QuestTRIGGER(pQstNODE->DATA.m_TriggerHASH);
                    }
                }

                pObjNODE = m_ObjLIST.GetNextNode(pObjNODE);
            }

            m_TriggerLIST.DeleteHeadNFree();
            pQstNODE = m_TriggerLIST.GetHeadNode();
        }
    }
    m_csTriggerLIST.Unlock();
}

//-------------------------------------------------------------------------------------------------
bool
CZoneTHREAD::Init(char* szBaseDIR, short nZoneNO) {
    /*
        m_ppMAP = new CMapGRID* [ MAP_COUNT_PER_ZONE_AXIS*PATCH_COUNT_PER_MAP_AXIS ];
        ::ZeroMemory (m_ppMAP,		sizeof( CMapGRID* ) *
       (MAP_COUNT_PER_ZONE_AXIS*PATCH_COUNT_PER_MAP_AXIS) ); for (nY=0; nY<MAP_COUNT_PER_ZONE_AXIS;
       nY++) { m_ppMAP[ nY ] = new CMapGRID [ MAP_COUNT_PER_ZONE_AXIS*PATCH_COUNT_PER_MAP_AXIS ];
        }
    */
    bool bValidZONE = this->LoadZONE(szBaseDIR, nZoneNO);

    return bValidZONE;
}

//-------------------------------------------------------------------------------------------------
bool
CZoneTHREAD::Free(void) {
    this->Terminate();

    if (!IsFinished() || !m_bEndExecuteFunc) {
        return false;
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
#define SHOUT_SECTOR 3
bool
CZoneTHREAD::SendShout(CGameOBJ* pGameOBJ, classPACKET* pCPacket, int iTeamNo) {
    short nX, nY, nMinX, nMinY, nMaxX, nMaxY;

    POINTS PosSECTOR = pGameOBJ->m_PosSECTOR;

    nMinX = (PosSECTOR.x - SHOUT_SECTOR < 0) ? 0 : PosSECTOR.x - SHOUT_SECTOR;
    nMinY = (PosSECTOR.y - SHOUT_SECTOR < 0) ? 0 : PosSECTOR.y - SHOUT_SECTOR;

    nMaxX = (PosSECTOR.x + SHOUT_SECTOR >= this->Get_SectorXCNT()) ? this->Get_SectorXCNT() - 1
                                                                   : PosSECTOR.x + SHOUT_SECTOR;
    nMaxY = (PosSECTOR.y + SHOUT_SECTOR >= this->Get_SectorYCNT()) ? this->Get_SectorYCNT() - 1
                                                                   : PosSECTOR.y + SHOUT_SECTOR;

    if (iTeamNo) {
        for (nY = nMinY; nY <= nMaxY; nY++)
            for (nX = nMinX; nX <= nMaxX; nX++) {
                m_ppSECTOR[nY][nX].SendTeamPacketToSECTOR(pCPacket, iTeamNo, pGameOBJ);
            }
    } else {
        for (nY = nMinY; nY <= nMaxY; nY++)
            for (nX = nMinX; nX <= nMaxX; nX++) {
                m_ppSECTOR[nY][nX].SendPacketToSECTOR(pCPacket, pGameOBJ);
            }
    }

    /*
    // ÀÌ ÇÔ¼ö°¡ È£ÃâµÇ´Â °÷Àº ¸ÞÀÎ ¾²·¹µå´Ù !!!
    // ¿©±â¼­ ÆÐÅ¶À» lockÇÏ°í ¾²·¹µå¾È¿¡¼­ unlockÇÏ´Â°ÍÀº ¹¹ÇÏ´Â Â¢ ???
    // ÇÒ´ç¸¸ ¹Þ°í lockÀº zone thread¾È¿¡¼­ ÇÏÀÚ !!!
    classSLLNODE< classPACKET* > *pNewNODE;

    pNewNODE = new classSLLNODE< classPACKET* >;

    pNewNODE->DATA = ::Packet_AllocOnly ();
    if ( NULL == pNewNODE->DATA )
        return false;
    pNewNODE->DATA->SetRefCnt( 1 );

    pNewNODE->DATA->m_HEADER.m_wType = GSV_SHOUT;
    pNewNODE->DATA->m_HEADER.m_nSize = sizeof( gsv_SHOUT );
    pNewNODE->DATA->AppendString( szName );
    pNewNODE->DATA->AppendString( szChatMSG );



    m_csPacketLIST.Lock ();
    m_ToSendPacketLIST.AppendNode( pNewNODE );
    m_csPacketLIST.Unlock ();
    */

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CZoneTHREAD::SendPacketToZONE(t_PACKET* pSendPacket) {
    // ÀÌ ÇÔ¼ö°¡ È£ÃâµÇ´Â °÷Àº ¸ÞÀÎ ¾²·¹µå´Ù !!!
    // ¿©±â¼­ ÆÐÅ¶À» lockÇÏ°í ¾²·¹µå¾È¿¡¼­ unlockÇÏ´Â°ÍÀº ¹¹ÇÏ´Â Â¢ ???
    // ÇÒ´ç¸¸ ¹Þ°í lockÀº zone thread¾È¿¡¼­ ÇÏÀÚ !!!
    classSLLNODE<classPACKET*>* pNewNODE;

    pNewNODE = new classSLLNODE<classPACKET*>;

    pNewNODE->DATA = ::Packet_AllocOnly();
    if (NULL == pNewNODE->DATA)
        return false;
    pNewNODE->DATA->SetRefCnt(1);

    assert(pNewNODE->DATA->GetRefCnt() == 1);
    // ÆÐÅ¶¿¡ lockÀº °ÉÁö ¾Ê°í iRefCNT¸¸ Áõ°¡ ½ÃÄÑ
    // µµÁß¿¡ ÆÐÅ¶ÀÌ Àü¼ÛµÇ¾îµµ Ç®¸®Áö ¾Ê°Ô ÇÑ´Ù.
    // pNewNODE->DATA->m_iRefCnt ++;

    ::CopyMemory(pNewNODE->DATA->m_pDATA, pSendPacket->m_pDATA, pSendPacket->m_HEADER.m_nSize);

    m_csPacketLIST.Lock();
    m_ToSendPacketLIST.AppendNode(pNewNODE);
    m_csPacketLIST.Unlock();

    return true;
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::Send_EconomyDATA() {
    t_PACKET* pPacket = (t_PACKET*)new char[MAX_PACKET_SIZE];

    pPacket->m_HEADER.m_wType = GSV_SET_GLOBAL_VAR;
    pPacket->m_HEADER.m_nSize = sizeof(gsv_SET_GLOBAL_VAR);

    pPacket->m_gsv_SET_GLOBAL_VAR.m_nWorld_PRODUCT = ::Get_WorldPROD();
    //	pPacket->m_gsv_SET_GLOBAL_VAR.m_nWorld_STAMINA	= ::Get_WorldSTAMINA();
    pPacket->m_gsv_SET_GLOBAL_VAR.m_nWorld_RATE = ::Get_WorldRATE();
    pPacket->m_gsv_SET_GLOBAL_VAR.m_dwGlobalFLAGS = this->m_dwGlobalFLAGS;

    pPacket->m_gsv_SET_GLOBAL_VAR.m_dwUpdateTIME = this->GetEconomyTIME();
    pPacket->m_gsv_SET_GLOBAL_VAR.m_btTOWN_RATE = this->m_Economy.Get_TownRATE();
    ::CopyMemory(pPacket->m_gsv_SET_GLOBAL_VAR.m_btItemRATE,
        this->m_Economy.m_btItemRATE,
        sizeof(BYTE) * MAX_PRICE_TYPE);

    this->SendPacketToZONE(pPacket);

    SAFE_DELETE_ARRAY(pPacket);
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::Send_GLOBAL_FLAG() {
    t_PACKET* pPacket = (t_PACKET*)new char[MAX_PACKET_SIZE];

    pPacket->m_HEADER.m_wType = GSV_SET_GLOBAL_FLAG;
    pPacket->m_HEADER.m_nSize = sizeof(gsv_SET_GLOBAL_FLAG);

    pPacket->m_gsv_SET_GLOBAL_FLAG.m_dwGlobalFLAGS = this->m_dwGlobalFLAGS;

    this->SendPacketToZONE(pPacket);

    SAFE_DELETE_ARRAY(pPacket);
}
DWORD
CZoneTHREAD::Toggle_PK_FLAG() {
    if (this->m_dwGlobalFLAGS & ZONE_FLAG_PK_ALLOWED)
        this->m_dwGlobalFLAGS &= ~ZONE_FLAG_PK_ALLOWED;
    else
        this->m_dwGlobalFLAGS |= ZONE_FLAG_PK_ALLOWED;

    this->Send_GLOBAL_FLAG();

    return this->m_dwGlobalFLAGS;
}
void
CZoneTHREAD::Set_PK_FLAG(BYTE btOnOff) {
    if (this->m_dwGlobalFLAGS & ZONE_FLAG_PK_ALLOWED) {
        if (btOnOff)
            return;
    } else {
        if (!btOnOff)
            return;
    }

    this->Toggle_PK_FLAG();
}

//-------------------------------------------------------------------------------------------------
CZoneSECTOR*
CZoneTHREAD::GetSECTOR(int iSectorX, int iSectorY) {
    assert(iSectorX >= 0 && iSectorX < Get_SectorXCNT());
    assert(iSectorY >= 0 && iSectorY < Get_SectorYCNT());

    return &m_ppSECTOR[iSectorY][iSectorX];
}

//-------------------------------------------------------------------------------------------------
bool
CZoneTHREAD::Add_RegenPOINT(FILE* fp, float fPosX, float fPosY) {
    CRegenPOINT* pNewPOINT = new CRegenPOINT(this, fPosX, fPosY);
    m_RegenLIST.AllocNAppend(pNewPOINT);

    pNewPOINT->Load(fp);
    return true;
}
bool
CZoneTHREAD::Load_Economy(FILE* fp) {
    m_Economy.Load(fp);
    return true;
}

bool
CZoneTHREAD::Add_DIRECT(CGameOBJ* pObj) {
    assert(pObj->Get_INDEX() == 0);

    pObj->m_iIndex = g_pObjMGR->AddData(pObj);
    if (!pObj->m_iIndex) {
        return false;
    }

    // ¼­¹ö objectindex¸¦ ³Ñ°Ü¾ß ÇÑ´Ù...
    pObj->Send_gsv_JOIN_ZONE(this);

    m_ObjLIST.AppendNode(&pObj->m_ZoneNODE);
    this->AddObjectToSector(pObj, SECTOR_UPDATE_ALL);
    pObj->SetZONE(this);

    return true;
}

//-------------------------------------------------------------------------------------------------
// Ãß°¡ °´Ã¼¸¦ ´ë±âÀÚ ¸ñ·Ï¿¡ ³Ö¾î¼­ °ü¸®...
bool
CZoneTHREAD::Add_OBJECT(CGameOBJ* pObj) {
    assert(pObj->Get_INDEX() == 0);

    pObj->ResetSECTOR(this->Get_SectorSIZE());

    if (pObj->m_PosSECTOR.x < 0 || pObj->m_PosSECTOR.x >= this->Get_SectorXCNT()
        || pObj->m_PosSECTOR.y < 0 || pObj->m_PosSECTOR.y >= this->Get_SectorYCNT()) {
        return false;
    }

    pObj->m_iIndex = g_pObjMGR->AddData(pObj);
    if (!pObj->m_iIndex) {
        return false;
    }

    // ÁÖÀÇ !!! Send_gsv_JOIN_ZONEº¸´Ù ¾Õ¼­¾ß ÇÑ´Ù.
    pObj->SetZONE(this);
    if (pObj->IsUSER()) {
        // Á¸¿¡¼­ ºüÁø »ç¿ëÀÚ ¸®½ºÆ®¿¡¼­ »èÁ¦...
        g_pUserLIST->Sub_NullZONE(&pObj->m_ZoneNODE); // CZoneTHREAD::Add_OBJECT
    }

    // ¼­¹ö objectindex¸¦ ³Ñ°Ü¾ß ÇÑ´Ù...
    pObj->Send_gsv_JOIN_ZONE(this);

    // ´ë±âÀÚ ¸®½ºÆ®¿¡ µî·Ï...
    m_csZoneObjWAIT.Lock();
    { m_ObjWAIT.AppendNode(&pObj->m_ZoneNODE); }
    m_csZoneObjWAIT.Unlock();

    return true;
}

//-------------------------------------------------------------------------------------------------
// ** CZoneTHREAD ¸ÞÀÎ ·çÇÁ¿¡¼­ Å»Ãâ ÇÏµµ·Ï ÇÑ´Ù.
void
CZoneTHREAD::Sub_DIRECT(CGameOBJ* pObj, bool bSubFromREGEN) {
    if (pObj->IsA(OBJ_MOB)) {
        if (bSubFromREGEN && ((CObjMOB*)pObj)->m_pRegenPOINT)
            ((CObjMOB*)pObj)->m_pRegenPOINT->SubLiveCNT();
    }

    pObj->Set_TargetObjIDX(0); // CCharOBJ°æ¿ì ¿¡¸¸ ÇØ´ç.

    this->SubObjectFromSector(pObj, SECTOR_UPDATE_ALL);

    m_ObjLIST.DeleteNode(&pObj->m_ZoneNODE);

    g_pObjMGR->DelData(pObj->m_iIndex);
    pObj->m_iIndex = 0;

    pObj->SetZONE(NULL);
    if (pObj->IsUSER()) {
        g_pUserLIST->Add_NullZONE(&pObj->m_ZoneNODE); // CZoneTHREAD::Sub_DIRECT
    }
}

//-------------------------------------------------------------------------------------------------
bool
CZoneTHREAD::RegenCharacter(CObjMOB* pNewMOB,
    float fCenterX,
    float fCenterY,
    int iRange,
    int iIndex,
    int iTeamNO) {
    if (iIndex < 1 || iIndex >= g_TblNPC.row_count) {
        return false;
    }
    if (!NPC_NAME(iIndex) || NPC_TYPE(iIndex) >= 900) {
        return false;
    }

    float fXPos, fYPos;

    fCenterX -= iRange;
    fCenterY -= iRange;

    fXPos = fCenterX + RANDOM(iRange * 2);
    fYPos = fCenterY + RANDOM(iRange * 2);

    return pNewMOB->Init(this, iIndex, fXPos, fYPos, iTeamNO, NULL);
}

//-------------------------------------------------------------------------------------------------
CObjMOB*
CZoneTHREAD::RegenMOB(float fCenterX,
    float fCenterY,
    int iRange,
    int iIndex,
    int iCount,
    int iTeamNO) {
    float fXPos, fYPos;

    fCenterX -= iRange;
    fCenterY -= iRange;

    CObjMOB* pNewOBJ = NULL;
    for (int iC = 0; iC < iCount; iC++) {
        fXPos = fCenterX + RANDOM(iRange * 2);
        fYPos = fCenterY + RANDOM(iRange * 2);

        pNewOBJ = new CObjMOB;
        if (!pNewOBJ->Init(this, iIndex, fXPos, fYPos, iTeamNO, NULL)) {
            delete pNewOBJ;
            return NULL;
        }

        // ¿©±â¼± ÇÏÁö ¸»ÀÚ... ÀÌÇÔ¼ö°¡ È£ÃâµÇ´Â °÷ÀÇ À§Ä¡°¡ User RecvPakcetÀÌ¶ó ¾îÂ¾Áö ºÒ¾È..
        // pNewOBJ->Do_CreatedAI ();
    }
    return pNewOBJ;
}

//-------------------------------------------------------------------------------------------------
CObjMOB*
CZoneTHREAD::RegenCharacter(float fCenterX,
    float fCenterY,
    int iRange,
    int iIndex,
    int iCount,
    int iTeamNO,
    bool bImmediate) {
    if (iIndex < 1 || iIndex >= g_TblNPC.row_count) {
        return NULL;
    }
    if (!NPC_NAME(iIndex) || NPC_TYPE(iIndex) >= 900) {
        return NULL;
    }

    if (!bImmediate) {
        classSLLNODE<tagCalledMOB>* pNewNODE;

        m_csZoneMobCALLED.Lock();
        {
            pNewNODE = m_MobCALLED.AllocNAppend();
            // 3ÃÊ µô·¹ÀÌ
            pNewNODE->DATA.m_dwActionTIME = this->GetTimeGetTIME() + 3 * 1000;
            pNewNODE->DATA.m_fCenterX = fCenterX;
            pNewNODE->DATA.m_fCenterY = fCenterY;
            pNewNODE->DATA.m_iRange = iRange;
            pNewNODE->DATA.m_iCharNO = iIndex;
            pNewNODE->DATA.m_iCount = iCount;
            pNewNODE->DATA.m_iTeamNO = iTeamNO;
        }
        m_csZoneMobCALLED.Unlock();

        return NULL;
    }
    return this->RegenMOB(fCenterX, fCenterY, iRange, iIndex, iCount, iTeamNO);
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::RegenCharacter(CRegenPOINT* pRegen, int iIndex, int iCount) {
    if (iIndex < 1 || iIndex >= g_TblNPC.row_count) {
        return;
    }
    if (!NPC_NAME(iIndex)) {
        return;
    }

    float fCX = pRegen->m_fXPos - pRegen->m_iRange;
    float fCY = pRegen->m_fYPos - pRegen->m_iRange;

    float fXPos, fYPos;
    for (int iC = 0; iC < iCount; iC++) {
        fXPos = fCX + RANDOM(pRegen->m_iRange * 2);
        fYPos = fCY + RANDOM(pRegen->m_iRange * 2);

        CObjMOB* pNewOBJ = new CObjMOB;
        if (!pNewOBJ->Init(this, iIndex, fXPos, fYPos, TEAMNO_MOB, pRegen)) {
            delete pNewOBJ;
            return;
        }

        pRegen->AddLiveCNT();
        pNewOBJ->Do_CreatedAI();
    }
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::SendPacketToSectors(CGameOBJ* pGameObj, classPACKET* pCPacket) {
    int iCenterSecX = pGameObj->m_PosSECTOR.x;
    int iCenterSecY = pGameObj->m_PosSECTOR.y;
    int iX, iY;

    for (iY = iCenterSecY - 1; iY <= iCenterSecY + 1; iY++) {
        if (iY < 0 || iY >= Get_SectorYCNT())
            continue;

        for (iX = iCenterSecX - 1; iX <= iCenterSecX + 1; iX++) {
            if (iX < 0 || iX >= Get_SectorXCNT())
                continue;

            m_ppSECTOR[iY][iX].SendPacketToSECTOR(pCPacket, pGameObj);
        }
    }
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::SendTeamPacketToSectors(CGameOBJ* pGameObj, classPACKET* pCPacket, int iTeamNO) {
    // int iX, iY;

    // for (iY=iCenterSecY-1; iY<=iCenterSecY+1; iY++) {
    //	if ( iY < 0 || iY >= Get_SectorYCNT() )
    //		continue;

    //	for (iX=iCenterSecX-1; iX<=iCenterSecX+1; iX++) {
    //		if ( iX < 0 || iX >= Get_SectorXCNT() )
    //			continue;

    //		m_ppSECTOR[ iY ][ iX ].SendTeamPacketToSECTOR( pCPacket, iTeamNO );
    //	}
    //}
}

//-------------------------------------------------------------------------------------------------
bool
CZoneTHREAD::AddObjectToSector(CGameOBJ* pObj, BYTE btUpdateFLAG) {
    classPACKET* pCPacket = Packet_AllocNLock();
    if (!pCPacket)
        return false;

    if (pObj->Make_gsv_ADD_OBJECT(pCPacket)) {
        // pObjectÃß°¡ ÆÐÅ¶ Àü¼Û.
        int iL, iSecX, iSecY;
        for (iL = 0; s_AddSecIdx[btUpdateFLAG][iL] >= 0; iL++) {
            iSecX = pObj->m_PosSECTOR.x + s_SecAdjPos[s_AddSecIdx[btUpdateFLAG][iL]].x;
            iSecY = pObj->m_PosSECTOR.y + s_SecAdjPos[s_AddSecIdx[btUpdateFLAG][iL]].y;

            if (iSecX < 0 || iSecX >= Get_SectorXCNT())
                continue;
            if (iSecY < 0 || iSecY >= Get_SectorYCNT())
                continue;

            if (pObj->IsUSER())
                m_ppSECTOR[iSecY][iSecX].SendAddUserPacket((classUSER*)pObj, pCPacket);
            else {
                m_ppSECTOR[iSecY][iSecX].SendPacketToSECTOR(pCPacket, pObj);
            }
        }
    }
    Packet_ReleaseNUnlock(pCPacket);

    m_ppSECTOR[pObj->m_PosSECTOR.y][pObj->m_PosSECTOR.x].InsToSEC(pObj);

    assert(pObj->m_pGroupSECTOR);

    return true;
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::SubObjectFromSector(CGameOBJ* pObj, char cUpdateFLAG) {
    // m_ppSECTOR[ pObj->m_PosSECTOR.y ][ pObj->m_PosSECTOR.x ].DelFromSEC( pObj );
    pObj->m_pGroupSECTOR->DelFromSEC(pObj);

    /*
        if ( cUpdateFLAG < 0 )
            return;
    */

    classPACKET* pCPacket = Packet_AllocNLock();
    if (!pCPacket)
        return;

    if (pObj->Make_gsv_SUB_OBJECT(pCPacket)) {
        int iL, iSecX, iSecY;
        for (iL = 0; s_SubSecIdx[cUpdateFLAG][iL] >= 0; iL++) {
            iSecX = pObj->m_PosSECTOR.x + s_SecAdjPos[s_SubSecIdx[cUpdateFLAG][iL]].x;
            iSecY = pObj->m_PosSECTOR.y + s_SecAdjPos[s_SubSecIdx[cUpdateFLAG][iL]].y;

            if (iSecX < 0 || iSecX >= Get_SectorXCNT())
                continue;
            if (iSecY < 0 || iSecY >= Get_SectorYCNT())
                continue;

            if (pObj->IsUSER())
                m_ppSECTOR[iSecY][iSecX].SendSubUserPacket((classUSER*)pObj, pCPacket);
            else {
                m_ppSECTOR[iSecY][iSecX].SendPacketToSECTOR(pCPacket, pObj);
            }
        }
    }
    Packet_ReleaseNUnlock(pCPacket);
}

//-------------------------------------------------------------------------------------------------
bool
CZoneTHREAD::UpdateSECTOR(CObjCHAR* pCHAR) {
    POINTS PosADJUST;
    BYTE btUpdateFLAG = 0;

    // ¼½ÅÍ ±×·ì °»½Å - ¼¾ÅÍ ¼½ÅÍ °»½Å.
    if ((int)pCHAR->m_PosCUR.x - pCHAR->m_pGroupSECTOR->m_iCenterX < -(this->m_iSectorLIMIT)) {
        btUpdateFLAG |= SECTOR_UPDATE_LEFT;
        PosADJUST.x = pCHAR->m_PosSECTOR.x - 1;
    } else if ((int)pCHAR->m_PosCUR.x - pCHAR->m_pGroupSECTOR->m_iCenterX > this->m_iSectorLIMIT) {
        btUpdateFLAG |= SECTOR_UPDATE_RIGHT;
        PosADJUST.x = pCHAR->m_PosSECTOR.x + 1;
    } else
        PosADJUST.x = pCHAR->m_PosSECTOR.x;

    if ((int)pCHAR->m_PosCUR.y - pCHAR->m_pGroupSECTOR->m_iCenterY < -(this->m_iSectorLIMIT)) {
        btUpdateFLAG |= SECTOR_UPDATE_UP;
        PosADJUST.y = pCHAR->m_PosSECTOR.y - 1;
    } else if ((int)pCHAR->m_PosCUR.y - pCHAR->m_pGroupSECTOR->m_iCenterY > this->m_iSectorLIMIT) {
        btUpdateFLAG |= SECTOR_UPDATE_DOWN;
        PosADJUST.y = pCHAR->m_PosSECTOR.y + 1;
    } else
        PosADJUST.y = pCHAR->m_PosSECTOR.y;

    if (btUpdateFLAG) {
        // LogString( 0xffff, "%s  [ %d, %d ] => ", pCHAR->Get_NAME(), pCHAR->m_PosSECTOR.x,
        // pCHAR->m_PosSECTOR.y ); NewSECTOR.x = pCHAR->m_PosSECTOR.x + PosADJUST.x; NewSECTOR.y =
        // pCHAR->m_PosSECTOR.y + PosADJUST.y;
        if (PosADJUST.x < 0 || PosADJUST.x >= this->Get_SectorXCNT() || PosADJUST.y < 0
            || PosADJUST.y >= this->Get_SectorYCNT()) {
            return false;
        }

        // ¼¾ÅÍ ¼½ÅÍ¸¦ ¹Ù²Û´Ù. - ±×·ìÀ» ¹Ù²Û´Ù.
        this->SubObjectFromSector(pCHAR, btUpdateFLAG); // classZONE::SetMapPos

        pCHAR->m_PosSECTOR = PosADJUST;

        this->AddObjectToSector(pCHAR, btUpdateFLAG); // classZONE::SetMapPos
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
CObjCHAR*
CZoneTHREAD::FindFirstCHAR(tPOINTF& PosCENTER, int iDistance, CGameOBJ* pObjFINDER) {
    m_iFindSecIDX = 0;
    m_iFindDistance = iDistance;
    m_pFindObjNODE = NULL;
    m_FindPOS = PosCENTER;
    m_pObjFINDER = pObjFINDER;

    m_FindSECTOR.x = (short)(m_FindPOS.x / this->Get_SectorSIZE());
    m_FindSECTOR.y = (short)(m_FindPOS.y / this->Get_SectorSIZE());

    return this->FindNextCHAR();
}
//-------------------------------------------------------------------------------------------------
CObjCHAR*
CZoneTHREAD::FindFirstCHAR(CObjCHAR* pCenterCHAR, int iDistance, CGameOBJ* pObjFINDER) {
    m_iFindSecIDX = 0;
    m_iFindDistance = iDistance;
    m_pFindObjNODE = NULL;
    m_FindPOS = pCenterCHAR->m_PosCUR;
    m_pObjFINDER = pObjFINDER;

    m_FindSECTOR = pCenterCHAR->m_PosSECTOR;

    return this->FindNextCHAR();
}

//-------------------------------------------------------------------------------------------------
CObjCHAR*
CZoneTHREAD::FindNextCHAR() {
    int iSecX, iSecY;

    for (; m_iFindSecIDX < 9; m_iFindSecIDX++) {
        iSecX = m_FindSECTOR.x + s_SecAdjPos[s_AddSecIdx[SECTOR_UPDATE_ALL][m_iFindSecIDX]].x;
        iSecY = m_FindSECTOR.y + s_SecAdjPos[s_AddSecIdx[SECTOR_UPDATE_ALL][m_iFindSecIDX]].y;

        if (iSecX < 0 || iSecX >= Get_SectorXCNT())
            continue;
        if (iSecY < 0 || iSecY >= Get_SectorYCNT())
            continue;

        if (!m_pFindObjNODE)
            m_pFindObjNODE = m_ppSECTOR[iSecY][iSecX].FindFirstCHAR(m_pObjFINDER);
        else
            m_pFindObjNODE = m_ppSECTOR[iSecY][iSecX].FindNextCHAR(m_pFindObjNODE, m_pObjFINDER);

        while (m_pFindObjNODE) {
            if (((CObjCHAR*)m_pFindObjNODE->DATA)->Get_HP() > 0) {
                if (((CObjCHAR*)m_pFindObjNODE->DATA)->Get_DISTANCE(m_FindPOS) <= m_iFindDistance) {
                    return ((CObjCHAR*)(m_pFindObjNODE->DATA));
                }
            }

            m_pFindObjNODE = m_ppSECTOR[iSecY][iSecX].FindNextCHAR(m_pFindObjNODE, m_pObjFINDER);
        }
    }

    return NULL;
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::Kill_AllMOB(classUSER* pUSER, bool drop_items) {
    if (!pUSER) {
        return;
    }

    classDLLNODE<CGameOBJ*>* object_node = m_ObjLIST.GetHeadNode();
    if (!object_node) {
        return;
    }

    uniDAMAGE max_damage;
    max_damage.m_wDamage = GameStaticConfig::MAX_DAMAGE;

    while (object_node) {
        if (object_node->DATA->IsA(OBJ_MOB)) {
            CObjCHAR* mob = reinterpret_cast<CObjCHAR*>(object_node->DATA);
            if (mob) {
                while (mob->Get_HP() > 0) {
                    pUSER->Give_DAMAGE(mob, max_damage, drop_items);
                }
            }
        }
        object_node = m_ObjLIST.GetNextNode(object_node);
    }
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::Reset_REGEN() {
    classDLLNODE<CRegenPOINT*>* pRegenNODE;

    pRegenNODE = m_RegenLIST.GetHeadNode();
    while (pRegenNODE) {
        pRegenNODE->DATA->Reset();
        pRegenNODE = m_RegenLIST.GetNextNode(pRegenNODE);
    }
}

//-------------------------------------------------------------------------------------------------
short
CZoneTHREAD::GetRegenPointMOB(short nIndex, classUSER* pUSER) {
    if (nIndex < 0 || nIndex >= m_RegenLIST.GetNodeCount())
        return 0;

    classDLLNODE<CRegenPOINT*>* pRegenNODE;

    short nCnt = 0;
    CObjMOB* pMOB;
    CStrVAR* pStrVAR = this->GetStrVAR();

    pRegenNODE = m_RegenLIST.GetHeadNode();
    while (pRegenNODE) {
        if (nCnt == nIndex) {
            nCnt = 0;
            classDLLNODE<CGameOBJ*>*pObjNODE, *pNextNODE;
            for (pObjNODE = m_ObjLIST.GetHeadNode(); pObjNODE;) {
                pNextNODE = m_ObjLIST.GetNextNode(pObjNODE);

                if (pObjNODE->DATA->IsA(OBJ_MOB)) {
                    pMOB = (CObjMOB*)pObjNODE->DATA;
                    if (pMOB->m_pRegenPOINT == pRegenNODE->DATA) {
                        pStrVAR->Printf("%d:%s, HP:%d, CMD:%x, (%.0f,%.0f)",
                            nCnt,
                            pMOB->Get_NAME(),
                            pMOB->Get_HP(),
                            pMOB->Get_COMMAND(),
                            pMOB->m_PosCUR.x,
                            pMOB->m_PosCUR.y);
                        pUSER->Send_gsv_WHISPER(">", pStrVAR->Get());
                        nCnt++;
                    }
                }

                pObjNODE = pNextNODE;
            }
            return nCnt;
        }

        nCnt++;
        pRegenNODE = m_RegenLIST.GetNextNode(pRegenNODE);
    }

    return 0;
}

//-------------------------------------------------------------------------------------------------
short
CZoneTHREAD::CallRegenPointMOB(short nIndex) {
    if (nIndex < 0 || nIndex >= m_RegenLIST.GetNodeCount())
        return 0;

    classDLLNODE<CRegenPOINT*>* pRegenNODE;

    CObjMOB* pMOB;
    short nCnt = 0;
    pRegenNODE = m_RegenLIST.GetHeadNode();
    while (pRegenNODE) {
        if (nCnt == nIndex) {
            nCnt = 0;
            classDLLNODE<CGameOBJ*>*pObjNODE, *pNextNODE;
            for (pObjNODE = m_ObjLIST.GetHeadNode(); pObjNODE;) {
                pNextNODE = m_ObjLIST.GetNextNode(pObjNODE);

                if (pObjNODE->DATA->IsA(OBJ_MOB)) {
                    pMOB = (CObjMOB*)pObjNODE->DATA;
                    if (pMOB->Get_HP() > 0) {
                        if (((CObjMOB*)pObjNODE->DATA)->m_pRegenPOINT == pRegenNODE->DATA) {
                            // ¼ÒÈ¯..
                            this->Sub_DIRECT(pMOB, false);
                            pMOB->m_PosCUR.x = pRegenNODE->DATA->m_fXPos;
                            pMOB->m_PosCUR.y = pRegenNODE->DATA->m_fYPos;
                            pMOB->CObjAI::SetCMD_STOP();
                            this->Add_OBJECT(pMOB);
                            nCnt++;
                        }
                    }
                }

                pObjNODE = pNextNODE;
            }
            return nCnt;
        }

        nCnt++;
        pRegenNODE = m_RegenLIST.GetNextNode(pRegenNODE);
    }

    return 0;
}

//-------------------------------------------------------------------------------------------------
short
CZoneTHREAD::GetRegenPointCNT() {
    return m_RegenLIST.GetNodeCount();
}

CRegenPOINT*
CZoneTHREAD::GetRegenPOINT(short nIndex) {
    if (nIndex < 0 || nIndex >= m_RegenLIST.GetNodeCount())
        return NULL;

    classDLLNODE<CRegenPOINT*>* pRegenNODE;

    short nCnt = 0;
    pRegenNODE = m_RegenLIST.GetHeadNode();
    while (pRegenNODE) {
        if (nCnt == nIndex) {
            return pRegenNODE->DATA;
        }

        nCnt++;
        pRegenNODE = m_RegenLIST.GetNextNode(pRegenNODE);
    }

    return NULL;
}

//-------------------------------------------------------------------------------------------------
short
CZoneTHREAD::GetNearstRegenPOINT(tPOINTF& PosCENTER) {
    float fDist, fNearDist = (float)MAX_DWORD;

    classDLLNODE<CRegenPOINT*>* pRegenNODE;

    tPOINTF PosREGEN;
    pRegenNODE = m_RegenLIST.GetHeadNode();

    short nCnt = 0, nNearst = 0;
    while (pRegenNODE) {
        PosREGEN.x = pRegenNODE->DATA->m_fXPos;
        PosREGEN.y = pRegenNODE->DATA->m_fYPos;

        fDist = ::distance(PosCENTER, PosREGEN);
        if (fDist < fNearDist) {
            fNearDist = fDist;
            nNearst = nCnt;
        }
        nCnt++;

        pRegenNODE = m_RegenLIST.GetNextNode(pRegenNODE);
    }

    return nNearst;
}

//-------------------------------------------------------------------------------------------------
void
CZoneTHREAD::Do_QuestTrigger(short nTeamNO, t_HASHKEY HashKEY) {
    classSLLNODE<tagZoneTRIGGER>* pQstNODE;
    this->m_csTriggerLIST.Lock();
    {
        pQstNODE = m_TriggerLIST.AllocNAppend();

        pQstNODE->DATA.m_nTeamNO = nTeamNO;
        pQstNODE->DATA.m_TriggerHASH = HashKEY;
    }
    this->m_csTriggerLIST.Unlock();
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
