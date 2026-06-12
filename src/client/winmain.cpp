#include "stdafx.h"

#include "CApplication.h"
#include "Game.h"
#include "Network/CNetwork.h"
#include "Util/VFSManager.h"
#include "CClientStorage.h"
#include "System/CGame.h"
#include "Interface/ExternalUI/CLogin.h"

#include "Util/CheckHack.h"

#include "rose/common/common_interface.h"

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

using namespace Rose;

class HighResTimerScope {
public:
    HighResTimerScope() : active(::timeBeginPeriod(1) == TIMERR_NOERROR) {}
    ~HighResTimerScope() {
        if (active) {
            ::timeEndPeriod(1);
        }
    }
    HighResTimerScope(const HighResTimerScope&) = delete;
    HighResTimerScope& operator=(const HighResTimerScope&) = delete;

private:
    bool active;
};

bool
Init_DEVICE(void) {
    bool bRet = false;

    //--------------------------[ engine related ]-----------------------//
    ::initZnzin();
    ::openFileSystem("data.idx");
    ::doScript("scripts/init.lua");

    t_OptionResolution Resolution = g_ClientStorage.GetResolution();
    ::setDisplayQualityLevel(c_iPeformances[g_ClientStorage.GetVideoPerformance()]);
    t_OptionVideo Video;
    g_ClientStorage.GetVideoOption(Video);
    setFullSceneAntiAliasing(Video.iAntiAlising);

    if (!g_pCApp->IsFullScreenMode()) {
        RECT ClientRt;
        GetClientRect(g_pCApp->GetHWND(), &ClientRt);
        ::setScreen(ClientRt.right,
            ClientRt.bottom,
            Resolution.iDepth,
            g_pCApp->IsFullScreenMode());
    } else
        ::setScreen(g_pCApp->GetWIDTH(),
            g_pCApp->GetHEIGHT(),
            Resolution.iDepth,
            g_pCApp->IsFullScreenMode());

    bRet = ::attachWindow((const void*)g_pCApp->GetHWND());

    CD3DUtil::Init();

    g_pSoundLIST = new CSoundLIST(g_pCApp->GetHWND());
    g_pSoundLIST->Load("3DDATA\\STB\\FILE_SOUND.stb");

    return bRet;
}

//-------------------------------------------------------------------------------------------------
void
Free_DEVICE(void) {
    delete g_pSoundLIST;

    CD3DUtil::Free();

    //--------------------------[ engine related ]-----------------------//
    ::detachWindow();

    ::closeFileSystem();
    ::destZnzin();
}

int APIENTRY
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow) {
    HighResTimerScope timer_scope;

    // Initialize the logger so LogString / LOG_* calls reach a file. Without
    // this call, log::set_max_level() is never invoked and every log line is
    // silently dropped by the log crate's default LevelFilter::Off.
    // Raise to LogLevel::Debug when investigating combat/skill issues
    // (LogString(LOG_DEBUG_, ...) lines, CombatTrace, etc.).
    Rose::Common::logger_init("client.log", Rose::Common::LogLevel::Info);

    VHANDLE hVFS = OpenVFS("data.idx", "r");

    CVFSManager& vfs = CVFSManager::GetSingleton();
    vfs.SetVFS(hVFS);
    vfs.InitVFS(VFS_TRIGGER_VFS);

    GetLocalTime(&g_GameDATA.m_SystemTime);

    g_pCApp = CApplication::Instance();
    g_pNet = CNetwork::Instance(hInstance);
    g_pCRange = CRangeTBL::Instance();

    if (!g_pCRange->Load_TABLE("3DDATA\\TERRAIN\\O_RANGE.TBL")) {
        g_pCApp->ErrorBOX("3DDATA\\TERRAIN\\O_Range.TBL file open error",
            CUtil::GetCurrentDir(),
            MB_OK);
        return 0;
    }

    if (!g_pCApp->ParseArgument(lpCmdLine)) {
        return 0;
    }

    vfs.load_stb(g_TblResolution, RESOLUTION_STB);
    vfs.load_stb(g_TblCamera, CAMERA_STB);

    g_ClientStorage.Load();

    t_OptionResolution Resolution = g_ClientStorage.GetResolution();
    UINT iFullScreen = g_ClientStorage.GetVideoFullScreen();

    g_pCApp->SetFullscreenMode(iFullScreen);
    g_pCApp->CreateWND("classCLIENT",
        CStr::Printf("%s", GameStaticConfig::NAME),
        Resolution.iWidth,
        Resolution.iHeight,
        Resolution.iDepth,
        hInstance);

    g_pObjMGR = CObjectMANAGER::Instance();

    g_pCApp->ResetExitGame();

#ifdef DISCORD
    g_pCApp->init_discord();
    g_pCApp->update_discord_status(nullptr);
#endif

    bool bDeviceInitialized = Init_DEVICE();
    if (bDeviceInitialized) {
        CGame::GetInstance().GameLoop();
    }

    Free_DEVICE();

    g_pCApp->Destroy();
    g_pNet->Destroy();

    g_pCRange->Destroy();

    return 0;
}
