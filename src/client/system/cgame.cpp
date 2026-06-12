#include "stdafx.h"

#include ".\cgame.h"

#include "CGameState.h"
#include "CGameStateNull.h"
#include "CGameStateTitle.h"
#include "CGameStateLogin.h"
#include "CGameStateSelectSvr.h"
#include "CGameStatePrepareSelectAvatar.h"
#include "CGameStateSelectAvatar.h"
#include "CGameStateCreateAvatar.h"
#include "CGameStatePrepareMain.h"
#include "CGameStateMain.h"
#include "CGameStateWarp.h"
#include "CGameStateExitMain.h"
#include "CGameStateMoveMain.h"
#include "CGameStateLoginVirtual.h"
#include "GameStateMovePlanet.h"
#include "CGameStateReLogin.h"
#include "CGameStateAutoConnect.h"

#include "SqliteDB.h"

#include "../Network/CNetwork.h"
#include "../IO_Event.h"
#include "../CClientStorage.h"
#include "../CViewMsg.h"
#include "../CCamera.h"
#include "../interface/CTDrawImpl.h"
#include "../interface/CTFontImpl.h"
#include "../interface/CLoading.h"
#include "../interface/CTSoundImpl.h"
#include "../interface/CUIMediator.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/dlgs/CClanDlg.h"
#include "../interface/dlgs/CPatDlg.h"
#include "../interface/dlgs/COptionDlg.h"
#include "../interface/dlgs/CharacterDlg.h"
#include "../interface/dlgs/QuickToolBar.h"
#include "../interface/dlgs/CSkillDlg.h"
#include "../interface/dlgs/SkillTreeDlg.h"
#include "../interface/dlgs/MakeDlg.h"
#include "../interface/dlgs/CItemDlg.h"
#include "../interface/dlgs/AvatarInfoDlg.h"
#include "../interface/cursor/ccursor.h"
#include "../interface/ExternalUI/ExternalUILobby.h"
#include "interface/dev/dev_ui.h"

#include "io_quest.h"
#include "rose/io/stb.h"
#include "../gamedata/cmanufacture.h"
#include "../gamedata/CExchange.h"
#include "../gamedata/CSeparate.h"
#include "../gamedata/cdealdata.h"
#include "../gamedata/CPrivateStore.h"
#include "../gamedata/CClan.h"

#include "../Game_FUNC.h"
#include "../Game.h"
#include "../Bullet.h"
#include "../CSkyDome.h"
#include "../util/Localizing.h"
#include <resource.h>

#include "SystemProcScript.h"
#include "../ObjectActionProcessor.h"

#include "../GameCommon/StringManager.h"

#include "../Util/Localizing.h"
#include "../Util/classTIME.h"

#include "../Sound/MusicMgr.h"

#include "tgamectrl/resourcemgr.h"
#include "tgamectrl/time2.h"

#include "../JCommandState.h"
#include "GameCommon/LngTbl.h"

#include "GameProc/SkillCommandDelay.h"

#include "Tutorial/TutorialEventManager.h"

#include "../sfx/SFXManager.h"
#include "../sfx/SFXFont.h"
#include "../misc/gameutil.h"

#include "GameProc/UseItemDelay.h"

bool g_bDontDeleteCapFile = false;

CGame::CGame(void) {
    m_GameStates[GS_NULL] = new CGameStateNull(GS_NULL);

    for (int i = GS_TITLE; i < GS_MAX; ++i)
        m_GameStates[i] = NULL;

    this->active_state = m_GameStates[GS_NULL];

    //	m_iCurrentCursorType = 4;
    m_iCheckFrame = 0;
    g_bDontDeleteCapFile = false;

    m_ptCurrMouse.x = 0;
    m_ptCurrMouse.y = 0;
    m_iRepairMode = REPAIR_NONE;
    m_bLoadedBasicData2 = false;

    m_iJoinChannelNo = 0;
    m_uiSelectedServerID = 0;
    m_bAutoRun = false;
}

CGame::~CGame(void) {
    for (int i = 0; i < GS_MAX; ++i) {
        if (m_GameStates[i]) {
            delete m_GameStates[i];
            m_GameStates[i] = NULL;
        }
    }
}

CGame&
CGame::GetInstance() {
    static CGame s_Instance;
    return s_Instance;
}

CGameState*
CGame::MakeState(int iState) {
    CGameState* pState = NULL;
    switch (iState) {
        case GS_TITLE:
            pState = new CGameStateTitle(GS_TITLE);
            break;
        case GS_LOGIN:
            pState = new CGameStateLogin(GS_LOGIN);
            break;
        case GS_SELECTSVR:
            pState = new CGameStateSelectSvr(GS_SELECTSVR);
            break;
        case GS_PREPARESELECTAVATAR:
            pState = new CGameStatePrepareSelectAvatar(GS_PREPARESELECTAVATAR);
            break;
        case GS_SELECTAVATAR:
            pState = new CGameStateSelectAvatar(GS_SELECTAVATAR);
            break;
        case GS_LOGINVIRTUAL:
            pState = new CGameStateLoginVirtual(GS_LOGINVIRTUAL);
            break;
        case GS_CREATEAVATAR:
            pState = new CGameStateCreateAvatar(GS_CREATEAVATAR);
            break;
        case GS_PREPAREMAIN:
            pState = new CGameStatePrepareMain(GS_PREPAREMAIN);
            break;
        case GS_MAIN:
            pState = new CGameStateMain(GS_MAIN);
            break;
        case GS_WARP:
            pState = new CGameStateWarp(GS_WARP);
            break;
        case GS_EXITMAIN:
            pState = new CGameStateExitMain(GS_EXITMAIN);
            break;
        case GS_MOVEMAIN:
            pState = new CGameStateMoveMain(GS_MOVEMAIN);
            break;

        case GS_MOVEPLANET:
            pState = new CGameStateMovePlanet(GS_MOVEPLANET);
            break;

        case GS_RELOGIN:
            pState = new CGameStateReLogin(GS_RELOGIN);
            break;
        case GS_AUTOCONNECT:
            pState = new CGameStateAutoConnect(GS_AUTOCONNECT);
        default:
            break;
    }
    assert(pState != NULL);
    return pState;
}

void
CGame::ChangeState(int newState) {
    if (m_GameStates[newState] == NULL)
        m_GameStates[newState] = MakeState(newState);

    this->active_state->Leave(newState);
    int oldState = this->active_state->GetStateID();

    this->active_state = m_GameStates[newState];
    this->active_state->Enter(oldState);
}

void
CGame::GameLoop() {
    if (Init() != 0)
        return;

    ChangeState(GS_TITLE);

    dev_ui_init(g_pCApp->GetHWND());

    do {
        bool bLostFocus = g_pCApp->GetMessage();

        g_GameDATA.Update();

        g_pNet->Proc();
        ProcInput();

        this->active_state->Update(bLostFocus);

        m_MouseTargetEffect.Proc();

        ProcCommand();

    } while (!g_pCApp->IsExitGame());

    dev_ui_destroy();
    Exit();
}

int
CGame::Init() {
    /// Get current code page
    (CLocalizing::GetSingleton()).GetCurrentCodePage();
    if (!(CStringManager::GetSingleton())
             .LoadTables((CLocalizing::GetSingleton()).GetCurrentCharSet()))
        return false;

    ::activateLog(true);

    if (g_pNet == NULL) {
        g_pCApp->ErrorBOX("init socket failed", "error", MB_OK);
        return 1;
    }

    g_pEventLIST = new CEventLIST;
    g_pBltMGR = CBulletMANAGER::Instance();
    g_pViewMSG = CViewMSG::Instance();
    g_pObjMGR = CObjectMANAGER::Instance();

    g_pObjMGR->ReserverViewObjectList();

    g_pTerrain = CTERRAIN::Instance();

    it_Init(g_pCApp->GetHWND(), &g_DrawImpl, &g_SoundImpl, &g_FontImpl);

    //--------------------------------------------------------------------------------
    /// 현재 코드페이지를 새로 구할경우 항상 TGameCtrl에 알려줄것
    //--------------------------------------------------------------------------------
    CLocalizing::GetSingleton().GetCurrentCodePage();
    it_SetCodePage(CLocalizing::GetSingleton().GetCurrentCodePageNO());

    SIZE sizeScreen = {g_pCApp->GetWIDTH(), g_pCApp->GetHEIGHT()};
    CTIme::GetInstance().Initialize(sizeScreen);
    //--------------------------------------------------------------------------------

    //--------------------------------------------------------------------------------
    /// for processing userdata registed engine
    //--------------------------------------------------------------------------------
    CObjectActionProcessor::GetSingleton().MakeChain();

    //--------------------------------------------------------------------------------
    // Music init
    //--------------------------------------------------------------------------------
    CMusicMgr& musicMgr = CMusicMgr::GetSingleton();
    if (musicMgr.Init(OGG_MUSIC_TYPE)) {
        // musicMgr.Play ( "Sound\\BGM\\Prologue.mp3" );
        musicMgr.Play("Sound\\BGM\\Prologue.ogg");
        musicMgr.SetVolume(
            g_ClientStorage.GetBgmVolumeByIndex(g_ClientStorage.GetBgmVolumeIndex()));
    } else {
        MessageBox(g_pCApp->GetHWND(), "사운드 카드에 문제가 있습니다.", "ERROR", MB_OK);
    }

    //--------------------------------------------------------------------------------
    /// sound
    //--------------------------------------------------------------------------------
    g_pSoundLIST->SetVolume(SOUND_VOLUME_MIN);

    //--------------------------------------------------------------------------------
    /// Loading class
    MakeFont();
//	LoadUserCursor();

//	SetUserCursor(CURSOR_DEFAULT);
#ifdef __VIRTUAL_SERVER
    g_pCApp->SetStatus(AS_MAIN_GAME);
#endif
    g_pCApp->Show();

    CVFSManager::GetSingleton().load_stb(g_TblString, STRING_STB);
    //	if( CreateAllStringTables( &g_TblString ) == false )
    //		return false;

    CSystemProcScript& script_system = CSystemProcScript::GetSingleton();
    if (!script_system.InitSystem()) {
        // TODO: Install error handler for lua and do something with the
        // error message
        return false;
    }

    CCursor::GetInstance().Initialize();
    CCursor::GetInstance().SetCursorType(CCursor::CURSOR_DEFAULT);

    /// skill dealy
    CSkillCommandDelay::GetSingleton().Init();

    /// Tutorial Event
    if (CTutorialEventManager::GetSingleton().Init() == false) {
        MessageBox(NULL, "Load tutorial failed", "ERROR", MB_OK);
        return 0;
    }

    // CSqliteDB::GetInstance()
    /*
    if( CSqliteDB::GetInstance().Open( "Rose.db") == 0 )
    {
        const char* sqlCreateMemoTable = "create table memo ( name string, sender string, content
    string, time int )"; CSqliteDB::GetInstance().Exec( sqlCreateMemoTable, NULL );
    }
    */

    CSFXFont* sfx_font = new CSFXFont;
    CSFXManager::GetSingleton().AddSFX(sfx_font);

    ///클라이언트 실행시에 필요한 디렉토리를 만든다.
    CreateDirectory(CClanMarkUserDefined::ClanMarkFileDirectory.c_str(), NULL);
    CreateDirectory("Chat", NULL);

    m_ClientFileVersion.GetVersionInfo("rosenext.exe");

    m_ClientFileVersion.m_strFileVersion = CStr::Printf("%d.%d.%d.%d",
        m_ClientFileVersion.m_iMajorVersion,
        m_ClientFileVersion.m_MinorVersion,
        m_ClientFileVersion.m_BuildNumber,
        m_ClientFileVersion.m_RevisionNumber);

#ifdef _TAIWAN
    m_ClientFileVersion.m_strFileVersion.append("T");
#endif

#ifdef _DE
    m_ClientFileVersion.m_strFileVersion.append("DE");
#endif

    return 0;
}

void
CGame::Exit() {
    CSFXManager::GetSingleton().Clear();

    // CSqliteDB::GetInstance().Close();

    CCursor::GetInstance().Finalize();

    m_MouseTargetEffect.Clear();

    CSkyDOME::Free();

    g_itMGR.Free();

    CImageResManager::GetSingletonPtr()->ReleaseResources();
    CResourceMgr::GetInstance()->UnLoadResource(IMAGE_RES_EXUI);
    CResourceMgr::GetInstance()->UnLoadResource(IMAGE_RES_UI);
    CResourceMgr::GetInstance()->UnLoadResource(IMAGE_RES_ITEM);
    //	CResourceMgr::GetInstance()->UnLoadResource(IMAGE_RES_FACE);

    for (int i = 0; i < MAX_FONT; i++) {
        if (g_GameDATA.m_hFONT[i] != NULL) {
            ::unloadFont(g_GameDATA.m_hFONT[i]);
            g_GameDATA.m_hFONT[i] = NULL;
        }
    }

    //----------------------------------------------------------------------------------------
    /// clear music resource
    //----------------------------------------------------------------------------------------
    if (CMusicMgr::GetSingleton().bIsReady())
        CMusicMgr::GetSingleton().Clear();

    g_pTerrain->Destroy();
    g_pObjMGR->Destroy();
    g_pViewMSG->Destroy();

    Free_BasicDATA();
    it_Destroy();
    SAFE_DELETE(g_pEventLIST);
    CObjectMANAGER::Destroy();

    CSystemProcScript::GetSingleton().ClearSystem();

    CTIme::GetInstance().Finalize();

    CSkillCommandDelay::GetSingleton().Release();

    /// Tutorial event
    CTutorialEventManager::GetSingleton().Release();
}
void
CGame::Load_NewVersionData() {
    STBDATA stbNPCFACE;
    CVFSManager::GetSingleton().load_stb(stbNPCFACE, NPC_FACE_STB);

    if (stbNPCFACE.row_count < 1 || stbNPCFACE.col_count < 2) {
        _RPT0(_CRT_WARN, "LIST_NPCFACE STB's schima mismatch\n");
        return;
    }

    std::string directory(stbNPCFACE.value(0, 1));
    std::string file;
    for (int row = 1; row < stbNPCFACE.row_count; row++) {
        file = directory;
        if (!stbNPCFACE.value(row, 1).empty()) {
            file.append(stbNPCFACE.value(row, 1));
            CImageResManager::GetSingleton().Add_NpcFaceFileInfo(
                stbNPCFACE.get_int16(row, 0),
                file.c_str());
        }
    }
}
bool
CGame::Load_BasicDATA2() {
    if (m_bLoadedBasicData2)
        return true;

    m_bLoadedBasicData2 = true;

    DWORD dwStartTime = timeGetTime();
    g_SkillList.LoadSkillTable("3DData\\STB\\LIST_SKILL.STB");

    CVFSManager& vfs = CVFSManager::GetSingleton();

    // NPC
    vfs.load_stb(g_TblNPC, NPC_STB);
    g_MOBandNPC.Load_MOBorNPC("3DDATA\\NPC\\LIST_NPC.CHR");
    g_QuestList.LoadQuestTable("3DDATA\\STB\\LIST_QUEST.STB", "3DDATA\\STB\\LIST_QuestDATA.STB");

    // Items
    vfs.load_stb(g_TblFACEITEM, FACE_ITEM_STB);
    vfs.load_stb(g_TblARMOR, BODY_STB);
    vfs.load_stb(g_TblGAUNTLET, ARMS_STB);
    vfs.load_stb(g_TblBOOTS, FOOT_STB);
    vfs.load_stb(g_TblBACKITEM, BACK_STB);
    vfs.load_stb(g_TblJEWELITEM, JEWEL_STB);
    vfs.load_stb(g_TblWEAPON, WEAPON_STB);
    vfs.load_stb(g_TblSUBWPN, SUBWPN_STB);
    vfs.load_stb(g_TblUSEITEM, USE_ITEM_STB);
    vfs.load_stb(g_TblGEMITEM, JEM_ITEM_STB);
    vfs.load_stb(g_TblNATUAL, NATURAL_STB);
    vfs.load_stb(g_TblQUESTITEM, QUEST_ITEM_STB);
    vfs.load_stb(g_PatITEM, PAT_STB);

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

    return true;
}

bool
CGame::Load_BasicDATA() {
    //---------------------------------------------------------------------
    /// string table load
    //---------------------------------------------------------------------

    DWORD dwStartTime = timeGetTime();

    CObjITEM::SetMotion("3DData\\Motion\\item_ani.ZMO");

    int iCurrentLang = CStringManager::GetSingleton().GetLanguageIDInGame(
        CLocalizing::GetSingleton().GetCurrentCharSet());

    g_AI_LIST.Load(NULL, "3DDATA\\STB\\FILE_AI.STB", "3DDATA\\AI\\AI_S.stb", iCurrentLang);
    g_pEffectLIST =
        new CEffectLIST("3ddata\\stb\\FILE_EFFECT.stb"); // shader 설정된후 로드해야 함..

    if (!g_DATA.Load3DDATA()) {
        return false;
    }

    CVFSManager& vfs = CVFSManager::GetSingleton();
    vfs.load_stb(g_TblHELMET, CAP_STB);
    vfs.load_stb(g_TblClass, CLASS_STB);
    vfs.load_stb(g_TblEFFECT, EFFECT_STB);
    vfs.load_stb(g_TblEVENT, EVENT_STB);
    vfs.load_stb(g_TblFACE, FACE_STB);
    vfs.load_stb(g_TblItemGRADE, GRADE_STB);
    vfs.load_stb(g_TblHAIR, HAIR_STB);
    vfs.load_stb(g_TblHitSound, HIT_SOUND_STB);
    vfs.load_stb(g_TblAVATAR, INIT_AVATAR_STB);
    vfs.load_stb(g_TblDropITEM, ITEM_DROP_STB);
    vfs.load_stb(g_TblPRODUCT, PRODUCT_STB);
    vfs.load_stb(g_TblRangeSet, RANGE_SET_STB);
    vfs.load_stb(g_TblStore, SELL_STB);
    vfs.load_stb(g_TblSKY, SKY_STB);
    vfs.load_stb(g_TblSTATE, STATUS_STB);
    vfs.load_stb(g_TblStepSound, STEP_SOUND_STB);
    vfs.load_stb(g_TblAniTYPE, TYPE_MOTION_STB);
    vfs.load_stb(g_TblUnion, UNION_STB);
    vfs.load_stb(g_TblWARP, WARP_STB);
    vfs.load_stb(g_TblZONE, ZONE_STB);

    if (g_MotionFILE.Load("3DDATA\\STB\\FILE_MOTION.stb") == false)
        return false;

    if (g_LngTBL.Load(iCurrentLang) == false)
        return false;

    InitScript();
    g_UserInputSystem.Init();

    _RPT1(_CRT_WARN, "Loading Time Basic Data %d \n", timeGetTime() - dwStartTime);
    return true;
}

bool
CGame::Load_DataNotUseThread() ///쓰레드를 사용해서 읽으면 안되는 Data를 Load하는 Method: 순서상
                               ///제일먼저해야한다.
{
    g_pCamera = CCamera::Instance();

    ::doScript("scripts/load.lua");

    g_pCamera->Init(findNode("motion_camera"));

    g_GameDATA.m_hShader_nolit_skin = ::findNode("shader_nolit_skin");
    g_GameDATA.m_hShader_nolit = ::findNode("shader_nolit");
    g_GameDATA.m_hShader_terrain = ::findNode("shader_terrain");
    g_GameDATA.m_hShader_terrain_Rough = ::findNode("shader_terrain_rough");
    g_GameDATA.m_hShader_lightmap = ::findNode("shader_lightmap_nolit");
    g_GameDATA.m_hShader_ocean = ::findNode("shader_ocean");
    g_GameDATA.m_hShader_specular = ::findNode("shader_specular");
    g_GameDATA.m_hShader_specular_skin = ::findNode("shader_specular_skin");
    g_GameDATA.m_hShader_lit_skin = ::findNode("shader_lit_skin");
    g_GameDATA.m_hShader_lit = ::findNode("shader_lit");
    g_GameDATA.m_hShader_sky = ::findNode("shader_sky");

    g_GameDATA.m_hLight = ::findNode("light_01");
    g_GameDATA.m_hCharacterLight = ::findNode("light_02");
    g_GameDATA.m_hCharacterLight2 = ::findNode("light_03");

    g_GameDATA.m_TrailTexture.Set("3DDATA\\Effect\\trail.dds");

    if (g_UIMed.InitMediator() == false) {
        g_pCApp->ErrorBOX("Load UIMED failed", "ERROR");
        return false;
    }

    return true;
}

#include "Util/FileVersionInfo.h"
void
CGame::MakeFont() {
    // CFileVersionInfo versionInfo;
    // versionInfo.GetVersionInfo( "d_client.exe" );

    for (int i = 0; i < MAX_FONT; i++) {
        g_GameDATA.m_hFONT[i] = NULL;
    }

    int iCharSet = CLocalizing::GetSingleton().GetCurrentCharSet();
    const char* szFont = CStringManager::GetSingleton().GetFontNameByCharSet(iCharSet);

    // const char* szFont = "Verdana";

    int iFontSize = 9;
    if (iCharSet == 0)
        iFontSize = 11;

    /// Normal
    g_GameDATA.m_hFONT[FONT_NORMAL] = ::loadFont("Normal_Font",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        iFontSize,
        0,
        0,
        255,
        255,
        255,
        255);

    /// Normal bold
    g_GameDATA.m_hFONT[FONT_NORMAL_BOLD] = ::loadFont("Normal_Font_B",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        iFontSize,
        1,
        0,
        255,
        255,
        255,
        255);

    /// Small
    iFontSize = 8;
    if (iCharSet == 0)
        iFontSize = 10;

    g_GameDATA.m_hFONT[FONT_SMALL] = ::loadFont("Small_Font",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        iFontSize,
        0,
        0,
        255,
        255,
        255,
        255);

    /// Small bold
    g_GameDATA.m_hFONT[FONT_SMALL_BOLD] = ::loadFont("Small_Font_B",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        iFontSize,
        1,
        0,
        255,
        255,
        255,
        255);

    /// Large
    g_GameDATA.m_hFONT[FONT_LARGE] = ::loadFont("Large_Font",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        12,
        0,
        0,
        255,
        255,
        255,
        255);

    /// Large bold
    g_GameDATA.m_hFONT[FONT_LARGE_BOLD] = ::loadFont("Large_Font_B",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        12,
        1,
        0,
        255,
        255,
        255,
        255);

    g_GameDATA.m_hFONT[FONT_OUTLINE_18_BOLD] = ::loadFontOutline("FONT_OUTLINE_18",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        18,
        1,
        0,
        1,
        D3DCOLOR_ARGB(255, 255, 255, 255),
        D3DCOLOR_ARGB(255, 0, 0, 0));

    g_GameDATA.m_hFONT[FONT_OUTLINE_14_BOLD] = ::loadFontOutline("FONT_OUTLINE_14",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        14,
        1,
        0,
        1,
        D3DCOLOR_ARGB(255, 255, 255, 255),
        D3DCOLOR_ARGB(255, 0, 0, 0));

    g_GameDATA.m_hFONT[FONT_OUTLINE_24_BOLD] = ::loadFontOutline("FONT_OUTLINE_24",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        24,
        1,
        0,
        1,
        D3DCOLOR_ARGB(255, 255, 255, 255),
        D3DCOLOR_ARGB(255, 0, 0, 0));

    g_GameDATA.m_hFONT[FONT_OUTLINE_16_BOLD] = ::loadFontOutline("FONT_OUTLINE_16",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        16,
        1,
        0,
        1,
        D3DCOLOR_ARGB(255, 255, 255, 255),
        D3DCOLOR_ARGB(255, 0, 0, 0));

    iFontSize = 9;
    if (0 == iCharSet)
        iFontSize = 11;
    g_GameDATA.m_hFONT[FONT_NORMAL_OUTLINE] = loadFontOutline("Normal_Font_Outline",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        iFontSize,
        0,
        0,
        1,
        D3DCOLOR_ARGB(255, 255, 255, 255),
        D3DCOLOR_ARGB(255, 0, 0, 0));

    g_GameDATA.m_hFONT[FONT_OUTLINE_11_BOLD] = loadFontOutline("FONT_OUTLINE_11_BOLD",
        szFont,
        CLocalizing::GetSingleton().GetCurrentCharSet(),
        11,
        1,
        0,
        1,
        D3DCOLOR_ARGB(255, 255, 255, 255),
        D3DCOLOR_ARGB(255, 0, 0, 0));
}

void
CGame::Free_BasicDATA() {
    CObjITEM::ClearMotion();

    g_UIMed.FreeResource();
    CImageResManager::GetSingletonPtr()->ReleaseResources();

    FreeScript();

    g_MatFILE.Free();
    g_MeshFILE.Free();
    g_MotionFILE.Free();

    g_MOBandNPC.Free();

    g_DATA.Free3DDATA();

    SAFE_DELETE(g_pEffectLIST);

    g_pCamera->Destroy();

    g_UserInputSystem.Clear();
}

bool
CGame::AddWndMsgQ(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    if (uiMsg == WM_MOUSEMOVE) {
        m_ptPrevMouse = m_ptCurrMouse;

        m_ptCurrMouse.x = LOWORD(lParam);
        m_ptCurrMouse.y = HIWORD(lParam);

        // POINT ptWindow = m_ptCurrMouse;
        // ClientToScreen( g_pCApp->GetHWND(), &ptWindow );
        // setCursorPosition ( m_hDxCursor[m_iCurrentCursorType], ptWindow.x, ptWindow.y );
    }
    if (this->active_state->ProcWndMsgInstant(uiMsg, wParam, lParam)) {
        return true;
    }

    return m_WndMsgQ.AddMsgToQ(uiMsg, wParam, lParam);
}

// void CGame::LoadUserCursor()
//{
//	m_hCursor[ CURSOR_DEFAULT ]   = (HCURSOR)::LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_DEFAULT ) ); 	m_hDxCursor[ CURSOR_DEFAULT ] = loadCursor(
//"CURSOR_DEFAULT",m_hCursor[ CURSOR_DEFAULT] );
//
//	m_hCursor[ CURSOR_ATTACK ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_ATTACK ) ); 	m_hDxCursor[ CURSOR_ATTACK ] = loadCursor(
//"CURSOR_ATTACK", m_hCursor[ CURSOR_ATTACK] );
//
//	m_hCursor[ CURSOR_ITEM_INVEN ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_ITEM_INVEN ) ); 	m_hDxCursor[ CURSOR_ITEM_INVEN ] = loadCursor(
//"CURSOR_ITEM_INVEN", m_hCursor[ CURSOR_ITEM_INVEN ] );
//
//	m_hCursor[ CURSOR_ITEM_PICK ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_ITEM_PICK ) ); 	m_hDxCursor[ CURSOR_ITEM_PICK ] = loadCursor(
//"CURSOR_ITEM_PICK", m_hCursor[ CURSOR_ITEM_PICK ] );
//
//	m_hCursor[ CURSOR_LEFT ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_LEFT ) ); 	m_hDxCursor[ CURSOR_LEFT ] = loadCursor( "CURSOR_LEFT",
// m_hCursor[ CURSOR_LEFT ] );
//
//	m_hCursor[ CURSOR_RIGHT ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_RIGHT ) ); 	m_hDxCursor[ CURSOR_RIGHT ] = loadCursor(
// "CURSOR_RIFHT", m_hCursor[ CURSOR_RIGHT ] );
//
//	m_hCursor[ CURSOR_NPC ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_NPC ) ); 	m_hDxCursor[ CURSOR_NPC ] = loadCursor( "CURSOR_NPC",
// m_hCursor[ CURSOR_NPC ]);
//
//	m_hCursor[ CURSOR_USER ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_USER ) ); 	m_hDxCursor[ CURSOR_USER ] = loadCursor( "CURSOR_USER",
// m_hCursor[ CURSOR_USER ] );
//
//	m_hCursor[ CURSOR_WHEEL ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_WHEEL ) ); 	m_hDxCursor[ CURSOR_WHEEL ] = loadCursor( "CURSOR_WHEEL"
// , m_hCursor[ CURSOR_WHEEL ] );
//
//	m_hCursor[ CURSOR_NOUI ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_NOUI ) ); 	m_hDxCursor[ CURSOR_NOUI ] = loadCursor( "CURSOR_NOUI",
// m_hCursor[ CURSOR_NOUI ] );
//
//	m_hCursor[ CURSOR_REPAIR ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_CURSOR_REPAIR ) ); 	m_hDxCursor[ CURSOR_REPAIR ] =  loadCursor(
//"CURSOR_REPAIR", m_hCursor[ CURSOR_REPAIR ] );
//
//	m_hCursor[ CURSOR_APPRAISAL ] = (HCURSOR)LoadCursor( CApplication::Instance()->GetHINS(),
// MAKEINTRESOURCE( IDC_IDENTIFY ) ); 	m_hDxCursor[ CURSOR_APPRAISAL ] =  loadCursor(
//"CURSOR_APPRAISAL", m_hCursor[ CURSOR_APPRAISAL ] );
//
//	m_bUseUserMouse = true;
//}
// void CGame::SetDisableUserCursor()
//{
//	m_bUseUserMouse = false;
//}
// void CGame::SetUserCursor( int iCursorType ,CInfo* MouseInfo )
//{
//	if( !m_bUseUserMouse )
//		return;
//
//	if( iCursorType < 0 || iCursorType >= CURSOR_MAX )
//		return;
//
////	if( m_iCurrentCursorType != iCursorType )
//	{
//		m_MouseInfo.Clear();
//
//		m_iCurrentCursorType = iCursorType;
/////#ifdef _DEBUG
//		if( g_GameDATA.m_bNoUI )
//		{
//			SetCursor( NULL );
//			showCursor( m_hDxCursor[ CURSOR_NOUI ] );
//			//SetCursor( m_hCursor[CURSOR_NOUI] );
//			//m_iCurrentCursorType = CURSOR_NOUI;
//		}
//		else
/////#endif
//		{
//			SetCursor( NULL );
//			showCursor( m_hDxCursor[ iCursorType ] );
//
//			if( !g_GameDATA.m_bShowCursor )
//				SetCursor( NULL );
////			SetCursor( m_hCursor[ iCursorType ] );
//		}
//
//
//		if( MouseInfo )
//			m_MouseInfo = *MouseInfo;
//
//	}
//}

void
CGame::CreateSelectedAvata() {

    strcpy(g_MsgBuf, m_strAvataName.c_str());
    g_pAVATAR = (CObjUSER*)g_pObjMGR->New_UserCHAR(0, g_MsgBuf);

    //	g_pAVATAR->SetAllPARTS( m_SelectedAvataInfo.m_nPartItemIDX );
    g_pAVATAR->SetAllPARTS(m_SelectedAvataInfo.m_PartITEM);
    g_pAVATAR->set_costume(m_SelectedAvataInfo.costume);

    /// Add Observer to CItemSlot<-
    CItemSlot* pItemSlot = g_pAVATAR->GetItemSlot();
    assert(pItemSlot);

    CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_ITEM);
    if (pDlg) {
        CItemDlg* pItemDlg = (CItemDlg*)pDlg;
        pItemSlot->AddObserver(pItemDlg);
    }

    /*pDlg = g_itMGR.FindDlg( DLG_TYPE_PAT );
    if( pDlg )
    {
        CPatDlg* pPatDlg = ( CPatDlg* ) pDlg;
        pItemSlot->AddObserver( pPatDlg );
    }*/
    /*
    pDlg = g_itMGR.FindDlg( DLG_TYPE_CHAR );
    if( pDlg )
    {
        CCharacterDLG* pCharDlg = ( CCharacterDLG*) pDlg;
        pItemSlot->AddObserver( pCharDlg );
    }*/

    pDlg = g_itMGR.FindDlg(DLG_TYPE_QUICKBAR);
    if (pDlg) {
        CQuickBAR* pQuickBar = (CQuickBAR*)pDlg;
        pItemSlot->AddObserver(pQuickBar);
    }

    pDlg = g_itMGR.FindDlg(DLG_TYPE_QUICKBAR_EXT);
    if (pDlg) {
        CQuickBAR* pQuickBar = (CQuickBAR*)pDlg;
        pItemSlot->AddObserver(pQuickBar);
    }

    pDlg = g_itMGR.FindDlg(DLG_TYPE_INFO);
    if (pDlg) {
        CAvatarInfoDlg* pInfoDlg = (CAvatarInfoDlg*)pDlg;
        pItemSlot->AddObserver(pInfoDlg);
    }

    pItemSlot->AddObserver(&(CManufacture::GetInstance()));
    pItemSlot->AddObserver(&(CDragNDropMgr::GetInstance()));
    pItemSlot->AddObserver(&(CDealData::GetInstance()));
    pItemSlot->AddObserver(&(CExchange::GetInstance()));
    pItemSlot->AddObserver(&(CPrivateStore::GetInstance()));
    pItemSlot->AddObserver(&(CSeparate::GetInstance()));

    pDlg = g_itMGR.FindDlg(DLG_TYPE_MAKE);
    CMakeDLG* pMakeDlg = (CMakeDLG*)pDlg;
    CManufacture::GetInstance().AddObserver(pMakeDlg);
    /// Add Observer to CSkillSlot<-

    CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();

    pDlg = g_itMGR.FindDlg(DLG_TYPE_SKILL);
    CSkillDLG* pSkillDlg = (CSkillDLG*)pDlg;
    pSkillSlot->AddObserver(pSkillDlg);

    pDlg = g_itMGR.FindDlg(DLG_TYPE_SKILLTREE);
    CSkillTreeDlg* pSkillTreeDlg = (CSkillTreeDlg*)pDlg;
    pSkillSlot->AddObserver(pSkillTreeDlg);

    g_pAVATAR->m_nReviveZoneNO = m_SelectedAvataInfo.m_nReviveZoneNO;
    ::CopyMemory(&g_pAVATAR->m_BasicINFO, &m_SelectedAvataInfo.m_BasicINFO, sizeof(tagBasicINFO));
    ::CopyMemory(&g_pAVATAR->m_BasicAbility,
        &m_SelectedAvataInfo.m_BasicAbility,
        sizeof(tagBasicAbility));
    ::CopyMemory(&g_pAVATAR->m_BasicAbility,
        &m_SelectedAvataInfo.m_BasicAbility,
        sizeof(tagBasicAbility));
    ::CopyMemory(&g_pAVATAR->m_GrowAbility,
        &m_SelectedAvataInfo.m_GrowAbility,
        sizeof(tagGrowAbility));
    ::CopyMemory(&g_pAVATAR->m_Skills, &m_SelectedAvataInfo.m_Skill, sizeof(tagSkillAbility));
    ::CopyMemory(&g_pAVATAR->m_Quests, &m_QuestData.m_Quests, sizeof(tagQuestData));
    ::CopyMemory(&g_pAVATAR->m_WishLIST, &m_QuestData.m_WishLIST, sizeof(tagWishLIST));

    memcpy(g_pAVATAR->m_Inventory.m_ItemLIST,
        m_SelectedAvataINV.m_INV.m_ItemLIST,
        sizeof(g_pAVATAR->m_Inventory.m_ItemLIST));

    for (int i = 0; i < MAX_WISH_ITEMS; ++i)
        CPrivateStore::GetInstance().AddItemWishList(g_pAVATAR->m_WishLIST.m_WishITEM[i], false, i);

    // 패시브 스킬에의한 능력치 초기화...
    g_pAVATAR->InitPassiveSkill();

    // g_pAVATAR->m_Inventory = m_SelectedAvataINV.m_INV;
    g_pAVATAR->Set_SEX(m_SelectedAvataInfo.m_btCharRACE);
    g_pAVATAR->UpdateAbility();

    /// skill slot setting : UpdateAbility뒤로 이동

    for (int i = 0; i < MAX_LEARNED_SKILL_CNT; i++)
        g_pAVATAR->AddNewSkill(g_pAVATAR->m_Skills.m_nSkillINDEX[i], i);

    g_ClientStorage.SetSelectAvatarName(m_strAvataName.c_str());
    g_ClientStorage.LoadAvatarData();

    /// Add Item to ItemSlot<-
    g_pAVATAR->m_Inventory.Clear();
    for (int iInvenIdx = 0; iInvenIdx < INVENTORY_TOTAL_SIZE; ++iInvenIdx) {
        if (m_SelectedAvataINV.m_INV.m_ItemLIST[iInvenIdx].IsEmpty())
            continue;

        g_pAVATAR->Set_ITEM(iInvenIdx, m_SelectedAvataINV.m_INV.m_ItemLIST[iInvenIdx]);
    }

    g_pAVATAR->m_Inventory.MakeItemIndexList();

    /// 클라이언트에서의 효과및 모션과 동기화 시키기 위하여 사용되는 변수들 Setting : 2004 / 2 / 23
    /// CObjAVT::m_ShotData는 다른 아바타들은 실제로 모션과 동기화 시키지 않고
    /// CObjUSER만 CInventory::m_ItemSHOT와 같이 2개를 사용해서 서버와 클라이언트의 모션과 동기화
    /// 시킨다.
    g_pAVATAR->SetShotData(SHOT_TYPE_ARROW,
        g_pAVATAR->CUserDATA::m_Inventory.m_ItemSHOT[SHOT_TYPE_ARROW].GetItemNO());
    g_pAVATAR->SetShotData(SHOT_TYPE_BULLET,
        g_pAVATAR->CUserDATA::m_Inventory.m_ItemSHOT[SHOT_TYPE_BULLET].GetItemNO());
    g_pAVATAR->SetShotData(SHOT_TYPE_THROW,
        g_pAVATAR->CUserDATA::m_Inventory.m_ItemSHOT[SHOT_TYPE_THROW].GetItemNO());

    //----------------------------------------------------------------------------------------------------
    /// 슬롯 정보 세팅 + CQuickDlg 의 슬롯 업데이트( Inventory와 Skill정보를 먼저 받아야 한다.)
    ///::CopyMemory ( &g_pAVATAR->m_HotICONS,		&m_SelectedAvataInfo.m_HotICONS,
    ///sizeof(CHotICONS)
    ///);
    //----------------------------------------------------------------------------------------------------
    (g_pAVATAR->GetHotIconSlot())->SetHotIcons(&m_SelectedAvataInfo.m_HotICONS);

    g_pAVATAR->SetCur_MONEY(m_SelectedAvataINV.m_INV.m_i64Money);
    g_pAVATAR->SetUniqueTag(m_SelectedAvataInfo.m_dwUniqueTAG);

    g_pAVATAR->UpdateAbility();

    char* pszName = NULL;
    SYSTEMTIME stCurrTime;
    DWORD dwABS = 0;
    for (int i = 0; i < MAX_MAINTAIN_STATUS; ++i) {
        if (dwABS = g_pAVATAR->m_GrowAbility.m_STATUS[i].m_dwExpiredSEC) {
            pszName = CStringManager::GetSingleton().GetAbility(i + AT_MAINTAIN_ABILITY);
            ;
            classTIME::AbsSecondToSystem(dwABS, stCurrTime);
            // g_itMGR.AppendChatMsg( CStr::Printf("%s을 %d년 %d월 %d일 %d시까지 사용가능합니다",
            // pszName, stCurrTime.wYear, stCurrTime.wMonth, stCurrTime.wDay, stCurrTime.wHour ),
            // IT_MGR::CHAT_TYPE_NOTICE);
            g_itMGR.AppendChatMsg(
                CStr::Printf("%s is available until %d/%d/%d at %d:%d:%02d %s",
                    pszName,
                    stCurrTime.wMonth,
                    stCurrTime.wDay,
                    stCurrTime.wYear,
                    (stCurrTime.wHour > 12) ? stCurrTime.wHour - 12 : stCurrTime.wHour,
                    stCurrTime.wMinute,
                    stCurrTime.wSecond,
                    (stCurrTime.wHour > 12) ? "PM" : "AM"),
                IT_MGR::CHAT_TYPE_NOTICE);
        }
    }

    g_pAVATAR->Set_RareITEM_Glow();
    g_pAVATAR->CreateGradeEffect();

    /// 드라이브 스킬 쿨타임 적용 : 2005/7/27 - nAvy
    /// 쿨타임이 없을경우 g_pAVATAR->m_GrowAbility.m_dwPatCoolTIME == 0
    g_SoloSkillDelayTick.SetUseItemDelay(17, (float)g_pAVATAR->GetCur_PatCoolTIME());
}

void
CGame::ProcWndMsg(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    switch (uiMsg) {
        case WM_CLOSE:
            if (this->active_state->GetStateID() == GS_MAIN)
                g_itMGR.OpenDialog(DLG_TYPE_SYSTEM);
            else
                g_pCApp->SetExitGame();
            break;
        case WM_USER_CLOSE_MSGBOX:
            if (this->active_state->GetStateID() == GS_LOGIN
                || this->active_state->GetStateID() == GS_SELECTAVATAR)
                g_EUILobby.HideMsgBox();

            break;
        case WM_USER_SERVER_DISCONNECTED:
            if (!g_pCApp->IsExitGame())
                this->active_state->ServerDisconnected();
            break;
        case WM_USER_WORLDSERVER_DISCONNECTED:
            if (!g_pCApp->IsExitGame())
                this->active_state->WorldServerDisconnected();
            break;
        default:
            break;
    }
}
int
CGame::GetCurrStateID() {
    return this->active_state->GetStateID();
}

/// 스크린 캡쳐시에 BMP 파일을 지울것이냐 말것이냐..
// bool g_bDontDeleteCapFile = false;

void
CGame::ProcInput() {
    tagWNDMSG Msg;
    while (CGame::GetInstance().m_WndMsgQ.GetMouseMsgFromQ(Msg)) {
        if (!this->active_state->ProcMouseInput(Msg.uiMsg, Msg.wParam, Msg.lParam)) {
        }
    }
    while (CGame::GetInstance().m_WndMsgQ.GetKeyboardMsgFromQ(Msg)) {
        this->active_state->ProcKeyboardInput(Msg.uiMsg, Msg.wParam, Msg.lParam);

        switch (Msg.uiMsg) {
            case WM_SYSKEYDOWN:
                switch (Msg.wParam) {
                    case '0':
                        CCursor::GetInstance().ChangeNextState();
                        // CGame::GetInstance().ToggleUserMouseFlag();
                        // CGame::GetInstance().SetUserCursor( CGame::CURSOR_DEFAULT );
                        break;
                    case '/':
                        g_bDontDeleteCapFile = !g_bDontDeleteCapFile;
                        break;
                    case VK_RETURN: {
                        ChangeScreenMode();
                        CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_OPTION);
                        if (pDlg) {
                            COptionDlg* pOptionDlg = (COptionDlg*)pDlg;
                            pOptionDlg->ChangeScreenModeByHotKey();
                        }
                        break;
                    }
                    default:
                        break;
                }
            default:
                break;
        }
    }
}
void
CGame::ChangeScreenMode() {
    t_OptionResolution Resolution = g_ClientStorage.GetResolution();
    int iWidth = Resolution.iWidth;
    int iHeight = Resolution.iHeight;
    int iDepth = Resolution.iDepth;

    if (g_pCApp->IsFullScreenMode())
        g_pCApp->SetFullscreenMode(false);
    else
        g_pCApp->SetFullscreenMode(true);

    g_pCApp->ResizeWindowByClientSize(iWidth, iHeight, iDepth, true);

    CCursor::GetInstance().ReloadCursor();
}

short
CGame::Get_XPos() {
    return (short)m_ptCurrMouse.x;
}

short
CGame::Get_YPos() {
    return (short)m_ptCurrMouse.y;
}

void
CGame::BeginRepair(int iType) {
    CCursor::GetInstance().SetCursorType(CCursor::CURSOR_REPAIR);
    m_iRepairMode = iType;
    g_itMGR.OpenDialog(DLG_TYPE_ITEM, false);
    g_itMGR.BeginRepair();
}

void
CGame::EndRepair() {
    m_iRepairMode = REPAIR_NONE;
    g_itMGR.EndRepair();
}

int
CGame::GetRepairMode() {
    return m_iRepairMode;
}

void
CGame::ResetRepairMode() {
    m_iRepairMode = REPAIR_NONE;
}

void
CGame::SetUsingRepairItemInvenIdx(int iItemInvenIdx) {
    m_iUsingRepairItemInvenIdx = iItemInvenIdx;
}
int
CGame::GetUsingRepairItemInvenIdx() {
    return m_iUsingRepairItemInvenIdx;
}
void
CGame::SetRepairNpcSvrIdx(WORD wNpcSvrIdx) {
    m_wRepairNpc = wNpcSvrIdx;
}
WORD
CGame::GetRepairNpcSvrIdx() {
    return m_wRepairNpc;
}

DWORD
CGame::GetRight() {
    return m_dwRight;
}

void
CGame::SetRight(DWORD dwRight) {
    m_dwRight = dwRight;
}

void
CGame::SetAppraisalNpcSvrIdx(WORD wNpcSvrIdx) {
    m_wAppraisalNpc = wNpcSvrIdx;
}

WORD
CGame::GetAppraisalNpcSvrIdx() {
    return m_wAppraisalNpc;
}

void
CGame::BeginAppraisal() {
    CCursor::GetInstance().SetCursorType(CCursor::CURSOR_APPRAISAL);
    m_iAppraisalMode = APPRAISAL_NPC;
    g_itMGR.BeginAppraisal();
}

void
CGame::EndAppraisal() {
    m_iAppraisalMode = APPRAISAL_NONE;
    g_itMGR.EndAppraisal();
}

__int64
CGame::GetAppraisalCost() {
    return m_i64AppraisalCost;
}

void
CGame::SetAppraisalCost(__int64 i64Cost) {
    m_i64AppraisalCost = i64Cost;
}

void
CGame::AcceptedConnectLoginSvr() {
    this->active_state->AcceptedConnectLoginSvr();
}

void
CGame::UpdateCurrentState() {
    this->active_state->Update(g_pCApp->GetMessage());
}

void
CGame::SetJoinChannelNo(int iNo) {
    m_iJoinChannelNo = iNo;
}
int
CGame::GetJoinChannelNo() {
    return m_iJoinChannelNo;
}

bool
CGame::AddTCommand(CTCommand* pCmd) {
    assert(pCmd);
    if (pCmd) {
        m_CommandQ.Push_Back(pCmd);
        return true;
    }
    return false;
}

void
CGame::ProcCommand() {
    CTCommand* pCmd = m_CommandQ.GetCommand();
    if (pCmd) {
        if (pCmd->Exec(NULL))
            delete pCmd;
        else {
            if (pCmd->IsWaitExecSucceed())
                m_CommandQ.Push_Front(pCmd);
            else
                m_CommandQ.Push_Back(pCmd);
        }
    }
}

void
CGame::SetPayType(WORD paytype) {
    m_paytype = paytype;
}

WORD
CGame::GetPayType() {
    // EXT
    if (m_paytype & PLAY_FLAG_EXTRA_CHAR && m_paytype & PLAY_FLAG_EXTRA_STOCK) {
        return PAY_PLATINUM;
    } else if (m_paytype & PLAY_FLAG_KOREA_DEFAULT) {
        return PAY_PREMIUM;
    }
    // NO EXT
    else {
        switch (m_paytype) {
            case BILLING_MSG_PAY_FU: // FU	로그인 된 사용자는 무료 아이디 사용자임을 나타낸다.
                return PAY_FREE;
            case BILLING_MSG_PAY_FQ: // FQ	로그인 된 사용자는 개인정량 사용자(프리미엄)임을
                                     // 나타낸다.
            case BILLING_MSG_PAY_FA: // FA	로그인 된 사용자는 개인정액 사용자(프리미엄)임을
                                     // 나타낸다.
                return PAY_PREMIUM;
            case BILLING_MSG_PAY_GU: // GU	로그인 된 사용자는 게임방 사용자(플레티넘)임을 나타낸다.
            case BILLING_MSG_PAY_GQ: // GQ	(플레티넘)게임방에서 로그인을 시도하였으나 게임방의
                                     // 과금기간(시간)이 만료되어 개인정량으로 로그인 되었음을
                                     // 나타낸다.
            case BILLING_MSG_PAY_IQ: // IQ	(플레티넘)게임방에서 로그인을 시도하였으나 이미 계약된
                                     // IP 숫자를 모두 사용하고 있어 개인정량으로 로그인 되었음을
                                     // 나타낸다.
            case BILLING_MSG_PAY_FAP: // FAP	로그인 된 사용자는 개인정액 사용자(플레티넘)임을
                                      // 나타낸다.
                return PAY_PLATINUM;

            default:
                break;
        }
    }

    return PAY_FREE;
}

WORD
CGame::GetDetailPayType() {
    return m_paytype;
}

void
CGame::SetExpireTime(const char* time) {
    assert(time);
    if (time) {
        m_time_setexpiretime = g_GameDATA.GetGameTime();
        m_expireTime = time;
    }
}

void
CGame::SetExpireTimeExt(int type, const char* time) {
    assert(time);

    if (time && type > 0 && type < PAY_FLAG_JP_SIZE) {
        m_time_setexpiretimeExt[type] = g_GameDATA.GetGameTime();
        m_expireTimeExt[type] = time;
    }
}

const std::string&
CGame::GetExpireTime() {
    return m_expireTime;
}

const std::string&
CGame::GetExpireTimeExt(int type) {
    if (type > 0 && type < PAY_FLAG_JP_SIZE) {
        return m_expireTimeExt[type];
    }
    return "";
}

DWORD
CGame::GetSetExpireTime() {
    return m_time_setexpiretime;
}

DWORD
CGame::GetSetExpireTimeExt(int type) {
    if (type > 0 && type < PAY_FLAG_JP_SIZE) {
        return m_time_setexpiretimeExt[type];
    }
    return 0;
}

/// 1: 날짜, 2:분, 0:없음
void
CGame::SetExpireTimeType(int type) {
    m_expiretime_type = type;
}

void
CGame::SetExpireTimeTypeExt(int type, int time) {
    if (type > 0 && type < PAY_FLAG_JP_SIZE) {
        m_expiretime_typeExt[type] = type;
    }
}

int
CGame::GetExpireTimeType() {
    return m_expiretime_type;
}

int
CGame::GetExpireTimeTypeExt(int type) {
    if (type > 0 && type < PAY_FLAG_JP_SIZE) {
        return m_expiretime_typeExt[type];
    }
    return 0;
}

void
CGame::SetSelectedServerID(unsigned int id) {
    m_uiSelectedServerID = id;
}

unsigned int
CGame::GetSelectedServerID() {
    return m_uiSelectedServerID;
}
bool
CGame::IsAutoRun() {
    return m_bAutoRun;
}
void
CGame::ToggleAutoRun() {
    if (m_bAutoRun)
        m_bAutoRun = false;
    else
        m_bAutoRun = true;
}

void
CGame::ResetAutoRun() {
    m_bAutoRun = false;
}

//-------------------------------------------------------------------------------------------
/// @brief 일본 파트너사 구분을 위한 콤보박스를 활성화시킬것인가?
///    - 일본이면서 NHN이 아닐경우에만 활성화 시킨다.
//-------------------------------------------------------------------------------------------
bool
CGame::IsActiveRouteComboBox() {
    return false;
}

std::string&
CGame::GetClientFileVersion() {
    return m_ClientFileVersion.m_strFileVersion;
}