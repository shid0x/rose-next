#include "stdafx.h"

#include "RoseRmlSeparate.h"
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
#include "../gamedata/CSeparate.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CUIMediator.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Icon/CIconItem.h"
#include "../interface/Dlgs/CSeparateDlg.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>

namespace {

/// The four output rows ( CSeparateDlg's output slots ).
const int kOuts = 4;

/// Cell kinds ( slot-kind ).
const int KIND_INPUT = 0;
const int KIND_OUTPUT = 1;

Rml::String
Money(__int64 iMoney) {
    char szBuf[64];
    CGameUtil::ConvertMoney2String(iMoney, szBuf, sizeof(szBuf));
    return szBuf;
}

RoseRmlSeparate::OutVM
MakeOut(int iIndex) {
    RoseRmlSeparate::OutVM vm;
    vm.index = iIndex;
    vm.filled = false;
    return vm;
}

template<class T>
void
Set(T& member, const T& value, Rml::DataModelHandle& model, const char* pszName) {
    if (member != value) {
        member = value;
        model.DirtyVariable(pszName);
    }
}

} // namespace

RoseRmlSeparate::RoseRmlSeparate():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_bPress(false),
    m_iPressX(0),
    m_iPressY(0),
    m_iDropType(DLG_TYPE_SEPARATE),
    m_bHasItem(false),
    m_strCostLabel("MP"),
    m_strCost("0"),
    m_strHave("0"),
    m_bShort(false),
    m_bBusy(false),
    m_bCanStart(false) {}

bool
RoseRmlSeparate::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    for (int i = 0; i < kOuts; ++i)
        m_Outs.push_back(MakeOut(i));

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("separate");
    if (!constructor)
        return false;

    if (auto out = constructor.RegisterStruct<OutVM>()) {
        out.RegisterMember("index", &OutVM::index);
        out.RegisterMember("filled", &OutVM::filled);
        out.RegisterMember("src", &OutVM::src);
        out.RegisterMember("rect", &OutVM::rect);
        out.RegisterMember("name", &OutVM::name);
        out.RegisterMember("count", &OutVM::count);
    }
    constructor.RegisterArray<std::vector<OutVM>>();

    constructor.Bind("drop_type", &m_iDropType);
    constructor.Bind("where", &m_strWhere);
    constructor.Bind("has_item", &m_bHasItem);
    constructor.Bind("item_src", &m_strItemSrc);
    constructor.Bind("item_rect", &m_strItemRect);
    constructor.Bind("item_name", &m_strItemName);
    constructor.Bind("kind", &m_strKind);
    constructor.Bind("outs", &m_Outs);
    constructor.Bind("cost_label", &m_strCostLabel);
    constructor.Bind("cost", &m_strCost);
    constructor.Bind("have", &m_strHave);
    constructor.Bind("short", &m_bShort);
    constructor.Bind("busy", &m_bBusy);
    constructor.Bind("can_start", &m_bCanStart);

    /// press(kind, index)
    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.size() < 2 || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
            OnPress(args[0].Get<int>(), args[1].Get<int>());
        });
    /// take_out() -- double-click on the item: back to the bag.
    constructor.BindEventCallback("take_out",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            m_bPress = false;
            if (!CSeparate::GetInstance().IsWaitingReply())
                CSeparate::GetInstance().RemoveItem();
        });
    constructor.BindEventCallback("start",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            /// Greyed Break down: nothing to say ( the checks would repeat what
            /// the window already shows ); busy: already sent.
            if (!m_bCanStart || CSeparate::GetInstance().IsWaitingReply())
                return;
            if (CSeparateDlg* pDlg = Dlg())
                pDlg->Start();
        });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_SEPARATE);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "separate.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load break-down document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("separate");
    RoseRmlLayout::Track(m_pPanel, "separate");

    LOG_INFO("[rmlui] break-down document loaded");
    return true;
}

void
RoseRmlSeparate::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CSeparateDlg*
RoseRmlSeparate::Dlg() const {
    return (CSeparateDlg*)g_itMGR.FindDlg(DLG_TYPE_SEPARATE);
}

CIcon*
RoseRmlSeparate::IconAt(int iKind, int iIndex) const {
    CSeparateDlg* pDlg = Dlg();
    if (pDlg == NULL)
        return NULL;
    CSlot* pSlot = (iKind == KIND_INPUT) ? pDlg->GetMaterialSlot() : pDlg->GetOutputSlot(iIndex);
    return pSlot ? pSlot->GetIcon() : NULL;
}

bool
RoseRmlSeparate::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlSeparate::SetOpen(bool bOpen) {
    if (bOpen != m_bOpen) {
        RoseUi2::PlayWindowSound(DLG_TYPE_SEPARATE, bOpen);
        /// CSeparateDlg's Close button: the item goes back ( its fragment
        /// released, so the bag's copy is usable again ).
        if (!bOpen)
            CSeparate::GetInstance().RemoveItem();
    }
    m_bOpen = bOpen;
    m_bPress = false;
    if (bOpen)
        Sample();
}

void
RoseRmlSeparate::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_bPress = false;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlSeparate::Sample() {
    CSeparateDlg* pDlg = Dlg();
    if (pDlg == NULL || g_pAVATAR == NULL)
        return;
    CSeparate& Separate = CSeparate::GetInstance();
    const bool bNpc = Separate.GetType() == CSeparate::TYPE_NPC;

    /// Where: your skill, or the NPC's name.
    Rml::String strWhere = "With your skill";
    if (bNpc) {
        CObjCHAR* pNpc = g_pObjMGR->Get_ClientCharOBJ(Separate.GetNpcSvrIdx(), false);
        strWhere = pNpc ? "At " + RoseRmlText::FromGame(pNpc->Get_NAME()) : Rml::String("At an NPC");
    }
    Set(m_strWhere, strWhere, m_Model, "where");

    /// The item.
    CIcon* pIcon = IconAt(KIND_INPUT, 0);
    Rml::String strSrc, strRect, strName, strKind;
    int iModule = 0, iGraphic = 0;
    const bool bHasItem = pIcon != NULL && pIcon->IsItemIcon() && pIcon->GetSprite(iModule, iGraphic)
        && RoseRmlIcons::Resolve(iModule, iGraphic, strSrc, strRect);
    if (bHasItem) {
        strName = RoseRmlText::FromGame(pIcon->GetName());
        switch (Separate.GetBreakType()) {
            case CSeparate::TYPE_SEPARATE:
                strKind = "Takes its gem out";
                break;
            case CSeparate::TYPE_DECOMPOSITION:
                strKind = "Breaks it into materials";
                break;
            default:
                break;
        }
    }
    Set(m_bHasItem, bHasItem, m_Model, "has_item");
    Set(m_strItemSrc, strSrc, m_Model, "item_src");
    Set(m_strItemRect, strRect, m_Model, "item_rect");
    Set(m_strItemName, strName, m_Model, "item_name");
    Set(m_strKind, strKind, m_Model, "kind");

    /// What comes out.
    int iOuts = 0;
    std::vector<OutVM> outs;
    outs.reserve(kOuts);
    for (int i = 0; i < kOuts; ++i) {
        OutVM vm = MakeOut(i);
        CIcon* pOut = IconAt(KIND_OUTPUT, i);
        if (pOut != NULL && pOut->IsItemIcon() && pOut->GetSprite(iModule, iGraphic)
            && RoseRmlIcons::Resolve(iModule, iGraphic, vm.src, vm.rect)) {
            vm.filled = true;
            vm.name = RoseRmlText::FromGame(pOut->GetName());
            tagITEM& Item = ((CIconItem*)pOut)->GetItem();
            if (Item.IsEnableDupCNT()) {
                char szCount[32];
                _snprintf(szCount, sizeof(szCount), "x %u", Item.GetQuantity());
                szCount[sizeof(szCount) - 1] = '\0';
                vm.count = szCount;
            }
            ++iOuts;
        }
        outs.push_back(vm);
    }
    if (outs != m_Outs) {
        m_Outs.swap(outs);
        m_Model.DirtyVariable("outs");
    }

    /// What it costs: MP with the skill, zuly at an NPC ( the reply takes it ).
    __int64 iCost = 0, iHave = 0;
    if (bNpc) {
        iCost = Separate.GetRequireMoney();
        iHave = g_pAVATAR->Get_MONEY();
    } else {
        iCost = bHasItem ? Separate.GetRequireMp() : 0;
        iHave = g_pAVATAR->Get_MP();
    }
    Set(m_strCostLabel, Rml::String(bNpc ? "Zuly" : "MP"), m_Model, "cost_label");
    Set(m_strCost, Money(iCost), m_Model, "cost");
    Set(m_strHave, Money(iHave), m_Model, "have");
    const bool bShort = bHasItem && iCost > iHave;
    Set(m_bShort, bShort, m_Model, "short");

    const bool bBusy = Separate.IsWaitingReply();
    Set(m_bBusy, bBusy, m_Model, "busy");
    Set(m_bCanStart, bHasItem && iOuts > 0 && !bBusy && !bShort, m_Model, "can_start");
}

/// --- actions ----------------------------------------------------------------------

/// A left press on a cell: Alt previews, Shift links to chat; on the item, it
/// may become a drag back off ( CSeparateDlg's drag item ).
void
RoseRmlSeparate::OnPress(int iKind, int iIndex) {
    m_bPress = false;

    CIcon* pIcon = IconAt(iKind, iIndex);
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

    if (iKind == KIND_INPUT && !CSeparate::GetInstance().IsWaitingReply())
        m_bPress = true;
}

void
RoseRmlSeparate::UpdateDragStart() {
    if (!m_bPress)
        return;

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_bPress = false;
        return;
    }

    /// CSlot::Update's threshold: a two-hundredth of the screen.
    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const int iSlopX = max(3, g_pCApp->GetWIDTH() / 200);
    const int iSlopY = max(3, g_pCApp->GetHEIGHT() / 200);
    if (abs(ptMouse.x - m_iPressX) < iSlopX && abs(ptMouse.y - m_iPressY) < iSlopY)
        return;

    m_bPress = false;

    CSeparateDlg* pDlg = Dlg();
    CIcon* pIcon = IconAt(KIND_INPUT, 0);
    CDragItem* pDrag = pDlg ? pDlg->GetDragItem() : NULL;
    if (pIcon == NULL || pDrag == NULL)
        return;

    pDrag->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDrag);
}

void
RoseRmlSeparate::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    int iKind = -1, iIndex = -1;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("slot-kind")) {
            iKind = pEl->GetAttribute<int>("slot-kind", -1);
            iIndex = pEl->GetAttribute<int>("slot-index", 0);
            break;
        }
    }
    if (iKind < 0)
        return;

    CIcon* pIcon = IconAt(iKind, iIndex);
    if (pIcon == NULL)
        return;

    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip, DLG_TYPE_SEPARATE, 0);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlSeparate::PlaceDefault() {
    /// Beside the bag ( the skill and the NPC open it too ), on whichever
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
RoseRmlSeparate::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen) {
        if (!IsInWorld()) {
            SetOpen(false);
        } else if (CSeparate::GetInstance().GetType() == CSeparate::TYPE_NPC) {
            /// CSeparateDlg::Update ( which only ran while it was on screen ):
            /// the NPC gone or out of reach.
            CObjCHAR* pNpc =
                g_pObjMGR->Get_ClientCharOBJ(CSeparate::GetInstance().GetNpcSvrIdx(), false);
            if (!(pNpc && g_pAVATAR->IsInRANGE(pNpc, AVT_CLICK_EVENT_RANGE)))
                g_itMGR.CloseDialog(DLG_TYPE_SEPARATE);
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
