#include "stdafx.h"

#include "RoseRmlStatusPanel.h"
#include "RoseRmlLayout.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "..\\Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../JCommandState.h"
#include "../System/CGame.h"
#include "../gamecommon/StringManager.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"

#include "rose/common/log.h"

#include <stdio.h>

namespace {

/// Below this share of max HP the bar switches to its warning look.
const float kLowHpPct = 25.0f;

/// A press that moves the panel less than this ( in pixels, either axis ) is a
/// click. RmlUi has no drag threshold, so hand jitter alone starts a "drag".
const int kClickSlop = 4;

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

} // namespace

RoseRmlStatusPanel::RoseRmlStatusPanel():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_bVisible(false),
    m_pPanel(NULL),
    m_iPressX(0),
    m_iPressY(0),
    m_fHpWidth(0.0f),
    m_fMpWidth(0.0f),
    m_fExpWidth(0.0f),
    m_bHpLow(false) {
}

bool
RoseRmlStatusPanel::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("status");
    if (!constructor)
        return false;

    constructor.Bind("name", &m_strName);
    constructor.Bind("level", &m_strLevel);
    constructor.Bind("job", &m_strJob);
    constructor.Bind("hp", &m_strHp);
    constructor.Bind("hp_pct", &m_strHpPct);
    constructor.Bind("hp_width", &m_fHpWidth);
    constructor.Bind("hp_low", &m_bHpLow);
    constructor.Bind("mp", &m_strMp);
    constructor.Bind("mp_pct", &m_strMpPct);
    constructor.Bind("mp_width", &m_fMpWidth);
    constructor.Bind("exp_pct", &m_strExpPct);
    constructor.Bind("exp_width", &m_fExpWidth);
    constructor.Bind("weight", &m_strWeight);

    constructor.BindEventCallback("press",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            m_iPressX = ev.GetParameter<int>("mouse_x", 0);
            m_iPressY = ev.GetParameter<int>("mouse_y", 0);
        });

    /// Clicking the panel targets yourself, as clicking the legacy name strip
    /// ( IID_BTN_SELFTARGET ) did -- unless the press was really a move.
    constructor.BindEventCallback("self_target",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            const int dx = ev.GetParameter<int>("mouse_x", 0) - m_iPressX;
            const int dy = ev.GetParameter<int>("mouse_y", 0) - m_iPressY;
            if (abs(dx) >= kClickSlop || abs(dy) >= kClickSlop)
                return;
            g_UserInputSystem.SetTargetSelf();
        });

    /// The menu button sits inside the panel; stop the click here or it also
    /// bubbles up to the panel and retargets you.
    constructor.BindEventCallback("open_menu",
        [](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            ev.StopPropagation();
            g_itMGR.OpenDialog(DLG_TYPE_MENU);
        });

    /// Interface settings ( scale, lock, reset layout ). Same bubbling guard.
    constructor.BindEventCallback("open_ui",
        [](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            ev.StopPropagation();
            RoseRmlUi::ToggleInterfacePanel();
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "status.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load status panel document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("status");
    RoseRmlLayout::Track(m_pPanel, "status");

    LOG_INFO("[rmlui] status panel document loaded");
    return true;
}

void
RoseRmlStatusPanel::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
}

void
RoseRmlStatusPanel::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible) {
        /// Start the bars where the values are, not where they were when the
        /// panel was last seen ( another character, before a zone change ).
        m_HpBar.Reset();
        m_MpBar.Reset();
        m_ExpBar.Reset();
        m_pDocument->Show();
    } else {
        m_pDocument->Hide();
    }
}

void
RoseRmlStatusPanel::Sample() {
    CObjUSER* pAvatar = g_pAVATAR;

    Assign(m_strName, Rml::String(pAvatar->Get_NAME() ? pAvatar->Get_NAME() : ""), "name");
    Assign(m_strLevel, Printf("%d", pAvatar->Get_LEVEL()), "level");

    const char* pszJob = CStringManager::GetSingleton().GetJobName(pAvatar->Get_JOB());
    Assign(m_strJob, Rml::String(pszJob ? pszJob : ""), "job");

    /// Numbers are exact and immediate; only the bars ease.
    const int iHp = max(0, pAvatar->Get_HP());
    const int iMaxHp = pAvatar->Get_MaxHP();
    const float fHpPct = Percent(iHp, iMaxHp);
    Assign(m_strHp, Printf("%d / %d", iHp, iMaxHp), "hp");
    Assign(m_strHpPct, Printf("%d%%", (int)fHpPct), "hp_pct");
    Assign(m_bHpLow, iMaxHp > 0 && fHpPct < kLowHpPct, "hp_low");

    const int iMp = max(0, pAvatar->Get_MP());
    const int iMaxMp = pAvatar->Get_MaxMP();
    const float fMpPct = Percent(iMp, iMaxMp);
    Assign(m_strMp, Printf("%d / %d", iMp, iMaxMp), "mp");
    Assign(m_strMpPct, Printf("%d%%", (int)fMpPct), "mp_pct");

    /// GetCur_EXP, not Get_EXP: the latter returns int and truncates the
    /// 64-bit EXP that the level-240 curve needs.
    const __int64 iExp = pAvatar->GetCur_EXP();
    const __int64 iNeedExp = pAvatar->Get_NeedEXP(pAvatar->Get_LEVEL());
    const float fExpPct = Percent(iExp, iNeedExp);
    Assign(m_strExpPct, Printf("%.2f%%", fExpPct), "exp_pct");

    const int iMaxWeight = pAvatar->GetCur_MaxWEIGHT();
    const int iWeightPct =
        (iMaxWeight > 0) ? (int)(pAvatar->GetCur_WEIGHT() * 100 / iMaxWeight) : 0;
    Assign(m_strWeight, Printf("%d%%", iWeightPct), "weight");

    const DWORD dwNow = g_GameDATA.GetGameTime();
    if (m_HpBar.Step(fHpPct, dwNow)) {
        m_fHpWidth = m_HpBar.fShown;
        m_Model.DirtyVariable("hp_width");
    }
    if (m_MpBar.Step(fMpPct, dwNow)) {
        m_fMpWidth = m_MpBar.fShown;
        m_Model.DirtyVariable("mp_width");
    }
    if (m_ExpBar.Step(fExpPct, dwNow)) {
        m_fExpWidth = m_ExpBar.fShown;
        m_Model.DirtyVariable("exp_width");
    }
}

void
RoseRmlStatusPanel::Update() {
    if (m_pDocument == NULL)
        return;

    /// Shown only in the world, and only while UI2 is the chosen interface;
    /// RoseUi2 keeps the legacy CAvatarInfoDlg hidden for the same condition.
    const bool bWant = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
    SetVisible(bWant);

    if (m_bVisible) {
        const Rml::Vector2i view = m_pContext->GetDimensions();
        RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
        Sample();
    }
}
