#include "stdafx.h"

#include "RoseRmlTargetFrame.h"
#include "RoseRmlIcons.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include "..\\Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../JCommandState.h"
#include "../CClientStorage.h"
#include "../System/CGame.h"
#include "../interface/CEnduranceProperty.h"
#include "../interface/io_imageres.h"

#include "rose/common/log.h"

#include <stdio.h>
#include <stdarg.h>

namespace {

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

/// The overhead tag's level-difference colours. MIRROR of
/// CNameBox::GetTargetMobNameColor and its g_dw*Name constants ( cnamebox.cpp,
/// file-local, so they cannot be shared ) -- keep the two in step.
const char*
LevelColor(int iAvatarLv, int iMobLv) {
    const int iDiff = iAvatarLv - iMobLv;
    if (iDiff <= -23)
        return "#e095ff"; /// violet
    if (iDiff <= -16)
        return "#ff88c8"; /// pink
    if (iDiff <= -10)
        return "#ff716b"; /// red
    if (iDiff <= -4)
        return "#ffa66b"; /// orange
    if (iDiff <= 3)
        return "#ffe47a"; /// yellow
    if (iDiff <= 8)
        return "#96ff7a"; /// green
    if (iDiff <= 14)
        return "#89f3ff"; /// blue
    if (iDiff <= 21)
        return "#caf3ff"; /// light blue
    return "#d9d9d9"; /// gray
}

const char* kSummonColor = "#89f3ff"; ///< CNameBox: summons are always blue
const char* kNpcColor = "#e7ffae"; ///< CNameBox::DrawNpcName's name colour
const char* kPlainColor = "#f0f0f0";

} // namespace

RoseRmlTargetFrame::RoseRmlTargetFrame():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false),
    m_iLastTarget(0),
    /// Bound through data-style-color, which is applied as soon as the document
    /// loads: an empty string parses as "color: ;" and logs a syntax warning
    /// ( and the RmlUi debugger pops its event log open ). Start valid.
    m_strNameColor(kPlainColor),
    m_bMark(false),
    m_bHp(false),
    m_fHpWidth(0.0f) {}

bool
RoseRmlTargetFrame::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("target");
    if (!constructor)
        return false;

    RoseRmlBuffBar::RegisterBuffStruct(constructor);

    constructor.Bind("name", &m_strName);
    constructor.Bind("name_color", &m_strNameColor);
    constructor.Bind("level", &m_strLevel);
    constructor.Bind("kind", &m_strKind);
    constructor.Bind("has_mark", &m_bMark);
    constructor.Bind("mark_src", &m_strMarkSrc);
    constructor.Bind("mark_rect", &m_strMarkRect);
    constructor.Bind("has_hp", &m_bHp);
    constructor.Bind("hp", &m_strHp);
    constructor.Bind("hp_pct", &m_strHpPct);
    constructor.Bind("hp_width", &m_fHpWidth);
    constructor.Bind("buffs", &m_Buffs);

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "target.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load target frame document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("target");
    RoseRmlLayout::Track(m_pPanel, "target");

    LOG_INFO("[rmlui] target frame document loaded");
    return true;
}

void
RoseRmlTargetFrame::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
}

void
RoseRmlTargetFrame::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlTargetFrame::Sample(CObjCHAR* pTarget) {
    CObjUSER* pAvatar = g_pAVATAR;
    const bool bSelf = (pTarget == (CObjCHAR*)pAvatar);
    const bool bMob = !bSelf && pTarget->IsA(OBJ_MOB);
    const bool bNpc = !bSelf && pTarget->IsA(OBJ_NPC);
    const bool bSummon =
        bMob && (pTarget->m_EndurancePack.GetStateFlag() & FLAG_ING_DEC_LIFE_TIME) != 0;

    /// --- name, colour, subtitle -----------------------------------------
    Rml::String strName = pTarget->Get_NAME() ? pTarget->Get_NAME() : "";
    Rml::String strKind;
    const char* pszColor = kPlainColor;

    if (bNpc) {
        /// NPC names carry their role in front: "[Weapon Merchant] Raffle".
        const size_t iClose = strName.find(']');
        if (!strName.empty() && strName[0] == '[' && iClose != Rml::String::npos) {
            strKind = strName.substr(1, iClose - 1);
            strName = strName.substr(iClose + 1);
            while (!strName.empty() && strName[0] == ' ')
                strName.erase(0, 1);
        }
        pszColor = kNpcColor;
    } else if (bSummon) {
        strKind = "Summon";
        pszColor = kSummonColor;
    } else if (bMob) {
        pszColor = LevelColor(pAvatar->Get_LEVEL(), pTarget->Get_LEVEL());
    } else if (bSelf) {
        strKind = "You";
    } else {
        strKind = "Player";
    }

    Assign(m_strName, strName, "name");
    Assign(m_strNameColor, Rml::String(pszColor), "name_color");
    Assign(m_strKind, strKind, "kind");
    Assign(m_strLevel,
        (bMob || bSelf) ? Printf("%d", pTarget->Get_LEVEL()) : Rml::String(), "level");

    /// --- type mark ( TARGETMARK.TSI, indexed by LIST_NPC NPC_TYPE ) ---------
    Rml::String strMarkSrc, strMarkRect;
    bool bMark = false;
    if (bMob) {
        const int iType = NPC_TYPE(pTarget->Get_CharNO());
        if (iType > 0)
            bMark = RoseRmlIcons::Resolve(IMAGE_RES_TARGETMARK, iType, strMarkSrc, strMarkRect);
    }
    Assign(m_bMark, bMark, "has_mark");
    Assign(m_strMarkSrc, strMarkSrc, "mark_src");
    Assign(m_strMarkRect, strMarkRect, "mark_rect");

    /// --- HP: monsters and yourself only ------------------------------------
    const bool bHp = bMob || bSelf;
    Assign(m_bHp, bHp, "has_hp");
    if (bHp) {
        const int iHp = max(0, pTarget->Get_HP());
        const int iMaxHp = pTarget->Get_MaxHP();
        const float fPct = (iMaxHp > 0) ? 100.0f * (float)min(iHp, iMaxHp) / (float)iMaxHp : 0.0f;

        const bool bNumbers = bSelf || g_ClientStorage.IsShowMobHp();
        Assign(m_strHp, bNumbers ? Printf("%d / %d", iHp, iMaxHp) : Rml::String(), "hp");
        Assign(m_strHpPct, Printf("%d%%", (int)fPct), "hp_pct");

        if (m_HpBar.Step(fPct, g_GameDATA.GetGameTime())) {
            m_fHpWidth = m_HpBar.fShown;
            m_Model.DirtyVariable("hp_width");
        }
    }

    /// --- status effects -----------------------------------------------------
    std::vector<RoseRmlBuffBar::BuffVM> buffs;
    RoseRmlBuffBar::CollectBuffs(pTarget, buffs);
    if (buffs != m_Buffs) {
        m_Buffs.swap(buffs);
        m_Model.DirtyVariable("buffs");
    }
}

void
RoseRmlTargetFrame::Update() {
    if (m_pDocument == NULL)
        return;

    CObjCHAR* pTarget = NULL;
    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
    const int iTarget = bInWorld ? g_UserInputSystem.GetCurrentTarget() : 0;
    if (iTarget != 0) {
        /// bCheckHP: a dead target resolves to NULL, so the frame closes on
        /// the kill rather than lingering on a corpse.
        pTarget = g_pObjMGR->Get_CharOBJ(iTarget, true);
    }

    /// A new target starts its bar where its HP is, not where the last one's was.
    if (iTarget != m_iLastTarget) {
        m_HpBar.Reset();
        m_iLastTarget = iTarget;
    }

    if (pTarget != NULL)
        Sample(pTarget);
    SetVisible(pTarget != NULL);

    if (m_bVisible) {
        const Rml::Vector2i view = m_pContext->GetDimensions();
        RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
    }
}
