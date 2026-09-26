#include "stdafx.h"

#include "RoseRmlStorage.h"
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
#include "../gamedata/CBank.h"
#include "../gamecommon/item.h"
#include "../Network/CNetwork.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CUIMediator.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/typeresource.h"
#include "../interface/Icon/CIconItem.h"
#include "../interface/Command/CTCmdNumberInput.h"
#include "../interface/Command/uicommand.h"
#include "../interface/Dlgs/CBankDlg.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"

#include "rose/common/log.h"
#include "rose/io/stb.h"

#include <math.h>

namespace {

/// CBankDlg::Update's reach: past this ( cm ) from the storage keeper it closes.
const float kCloseDistance = 1000.0f;

/// The Platinum tab ( CTCmdMoveItemInv2Bank: tab 3 ).
const int kPlatinumTab = 3;

Rml::String
Money(__int64 iMoney) {
    char szBuf[64];
    CGameUtil::ConvertMoney2String(iMoney, szBuf, sizeof(szBuf));
    return szBuf;
}

/// CBankWindowDlg's OK, one command per direction ( the quantity popup runs it ).
class CTCmdDepositZuly: public CTCmdNumberInput {
public:
    virtual bool Exec(CTObject*) {
        if (m_iNumber <= 0 || g_pAVATAR == NULL || g_pNet == NULL)
            return true;
        if (m_iNumber <= g_pAVATAR->Get_MONEY())
            g_pNet->Send_cli_MOVE_ZULY_INV2BANK(m_iNumber);
        else
            g_itMGR.OpenMsgBox(STR_BANK_SAVE_FAIL);
        return true;
    }
};

class CTCmdWithdrawZuly: public CTCmdNumberInput {
public:
    virtual bool Exec(CTObject*) {
        if (m_iNumber <= 0 || g_pNet == NULL)
            return true;
        if (m_iNumber <= CBank::GetInstance().GetMoney())
            g_pNet->Send_cli_MOVE_ZULY_BANK2INV(m_iNumber);
        else
            g_itMGR.OpenMsgBox(STR_BANK_WITHDRAW_FAIL);
        return true;
    }
};

/// A cell's picture from the classic slot behind it.
void
FillCell(RoseRmlStorage::CellVM& vm, CSlot* pSlot) {
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

RoseRmlStorage::CellVM
MakeCell(int iIndex) {
    RoseRmlStorage::CellVM vm;
    vm.index = iIndex;
    vm.filled = false;
    vm.count = 0;
    vm.socket = 0;
    return vm;
}

} // namespace

RoseRmlStorage::RoseRmlStorage():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_pCmdWithdrawItem(new CTCmdMoveItemBank2Inv),
    m_pCmdOpenWithdrawItem(new CTCmdOpenNumberInputDlg),
    m_pCmdDeposit(new CTCmdDepositZuly),
    m_pCmdWithdraw(new CTCmdWithdrawZuly),
    m_iPressIndex(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iDropType(DLG_TYPE_BANK),
    m_iTab(0),
    m_strMoney("0"),
    m_bPlatinumLocked(false) {
    m_pCmdOpenWithdrawItem->SetCommand(m_pCmdWithdrawItem);
}

RoseRmlStorage::~RoseRmlStorage() {
    delete m_pCmdOpenWithdrawItem;
    delete m_pCmdWithdrawItem;
    delete m_pCmdDeposit;
    delete m_pCmdWithdraw;
}

bool
RoseRmlStorage::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    /// Every cell exists from the start, empty: built while the document loads,
    /// not in the frame the storage first opens.
    for (int i = 0; i < g_iSlotCountPerPage; ++i)
        m_Cells.push_back(MakeCell(i));

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("storage");
    if (!constructor)
        return false;

    if (auto tab = constructor.RegisterStruct<TabVM>()) {
        tab.RegisterMember("index", &TabVM::index);
        tab.RegisterMember("name", &TabVM::name);
        tab.RegisterMember("count", &TabVM::count);
    }
    constructor.RegisterArray<std::vector<TabVM>>();

    if (auto cell = constructor.RegisterStruct<CellVM>()) {
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
    constructor.Bind("title", &m_strTitle);
    constructor.Bind("tab", &m_iTab);
    constructor.Bind("tabs", &m_Tabs);
    constructor.Bind("cells", &m_Cells);
    constructor.Bind("money", &m_strMoney);
    constructor.Bind("deposit_label", &m_strDeposit);
    constructor.Bind("withdraw_label", &m_strWithdraw);
    constructor.Bind("platinum_locked", &m_bPlatinumLocked);
    constructor.Bind("platinum_note", &m_strPlatinumNote);

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
    /// use(index) -- double-click: out to the bag.
    constructor.BindEventCallback("use",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            m_iPressIndex = -1;
            OnUse(args[0].Get<int>());
        });
    constructor.BindEventCallback("deposit",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnMoney(true); });
    constructor.BindEventCallback("withdraw",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnMoney(false); });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_BANK);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "storage.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load storage document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("storage");
    RoseRmlLayout::Track(m_pPanel, "storage");

    LOG_INFO("[rmlui] storage document loaded");
    return true;
}

void
RoseRmlStorage::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CBankDlg*
RoseRmlStorage::BankDlg() const {
    return (CBankDlg*)g_itMGR.FindDlg(DLG_TYPE_BANK);
}

CSlot*
RoseRmlStorage::SlotFor(int iIndex) const {
    CBankDlg* pDlg = BankDlg();
    return pDlg ? pDlg->GetSlot(m_iTab, iIndex) : NULL;
}

bool
RoseRmlStorage::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlStorage::SetOpen(bool bOpen) {
    if (bOpen != m_bOpen)
        RoseUi2::PlayWindowSound(DLG_TYPE_BANK, bOpen);
    if (bOpen && !m_bOpen)
        SetTab(0); /// as CBankDlg::Show
    m_bOpen = bOpen;
    m_iPressIndex = -1;
    if (bOpen)
        Sample(); /// no stale shelf on the first frame
    else if (g_itMGR.IsDlgOpened(DLG_TYPE_N_INPUT))
        g_itMGR.CloseDialog(DLG_TYPE_N_INPUT); /// a "how much?" for storage is moot
}

void
RoseRmlStorage::SetVisible(bool bVisible) {
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
RoseRmlStorage::SetTab(int iTab) {
    if (iTab < 0 || iTab >= g_iPageCount)
        return;
    /// The hidden dialog's tab too: the deposit command reads it there.
    if (CBankDlg* pDlg = BankDlg())
        pDlg->SetCurrentTab(iTab);
    if (iTab != m_iTab) {
        m_iTab = iTab;
        m_iPressIndex = -1;
        m_Model.DirtyVariable("tab");
    }
    Sample();
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlStorage::Sample() {
    CBankDlg* pDlg = BankDlg();
    if (pDlg == NULL)
        return;

    /// Title and labels in the game's words ( F_STR_BANK_TITLE: "%s's Storage",
    /// the account's ).
    const Rml::String strTitle =
        RoseRmlText::FromGame(CStr::Printf(F_STR_BANK_TITLE, g_GameDATA.username.c_str()));
    if (strTitle != m_strTitle) {
        m_strTitle = strTitle;
        m_Model.DirtyVariable("title");
    }
    const Rml::String strDeposit = RoseRmlText::FromGame(STR_SAVE);
    const Rml::String strWithdraw = RoseRmlText::FromGame(STR_WITHDRAW);
    if (strDeposit != m_strDeposit || strWithdraw != m_strWithdraw) {
        m_strDeposit = strDeposit;
        m_strWithdraw = strWithdraw;
        m_Model.DirtyVariable("deposit_label");
        m_Model.DirtyVariable("withdraw_label");
    }

    /// Tabs: CBankDlg::Create's names ( "Storage1".."Storage3", "Platinum" ),
    /// each with how full it is.
    std::vector<TabVM> tabs;
    for (int t = 0; t < g_iPageCount; ++t) {
        TabVM vm;
        vm.index = t;
        vm.name = (t == kPlatinumTab) ? RoseRmlText::FromGame(STR_PLATINUM)
                                      : RoseRmlText::FromGame(CStr::Printf("%s%d", STR_BANK, t + 1));
        vm.count = 0;
        for (int i = 0; i < g_iSlotCountPerPage; ++i) {
            CSlot* pSlot = pDlg->GetSlot(t, i);
            if (pSlot && pSlot->GetIcon())
                ++vm.count;
        }
        tabs.push_back(vm);
    }
    if (tabs != m_Tabs) {
        m_Tabs.swap(tabs);
        m_Model.DirtyVariable("tabs");
    }

    std::vector<CellVM> cells;
    cells.reserve(g_iSlotCountPerPage);
    for (int i = 0; i < g_iSlotCountPerPage; ++i) {
        CellVM vm = MakeCell(i);
        FillCell(vm, pDlg->GetSlot(m_iTab, i));
        cells.push_back(vm);
    }
    if (cells != m_Cells) {
        m_Cells.swap(cells);
        m_Model.DirtyVariable("cells");
    }

    const Rml::String strMoney = Money(CBank::GetInstance().GetMoney());
    if (strMoney != m_strMoney) {
        m_strMoney = strMoney;
        m_Model.DirtyVariable("money");
    }

    /// Only Platinum members can store on the Platinum tab
    /// ( CTCmdMoveItemInv2Bank refuses the rest ); say so on the tab.
    const bool bLocked = m_iTab == kPlatinumTab
        && CGame::GetInstance().GetPayType() != CGame::PAY_PLATINUM;
    if (bLocked != m_bPlatinumLocked) {
        m_bPlatinumLocked = bLocked;
        m_Model.DirtyVariable("platinum_locked");
    }
    if (bLocked) {
        const Rml::String strNote = RoseRmlText::FromGame(STR_ONLY_PLATINUM_TAB_BANK);
        if (strNote != m_strPlatinumNote) {
            m_strPlatinumNote = strNote;
            m_Model.DirtyVariable("platinum_note");
        }
    }
}

/// --- actions ----------------------------------------------------------------------

void
RoseRmlStorage::OnMoney(bool bDeposit) {
    if (g_pAVATAR == NULL)
        return;
    const __int64 iMax = bDeposit ? g_pAVATAR->Get_MONEY() : CBank::GetInstance().GetMoney();
    if (iMax <= 0)
        return;

    CTCmdOpenNumberInputDlg OpenCmd;
    OpenCmd.SetCommand(bDeposit ? m_pCmdDeposit : m_pCmdWithdraw);
    OpenCmd.SetMaximum(iMax);
    OpenCmd.Exec(NULL);
}

/// A left press on a cell: Alt previews, Shift links to chat; otherwise it
/// may become a drag ( CBankDlg's own, to the bag ).
void
RoseRmlStorage::OnPress(int iIndex) {
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

    if (pIcon->IsEnable())
        m_iPressIndex = iIndex;
}

void
RoseRmlStorage::OnUse(int iIndex) {
    /// The drag's own command ( how many? -> CTCmdMoveItemBank2Inv ), run on
    /// the stored item. The classic icon had no command: a double-click did
    /// nothing.
    CSlot* pSlot = SlotFor(iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;
    if (CItem* pItem = ((CIconItem*)pIcon)->GetCItem())
        m_pCmdOpenWithdrawItem->Exec(pItem);
}

void
RoseRmlStorage::UpdateDragStart() {
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

    CBankDlg* pDlg = BankDlg();
    CSlot* pSlot = SlotFor(iIndex);
    CIcon* pIcon = pSlot ? pSlot->GetIcon() : NULL;
    CDragItem* pDrag = pDlg ? pDlg->GetDragItem() : NULL;
    if (pIcon == NULL || pDrag == NULL)
        return;

    pDrag->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDrag);
}

void
RoseRmlStorage::UpdateTooltip() {
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

    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip, DLG_TYPE_BANK, 0);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlStorage::PlaceDefault() {
    /// Beside the bag ( the storage opens it too ), on whichever side has
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
RoseRmlStorage::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen) {
        if (!IsInWorld()) {
            SetOpen(false);
        } else {
            /// CBankDlg::Update: the storage keeper gone or too far.
            CObjCHAR* pNpc =
                g_pObjMGR->Get_CharOBJ(CBank::GetInstance().GetNpcClientIndex(), false);
            if (pNpc == NULL || g_pAVATAR->Get_DISTANCE(pNpc) >= kCloseDistance)
                g_itMGR.CloseDialog(DLG_TYPE_BANK);
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
