#include "stdafx.h"

#include "RoseRmlUi.h"

#include "RoseRmlBuffBar.h"
#include "RoseRmlDamageMeter.h"
#include "RoseRmlInterfacePanel.h"
#include "RoseRmlLayout.h"
#include "RoseRmlStatusPanel.h"
#include "RoseRmlTargetFrame.h"
#include "RoseRmlRenderer.h"
#include "RoseRmlSkillBar.h"
#include "RoseRmlSkillWindow.h"
#include "RoseRmlCharacterWindow.h"
#include "RoseRmlPartyFrames.h"
#include "RoseRmlPartyInvite.h"
#include "RoseRmlPartyOptions.h"
#include "RoseRmlMessageBox.h"
#include "RoseRmlInventory.h"
#include "RoseRmlQuestJournal.h"
#include "RoseRmlSystem.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include "rose/common/log.h"

#include "../interface/CDragNDropMgr.h"
#include "tgamectrl/winctrl.h"
#include "../interface/interfacetype.h"
#include "../Sound/IO_Sound.h"

#include <stdlib.h>
#include <string>

namespace {

RoseRmlRenderer* g_pRenderer = NULL;
RoseRmlSystem* g_pSystem = NULL;
Rml::Context* g_pContext = NULL;
RoseRmlDamageMeter g_DamageMeter;
RoseRmlStatusPanel g_StatusPanel; ///< UI2: replaces CAvatarInfoDlg
RoseRmlBuffBar g_BuffBar; ///< UI2: replaces CEndurancePack::Draw
RoseRmlTargetFrame g_TargetFrame; ///< UI2: new, the selected target
RoseRmlInterfacePanel g_InterfacePanel; ///< UI2: scale / lock / reset settings
RoseRmlSkillBar g_SkillBar; ///< UI2: replaces the two CQuickBARs ( as their view )
RoseRmlSkillWindow g_SkillWindow; ///< UI2: replaces CSkillDLG ( a window )
RoseRmlCharacterWindow g_CharacterWindow; ///< UI2: replaces CCharacterDLG ( a window )
RoseRmlPartyFrames g_PartyFrames; ///< UI2: replaces CPartyDlg
RoseRmlPartyOptions g_PartyOptions; ///< UI2: replaces CPartyOptionDlg ( a window )
RoseRmlPartyInvite g_PartyInvite; ///< UI2: replaces the party request message box
RoseRmlMessageBox g_MessageBox; ///< UI2: questions and notices ( opt-in CMsgBox stand-in )
RoseRmlInventory g_Inventory; ///< UI2: replaces CItemDlg ( a window, a view over it )
RoseRmlQuestJournal g_QuestJournal; ///< UI2: replaces CQuestDlg ( a window )
bool g_bInitialised = false;
int g_iEnabled = -1; ///< -1 = not yet resolved

/// The classic button click ( CLICKSID in the dialog XML: 90 of the 96
/// buttons that have one use 8 ).
const short kClickSound = 8;

/// UI2 buttons click like the classic ones. One listener on the context sees
/// every click ( they bubble to the root ): a .ui-btn, or any custom button
/// marked .ui-click, plays the sound. .ui-close is left out on purpose -- the
/// window's own close sound plays instead ( RoseUi2::PlayWindowSound ).
class ClickSoundListener: public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& ev) override {
        for (Rml::Element* pEl = ev.GetTargetElement(); pEl != NULL; pEl = pEl->GetParentNode()) {
            if (pEl->IsClassSet("ui-static")) /// styled as a button, is not one
                return;
            if (pEl->IsClassSet("ui-btn") || pEl->IsClassSet("ui-click")) {
                if (g_pSoundLIST != NULL)
                    g_pSoundLIST->IDX_PlaySound(kClickSound);
                return;
            }
            if (pEl->GetOwnerDocument() == pEl)
                return;
        }
    }
};
ClickSoundListener g_ClickSound;

/// True between a left-press on a panel and its release. Needed because a panel
/// drag continues after the cursor leaves the panel: without it, the move and
/// release events stop being forwarded mid-drag and the handle latches on.
bool g_bDragging = false;

/// Assets live loose under the launch dir. The VFS-vs-loose decision for
/// shipping .rml/.rcss is Phase 1 work ( see doc/rmlui-evaluation.md ); the
/// spike deliberately uses loose files so iteration needs no rebake.
const char* kAssetDir = "3ddata/rmlui/";

bool
ResolveEnabled() {
    if (g_iEnabled >= 0)
        return g_iEnabled != 0;

    g_iEnabled = 0;

    /// UI2 ( RoseUi2.h ) is built on this overlay, so asking for UI2 also
    /// switches RmlUi on -- a player picking the new interface should not have
    /// to know about a second key.
    const char* kEnvKeys[] = {"ROSE_RMLUI", "ROSE_UI2"};
    const char* kIniKeys[] = {"RMLUI", "UI2"};
    for (int i = 0; i < 2 && !g_iEnabled; ++i) {
        const char* pEnv = getenv(kEnvKeys[i]);
        if (pEnv != NULL && *pEnv != '0') {
            g_iEnabled = 1;
        } else {
            char szBuf[16] = {0};
            /// Same INI the D3D9EX A/B switch uses.
            GetPrivateProfileStringA("VIDEO", kIniKeys[i], "0", szBuf, sizeof(szBuf),
                ".\\rose-next.ini");
            if (szBuf[0] != '\0' && szBuf[0] != '0')
                g_iEnabled = 1;
        }
    }

    /// Always say which path is live: a toggle that silently does nothing
    /// produces a false negative, which cost a debugging round on the D3D9Ex
    /// work.
    LOG_INFO("[rmlui] spike overlay {}", g_iEnabled ? "ENABLED" : "disabled");
    return g_iEnabled != 0;
}

/// Whether the cursor is over an actual panel, i.e. whether an input event
/// should be consumed instead of reaching the game world.
///
/// Deliberately NOT based on Context::GetHoverElement(). Each document's <body>
/// spans the whole viewport on purpose -- ElementHandle clamps a panel drag to
/// the move target's containing block, so a collapsed body makes panels nearly
/// immovable -- which means the hover element is a screen-wide hit target
/// whenever a document is open. Filtering that by tag name proved unreliable
/// and cost a round of "the game accepts no input at all".
///
/// Instead this tests the cursor against the geometry of each visible panel.
/// The contract is ours to keep: **a panel is a direct child of <body>**, so
/// every top-level child of every visible document is treated as solid UI and
/// everything else is transparent to the game. Nested content does not need
/// checking because it is inside its panel's box by construction.
bool
IsPointOverPanel(int x, int y) {
    if (g_pContext == NULL)
        return false;

    const float fx = (float)x;
    const float fy = (float)y;

    const int iDocs = g_pContext->GetNumDocuments();
    for (int i = 0; i < iDocs; ++i) {
        Rml::ElementDocument* pDoc = g_pContext->GetDocument(i);
        if (pDoc == NULL || !pDoc->IsVisible())
            continue;

        const int iChildren = pDoc->GetNumChildren();
        for (int c = 0; c < iChildren; ++c) {
            Rml::Element* pPanel = pDoc->GetChild(c);
            if (pPanel == NULL || !pPanel->IsVisible())
                continue;

            const Rml::Vector2f offset = pPanel->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = pPanel->GetBox().GetSize(Rml::BoxArea::Border);
            if (size.x <= 0.0f || size.y <= 0.0f)
                continue;

            if (fx >= offset.x && fy >= offset.y && fx < offset.x + size.x
                && fy < offset.y + size.y)
                return true;
        }
    }

    return false;
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
    g_pContext->AddEventListener("click", &g_ClickSound);

    /// Fonts: the client's own UI font is Verdana ( CStringManager::
    /// GetFontNameByCharSet is hardcoded to it ), so load that from the system
    /// font directory to match the rest of the HUD. The bundled Lato face is the
    /// fallback for machines where it is missing.
    bool bFont = false;
    char szWinDir[MAX_PATH] = {0};
    if (GetWindowsDirectoryA(szWinDir, MAX_PATH) > 0) {
        const std::string strFonts = std::string(szWinDir) + "\\Fonts\\";
        bFont = Rml::LoadFontFace((strFonts + "verdana.ttf").c_str());
        /// Bold is a separate face; without it font-weight: bold silently
        /// synthesises nothing and headers look identical to body text.
        Rml::LoadFontFace((strFonts + "verdanab.ttf").c_str());
    }
    if (!bFont) {
        const std::string strFallback = std::string(kAssetDir) + "fonts/LatoLatin-Regular.ttf";
        if (!Rml::LoadFontFace(strFallback.c_str()))
            LOG_WARN("[rmlui] no font face could be loaded; text will not render");
        else
            LOG_WARN("[rmlui] Verdana unavailable, using bundled Lato fallback");
    }

    g_DamageMeter.Initialise(g_pContext, kAssetDir);
    g_StatusPanel.Initialise(g_pContext, kAssetDir);
    g_BuffBar.Initialise(g_pContext, kAssetDir);
    g_BuffBar.SetAnchor(g_StatusPanel.GetPanel());
    g_TargetFrame.Initialise(g_pContext, kAssetDir);
    g_InterfacePanel.Initialise(g_pContext, kAssetDir);
    g_SkillBar.Initialise(g_pContext, kAssetDir);
    g_SkillWindow.Initialise(g_pContext, kAssetDir);
    g_CharacterWindow.Initialise(g_pContext, kAssetDir);
    g_PartyFrames.Initialise(g_pContext, kAssetDir);
    g_PartyOptions.Initialise(g_pContext, kAssetDir);
    g_PartyOptions.SetAnchor(g_PartyFrames.GetPanel());
    g_PartyInvite.Initialise(g_pContext, kAssetDir);
    g_Inventory.Initialise(g_pContext, kAssetDir);
    g_QuestJournal.Initialise(g_pContext, kAssetDir);
    g_MessageBox.Initialise(g_pContext, kAssetDir);

    /// After every document is loaded: the lock walks their <handle>s.
    RoseRmlLayout::Initialise(g_pContext);

    g_bInitialised = true;
    return true;
}

void
Shutdown() {
    if (!g_bInitialised)
        return;

    g_DamageMeter.Shutdown();
    g_StatusPanel.Shutdown();
    g_BuffBar.Shutdown();
    g_TargetFrame.Shutdown();
    g_InterfacePanel.Shutdown();
    g_SkillBar.Shutdown();
    g_SkillWindow.Shutdown();
    g_CharacterWindow.Shutdown();
    g_PartyFrames.Shutdown();
    g_PartyOptions.Shutdown();
    g_PartyInvite.Shutdown();
    g_Inventory.Shutdown();
    g_QuestJournal.Shutdown();
    g_MessageBox.Shutdown();
    RoseRmlLayout::Shutdown();
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

    /// Reaching this means an unhooked resetScreen() ran -- e.g. the video
    /// options apply path ( coptiondlg.cpp ). That is expected and handled; the
    /// line is here so such paths stay visible rather than silently working.
    LOG_INFO("[rmlui] device changed without a hook, rebuilding overlay resources");

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
    g_DamageMeter.Update();
    g_StatusPanel.Update();
    g_BuffBar.Update();
    g_TargetFrame.Update();
    g_InterfacePanel.Update();
    g_SkillBar.Update();
    g_SkillWindow.Update();
    g_CharacterWindow.Update();
    g_PartyFrames.Update();
    g_PartyOptions.Update();
    g_PartyInvite.Update();
    g_Inventory.Update();
    g_QuestJournal.Update();
    g_MessageBox.Update();
    g_pContext->Update();
}

short
SkillBarSlotAt(int x, int y, int iDlgType) {
    return g_bInitialised ? g_SkillBar.SlotAt(x, y, iDlgType) : -1;
}

void
SetWindowOpen(int iDlgType, bool bOpen) {
    if (!g_bInitialised)
        return;
    switch (iDlgType) {
        case DLG_TYPE_SKILL:
            g_SkillWindow.SetOpen(bOpen);
            break;
        case DLG_TYPE_CHAR:
            g_CharacterWindow.SetOpen(bOpen);
            break;
        case DLG_TYPE_PARTYOPTION:
            g_PartyOptions.SetOpen(bOpen);
            break;
        case DLG_TYPE_ITEM:
            g_Inventory.SetOpen(bOpen);
            break;
        case DLG_TYPE_QUEST:
            g_QuestJournal.SetOpen(bOpen);
            break;
        default:
            break;
    }
}

bool
IsWindowOpen(int iDlgType) {
    if (!g_bInitialised)
        return false;
    switch (iDlgType) {
        case DLG_TYPE_SKILL:
            return g_SkillWindow.IsOpen();
        case DLG_TYPE_CHAR:
            return g_CharacterWindow.IsOpen();
        case DLG_TYPE_PARTYOPTION:
            return g_PartyOptions.IsOpen();
        case DLG_TYPE_ITEM:
            return g_Inventory.IsOpen();
        case DLG_TYPE_QUEST:
            return g_QuestJournal.IsOpen();
        default:
            return false;
    }
}

bool
ShowPartyInvite(unsigned short wFromObjSvrIdx, const char* pszFrom, bool bMake) {
    return g_bInitialised && g_PartyInvite.Show(wFromObjSvrIdx, pszFrom, bMake);
}

bool
IsPartyInvitePending() {
    return g_bInitialised && g_PartyInvite.IsPending();
}

bool
ConfirmBox(const char* pszTitle,
    const char* pszText,
    const char* pszOk,
    const char* pszCancel,
    CTCommand* pOk,
    CTCommand* pCancel) {
    return g_bInitialised && g_MessageBox.Confirm(pszTitle, pszText, pszOk, pszCancel, pOk, pCancel);
}

bool
NoticeBox(const char* pszTitle, const char* pszText) {
    return g_bInitialised && g_MessageBox.Notice(pszTitle, pszText);
}

bool
InventoryBagAt(int x, int y) {
    return g_bInitialised && g_Inventory.BagAt(x, y);
}

bool
InventoryBagSlotAt(int x, int y, int& iPage, int& iSlot) {
    return g_bInitialised && g_Inventory.BagSlotAt(x, y, iPage, iSlot);
}

bool
InventoryEquipAt(int x, int y) {
    return g_bInitialised && g_Inventory.EquipAt(x, y);
}

int
InventoryEquipSlotAt(int x, int y) {
    return g_bInitialised ? g_Inventory.EquipSlotAt(x, y) : -1;
}

bool
InventoryCostumeOpen() {
    return g_bInitialised && g_Inventory.CostumeOpen();
}

void
SetSkillBarVertical(bool bVertical) {
    if (g_bInitialised)
        g_SkillBar.SetVertical(bVertical);
}

bool
IsSkillBarVertical() {
    return g_bInitialised && g_SkillBar.IsVertical();
}

void
ToggleInterfacePanel() {
    if (g_bInitialised)
        g_InterfacePanel.Toggle();
}

bool
IsInitialised() {
    return g_bInitialised;
}

int
ReloadStyleSheets() {
    if (!g_bInitialised || g_pContext == NULL)
        return 0;

    int iCount = 0;
    for (int i = 0; i < g_pContext->GetNumDocuments(); ++i) {
        Rml::ElementDocument* pDoc = g_pContext->GetDocument(i);
        if (pDoc == NULL)
            continue;

        /// The debugger's documents are built in memory and have no file to
        /// re-read; only the ones loaded from an .rml on disk are reloadable.
        const Rml::String& strUrl = pDoc->GetSourceURL();
        if (strUrl.size() < 4 || strUrl.compare(strUrl.size() - 4, 4, ".rml") != 0)
            continue;

        /// Clears the stylesheet cache and re-parses every <link>ed sheet, so
        /// an edit to the shared theme reaches every panel at once.
        pDoc->ReloadStyleSheet();
        ++iCount;
    }

    LOG_INFO("[rmlui] reloaded stylesheets of {} document(s)", iCount);
    return iCount;
}

void
ToggleDamageMeter() {
    if (g_bInitialised)
        g_DamageMeter.Toggle();
}

bool
IsDamageMeterVisible() {
    return g_bInitialised && g_DamageMeter.IsVisible();
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

    /// Logged so the hooked path is visible too. Without this the common case is
    /// silent and only the fallback announces itself, which reads as "the
    /// fallback never runs" when the truth is "the hook got there first".
    LOG_INFO("[rmlui] device rebuilt via hook ({}x{})", iWidth, iHeight);
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

/// The legacy drop-target type declared by the element under the point, or 0
/// ( a UI2 panel that accepts nothing ). A row of the skill bar declares
/// drop-target="8" ( DLG_TYPE_QUICKBAR ): a drop there is a drop on that
/// legacy dialog, whose command then asks CQuickBAR::GetMouseClickSlot.
static int
DropTargetAt(int x, int y) {
    for (Rml::Element* pEl = g_pContext->GetElementAtPoint(Rml::Vector2f((float)x, (float)y));
         pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("drop-target"))
            return pEl->GetAttribute<int>("drop-target", 0);
    }
    return 0;
}

/// A left release while the LEGACY drag-and-drop system carries an icon.
/// Without this, a release over any RmlUi panel was eaten here and never
/// reached CITStateNormal, so DragEnd never ran and the icon stayed glued to
/// the cursor.
///   - over a UI2 drop target: end the legacy drag there ( its command runs );
///   - over any other UI2 panel: cancel it -- nothing happens, as if dropped
///     back where it came from ( NOT the ground, which removes / drops items );
///   - elsewhere: hand the release to the legacy chain untouched, which drops
///     on the ground or on a legacy dialog as it always did.
static bool
ReleaseLegacyDrag(int x, int y) {
    const bool bOverPanel = IsPointOverPanel(x, y);

    /// RmlUi still needs the release to close whatever press it tracked.
    if (g_bDragging || bOverPanel)
        g_pContext->ProcessMouseButtonUp(0, 0);
    g_bDragging = false;

    if (!bOverPanel)
        return false;

    /// The legacy chain resets this on every release; it is skipped here.
    CWinCtrl::SetMouseExclusiveCtrl(NULL);

    const int iTarget = DropTargetAt(x, y);
    if (iTarget > 0)
        CDragNDropMgr::GetInstance().DragEnd(iTarget);
    else
        CDragNDropMgr::GetInstance().DragCancel();
    return true;
}

bool
ProcessWndMsg(HWND hWnd, UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    if (!g_bInitialised || g_pContext == NULL)
        return false;

    const int x = (short)LOWORD(lParam);
    const int y = (short)HIWORD(lParam);

    switch (uiMsg) {
        case WM_MOUSEMOVE: {
            /// RmlUi always sees the move -- an in-progress panel drag has to
            /// keep tracking once the cursor leaves the panel. Only the consume
            /// decision depends on where the cursor is.
            g_pContext->ProcessMouseMove(x, y, 0);
            return g_bDragging || IsPointOverPanel(x, y);
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const int iButton = MouseButtonFromMsg(uiMsg);
            if (iButton < 0)
                return false;
            if (!IsPointOverPanel(x, y))
                return false; /// let the world have it; RmlUi gets no phantom press
            if (iButton == 0)
                g_bDragging = true;
            g_pContext->ProcessMouseButtonDown(iButton, 0);
            return true;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            const int iButton = MouseButtonFromMsg(uiMsg);
            if (iButton < 0)
                return false;
            if (iButton == 0 && CDragNDropMgr::GetInstance().IsDraging())
                return ReleaseLegacyDrag(x, y);
            /// A release that ends a panel drag must reach RmlUi even if the
            /// cursor has left the panel, or the handle keeps dragging forever.
            const bool bConsume = g_bDragging || IsPointOverPanel(x, y);
            if (iButton == 0)
                g_bDragging = false;
            if (!bConsume)
                return false;
            g_pContext->ProcessMouseButtonUp(iButton, 0);
            return true;
        }
        case WM_MOUSEWHEEL: {
            /// Wheel coordinates are screen-space, unlike every other mouse
            /// message here, so convert before hit-testing -- otherwise camera
            /// zoom breaks in a way that depends on where the window sits.
            POINT pt = {x, y};
            ::ScreenToClient(hWnd, &pt);
            if (!IsPointOverPanel(pt.x, pt.y))
                return false;
            const float fDelta = -(float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
            g_pContext->ProcessMouseWheel(fDelta, 0);
            return true;
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
