#include "stdafx.h"

#include "LIB_gsMAIN.h"

#include "CSocketWND.h"

#include "GS_ThreadSQL.h"
#include "GS_ThreadZONE.h"

#include "CThreadGUILD.h"
#include "GS_ListUSER.h"
#include "GS_PARTY.h"
#include "GS_SocketLSV.h"
#include "IO_AI.h"
#include "rose/io/stb.h"
#include "IO_Quest.h"
#include "IO_Skill.h"
#include "ZoneLIST.h"
#include "classTIME.h"
#include "ioDataPOOL.h"

#include "rose/common/common_interface.h"

#define TEST_ZONE_NO 100

#define MAX_GAME_OBJECTS 65535
#define DEF_GAME_USER_POOL_SIZE 8192
#define INC_GAME_USER_POOL_SIZE 1024

#define DEF_GAME_PARTY_POOL_SIZE 4096
#define INC_GAME_PARTY_POOL_SIZE 1024

#define INC_SEND_IO_POOL_SIZE 8192
#define DEF_SEND_IO_POOL_SIZE 32768

STBDATA g_TblHAIR;
STBDATA g_TblFACE;
STBDATA g_TblARMOR;
STBDATA g_TblGAUNTLET;
STBDATA g_TblBOOTS;
STBDATA g_TblHELMET;
STBDATA g_TblWEAPON;
STBDATA g_TblSUBWPN;
STBDATA g_TblEFFECT;
STBDATA g_TblNPC;
STBDATA g_TblAniTYPE;
STBDATA g_TblPRODUCT;
STBDATA g_TblNATUAL;

STBDATA g_TblFACEITEM;
STBDATA g_TblUSEITEM;
STBDATA g_TblBACKITEM;
STBDATA g_TblGEMITEM;
STBDATA g_TblQUESTITEM;
STBDATA g_TblJEWELITEM;
STBDATA g_PatITEM;

STBDATA g_TblDropITEM;

STBDATA g_TblStore;

STBDATA g_TblWARP;
STBDATA g_TblEVENT;

STBDATA g_TblZONE;

STBDATA* g_pTblSTBs[ITEM_TYPE_RIDE_PART + 1];

STBDATA g_TblAVATAR;
STBDATA g_TblSTATE;

STBDATA g_TblUnion;
STBDATA g_TblClass;
STBDATA g_TblItemGRADE;

STBDATA g_TblSkillPoint;

CObjMNG* g_pObjMGR = NULL;
CAI_LIST g_AI_LIST;

CUserLIST* g_pUserLIST;
CZoneLIST* g_pZoneLIST;
CCharDatLIST* g_pCharDATA;
CMotionLIST g_MotionFILE;
CPartyBUFF* g_pPartyBUFF;

GS_lsvSOCKET* g_pSockLSV = NULL;

GS_CThreadSQL* g_pThreadSQL = NULL;
CThreadGUILD* g_pThreadGUILD = NULL;

char g_szURL[MAX_PATH];

//#define BASE_DATA_DIR m_BaseDataDIR.Get()
#define BASE_DATA_DIR (char*)this->config.gameserver.data_dir.c_str()

CLIB_GameSRV* CLIB_GameSRV::m_pInstance = NULL;

FILE* g_fpTXT = NULL;

using namespace Rose::Common;

#define WM_LSVSOCK_MSG (WM_SOCKETWND_MSG + 0)
#define WORLD_TIME_TICK 10000 // 10 sec

void CALLBACK
GS_TimerProc(HWND hwnd /* handle to window */,
    UINT uMsg /* WM_TIMER message */,
    UINT_PTR idEvent /* timer identifier */,
    DWORD dwTime /* current system time */) {
    switch (idEvent) {
        case GS_TIMER_LSV: {
            CLIB_GameSRV* pGS = CLIB_GameSRV::GetInstance();
            if (pGS) {
                pGS->ConnectToLSV();
            }
            break;
        }

        case GS_TIMER_WORLD_TIME: {
            g_pZoneLIST->Inc_WorldTIME();

            switch (g_pZoneLIST->m_dwAccTIME % 6) {
                case 0:
                case 3: // 30초에 한번씩 체크...
                    g_pUserLIST->Check_SocketALIVE();
                    break;
                case 1: // case 4 :
                    g_pUserLIST->CloseIdleSCOKET(90 * 1000);
                    break;
            }

            break;
        }
    };
}

void
WriteLOG(char* szMSG) {
    LOG_INFO(szMSG);
}

CLIB_GameSRV::CLIB_GameSRV() {
#if !defined(__SERVER)
    COMPILE_TIME_ASSERT(0);
#endif

    COMPILE_TIME_ASSERT(MAX_ZONE_USER_BUFF > 4096);
    COMPILE_TIME_ASSERT(sizeof(gsv_SELECT_CHAR) < 1024);
    COMPILE_TIME_ASSERT(sizeof(tagGrowAbility) < 384);
    COMPILE_TIME_ASSERT(
        (sizeof(__int64) + sizeof(tagITEM) * INVENTORY_TOTAL_SIZE) == sizeof(CInventory));
}

CLIB_GameSRV::~CLIB_GameSRV() {
    Shutdown();

    if (g_pThreadSQL) {
        g_pThreadSQL->Free();
        SAFE_DELETE(g_pThreadSQL);
    }

    if (g_pThreadGUILD) {
        g_pThreadGUILD->Free();
        SAFE_DELETE(g_pThreadGUILD);
    }

    DisconnectFromLSV();

    SAFE_DELETE(g_pUserLIST);
    SAFE_DELETE(g_pPartyBUFF);

    m_pInstance = NULL;

    SAFE_DELETE(g_pSockLSV);

    Free_BasicDATA();
    g_pCharDATA->Destroy();

    CPoolSENDIO::Destroy();

    CStr::Free();

    if (CSocketWND::GetInstance())
        CSocketWND::GetInstance()->Destroy();

    SAFE_DELETE_ARRAY(m_pCheckedLocalZONE);

    CSOCKET::Free();
}

void
CLIB_GameSRV::init(HINSTANCE hInstance, const ServerConfig& config) {
    this->config = config;

    m_pInstance = this;

    if (this->config.gameserver.language < 0) {
        this->config.gameserver.language = 0;
    }

    if (this->config.gameserver.data_dir.back() != '\\') {
        this->config.gameserver.data_dir.push_back('\\');
    }

    m_iLangTYPE = this->config.gameserver.language;
    m_pCheckedLocalZONE = NULL;
    m_pWorldTIMER = NULL;

    CSOCKET::Init();
    CStr::Init();

    CPoolSENDIO::Instance(DEF_SEND_IO_POOL_SIZE, INC_SEND_IO_POOL_SIZE);

    g_pCharDATA = CCharDatLIST::Instance();

    Load_BasicDATA();

    g_pSockLSV = new GS_lsvSOCKET;

    //	g_pSockLOG = new GS_logSOCKET( USE_MY_SQL_AGENT );

    g_pPartyBUFF = new CPartyBUFF(MAX_PARTY_BUFF);
    g_pUserLIST = new CUserLIST(DEF_GAME_USER_POOL_SIZE, INC_GAME_USER_POOL_SIZE);

    // Lsv, Log = 2
    CSocketWND* pSockWND = CSocketWND::InitInstance(hInstance, 2);
    if (pSockWND) {
        pSockWND->AddSocket(&g_pSockLSV->m_SockLSV, WM_LSVSOCK_MSG);
    }

    this->InitLocalZone(true);
}

bool
CLIB_GameSRV::CheckSTB_UseITEM() {
    for (short nD = 0; nD < g_TblUSEITEM.row_count; nD++) {
        if (USEITEM_COOLTIME_TYPE(nD) < 0)
            SET_USEITEM_COOLTIME_TYPE(nD, 0);
        else if (USEITEM_COOLTIME_TYPE(nD) >= MAX_USEITEM_COOLTIME_TYPE)
            SET_USEITEM_COOLTIME_TYPE(nD, 0);
        if (USEITEM_COOLTIME_DELAY(nD) < 0)
            SET_USEITEM_COOLTIME_DELAY(nD, 0);

        if (0 == USEITME_STATUS_STB(nD))
            continue;

        short nIngSTB = USEITME_STATUS_STB(nD);
        short nDuringTime;

        for (short nI = 0; nI < STATE_APPLY_ABILITY_CNT; nI++) {
            if (!STATE_APPLY_ABILITY_VALUE(nIngSTB, nI))
                continue;

            assert(0 != STATE_APPLY_ABILITY_VALUE(nIngSTB, nI));

            // 총 증가할 수치.
            nDuringTime = USEITEM_ADD_DATA_VALUE(nD) / STATE_APPLY_ABILITY_VALUE(nIngSTB, nI);
        }
    }

    return true;
}

bool
CLIB_GameSRV::CheckSTB_NPC() {
    t_HASHKEY HashKey;
    CQuestTRIGGER* pQuestTrigger;

    int iDropTYPE;
    for (short nI = 0; nI < g_TblNPC.row_count; nI++) {
        if (NPC_LEVEL(nI) < 1)
            SET_NPC_LEVEL(nI, 1);
        if (NPC_ATK_SPEED(nI) <= 0)
            SET_NPC_ATK_SPEED(nI, 100);
        if (NPC_DROP_TYPE(nI) >= g_TblDropITEM.row_count) {
            iDropTYPE = NPC_DROP_TYPE(nI);
            SET_NPC_DROP_TYPE(nI, 0);

            assert(NPC_DROP_TYPE(nI) < g_TblDropITEM.row_count);
        }

        if (nI && NPC_DEAD_EVENT(nI)) {
            HashKey = ::StrToHashKey(NPC_DEAD_EVENT(nI));
            pQuestTrigger = g_QuestList.GetQuest(HashKey);
            if (!pQuestTrigger) {
                SET_NPC_DEAD_EVENT(nI, NULL);
            } else {
                do {
                    pQuestTrigger->m_iOwerNpcIDX = nI; // 죽을때 발생되는 트리거다.
                    pQuestTrigger = pQuestTrigger->m_pNextTrigger;
                } while (pQuestTrigger);
            }
        }
    }
    return true;
}

bool
CLIB_GameSRV::CheckSTB_DropITEM() {
    //	/*
    //아이템 드롭 계산방식이 바뀌면서 stb참조 값이 틀려졌다.
    int iDropITEM;
    tagITEM sITEM;

    for (short nI = 1; nI < g_TblDropITEM.row_count; nI++) {
        for (short nC = 1; nC < g_TblDropITEM.col_count; nC++) {
            iDropITEM = DROPITEM_ITEMNO(nI, nC);
            if (iDropITEM <= 0)
                continue;

            sITEM.m_cType = (BYTE)(iDropITEM / 1000);
            sITEM.m_nItemNo = iDropITEM % 1000;

            if ((sITEM.m_cType < ITEM_TYPE_FACE_ITEM || sITEM.m_cType > ITEM_TYPE_RIDE_PART
                    || sITEM.m_cType == ITEM_TYPE_QUEST)
                && iDropITEM > 1000) {
                SET_DROPITEM_ITEMNO(nI, nC, 0);
                continue;
            }

            if (iDropITEM <= 1000) {
                if (iDropITEM >= 1 && iDropITEM <= 4) {
                    // 다시 계산
                    int iDropTblIDX = 26 + (iDropITEM * 5) + 4 /*RANDOM(5)의 최대값 4 */;
                    if (iDropTblIDX >= g_TblDropITEM.col_count) {
                        // 테이블 컬럼 갯수 초과...
                        g_LOG.CS_ODS(0xffff, "This drop item[ %d %d ] may be too big\n", nI, nC);
                    }
                    continue;
                }
                SET_DROPITEM_ITEMNO(nI, nC, 0);
                continue;
            }

            if (sITEM.m_nItemNo > g_pTblSTBs[sITEM.m_cType]->row_count) {
                SET_DROPITEM_ITEMNO(nI, nC, 0);
                continue;
            }

            assert(DROPITEM_ITEMNO(nI, nC) > 1000);
        }
    }
    //	*/

    return true;
}

bool
CLIB_GameSRV::CheckSTB_ItemRateTYPE() {
    short nD, nRateTYPE;

    for (nD = 0; nD < g_TblUSEITEM.row_count; nD++) {
        nRateTYPE = ITEM_RATE_TYPE(ITEM_TYPE_USE, nD);
        assert(nRateTYPE >= 0 && nRateTYPE < MAX_PRICE_TYPE);
    }

    for (nD = 0; nD < g_TblNATUAL.row_count; nD++) {
        nRateTYPE = ITEM_RATE_TYPE(ITEM_TYPE_NATURAL, nD);
        assert(nRateTYPE >= 0 && nRateTYPE < MAX_PRICE_TYPE);
    }
    return true;
}

bool
CLIB_GameSRV::CheckSTB_Motion() {
    short nX, nY, nFileIDX;

    // type_motion.stb
    for (nY = 0; nY < g_TblAniTYPE.row_count; nY++) {
        for (nX = 0; nX < g_TblAniTYPE.col_count; nX++) {
            nFileIDX = FILE_MOTION(nX, nY);
            if (!nFileIDX)
                continue;
        }
    }
    return true;
}

bool
CLIB_GameSRV::CheckSTB_GemITEM() {
    short nType, nValue;

    for (short nC = 0; nC < g_TblGEMITEM.row_count; nC++) {
        for (short nI = 0; nI < 2; nI++) {
            nType = GEMITEM_ADD_DATA_TYPE(nC, nI);
            nValue = GEMITEM_ADD_DATA_VALUE(nC, nI);

            _ASSERT(nType >= 0 && nType <= AT_MAX);
        }
    }
    return true;
}

bool
CLIB_GameSRV::CheckSTB_ListPRODUCT() {
    tagITEM sOutITEM;
    for (short nI = 0; nI < g_TblPRODUCT.row_count; nI++) {
        for (short nS = 0; nS < 4; nS++) {
            if (PRODUCT_NEED_ITEM_NO(nI, nS)) {
                sOutITEM.Init(PRODUCT_NEED_ITEM_NO(nI, nS), 1);
                if (!sOutITEM.IsValidITEM()) {
                    _ASSERT(0);
                }
                // 재료 아이템 번호
                _ASSERT(PRODUCT_NEED_ITEM_CNT(nI, nS) > 0); // 팰요 갯수
            }
        }
    }

    return true;
}

#include "IP_Addr.h"
bool
CLIB_GameSRV::Load_BasicDATA() {
    size_t ttt = sizeof(tagGrowAbility);

    if (!g_AI_LIST.Load(BASE_DATA_DIR,
            "3DDATA\\STB\\FILE_AI.STB",
            "3DDATA\\AI\\AI_s.STB",
            m_iLangTYPE))
        return false;

    std::filesystem::path base_dir(BASE_DATA_DIR);

    g_TblGAUNTLET.load(base_dir / ARMS_STB);
    g_TblBACKITEM.load(base_dir / BACK_STB);
    g_TblARMOR.load(base_dir / BODY_STB);
    g_TblHELMET.load(base_dir / CAP_STB);
    g_TblClass.load(base_dir / CLASS_STB);
    g_TblEFFECT.load(base_dir / EFFECT_STB);
    g_TblEVENT.load(base_dir / EVENT_STB);
    g_TblFACE.load(base_dir / FACE_STB);
    g_TblFACEITEM.load(base_dir / FACE_ITEM_STB);
    g_TblBOOTS.load(base_dir / FOOT_STB);
    g_TblItemGRADE.load(base_dir / GRADE_STB);
    g_TblHAIR.load(base_dir / HAIR_STB);
    g_TblDropITEM.load(base_dir / ITEM_DROP_STB);
    g_TblAVATAR.load(base_dir / INIT_AVATAR_STB);
    g_TblGEMITEM.load(base_dir / JEM_ITEM_STB);
    g_TblJEWELITEM.load(base_dir / JEWEL_STB);
    g_TblNATUAL.load(base_dir / NATURAL_STB);
    g_TblNPC.load(base_dir / NPC_STB);
    g_TblPRODUCT.load(base_dir / PRODUCT_STB);
    g_PatITEM.load(base_dir / PAT_STB);
    g_TblQUESTITEM.load(base_dir / QUEST_ITEM_STB);
    g_TblStore.load(base_dir / SELL_STB);
    g_TblSkillPoint.load(base_dir / SKILL_P_STB);
    g_TblSTATE.load(base_dir / STATUS_STB);
    g_TblSUBWPN.load(base_dir / SUBWPN_STB);
    g_TblAniTYPE.load(base_dir / TYPE_MOTION_STB);
    g_TblUnion.load(base_dir / UNION_STB);
    g_TblUSEITEM.load(base_dir / USE_ITEM_STB);
    g_TblWARP.load(base_dir / WARP_STB);
    g_TblWEAPON.load(base_dir / WEAPON_STB);
    g_TblZONE.load(base_dir / ZONE_STB);

    if (!g_MotionFILE.Load("3DDATA\\STB\\FILE_MOTION.stb", 0, BASE_DATA_DIR))
        return false;

    if (!g_QuestList.LoadQuestTable("3DDATA\\STB\\LIST_Quest.STB",
            "3DDATA\\STB\\LIST_QuestDATA.STB",
            BASE_DATA_DIR,
            "3DDATA\\QuestData\\Quest_s.STB",
            m_iLangTYPE))
        return false;

    if (!g_SkillList.LoadSkillTable(
            CStr::Printf("%s%s", BASE_DATA_DIR, "3DDATA\\STB\\LIST_SKILL.STB")))
        return false;

    if (!g_pCharDATA->Load_MOBorNPC(BASE_DATA_DIR, "3DDATA\\NPC\\LIST_NPC.CHR")) {
        assert(0 && "LIST_NPC.chr loading error");
        return false;
    }

    g_pTblSTBs[ITEM_TYPE_FACE_ITEM] = &g_TblFACEITEM;
    g_pTblSTBs[ITEM_TYPE_HELMET] = &g_TblHELMET;
    g_pTblSTBs[ITEM_TYPE_ARMOR] = &g_TblARMOR;
    g_pTblSTBs[ITEM_TYPE_GAUNTLET] = &g_TblGAUNTLET;
    g_pTblSTBs[ITEM_TYPE_BOOTS] = &g_TblBOOTS;
    g_pTblSTBs[ITEM_TYPE_KNAPSACK] = &g_TblBACKITEM;
    g_pTblSTBs[ITEM_TYPE_JEWEL] = &g_TblJEWELITEM;
    g_pTblSTBs[ITEM_TYPE_WEAPON] = &g_TblWEAPON;
    g_pTblSTBs[ITEM_TYPE_SUBWPN] = &g_TblSUBWPN;
    g_pTblSTBs[ITEM_TYPE_USE] = &g_TblUSEITEM;
    g_pTblSTBs[ITEM_TYPE_GEM] = &g_TblGEMITEM;
    g_pTblSTBs[ITEM_TYPE_NATURAL] = &g_TblNATUAL;
    g_pTblSTBs[ITEM_TYPE_QUEST] = &g_TblQUESTITEM;
    g_pTblSTBs[ITEM_TYPE_RIDE_PART] = &g_PatITEM;

    //	CheckSTB_AllITEM ();
    CheckSTB_UseITEM();
    CheckSTB_DropITEM();
    CheckSTB_NPC();

    CheckSTB_ItemRateTYPE();
    CheckSTB_Motion();
    CheckSTB_GemITEM();
    CheckSTB_ListPRODUCT();

    return true;
}

void
CLIB_GameSRV::Free_BasicDATA() {
    // STBDATA는 자동 풀림..
    g_QuestList.Free();
    g_SkillList.Free();
    g_MotionFILE.Free();
    g_AI_LIST.Free();
}

//-------------------------------------------------------------------------------------------------
char*
CLIB_GameSRV::GetZoneName(short nZoneNO) {
    if (nZoneNO > 0 && nZoneNO < g_TblZONE.row_count) {
        if (nZoneNO >= TEST_ZONE_NO)
            return NULL;

        if (ZONE_NAME(nZoneNO) && ZONE_FILE(nZoneNO)) {
            char* szZoneFILE = CStr::Printf("%s%s", BASE_DATA_DIR, ZONE_FILE(nZoneNO));
            if (CUtil::Is_FileExist(szZoneFILE)) {
                return ZONE_NAME(nZoneNO);
            }
        }
    }
    return NULL;
}

short
CLIB_GameSRV::InitLocalZone(bool bAllActive) {
    SAFE_DELETE_ARRAY(m_pCheckedLocalZONE);

    m_pCheckedLocalZONE = new bool[g_TblZONE.row_count];

    ::FillMemory(m_pCheckedLocalZONE, sizeof(bool) * g_TblZONE.row_count, bAllActive);

    for (short nI = TEST_ZONE_NO; nI < g_TblZONE.row_count; nI++) {
        m_pCheckedLocalZONE[nI] = false;
    }

    return g_TblZONE.row_count;
}
bool
CLIB_GameSRV::CheckZoneToLocal(short nZoneNO, bool bChecked) {
    if (nZoneNO > 0 && nZoneNO < g_TblZONE.row_count) {
        if (nZoneNO >= TEST_ZONE_NO)
            return false;

        m_pCheckedLocalZONE[nZoneNO] = bChecked;
        return m_pCheckedLocalZONE[nZoneNO];
    }

    return false;
}

//-------------------------------------------------------------------------------------------------
#define IS_SIGNED(type) (((type)(-1)) < ((type)0))
#define IS_UNSIGNED(type) (((type)(-1)) > ((type)0))

#ifndef COMPILE_TIME_ASSERT
    #define COMPILE_TIME_ASSERT(expr) \
        { typedef int compile_time_assert_fail[1 - 2 * !(expr)]; }
#endif

//-------------------------------------------------------------------------------------------------
bool
CLIB_GameSRV::connect_database() {
    if (!this->GetInstance()) {
        return false;
    }

    g_pThreadSQL = new GS_CThreadSQL;
    if (!g_pThreadSQL->db.connect(this->config.database.connection_string)) {
        std::string error_message = g_pThreadSQL->db.last_error_message();
        LOG_ERROR("Failed to connect to the database: {}", error_message.c_str());

        g_pThreadSQL = NULL;
        return false;
    }
    g_pThreadSQL->Resume();

    g_pThreadGUILD = new CThreadGUILD(32, 16);
    g_pThreadGUILD->set_config(this->config);
    if (!g_pThreadGUILD->db.connect(this->config.database.connection_string)) {
        std::string error_message = g_pThreadGUILD->db.last_error_message();
        LOG_ERROR("Failed to connect to the database: {}", error_message.c_str());

        g_pThreadGUILD = NULL;
        return false;
    }
    g_pThreadGUILD->Resume();

    return true;
}

bool
CLIB_GameSRV::reload_game_config() {
    Rose::Common::ServerConfig config;
    bool config_loaded = config.load(this->config.path, "ROSE");
    if (!config_loaded) {
        return false;
    }

    this->config.game = config.game;

    g_pUserLIST->for_each_user([](classUSER* user) { user->UpdateAbility(); });
    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CLIB_GameSRV::Start(HWND hMainWND, BYTE btChannelNO, BYTE btLowAge, BYTE btHighAge) {
    srand(timeGetTime());

    m_dwUserLIMIT = MAX_ZONE_USER_BUFF;

    g_pSockLSV->Init(CSocketWND::GetInstance()->GetWindowHandle(),
        (char*)this->config.worldserver.ip.c_str(),
        this->config.worldserver.server_port,
        WM_LSVSOCK_MSG);

    this->ConnectToLSV();

    m_btChannelNO = btChannelNO;
    m_btLowAGE = btLowAge;
    m_btHighAGE = btHighAge;

    COMPILE_TIME_ASSERT(sizeof(t_PACKETHEADER) == 6);
    COMPILE_TIME_ASSERT(sizeof(char) == 1);
    COMPILE_TIME_ASSERT(sizeof(short) >= 2);
    COMPILE_TIME_ASSERT(sizeof(long) >= 4);
    COMPILE_TIME_ASSERT(sizeof(int) >= sizeof(short));
    COMPILE_TIME_ASSERT(sizeof(long) >= sizeof(int));

    COMPILE_TIME_ASSERT(IS_SIGNED(long));
    COMPILE_TIME_ASSERT(IS_UNSIGNED(DWORD));

    this->Shutdown();

    g_pObjMGR = new CObjMNG(MAX_GAME_OBJECTS);
    g_pZoneLIST = CZoneLIST::Instance();

    g_pZoneLIST->InitZoneLIST(BASE_DATA_DIR);

    m_dwRandomSEED = ::timeGetTime();
    
    m_pWorldTIMER = new CTimer(CSocketWND::GetInstance()->GetWindowHandle(),
        GS_TIMER_WORLD_TIME,
        WORLD_TIME_TICK,
        (TIMERPROC)GS_TimerProc);
    m_pWorldTIMER->Start();

    g_pUserLIST->Active(config.gameserver.port, MAX_ZONE_USER_BUFF, 5 * 60); // 5분 대기.

    return true;
}

//-------------------------------------------------------------------------------------------------
void
CLIB_GameSRV::Shutdown() {
    SAFE_DELETE(m_pWorldTIMER); // 타이머 삭제가 앞서도록...

    g_pUserLIST->ShutdownACCEPT();

    if (g_pZoneLIST) {
        g_pZoneLIST->Destroy();
        g_pZoneLIST = NULL;
    }

    g_pUserLIST->ShutdownWORKER();
    g_pUserLIST->ShutdownSOCKET();

    // sql thread의 모든 내용이 기록 될동안 대기...
    if (g_pThreadSQL) {
        _ASSERT(g_pThreadSQL);
        g_pThreadSQL->Set_EVENT();
        do {
            ::Sleep(200); // wait 0.2 sec
        } while (
            !g_pThreadSQL->IsWaiting() || g_pThreadSQL->WaitUserCNT() > 0); // 처리중이면 대기..
    }

    SAFE_DELETE(g_pObjMGR);
    //	SAFE_DELETE( m_pWorldTIMER );
}

//-------------------------------------------------------------------------------------------------
bool
CLIB_GameSRV::ConnectToLSV() {
    return g_pSockLSV->Connect();
}
void
CLIB_GameSRV::DisconnectFromLSV() {
    g_pSockLSV->Disconnect();
}

void
CLIB_GameSRV::Set_UserLIMIT(DWORD dwUserLimit) {
    m_dwUserLIMIT = dwUserLimit;

    if (g_pSockLSV) {
        g_pSockLSV->Send_srv_USER_LIMIT(dwUserLimit);
    }
}
