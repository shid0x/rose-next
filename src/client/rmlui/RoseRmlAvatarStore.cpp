#include "stdafx.h"

#include "RoseRmlAvatarStore.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseRmlUi.h"
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
#include "../gamecommon/item.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CUIMediator.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Icon/CIconItem.h"
#include "../interface/Command/CTCmdNumberInput.h"
#include "../interface/Command/uicommand.h"
#include "../interface/Dlgs/CAvatarStoreDlg.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"

#include "rose/common/log.h"
#include "rose/io/stb.h"

#include <math.h>
#include <stdio.h>

namespace {

/// Both lists hold up to 30 ( c_iAvatarStoreMaxSlotCount ).
const int kCells = c_iAvatarStoreMaxSlotCount;

/// A price small enough to print whole under a 42 dp icon; above it, k / M
/// ( as the NPC shop ).
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

RoseRmlAvatarStore::CellVM
MakeCell(int iIndex) {
    RoseRmlAvatarStore::CellVM vm;
    vm.index = iIndex;
    vm.filled = false;
    vm.count = 0;
    vm.socket = 0;
    vm.dear = false;
    return vm;
}

/// A cell's picture and price from the classic slot behind it.
void
FillCell(RoseRmlAvatarStore::CellVM& vm, CSlot* pSlot, bool bForSale, __int64 iHave) {
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    int iModule = 0, iGraphic = 0;
    if (pIcon == NULL || !pIcon->IsItemIcon() || !pIcon->GetSprite(iModule, iGraphic)
        || !RoseRmlIcons::Resolve(iModule, iGraphic, vm.src, vm.rect))
        return;

    tagITEM& Item = ((CIconItem*)pIcon)->GetItem();
    vm.filled = true;
    vm.count = pIcon->GetStackCount();

    if (Item.HasSocket()) {
        vm.socket = 1;
        const int iGem = Item.GetGemNO();
        if (iGem > 300 && iGem <= (int)g_TblGEMITEM.row_count
            && RoseRmlIcons::Resolve(
                IMAGE_RES_SOCKETJAM_ICON, GEMITEM_MARK_IMAGE(iGem), vm.gem_src, vm.gem_rect))
            vm.socket = 2;
    }

    if (CItem* pItem = ((CIconItem*)pIcon)->GetCItem()) {
        const __int64 iPrice = pItem->GetUnitPrice();
        vm.price = ShortPrice(iPrice);
        vm.dear = bForSale && iPrice > iHave;
    }
}

} // namespace

RoseRmlAvatarStore::RoseRmlAvatarStore():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_pCmdBuy(new CTCmdBuyItemAtAvatarStore),
    m_pCmdOpenBuy(new CTCmdOpenNumberInputDlg),
    m_iPressIndex(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iDropType(DLG_TYPE_AVATARSTORE),
    m_iTab(TAB_FOR_SALE),
    m_strMoney("0") {
    m_iCount[0] = m_iCount[1] = 0;
    m_pCmdOpenBuy->SetCommand(m_pCmdBuy);
}

RoseRmlAvatarStore::~RoseRmlAvatarStore() {
    delete m_pCmdOpenBuy;
    delete m_pCmdBuy;
}

bool
RoseRmlAvatarStore::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    /// Every cell exists from the start, empty: built while the document loads,
    /// not in the frame the shop first opens.
    for (int i = 0; i < kCells; ++i)
        m_Cells.push_back(MakeCell(i));

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("avatarstore");
    if (!constructor)
        return false;

    if (auto cell = constructor.RegisterStruct<CellVM>()) {
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

    constructor.Bind("drop_type", &m_iDropType);
    constructor.Bind("title", &m_strTitle);
    constructor.Bind("owner", &m_strOwner);
    constructor.Bind("tab", &m_iTab);
    constructor.Bind("sale_count", &m_iCount[TAB_FOR_SALE]);
    constructor.Bind("wanted_count", &m_iCount[TAB_WANTED]);
    constructor.Bind("cells", &m_Cells);
    constructor.Bind("money", &m_strMoney);

    constructor.BindEventCallback("set_tab",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                SetTab(args[0].Get<int>());
        });
    /// press(index)
    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.empty() || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
            OnPress(args[0].Get<int>());
        });
    /// use(index) -- double-click: buy.
    constructor.BindEventCallback("use",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            m_iPressIndex = -1;
            OnUse(args[0].Get<int>());
        });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_AVATARSTORE);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "avatarstore.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load avatar store document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("avatarstore");
    RoseRmlLayout::Track(m_pPanel, "avatarstore");

    LOG_INFO("[rmlui] avatar store document loaded");
    return true;
}

void
RoseRmlAvatarStore::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CAvatarStoreDlg*
RoseRmlAvatarStore::StoreDlg() const {
    return (CAvatarStoreDlg*)g_itMGR.FindDlg(DLG_TYPE_AVATARSTORE);
}

CSlot*
RoseRmlAvatarStore::SlotFor(int iIndex) const {
    CAvatarStoreDlg* pDlg = StoreDlg();
    return pDlg ? pDlg->GetSlot(m_iTab, iIndex) : NULL;
}

bool
RoseRmlAvatarStore::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlAvatarStore::SetOpen(bool bOpen) {
    if (bOpen != m_bOpen) {
        RoseUi2::PlayWindowSound(DLG_TYPE_AVATARSTORE, bOpen);
        /// The halves of CAvatarStoreDlg::Show / Hide that are not about the
        /// dialog on screen: it stays hidden as the shop's model.
        if (CAvatarStoreDlg* pDlg = StoreDlg()) {
            if (bOpen)
                pDlg->Prepare();
            else
                pDlg->Reset(); /// frees the goods, closes a pending "how many?"
        }
        if (bOpen)
            SetTab(TAB_FOR_SALE);
    }
    m_bOpen = bOpen;
    m_iPressIndex = -1;
    if (bOpen)
        Sample(); /// no stale shelf on the first frame
}

void
RoseRmlAvatarStore::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_iPressIndex = -1;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlAvatarStore::SetTab(int iTab) {
    if (iTab != TAB_FOR_SALE && iTab != TAB_WANTED)
        return;
    if (iTab != m_iTab) {
        m_iTab = iTab;
        m_iPressIndex = -1;
        m_Model.DirtyVariable("tab");
    }
    Sample();
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlAvatarStore::Sample() {
    CAvatarStoreDlg* pDlg = StoreDlg();
    if (pDlg == NULL || g_pAVATAR == NULL)
        return;

    /// The shop's sign, and whose shop it is.
    const char* pszTitle = pDlg->GetTitle();
    const Rml::String strTitle =
        RoseRmlText::FromGame((pszTitle && pszTitle[0]) ? pszTitle : "Player shop");
    if (strTitle != m_strTitle) {
        m_strTitle = strTitle;
        m_Model.DirtyVariable("title");
    }
    Rml::String strOwner;
    if (CObjAVT* pAvt = g_pObjMGR->Get_CharAVT(
            g_pObjMGR->Get_ClientObjectIndex(pDlg->GetMasterSvrObjIdx()), false))
        strOwner = RoseRmlText::FromGame(pAvt->Get_NAME());
    if (strOwner != m_strOwner) {
        m_strOwner = strOwner;
        m_Model.DirtyVariable("owner");
    }

    /// How many on each list, for the tabs.
    for (int t = 0; t < 2; ++t) {
        int iCount = 0;
        for (int i = 0; i < kCells; ++i) {
            CSlot* pSlot = pDlg->GetSlot(t, i);
            if (pSlot && pSlot->GetIcon())
                ++iCount;
        }
        if (iCount != m_iCount[t]) {
            m_iCount[t] = iCount;
            m_Model.DirtyVariable(t == TAB_FOR_SALE ? "sale_count" : "wanted_count");
        }
    }

    const __int64 iHave = g_pAVATAR->Get_MONEY();
    std::vector<CellVM> cells;
    cells.reserve(kCells);
    for (int i = 0; i < kCells; ++i) {
        CellVM vm = MakeCell(i);
        FillCell(vm, pDlg->GetSlot(m_iTab, i), m_iTab == TAB_FOR_SALE, iHave);
        cells.push_back(vm);
    }
    if (cells != m_Cells) {
        m_Cells.swap(cells);
        m_Model.DirtyVariable("cells");
    }

    const Rml::String strMoney = Money(iHave);
    if (strMoney != m_strMoney) {
        m_strMoney = strMoney;
        m_Model.DirtyVariable("money");
    }
}

/// --- actions ----------------------------------------------------------------------

/// A left press on a cell: Alt previews, Shift links to chat, Ctrl adds to the
/// wishlist ( the icon's own ); otherwise, on their goods, it may become a
/// drag ( CAvatarStoreDlg's, to the bag: buy ).
void
RoseRmlAvatarStore::OnPress(int iIndex) {
    m_iPressIndex = -1;

    CSlot* pSlot = SlotFor(iIndex);
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

    if (GetAsyncKeyState(VK_CONTROL) < 0 && pIcon->Process(WM_LBUTTONDOWN, MK_CONTROL, 0))
        return;

    /// Only their goods drag ( the wanted list is filled from your bag ).
    if (m_iTab == TAB_FOR_SALE && pIcon->IsEnable())
        m_iPressIndex = iIndex;
}

void
RoseRmlAvatarStore::OnUse(int iIndex) {
    /// Their goods: the drag's own command ( how many? -> the classic buy
    /// question ), run on the item. The classic icon had no command: a
    /// double-click did nothing.
    if (m_iTab != TAB_FOR_SALE)
        return;
    CSlot* pSlot = SlotFor(iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;
    if (CItem* pItem = ((CIconItem*)pIcon)->GetCItem())
        m_pCmdOpenBuy->Exec(pItem);
}

void
RoseRmlAvatarStore::UpdateDragStart() {
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

    const int iIndex = m_iPressIndex;
    m_iPressIndex = -1;

    CAvatarStoreDlg* pDlg = StoreDlg();
    CSlot* pSlot = SlotFor(iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    CDragItem* pDrag = pDlg ? pDlg->GetSellDragItem() : NULL;
    if (pIcon == NULL || pDrag == NULL)
        return;

    pDrag->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDrag);
}

void
RoseRmlAvatarStore::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    int iIndex = -1;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("slot-index")) {
            iIndex = pEl->GetAttribute<int>("slot-index", -1);
            break;
        }
    }
    if (iIndex < 0)
        return;

    CSlot* pSlot = SlotFor(iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL)
        return;

    /// The classic slots' tooltip types: the price they ask, or pay.
    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip,
        DLG_TYPE_AVATARSTORE,
        m_iTab == TAB_FOR_SALE ? INFO_ADD_PRICE_AVATARSTORE_SELLTAB
                               : INFO_ADD_PRICE_AVATARSTORE_BUYTAB);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlAvatarStore::PlaceDefault() {
    /// Beside the bag, on whichever side has room -- only while no position is
    /// set ( dragged, saved or reset ).
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
RoseRmlAvatarStore::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen) {
        if (!IsInWorld()) {
            SetOpen(false);
        } else if (CAvatarStoreDlg* pDlg = StoreDlg()) {
            /// CAvatarStoreDlg::Update ( which only ran while it was on
            /// screen ): the owner gone or out of reach.
            CObjAVT* pAvt = g_pObjMGR->Get_CharAVT(
                g_pObjMGR->Get_ClientObjectIndex(pDlg->GetMasterSvrObjIdx()), false);
            if (pAvt == NULL || !g_pAVATAR->IsInRANGE(pAvt, AVT_CLICK_EVENT_RANGE))
                g_itMGR.CloseDialog(DLG_TYPE_AVATARSTORE);
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
