#include "stdafx.h"

#include "RoseRmlCraft.h"
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
#include "../gamedata/CManufacture.h"
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
#include "../interface/Dlgs/MakeDLG.h"
#include "../interface/Dlgs/ChattingDlg.h"
#include "../interface/Dlgs/subclass/CSlot.h"

#include "rose/common/log.h"
#include "rose/io/stb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

/// After a result the bars hold their fill this long ( the roll can be
/// read ), then go back to empty; the outcome line stays.
const DWORD kHoldBarsMs = 1500;

/// Cell kinds ( slot-kind ).
const int KIND_MAKE = 0; ///< the item to make ( tooltip only )
const int KIND_MATERIAL = 1;
const int KIND_LIST = 2; ///< a row of the recipe list ( tooltip only )

/// CMakeStateResult's bars: 0..100, filling at 50 a second.
const int kBarSpeed = 50;

/// A request the server never answers would keep the window busy forever
/// ( the classic dialog stayed modal ): give up after this.
const DWORD kWaitTimeoutMs = 20000;

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

int
Clamp100(int i) {
    return i < 0 ? 0 : (i > 100 ? 100 : i);
}

template<class T>
void
Set(T& member, const T& value, Rml::DataModelHandle& model, const char* pszName) {
    if (member != value) {
        member = value;
        model.DirtyVariable(pszName);
    }
}

/// The report the server echoes as the crafting effect on the avatar.
void
Report(BYTE btReport) {
    int iType = 0, iNo = 0;
    if (CItem* pItem = CManufacture::GetInstance().GetMakeItem()) {
        iType = pItem->GetItem().GetTYPE();
        iNo = pItem->GetItem().GetItemNO();
    }
    if (g_pNet)
        g_pNet->Send_cli_ITEM_RESULT_REPORT(btReport, iType, iNo);
}

} // namespace

RoseRmlCraft::RoseRmlCraft():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iPressKind(-1),
    m_iPressIndex(0),
    m_iPressX(0),
    m_iPressY(0),
    m_iClass(-1),
    m_iHoverItem(-1),
    m_pHoverIcon(NULL),
    m_iPhase(PHASE_NONE),
    m_dwBarsStart(0),
    m_dwDoneAt(0),
    m_iBarCount(0),
    m_dwWaitStart(0),
    m_iDropType(DLG_TYPE_MAKE),
    m_bHasItem(false),
    m_strCost("0"),
    m_strHave("0"),
    m_bShort(false),
    m_iState(CMakeDLG::STATE_NORMAL),
    m_bCanStart(false),
    m_bLocked(false),
    m_iButton(0),
    m_bOutcomeGood(false) {
    for (int i = 0; i < 4; ++i)
        m_iBarTarget[i] = 0;
}

RoseRmlCraft::~RoseRmlCraft() {
    delete m_pHoverIcon;
}

bool
RoseRmlCraft::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    for (int i = 0; i < g_iMaxCountMaterial; ++i) {
        MatVM vm;
        vm.index = i;
        vm.needed = vm.placed = vm.missing = vm.green = false;
        vm.mark = "0%";
        vm.fill = "scaleX(0)";
        m_Mats.push_back(vm);
    }

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("craft");
    if (!constructor)
        return false;

    if (auto cls = constructor.RegisterStruct<ClassVM>()) {
        cls.RegisterMember("index", &ClassVM::index);
        cls.RegisterMember("used", &ClassVM::used);
        cls.RegisterMember("on", &ClassVM::on);
        cls.RegisterMember("name", &ClassVM::name);
    }
    constructor.RegisterArray<std::vector<ClassVM>>();
    if (auto item = constructor.RegisterStruct<ItemVM>()) {
        item.RegisterMember("index", &ItemVM::index);
        item.RegisterMember("used", &ItemVM::used);
        item.RegisterMember("on", &ItemVM::on);
        item.RegisterMember("src", &ItemVM::src);
        item.RegisterMember("rect", &ItemVM::rect);
        item.RegisterMember("name", &ItemVM::name);
    }
    constructor.RegisterArray<std::vector<ItemVM>>();
    if (auto mat = constructor.RegisterStruct<MatVM>()) {
        mat.RegisterMember("index", &MatVM::index);
        mat.RegisterMember("needed", &MatVM::needed);
        mat.RegisterMember("placed", &MatVM::placed);
        mat.RegisterMember("missing", &MatVM::missing);
        mat.RegisterMember("src", &MatVM::src);
        mat.RegisterMember("rect", &MatVM::rect);
        mat.RegisterMember("name", &MatVM::name);
        mat.RegisterMember("count", &MatVM::count);
        mat.RegisterMember("mark", &MatVM::mark);
        mat.RegisterMember("fill", &MatVM::fill);
        mat.RegisterMember("green", &MatVM::green);
    }
    constructor.RegisterArray<std::vector<MatVM>>();

    constructor.Bind("drop_type", &m_iDropType);
    constructor.Bind("classes", &m_Classes);
    constructor.Bind("items", &m_Items);
    constructor.Bind("has_item", &m_bHasItem);
    constructor.Bind("item_src", &m_strItemSrc);
    constructor.Bind("item_rect", &m_strItemRect);
    constructor.Bind("item_name", &m_strItemName);
    constructor.Bind("mats", &m_Mats);
    constructor.Bind("cost", &m_strCost);
    constructor.Bind("have", &m_strHave);
    constructor.Bind("short", &m_bShort);
    constructor.Bind("state", &m_iState);
    constructor.Bind("can_start", &m_bCanStart);
    constructor.Bind("locked", &m_bLocked);
    constructor.Bind("button", &m_iButton);
    constructor.Bind("outcome", &m_strOutcome);
    constructor.Bind("outcome_good", &m_bOutcomeGood);

    constructor.BindEventCallback("set_class",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                SelectClass(args[0].Get<int>());
        });
    constructor.BindEventCallback("set_item",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                SelectItem(args[0].Get<int>());
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
    /// use(kind, index) -- double-click: a material back to the bag.
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
            CMakeDLG* pDlg = Dlg();
            if (pDlg == NULL)
                return;
            ClearResult();
            if (pDlg->Start())
                m_dwWaitStart = GetTickCount();
        });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_MAKE);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "craft.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load crafting document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("craft");
    RoseRmlLayout::Track(m_pPanel, "craft");

    LOG_INFO("[rmlui] crafting document loaded");
    return true;
}

void
RoseRmlCraft::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
    delete m_pHoverIcon;
    m_pHoverIcon = NULL;
    m_iHoverItem = -1;
}

CMakeDLG*
RoseRmlCraft::Dlg() const {
    return (CMakeDLG*)g_itMGR.FindDlg(DLG_TYPE_MAKE);
}

CIcon*
RoseRmlCraft::MaterialIcon(int iIndex) const {
    CMakeDLG* pDlg = Dlg();
    CSlot* pSlot = pDlg ? pDlg->GetMaterialSlot(iIndex) : NULL;
    return pSlot ? pSlot->GetIcon() : NULL;
}

bool
RoseRmlCraft::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

/// A request sent, a result not yet applied, or the bars still running.
bool
RoseRmlCraft::IsBusy() const {
    CMakeDLG* pDlg = Dlg();
    return (pDlg != NULL && pDlg->GetState() != CMakeDLG::STATE_NORMAL) || m_iPhase == PHASE_BARS;
}

void
RoseRmlCraft::SetOpen(bool bOpen) {
    CMakeDLG* pDlg = Dlg();

    if (bOpen) {
        if (m_bOpen || pDlg == NULL)
            return;
        /// CMakeDLG::Show: nothing to make ( the skill's level is too low for
        /// every recipe ), no window. It showed nothing; say why.
        const std::list<int>& Classes = CManufacture::GetInstance().GetMakableClasses();
        if (Classes.empty()) {
            RoseRmlUi::NoticeBox("Crafting", "There is nothing you can craft with this skill yet.");
            return;
        }
        RoseUi2::PlayWindowSound(DLG_TYPE_MAKE, true);
        m_bOpen = true;
        pDlg->ResetState();
        m_iPhase = PHASE_NONE;
        ClearResult();
        /// The first kind, and with it the first item ( CManufacture's list
        /// reload selects it, through the hidden dialog ).
        m_iClass = Classes.front();
        CManufacture::GetInstance().SetMakeClass(m_iClass);
        m_iPressKind = -1;
        SampleLists();
        Sample();
        return;
    }

    if (!m_bOpen)
        return;
    if (IsBusy() && IsInWorld())
        return; /// the result must reach the bag first ( see the header )

    /// Leaving the world with a result pending: apply it now.
    if (m_iPhase == PHASE_BARS
        || (pDlg != NULL && pDlg->GetState() == CMakeDLG::STATE_RESULT && m_iPhase == PHASE_NONE)) {
        if (m_iPhase == PHASE_NONE)
            BeginResult();
        if (m_iPhase == PHASE_BARS)
            FinishResult();
    }
    if (pDlg != NULL)
        pDlg->ResetState();

    RoseUi2::PlayWindowSound(DLG_TYPE_MAKE, false);
    m_bOpen = false;
    m_iPressKind = -1;
    m_iPhase = PHASE_NONE;
    /// CMakeDLG::Hide: the materials go back to the bag.
    CManufacture::GetInstance().Clear();
}

void
RoseRmlCraft::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    m_iPressKind = -1;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- the lists ------------------------------------------------------------------

/// Kinds and items, rebuilt only when CManufacture's lists change. Rows are
/// never removed, only hidden: a data-for that shrinks while its rows read a
/// changing variable is evaluated past the end.
void
RoseRmlCraft::SampleLists() {
    CManufacture& Make = CManufacture::GetInstance();

    std::vector<int> ids(Make.GetMakableClasses().begin(), Make.GetMakableClasses().end());
    if (ids != m_ClassIds) {
        m_ClassIds.swap(ids);
        std::vector<ClassVM> classes = m_Classes;
        if (classes.size() < m_ClassIds.size())
            classes.resize(m_ClassIds.size());
        for (size_t i = 0; i < classes.size(); ++i) {
            ClassVM& vm = classes[i];
            vm.index = (int)i;
            vm.used = i < m_ClassIds.size();
            vm.on = false;
            vm.name.clear();
            if (vm.used) {
                CMakeComboClass Name(m_ClassIds[i]);
                vm.name = RoseRmlText::FromGame(Name.GetIndetify());
            }
        }
        m_Classes.swap(classes);
        m_Model.DirtyVariable("classes");
    }
    for (size_t i = 0; i < m_Classes.size(); ++i) {
        const bool bOn = m_Classes[i].used && m_ClassIds[i] == m_iClass;
        if (m_Classes[i].on != bOn) {
            m_Classes[i].on = bOn;
            m_Model.DirtyVariable("classes");
        }
    }

    const std::list<tagITEM>& Items = Make.GetMakableItems();
    bool bSame = Items.size() == m_ItemList.size();
    if (bSame) {
        size_t i = 0;
        for (std::list<tagITEM>::const_iterator it = Items.begin(); it != Items.end(); ++it, ++i) {
            tagITEM a = *it;
            if (a.GetTYPE() != m_ItemList[i].GetTYPE() || a.GetItemNO() != m_ItemList[i].GetItemNO()) {
                bSame = false;
                break;
            }
        }
    }
    if (!bSame) {
        m_ItemList.assign(Items.begin(), Items.end());
        delete m_pHoverIcon; /// the hovered row may be another item now
        m_pHoverIcon = NULL;
        m_iHoverItem = -1;

        std::vector<ItemVM> items = m_Items;
        if (items.size() < m_ItemList.size())
            items.resize(m_ItemList.size());
        for (size_t i = 0; i < items.size(); ++i) {
            ItemVM& vm = items[i];
            vm.index = (int)i;
            vm.used = i < m_ItemList.size();
            vm.on = false;
            vm.src.clear();
            vm.rect.clear();
            vm.name.clear();
            if (vm.used) {
                tagITEM& Item = m_ItemList[i];
                RoseRmlIcons::Resolve(
                    IMAGE_RES_ITEM, ITEM_ICON_NO(Item.GetTYPE(), Item.GetItemNO()), vm.src, vm.rect);
                vm.name = RoseRmlText::FromGame(Item.GetName());
            }
        }
        m_Items.swap(items);
        m_Model.DirtyVariable("items");
    }
    CItem* pMake = Make.GetMakeItem();
    for (size_t i = 0; i < m_Items.size(); ++i) {
        bool bOn = false;
        if (m_Items[i].used && pMake != NULL) {
            tagITEM& Item = pMake->GetItem();
            bOn = Item.GetTYPE() == m_ItemList[i].GetTYPE() && Item.GetItemNO() == m_ItemList[i].GetItemNO();
        }
        if (m_Items[i].on != bOn) {
            m_Items[i].on = bOn;
            m_Model.DirtyVariable("items");
        }
    }
}

void
RoseRmlCraft::SelectClass(int iIndex) {
    if (IsBusy() || iIndex < 0 || iIndex >= (int)m_ClassIds.size())
        return;
    if (m_ClassIds[iIndex] == m_iClass)
        return;
    ClearResult();
    m_iClass = m_ClassIds[iIndex];
    /// Loads the kind's items; the hidden dialog's list reload then selects
    /// the first ( CMakeDLG::ReloadItemList ).
    CManufacture::GetInstance().SetMakeClass(m_iClass);
}

void
RoseRmlCraft::SelectItem(int iIndex) {
    if (IsBusy() || iIndex < 0 || iIndex >= (int)m_ItemList.size())
        return;
    ClearResult();
    /// A different item takes the placed materials back ( CManufacture ).
    CManufacture::GetInstance().SetMakeItem(m_ItemList[iIndex]);
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlCraft::Sample() {
    CMakeDLG* pDlg = Dlg();
    if (pDlg == NULL || g_pAVATAR == NULL)
        return;
    CManufacture& Make = CManufacture::GetInstance();

    const int iState = pDlg->GetState();
    const bool bBusy = IsBusy();
    Set(m_iState, iState, m_Model, "state");
    Set(m_bLocked, bBusy, m_Model, "locked");
    Set(m_iButton, iState == CMakeDLG::STATE_WAIT ? 1 : (bBusy ? 2 : 0), m_Model, "button");
    Set(m_iDropType, bBusy ? 0 : (int)DLG_TYPE_MAKE, m_Model, "drop_type");

    /// The item to make.
    CSlot* pMakeSlot = pDlg->GetMakeSlot();
    CIcon* pIcon = pMakeSlot ? pMakeSlot->GetIcon() : NULL;
    Rml::String strSrc, strRect, strName;
    int iModule = 0, iGraphic = 0;
    const bool bHasItem = pIcon != NULL && pIcon->IsItemIcon()
        && pIcon->GetSprite(iModule, iGraphic) && RoseRmlIcons::Resolve(iModule, iGraphic, strSrc, strRect);
    if (bHasItem)
        strName = RoseRmlText::FromGame(pIcon->GetName());
    Set(m_bHasItem, bHasItem, m_Model, "has_item");
    Set(m_strItemSrc, strSrc, m_Model, "item_src");
    Set(m_strItemRect, strRect, m_Model, "item_rect");
    Set(m_strItemName, strName, m_Model, "item_name");

    /// Its materials: what each row asks for, what is placed, and the bar
    /// with its success point ( the roll must pass it ).
    bool bAllPlaced = true;
    std::vector<MatVM> mats = m_Mats;
    for (int i = 0; i < g_iMaxCountMaterial; ++i) {
        MatVM& vm = mats[i];
        CRequireMaterial& Need = Make.GetRequireMaterial(i);
        vm.needed = bHasItem && !Need.IsEmpty();
        vm.placed = false;
        vm.src.clear();
        vm.rect.clear();
        vm.name.clear();
        vm.count.clear();
        if (vm.needed) {
            vm.name = RoseRmlText::FromGame(Need.GetName());
            vm.count = Printf("x %d", Need.GetRequireCount());
            vm.mark = Printf("%d%%", Clamp100(Make.GetSuccessPoint(i) / 10));
            CIcon* pMat = MaterialIcon(i);
            if (pMat != NULL && pMat->IsItemIcon() && pMat->GetSprite(iModule, iGraphic)
                && RoseRmlIcons::Resolve(iModule, iGraphic, vm.src, vm.rect))
                vm.placed = true;
            if (!vm.placed)
                bAllPlaced = false;
        }
        vm.missing = vm.needed && !vm.placed;
        if (m_iPhase == PHASE_NONE) {
            vm.fill = "scaleX(0)";
            vm.green = false;
        }
    }
    if (mats != m_Mats) {
        m_Mats.swap(mats);
        m_Model.DirtyVariable("mats");
    }

    /// The skill's MP.
    const int iCost = bHasItem ? Make.GetCosumeMP() : 0;
    const int iHave = g_pAVATAR->Get_MP();
    Set(m_strCost, Money(iCost), m_Model, "cost");
    Set(m_strHave, Money(iHave), m_Model, "have");
    const bool bShort = bHasItem && iCost > iHave;
    Set(m_bShort, bShort, m_Model, "short");

    Set(m_bCanStart, !bBusy && bHasItem && bAllPlaced && !bShort, m_Model, "can_start");
}

/// --- the result --------------------------------------------------------------------

void
RoseRmlCraft::ClearResult() {
    if (m_iPhase == PHASE_BARS)
        return;
    m_iPhase = PHASE_NONE;
    Set(m_strOutcome, Rml::String(), m_Model, "outcome");
}

/// The server answered ( CMakeDLG::RecvResult put the dialog in RESULT ).
void
RoseRmlCraft::BeginResult() {
    CMakeDLG* pDlg = Dlg();
    if (pDlg == NULL)
        return;
    CManufacture& Make = CManufacture::GetInstance();
    const BYTE btResult = pDlg->GetResultCode();
    const int iStep = pDlg->GetResultStep();
    const int iCount = Make.GetMaterialCount();

    if (btResult == RESULT_CREATE_ITEM_SUCCESS || btResult == RESULT_CREATE_ITEM_FAILED) {
        /// The bars: every material on a success, up to the failed one on a
        /// failure; each fills to the server's point / 10.
        m_iBarCount = (btResult == RESULT_CREATE_ITEM_SUCCESS) ? iCount : iStep + 1;
        if (m_iBarCount > g_iMaxCountMaterial)
            m_iBarCount = g_iMaxCountMaterial;
        if (m_iBarCount < 0)
            m_iBarCount = 0;
        for (int i = 0; i < g_iMaxCountMaterial; ++i)
            m_iBarTarget[i] = (i < m_iBarCount) ? Clamp100(pDlg->GetResultPoint(i) / 10) : 0;
        m_dwBarsStart = GetTickCount();
        m_iPhase = PHASE_BARS;
        Set(m_strOutcome, Rml::String(), m_Model, "outcome");
        return;
    }

    /// Refused ( CMakeStateResult::Init's other answers ): say why, take out
    /// what the server consumed, back to NORMAL.
    const char* pszWhy = NULL;
    switch (btResult) {
        case RESULT_CREATE_ITEM_INVALID_CONDITION:
            pszWhy = STR_NOT_ENOUGH_MANA;
            break;
        case RESULT_CREATE_ITEM_NEED_ITEM:
            pszWhy = STR_NOT_ENOUGH_MATERIAL;
            Make.SubItemsAfterRecvResult(iStep);
            break;
        case RESULT_CREATE_ITEM_INVALID_ITEM:
            pszWhy = STR_NOT_EXIST_MATERIAL;
            Make.SubItemsAfterRecvResult(iStep);
            break;
        case RESULT_CREATE_ITEM_NEED_SKILL_LEV:
            pszWhy = STR_NOT_ENOUGH_MAKE_SKILL_LEVEL;
            break;
        default:
            break;
    }
    if (pszWhy != NULL)
        g_itMGR.OpenMsgBox(pszWhy);
    pDlg->ResetState();
    m_iPhase = PHASE_NONE;
}

void
RoseRmlCraft::UpdateResult() {
    CMakeDLG* pDlg = Dlg();
    if (pDlg == NULL)
        return;

    /// The safety timeout on an unanswered request.
    if (pDlg->GetState() == CMakeDLG::STATE_WAIT) {
        if (GetTickCount() - m_dwWaitStart > kWaitTimeoutMs) {
            pDlg->ResetState();
            g_itMGR.AppendChatMsg("The crafting request got no answer.", IT_MGR::CHAT_TYPE_SYSTEM);
        }
        return;
    }

    /// A result shown a moment: the bars empty again ( Sample resets them
    /// outside a result ), the outcome line stays until the next change.
    if (m_iPhase == PHASE_DONE && GetTickCount() - m_dwDoneAt >= kHoldBarsMs)
        m_iPhase = PHASE_NONE;

    if (pDlg->GetState() == CMakeDLG::STATE_RESULT && m_iPhase != PHASE_BARS)
        BeginResult();
    if (m_iPhase != PHASE_BARS)
        return;

    /// The bars one after another, each at 50 a second.
    CManufacture& Make = CManufacture::GetInstance();
    const DWORD dwElapsed = GetTickCount() - m_dwBarsStart;
    DWORD dwAt = 0;
    bool bDone = true;
    std::vector<MatVM> mats = m_Mats;
    for (int i = 0; i < g_iMaxCountMaterial; ++i) {
        int iValue = 0;
        if (i < m_iBarCount) {
            const DWORD dwLen = (DWORD)(m_iBarTarget[i] * 1000 / kBarSpeed);
            if (dwElapsed >= dwAt + dwLen) {
                iValue = m_iBarTarget[i];
            } else {
                bDone = false;
                if (dwElapsed > dwAt)
                    iValue = (int)((dwElapsed - dwAt) * kBarSpeed / 1000);
            }
            dwAt += dwLen;
        }
        char szFill[48];
        _snprintf(szFill, sizeof(szFill), "scaleX(%.3f)", (float)iValue / 100.0f);
        szFill[sizeof(szFill) - 1] = '\0';
        mats[i].fill = szFill;
        mats[i].green = iValue > 0 && iValue >= Make.GetSuccessPoint(i) / 10;
    }
    if (mats != m_Mats) {
        m_Mats.swap(mats);
        m_Model.DirtyVariable("mats");
    }

    if (bDone)
        FinishResult();
}

/// The bars are done: the result into the bag, the report ( the effect on
/// the avatar ), the box, and the dialog back to NORMAL.
void
RoseRmlCraft::FinishResult() {
    CMakeDLG* pDlg = Dlg();
    if (pDlg == NULL || g_pAVATAR == NULL)
        return;
    CManufacture& Make = CManufacture::GetInstance();
    const bool bSuccess = pDlg->GetResultCode() == RESULT_CREATE_ITEM_SUCCESS;
    const int iStep = pDlg->GetResultStep();

    if (bSuccess) {
        /// CMakeStateResult::Update: the materials leave the bag, the made
        /// item arrives in the slot the server gave.
        Make.SubItemsAfterRecvResult(Make.GetMaterialCount());
        tagITEM Created = pDlg->GetCreatedItem();
        g_pAVATAR->Add_ITEM(iStep, Created);
        Report(REPORT_ITEM_CREATE_SUCCESS);

        std::vector<tagITEM> Made(1, Created);
        if (!RoseRmlUi::ItemsBox("Crafted", STR_SUCCESS_MAKE_ITEM, Made))
            g_itMGR.OpenMsgBox(STR_SUCCESS_MAKE_ITEM);
        Set(m_strOutcome, Rml::String("Success"), m_Model, "outcome");
    } else {
        /// The materials up to the failed one are used up.
        Make.SubItemsAfterRecvResult(iStep + 1);
        Report(REPORT_ITEM_CREATE_FAILED);
        g_itMGR.OpenMsgBox(STR_FAIL_MAKE_ITEM);
        Set(m_strOutcome, Printf("Failed at material %d", iStep + 1), m_Model, "outcome");
    }
    Set(m_bOutcomeGood, bSuccess, m_Model, "outcome_good");

    pDlg->ResetState();
    m_iPhase = PHASE_DONE;
    m_dwDoneAt = GetTickCount();
}

/// --- actions ----------------------------------------------------------------------

/// A left press on a cell: Alt previews, Shift links to chat; a material may
/// become a drag back off ( CMakeDLG's drag item ).
void
RoseRmlCraft::OnPress(int iKind, int iIndex) {
    m_iPressKind = -1;

    tagITEM Item;
    Item.Clear();
    CIcon* pIcon = NULL;
    if (iKind == KIND_MATERIAL) {
        pIcon = MaterialIcon(iIndex);
        if (pIcon != NULL && pIcon->IsItemIcon())
            Item = ((CIconItem*)pIcon)->GetItem();
    } else if (iKind == KIND_LIST) {
        if (iIndex >= 0 && iIndex < (int)m_ItemList.size())
            Item = m_ItemList[iIndex];
    } else if (iKind == KIND_MAKE) {
        CMakeDLG* pDlg = Dlg();
        CSlot* pSlot = pDlg ? pDlg->GetMakeSlot() : NULL;
        pIcon = pSlot ? pSlot->GetIcon() : NULL;
        if (pIcon != NULL && pIcon->IsItemIcon())
            Item = ((CIconItem*)pIcon)->GetItem();
    }
    if (Item.IsEmpty())
        return;

    if (GetAsyncKeyState(VK_MENU) < 0 && g_UIMed.OpenItemPreview(Item))
        return;

    if (GetAsyncKeyState(VK_SHIFT) < 0) {
        if (CChatDLG* pChatDlg = g_itMGR.GetChatDLG())
            pChatDlg->AddItemLinkToInput(Item);
        return;
    }

    if (iKind == KIND_MATERIAL && pIcon != NULL && !IsBusy()) {
        m_iPressKind = iKind;
        m_iPressIndex = iIndex;
    }
}

void
RoseRmlCraft::OnUse(int iKind, int iIndex) {
    if (IsBusy() || iKind != KIND_MATERIAL)
        return;
    CIcon* pIcon = MaterialIcon(iIndex);
    if (pIcon == NULL || !pIcon->IsItemIcon())
        return;
    /// The drag item's own command ( CTCmdTakeOutItemFromMakeDlg ).
    if (CItem* pItem = ((CIconItem*)pIcon)->GetCItem())
        CManufacture::GetInstance().RemoveMaterialItem(pItem);
}

void
RoseRmlCraft::UpdateDragStart() {
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

    m_iPressKind = -1;

    CMakeDLG* pDlg = Dlg();
    CIcon* pIcon = MaterialIcon(m_iPressIndex);
    CDragItem* pDrag = pDlg ? pDlg->GetDragItem() : NULL;
    if (pIcon == NULL || pDrag == NULL)
        return;

    pDrag->SetIcon(pIcon);
    CDragNDropMgr::GetInstance().DragStart(pDrag);
}

void
RoseRmlCraft::UpdateTooltip() {
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

    CIcon* pIcon = NULL;
    if (iKind == KIND_MAKE) {
        CMakeDLG* pDlg = Dlg();
        CSlot* pSlot = pDlg ? pDlg->GetMakeSlot() : NULL;
        pIcon = pSlot ? pSlot->GetIcon() : NULL;
    } else if (iKind == KIND_MATERIAL) {
        pIcon = MaterialIcon(iIndex);
    } else if (iKind == KIND_LIST && iIndex >= 0 && iIndex < (int)m_ItemList.size()) {
        /// A recipe row has no icon of its own: one is made for it, and kept
        /// while the mouse stays on the row.
        if (iIndex != m_iHoverItem || m_pHoverIcon == NULL) {
            delete m_pHoverIcon;
            m_pHoverIcon = new CIconItem(&m_ItemList[iIndex]);
            m_iHoverItem = iIndex;
        }
        pIcon = m_pHoverIcon;
    }
    if (pIcon == NULL)
        return;

    CInfo ToolTip;
    ToolTip.Clear();
    pIcon->GetToolTip(ToolTip, DLG_TYPE_MAKE, 0);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlCraft::PlaceDefault() {
    /// Beside the bag ( the skill opens it too ), on whichever side has room
    /// -- only while no position is set ( dragged, saved or reset ).
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
RoseRmlCraft::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen && !IsInWorld())
        SetOpen(false);

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    UpdateResult();
    SampleLists();
    Sample();
    PlaceDefault();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateDragStart();
    UpdateTooltip();
}
