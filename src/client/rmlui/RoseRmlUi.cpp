#include "stdafx.h"

#include "RoseRmlUi.h"

#include "RoseRmlRenderer.h"
#include "RoseRmlSystem.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include "rose/common/log.h"

#include <stdlib.h>
#include <string>

namespace {

RoseRmlRenderer* g_pRenderer = NULL;
RoseRmlSystem* g_pSystem = NULL;
Rml::Context* g_pContext = NULL;
Rml::ElementDocument* g_pSpikeDocument = NULL;
bool g_bInitialised = false;
int g_iEnabled = -1; ///< -1 = not yet resolved

/// Assets live loose under the launch dir. The VFS-vs-loose decision for
/// shipping .rml/.rcss is Phase 1 work ( see doc/rmlui-evaluation.md ); the
/// spike deliberately uses loose files so iteration needs no rebake.
const char* kAssetDir = "3ddata/rmlui/";

bool
ResolveEnabled() {
    if (g_iEnabled >= 0)
        return g_iEnabled != 0;

    g_iEnabled = 0;

    const char* pEnv = getenv("ROSE_RMLUI");
    if (pEnv != NULL && *pEnv != '0') {
        g_iEnabled = 1;
    } else {
        char szBuf[16] = {0};
        /// Same INI the D3D9EX A/B switch uses.
        GetPrivateProfileStringA("VIDEO", "RMLUI", "0", szBuf, sizeof(szBuf),
            ".\\rose-next.ini");
        if (szBuf[0] != '\0' && szBuf[0] != '0')
            g_iEnabled = 1;
    }

    /// Always say which path is live: a toggle that silently does nothing
    /// produces a false negative, which cost a debugging round on the D3D9Ex
    /// work.
    LOG_INFO("[rmlui] spike overlay {}", g_iEnabled ? "ENABLED" : "disabled");
    return g_iEnabled != 0;
}

/// Maps a Win32 mouse message to RmlUi's button index, or -1.
int
MouseButtonFromMsg(UINT uiMsg) {
    switch (uiMsg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            return 0;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            return 1;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            return 2;
        default:
            return -1;
    }
}

} // namespace

namespace RoseRmlUi {

bool
IsEnabled() {
    return ResolveEnabled();
}

bool
Initialise(HWND hWnd, void* pD3DDevice, int iWidth, int iHeight) {
    if (!ResolveEnabled())
        return false;
    if (g_bInitialised)
        return true;
    if (pD3DDevice == NULL)
        return false;

    g_pSystem = new RoseRmlSystem();
    g_pRenderer = new RoseRmlRenderer();

    if (!g_pRenderer->Initialise((IDirect3DDevice9*)pD3DDevice)) {
        LOG_ERROR("[rmlui] render interface failed to initialise");
        delete g_pRenderer;
        g_pRenderer = NULL;
        delete g_pSystem;
        g_pSystem = NULL;
        return false;
    }
    g_pRenderer->SetViewportSize(iWidth, iHeight);

    Rml::SetSystemInterface(g_pSystem);
    Rml::SetRenderInterface(g_pRenderer);

    if (!Rml::Initialise()) {
        LOG_ERROR("[rmlui] Rml::Initialise failed");
        return false;
    }

    g_pContext = Rml::CreateContext("main", Rml::Vector2i(iWidth, iHeight));
    if (g_pContext == NULL) {
        LOG_ERROR("[rmlui] failed to create context");
        Rml::Shutdown();
        return false;
    }

    Rml::Debugger::Initialise(g_pContext);

    /// Fonts: FreeType-backed. Falls back silently if the spike assets are not
    /// deployed - the log line above is the signal to check.
    const std::string strFont = std::string(kAssetDir) + "fonts/LatoLatin-Regular.ttf";
    if (!Rml::LoadFontFace(strFont.c_str()))
        LOG_WARN("[rmlui] could not load font face '{}'", strFont.c_str());

    const std::string strDoc = std::string(kAssetDir) + "spike.rml";
    g_pSpikeDocument = g_pContext->LoadDocument(strDoc.c_str());
    if (g_pSpikeDocument != NULL) {
        g_pSpikeDocument->Show();
        LOG_INFO("[rmlui] loaded spike document '{}'", strDoc.c_str());
    } else {
        LOG_WARN("[rmlui] could not load document '{}'", strDoc.c_str());
    }

    g_bInitialised = true;
    return true;
}

void
Shutdown() {
    if (!g_bInitialised)
        return;

    g_pSpikeDocument = NULL;
    g_pContext = NULL;

    Rml::Debugger::Shutdown();
    Rml::Shutdown();

    if (g_pRenderer != NULL) {
        g_pRenderer->Shutdown();
        delete g_pRenderer;
        g_pRenderer = NULL;
    }
    delete g_pSystem;
    g_pSystem = NULL;

    g_bInitialised = false;
}

/// resetScreen() releases the D3D device and creates a brand new one
/// ( zz_renderer_d3d::cleanup -> SAFE_RELEASE(d3d_device), then initialize() ).
/// Any cached device pointer is therefore dead after a resolution change,
/// fullscreen toggle or frame-drag resize.
///
/// Explicitly hooking every call site proved unreliable -- they are spread
/// across CApplication (4) and the video options dialog (2), and it is easy to
/// add a seventh. So the authoritative check is this one: compare the engine's
/// current device against ours every frame and rebuild when it changes. The
/// explicit hooks remain as an optimisation ( they release while the old device
/// is still current, which is tidier ), but correctness does not depend on them.
///
/// Releasing our resources here is safe even though the engine already dropped
/// its reference: our own outstanding references keep the old device object
/// alive until we let go, so this is an orderly teardown rather than a
/// use-after-free.
static void
SyncDeviceIfChanged() {
    if (!g_bInitialised || g_pRenderer == NULL)
        return;

    IDirect3DDevice9* pCurrent = reinterpret_cast<IDirect3DDevice9*>(::getDevice());
    if (pCurrent == g_pRenderer->GetDevice())
        return;

    LOG_INFO("[rmlui] device changed, rebuilding overlay resources");

    Rml::ReleaseTextures();
    g_pRenderer->ReleaseDeviceObjects();
    g_pRenderer->SetDevice(pCurrent);

    if (pCurrent != NULL) {
        int iWidth = g_pCApp->GetWIDTH();
        int iHeight = g_pCApp->GetHEIGHT();
        g_pRenderer->SetViewportSize(iWidth, iHeight);
        g_pRenderer->CreateDeviceObjects();
        if (g_pContext != NULL)
            g_pContext->SetDimensions(Rml::Vector2i(iWidth, iHeight));
    }
}

void
Update() {
    if (!g_bInitialised || g_pContext == NULL)
        return;

    SyncDeviceIfChanged();
    g_pContext->Update();
}

void
Render() {
    if (!g_bInitialised || g_pContext == NULL || g_pRenderer == NULL)
        return;

    g_pRenderer->BeginFrame();
    g_pContext->Render();
    g_pRenderer->EndFrame();
}

void
OnBeforeDeviceRebuild() {
    if (!g_bInitialised)
        return;

    /// Force RmlUi to drop and re-request every texture; our own records handle
    /// the compiled geometry, which RmlUi will NOT re-issue on its own.
    Rml::ReleaseTextures();
    if (g_pRenderer != NULL)
        g_pRenderer->ReleaseDeviceObjects();
}

void
OnAfterDeviceRebuild(int iWidth, int iHeight) {
    if (!g_bInitialised || g_pRenderer == NULL)
        return;

    /// The device object itself is new -- adopt it before touching anything.
    g_pRenderer->SetDevice(reinterpret_cast<IDirect3DDevice9*>(::getDevice()));
    g_pRenderer->SetViewportSize(iWidth, iHeight);
    g_pRenderer->CreateDeviceObjects();

    if (g_pContext != NULL)
        g_pContext->SetDimensions(Rml::Vector2i(iWidth, iHeight));
}

void
OnResize(int iWidth, int iHeight) {
    if (!g_bInitialised)
        return;

    if (g_pRenderer != NULL)
        g_pRenderer->SetViewportSize(iWidth, iHeight);
    if (g_pContext != NULL)
        g_pContext->SetDimensions(Rml::Vector2i(iWidth, iHeight));
}

bool
ProcessWndMsg(HWND hWnd, UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    if (!g_bInitialised || g_pContext == NULL)
        return false;

    switch (uiMsg) {
        case WM_MOUSEMOVE: {
            const int x = (short)LOWORD(lParam);
            const int y = (short)HIWORD(lParam);
            g_pContext->ProcessMouseMove(x, y, 0);
            /// Consume only when the cursor is actually over a document element,
            /// otherwise the world would stop receiving hover/camera input.
            return g_pContext->GetHoverElement() != NULL
                && g_pContext->GetHoverElement() != g_pContext->GetRootElement();
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const int iButton = MouseButtonFromMsg(uiMsg);
            if (iButton < 0)
                return false;
            const bool bOver = g_pContext->GetHoverElement() != NULL
                && g_pContext->GetHoverElement() != g_pContext->GetRootElement();
            g_pContext->ProcessMouseButtonDown(iButton, 0);
            return bOver;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            const int iButton = MouseButtonFromMsg(uiMsg);
            if (iButton < 0)
                return false;
            const bool bOver = g_pContext->GetHoverElement() != NULL
                && g_pContext->GetHoverElement() != g_pContext->GetRootElement();
            g_pContext->ProcessMouseButtonUp(iButton, 0);
            return bOver;
        }
        case WM_MOUSEWHEEL: {
            const float fDelta = -(float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
            return g_pContext->ProcessMouseWheel(fDelta, 0) == false;
        }
        default:
            break;
    }

    return false;
}

int
GetDrawCallCount() {
    return (g_pRenderer != NULL) ? g_pRenderer->GetDrawCallCount() : 0;
}

} // namespace RoseRmlUi
