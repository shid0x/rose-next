/*
    $Header: /Client/CApplication.h 30    05-03-15 9:32a Navy $
*/

#ifndef __CAPPLICATION_H
#define __CAPPLICATION_H

#include <set>

#include "util\CRangeTBL.h"
#include "sound\IO_Sound.h"

#include "discord_config.h"

#ifdef DISCORD
    #include "discord.h"

    // clang-format off
    namespace Rose::Common {
        enum class Job;
    }
    // clang-format on
#endif

class ApplicationVideoMode {
public:
    int depth;
    int width;
    int height;
    int refresh_rate;

    bool operator<(const ApplicationVideoMode& other) const {
        if (depth < other.depth) {
            return true;
        }

        if (other.depth < depth) {
            return false;
        }

        if (width < other.width) {
            return true;
        }

        if (other.width < width) {
            return false;
        }

        if (height < other.height) {
            return true;
        }

        if (other.height < height) {
            return false;
        }

        if (refresh_rate < other.refresh_rate) {
            return true;
        }

        if (other.refresh_rate < refresh_rate) {
            return false;
        }

        return false;
    }
};

///
/// How the game window relates to the screen.
///
/// Borderless is a *windowed* D3D device that happens to cover the monitor. That
/// distinction is the whole point: an exclusive device owns the display mode, and every
/// handover of that ownership back to the compositor blanks the monitor for a frame or two.
/// See doc/d3d9ex-migration.md -> "Black flashes in exclusive fullscreen".
///
enum e_ScreenMode {
    SCREEN_MODE_WINDOWED = 0, ///< resizable frame, caption, user-chosen client size
    SCREEN_MODE_BORDERLESS, ///< WS_POPUP over the whole monitor, windowed D3D device
    SCREEN_MODE_EXCLUSIVE, ///< legacy Windowed=FALSE device that owns the display mode
};

///
/// Application Class
///
class CApplication {
private:
    static CApplication* m_pInstance;

    HWND m_hWND; ///< Window Handle
    HINSTANCE m_hINS;
    MSG m_Message;

    bool m_bExitGame;
    //	WORD		m_wStatus;
    WORD m_wActive;
    e_ScreenMode m_ScreenMode;
    bool m_bViewWireMode;
    short m_nScrWidth;
    short m_nScrHeight;
    int m_nScrDepth; ///< colour depth last handed to the engine

    /// Inside a user drag of the window frame (WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE).
    /// WM_SIZE fires continuously during the drag and every resize costs a full device
    /// teardown, so the work is deferred to the end of the drag.
    bool m_bInSizeMove;
    /// Re-entrancy guard. Our own resizing calls MoveWindow, which synchronously posts
    /// WM_SIZE -- without this the handler would recurse back into the resize.
    bool m_bResizingEngine;
    /// The window is created before initZnzin(), and torn down before the process exits,
    /// so there are windows either side of the game loop where the engine globals are
    /// null. setScreen()/resetScreen() would dereference them, so resize handling is
    /// gated on this rather than on reasoning about which messages can arrive when.
    bool m_bEngineReady;

    CStrVAR m_Caption;

protected:
    CApplication();
    ~CApplication();

public:
    static CApplication* Instance();
    void Destroy();

    HWND GetHWND() { return m_hWND; }
    HINSTANCE GetHINS() { return m_hINS; }

    /// True only for the *exclusive* device. Every caller of this drives
    /// setScreen(..., use_fullscreen) and therefore D3DPRESENT_PARAMETERS::Windowed, so
    /// borderless must report false here -- it is a windowed device by construction.
    bool IsFullScreenMode() { return m_ScreenMode == SCREEN_MODE_EXCLUSIVE; }
    /// True when the window covers the monitor with no frame (windowed device).
    bool IsBorderless() { return m_ScreenMode == SCREEN_MODE_BORDERLESS; }
    /// True only for the ordinary resizable window -- the one case where the user can
    /// change the client area by dragging, and the only one WM_SIZE should act on.
    bool IsWindowedFrame() { return m_ScreenMode == SCREEN_MODE_WINDOWED; }
    e_ScreenMode GetScreenMode() { return m_ScreenMode; }

    WORD IsActive() { return m_wActive; }

    bool IsExitGame() { return m_bExitGame; }
    void SetExitGame();
    void ResetExitGame();

    //	WORD		GetStatus ()			{	return m_wStatus;			}
    //	void		SetStatus(WORD wStatus)	{	m_wStatus = wStatus;		}

    short GetWIDTH() { return m_nScrWidth; }
    short GetHEIGHT() { return m_nScrHeight; }
    void SetWIDTH(short iWidth) { m_nScrWidth = iWidth; }
    void SetHEIGHT(short iHeight) { m_nScrHeight = iHeight; }

    /// Set once the engine is up (after Init_DEVICE) and cleared before teardown.
    /// Gates the WM_SIZE resize path, which would otherwise touch null engine globals.
    void SetEngineReady(bool bReady) { m_bEngineReady = bReady; }

    /// Reconfigure the renderer to whatever the window's client area currently is.
    /// Windowed mode only. The backbuffer is stretched to the client rect while mouse
    /// input stays in raw client pixels, so any mismatch silently desyncs hit-testing
    /// from what is drawn. Unlike ResizeWindowByClientSize this never moves or resizes
    /// the window -- it accepts the size the user chose and makes the engine match.
    void ApplyWindowedClientResize();

    void Show() {
        ::ShowWindow(GetHWND(), SW_SHOWNORMAL);
        ::UpdateWindow(GetHWND());
        ::SetFocus(GetHWND());
    }

    bool ParseArgument(char* pStr);
    bool CreateWND(char* szClassName,
        char* szWindowName,
        short nWidth,
        short nHeight,
        int iDepth,
        HINSTANCE hInstance);
    void DestroyWND(void);

    void SetCaption(char* szStr);
    DWORD DisplayFrameRate(void);
    bool GetMessage(void);

    int wm_COMMAND(WPARAM wParam);
    LRESULT MessageProc(HWND hWnd, UINT uiMsg, WPARAM wParam, LPARAM lParam);
    void ErrorBOX(char* szText, char* szCaption, UINT uType = (MB_OK | MB_TOPMOST));
    void
    ResizeWindowByClientSize(int& iClientWidth, int& iClientHeight, int iDepth, bool update_engine);

    /// Compatibility shim for the existing call sites (winmain, the options dialog,
    /// Alt+Enter). "true" means whichever fullscreen flavour is configured -- see
    /// PreferredFullscreenMode().
    void SetFullscreenMode(bool bFullScreenMode);
    void SetScreenMode(e_ScreenMode mode);

    /// Which mode "fullscreen" resolves to. Borderless unless the player opts back into
    /// the legacy exclusive device with [VIDEO] EXCLUSIVE_FULLSCREEN=1 in rose-next.ini
    /// (or ROSE_EXCLUSIVE_FULLSCREEN=1 in the environment). Read once, then cached.
    static e_ScreenMode PreferredFullscreenMode();

    /// Full pixel rect of the monitor this window is on (primary monitor before the window
    /// exists). This is the only correct size for a borderless backbuffer: anything else
    /// is stretched by D3D and silently desyncs mouse hit-testing from what is drawn.
    static void GetMonitorRect(HWND hWnd, RECT& out);

    std::set<ApplicationVideoMode> get_video_modes();

#ifdef DISCORD
    std::unique_ptr<discord::Core> discord_core;

    bool CApplication::init_discord();
    void update_discord_status(CObjUSER* user);
#endif
};

//-------------------------------------------------------------------------------------------------

extern CApplication* g_pCApp;
extern CRangeTBL* g_pCRange;
extern CSoundLIST* g_pSoundLIST;

//-------------------------------------------------------------------------------------------------
#endif