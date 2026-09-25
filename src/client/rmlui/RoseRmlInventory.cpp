#include "stdafx.h"

#include "RoseRmlInventory.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../misc/gameutil.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CToolTipMgr.h"
#include "../interface/CUIMediator.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Icon/CIconItem.h"
#include "../interface/Dlgs/CItemDlg.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>
#include <stdarg.h>

namespace {

/// The three bag tabs of the items section ( INV_WEAPON, INV_USE, INV_ETC ).
const int kBagTabs = 3;

/// CIconItem::Update's red tint: an item under this much life.
const int kWornLife = 50;

Rml::String
Printf(const char* pszFormat, ...) {
    char szBuf[96];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf(szBuf, sizeof(szBuf), pszFormat, args);
    va_end(args);
    szBuf[sizeof(szBuf) - 1] = '\0';
    return szBuf;
}

/// Walks up from the element under the point to the window's drop-target
/// root, returning the first element carrying pszAttr on the way ( NULL when
/// there is none, or the point is not over this window ).
Rml::Element*
FindUp(Rml::Context* pContext, int x, int y, const char* pszAttr) {
    if (pContext == NULL)
        return NULL;
    for (Rml::Element* pEl = pContext->GetElementAtPoint(Rml::Vector2f((float)x, (float)y));
         pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute(pszAttr))
            return pEl;
        if (pEl->GetId() == "inventory")
            return NULL;
    }
    return NULL;
}

} // namespace

RoseRmlInventory::RoseRmlInventory():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iPressSlot(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iDropType(DLG_TYPE_ITEM),
    m_iPage(INV_WEAPON),
    m_strMoney("0"),
    m_strWeight("0 / 0"),
    m_fWeightPct(0.0f),
    m_iWeightLevel(0) {
    for (int i = 0; i < kBagTabs; ++i)
        m_iCount[i] = 0;
}

bool
RoseRmlInventory::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("inventory");
    if (!constructor)
        return false;

    if (auto cell = constructor.RegisterStruct<CellVM>()) {
        cell.RegisterMember("index", &CellVM::index);
        cell.RegisterMember("filled", &CellVM::filled);
        cell.RegisterMember("src", &CellVM::src);
        cell.RegisterMember("rect", &CellVM::rect);
        cell.RegisterMember("count", &CellVM::count);
        cell.RegisterMember("cd", &CellVM::cd);
        cell.RegisterMember("dim", &CellVM::dim);
        cell.RegisterMember("worn", &CellVM::worn);
        cell.RegisterMember("socket", &CellVM::socket);
        cell.RegisterMember("gem_src", &CellVM::gem_src);
        cell.RegisterMember("gem_rect", &CellVM::gem_rect);
    }
    constructor.RegisterArray<std::vector<CellVM>>();

    constructor.Bind("drop_type", &m_iDropType);
    constructor.Bind("page", &m_iPage);
    constructor.Bind("count0", &m_iCount[0]);
    constructor.Bind("count1", &m_iCount[1]);
    constructor.Bind("count2", &m_iCount[2]);
    constructor.Bind("cells", &m_Cells);
    constructor.Bind("money", &m_strMoney);
    constructor.Bind("weight", &m_strWeight);
    constructor.Bind("weight_pct", &m_fWeightPct);
    constructor.Bind("weight_level", &m_iWeightLevel);

    constructor.BindEventCallback("set_page",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int iPage = args[0].Get<int>();
            if (iPage >= 0 && iPage < kBagTabs && iPage != m_iPage) {
                m_iPage = iPage;
                m_iPressSlot = -1;
                m_Model.DirtyVariable("page");
                Sample();
            }
        });

    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.empty() || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
            OnPress(args[0].Get<int>());
        });

    /// Double-click: use / equip, as CSlot's WM_LBUTTONDBLCLK.
    constructor.BindEventCallback("use",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            m_iPressSlot = -1;
            CSlot* pSlot = BagSlot(args[0].Get<int>());
            if (pSlot && pSlot->GetIcon())
                pSlot->GetIcon()->ExecuteCommand();
        });

    constructor.BindEventCallback("money",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (CItemDlg* pDlg = ItemDlg())
                pDlg->OnMoneyButton();
        });

    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { SetOpen(false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "inventory.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load inventory document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("inventory");
    RoseRmlLayout::Track(m_pPanel, "inventory");

    LOG_INFO("[rmlui] inventory document loaded");
    return true;
}

void
RoseRmlInventory::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CItemDlg*
RoseRmlInventory::ItemDlg() const {
    return (CItemDlg*)g_itMGR.FindDlg(DLG_TYPE_ITEM);
}

int
RoseRmlInventory::CurrentPage() const {
    return m_iPage;
}

CSlot*
RoseRmlInventory::BagSlot(int iSlot) const {
    CItemDlg* pDlg = ItemDlg();
    return pDlg ? pDlg->GetBagSlot(CurrentPage(), iSlot) : NULL;
}

bool
RoseRmlInventory::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlInventory::SetOpen(bool bOpen) {
    m_bOpen = bOpen;
    m_iPressSlot = -1;
    if (bOpen)
        Sample(); /// no stale grid on the first frame
}

void
RoseRmlInventory::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_iPressSlot = -1;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- CItemDlg's questions ---------------------------------------------------

bool
RoseRmlInventory::BagAt(int x, int y) const {
    return m_bVisible && FindUp(m_pContext, x, y, "bag-area") != NULL;
}

bool
RoseRmlInventory::BagSlotAt(int x, int y, int& iPage, int& iSlot) const {
    if (!m_bVisible)
        return false;
    Rml::Element* pEl = FindUp(m_pContext, x, y, "bagslot");
    if (pEl == NULL)
        return false;
    iSlot = pEl->GetAttribute<int>("bagslot", -1);
    iPage = CurrentPage();
    return iSlot >= 0 && iSlot < INVENTORY_PAGE_SIZE;
}

bool
RoseRmlInventory::EquipAt(int x, int y) const {
    /// Phase 2: the equipment column.
    return false;
}

int
RoseRmlInventory::EquipSlotAt(int x, int y) const {
    return -1;
}

bool
RoseRmlInventory::CostumeOpen() const {
    return false;
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlInventory::Sample() {
    CObjUSER* pAvatar = g_pAVATAR;
    CItemDlg* pDlg = ItemDlg();
    if (pAvatar == NULL || pDlg == NULL)
        return;

    /// --- the page on screen -------------------------------------------------
    std::vector<CellVM> cells;
    cells.reserve(INVENTORY_PAGE_SIZE);
    for (int i = 0; i < INVENTORY_PAGE_SIZE; ++i) {
        CellVM vm;
        vm.index = i;
        vm.filled = false;
        vm.count = 0;
        vm.cd = 0.0f;
        vm.dim = false;
        vm.worn = false;
        vm.socket = 0;

        CSlot* pSlot = pDlg->GetBagSlot(CurrentPage(), i);
        CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
        int iModule = 0, iGraphic = 0;
        if (pIcon && pIcon->IsItemIcon() && pIcon->GetSprite(iModule, iGraphic)
            && RoseRmlIcons::Resolve(iModule, iGraphic, vm.src, vm.rect)) {
            CIconItem* pItemIcon = (CIconItem*)pIcon;
            tagITEM& Item = pItemIcon->GetItem();

            vm.filled = true;
            vm.count = pIcon->GetStackCount();
            vm.cd = floorf(pIcon->GetCooldown(NULL) * 100.0f + 0.5f);
            vm.dim = !pIcon->IsEnable();
            vm.worn = !vm.dim && Item.HasLife() && Item.GetLife() < kWornLife;

            /// The socket mark CIconItem::Draw puts on the icon.
            if (Item.HasSocket()) {
                vm.socket = 1;
                const int iGem = Item.GetGemNO();
                if (iGem > 300 && iGem <= (int)g_TblGEMITEM.row_count
                    && RoseRmlIcons::Resolve(IMAGE_RES_SOCKETJAM_ICON,
                        GEMITEM_MARK_IMAGE(iGem), vm.gem_src, vm.gem_rect))
                    vm.socket = 2;
            }
        }
        cells.push_back(vm);
    }
    if (cells != m_Cells) {
        m_Cells.swap(cells);
        m_Model.DirtyVariable("cells");
    }

    /// --- tab counts -----------------------------------------------------------
    static const char* const kCountNames[kBagTabs] = {"count0", "count1", "count2"};
    for (int t = 0; t < kBagTabs; ++t) {
        int iFilled = 0;
        for (int i = 0; i < INVENTORY_PAGE_SIZE; ++i) {
            CSlot* pSlot = pDlg->GetBagSlot(t, i);
            if (pSlot && pSlot->GetIcon())
                ++iFilled;
        }
        if (iFilled != m_iCount[t]) {
            m_iCount[t] = iFilled;
            m_Model.DirtyVariable(kCountNames[t]);
        }
    }

    /// --- money and weight -------------------------------------------------------
    char szMoney[64];
    CGameUtil::ConvertMoney2String(pAvatar->Get_MONEY(), szMoney, sizeof(szMoney));
    if (m_strMoney != szMoney) {
        m_strMoney = szMoney;
        m_Model.DirtyVariable("money");
    }

    const int iWeight = pAvatar->GetCur_WEIGHT();
    const int iMaxWeight = pAvatar->GetCur_MaxWEIGHT();
    const Rml::String strWeight = Printf("%d / %d", iWeight, iMaxWeight);
    if (strWeight != m_strWeight) {
        m_strWeight = strWeight;
        m_Model.DirtyVariable("weight");
    }
    const float fPct = (iMaxWeight > 0) ? 100.0f * (float)iWeight / (float)iMaxWeight : 0.0f;
    const float fBar = floorf(fPct > 100.0f ? 100.0f : fPct);
    if (fBar != m_fWeightPct) {
        m_fWeightPct = fBar;
        m_Model.DirtyVariable("weight_pct");
    }
    const int iLevel = (fPct >= 100.0f) ? 2 : (fPct >= 80.0f ? 1 : 0);
    if (iLevel != m_iWeightLevel) {
        m_iWeightLevel = iLevel;
        m_Model.DirtyVariable("weight_level");
    }
}

/// --- input ------------------------------------------------------------------------

/// A left press on a cell, with CSlot::Process's precedence: Alt previews,
/// Shift links, Ctrl asks for the wishlist; then repair / appraisal take it;
/// otherwise it may become a drag.
void
RoseRmlInventory::OnPress(int iSlot) {
    m_iPressSlot = -1;

    CSlot* pSlot = BagSlot(iSlot);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;

    CIconItem* pItemIcon = (CIconItem*)pIcon;
    tagITEM& Item = pItemIcon->GetItem();

    if (GetAsyncKeyState(VK_MENU) < 0 && !Item.IsEmpty() && g_UIMed.OpenItemPreview(Item))
        return;

    if (GetAsyncKeyState(VK_SHIFT) < 0) {
        if (!Item.IsEmpty()) {
            if (CChatDLG* pChatDlg = g_itMGR.GetChatDLG())
                pChatDlg->AddItemLinkToInput(Item);
        }
        return;
    }

    /// Ctrl: CIconItem::Process's wishlist question.
    if (GetAsyncKeyState(VK_CONTROL) < 0 && pIcon->Process(WM_LBUTTONDOWN, MK_CONTROL, 0))
        return;

    if (CItemDlg* pDlg = ItemDlg()) {
        if (pDlg->HandleStateClick(pSlot))
            return;
    }

    if (pIcon->IsEnable())
        m_iPressSlot = iSlot;
}

void
RoseRmlInventory::UpdateDragStart() {
    if (m_iPressSlot < 0)
        return;

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_iPressSlot = -1;
        return;
    }

    /// CSlot::Update's threshold: a two-hundredth of the screen.
    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const int iSlopX = max(3, g_pCApp->GetWIDTH() / 200);
    const int iSlopY = max(3, g_pCApp->GetHEIGHT() / 200);
    if (abs(ptMouse.x - m_iPressX) < iSlopX && abs(ptMouse.y - m_iPressY) < iSlopY)
        return;

    const int iSlot = m_iPressSlot;
    m_iPressSlot = -1;

    CItemDlg* pDlg = ItemDlg();
    CSlot* pSlot = BagSlot(iSlot);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pDlg == NULL || pIcon == NULL || pDlg->GetInvenDragItem() == NULL)
        return;

    /// CItemDlg's own drag: the clone keeps its slot ( CIconItem::Clone ),
    /// which the rearrange command needs to find where it came from.
    CDragItem* pDrag = pDlg->GetInvenDragItem();
    pDrag->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDrag);
}

void
RoseRmlInventory::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    int iSlot = -1;
    for (Rml::Element* pEl = m_pContext->GetHoverElement(); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("bagslot")) {
            iSlot = pEl->GetAttribute<int>("bagslot", -1);
            break;
        }
    }
    if (iSlot < 0)
        return;

    CSlot* pSlot = BagSlot(iSlot);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL)
        return;

    /// DLG_TYPE_ITEM, as CSlot passes its parent: the tooltip adds the repair,
    /// appraisal, sell or storage price the moment calls for.
    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip, DLG_TYPE_ITEM, 0);
    if (ToolTip.IsEmpty())
        return;

    /// Beside the window, on the side chosen by where the WINDOW sits.
    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const Rml::Vector2f pos = m_pPanel->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    const int iScreenW = g_pCApp->GetWIDTH();
    const int iScreenH = g_pCApp->GetHEIGHT();

    POINT pt;
    const bool bRightSide = (pos.x + size.x * 0.5f) < (float)iScreenW * 0.5f;
    pt.x = bRightSide ? (int)(pos.x + size.x) + 4 : (int)pos.x - ToolTip.GetWidth() - 4;
    if (pt.x > iScreenW - ToolTip.GetWidth())
        pt.x = iScreenW - ToolTip.GetWidth();
    if (pt.x < 0)
        pt.x = 0;
    pt.y = ptMouse.y - ToolTip.GetHeight() / 2;
    if (pt.y > iScreenH - ToolTip.GetHeight())
        pt.y = iScreenH - ToolTip.GetHeight();
    if (pt.y < 0)
        pt.y = 0;

    ToolTip.SetPosition(pt);
    CToolTipMgr::GetInstance().RegistInfo(ToolTip);
}

void
RoseRmlInventory::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bInWorld = IsInWorld();
    if (!bInWorld)
        m_bOpen = false;

    SetVisible(m_bOpen && bInWorld);
    if (!m_bVisible)
        return;

    Sample();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateDragStart();
    UpdateTooltip();
}
