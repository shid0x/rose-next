#include "stdafx.h"

#include "RoseRmlUpgrade.h"
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
#include "../gamedata/CUpgrade.h"
#include "../Network/CNetwork.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CUIMediator.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/typeresource.h"
#include "../interface/Icon/CIconItem.h"
#include "../interface/command/uicommand.h"
#include "../interface/Dlgs/CUpgradeDlg.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

/// CUpgradeDlgStateResult::kRefineDurationMs.
const DWORD kBarMs = 5000;

/// Cell kinds ( slot-kind ).
const int KIND_TARGET = 0;
const int KIND_MATERIAL = 1;

const int kMats = 3;

Rml::String
Money(__int64 iMoney) {
    char szBuf[64];
    CGameUtil::ConvertMoney2String(iMoney, szBuf, sizeof(szBuf));
    return szBuf;
}

Rml::String
Printf(const char* pszFormat, int i) {
    char szBuf[64];
    _snprintf(szBuf, sizeof(szBuf), pszFormat, i);
    szBuf[sizeof(szBuf) - 1] = '\0';
    return szBuf;
}

RoseRmlUpgrade::MatVM
MakeMat(int iIndex) {
    RoseRmlUpgrade::MatVM vm;
    vm.index = iIndex;
    vm.needed = false;
    vm.placed = false;
    vm.missing = false;
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

RoseRmlUpgrade::RoseRmlUpgrade():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iPressKind(-1),
    m_iPressIndex(0),
    m_iPressX(0),
    m_iPressY(0),
    m_iPhase(PHASE_NONE),
    m_dwBarStart(0),
    m_iBarTarget(0),
    m_iBarMark(0),
    m_bSuccess(false),
    m_iDropType(DLG_TYPE_UPGRADE),
    m_bHasTarget(false),
    m_strChance("-"),
    m_strCostLabel("MP"),
    m_strCost("0"),
    m_strHave("0"),
    m_bShort(false),
    m_iState(CUpgradeDlg::STATE_NORMAL),
    m_bCanStart(false),
    m_bShowBar(false),
    m_strFill("scaleX(0)"),
    m_strMark("0%"),
    m_bMarkHigh(false),
    m_bGreen(false),
    m_bOutcomeGood(false) {}

bool
RoseRmlUpgrade::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    for (int i = 0; i < kMats; ++i)
        m_Mats.push_back(MakeMat(i));

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("upgrade");
    if (!constructor)
        return false;

    if (auto mat = constructor.RegisterStruct<MatVM>()) {
        mat.RegisterMember("index", &MatVM::index);
        mat.RegisterMember("needed", &MatVM::needed);
        mat.RegisterMember("placed", &MatVM::placed);
        mat.RegisterMember("missing", &MatVM::missing);
        mat.RegisterMember("src", &MatVM::src);
        mat.RegisterMember("rect", &MatVM::rect);
        mat.RegisterMember("name", &MatVM::name);
        mat.RegisterMember("count", &MatVM::count);
    }
    constructor.RegisterArray<std::vector<MatVM>>();

    constructor.Bind("drop_type", &m_iDropType);
    constructor.Bind("where", &m_strWhere);
    constructor.Bind("has_target", &m_bHasTarget);
    constructor.Bind("target_src", &m_strTargetSrc);
    constructor.Bind("target_rect", &m_strTargetRect);
    constructor.Bind("target_name", &m_strTargetName);
    constructor.Bind("grade", &m_strGrade);
    constructor.Bind("mats", &m_Mats);
    constructor.Bind("chance", &m_strChance);
    constructor.Bind("cost_label", &m_strCostLabel);
    constructor.Bind("cost", &m_strCost);
    constructor.Bind("have", &m_strHave);
    constructor.Bind("short", &m_bShort);
    constructor.Bind("state", &m_iState);
    constructor.Bind("can_start", &m_bCanStart);
    constructor.Bind("show_bar", &m_bShowBar);
    constructor.Bind("fill", &m_strFill);
    constructor.Bind("mark", &m_strMark);
    constructor.Bind("mark_high", &m_bMarkHigh);
    constructor.Bind("green", &m_bGreen);
    constructor.Bind("outcome", &m_strOutcome);
    constructor.Bind("outcome_good", &m_bOutcomeGood);

    /// press(kind, index)
    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.size() < 2 || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
            OnPress(args[0].Get<int>(), args[1].Get<int>());
        });
    /// use(kind, index) -- double-click: back to the bag.
    constructor.BindEventCallback("use",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.size() < 2)
                return;
            m_iPressKind = -1;
            OnUse(args[0].Get<int>(), args[1].Get<int>());
        });
    constructor.BindEventCallback("start",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            if (!m_bCanStart || IsBusy())
                return;
            if (CUpgradeDlg* pDlg = Dlg())
                pDlg->Start();
        });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_UPGRADE);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "upgrade.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load upgrade document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("upgrade");
    RoseRmlLayout::Track(m_pPanel, "upgrade");

    LOG_INFO("[rmlui] upgrade document loaded");
    return true;
}

void
RoseRmlUpgrade::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CUpgradeDlg*
RoseRmlUpgrade::Dlg() const {
    return (CUpgradeDlg*)g_itMGR.FindDlg(DLG_TYPE_UPGRADE);
}

CIcon*
RoseRmlUpgrade::IconAt(int iKind, int iIndex) const {
    CUpgradeDlg* pDlg = Dlg();
    if (pDlg == NULL)
        return NULL;
    CSlot* pSlot = (iKind == KIND_TARGET) ? pDlg->GetTargetSlot() : pDlg->GetMaterialSlot(iIndex);
    return pSlot ? pSlot->GetIcon() : NULL;
}

bool
RoseRmlUpgrade::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

/// A request sent, or a result not yet in the bag.
bool
RoseRmlUpgrade::IsBusy() const {
    CUpgradeDlg* pDlg = Dlg();
    return pDlg != NULL && pDlg->GetState() != CUpgradeDlg::STATE_NORMAL;
}

void
RoseRmlUpgrade::SetOpen(bool bOpen) {
    if (!bOpen && m_bOpen && IsBusy() && IsInWorld())
        return; /// the result must reach the bag first ( see the header )

    if (bOpen != m_bOpen) {
        RoseUi2::PlayWindowSound(DLG_TYPE_UPGRADE, bOpen);
        if (!bOpen) {
            /// Leaving the world with a result pending: apply it now.
            CUpgradeDlg* pDlg = Dlg();
            if (pDlg != NULL && pDlg->GetState() == CUpgradeDlg::STATE_RESULT)
                pDlg->ChangeState(CUpgradeDlg::STATE_NORMAL);
            /// CUpgradeDlg::Hide: the items go back to the bag.
            CUpgrade::GetInstance().RemoveTargetItem();
            m_iPhase = PHASE_NONE;
        }
    }
    m_bOpen = bOpen;
    m_iPressKind = -1;
    if (bOpen)
        Sample();
}

void
RoseRmlUpgrade::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_iPressKind = -1;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlUpgrade::Sample() {
    CUpgradeDlg* pDlg = Dlg();
    if (pDlg == NULL || g_pAVATAR == NULL)
        return;
    CUpgrade& Upgrade = CUpgrade::GetInstance();
    const bool bNpc = Upgrade.GetType() == CUpgrade::TYPE_NPC;
    const int iState = pDlg->GetState();
    Set(m_iState, iState, m_Model, "state");
    Set(m_iDropType, iState == CUpgradeDlg::STATE_NORMAL ? (int)DLG_TYPE_UPGRADE : 0, m_Model, "drop_type");

    Rml::String strWhere = "With your skill";
    if (bNpc) {
        CObjCHAR* pNpc = g_pObjMGR->Get_ClientCharOBJ(Upgrade.GetNpcSvrIdx(), false);
        strWhere = pNpc ? "At " + RoseRmlText::FromGame(pNpc->Get_NAME()) : Rml::String("At an NPC");
    }
    Set(m_strWhere, strWhere, m_Model, "where");

    /// The item to refine, and the grade it goes to.
    CIcon* pIcon = IconAt(KIND_TARGET, 0);
    Rml::String strSrc, strRect, strName, strGrade;
    int iModule = 0, iGraphic = 0;
    const bool bHasTarget = pIcon != NULL && pIcon->IsItemIcon()
        && pIcon->GetSprite(iModule, iGraphic) && RoseRmlIcons::Resolve(iModule, iGraphic, strSrc, strRect);
    if (bHasTarget) {
        strName = RoseRmlText::FromGame(Upgrade.GetTargetItemName() ? Upgrade.GetTargetItemName()
                                                                    : pIcon->GetName());
        const int iGrade = ((CIconItem*)pIcon)->GetItem().GetGrade();
        strGrade = Printf("+%d", iGrade) + " -> " + Printf("+%d", iGrade + 1);
    }
    Set(m_bHasTarget, bHasTarget, m_Model, "has_target");
    Set(m_strTargetSrc, strSrc, m_Model, "target_src");
    Set(m_strTargetRect, strRect, m_Model, "target_rect");
    Set(m_strTargetName, strName, m_Model, "target_name");
    Set(m_strGrade, strGrade, m_Model, "grade");

    /// What it takes: a row per requirement, filled once placed.
    bool bAllPlaced = true;
    std::vector<MatVM> mats;
    mats.reserve(kMats);
    for (int i = 0; i < kMats; ++i) {
        MatVM vm = MakeMat(i);
        const char* pszNeed = bHasTarget ? Upgrade.GetRequireMaterialName(i) : NULL;
        if (pszNeed != NULL && pszNeed[0] != '\0') {
            vm.needed = true;
            vm.name = RoseRmlText::FromGame(pszNeed);
            vm.count = Printf("x %d", Upgrade.GetRequireMaterialCount(i));
            CIcon* pMat = IconAt(KIND_MATERIAL, i);
            if (pMat != NULL && pMat->IsItemIcon() && pMat->GetSprite(iModule, iGraphic)
                && RoseRmlIcons::Resolve(iModule, iGraphic, vm.src, vm.rect))
                vm.placed = true;
            if (!vm.placed) {
                vm.missing = true;
                bAllPlaced = false;
            }
        }
        mats.push_back(vm);
    }
    if (mats != m_Mats) {
        m_Mats.swap(mats);
        m_Model.DirtyVariable("mats");
    }

    /// The chance ( CUpgrade computes it once the first material is in ).
    const int iChance = Upgrade.GetSuccessProb();
    Set(m_strChance, (bHasTarget && iChance > 0) ? Printf("%d%%", iChance) : Rml::String("-"), m_Model, "chance");

    /// The cost: MP with the skill ( known once the first material is in ),
    /// zuly at an NPC.
    __int64 iCost = 0, iHave = 0;
    if (bNpc) {
        iCost = bHasTarget ? Upgrade.GetRequireMoney() : 0;
        iHave = g_pAVATAR->Get_MONEY();
    } else {
        iCost = bHasTarget ? Upgrade.GetRequireMp() : 0;
        iHave = g_pAVATAR->Get_MP();
    }
    Set(m_strCostLabel, Rml::String(bNpc ? "Zuly" : "MP"), m_Model, "cost_label");
    Set(m_strCost, Money(iCost), m_Model, "cost");
    Set(m_strHave, Money(iHave), m_Model, "have");
    const bool bShort = bHasTarget && iCost > iHave;
    Set(m_bShort, bShort, m_Model, "short");

    Set(m_bCanStart,
        iState == CUpgradeDlg::STATE_NORMAL && bHasTarget && bAllPlaced && !bShort,
        m_Model,
        "can_start");
}

/// --- the result --------------------------------------------------------------------

void
RoseRmlUpgrade::BeginResult() {
    /// CUpgradeDlgStateResult::Enter's numbers: on success the bar ends at
    /// the server's value and turns green a little before; on a failure it
    /// stops short of the green.
    CUpgrade& Upgrade = CUpgrade::GetInstance();
    m_bSuccess = Upgrade.GetResult() == CRAFT_UPGRADE_SUCCESS;
    if (m_bSuccess) {
        int iSuccess = Upgrade.GetResultSuccessProb() / 10;
        if (iSuccess <= 30)
            iSuccess = 31;
        if (iSuccess >= 100)
            iSuccess = 100;
        m_iBarMark = iSuccess - (rand() % 30);
        m_iBarTarget = iSuccess;
    } else {
        m_iBarMark = rand() % 50 + 30;
        m_iBarTarget = m_iBarMark - (rand() % 20);
    }
    m_dwBarStart = GetTickCount();
    m_iPhase = PHASE_BAR;

    Set(m_bShowBar, true, m_Model, "show_bar");
    Set(m_strMark, Printf("%d%%", m_iBarMark), m_Model, "mark");
    Set(m_bMarkHigh, m_iBarMark > 70, m_Model, "mark_high");
    Set(m_strFill, Rml::String("scaleX(0)"), m_Model, "fill");
    Set(m_bGreen, false, m_Model, "green");
    Set(m_strOutcome, Rml::String(), m_Model, "outcome");
}

/// The bar is full: the outcome in the window, the report ( the effect on
/// the avatar ), and the box whose OK puts the result in the bag.
void
RoseRmlUpgrade::FinishResult() {
    m_iPhase = PHASE_DONE;
    CUpgrade& Upgrade = CUpgrade::GetInstance();

    /// The new grade, from what the server put in the bag.
    Rml::String strOutcome = m_bSuccess ? "Success" : "Failed";
    if (CItemFragment* pTarget = Upgrade.GetTargetItem()) {
        const std::list<tag_SET_INVITEM>& Result = Upgrade.GetResultItemSet();
        for (std::list<tag_SET_INVITEM>::const_iterator it = Result.begin(); it != Result.end(); ++it) {
            if (it->m_btInvIDX != pTarget->GetIndex())
                continue;
            tagITEM Item = it->m_ITEM;
            if (Item.IsEmpty())
                strOutcome = "Failed: the item was destroyed";
            else
                strOutcome += Printf(": now +%d", Item.GetGrade());
            break;
        }
    }
    Set(m_strOutcome, strOutcome, m_Model, "outcome");
    Set(m_bOutcomeGood, m_bSuccess, m_Model, "outcome_good");

    int iItemType = 0, iItemNo = 0;
    if (CItemFragment* pTarget = Upgrade.GetTargetItem()) {
        iItemType = pTarget->GetItem().GetTYPE();
        iItemNo = pTarget->GetItem().GetItemNO();
    }
    if (g_pNet)
        g_pNet->Send_cli_ITEM_RESULT_REPORT(
            m_bSuccess ? REPORT_ITEM_UPGRADE_SUCCESS : REPORT_ITEM_UPGRADE_FAILED, iItemType, iItemNo);

    /// The classic box ( routed to UI2 ); its OK leaves the result state,
    /// which applies the result.
    g_itMGR.OpenMsgBox(m_bSuccess ? STR_CRAFT_UPGRADE_SUCCESS : STR_CRAFT_UPGRADE_FAILED,
        CMsgBox::BT_OK,
        true,
        0,
        new CTCmdChangeStateUpgradeDlg(CUpgradeDlg::STATE_NORMAL));
}

void
RoseRmlUpgrade::UpdateResult() {
    CUpgradeDlg* pDlg = Dlg();
    const bool bResult = pDlg != NULL && pDlg->GetState() == CUpgradeDlg::STATE_RESULT;

    if (!bResult) {
        if (m_iPhase != PHASE_NONE) {
            m_iPhase = PHASE_NONE;
            Set(m_bShowBar, false, m_Model, "show_bar");
        }
        return;
    }

    if (m_iPhase == PHASE_NONE)
        BeginResult();
    if (m_iPhase != PHASE_BAR)
        return;

    /// A roll, not a progress bar: it lands where it lands ( short of the
    /// mark on a failure ), so it eases into its spot instead of stopping
    /// dead at full speed -- the classic linear fill read as a stall.
    const DWORD dwElapsed = GetTickCount() - m_dwBarStart;
    const bool bDone = dwElapsed >= kBarMs;
    const float t = bDone ? 1.0f : (float)dwElapsed / (float)kBarMs;
    const float fEase = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    const int iValue = bDone ? m_iBarTarget : (int)((float)m_iBarTarget * fEase);

    char szFill[48];
    _snprintf(szFill, sizeof(szFill), "scaleX(%.3f)", (float)iValue / 100.0f);
    szFill[sizeof(szFill) - 1] = '\0';
    Set(m_strFill, Rml::String(szFill), m_Model, "fill");
    Set(m_bGreen, iValue >= m_iBarMark, m_Model, "green");

    if (bDone)
        FinishResult();
}

/// --- actions ----------------------------------------------------------------------

/// A left press on a cell: Alt previews, Shift links to chat; otherwise it
/// may become a drag back off ( CUpgradeDlg's drag items ).
void
RoseRmlUpgrade::OnPress(int iKind, int iIndex) {
    m_iPressKind = -1;

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

    if (IsBusy())
        return;
    m_iPressKind = iKind;
    m_iPressIndex = iIndex;
}

void
RoseRmlUpgrade::OnUse(int iKind, int iIndex) {
    if (IsBusy())
        return;
    CIcon* pIcon = IconAt(iKind, iIndex);
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;
    /// The drag items' own commands: the target takes its materials with it.
    if (iKind == KIND_TARGET)
        CUpgrade::GetInstance().RemoveTargetItem();
    else if (CItem* pItem = ((CIconItem*)pIcon)->GetCItem())
        CUpgrade::GetInstance().RemoveMaterialItem(pItem);
}

void
RoseRmlUpgrade::UpdateDragStart() {
    if (m_iPressKind < 0)
        return;

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_iPressKind = -1;
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
    m_iPressKind = -1;

    CUpgradeDlg* pDlg = Dlg();
    CIcon* pIcon = IconAt(iKind, m_iPressIndex);
    CDragItem* pDrag = NULL;
    if (pDlg != NULL)
        pDrag = (iKind == KIND_TARGET) ? pDlg->GetTargetDragItem() : pDlg->GetMaterialDragItem();
    if (pIcon == NULL || pDrag == NULL)
        return;

    pDrag->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDrag);
}

void
RoseRmlUpgrade::UpdateTooltip() {
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
    pIcon->GetToolTip(ToolTip, DLG_TYPE_UPGRADE, 0);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlUpgrade::PlaceDefault() {
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
RoseRmlUpgrade::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen) {
        if (!IsInWorld()) {
            SetOpen(false);
        } else if (!IsBusy() && CUpgrade::GetInstance().GetType() == CUpgrade::TYPE_NPC) {
            /// CUpgradeDlg::Update ( which only ran while it was on screen ):
            /// the NPC gone or out of reach. Not while busy: the result must
            /// reach the bag first.
            CObjCHAR* pNpc = g_pObjMGR->Get_ClientCharOBJ(CUpgrade::GetInstance().GetNpcSvrIdx(), false);
            if (!(pNpc && g_pAVATAR->IsInRANGE(pNpc, AVT_CLICK_EVENT_RANGE)))
                g_itMGR.CloseDialog(DLG_TYPE_UPGRADE);
        }
    }

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    UpdateResult();
    Sample();
    PlaceDefault();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateDragStart();
    UpdateTooltip();
}
