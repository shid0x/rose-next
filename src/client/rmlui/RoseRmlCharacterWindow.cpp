#include "stdafx.h"

#include "RoseRmlCharacterWindow.h"
#include "RoseRmlLayout.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CApplication.h"
#include "../CObjUSER.h"
#include "../Game.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../GameData/CClan.h"
#include "../gamecommon/StringManager.h"
#include "../Network/CNetwork.h"
#include "../interface/CDragNDropMgr.h"
#include "../interface/CInfo.h"
#include "../interface/CToolTipMgr.h"
#include "../interface/StatDescriptions.h"

#include "rose/common/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace {

/// Hover ids: an attribute is its BA_* index, a combat stat this plus its
/// index into g_DerivedStatDescriptions.
const int kCombatStatBase = 100;

const char* const kAttrAbbr[BA_MAX] = {"STR", "DEX", "INT", "CON", "CHA", "SEN"};
const char* const kAttrName[BA_MAX] = {
    "Strength", "Dexterity", "Intelligence", "Concentration", "Charm", "Sensibility"};

/// How long a row glows after a point goes into it ( matches the RCSS ).
const DWORD kFlashMs = 800;

/// LIST_UNION col 1 indexes g_dwCOLOR ( red, green, blue, black, white,
/// yellow, gray, violet, orange, pink ). Those are pure primaries, unreadable
/// on dark glass -- these are the same hues, lifted.
const char* const kUnionColors[] = {"#ff6a5a",
    "#7ad04a",
    "#6aa8ff",
    "#c8c8c8",
    "#f0f0f0",
    "#f0d060",
    "#a8a8a8",
    "#c080ff",
    "#ff9a5a",
    "#ff90c0"};

const char*
UnionColor(int iUnion) {
    const int iColor = UNION_COLOR(iUnion);
    if (iColor < 0 || iColor >= (int)(sizeof(kUnionColors) / sizeof(kUnionColors[0])))
        return "#f0f0f0";
    return kUnionColors[iColor];
}

/// Unions a character can hold points with: LIST_UNION rows 1-10, the ten
/// AT_UNION_POINT slots ( row 0 is "Independence", nobody's union ).
const int kMaxUnions = 10;

const char* const kCombatName[8] = {"Attack",
    "Defense",
    "Magic Resist",
    "Accuracy",
    "Critical",
    "Dodge",
    "Attack Speed",
    "Move Speed"};

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

/// "12,345,678".
Rml::String
Thousands(__int64 iValue) {
    char szRaw[32];
    _snprintf(szRaw, sizeof(szRaw), "%I64d", iValue < 0 ? -iValue : iValue);
    szRaw[sizeof(szRaw) - 1] = '\0';

    const int iLen = (int)strlen(szRaw);
    Rml::String out = iValue < 0 ? "-" : "";
    for (int i = 0; i < iLen; ++i) {
        if (i > 0 && ((iLen - i) % 3) == 0)
            out += ',';
        out += szRaw[i];
    }
    return out;
}

float
Percent(__int64 iValue, __int64 iMax) {
    if (iMax <= 0)
        return 0.0f;
    if (iValue < 0)
        iValue = 0;
    if (iValue > iMax)
        iValue = iMax;
    return (float)((double)iValue * 100.0 / (double)iMax);
}

/// ( total, base ) of one attribute.
void
AttrValues(CObjUSER* pAvatar, int iAttr, int& iTotal, int& iBase) {
    switch (iAttr) {
        case BA_STR:
            iTotal = pAvatar->GetCur_STR();
            iBase = pAvatar->GetDef_STR();
            break;
        case BA_DEX:
            iTotal = pAvatar->GetCur_DEX();
            iBase = pAvatar->GetDef_DEX();
            break;
        case BA_INT:
            iTotal = pAvatar->GetCur_INT();
            iBase = pAvatar->GetDef_INT();
            break;
        case BA_CON:
            iTotal = pAvatar->GetCur_CON();
            iBase = pAvatar->GetDef_CON();
            break;
        case BA_CHARM:
            iTotal = pAvatar->GetCur_CHARM();
            iBase = pAvatar->GetDef_CHARM();
            break;
        default:
            iTotal = pAvatar->GetCur_SENSE();
            iBase = pAvatar->GetDef_SENSE();
            break;
    }
}

/// Can this attribute take a point now? The server's two checks
/// ( Recv_cli_USE_BPOINT_REQ ): enough points, and the base under the cap.
bool
CanRaise(CObjUSER* pAvatar, int iAttr, int iBase) {
    return iBase < Rose::GameStaticConfig::MAX_STAT
        && pAvatar->Get_NeedPoint2AbilityUP((short)iAttr) <= pAvatar->Get_BonusPOINT();
}

} // namespace

RoseRmlCharacterWindow::RoseRmlCharacterWindow():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iLevel(0),
    m_fExpPct(0.0f),
    m_fStaminaPct(0.0f),
    m_iPoints(0),
    m_iTab(0),
    /// Never bind an empty string into data-style-*: it parses as "color: ;",
    /// warns, and pops the RmlUi debugger open before the first sample.
    m_strUnion("None"),
    m_strUnionColor("#9a9a9a") {
    for (int i = 0; i < BA_MAX; ++i) {
        m_iPrevBase[i] = -1;
        m_iFlash[i] = 0;
        m_dwFlashEnd[i] = 0;
    }
}

bool
RoseRmlCharacterWindow::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("character");
    if (!constructor)
        return false;

    if (auto attr = constructor.RegisterStruct<AttrVM>()) {
        attr.RegisterMember("id", &AttrVM::id);
        attr.RegisterMember("abbr", &AttrVM::abbr);
        attr.RegisterMember("name", &AttrVM::name);
        attr.RegisterMember("total", &AttrVM::total);
        attr.RegisterMember("bonus", &AttrVM::bonus);
        attr.RegisterMember("can_up", &AttrVM::can_up);
        attr.RegisterMember("up_short", &AttrVM::up_short);
        attr.RegisterMember("up_text", &AttrVM::up_text);
        attr.RegisterMember("flash", &AttrVM::flash);
    }
    constructor.RegisterArray<std::vector<AttrVM>>();

    if (auto stat = constructor.RegisterStruct<CombatVM>()) {
        stat.RegisterMember("id", &CombatVM::id);
        stat.RegisterMember("name", &CombatVM::name);
        stat.RegisterMember("value", &CombatVM::value);
    }
    constructor.RegisterArray<std::vector<CombatVM>>();

    if (auto un = constructor.RegisterStruct<UnionVM>()) {
        un.RegisterMember("id", &UnionVM::id);
        un.RegisterMember("name", &UnionVM::name);
        un.RegisterMember("color", &UnionVM::color);
        un.RegisterMember("points", &UnionVM::points);
        un.RegisterMember("mine", &UnionVM::mine);
    }
    constructor.RegisterArray<std::vector<UnionVM>>();

    constructor.Bind("name", &m_strName);
    constructor.Bind("subtitle", &m_strSubtitle);
    constructor.Bind("level", &m_iLevel);
    constructor.Bind("exp", &m_strExp);
    constructor.Bind("exp_pct", &m_strExpPct);
    constructor.Bind("exp_width", &m_fExpPct);
    constructor.Bind("stamina", &m_strStamina);
    constructor.Bind("stamina_width", &m_fStaminaPct);
    constructor.Bind("points", &m_iPoints);
    constructor.Bind("attrs", &m_Attrs);
    constructor.Bind("combat", &m_Combat);
    constructor.Bind("tab", &m_iTab);
    constructor.Bind("union_name", &m_strUnion);
    constructor.Bind("union_color", &m_strUnionColor);
    constructor.Bind("unions", &m_Unions);

    constructor.BindEventCallback("set_tab",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int iTab = args[0].Get<int>();
            if ((iTab == 0 || iTab == 1) && iTab != m_iTab) {
                m_iTab = iTab;
                m_Model.DirtyVariable("tab");
            }
        });

    constructor.BindEventCallback("raise",
        [](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            ev.StopPropagation();
            if (args.empty() || g_pAVATAR == NULL)
                return;
            const int iAttr = args[0].Get<int>();
            if (iAttr < 0 || iAttr >= BA_MAX)
                return;
            int iTotal, iBase;
            AttrValues(g_pAVATAR, iAttr, iTotal, iBase);
            if (CanRaise(g_pAVATAR, iAttr, iBase))
                g_pNet->Send_cli_USE_BPOINT_REQ((BYTE)iAttr);
        });

    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { SetOpen(false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "character.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load character window document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("character");
    RoseRmlLayout::Track(m_pPanel, "character");

    LOG_INFO("[rmlui] character window document loaded");
    return true;
}

void
RoseRmlCharacterWindow::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

void
RoseRmlCharacterWindow::SetOpen(bool bOpen) {
    if (bOpen != m_bOpen)
        RoseUi2::PlayWindowSound(DLG_TYPE_CHAR, bOpen);
    m_bOpen = bOpen;
    if (bOpen)
        Sample(); /// no stale numbers on the first frame
}

void
RoseRmlCharacterWindow::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlCharacterWindow::Sample() {
    CObjUSER* pAvatar = g_pAVATAR;
    if (pAvatar == NULL)
        return;

    /// --- identity ---------------------------------------------------------------
    const char* pszName = pAvatar->Get_NAME();
    Rml::String strName = pszName ? pszName : "";
    if (strName != m_strName) {
        m_strName.swap(strName);
        m_Model.DirtyVariable("name");
    }

    const char* pszJob = CStringManager::GetSingleton().GetJobName(pAvatar->Get_JOB());
    Rml::String strSubtitle = (pszJob && pszJob[0]) ? pszJob : "Visitor";
    const char* pszClan = CClan::GetInstance().GetName();
    if (pszClan && pszClan[0]) {
        strSubtitle += "  \xC2\xB7  "; /// middle dot, UTF-8
        strSubtitle += pszClan;
    }
    if (strSubtitle != m_strSubtitle) {
        m_strSubtitle.swap(strSubtitle);
        m_Model.DirtyVariable("subtitle");
    }

    const int iLevel = pAvatar->Get_LEVEL();
    if (iLevel != m_iLevel) {
        m_iLevel = iLevel;
        m_Model.DirtyVariable("level");
    }

    /// --- progress -----------------------------------------------------------------
    /// GetCur_EXP, not Get_EXP: the latter returns int and truncates the
    /// 64-bit EXP that the level-240 curve needs.
    const __int64 iExp = pAvatar->GetCur_EXP();
    const __int64 iNeedExp = pAvatar->Get_NeedEXP(iLevel);
    Rml::String strExp = Thousands(iExp) + " / "
        + Thousands(iNeedExp);
    if (strExp != m_strExp) {
        m_strExp.swap(strExp);
        m_Model.DirtyVariable("exp");
    }
    const float fExpPct = Percent(iExp, iNeedExp);
    if (fExpPct != m_fExpPct) {
        m_fExpPct = fExpPct;
        m_strExpPct = Printf("%.2f%%", fExpPct);
        m_Model.DirtyVariable("exp_width");
        m_Model.DirtyVariable("exp_pct");
    }

    const int iStamina = pAvatar->GetCur_STAMINA();
    const int iMaxStamina = pAvatar->GetCur_MaxSTAMINA();
    Rml::String strStamina = Thousands(iStamina) + " / "
        + Thousands(iMaxStamina);
    if (strStamina != m_strStamina) {
        m_strStamina.swap(strStamina);
        m_Model.DirtyVariable("stamina");
    }
    const float fStaminaPct = Percent(iStamina, iMaxStamina);
    if (fStaminaPct != m_fStaminaPct) {
        m_fStaminaPct = fStaminaPct;
        m_Model.DirtyVariable("stamina_width");
    }

    /// --- attributes ---------------------------------------------------------------
    const int iPoints = pAvatar->Get_BonusPOINT();
    if (iPoints != m_iPoints) {
        m_iPoints = iPoints;
        m_Model.DirtyVariable("points");
    }

    const DWORD dwNow = g_GameDATA.GetGameTime();
    std::vector<AttrVM> attrs;
    for (int i = 0; i < BA_MAX; ++i) {
        int iTotal, iBase;
        AttrValues(pAvatar, i, iTotal, iBase);

        /// A point went in ( the server's answer, not the click ): glow. The
        /// first sample only records, so opening the window flashes nothing.
        if (m_iPrevBase[i] >= 0 && iBase > m_iPrevBase[i]) {
            m_iFlash[i] = (m_iFlash[i] == 1) ? 2 : 1;
            m_dwFlashEnd[i] = dwNow + kFlashMs;
        } else if (m_iFlash[i] != 0 && (int)(dwNow - m_dwFlashEnd[i]) >= 0) {
            m_iFlash[i] = 0;
        }
        m_iPrevBase[i] = iBase;

        AttrVM vm;
        vm.id = i;
        vm.abbr = kAttrAbbr[i];
        vm.name = kAttrName[i];
        vm.total = iTotal;
        vm.bonus = iTotal - iBase;
        vm.can_up = CanRaise(pAvatar, i, iBase);
        if (iBase >= Rose::GameStaticConfig::MAX_STAT) {
            vm.up_text = "Max";
            vm.up_short = false;
        } else {
            const int iNeed = pAvatar->Get_NeedPoint2AbilityUP((short)i);
            vm.up_text = Printf("%d pt%s", iNeed, iNeed == 1 ? "" : "s");
            vm.up_short = iNeed > iPoints;
        }
        vm.flash = m_iFlash[i];
        attrs.push_back(vm);
    }
    if (attrs != m_Attrs) {
        m_Attrs.swap(attrs);
        m_Model.DirtyVariable("attrs");
    }

    /// --- combat ---------------------------------------------------------------------
    /// The legacy window's eight numbers, in its order.
    const int iValues[8] = {pAvatar->stats.attack_power,
        pAvatar->Get_DEF(),
        pAvatar->Get_RES(),
        pAvatar->stats.hit_rate,
        pAvatar->Get_CRITICAL(),
        pAvatar->Get_AVOID(),
        pAvatar->stats.attack_speed,
        pAvatar->stats.move_speed};

    std::vector<CombatVM> combat;
    for (int i = 0; i < 8; ++i) {
        CombatVM vm;
        vm.id = kCombatStatBase + i;
        vm.name = kCombatName[i];
        vm.value = Thousands(iValues[i]);
        combat.push_back(vm);
    }
    if (combat != m_Combat) {
        m_Combat.swap(combat);
        m_Model.DirtyVariable("combat");
    }

    SampleUnions();
}

void
RoseRmlCharacterWindow::SampleUnions() {
    CObjUSER* pAvatar = g_pAVATAR;
    const int iMine = pAvatar->Get_UNION();

    Rml::String strUnion = "None";
    Rml::String strColor = "#9a9a9a";
    if (iMine > 0 && iMine < (int)g_TblUnion.row_count) {
        const char* pszName = UNION_NAME(iMine);
        if (pszName && pszName[0]) {
            strUnion = pszName;
            strColor = UnionColor(iMine);
        }
    }
    if (strUnion != m_strUnion || strColor != m_strUnionColor) {
        m_strUnion.swap(strUnion);
        m_strUnionColor.swap(strColor);
        m_Model.DirtyVariable("union_name");
        m_Model.DirtyVariable("union_color");
    }

    std::vector<UnionVM> unions;
    for (int i = 1; i <= kMaxUnions && i < (int)g_TblUnion.row_count; ++i) {
        const char* pszName = UNION_NAME(i);
        if (pszName == NULL || pszName[0] == '\0')
            continue;

        UnionVM vm;
        vm.id = i;
        vm.name = pszName;
        vm.color = UnionColor(i);
        /// The union shops' own lookup ( CDealData ): union i, point slot i-1.
        vm.points = Thousands(pAvatar->Get_AbilityValue(AT_UNION_POINT1 - 1 + i));
        vm.mine = (i == iMine);
        unions.push_back(vm);
    }
    if (unions != m_Unions) {
        m_Unions.swap(unions);
        m_Model.DirtyVariable("unions");
    }
}

void
RoseRmlCharacterWindow::UpdateTooltip() {
    if (CDragNDropMgr::GetInstance().IsDraging())
        return;

    int iStat = -1;
    for (Rml::Element* pEl = RoseRmlLayout::HoverIn(m_pContext, m_pDocument); pEl != NULL;
         pEl = pEl->GetParentNode()) {
        if (pEl->HasAttribute("stat")) {
            iStat = pEl->GetAttribute<int>("stat", -1);
            break;
        }
    }

    const StatDescription* pDesc = NULL;
    if (iStat >= 0 && iStat < BA_MAX)
        pDesc = &g_BaseStatDescriptions[iStat];
    else if (iStat >= kCombatStatBase && iStat < kCombatStatBase + 8)
        pDesc = &g_DerivedStatDescriptions[iStat - kCombatStatBase];
    if (pDesc == NULL)
        return;

    CInfo ToolTip;
    ToolTip.Clear();
    BuildStatToolTip(*pDesc, ToolTip);

    RoseRmlUi::PlaceTooltipAtCursor(ToolTip);
}

void
RoseRmlCharacterWindow::Update() {
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

    UpdateTooltip();
}
