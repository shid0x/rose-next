#include "stdAFX.h"

#include "CWS_Client.h"
#include "CWS_Server.h"
#include "rose/io/stb.h"
#include "SHO_WS_LIB.h"
#include "WS_SocketLSV.h"
#include "WS_ThreadSQL.h"
#include "WS_ZoneLIST.h"

#include "CChatROOM.h"
#include "CThreadGUILD.h"
#include "CThreadMSGR.h"
#include "IO_Skill.h"

#include "rose/common/server_config.h"

using namespace Rose::Common;

STBDATA g_TblHAIR;
STBDATA g_TblFACE;
STBDATA g_TblARMOR;
STBDATA g_TblGAUNTLET;
STBDATA g_TblBOOTS;
STBDATA g_TblHELMET;
STBDATA g_TblWEAPON;
STBDATA g_TblSUBWPN;

STBDATA g_TblFACEITEM;
STBDATA g_TblUSEITEM;
STBDATA g_TblBACKITEM;
STBDATA g_TblGEMITEM;
STBDATA g_TblJEWELITEM;
STBDATA g_TblNATUAL;
STBDATA g_TblQUESTITEM;
STBDATA g_PatITEM;

/*
STBDATA			 g_TblEFFECT;
STBDATA			 g_TblNPC;
STBDATA			 g_TblAniTYPE;
STBDATA			 g_TblPRODUCT;
STBDATA			 g_TblDropITEM;
STBDATA          g_TblStore;
*/

STBDATA g_TblAVATAR; // 아바타 초기 설정 데이타..
STBDATA g_TblWARP;
STBDATA g_TblZONE;
STBDATA* g_pTblSTBs[ITEM_TYPE_RIDE_PART + 1];

//#define	BASE_DATA_DIR	"..\\..\\sho\\"
#define BASE_DATA_DIR m_BaseDataDIR.Get() //	"..\\..\\sho\\"

WS_ZoneLIST g_ZoneLIST;

//-------------------------------------------------------------------------------------------------

SHO_WS* SHO_WS::m_pInstance = NULL;

CWS_ThreadSQL* g_pThreadSQL = NULL;

CWS_ListSERVER* g_pListSERVER = NULL;
CWS_ListCLIENT* g_pUserLIST = NULL;

WS_lsvSOCKET* g_pSockLSV = NULL;
// WS_logSOCKET	*g_pSockLOG = NULL;

CChatRoomLIST* g_pChatROOMs = NULL;
// CWSPartyBUFF	*g_pPartyBUFF=NULL;

CThreadMSGR* g_pThreadMSGR = NULL;
CThreadGUILD* g_pThreadGUILD = NULL;

//-------------------------------------------------------------------------------------------------

#define DEF_CLIENT_POOL_SIZE 0x7fff // 32767	동시 접속 대기 소켓
#define INC_CLIENT_POOL_SIZE 0xfff // 4095

#define DEF_SERVER_POOL_SIZE 64
#define INC_SERVER_POOL_SIZE 16

#define DEF_SEND_IO_POOL_SIZE (DEF_CLIENT_POOL_SIZE + INC_CLIENT_POOL_SIZE)
#define INC_SEND_IO_POOL_SIZE (INC_CLIENT_POOL_SIZE)

//-------------------------------------------------------------------------------------------------
#define WM_LSVSOCK_MSG (WM_SOCKETWND_MSG + 0)
#define WM_LOGSOCK_MSG (WM_SOCKETWND_MSG + 1)

#define MAX_CHAT_ROOMS 150

bool
SHO_WS::ConnectToLOG() {
    //	return g_pSockLOG->Connect();
    return true;
}
void
SHO_WS::DisconnectFromLOG() {
    //	g_pSockLOG->Disconnect ();
}
bool
SHO_WS::ConnectToLSV() {
    return g_pSockLSV->Connect();
}
void
SHO_WS::DisconnectFromLSV() {
    g_pSockLSV->Disconnect();
}

#include "../SHO_WS.ver"
DWORD
GetServerBuildNO() {
    return BUILD_NUM;
}
DWORD g_dwStartTIME = 0;
DWORD
GetServerStartTIME() {
    return g_dwStartTIME;
}

SHO_WS::SHO_WS() {

#if (_WIN32_WINNT < 0x0500) || !defined(__SERVER)
    COMPILE_TIME_ASSERT(0);
#endif
    CSOCKET::Init();
    CStr::Init();
}
void
SHO_WS::SystemINIT(HINSTANCE hInstance, char* szBaseDataDIR, int iLangType) {
    if (iLangType < 0)
        iLangType = 0;
    m_iLangTYPE = iLangType;

    m_BaseDataDIR.Alloc(strlen(szBaseDataDIR) + 3);
    m_BaseDataDIR.Printf((char*)"%s\\", szBaseDataDIR);

    g_pSockLSV = new WS_lsvSOCKET;
    //	g_pSockLOG = new WS_logSOCKET( USE_MY_SQL_AGENT );

    // 로그인 서버에 접속할 클라이언트 소켓 생성...
    CSocketWND* pSockWND = CSocketWND::InitInstance(hInstance, 2);
    if (pSockWND) {
        // pSockWND->AddSocketMsgProc( WM_LSVSOCK_MSG, LSV_SocketMSG );
        pSockWND->AddSocket(&g_pSockLSV->m_SockLSV, WM_LSVSOCK_MSG);
    }

    //	g_pPartyBUFF  = new CWSPartyBUFF;

    g_pUserLIST = new CWS_ListCLIENT(DEF_CLIENT_POOL_SIZE, INC_CLIENT_POOL_SIZE);
    g_pListSERVER = new CWS_ListSERVER(DEF_SERVER_POOL_SIZE, INC_SERVER_POOL_SIZE);

    CPoolSENDIO::Instance(DEF_SEND_IO_POOL_SIZE, INC_SEND_IO_POOL_SIZE);

    Load_BasicDATA();

    m_pWorldTIMER = NULL;
}

SHO_WS::~SHO_WS() {
    if (g_pThreadSQL) {
        g_pThreadSQL->Free();
        SAFE_DELETE(g_pThreadSQL);
    }

    if (g_pThreadMSGR) {
        g_pThreadMSGR->Free();
        SAFE_DELETE(g_pThreadMSGR);
    }
    if (g_pThreadGUILD) {
        g_pThreadGUILD->Free();
        SAFE_DELETE(g_pThreadGUILD);
    }

    SAFE_DELETE(g_pUserLIST);
    SAFE_DELETE(g_pListSERVER);

    //	SAFE_DELETE( g_pPartyBUFF );

    CPoolSENDIO::Destroy();

    //	SAFE_DELETE( g_pSockLOG );
    SAFE_DELETE(g_pSockLSV);

    if (CSocketWND::GetInstance())
        CSocketWND::GetInstance()->Destroy();

    CStr::Free();
    CSOCKET::Free();
}

//-------------------------------------------------------------------------------------------------
// 서버 초기화
// 1. 디비 서버 접속
// 2. 존 서버 접속 허용
// 3. 사용자 접속 허용..
// 4. 로그인 서버 접속
bool
SHO_WS::connect_database(ServerConfig& config) {
    if (NULL == this->GetInstance())
        return false;

    g_pThreadSQL = new CWS_ThreadSQL; // suspend 모드로 시작됨.
    if (!g_pThreadSQL->db.connect(config.database.connection_string)) {
        std::string error_message = g_pThreadSQL->db.last_error_message();
        LOG_ERROR("Failed to connect to the database: {}", error_message.c_str());
        return false;
    }
    g_pThreadSQL->Resume();

    g_pThreadMSGR = new CThreadMSGR(16384, 1024);
    if (!g_pThreadMSGR->db.connect(config.database.connection_string)) {
        std::string error_message = g_pThreadSQL->db.last_error_message();
        LOG_ERROR("Failed to connect to the database: {}", error_message.c_str());
        return false;
    }
    g_pThreadMSGR->Resume();

    g_pThreadGUILD = new CThreadGUILD(8192, 512);
    g_pThreadGUILD->set_config(config);
    if (!g_pThreadGUILD->db.connect(config.database.connection_string)) {
        std::string error_message = g_pThreadGUILD->db.last_error_message();
        LOG_ERROR("Failed to connect to the database: {}", error_message.c_str());

        g_pThreadGUILD = NULL;
        return false;
    }
    g_pThreadGUILD->Resume();

    g_pThreadSQL->Load_WORLDVAR(g_ZoneLIST.m_nWorldVAR, MAX_WORLD_VAR_CNT);

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
SHO_WS::Start(HWND hMainWND,
    char* szLoginServerIP,
    int iLoginServerPort,
    char* szLogServerIP,
    int iLogServerPortNO,
    char* szWorldName,
    int iServerListenPortNO,
    int iClientListenPortNO,
    bool bBlockCreateCHAR) {
    g_dwStartTIME = classTIME::GetCurrentAbsSecond();
    m_hMainWND = hMainWND;

    m_bBlockCreateCHAR = bBlockCreateCHAR;
    m_iClientListenPortNO = iClientListenPortNO;
    m_iServerListenPortNO = iServerListenPortNO;

    m_ServerNAME.Set(szWorldName);
    m_ServerIP.Set(szLoginServerIP);

    g_pChatROOMs = new CChatRoomLIST(MAX_CHAT_ROOMS);

    g_pUserLIST->Active(m_iClientListenPortNO, MAX_WORLD_USER_UBFF, 5 * 60); // 5분 대기
    g_pListSERVER->Active(m_iServerListenPortNO, 512, 3 * 60); // 3분 대기

    m_dwRandomSEED = ::timeGetTime();

    this->ConnectToLOG();

    g_pSockLSV->Init(CSocketWND::GetInstance()->GetWindowHandle(),
        szLoginServerIP,
        iLoginServerPort,
        WM_LSVSOCK_MSG,
        true);

    this->ConnectToLSV();

    m_pWorldTIMER =
        new CTimer(m_hMainWND, WS_TIMER_WORLD_TIME, WORLD_TIME_TICK, (TIMERPROC)WS_TimerProc);
    m_pWorldTIMER->Start();

    return true;
}

//-------------------------------------------------------------------------------------------------
void
SHO_WS::Active(bool bActive) {
    g_pSockLSV->Send_srv_ACTIVE_MODE(bActive);
}

//-------------------------------------------------------------------------------------------------
void
SHO_WS::Shutdown() {
    SAFE_DELETE(m_pWorldTIMER);

    DisconnectFromLSV();

    g_pUserLIST->Shutdown();
    g_pListSERVER->Shutdown();

    SAFE_DELETE(g_pChatROOMs); // 제일 마지막에...
}

//-------------------------------------------------------------------------------------------------
void
SHO_WS::StartCLI_SOCKET() {
    g_pUserLIST->Active(m_iClientListenPortNO, MAX_WORLD_USER_UBFF, 5 * 60); // 5분 대기
}

void
SHO_WS::ShutdownCLI_SOCKET() {
    g_pUserLIST->Shutdown();
}

bool
SHO_WS::Load_BasicDATA() {
    std::filesystem::path base_dir(BASE_DATA_DIR);
    g_TblGAUNTLET.load(base_dir / ARMS_STB);
    g_TblARMOR.load(base_dir / BODY_STB);
    g_TblHELMET.load(base_dir / CAP_STB);
    g_TblFACE.load(base_dir / FACE_STB);
    g_TblFACEITEM.load(base_dir / FACE_ITEM_STB);
    g_TblBOOTS.load(base_dir / FOOT_STB);
    g_TblHAIR.load(base_dir / HAIR_STB);
    g_TblAVATAR.load(base_dir / INIT_AVATAR_STB);
    g_TblGEMITEM.load(base_dir / JEM_ITEM_STB);
    g_TblJEWELITEM.load(base_dir / JEWEL_STB);
    g_TblQUESTITEM.load(base_dir / QUEST_ITEM_STB);
    g_TblSUBWPN.load(base_dir / SUBWPN_STB);
    g_TblUSEITEM.load(base_dir / USE_ITEM_STB);
    g_TblWEAPON.load(base_dir / WEAPON_STB);
    g_PatITEM.load(base_dir / PAT_STB);
    g_TblZONE.load(base_dir / ZONE_STB);

    if (!g_SkillList.LoadSkillTable(
            CStr::Printf((char*)"%s%s", BASE_DATA_DIR, "3DDATA\\STB\\LIST_SKILL.STB")))
        return false;

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

    g_ZoneLIST.Init(BASE_DATA_DIR);

    return true;
}
void
SHO_WS::Free_BasicDATA() {
    // STBDATA는 자동 풀림..
    g_SkillList.Free();
}
void
SHO_WS::Send_ANNOUNCE(char* szMsg) {
    g_pListSERVER->Send_gsv_ANNOUNCE_CHAT(szMsg, NULL);
}

//-------------------------------------------------------------------------------------------------
void
SHO_WS::ToggleChannelActive(int iChannelNo) {
    g_pListSERVER->Set_ChannelACTIVE(iChannelNo, false, true);
}

void
SHO_WS::DeleteChannelServer(int iChannelNo) {
    g_pListSERVER->Del_ChannelSERVER(iChannelNo);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
VOID CALLBACK
WS_TimerProc(HWND hwnd /* handle to window */,
    UINT uMsg /* WM_TIMER message */,
    UINT_PTR idEvent /* timer identifier */,
    DWORD dwTime /* current system time */) {
    switch (idEvent) {
        case WS_TIMER_LSV: {
            SHO_WS* pGS = SHO_WS::GetInstance();
            if (pGS) {
                pGS->ConnectToLSV();
            }
            break;
        }

        case WS_TIMER_LOG: {
            SHO_WS* pGS = SHO_WS::GetInstance();
            if (pGS) {
                pGS->ConnectToLOG();
            }
            break;
        }

        case WS_TIMER_WORLD_TIME: {
            SHO_WS* pWS = SHO_WS::GetInstance();
            if (NULL == pWS)
                return;
            g_ZoneLIST.Inc_WorldTIME(true);

            switch (g_ZoneLIST.m_dwAccTIME % 6) {
                case 0:
                case 3: // 30초에 한번씩 체크...
                    g_pUserLIST->Check_SocketALIVE();
                    break;
                case 1: // case 4 :
                    g_pUserLIST->CloseIdleSCOKET(150 * 1000); // 2분 30초
                    break;
            }
            break;
        }
    };
}

//-------------------------------------------------------------------------------------------------
