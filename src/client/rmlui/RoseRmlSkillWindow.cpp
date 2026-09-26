#include "stdafx.h"

#include "RoseRmlSkillWindow.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../GameCommon/Skill.h"
#include "../gamecommon/StringManager.h"
#include "../Network/CNetwork.h"
#include "../interface/CDragItem.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CToolTipMgr.h"
#include "../interface/IO_ImageRes.h"
#include "../interface/Icon/CIconSkill.h"
#include "../interface/SlotContainer/CSkillSlot.h"
#include "../interface/TypeResource.h"
#include "../interface/command/dragcommand.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"

#include "rose/common/log.h"

#include <math.h>
#include <stdio.h>
#include <stdarg.h>

namespace {

/// CSkillSlot pages the window shows: basic, active, passive ( as CSkillDLG ).
const int kTabCount = 3;
const int kTabPassive = 2;

Rml::String
Printf(const char* pszFormat, ...) {
    char szBuf[64];
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf(szBuf, sizeof(szBuf), pszFormat, args);
    va_end(args);
    szBuf[sizeof(szBuf) - 1] = '\0';
    return szBuf;
}

/// The highest level this skill's line reaches: LIST_SKILL keeps a line's
/// ranks in consecutive rows sharing SKILL_1LEV_INDEX, one level apart --
/// the same walk the level-up check does one step of.
int
MaxLevelOf(int iSkillIdx) {
    int iLevel = SKILL_LEVEL(iSkillIdx);
    const int iRows = g_SkillList.Get_SkillCNT();
    for (int i = iSkillIdx; i + 1 < iRows; ++i) {
        if (SKILL_1LEV_INDEX(i + 1) != SKILL_1LEV_INDEX(i) || SKILL_LEVEL(i + 1) != SKILL_LEVEL(i) + 1)
            break;
        ++iLevel;
    }
    return iLevel;
}

} // namespace

RoseRmlSkillWindow::RoseRmlSkillWindow():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_pDragItem(NULL),
    m_iPressSlot(-1),
    m_iPressX(0),
    m_iPressY(0),
    m_iTab(1),
    m_iCountBasic(0),
    m_iCountActive(0),
    m_iCountPassive(0),
    m_iSkillPoints(0),
    m_bHasTree(false) {}

RoseRmlSkillWindow::~RoseRmlSkillWindow() {
    delete m_pDragItem; /// deletes its commands too
}

bool
RoseRmlSkillWindow::CanLevelUp(int iSlot, std::string* pstrWhy) {
    if (iSlot < 0 || iSlot >= MAX_LEARNED_SKILL_CNT || g_pAVATAR == NULL)
        return false;

    /// Mirrors CSkillListItem::IsValidLevelUp -- keep the two in step.
    const short nSkillIDX = g_pAVATAR->m_Skills.m_nSkillINDEX[iSlot];
    if (SKILL_1LEV_INDEX(nSkillIDX) != SKILL_1LEV_INDEX(nSkillIDX + 1)
        || SKILL_LEVEL(nSkillIDX) + 1 != SKILL_LEVEL(nSkillIDX + 1)) {
        if (pstrWhy)
            *pstrWhy = STR_CANT_LEVELUP;
        return false;
    }

    if (g_pAVATAR->Get_NeedPoint2SkillUP((short)iSlot) > g_pAVATAR->GetCur_SkillPOINT()) {
        if (pstrWhy)
            *pstrWhy = STR_NOT_ENOUGH_SKILLPOINT;
        return false;
    }

    for (int i = 0; i < SKILL_NEED_ABILITY_TYPE_CNT; ++i) {
        const int iType = SKILL_NEED_ABILITY_TYPE(nSkillIDX, i);
        if (iType && g_pAVATAR->Get_AbilityValue(iType) < SKILL_NEED_ABILITY_VALUE(nSkillIDX, i)) {
            if (pstrWhy)
                *pstrWhy = CStr::Printf(STR_NEED_ABILITY,
                    CStringManager::GetSingleton().GetAbility(iType),
                    SKILL_NEED_ABILITY_VALUE(nSkillIDX, i));
            return false;
        }
    }
    return true;
}

bool
RoseRmlSkillWindow::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    /// The legacy skill list item's drag: onto either skill bar row.
    m_pDragItem = new CDragItem;
    m_pDragItem->AddTarget(DLG_TYPE_QUICKBAR, new CTCmdDragSkill2QuickBar);
    m_pDragItem->AddTarget(DLG_TYPE_QUICKBAR_EXT, new CTCmdDragSkill2QuickBar(DLG_TYPE_QUICKBAR_EXT));

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("skills");
    if (!constructor)
        return false;

    if (auto skill = constructor.RegisterStruct<SkillVM>()) {
        skill.RegisterMember("slot", &SkillVM::slot);
        skill.RegisterMember("src", &SkillVM::src);
        skill.RegisterMember("rect", &SkillVM::rect);
        skill.RegisterMember("name", &SkillVM::name);
        skill.RegisterMember("level", &SkillVM::level);
        skill.RegisterMember("max_level", &SkillVM::max_level);
        skill.RegisterMember("level_pct", &SkillVM::level_pct);
        skill.RegisterMember("costs", &SkillVM::costs);
        skill.RegisterMember("cd", &SkillVM::cd);
        skill.RegisterMember("passive", &SkillVM::passive);
        skill.RegisterMember("can_up", &SkillVM::can_up);
        skill.RegisterMember("up_short", &SkillVM::up_short);
        skill.RegisterMember("up_text", &SkillVM::up_text);
    }
    constructor.RegisterArray<std::vector<SkillVM>>();

    constructor.Bind("tab", &m_iTab);
    constructor.Bind("count_basic", &m_iCountBasic);
    constructor.Bind("count_active", &m_iCountActive);
    constructor.Bind("count_passive", &m_iCountPassive);
    constructor.Bind("sp", &m_iSkillPoints);
    constructor.Bind("has_tree", &m_bHasTree);
    constructor.Bind("skills", &m_Skills);

    constructor.BindEventCallback("set_tab",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int iTab = args[0].Get<int>();
            if (iTab >= 0 && iTab < kTabCount && iTab != m_iTab) {
                m_iTab = iTab;
                m_Model.DirtyVariable("tab");
                Sample();
            }
        });

    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            if (args.empty() || ev.GetParameter<int>("button", 0) != 0)
                return;
            m_iPressSlot = args[0].Get<int>();
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
        });

    /// Double-click uses the skill, as in the legacy window ( a single click
    /// in a list you are browsing should not cast anything ).
    constructor.BindEventCallback("use",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty() || g_pAVATAR == NULL)
                return;
            CIconSkill icon(args[0].Get<int>());
            if (icon.GetSkill() != NULL)
                icon.ExecuteCommand();
        });

    constructor.BindEventCallback("level_up",
        [](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            ev.StopPropagation();
            if (args.empty())
                return;
            const int iSlot = args[0].Get<int>();
            std::string strWhy;
            if (CanLevelUp(iSlot, &strWhy))
                g_pNet->Send_cli_SKILL_LEVELUP_REQ((BYTE)iSlot);
            else if (!strWhy.empty())
                g_itMGR.AppendChatMsg(strWhy.c_str(), IT_MGR::CHAT_TYPE_SYSTEM);
        });

    constructor.BindEventCallback("open_tree",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.OpenDialog(DLG_TYPE_SKILLTREE);
        });

    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { SetOpen(false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "skills.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load skill window document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("skills");
    RoseRmlLayout::Track(m_pPanel, "skills");

    LOG_INFO("[rmlui] skill window document loaded");
    return true;
}

void
RoseRmlSkillWindow::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

void
RoseRmlSkillWindow::SetOpen(bool bOpen) {
    if (bOpen != m_bOpen)
        RoseUi2::PlayWindowSound(DLG_TYPE_SKILL, bOpen);
    m_bOpen = bOpen;
    if (bOpen)
        Sample(); /// no stale list on the first frame
}

void
RoseRmlSkillWindow::SetVisible(bool bVisible) {
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
RoseRmlSkillWindow::Sample() {
    if (g_pAVATAR == NULL)
        return;

    CSkillSlot* pSlots = g_pAVATAR->GetSkillSlot();
    if (pSlots == NULL)
        return;

    int iCounts[kTabCount] = {0, 0, 0};
    std::vector<SkillVM> skills;
    const int iPoints = g_pAVATAR->GetCur_SkillPOINT();

    for (int iPage = 0; iPage < kTabCount; ++iPage) {
        for (int i = 0; i < MAX_LEARNED_SKILL_PER_PAGE; ++i) {
            const int iSlot = iPage * MAX_LEARNED_SKILL_PER_PAGE + i;
            CSkill* pSkill = pSlots->GetSkill((short)iSlot);
            if (pSkill == NULL)
                continue;

            ++iCounts[iPage];
            if (iPage != m_iTab)
                continue;

            const int iIdx = pSkill->GetSkillIndex();

            SkillVM vm;
            vm.slot = iSlot;
            RoseRmlIcons::Resolve(IMAGE_RES_SKILL_ICON, SKILL_ICON_NO(iIdx), vm.src, vm.rect);
            const char* pszName = SKILL_NAME(iIdx);
            vm.name = pszName ? pszName : "";
            vm.level = pSkill->GetSkillLevel();
            vm.max_level = max(vm.level, MaxLevelOf(iIdx));
            vm.level_pct = (vm.max_level > 0) ? floorf(100.0f * vm.level / vm.max_level) : 100.0f;
            vm.passive = (iPage == kTabPassive);

            /// Use costs, as the legacy row listed them ( MP, HP, stamina ... ).
            for (int p = 0; p < SKILL_USE_PROPERTY_CNT; ++p) {
                const int iType = SKILL_USE_PROPERTY(iIdx, p);
                if (!iType)
                    continue;
                const char* pszAbility = CStringManager::GetSingleton().GetAbility(iType);
                if (!vm.costs.empty())
                    vm.costs += "   ";
                vm.costs += Printf("%s %d",
                    pszAbility ? pszAbility : "?",
                    g_pAVATAR->Skill_ToUseAbilityVALUE((short)iIdx, (short)p));
            }

            /// Cooldown, the skill bar's curtain ( CIconSkill::GetCooldown ).
            CIconSkill icon(iSlot);
            vm.cd = floorf(icon.GetCooldown(NULL) * 100.0f + 0.5f);

            /// Level-up: live when CanLevelUp says so; the text says what the
            /// next level costs, or that there is none.
            vm.can_up = CanLevelUp(iSlot, NULL);
            if (vm.level >= vm.max_level) {
                vm.up_text = "Max";
                vm.up_short = false;
            } else {
                const int iNeed = g_pAVATAR->Get_NeedPoint2SkillUP((short)iSlot);
                vm.up_text = Printf("%d SP", iNeed);
                vm.up_short = iNeed > iPoints;
            }
            skills.push_back(vm);
        }
    }

    if (skills != m_Skills) {
        m_Skills.swap(skills);
        m_Model.DirtyVariable("skills");
    }

    int* pCounts[kTabCount] = {&m_iCountBasic, &m_iCountActive, &m_iCountPassive};
    const char* pNames[kTabCount] = {"count_basic", "count_active", "count_passive"};
    for (int t = 0; t < kTabCount; ++t) {
        if (*pCounts[t] != iCounts[t]) {
            *pCounts[t] = iCounts[t];
            m_Model.DirtyVariable(pNames[t]);
        }
    }

    if (m_iSkillPoints != iPoints) {
        m_iSkillPoints = iPoints;
        m_Model.DirtyVariable("sp");
    }

    const bool bHasTree = g_pAVATAR->Get_JOB() != 0;
    if (bHasTree != m_bHasTree) {
        m_bHasTree = bHasTree;
        m_Model.DirtyVariable("has_tree");
    }
}

void
RoseRmlSkillWindow::UpdateDragStart() {
    if (m_iPressSlot < 0)
        return;

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        m_iPressSlot = -1;
        return;
    }

    POINT ptMouse;
    CGame::GetInstance().Get_MousePos(ptMouse);
    const int iSlopX = max(3, g_pCApp->GetWIDTH() / 200);
    const int iSlopY = max(3, g_pCApp->GetHEIGHT() / 200);
    if (abs(ptMouse.x - m_iPressX) < iSlopX && abs(ptMouse.y - m_iPressY) < iSlopY)
        return;

    const int iSlot = m_iPressSlot;
    m_iPressSlot = -1;

    /// Passive skills cannot go on a bar ( CSkillListItem::SetIcon ).
    if (iSlot / MAX_LEARNED_SKILL_PER_PAGE == kTabPassive || m_pDragItem == NULL)
        return;

    CIconSkill icon(iSlot);
    if (icon.GetSkill() == NULL)
        return;
    m_pDragItem->SetIcon(&icon); /// stores a clone
    CDragNDropMgr::GetInstance().DragStart(m_pDragItem);
}

void
RoseRmlSkillWindow::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    int iSlot = -1;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("skill")) {
            iSlot = pEl->GetAttribute<int>("skill", -1);
            break;
        }
    }
    if (iSlot < 0)
        return;

    CIconSkill icon(iSlot);
    if (icon.GetSkill() == NULL)
        return;

    CInfo ToolTip;
    ToolTip.Clear();
    icon.GetToolTip(ToolTip, DLG_TYPE_SKILL, 0);
    if (ToolTip.IsEmpty())
        return;

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlSkillWindow::Update() {
    if (m_pDocument == NULL)
        return;

    /// Closed with UI2 off or out of the world; hidden ( still open ) while
    /// dead, as the legacy window was.
    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
    if (!bInWorld)
        m_bOpen = false;

    SetVisible(m_bOpen && bInWorld && g_pAVATAR->Get_HP() > 0);
    if (!m_bVisible)
        return;

    Sample();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);

    UpdateDragStart();
    UpdateTooltip();
}
