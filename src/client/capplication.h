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
    bool m_bFullScreenMode;
    bool m_bViewWireMode;
    short m_nScrWidth;
    short m_nScrHeight;

    CStrVAR m_Caption;

protected:
    CApplication();
    ~CApplication();

public:
    static CApplication* Instance();
    void Destroy();

    HWND GetHWND() { return m_hWND; }
    HINSTANCE GetHINS() { return m_hINS; }
    bool IsFullScreenMode() { return m_bFullScreenMode; }
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

    void SetFullscreenMode(bool bFullScreenMode);

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