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

    // SHADOWQUALITY (rose-next.ini [VIDEO]) overrides the classic 256px
    // character shadowmap set up by INIT.LUA. Uses the engine-side override
    // (not setShadowmapSize) because INIT.LUA and the options dialog call
    // setDisplayQualityLevel, whose presets reset the raw shadowmap size.
    ::setShadowmapSizeOverride(
        ShadowQualityToShadowmapSize(g_ClientStorage.GetShadowQuality()));

    // Applied after doScript so it wins over INIT.LUA's setLazyBufferSize sizing.
    // This is what lets the lazy entrance line actually load ahead instead of
    // one node per update; 0 restores the old behaviour for A/B.
    ::setLoadBudgetPerFrameUsec((int)g_ClientStorage.GetLoadBudgetUs());
    // Logged to client.log, not just error.txt: without it there is no way to
    // tell from a user's log whether a streaming experiment was actually active,
    // which already cost one inconclusive test round.
    LOG_INFO("Engine load budget: {} us/frame ([VIDEO] LOAD_BUDGET_US, 0 = legacy pacing)",
        ::getLoadBudgetPerFrameUsec());

    t_OptionResolution Resolution = g_ClientStorage.GetResolution();
    ::setDisplayQualityLevel(c_iPeformances[g_ClientStorage.GetVideoPerformance()]);
    t_OptionVideo Video;
    g_ClientStorage.GetVideoOption(Video);
    setFullSceneAntiAliasing(Video.iAntiAlising);

    // Borderless takes this branch too, and correctly: it is a windowed device, and
    // CreateWND already snapped the window to the monitor, so the client rect *is* the
    // monitor size. Do not "fix" this to use Resolution.iWidth/iHeight -- a backbuffer that
    // differs from the client area is stretched by D3D and drifts every UI hit-test.
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

    // Into error.txt (not client-*.log) on purpose: this is the line that tells you which
    // mode is *live*, right next to the r_d3d device lines you would be comparing it
    // against. The engine alone cannot say -- borderless and windowed are both
    // "WiNmOdE(WxH)" to it, since borderless is a windowed device by design. Note this is
    // the active mode, whereas the client log's "Fullscreen resolves to ..." only says what
    // FULLSCREEN=1 *would* select.
    ::doLogf("screen: mode=%s size=%dx%d (ini FULLSCREEN=%d, EXCLUSIVE_FULLSCREEN=%d)\n",
        g_pCApp->IsFullScreenMode() ? "exclusive"
                                    : (g_pCApp->IsBorderless() ? "borderless" : "windowed"),
        (int)g_pCApp->GetWIDTH(),
        (int)g_pCApp->GetHEIGHT(),
        (int)g_ClientStorage.GetVideoFullScreen(),
        (CApplication::PreferredFullscreenMode() == SCREEN_MODE_EXCLUSIVE) ? 1 : 0);

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
    //
    // Log::set_max_level must mirror whatever is passed to logger_init: it is the
    // C++-side copy that lets Log::legacy_printf drop a filtered record *before*
    // formatting it. Miss it and the only cost is the old behaviour (format, then
    // discard in Rust) -- but that is thousands of wasted fmt::sprintf calls per
    // terrain chunk load. See rose/common/log.h.
    const Rose::Common::LogLevel log_level = Rose::Common::LogLevel::Info;
    Rose::Common::logger_init("client.log", log_level);
    Log::set_max_level(log_level);

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

    // Only now are the engine globals valid, so window-resize handling may touch them.
    g_pCApp->SetEngineReady(bDeviceInitialized);

    if (bDeviceInitialized) {
        CGame::GetInstance().GameLoop();
    }

    g_pCApp->SetEngineReady(false);

    Free_DEVICE();

    g_pCApp->Destroy();
    g_pNet->Destroy();

    g_pCRange->Destroy();

    return 0;
}
