#include "stdafx.h"

#include "RoseRmlSkillBar.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CToolTipMgr.h"
#include "../interface/Dlgs/QuickToolBar.h"
#include "../interface/Icon/CIcon.h"
#include "../interface/SlotContainer/HotIconSlot.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>
#include <stdarg.h>

namespace {

Rml::String
Printf(const char* pszFormat, ...) {
    char szBuf[32];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf(szBuf, sizeof(szBuf), pszFormat, args);
    va_end(args);
    szBuf[sizeof(szBuf) - 1] = '\0';
    return szBuf;
}

/// Seconds left, as the curtain's label. Nothing under a second: the global
/// casting delay alone would otherwise flash a "1" on every skill.
Rml::String
CooldownText(int iRemainMs) {
    if (iRemainMs < 1000)
        return Rml::String();
    const int iSec = (iRemainMs + 999) / 1000;
    if (iSec < 100)
        return Printf("%d", iSec);
    return Printf("%dm", (iSec + 59) / 60);
}

/// Gap between the bar and the screen edge at its default spot, dp.
const float kEdgeGap = 6.0f;

const char* kIniPath = ".\\rose-next.ini";

} // namespace

RoseRmlSkillBar::RoseRmlSkillBar():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false),
    m_iPressSlot(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iMainPage(1),
    m_iExtPage(1),
    m_iMainType(DLG_TYPE_QUICKBAR),
    m_iExtType(DLG_TYPE_QUICKBAR_EXT),
    m_bVertical(false) {
    /// Eight empty slots per row from the start, so the document lays out at
    /// its real size before the first sample.
    for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i) {
        SlotVM vm;
        vm.index = -1;
        vm.filled = false;
        vm.off = false;
        vm.cd = 0.0f;
        vm.count = 0;
        m_Main.push_back(vm);
        m_Ext.push_back(vm);
    }
}

CQuickBAR*
RoseRmlSkillBar::BarForSlot(int iSlot) {
    if (iSlot < 0 || iSlot >= MAX_HOT_ICONS)
        return NULL;
    /// Pages 0-3 are the main bar's, 4-7 the extension's ( SetQuickBarType ).
    return (iSlot < QUICKBAR_MAX_HOT_ICONS_PAGES * HOT_ICONS_PER_PAGE) ? g_itMGR.GetQuickBAR()
                                                                       : g_itMGR.GetQuickBAR_EXT();
}

CIcon*
RoseRmlSkillBar::IconForSlot(int iSlot) {
    if (g_pAVATAR == NULL || iSlot < 0 || iSlot >= MAX_HOT_ICONS)
        return NULL;
    CHotIconSlot* pHotIcons = g_pAVATAR->GetHotIconSlot();
    return pHotIcons ? pHotIcons->GetHotIcon(iSlot) : NULL;
}

bool
RoseRmlSkillBar::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("skillbar");
    if (!constructor)
        return false;

    if (auto slot = constructor.RegisterStruct<SlotVM>()) {
        slot.RegisterMember("index", &SlotVM::index);
        slot.RegisterMember("filled", &SlotVM::filled);
        slot.RegisterMember("off", &SlotVM::off);
        slot.RegisterMember("src", &SlotVM::src);
        slot.RegisterMember("rect", &SlotVM::rect);
        slot.RegisterMember("cd", &SlotVM::cd);
        slot.RegisterMember("cd_text", &SlotVM::cd_text);
        slot.RegisterMember("count", &SlotVM::count);
        slot.RegisterMember("key", &SlotVM::key);
    }
    constructor.RegisterArray<std::vector<SlotVM>>();

    constructor.Bind("main", &m_Main);
    constructor.Bind("ext", &m_Ext);
    constructor.Bind("main_page", &m_iMainPage);
    constructor.Bind("ext_page", &m_iExtPage);
    constructor.Bind("main_type", &m_iMainType);
    constructor.Bind("ext_type", &m_iExtType);
    constructor.Bind("vertical", &m_bVertical);

    m_bVertical = GetPrivateProfileIntA("VIDEO", "UI_SKILLBAR_VERTICAL", 0, kIniPath) != 0;

    /// A left press on a slot: remember it; Update() turns it into a drag if
    /// the mouse moves before the release.
    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.empty() || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressSlot = args[0].Get<int>();
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
        });

    /// Double-click on a slot, as the classic quickbar ( a single click does
    /// nothing, so a stray click in a fight casts nothing ). The second press
    /// re-arms m_iPressSlot just before RmlUi sends the dblclick; a press
    /// that turned into a drag has cleared it.
    constructor.BindEventCallback("use",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int iSlot = args[0].Get<int>();
            const bool bClick = (iSlot >= 0 && iSlot == m_iPressSlot);
            m_iPressSlot = -1;
            if (!bClick)
                return;
            if (CIcon* pIcon = IconForSlot(iSlot))
                pIcon->ExecuteCommand();
        });

    /// Page arrows: ( row, delta ), row 0 = main, 1 = extension.
    constructor.BindEventCallback("page",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.size() < 2)
                return;
            CQuickBAR* pBar = (args[0].Get<int>() == 0) ? g_itMGR.GetQuickBAR()
                                                        : g_itMGR.GetQuickBAR_EXT();
            if (pBar)
                pBar->ChangePage(args[1].Get<int>());
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "skillbar.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load skill bar document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("skillbar");
    RoseRmlLayout::Track(m_pPanel, "skillbar");

    LOG_INFO("[rmlui] skill bar document loaded");
    return true;
}

void
RoseRmlSkillBar::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
}

void
RoseRmlSkillBar::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_iPressSlot = -1;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlSkillBar::SampleRow(CQuickBAR* pBar, bool bCtrl, std::vector<SlotVM>& out, int& iPage) {
    out.clear();
    const int iCurrent = pBar ? pBar->GetCurrentPage() : 0;
    iPage = pBar ? (iCurrent - pBar->GetStartPage() + 1) : 1;

    for (int i = 0; i < HOT_ICONS_PER_PAGE; ++i) {
        SlotVM vm;
        vm.index = iCurrent * HOT_ICONS_PER_PAGE + i;
        vm.filled = false;
        vm.off = false;
        vm.cd = 0.0f;
        vm.count = 0;
        vm.key = bCtrl ? Printf("^F%d", i + 1) : Printf("F%d", i + 1);

        CIcon* pIcon = pBar ? IconForSlot(vm.index) : NULL;
        int iModule = 0, iGraphic = 0;
        if (pIcon != NULL && pIcon->GetSprite(iModule, iGraphic)
            && RoseRmlIcons::Resolve(iModule, iGraphic, vm.src, vm.rect)) {
            vm.filled = true;
            vm.off = !pIcon->IsEnable();
            vm.count = pIcon->GetStackCount();

            int iRemainMs = 0;
            const float fRate = pIcon->GetCooldown(&iRemainMs);
            /// Whole percent: the curtain moves smoothly enough and the view
            /// is not dirtied by sub-pixel changes every frame.
            vm.cd = (float)(int)(fRate * 100.0f + 0.5f);
            vm.cd_text = (vm.cd > 0.0f) ? CooldownText(iRemainMs) : Rml::String();
        }
        out.push_back(vm);
    }
}

void
RoseRmlSkillBar::Sample() {
    std::vector<SlotVM> main, ext;
    int iMainPage = 1, iExtPage = 1;
    SampleRow(g_itMGR.GetQuickBAR(), false, main, iMainPage);
    SampleRow(g_itMGR.GetQuickBAR_EXT(), true, ext, iExtPage);

    if (main != m_Main) {
        m_Main.swap(main);
        m_Model.DirtyVariable("main");
    }
    if (ext != m_Ext) {
        m_Ext.swap(ext);
        m_Model.DirtyVariable("ext");
    }
    if (iMainPage != m_iMainPage) {
        m_iMainPage = iMainPage;
        m_Model.DirtyVariable("main_page");
    }
    if (iExtPage != m_iExtPage) {
        m_iExtPage = iExtPage;
        m_Model.DirtyVariable("ext_page");
    }
}

void
RoseRmlSkillBar::PlaceDefault() {
    /// Only while no position is set inline: a saved or dragged spot, or a
    /// clamp, writes left/top; Reset in the Interface window removes them.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;

    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return; /// not laid out yet

    /// Horizontal: centred at the bottom of the screen. Vertical: centred on
    /// the right edge. A stylesheet cannot say either without margins that
    /// would then fight every saved position.
    /// Whole pixels: centring an odd width lands on x.5, and a panel on a half
    /// pixel samples every icon between two texels ( visibly blurred ).
    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fGap = kEdgeGap * RoseRmlLayout::GetScaleRatio();
    float x, y;
    if (m_bVertical) {
        x = floorf((float)view.x - size.x - fGap);
        y = floorf(((float)view.y - size.y) * 0.5f);
    } else {
        x = floorf(((float)view.x - size.x) * 0.5f);
        y = floorf((float)view.y - size.y - fGap);
    }
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
}

void
RoseRmlSkillBar::UpdateDragStart() {
    if (m_iPressSlot < 0)
        return;

    /// The release already arrived ( as a click, or somewhere else ): no drag.
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_iPressSlot = -1;
        return;
    }

    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);

    /// The legacy CSlot's threshold ( 1/200 of the screen ), at least 3 px.
    const int iSlopX = max(3, g_pCApp->GetWIDTH() / 200);
    const int iSlopY = max(3, g_pCApp->GetHEIGHT() / 200);
    if (abs(ptMouse.x - m_iPressX) < iSlopX && abs(ptMouse.y - m_iPressY) < iSlopY)
        return;

    const int iSlot = m_iPressSlot;
    m_iPressSlot = -1; /// whatever happens, the release is no longer a click

    CIcon* pIcon = IconForSlot(iSlot);
    CQuickBAR* pBar = BarForSlot(iSlot);
    if (pIcon == NULL || pBar == NULL || !pIcon->IsEnable())
        return;

    /// Exactly what the legacy slot does ( CSlot::Update ): the bar's own drag
    /// item, carrying a clone of the quick icon.
    CDragItem* pDragItem = pBar->GetDragItem();
    if (pDragItem == NULL)
        return;
    pDragItem->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDragItem);
}

void
RoseRmlSkillBar::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    /// The slot under the mouse, if any.
    int iSlot = -1;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("slot")) {
            iSlot = pEl->GetAttribute<int>("slot", -1);
            break;
        }
    }

    CIcon* pIcon = IconForSlot(iSlot);
    CQuickBAR* pBar = BarForSlot(iSlot);
    if (pIcon == NULL || pBar == NULL)
        return;

    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip, pBar->GetDialogType(), 0);
    if (ToolTip.IsEmpty())
        return;

    /// Outside the bar, so it is never drawn under it ( legacy tooltips draw
    /// before the RmlUi pass ): above a horizontal bar ( below it at the top
    /// of the screen ), beside a vertical one ( left of it at the right edge ).
    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const Rml::Vector2f panelPos = m_pPanel->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f panelSize = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    const int iScreenW = g_pCApp->GetWIDTH();
    const int iScreenH = g_pCApp->GetHEIGHT();

    POINT pt;
    if (m_bVertical) {
        /// Side from where the bar is, not from this tooltip's width.
        const bool bRightSide = (panelPos.x + panelSize.x * 0.5f) < (float)iScreenW * 0.5f;
        pt.x = bRightSide ? (int)(panelPos.x + panelSize.x) + 4
                          : (int)panelPos.x - ToolTip.GetWidth() - 4;
        pt.y = ptMouse.y - ToolTip.GetHeight() / 2;
    } else {
        pt.x = ptMouse.x + 12;
        pt.y = (int)panelPos.y - ToolTip.GetHeight() - 4;
        if (pt.y < 0)
            pt.y = (int)(panelPos.y + panelSize.y) + 4;
    }
    if (pt.x > iScreenW - ToolTip.GetWidth())
        pt.x = iScreenW - ToolTip.GetWidth();
    if (pt.x < 0)
        pt.x = 0;
    if (pt.y > iScreenH - ToolTip.GetHeight())
        pt.y = iScreenH - ToolTip.GetHeight();
    if (pt.y < 0)
        pt.y = 0;

    ToolTip.SetPosition(pt);
    CToolTipMgr::GetInstance().RegistInfo(ToolTip);
}

void
RoseRmlSkillBar::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN
        && g_itMGR.GetQuickBAR() != NULL;
    SetVisible(bInWorld);
    if (!m_bVisible)
        return;

    Sample();
    PlaceDefault();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateDragStart();
    UpdateTooltip();
}

short
RoseRmlSkillBar::SlotAt(int x, int y, int iDlgType) {
    if (!m_bVisible || m_pContext == NULL)
        return -1;

    int iSlot = -1;
    for (Rml::Element* pEl = m_pContext->GetElementAtPoint(Rml::Vector2f((float)x, (float)y));
         pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (iSlot < 0 && pEl->HasAttribute("slot"))
            iSlot = pEl->GetAttribute<int>("slot", -1);
        if (pEl->HasAttribute("drop-target"))
            return (pEl->GetAttribute<int>("drop-target", 0) == iDlgType) ? (short)iSlot : -1;
    }
    return -1;
}

void
RoseRmlSkillBar::SetVertical(bool bVertical) {
    if (bVertical == m_bVertical)
        return;
    m_bVertical = bVertical;
    m_Model.DirtyVariable("vertical");
    WritePrivateProfileStringA("VIDEO", "UI_SKILLBAR_VERTICAL", bVertical ? "1" : "0", kIniPath);
    /// The bar keeps its top-left corner; Clamp pulls it back if the new
    /// shape runs off screen. A bar still at its default spot moves to the
    /// new default ( PlaceDefault only runs while no position is set ).
}
