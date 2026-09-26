#include "stdafx.h"

#include "RoseRmlPrivateStore.h"
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
#include "../gamedata/CPrivateStore.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CUIMediator.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/typeresource.h"
#include "../interface/Icon/CIconItem.h"
#include "../interface/Dlgs/CGoodsDlg.h"
#include "../interface/Dlgs/CPrivateStoreDlg.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"

#include "rose/common/log.h"
#include "rose/io/stb.h"

#include <math.h>
#include <stdio.h>

namespace {

/// Both lists show 30 ( c_iMaxSlotCount ).
const int kCells = c_iMaxSlotCount;

/// The sign's length ( the classic edit box's LIMITTEXT ).
const int kSignMax = 30;

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

RoseRmlPrivateStore::CellVM
MakeCell(int iIndex) {
    RoseRmlPrivateStore::CellVM vm;
    vm.index = iIndex;
    vm.filled = false;
    vm.count = 0;
    vm.socket = 0;
    vm.priced = false;
    vm.unpriced = false;
    return vm;
}

/// How many a listed line is for: a stack's count, else one.
__int64
LineQuantity(CItem* pItem) {
    if (pItem == NULL)
        return 0;
    if (!pItem->GetItem().IsEnableDupCNT())
        return 1;
    return pItem->GetQuantity();
}

/// The classic default sign, "<name>'s shop".
std::string
DefaultSign() {
    if (g_pAVATAR == NULL)
        return std::string();
    return CStr::Printf(STR_DEFAULT_PRIVATESTORE_TITLE, g_pAVATAR->Get_NAME());
}

} // namespace

RoseRmlPrivateStore::RoseRmlPrivateStore():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iPressIndex(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iDropType(DLG_TYPE_PRIVATESTORE),
    m_bSelling(false),
    m_iTab(TAB_SELL),
    m_iSellCount(0),
    m_iWishCount(0),
    m_iPricedCount(0),
    m_strEarn("0"),
    m_strCost("0"),
    m_strMoney("0"),
    m_bShort(false),
    m_bCanOpen(false) {}

bool
RoseRmlPrivateStore::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    /// Every cell exists from the start, empty: built while the document loads,
    /// not in the frame the shop first opens.
    for (int i = 0; i < kCells; ++i)
        m_Cells.push_back(MakeCell(i));

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("privatestore");
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
        cell.RegisterMember("priced", &CellVM::priced);
        cell.RegisterMember("unpriced", &CellVM::unpriced);
    }
    constructor.RegisterArray<std::vector<CellVM>>();

    constructor.Bind("drop_type", &m_iDropType);
    constructor.Bind("selling", &m_bSelling);
    constructor.Bind("sign", &m_strSign);
    constructor.Bind("tab", &m_iTab);
    constructor.Bind("sell_count", &m_iSellCount);
    constructor.Bind("wish_count", &m_iWishCount);
    constructor.Bind("priced_count", &m_iPricedCount);
    constructor.Bind("cells", &m_Cells);
    constructor.Bind("earn", &m_strEarn);
    constructor.Bind("cost", &m_strCost);
    constructor.Bind("money", &m_strMoney);
    constructor.Bind("short", &m_bShort);
    constructor.Bind("can_open", &m_bCanOpen);
    constructor.Bind("hint", &m_strHint);

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
    /// use(index) -- double-click.
    constructor.BindEventCallback("use",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            m_iPressIndex = -1;
            OnUse(args[0].Get<int>());
        });
    constructor.BindEventCallback("open_shop",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnOpenShop(); });
    constructor.BindEventCallback("close_shop",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnCloseShop(); });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_PRIVATESTORE);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "privatestore.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load private store document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("privatestore");
    RoseRmlLayout::Track(m_pPanel, "privatestore");

    LOG_INFO("[rmlui] private store document loaded");
    return true;
}

void
RoseRmlPrivateStore::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CPrivateStoreDlg*
RoseRmlPrivateStore::StoreDlg() const {
    return (CPrivateStoreDlg*)g_itMGR.FindDlg(DLG_TYPE_PRIVATESTORE);
}

/// The icon in a cell of the current tab; pbPriced: on the list visitors see.
CIcon*
RoseRmlPrivateStore::IconAt(int iIndex, bool* pbPriced) const {
    CPrivateStoreDlg* pDlg = StoreDlg();
    if (pDlg == NULL)
        return NULL;
    if (m_iTab == TAB_SELL) {
        CSlot* pSlot = pDlg->GetSellSlot(iIndex);
        if (pbPriced)
            *pbPriced = true;
        return pSlot ? pSlot->GetIcon() : NULL;
    }
    CSlotBuyPrivateStore* pSlot = pDlg->GetBuySlot(iIndex);
    if (pbPriced)
        *pbPriced = pSlot != NULL && pSlot->IsExhibition();
    return pSlot ? pSlot->GetIcon() : NULL;
}

bool
RoseRmlPrivateStore::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlPrivateStore::SetOpen(bool bOpen) {
    if (bOpen != m_bOpen) {
        RoseUi2::PlayWindowSound(DLG_TYPE_PRIVATESTORE, bOpen);
        /// The halves of CPrivateStoreDlg::Show / Hide that are not about the
        /// dialog on screen: it stays hidden as the shop's model.
        if (CPrivateStoreDlg* pDlg = StoreDlg()) {
            if (bOpen)
                pDlg->Prepare(); /// lists cleared, the wish list attached
            else
                pDlg->Teardown(); /// the shop closed, lists cleared
        }
        if (bOpen) {
            m_iTab = -1; /// forces SetTab to push the tab to the dialog
            SetTab(TAB_SELL);
            /// The classic default sign, as CPrivateStoreDlg::Show.
            if (m_pDocument != NULL) {
                if (Rml::Element* pSign = m_pDocument->GetElementById("sign"))
                    pSign->SetAttribute("value", RoseRmlText::FromGame(DefaultSign().c_str()));
            }
        } else if (g_itMGR.IsDlgOpened(DLG_TYPE_GOODS)) {
            g_itMGR.CloseDialog(DLG_TYPE_GOODS); /// a price for a shop that is gone
        }
    }
    m_bOpen = bOpen;
    m_iPressIndex = -1;
    if (bOpen)
        Sample(); /// no stale list on the first frame
}

void
RoseRmlPrivateStore::SetVisible(bool bVisible) {
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
RoseRmlPrivateStore::SetTab(int iTab) {
    if (iTab != TAB_SELL && iTab != TAB_WANTED)
        return;
    /// The hidden dialog's tab too: the bag's drop command reads it there
    /// ( selling list or wish list ).
    if (CPrivateStoreDlg* pDlg = StoreDlg())
        pDlg->SetTab(iTab);
    if (iTab != m_iTab) {
        m_iTab = iTab;
        m_iPressIndex = -1;
        m_Model.DirtyVariable("tab");
    }
    Sample();
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlPrivateStore::Sample() {
    CPrivateStoreDlg* pDlg = StoreDlg();
    if (pDlg == NULL || g_pAVATAR == NULL)
        return;

    CPrivateStore& refStore = CPrivateStore::GetInstance();
    const bool bSelling = refStore.IsOpened();
    if (bSelling != m_bSelling) {
        m_bSelling = bSelling;
        m_Model.DirtyVariable("selling");
    }
    const Rml::String strSign = RoseRmlText::FromGame(refStore.GetTitle());
    if (strSign != m_strSign) {
        m_strSign = strSign;
        m_Model.DirtyVariable("sign");
    }

    /// Both lists, counted and totalled.
    int iSell = 0, iWish = 0, iPriced = 0;
    __int64 iEarn = 0, iCost = 0;
    for (int i = 0; i < kCells; ++i) {
        if (CSlot* pSlot = pDlg->GetSellSlot(i)) {
            if (CIcon* pIcon = pSlot->GetIcon()) {
                ++iSell;
                if (pIcon->IsItemIcon()) {
                    CItem* pItem = ((CIconItem*)pIcon)->GetCItem();
                    if (pItem)
                        iEarn += (__int64)pItem->GetUnitPrice() * LineQuantity(pItem);
                }
            }
        }
        if (CSlotBuyPrivateStore* pSlot = pDlg->GetBuySlot(i)) {
            if (CIcon* pIcon = pSlot->GetIcon()) {
                ++iWish;
                if (pSlot->IsExhibition()) {
                    ++iPriced;
                    CItem* pItem = pIcon->IsItemIcon() ? ((CIconItem*)pIcon)->GetCItem() : NULL;
                    if (pItem)
                        iCost += (__int64)pItem->GetUnitPrice() * LineQuantity(pItem);
                }
            }
        }
    }
    if (iSell != m_iSellCount) {
        m_iSellCount = iSell;
        m_Model.DirtyVariable("sell_count");
    }
    if (iWish != m_iWishCount) {
        m_iWishCount = iWish;
        m_Model.DirtyVariable("wish_count");
    }
    if (iPriced != m_iPricedCount) {
        m_iPricedCount = iPriced;
        m_Model.DirtyVariable("priced_count");
    }

    const __int64 iHave = g_pAVATAR->Get_MONEY();
    const Rml::String strEarn = Money(iEarn);
    const Rml::String strCost = Money(iCost);
    const Rml::String strMoney = Money(iHave);
    /// CPrivateStore::Open refuses a wanted list you cannot pay for.
    const bool bShort = iCost > iHave;
    if (strEarn != m_strEarn) {
        m_strEarn = strEarn;
        m_Model.DirtyVariable("earn");
    }
    if (strCost != m_strCost) {
        m_strCost = strCost;
        m_Model.DirtyVariable("cost");
    }
    if (strMoney != m_strMoney) {
        m_strMoney = strMoney;
        m_Model.DirtyVariable("money");
    }
    if (bShort != m_bShort) {
        m_bShort = bShort;
        m_Model.DirtyVariable("short");
    }
    const bool bCanOpen = (iSell > 0 || iPriced > 0) && !bShort;
    if (bCanOpen != m_bCanOpen) {
        m_bCanOpen = bCanOpen;
        m_Model.DirtyVariable("can_open");
    }

    const char* pszHint = "Close the shop to change your goods.";
    if (!bSelling && m_iTab == TAB_SELL)
        pszHint = "Drop items from your bag to sell them. Double-click or drag one off to "
                  "take it back.";
    else if (!bSelling)
        pszHint = "Drop an item from your bag to buy more of it. Double-click to set or remove "
                  "a price; drag an unpriced one off to forget it.";
    if (m_strHint != pszHint) {
        m_strHint = pszHint;
        m_Model.DirtyVariable("hint");
    }

    /// The current tab's cells.
    std::vector<CellVM> cells;
    cells.reserve(kCells);
    for (int i = 0; i < kCells; ++i) {
        CellVM vm = MakeCell(i);
        bool bPriced = false;
        CIcon* pIcon = IconAt(i, &bPriced);
        int iModule = 0, iGraphic = 0;
        if (pIcon != NULL && pIcon->IsItemIcon() && pIcon->GetSprite(iModule, iGraphic)
            && RoseRmlIcons::Resolve(iModule, iGraphic, vm.src, vm.rect)) {
            tagITEM& Item = ((CIconItem*)pIcon)->GetItem();
            vm.filled = true;
            vm.count = pIcon->GetStackCount();
            vm.priced = bPriced;
            vm.unpriced = !bPriced;
            if (Item.HasSocket()) {
                vm.socket = 1;
                const int iGem = Item.GetGemNO();
                if (iGem > 300 && iGem <= (int)g_TblGEMITEM.row_count
                    && RoseRmlIcons::Resolve(IMAGE_RES_SOCKETJAM_ICON,
                        GEMITEM_MARK_IMAGE(iGem),
                        vm.gem_src,
                        vm.gem_rect))
                    vm.socket = 2;
            }
            CItem* pItem = ((CIconItem*)pIcon)->GetCItem();
            if (bPriced && pItem != NULL)
                vm.price = ShortPrice(pItem->GetUnitPrice());
        }
        cells.push_back(vm);
    }
    if (cells != m_Cells) {
        m_Cells.swap(cells);
        m_Model.DirtyVariable("cells");
    }
}

/// --- actions ----------------------------------------------------------------------

/// A left press on a cell: Alt previews, Shift links to chat; otherwise it may
/// become a drag off the list ( the classic dialog's drag items ).
void
RoseRmlPrivateStore::OnPress(int iIndex) {
    m_iPressIndex = -1;

    bool bPriced = false;
    CIcon* pIcon = IconAt(iIndex, &bPriced);
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

    /// Nothing changes while the shop is open ( CPrivateStore refuses ). A
    /// priced wanted item comes off its price first ( double-click ).
    if (CPrivateStore::GetInstance().IsOpened())
        return;
    if (m_iTab == TAB_WANTED && bPriced)
        return;
    m_iPressIndex = iIndex;
}

void
RoseRmlPrivateStore::OnUse(int iIndex) {
    CPrivateStore& refStore = CPrivateStore::GetInstance();
    if (refStore.IsOpened())
        return;

    bool bPriced = false;
    CIcon* pIcon = IconAt(iIndex, &bPriced);
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;
    CIconItem* pItemIcon = (CIconItem*)pIcon;

    if (m_iTab == TAB_SELL) {
        /// Off the selling list ( as the classic drag off: the bag index ).
        refStore.RemoveItemSellList(pItemIcon->GetIndex());
        return;
    }
    if (bPriced) {
        /// The price off: back to the plain wish list ( the wish index ).
        refStore.RemoveItemBuyList(pItemIcon->GetIndex());
        return;
    }
    /// Price it ( the classic "add to buy list" button ).
    if (CGoodsDlg* pGoodsDlg = (CGoodsDlg*)g_itMGR.FindDlg(DLG_TYPE_GOODS)) {
        pGoodsDlg->SetIcon(pItemIcon);
        pGoodsDlg->SetType(CGoodsDlg::ADD_BUYLIST);
        g_itMGR.OpenDialog(DLG_TYPE_GOODS);
    }
}

void
RoseRmlPrivateStore::OnOpenShop() {
    CPrivateStore& refStore = CPrivateStore::GetInstance();
    if (refStore.IsOpened())
        return;
    /// CPrivateStore::Open sends nothing for an empty shop, silently.
    if (!m_bCanOpen)
        return;
    /// Too close to an NPC: it says so in the chat.
    if (!refStore.IsValidOpen())
        return;

    /// The sign, as typed ( plain text: the field takes printable ASCII ),
    /// trimmed; blank is the classic default.
    std::string strSign;
    if (m_pDocument != NULL) {
        if (Rml::Element* pSign = m_pDocument->GetElementById("sign")) {
            strSign = pSign->GetAttribute<Rml::String>("value", "");
            pSign->Blur();
        }
    }
    while (!strSign.empty() && strSign[0] == ' ')
        strSign.erase(0, 1);
    while (!strSign.empty() && strSign[strSign.size() - 1] == ' ')
        strSign.erase(strSign.size() - 1);
    if ((int)strSign.size() > kSignMax)
        strSign.resize(kSignMax);
    if (strSign.empty())
        strSign = DefaultSign();
    refStore.SetTitle(strSign.c_str());

    /// CPrivateStoreDlg's Start button.
    refStore.SortItemSellList();
    refStore.Open();
    Sample();
}

void
RoseRmlPrivateStore::OnCloseShop() {
    /// CPrivateStoreDlg's Stop button: the window stays, the goods too.
    CPrivateStore& refStore = CPrivateStore::GetInstance();
    if (!refStore.IsOpened())
        return;
    refStore.Close();
    Sample();
}

void
RoseRmlPrivateStore::UpdateDragStart() {
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

    CPrivateStoreDlg* pDlg = StoreDlg();
    CIcon* pIcon = IconAt(iIndex);
    CDragItem* pDrag = NULL;
    if (pDlg != NULL)
        pDrag = (m_iTab == TAB_SELL) ? pDlg->GetSellDragItem() : pDlg->GetBuyDragItem();
    if (pIcon == NULL || pDrag == NULL)
        return;

    pDrag->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDrag);
}

void
RoseRmlPrivateStore::UpdateTooltip() {
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

    CIcon* pIcon = IconAt(iIndex);
    if (pIcon == NULL)
        return;

    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip, DLG_TYPE_PRIVATESTORE, 0);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlPrivateStore::PlaceDefault() {
    /// Beside the bag ( the shop skill opens it too ), on whichever side has
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
RoseRmlPrivateStore::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen && !IsInWorld())
        SetOpen(false);

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
