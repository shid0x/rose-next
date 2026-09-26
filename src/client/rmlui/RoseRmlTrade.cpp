#include "stdafx.h"

#include "RoseRmlTrade.h"
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
#include "../System/CGame.h"
#include "../misc/gameutil.h"
#include "../gamedata/CExchange.h"
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
#include "../interface/Command/CTCmdNumberInput.h"
#include "../interface/Command/uicommand.h"
#include "../interface/Dlgs/ExchangeDlg.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"

#include "net_prototype.h"
#include "rose/common/log.h"
#include "rose/io/stb.h"

#include <math.h>

namespace {

/// CTCmdAddMyMoney2Exchange's cap ( ctcmdnumberinput.cpp keeps it file-local;
/// the wire field is a DWORD ).
const __int64 kMaxTradeMoney = 1000000000;

Rml::String
Money(__int64 iMoney) {
    char szBuf[64];
    CGameUtil::ConvertMoney2String(iMoney, szBuf, sizeof(szBuf));
    return szBuf;
}

/// Set how much money is on our offer ( the quantity popup's answer ). The
/// classic window could only add ( from the bag ) or take back.
class CTCmdSetMyTradeMoney: public CTCmdNumberInput {
public:
    virtual bool Exec(CTObject*) {
        if (m_iNumber <= 0 || g_pAVATAR == NULL || CExchange::GetInstance().IsReadyMe())
            return true;
        __int64 iMoney = m_iNumber;
        if (iMoney > g_pAVATAR->Get_MONEY())
            iMoney = g_pAVATAR->Get_MONEY();
        if (iMoney > kMaxTradeMoney)
            iMoney = kMaxTradeMoney;
        CExchange::GetInstance().SetMyTradeMoney(iMoney);
        return true;
    }
};

/// A cell's picture from the classic slot behind it.
void
FillCell(RoseRmlTrade::CellVM& vm, CSlot* pSlot) {
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
    vm.count = pIcon->GetStackCount();

    if (Item.HasSocket()) {
        vm.socket = 1;
        const int iGem = Item.GetGemNO();
        if (iGem > 300 && iGem <= (int)g_TblGEMITEM.row_count
            && RoseRmlIcons::Resolve(
                IMAGE_RES_SOCKETJAM_ICON, GEMITEM_MARK_IMAGE(iGem), vm.gem_src, vm.gem_rect))
            vm.socket = 2;
    }
}

RoseRmlTrade::CellVM
MakeCell(int iKind, int iIndex) {
    RoseRmlTrade::CellVM vm;
    vm.kind = iKind;
    vm.index = iIndex;
    vm.filled = false;
    vm.count = 0;
    vm.socket = 0;
    return vm;
}

} // namespace

RoseRmlTrade::RoseRmlTrade():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_bDonePressed(false),
    m_pCmdSetMoney(new CTCmdSetMyTradeMoney),
    m_iPressKind(KIND_MINE),
    m_iPressIndex(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iDropType(DLG_TYPE_EXCHANGE),
    m_strMyMoney("0"),
    m_strTheirMoney("0"),
    m_bMeReady(false),
    m_bTheyReady(false),
    m_bDone(false),
    m_bChanged(false) {}

RoseRmlTrade::~RoseRmlTrade() {
    delete m_pCmdSetMoney;
    m_pCmdSetMoney = NULL;
}

bool
RoseRmlTrade::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    /// Every row exists from the start, empty, as the classic dialog's slots
    /// did: built while the document loads, not in the frame a trade opens.
    for (int i = 0; i < TOTAL_EXCHANGE_INVENTORY; ++i) {
        m_Mine.push_back(MakeCell(KIND_MINE, i));
        m_Theirs.push_back(MakeCell(KIND_THEIRS, i));
    }

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("trade");
    if (!constructor)
        return false;

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
    }
    constructor.RegisterArray<std::vector<CellVM>>();

    constructor.Bind("drop_type", &m_iDropType);
    constructor.Bind("me", &m_strMe);
    constructor.Bind("other", &m_strOther);
    constructor.Bind("mine", &m_Mine);
    constructor.Bind("theirs", &m_Theirs);
    constructor.Bind("my_money", &m_strMyMoney);
    constructor.Bind("their_money", &m_strTheirMoney);
    constructor.Bind("me_ready", &m_bMeReady);
    constructor.Bind("they_ready", &m_bTheyReady);
    constructor.Bind("done", &m_bDone);
    constructor.Bind("changed", &m_bChanged);

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
    constructor.BindEventCallback("ready",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnReady(); });
    constructor.BindEventCallback("trade",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnTrade(); });
    constructor.BindEventCallback("cancel",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnCancel(); });
    constructor.BindEventCallback("set_money",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnSetMoney(); });
    constructor.BindEventCallback("clear_money",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnClearMoney(); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "trade.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load trade document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("trade");
    RoseRmlLayout::Track(m_pPanel, "trade");

    LOG_INFO("[rmlui] trade document loaded");
    return true;
}

void
RoseRmlTrade::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CExchangeDLG*
RoseRmlTrade::ExchangeDlg() const {
    return g_itMGR.GetExchangeDLG();
}

CSlot*
RoseRmlTrade::SlotFor(int iKind, int iIndex) const {
    CExchangeDLG* pDlg = ExchangeDlg();
    if (pDlg == NULL)
        return NULL;
    return (iKind == KIND_MINE) ? pDlg->GetMySlot(iIndex) : pDlg->GetOtherSlot(iIndex);
}

CDragItem*
RoseRmlTrade::DragItemFor(int iKind) const {
    CExchangeDLG* pDlg = ExchangeDlg();
    if (pDlg == NULL)
        return NULL;
    return (iKind == KIND_MINE) ? pDlg->GetMyDragItem() : pDlg->GetOtherDragItem();
}

bool
RoseRmlTrade::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlTrade::SetOpen(bool bOpen) {
    if (bOpen) {
        if (!m_bOpen)
            RoseUi2::PlayWindowSound(DLG_TYPE_EXCHANGE, true);
        m_bOpen = true;
        m_bDonePressed = false;
        m_bChanged = false;
        m_Model.DirtyVariable("changed");
        m_iPressIndex = -1;
        Sample(); /// no stale offer on the first frame
        return;
    }

    if (!m_bOpen)
        return;
    m_bOpen = false;
    m_iPressIndex = -1;
    RoseUi2::PlayWindowSound(DLG_TYPE_EXCHANGE, false);

    /// CExchangeDLG::Hide's job: the trade is over on this side.
    CExchange::GetInstance().EndExchange();

    /// A "how much?" asked for this trade is moot.
    if (g_itMGR.IsDlgOpened(DLG_TYPE_N_INPUT))
        g_itMGR.CloseDialog(DLG_TYPE_N_INPUT);
}

bool
RoseRmlTrade::OfferChangedWhileReady() {
    if (!m_bOpen || !RoseUi2::IsActive())
        return false;

    CExchange& Exchange = CExchange::GetInstance();
    if (Exchange.IsReadyMe()) {
        Exchange.SendTradePacket(RESULT_TRADE_UNCHECK_READY);
        Exchange.SetReadyMe(false);
    }
    m_bDonePressed = false;
    m_bChanged = true;
    m_Model.DirtyVariable("changed");
    return true;
}

void
RoseRmlTrade::SetVisible(bool bVisible) {
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
RoseRmlTrade::Sample() {
    CExchangeDLG* pDlg = ExchangeDlg();
    if (g_pAVATAR == NULL || pDlg == NULL)
        return;

    CExchange& Exchange = CExchange::GetInstance();

    const Rml::String strMe = RoseRmlText::FromGame(g_pAVATAR->Get_NAME());
    const Rml::String strOther = RoseRmlText::FromGame(Exchange.GetOtherName().c_str());
    if (strMe != m_strMe) {
        m_strMe = strMe;
        m_Model.DirtyVariable("me");
    }
    if (strOther != m_strOther) {
        m_strOther = strOther;
        m_Model.DirtyVariable("other");
    }

    std::vector<CellVM> mine, theirs;
    for (int i = 0; i < TOTAL_EXCHANGE_INVENTORY; ++i) {
        CellVM a = MakeCell(KIND_MINE, i);
        FillCell(a, pDlg->GetMySlot(i));
        mine.push_back(a);

        CellVM b = MakeCell(KIND_THEIRS, i);
        FillCell(b, pDlg->GetOtherSlot(i));
        theirs.push_back(b);
    }
    if (mine != m_Mine) {
        m_Mine.swap(mine);
        m_Model.DirtyVariable("mine");
    }
    if (theirs != m_Theirs) {
        m_Theirs.swap(theirs);
        m_Model.DirtyVariable("theirs");
    }

    const Rml::String strMyMoney = Money(Exchange.GetMyTradeMoney());
    const Rml::String strTheirMoney = Money(Exchange.GetOtherTradeMoney());
    if (strMyMoney != m_strMyMoney) {
        m_strMyMoney = strMyMoney;
        m_Model.DirtyVariable("my_money");
    }
    if (strTheirMoney != m_strTheirMoney) {
        m_strTheirMoney = strTheirMoney;
        m_Model.DirtyVariable("their_money");
    }

    /// Trade pressed stands only while both sides stay ready: an Unready
    /// from either clears it on the server as well.
    if (!Exchange.IsReadyAll())
        m_bDonePressed = false;

    if (Exchange.IsReadyMe() != m_bMeReady) {
        m_bMeReady = Exchange.IsReadyMe();
        m_Model.DirtyVariable("me_ready");
    }
    if (Exchange.IsReadyOther() != m_bTheyReady) {
        m_bTheyReady = Exchange.IsReadyOther();
        m_Model.DirtyVariable("they_ready");
    }
    if (m_bDonePressed != m_bDone) {
        m_bDone = m_bDonePressed;
        m_Model.DirtyVariable("done");
    }
}

/// --- actions ----------------------------------------------------------------------

void
RoseRmlTrade::OnReady() {
    CExchange& Exchange = CExchange::GetInstance();
    if (!Exchange.IsExchange())
        return;

    if (!Exchange.IsReadyMe()) {
        /// CExchangeDLG's OK button.
        Exchange.SendTradePacket(RESULT_TRADE_CHECK_READY);
        Exchange.SetReadyMe(true);
        if (m_bChanged) {
            m_bChanged = false;
            m_Model.DirtyVariable("changed");
        }
    } else {
        /// Taken back: the server clears our ready and done bits, and the
        /// other side's done bit, and tells the other side.
        Exchange.SendTradePacket(RESULT_TRADE_UNCHECK_READY);
        Exchange.SetReadyMe(false);
        m_bDonePressed = false;
    }
    Sample();
}

void
RoseRmlTrade::OnTrade() {
    CExchange& Exchange = CExchange::GetInstance();
    if (!Exchange.IsReadyAll() || m_bDonePressed)
        return;
    /// CExchangeDLG's Exchange button. The server completes the trade once
    /// both sides have sent it.
    Exchange.SendTradePacket(RESULT_TRADE_DONE);
    m_bDonePressed = true;
    Sample();
}

void
RoseRmlTrade::OnCancel() {
    /// CExchangeDLG's close button: tell the other side, then close.
    if (CExchange::GetInstance().IsExchange())
        CExchange::GetInstance().SendTradePacket(RESULT_TRADE_CANCEL);
    g_itMGR.CloseDialog(DLG_TYPE_EXCHANGE);
}

void
RoseRmlTrade::OnSetMoney() {
    if (g_pAVATAR == NULL || CExchange::GetInstance().IsReadyMe())
        return;
    __int64 iMax = g_pAVATAR->Get_MONEY();
    if (iMax > kMaxTradeMoney)
        iMax = kMaxTradeMoney;
    if (iMax <= 0)
        return;

    CTCmdOpenNumberInputDlg OpenCmd;
    OpenCmd.SetCommand(m_pCmdSetMoney);
    OpenCmd.SetMaximum(iMax);
    OpenCmd.Exec(NULL);
}

void
RoseRmlTrade::OnClearMoney() {
    CExchange& Exchange = CExchange::GetInstance();
    if (Exchange.IsReadyMe() || Exchange.GetMyTradeMoney() <= 0)
        return;
    Exchange.SetMyTradeMoney(0);
}

/// --- input ------------------------------------------------------------------------

/// A left press on a cell, with CSlot::Process's precedence: Alt previews,
/// Shift links, Ctrl asks for the wishlist ( their items ); otherwise it may
/// become a drag.
void
RoseRmlTrade::OnPress(int iKind, int iIndex) {
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

    if (iKind == KIND_THEIRS && GetAsyncKeyState(VK_CONTROL) < 0
        && pIcon->Process(WM_LBUTTONDOWN, MK_CONTROL, 0))
        return;

    if (pIcon->IsEnable()) {
        m_iPressKind = iKind;
        m_iPressIndex = iIndex;
    }
}

void
RoseRmlTrade::OnUse(int iKind, int iIndex) {
    /// Our item comes back ( its icon's command, CTCmdRemoveMyItemFromExchange,
    /// which refuses while we are ready ); theirs does nothing.
    if (iKind != KIND_MINE)
        return;
    CSlot* pSlot = SlotFor(iKind, iIndex);
    if (pSlot && pSlot->GetIcon())
        pSlot->GetIcon()->ExecuteCommand();
}

void
RoseRmlTrade::UpdateDragStart() {
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
RoseRmlTrade::UpdateTooltip() {
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

    /// The classic slots' parent: full stats, which matter most for what the
    /// other side is offering.
    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip, DLG_TYPE_EXCHANGE, 0);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlTrade::PlaceDefault() {
    /// Beside the inventory ( accepting a trade opens it too ), on whichever
    /// side has room -- only while no position is set ( dragged, saved or
    /// reset ).
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
RoseRmlTrade::Update() {
    if (m_pDocument == NULL)
        return;

    /// Leaving the world ends the trade here, as the classic dialog's Hide did
    /// when the state change closed every dialog.
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
