#include "stdafx.h"

#include "RoseRmlShop.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlUi.h"
#include "RoseRmlText.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../IO_Terrain.h"
#include "../System/CGame.h"
#include "../misc/gameutil.h"
#include "../gamedata/CStore.h"
#include "../gamedata/CDealData.h"
#include "../gamecommon/item.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CToolTipMgr.h"
#include "../interface/CUIMediator.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Icon/CIconItem.h"
#include "../interface/Dlgs/CStoreDLG.h"
#include "../interface/Dlgs/DealDlg.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"

#include "rose/common/log.h"
#include "rose/io/stb.h"

#include <math.h>
#include <stdio.h>

namespace {

/// CStoreDLG::Update's reach: past this ( cm ) from its NPC the shop closes.
const float kCloseDistance = 1000.0f;

/// The store has four tabs of 48 ( CStore; c_iSlotCountPerTab ).
const int kStoreTabs = 4;

/// A price small enough to print whole under a 42 dp icon; above it, k / M.
Rml::String
ShortPrice(__int64 iPrice) {
    char szBuf[32];
    if (iPrice < 10000) {
        _snprintf(szBuf, sizeof(szBuf), "%d", (int)iPrice);
    } else if (iPrice < 1000000) {
        const double d = (double)iPrice / 1000.0;
        _snprintf(szBuf, sizeof(szBuf), d < 100.0 ? "%.1fk" : "%.0fk", floor(d * 10.0) / 10.0);
    } else {
        const double d = (double)iPrice / 1000000.0;
        _snprintf(szBuf, sizeof(szBuf), d < 100.0 ? "%.2fM" : "%.0fM", floor(d * 100.0) / 100.0);
    }
    szBuf[sizeof(szBuf) - 1] = '\0';

    /// "12.0k" -> "12k", "1.50M" -> "1.5M".
    Rml::String str = szBuf;
    const size_t iDot = str.find('.');
    if (iDot != Rml::String::npos) {
        const char cUnit = str[str.size() - 1];
        Rml::String strNum = str.substr(0, str.size() - 1);
        while (!strNum.empty() && strNum[strNum.size() - 1] == '0')
            strNum.erase(strNum.size() - 1);
        if (!strNum.empty() && strNum[strNum.size() - 1] == '.')
            strNum.erase(strNum.size() - 1);
        str = strNum + cUnit;
    }
    return str;
}

Rml::String
Money(__int64 iMoney) {
    char szBuf[64];
    CGameUtil::ConvertMoney2String(iMoney, szBuf, sizeof(szBuf));
    return szBuf;
}

/// A cell's picture from the classic slot behind it.
void
FillCell(RoseRmlShop::CellVM& vm, CSlot* pSlot, bool bCount) {
    vm.filled = false;
    vm.count = 0;
    vm.socket = 0;

    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    int iModule = 0, iGraphic = 0;
    if (pIcon == NULL || !pIcon->IsItemIcon() || !pIcon->GetSprite(iModule, iGraphic)
        || !RoseRmlIcons::Resolve(iModule, iGraphic, vm.src, vm.rect))
        return;

    tagITEM& Item = ((CIconItem*)pIcon)->GetItem();
    vm.filled = true;
    if (bCount)
        vm.count = pIcon->GetStackCount();

    /// The socket mark CIconItem::Draw puts on the icon.
    if (Item.HasSocket()) {
        vm.socket = 1;
        const int iGem = Item.GetGemNO();
        if (iGem > 300 && iGem <= (int)g_TblGEMITEM.row_count
            && RoseRmlIcons::Resolve(
                IMAGE_RES_SOCKETJAM_ICON, GEMITEM_MARK_IMAGE(iGem), vm.gem_src, vm.gem_rect))
            vm.socket = 2;
    }
}

RoseRmlShop::CellVM
MakeCell(int iKind, int iIndex) {
    RoseRmlShop::CellVM vm;
    vm.kind = iKind;
    vm.index = iIndex;
    vm.filled = false;
    vm.count = 0;
    vm.socket = 0;
    vm.dear = false;
    return vm;
}

/// The union points of the avatar's union, what a union shop charges.
__int64
UnionPoints() {
    if (g_pAVATAR == NULL)
        return 0;
    return g_pAVATAR->Get_AbilityValue(AT_UNION_POINT1 - 1 + g_pAVATAR->Get_UNION());
}

} // namespace

RoseRmlShop::RoseRmlShop():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iNpcAtOpen(-1),
    m_iPressKind(KIND_STORE),
    m_iPressIndex(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iStoreDrop(DLG_TYPE_STORE),
    m_iDealDrop(DLG_TYPE_DEAL),
    m_iTab(0),
    m_iBuyCount(0),
    m_iSellCount(0),
    m_bUnion(false),
    m_strBuyTotal("0"),
    m_strSellTotal("0"),
    m_strMoney("0"),
    m_strAfter("0"),
    m_bShort(false) {}

bool
RoseRmlShop::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    /// Every row exists from the start, empty, as the classic dialog's slots
    /// did: the rows' elements are then built while the document loads, not
    /// in the frame the first shop opens.
    for (int i = 0; i < c_iSlotCountPerTab; ++i)
        m_Store.push_back(MakeCell(KIND_STORE, i));
    for (int i = 0; i < TOTAL_DEAL_INVENTORY; ++i) {
        m_Buy.push_back(MakeCell(KIND_BUY, i));
        m_Sell.push_back(MakeCell(KIND_SELL, i));
    }

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("shop");
    if (!constructor)
        return false;

    if (auto tab = constructor.RegisterStruct<TabVM>()) {
        tab.RegisterMember("index", &TabVM::index);
        tab.RegisterMember("name", &TabVM::name);
    }
    constructor.RegisterArray<std::vector<TabVM>>();

    if (auto cell = constructor.RegisterStruct<CellVM>()) {
        cell.RegisterMember("kind", &CellVM::kind);
        cell.RegisterMember("index", &CellVM::index);
        cell.RegisterMember("filled", &CellVM::filled);
        cell.RegisterMember("src", &CellVM::src);
        cell.RegisterMember("rect", &CellVM::rect);
        cell.RegisterMember("count", &CellVM::count);
        cell.RegisterMember("socket", &CellVM::socket);
        cell.RegisterMember("gem_src", &CellVM::gem_src);
        cell.RegisterMember("gem_rect", &CellVM::gem_rect);
        cell.RegisterMember("price", &CellVM::price);
        cell.RegisterMember("dear", &CellVM::dear);
    }
    constructor.RegisterArray<std::vector<CellVM>>();

    constructor.Bind("store_drop", &m_iStoreDrop);
    constructor.Bind("deal_drop", &m_iDealDrop);
    constructor.Bind("name", &m_strName);
    constructor.Bind("role", &m_strRole);
    constructor.Bind("tab", &m_iTab);
    constructor.Bind("tabs", &m_Tabs);
    constructor.Bind("store", &m_Store);
    constructor.Bind("buy", &m_Buy);
    constructor.Bind("sell", &m_Sell);
    constructor.Bind("buy_count", &m_iBuyCount);
    constructor.Bind("sell_count", &m_iSellCount);
    constructor.Bind("union", &m_bUnion);
    constructor.Bind("union_src", &m_strUnionSrc);
    constructor.Bind("union_rect", &m_strUnionRect);
    constructor.Bind("buy_total", &m_strBuyTotal);
    constructor.Bind("sell_total", &m_strSellTotal);
    constructor.Bind("money", &m_strMoney);
    constructor.Bind("after", &m_strAfter);
    constructor.Bind("short", &m_bShort);

    constructor.BindEventCallback("set_tab",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int iTab = args[0].Get<int>();
            if (iTab >= 0 && iTab < kStoreTabs && iTab != m_iTab) {
                m_iTab = iTab;
                m_iPressIndex = -1;
                m_Model.DirtyVariable("tab");
                Sample();
            }
        });

    /// press(kind, index)
    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.size() < 2 || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
            OnPress(args[0].Get<int>(), args[1].Get<int>());
        });

    /// use(kind, index) -- double-click.
    constructor.BindEventCallback("use",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.size() < 2)
                return;
            m_iPressIndex = -1;
            OnUse(args[0].Get<int>(), args[1].Get<int>());
        });

    constructor.BindEventCallback("confirm",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (CDealData::GetInstance().GetTradeItemCnt() > 0)
                CDealData::GetInstance().SendTradeReq();
        });

    constructor.BindEventCallback("clear",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (CDealData::GetInstance().GetTradeItemCnt() > 0)
                CDealData::GetInstance().ClearTradeList();
        });

    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.Close_store();
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "shop.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load shop document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("shop");
    RoseRmlLayout::Track(m_pPanel, "shop");

    LOG_INFO("[rmlui] shop document loaded");
    return true;
}

void
RoseRmlShop::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CStoreDLG*
RoseRmlShop::StoreDlg() const {
    return (CStoreDLG*)g_itMGR.FindDlg(DLG_TYPE_STORE);
}

CDealDLG*
RoseRmlShop::DealDlg() const {
    return (CDealDLG*)g_itMGR.FindDlg(DLG_TYPE_DEAL);
}

CSlot*
RoseRmlShop::SlotFor(int iKind, int iIndex) const {
    switch (iKind) {
        case KIND_STORE: {
            CStoreDLG* pDlg = StoreDlg();
            return pDlg ? pDlg->GetSlot(m_iTab, iIndex) : NULL;
        }
        case KIND_BUY: {
            CDealDLG* pDlg = DealDlg();
            return pDlg ? pDlg->GetSlot(DEAL_BUY, iIndex) : NULL;
        }
        case KIND_SELL: {
            CDealDLG* pDlg = DealDlg();
            return pDlg ? pDlg->GetSlot(DEAL_SELL, iIndex) : NULL;
        }
        default:
            return NULL;
    }
}

CDragItem*
RoseRmlShop::DragItemFor(int iKind) const {
    if (iKind == KIND_STORE) {
        CStoreDLG* pDlg = StoreDlg();
        return pDlg ? pDlg->GetDragItem() : NULL;
    }
    CDealDLG* pDlg = DealDlg();
    if (pDlg == NULL)
        return NULL;
    return pDlg->GetDragItem(iKind == KIND_BUY ? DEAL_BUY : DEAL_SELL);
}

bool
RoseRmlShop::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlShop::SetOpen(bool bOpen) {
    if (!bOpen) {
        Close();
        return;
    }

    /// GF_openStore opens the store and then the basket: one window.
    const int iNpc = CStore::GetInstance().GetNpcObjIndex();
    if (m_bOpen && iNpc == m_iNpcAtOpen)
        return;

    /// Another merchant while open: the basket held the last one's goods.
    if (m_bOpen)
        CDealData::GetInstance().ClearTradeList();
    else
        RoseUi2::PlayWindowSound(DLG_TYPE_STORE, true);

    m_bOpen = true;
    m_iNpcAtOpen = iNpc;
    m_iPressIndex = -1;

    /// The first tab that has anything, as the classic dialog opened on tab 1.
    m_iTab = 0;
    for (int i = 0; i < kStoreTabs; ++i) {
        if (!CStore::GetInstance().GetTabName(i).empty()) {
            m_iTab = i;
            break;
        }
    }
    m_Model.DirtyVariable("tab");

    SampleName();
    Sample(); /// no stale shelf on the first frame
}

void
RoseRmlShop::Close() {
    if (!m_bOpen)
        return;

    m_bOpen = false;
    m_iPressIndex = -1;
    RoseUi2::PlayWindowSound(DLG_TYPE_STORE, false);

    /// CDealDLG::Hide's job: nothing stays on the lists once the shop is gone.
    CDealData::GetInstance().ClearTradeList();

    /// IT_MGR::Close_store's: a "how many?" asked for this shop is moot.
    if (g_itMGR.IsDlgOpened(DLG_TYPE_N_INPUT))
        g_itMGR.CloseDialog(DLG_TYPE_N_INPUT);
}

void
RoseRmlShop::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_iPressIndex = -1;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlShop::SampleName() {
    /// "[Weapon Merchant] Raffle" splits in two, as the conversation's title.
    Rml::String strName, strRole;
    if (CObjCHAR* pNpc = g_pObjMGR->Get_CharOBJ(CStore::GetInstance().GetNpcObjIndex(), false)) {
        strName = RoseRmlText::FromGame(pNpc->Get_NAME());
        const size_t iClose = strName.find(']');
        if (!strName.empty() && strName[0] == '[' && iClose != Rml::String::npos) {
            strRole = strName.substr(1, iClose - 1);
            strName = strName.substr(iClose + 1);
            while (!strName.empty() && strName[0] == ' ')
                strName.erase(0, 1);
        }
    }
    if (strName.empty())
        strName = "Shop";
    if (strName != m_strName || strRole != m_strRole) {
        m_strName = strName;
        m_strRole = strRole;
        m_Model.DirtyVariable("name");
        m_Model.DirtyVariable("role");
    }
}

void
RoseRmlShop::Sample() {
    CObjUSER* pAvatar = g_pAVATAR;
    CStoreDLG* pStoreDlg = StoreDlg();
    CDealDLG* pDealDlg = DealDlg();
    if (pAvatar == NULL || pStoreDlg == NULL || pDealDlg == NULL || g_pTerrain == NULL)
        return;

    CStore& Store = CStore::GetInstance();
    CDealData& Deal = CDealData::GetInstance();

    /// --- the currency -----------------------------------------------------------
    const bool bUnion = Store.IsUnionStore();
    if (bUnion != m_bUnion) {
        m_bUnion = bUnion;
        m_Model.DirtyVariable("union");
    }
    if (bUnion) {
        Rml::String strSrc, strRect;
        RoseRmlIcons::Resolve(IMAGE_RES_STATE_ICON, UNION_MARK(Store.GetUnionNo()), strSrc, strRect);
        if (strSrc != m_strUnionSrc || strRect != m_strUnionRect) {
            m_strUnionSrc = strSrc;
            m_strUnionRect = strRect;
            m_Model.DirtyVariable("union_src");
            m_Model.DirtyVariable("union_rect");
        }
    }
    const __int64 iHave = bUnion ? UnionPoints() : pAvatar->Get_MONEY();

    /// --- tabs ---------------------------------------------------------------------
    std::vector<TabVM> tabs;
    for (int i = 0; i < kStoreTabs; ++i) {
        const std::string& strTab = Store.GetTabName(i);
        if (strTab.empty())
            continue;
        TabVM vm;
        vm.index = i;
        vm.name = RoseRmlText::FromGame(strTab.c_str());
        tabs.push_back(vm);
    }
    if (tabs != m_Tabs) {
        m_Tabs.swap(tabs);
        m_Model.DirtyVariable("tabs");
    }

    /// --- the shelf on screen, priced ----------------------------------------------
    std::vector<CellVM> store;
    store.reserve(c_iSlotCountPerTab);
    for (int i = 0; i < c_iSlotCountPerTab; ++i) {
        CellVM vm = MakeCell(KIND_STORE, i);
        CSlot* pSlot = pStoreDlg->GetSlot(m_iTab, i);
        /// No stack count on the shelf, as CIconItem::Draw skipped it there.
        FillCell(vm, pSlot, false);
        if (vm.filled) {
            tagITEM& Item = ((CIconItem*)pSlot->GetIcon())->GetItem();
            const __int64 iPrice = bUnion
                ? (__int64)ITEM_TRADE_UNIONPOINT(Item.GetTYPE(), Item.GetItemNO())
                : (__int64)g_pTerrain->m_Economy.Get_ItemBuyPRICE(
                    Item.GetTYPE(), Item.GetItemNO(), pAvatar->GetBuySkillVALUE());
            vm.price = ShortPrice(iPrice);
            vm.dear = iPrice > iHave;
        }
        store.push_back(vm);
    }
    if (store != m_Store) {
        m_Store.swap(store);
        m_Model.DirtyVariable("store");
    }

    /// --- the basket -------------------------------------------------------------------
    int iBuy = 0, iSell = 0;
    std::vector<CellVM> buy, sell;
    for (int i = 0; i < TOTAL_DEAL_INVENTORY; ++i) {
        CellVM b = MakeCell(KIND_BUY, i);
        FillCell(b, pDealDlg->GetSlot(DEAL_BUY, i), true);
        iBuy += b.filled ? 1 : 0;
        buy.push_back(b);

        CellVM s = MakeCell(KIND_SELL, i);
        FillCell(s, pDealDlg->GetSlot(DEAL_SELL, i), true);
        iSell += s.filled ? 1 : 0;
        sell.push_back(s);
    }
    if (buy != m_Buy) {
        m_Buy.swap(buy);
        m_Model.DirtyVariable("buy");
    }
    if (sell != m_Sell) {
        m_Sell.swap(sell);
        m_Model.DirtyVariable("sell");
    }
    if (iBuy != m_iBuyCount) {
        m_iBuyCount = iBuy;
        m_Model.DirtyVariable("buy_count");
    }
    if (iSell != m_iSellCount) {
        m_iSellCount = iSell;
        m_Model.DirtyVariable("sell_count");
    }

    /// --- totals: a union shop charges points but still pays zuly ----------------
    const __int64 iBuyTotal = Deal.GetTotal_BUY();
    const __int64 iSellTotal = Deal.GetTotal_SELL();
    const __int64 iAfter = bUnion ? iHave - iBuyTotal : iHave + iSellTotal - iBuyTotal;

    const Rml::String strBuy = Money(iBuyTotal);
    const Rml::String strSell = Money(iSellTotal);
    const Rml::String strMoney = Money(iHave);
    const Rml::String strAfter = (iAfter < 0) ? "-" + Money(-iAfter) : Money(iAfter);
    if (strBuy != m_strBuyTotal) {
        m_strBuyTotal = strBuy;
        m_Model.DirtyVariable("buy_total");
    }
    if (strSell != m_strSellTotal) {
        m_strSellTotal = strSell;
        m_Model.DirtyVariable("sell_total");
    }
    if (strMoney != m_strMoney) {
        m_strMoney = strMoney;
        m_Model.DirtyVariable("money");
    }
    if (strAfter != m_strAfter) {
        m_strAfter = strAfter;
        m_Model.DirtyVariable("after");
    }
    if ((iAfter < 0) != m_bShort) {
        m_bShort = iAfter < 0;
        m_Model.DirtyVariable("short");
    }
}

/// --- input ------------------------------------------------------------------------

/// A left press on a cell, with CSlot::Process's precedence: Alt previews,
/// Shift links, Ctrl asks for the wishlist ( the shelf ); otherwise it may
/// become a drag.
void
RoseRmlShop::OnPress(int iKind, int iIndex) {
    m_iPressIndex = -1;

    CSlot* pSlot = SlotFor(iKind, iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;

    tagITEM& Item = ((CIconItem*)pIcon)->GetItem();

    if (GetAsyncKeyState(VK_MENU) < 0 && !Item.IsEmpty() && g_UIMed.OpenItemPreview(Item))
        return;

    if (GetAsyncKeyState(VK_SHIFT) < 0) {
        if (!Item.IsEmpty()) {
            if (CChatDLG* pChatDlg = g_itMGR.GetChatDLG())
                pChatDlg->AddItemLinkToInput(Item);
        }
        return;
    }

    if (iKind == KIND_STORE && GetAsyncKeyState(VK_CONTROL) < 0
        && pIcon->Process(WM_LBUTTONDOWN, MK_CONTROL, 0))
        return;

    if (pIcon->IsEnable()) {
        m_iPressKind = iKind;
        m_iPressIndex = iIndex;
    }
}

void
RoseRmlShop::OnUse(int iKind, int iIndex) {
    CSlot* pSlot = SlotFor(iKind, iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;

    switch (iKind) {
        case KIND_STORE:
            /// The shelf icon's command: how many? -> onto the buying list.
            pIcon->ExecuteCommand();
            break;
        case KIND_BUY:
            CDealData::GetInstance().RemoveItemFromBuyList(((CIconItem*)pIcon)->GetCItem());
            break;
        case KIND_SELL:
            CDealData::GetInstance().RemoveItemFromSellList(((CIconItem*)pIcon)->GetCItem());
            break;
        default:
            break;
    }
}

void
RoseRmlShop::UpdateDragStart() {
    if (m_iPressIndex < 0)
        return;

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_iPressIndex = -1;
        return;
    }

    /// CSlot::Update's threshold: a two-hundredth of the screen.
    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const int iSlopX = max(3, g_pCApp->GetWIDTH() / 200);
    const int iSlopY = max(3, g_pCApp->GetHEIGHT() / 200);
    if (abs(ptMouse.x - m_iPressX) < iSlopX && abs(ptMouse.y - m_iPressY) < iSlopY)
        return;

    const int iKind = m_iPressKind;
    const int iIndex = m_iPressIndex;
    m_iPressIndex = -1;

    CSlot* pSlot = SlotFor(iKind, iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    CDragItem* pDrag = DragItemFor(iKind);
    if (pIcon == NULL || pDrag == NULL)
        return;

    pDrag->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDrag);
}

void
RoseRmlShop::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    int iKind = -1, iIndex = -1;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("slot-kind")) {
            iKind = pEl->GetAttribute<int>("slot-kind", -1);
            iIndex = pEl->GetAttribute<int>("slot-index", -1);
            break;
        }
    }
    if (iKind < 0 || iIndex < 0)
        return;

    CSlot* pSlot = SlotFor(iKind, iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL)
        return;

    /// The classic slots' parents: the shelf adds its price line, the basket
    /// the full stats.
    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip, (iKind == KIND_STORE) ? DLG_TYPE_STORE : DLG_TYPE_DEAL, 0);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlShop::PlaceDefault() {
    /// Beside the inventory ( the shop opens it too ), on whichever side has
    /// room -- only while no position is set ( dragged, saved or reset ).
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fGap = floorf(8.0f * RoseRmlLayout::GetScaleRatio());
    float fLeft = floorf(((float)view.x - size.x) * 0.5f);
    float fTop = floorf(((float)view.y - size.y) * 0.4f);

    Rml::Element* pInventory = NULL;
    for (int i = 0; i < m_pContext->GetNumDocuments() && pInventory == NULL; ++i) {
        if (Rml::ElementDocument* pDoc = m_pContext->GetDocument(i))
            pInventory = pDoc->IsVisible() ? pDoc->GetElementById("inventory") : NULL;
    }
    if (pInventory != NULL) {
        const Rml::Vector2f invPos = pInventory->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f invSize = pInventory->GetBox().GetSize(Rml::BoxArea::Border);
        if (invPos.x - fGap - size.x >= 0.0f)
            fLeft = floorf(invPos.x - fGap - size.x);
        else if (invPos.x + invSize.x + fGap + size.x <= (float)view.x)
            fLeft = floorf(invPos.x + invSize.x + fGap);
        fTop = floorf(invPos.y);
    }

    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

void
RoseRmlShop::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen) {
        if (!IsInWorld()) {
            Close();
        } else {
            /// CStoreDLG::Update: the merchant gone or too far.
            CObjCHAR* pNpc = g_pObjMGR->Get_CharOBJ(CStore::GetInstance().GetNpcObjIndex(), false);
            if (pNpc == NULL || g_pAVATAR->Get_DISTANCE(pNpc) >= kCloseDistance)
                g_itMGR.Close_store();
        }
    }

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    Sample();
    PlaceDefault();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateDragStart();
    UpdateTooltip();
}
