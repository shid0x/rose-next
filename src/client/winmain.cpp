#include "stdafx.h"

#include "CApplication.h"
#include "Game.h"
#include "Network/CNetwork.h"
#include "Util/VFSManager.h"
#include "CClientStorage.h"
#include "System/CGame.h"
#include "System/FrameProfiler.h"
#include "Interface/ExternalUI/CLogin.h"

#include "Util/CheckHack.h"

#include "rose/common/common_interface.h"

#include <ctype.h>
#include <string.h>

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

    // Whole-frame hitch log. Hand-edited key with no options-screen control, so
    // reading it once here is enough. Logged for the same reason as the budget
    // above: a diagnostic that is silently off looks exactly like a session with
    // no hitches in it.
    FrameProfiler::SetSpikeLogMs(g_ClientStorage.GetFrameSpikeLogMs());
    if (g_ClientStorage.GetFrameSpikeLogMs() > 0) {
        LOG_INFO("Frame spike log: ON, threshold {} ms ([VIDEO] FRAME_SPIKE_LOG_MS)",
            g_ClientStorage.GetFrameSpikeLogMs());
    }

    // Framerate cap. Applied after doScript for the same reason as the load
    // budget above: INIT.LUA line 39 calls setFramerateRange(15, 1000), so
    // anything set before it is overwritten. 0 leaves that alone.
    //
    // The engine's limiter is ::Sleep()-based (zz_system::sleep), so the cap is
    // approximate and lands slightly under the target -- Sleep(n) waits at least
    // n ms. It also only ever *adds* delay, so it cannot help a frame that is
    // already late.
    //
    // min_framerate stays at INIT.LUA's 15: it feeds min_swap_msec, which sizes
    // the amortiser's per-frame allowance, and is not a framerate cap.
    if (g_ClientStorage.GetMaxFps() > 0) {
        ::setFramerateRange(15, (int)g_ClientStorage.GetMaxFps());
        LOG_INFO("Framerate cap: {} fps ([VIDEO] MAX_FPS)", g_ClientStorage.GetMaxFps());
    } else {
        LOG_INFO("Framerate cap: none ([VIDEO] MAX_FPS=0; INIT.LUA sets max 1000)");
    }

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

//---------------------------------------------------------------------------------------------------------
/// Log verbosity for client.log.
///
/// Info by default. The CombatTrace / skill / streaming diagnostics all report at
/// Debug (LogString(LOG_DEBUG_, ...) maps to LogLevel::Debug), so investigating a
/// combat presentation problem used to need a rebuild of this file. It is now a
/// runtime switch so a player can reproduce and hand over a log:
///
///   rose-next.ini  ->  [LOG]  LEVEL=debug
///   environment    ->  ROSE_LOG_LEVEL=debug        (wins over the ini)
///
/// Accepted: trace, debug, info, warn, error, off. Anything unrecognised falls back
/// to Info rather than silently disabling the log.
///
/// Debug is *loud* -- every terrain object logs one line while chunks stream in (see
/// the note in rose/common/log.h), so client.log grows fast and the formatting cost
/// is paid on exactly the frames that already hitch. It is a diagnostic setting, not
/// something to leave on.
//---------------------------------------------------------------------------------------------------------
static const struct {
    const char* szName;
    Rose::Common::LogLevel eLevel;
} g_LogLevelNAMES[] = {
    {"trace", Rose::Common::LogLevel::Trace},
    {"debug", Rose::Common::LogLevel::Debug},
    {"info", Rose::Common::LogLevel::Info},
    {"warn", Rose::Common::LogLevel::Warn},
    {"error", Rose::Common::LogLevel::Error},
    {"off", Rose::Common::LogLevel::Off},
};

static Rose::Common::LogLevel
ResolveLogLevel(void) {
    char szLevel[32] = {0};

    ::GetPrivateProfileStringA("LOG", "LEVEL", "info", szLevel, sizeof(szLevel), "./rose-next.ini");

    char szEnv[32] = {0};
    if (::GetEnvironmentVariableA("ROSE_LOG_LEVEL", szEnv, sizeof(szEnv)) != 0) {
        ::lstrcpynA(szLevel, szEnv, sizeof(szLevel));
    }

    for (int iC = 0; szLevel[iC]; ++iC) {
        szLevel[iC] = (char)::tolower((unsigned char)szLevel[iC]);
    }

    for (size_t iL = 0; iL < _countof(g_LogLevelNAMES); ++iL) {
        if (0 == ::strcmp(szLevel, g_LogLevelNAMES[iL].szName)) {
            return g_LogLevelNAMES[iL].eLevel;
        }
    }

    return Rose::Common::LogLevel::Info;
}

static const char*
LogLevelNAME(Rose::Common::LogLevel eLevel) {
    for (size_t iL = 0; iL < _countof(g_LogLevelNAMES); ++iL) {
        if (g_LogLevelNAMES[iL].eLevel == eLevel) {
            return g_LogLevelNAMES[iL].szName;
        }
    }
    return "info";
}

int APIENTRY
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow) {
    HighResTimerScope timer_scope;

    // Initialize the logger so LogString / LOG_* calls reach a file. Without
    // this call, log::set_max_level() is never invoked and every log line is
    // silently dropped by the log crate's default LevelFilter::Off.
    // Level comes from [LOG] LEVEL / ROSE_LOG_LEVEL -- see ResolveLogLevel above;
    // use debug when investigating combat/skill issues (LogString(LOG_DEBUG_, ...)
    // lines, CombatTrace, etc.).
    //
    // Log::set_max_level must mirror whatever is passed to logger_init: it is the
    // C++-side copy that lets Log::legacy_printf drop a filtered record *before*
    // formatting it. Miss it and the only cost is the old behaviour (format, then
    // discard in Rust) -- but that is thousands of wasted fmt::sprintf calls per
    // terrain chunk load. See rose/common/log.h.
    const Rose::Common::LogLevel log_level = ResolveLogLevel();
    Rose::Common::logger_init("client.log", log_level);
    Log::set_max_level(log_level);

    // Recorded so a handed-over client.log states its own verbosity: a log with no
    // CombatTrace lines otherwise reads identically whether the trace was off or
    // the traced code never ran.
    LOG_INFO("Log level: {} ([LOG] LEVEL in rose-next.ini, ROSE_LOG_LEVEL overrides)",
        LogLevelNAME(log_level));

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
