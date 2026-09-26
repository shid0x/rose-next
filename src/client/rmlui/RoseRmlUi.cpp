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
#include "RoseRmlMinimap.h"
#include "RoseRmlConversation.h"
#include "RoseRmlShop.h"
#include "RoseRmlNumberInput.h"
#include "RoseRmlTrade.h"
#include "RoseRmlTradeInvite.h"
#include "RoseRmlSystem.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include "rose/common/log.h"

#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CToolTipMgr.h"
#include "../System/CGame.h"
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
RoseRmlMinimap g_Minimap; ///< UI2: replaces CMinimapDLG ( a view over it )
RoseRmlConversation g_Conversation; ///< UI2: replaces the three conversation dialogs
RoseRmlShop g_Shop; ///< UI2: replaces CStoreDLG + CDealDLG ( a view over them )
RoseRmlNumberInput g_NumberInput; ///< UI2: replaces CNumberInputDlg
RoseRmlTrade g_Trade; ///< UI2: replaces CExchangeDLG ( a view over it )
RoseRmlTradeInvite g_TradeInvite; ///< UI2: replaces the trade request message box
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
                /// click-sid: the control's own classic sound, where it had
                /// another ( the store's tabs are radio buttons, which play 1 ).
                const int iSound = pEl->GetAttribute<int>("click-sid", kClickSound);
                if (g_pSoundLIST != NULL && iSound > 0)
                    g_pSoundLIST->IDX_PlaySound((short)iSound);
                return;
            }
            if (pEl->GetOwnerDocument() == pEl)
                return;
        }
    }
};
ClickSoundListener g_ClickSound;

/// --- slow-frame report ---------------------------------------------------------
/// When the UI's share of one frame ( every panel's Update, RmlUi's own update
/// -- data bindings, styles, layout -- and the render, which also decodes queued
/// textures ) passes kSlowUiMs, one log line says where it went. First opens
/// are the usual suspects: a document laid out and its data-for rows built for
/// the first time. At most one line a second.
const double kSlowUiMs = 8.0;

struct UiSlice {
    const char* pszName;
    double fMs;
};
UiSlice g_UiSlices[32];
int g_iUiSlices = 0;
DWORD g_dwLastSlowLog = 0;

double
NowMs() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}

void
AddSlice(const char* pszName, double fMs) {
    if (g_iUiSlices < (int)(sizeof(g_UiSlices) / sizeof(g_UiSlices[0]))) {
        g_UiSlices[g_iUiSlices].pszName = pszName;
        g_UiSlices[g_iUiSlices].fMs = fMs;
        ++g_iUiSlices;
    }
}

/// Times one statement into the report.
#define UI_TIMED(name, stmt)                   \
    do {                                       \
        const double fStart_ = NowMs();        \
        stmt;                                  \
        AddSlice(name, NowMs() - fStart_);     \
    } while (0)

void
ReportSlowUiFrame() {
    double fTotal = 0.0;
    for (int i = 0; i < g_iUiSlices; ++i)
        fTotal += g_UiSlices[i].fMs;

    if (fTotal >= kSlowUiMs && GetTickCount() - g_dwLastSlowLog >= 1000) {
        g_dwLastSlowLog = GetTickCount();
        std::string strParts;
        for (int i = 0; i < g_iUiSlices; ++i) {
            if (g_UiSlices[i].fMs < 0.5)
                continue;
            char szPart[64];
            _snprintf(szPart, sizeof(szPart), " %s=%.1f", g_UiSlices[i].pszName, g_UiSlices[i].fMs);
            szPart[sizeof(szPart) - 1] = '\0';
            strParts += szPart;
        }
        /// The renderer's share of the render: what it had to build.
        const RoseRmlRenderer::WorkStats work = g_pRenderer ? g_pRenderer->TakeWorkStats()
                                                            : RoseRmlRenderer::WorkStats();
        LOG_INFO("[rmlui] slow UI frame: {:.1f} ms:{} | built: {} geometry {:.1f} ms, {} textures "
                 "{:.1f} ms, {} gradients {:.1f} ms",
            fTotal, strParts.c_str(), work.iGeometries, work.fGeometryMs, work.iGenerated,
            work.fGeneratedMs, work.iShaders, work.fShaderMs);
    }
    if (g_pRenderer)
        g_pRenderer->TakeWorkStats(); /// per frame
    g_iUiSlices = 0;
}

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
    g_Minimap.Initialise(g_pContext, kAssetDir);
    g_Conversation.Initialise(g_pContext, kAssetDir);
    g_Shop.Initialise(g_pContext, kAssetDir);
    g_Trade.Initialise(g_pContext, kAssetDir);
    g_TradeInvite.Initialise(g_pContext, kAssetDir);
    /// Late, so they stack over the windows that ask them.
    g_NumberInput.Initialise(g_pContext, kAssetDir);
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
    g_Minimap.Shutdown();
    g_Conversation.Shutdown();
    g_Shop.Shutdown();
    g_Trade.Shutdown();
    g_TradeInvite.Shutdown();
    g_NumberInput.Shutdown();
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
    g_iUiSlices = 0;
    UI_TIMED("meter", g_DamageMeter.Update());
    UI_TIMED("status", g_StatusPanel.Update());
    UI_TIMED("buffs", g_BuffBar.Update());
    UI_TIMED("target", g_TargetFrame.Update());
    UI_TIMED("iface", g_InterfacePanel.Update());
    UI_TIMED("skillbar", g_SkillBar.Update());
    UI_TIMED("skills", g_SkillWindow.Update());
    UI_TIMED("char", g_CharacterWindow.Update());
    UI_TIMED("party", g_PartyFrames.Update());
    UI_TIMED("partyopt", g_PartyOptions.Update());
    UI_TIMED("invite", g_PartyInvite.Update());
    UI_TIMED("inventory", g_Inventory.Update());
    UI_TIMED("quests", g_QuestJournal.Update());
    UI_TIMED("minimap", g_Minimap.Update());
    UI_TIMED("talk", g_Conversation.Update());
    UI_TIMED("shop", g_Shop.Update());
    UI_TIMED("trade", g_Trade.Update());
    UI_TIMED("tradeinvite", g_TradeInvite.Update());
    UI_TIMED("numinput", g_NumberInput.Update());
    UI_TIMED("msgbox", g_MessageBox.Update());
    /// Data bindings, styles and layout of every document.
    UI_TIMED("rml-update", g_pContext->Update());
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
        case DLG_TYPE_MINIMAP:
            g_Minimap.SetOpen(bOpen);
            break;
        case DLG_TYPE_DIALOG:
            g_Conversation.SetOpen(RoseRmlConversation::MODE_NPC, bOpen);
            break;
        case DLG_TYPE_SELECTEVENT:
            g_Conversation.SetOpen(RoseRmlConversation::MODE_SELECT, bOpen);
            break;
        case DLG_TYPE_EVENTDIALOG:
            g_Conversation.SetOpen(RoseRmlConversation::MODE_EVENT, bOpen);
            break;
        case DLG_TYPE_STORE:
        case DLG_TYPE_DEAL:
            g_Shop.SetOpen(bOpen);
            break;
        case DLG_TYPE_N_INPUT:
            g_NumberInput.SetOpen(bOpen);
            break;
        case DLG_TYPE_EXCHANGE:
            g_Trade.SetOpen(bOpen);
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
        case DLG_TYPE_MINIMAP:
            return g_Minimap.IsOpen();
        case DLG_TYPE_DIALOG:
            return g_Conversation.IsOpen(RoseRmlConversation::MODE_NPC);
        case DLG_TYPE_SELECTEVENT:
            return g_Conversation.IsOpen(RoseRmlConversation::MODE_SELECT);
        case DLG_TYPE_EVENTDIALOG:
            return g_Conversation.IsOpen(RoseRmlConversation::MODE_EVENT);
        case DLG_TYPE_STORE:
        case DLG_TYPE_DEAL:
            return g_Shop.IsOpen();
        case DLG_TYPE_N_INPUT:
            return g_NumberInput.IsOpen();
        case DLG_TYPE_EXCHANGE:
            return g_Trade.IsOpen();
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
ShowTradeInvite(unsigned short wFromObjSvrIdx, const char* pszFrom) {
    return g_bInitialised && g_TradeInvite.Show(wFromObjSvrIdx, pszFrom);
}

bool
IsTradeInvitePending() {
    return g_bInitialised && g_TradeInvite.IsPending();
}

bool
TradeOfferChangedWhileReady() {
    return g_bInitialised && g_Trade.OfferChangedWhileReady();
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
MinimapToggleCollapsed() {
    if (g_bInitialised)
        g_Minimap.ToggleCollapsed();
}

void
MinimapCycleSize() {
    if (g_bInitialised)
        g_Minimap.CycleSize();
}

void
ConversationBegin(int iMode, const char* pszText, int iOwnerClientIdx) {
    if (g_bInitialised)
        g_Conversation.Begin(iMode, pszText, iOwnerClientIdx);
}

void
ConversationAddAnswer(const char* pszText, int iEventID, void (*fpHandler)(int iEventID)) {
    if (g_bInitialised)
        g_Conversation.AddAnswer(pszText, iEventID, fpHandler);
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

    /// BeginFrame also decodes queued textures ( within its own budget ).
    UI_TIMED("rml-begin", g_pRenderer->BeginFrame());
    UI_TIMED("rml-render", g_pContext->Render());
    g_pRenderer->EndFrame();
    ReportSlowUiFrame();
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
        case WM_KEYDOWN:
        case WM_CHAR:
            /// Only the UI2 quantity question takes keys ( digits, Enter,
            /// Escape ), and only while it is up; everything else is the game's.
            return g_NumberInput.ProcessKey(uiMsg, wParam);
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

void
PlaceTooltipAtCursor(CInfo& ToolTip) {
    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const int iScreenW = g_pCApp->GetWIDTH();
    const int iScreenH = g_pCApp->GetHEIGHT();
    const int iWidth = ToolTip.GetWidth();
    const int iHeight = ToolTip.GetHeight();

    /// CSlot::Update's spot: 20 px right of the cursor, top at the cursor.
    /// Near the right edge it goes to the cursor's left rather than being
    /// clamped back under it.
    POINT pt;
    pt.x = ptMouse.x + 20;
    if (pt.x + iWidth > iScreenW)
        pt.x = ptMouse.x - iWidth - 8;
    pt.y = ptMouse.y;
    if (pt.y + iHeight > iScreenH)
        pt.y = iScreenH - iHeight;
    if (pt.x < 0)
        pt.x = 0;
    if (pt.y < 0)
        pt.y = 0;

    ToolTip.SetPosition(pt);
    CToolTipMgr::GetInstance().RegistInfo(ToolTip);
}

int
GetDrawCallCount() {
    return (g_pRenderer != NULL) ? g_pRenderer->GetDrawCallCount() : 0;
}

} // namespace RoseRmlUi
