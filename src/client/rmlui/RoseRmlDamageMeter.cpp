#include "stdafx.h"

#include "RoseRmlDamageMeter.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include "..\\Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "Common/IO_Skill.h"

#include "rose/common/log.h"

#include <stdio.h>

namespace {

/// Same cadence as the legacy panel: aggregating a few times a second is
/// plenty, and it keeps the DOM churn ( and therefore reflow ) bounded.
const DWORD kSnapshotRefreshMs = 500;
const int kMaxRows = 10;

enum {
    VIEW_DAMAGE_DONE = 0,
    VIEW_MY_SKILLS,
    VIEW_DAMAGE_TAKEN,
    VIEW_COUNT,
};

const char*
ViewName(int iView) {
    switch (iView) {
        case VIEW_MY_SKILLS:
            return "My Skills";
        case VIEW_DAMAGE_TAKEN:
            return "Damage Taken";
        case VIEW_DAMAGE_DONE:
        default:
            return "Damage Done";
    }
}

} // namespace

RoseRmlDamageMeter::RoseRmlDamageMeter():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_bLive(false),
    m_iView(VIEW_DAMAGE_DONE),
    m_bVisible(false),
    m_dwLastRefresh(0) {}

Rml::String
RoseRmlDamageMeter::FormatThousands(__int64 value) {
    char szRaw[32];
    _snprintf(szRaw, sizeof(szRaw), "%I64d", value);
    szRaw[sizeof(szRaw) - 1] = '\0';

    const int iLen = (int)strlen(szRaw);
    Rml::String out;
    for (int i = 0; i < iLen; ++i) {
        if (i > 0 && ((iLen - i) % 3) == 0)
            out += ',';
        out += szRaw[i];
    }
    return out;
}

bool
RoseRmlDamageMeter::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("damage_meter");
    if (!constructor)
        return false;

    if (auto row = constructor.RegisterStruct<RowVM>()) {
        row.RegisterMember("name", &RowVM::name);
        row.RegisterMember("value", &RowVM::value);
        row.RegisterMember("pct", &RowVM::pct);
        row.RegisterMember("self", &RowVM::is_self);
    }
    constructor.RegisterArray<std::vector<RowVM>>();

    constructor.Bind("title", &m_strTitle);
    constructor.Bind("fight", &m_strFight);
    constructor.Bind("footer", &m_strFooter);
    constructor.Bind("live", &m_bLive);
    constructor.Bind("rows", &m_Rows);

    /// Header buttons. Bound by name so the markup decides which element does
    /// what -- no hit-test rectangles, no ordering constraints against the
    /// legacy ProcWndMsgInstant panel chain.
    constructor.BindEventCallback("cycle_view",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { CycleView(); });
    constructor.BindEventCallback("reset_data",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { ResetData(); });
    constructor.BindEventCallback("close_panel",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Hide(); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "damagemeter.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load damage meter document '{}'", strDoc.c_str());
        return false;
    }

    m_strTitle = ViewName(m_iView);
    LOG_INFO("[rmlui] damage meter document loaded");
    return true;
}

void
RoseRmlDamageMeter::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_bVisible = false;
}

void
RoseRmlDamageMeter::Show() {
    if (m_pDocument == NULL)
        return;
    m_pDocument->Show();
    m_bVisible = true;
    m_dwLastRefresh = 0; /// force an immediate aggregate
}

void
RoseRmlDamageMeter::Hide() {
    if (m_pDocument == NULL)
        return;
    m_pDocument->Hide();
    m_bVisible = false;
}

void
RoseRmlDamageMeter::Toggle() {
    if (m_bVisible)
        Hide();
    else
        Show();
}

void
RoseRmlDamageMeter::CycleView() {
    m_iView = (m_iView + 1) % VIEW_COUNT;
    m_strTitle = ViewName(m_iView);
    m_Model.DirtyVariable("title");
    m_dwLastRefresh = 0;
}

void
RoseRmlDamageMeter::ResetData() {
    CDamageMeter::GetInstance().Reset();
    m_dwLastRefresh = 0;
}

void
RoseRmlDamageMeter::Update() {
    if (!m_bVisible || m_pDocument == NULL || g_pAVATAR == NULL)
        return;

    const DWORD dwNow = g_GameDATA.GetGameTime();
    if (m_dwLastRefresh != 0 && (dwNow - m_dwLastRefresh) < kSnapshotRefreshMs)
        return;
    m_dwLastRefresh = dwNow;

    CDamageMeter::GetInstance().BuildSnapshot(m_Snapshot);
    RebuildRows();
}

void
RoseRmlDamageMeter::RebuildRows() {
    const std::vector<CDamageMeter::Row>* pRows = NULL;
    switch (m_iView) {
        case VIEW_MY_SKILLS:
            pRows = &m_Snapshot.OutgoingSkillsSelf;
            break;
        case VIEW_DAMAGE_TAKEN:
            pRows = &m_Snapshot.IncomingByAttacker;
            break;
        case VIEW_DAMAGE_DONE:
        default:
            pRows = &m_Snapshot.OutgoingByAttacker;
            break;
    }

    const DWORD dwDurMs = m_Snapshot.DurationMs();
    const int iDurSec = (int)(dwDurMs / 1000);
    const double dDurSec = (dwDurMs > 0) ? (dwDurMs / 1000.0) : 1.0;

    char szBuf[160];
    _snprintf(szBuf, sizeof(szBuf), "Fight %d:%02d", iDurSec / 60, iDurSec % 60);
    szBuf[sizeof(szBuf) - 1] = '\0';
    m_strFight = szBuf;
    m_bLive = m_Snapshot.bActive;

    __int64 iViewTotal = 0;
    for (size_t i = 0; i < pRows->size(); ++i)
        iViewTotal += (*pRows)[i].iTotal;
    if (iViewTotal < 1)
        iViewTotal = 1;

    __int64 iTopTotal = 1;
    if (!pRows->empty() && (*pRows)[0].iTotal > 0)
        iTopTotal = (*pRows)[0].iTotal;

    const char* pSelfName = g_pAVATAR->Get_NAME();
    const int iCount = min((int)pRows->size(), kMaxRows);

    m_Rows.clear();
    m_Rows.reserve(iCount);

    for (int i = 0; i < iCount; ++i) {
        const CDamageMeter::Row& src = (*pRows)[i];

        RowVM vm;
        vm.name = src.strName.c_str();
        vm.pct = (float)((double)src.iTotal * 100.0 / (double)iTopTotal);
        vm.is_self = (m_iView == VIEW_DAMAGE_DONE && pSelfName != NULL
            && src.strName == pSelfName);

        const int iPct = (int)(src.iTotal * 100 / iViewTotal);
        if (m_iView == VIEW_DAMAGE_DONE) {
            const int iDps = (int)(src.iTotal / dDurSec);
            _snprintf(szBuf, sizeof(szBuf), "%s (%d/s, %d%%)",
                FormatThousands(src.iTotal).c_str(), iDps, iPct);
        } else {
            _snprintf(szBuf, sizeof(szBuf), "%s (x%d, %d%%)",
                FormatThousands(src.iTotal).c_str(), src.iHits, iPct);
        }
        szBuf[sizeof(szBuf) - 1] = '\0';
        vm.value = szBuf;

        m_Rows.push_back(vm);
    }

    /// Footer: biggest hit across the whole view, same as the legacy panel.
    int iMaxHit = 0;
    int iMaxHitSkill = 0;
    std::string strMaxOwner;
    for (size_t i = 0; i < pRows->size(); ++i) {
        if ((*pRows)[i].iMaxHit > iMaxHit) {
            iMaxHit = (*pRows)[i].iMaxHit;
            iMaxHitSkill = (*pRows)[i].iMaxHitSkill;
            strMaxOwner = (*pRows)[i].strName;
        }
    }

    if (iMaxHit > 0) {
        const char* pSkillName = (iMaxHitSkill > 0) ? SKILL_NAME(iMaxHitSkill) : NULL;
        if (pSkillName != NULL && pSkillName[0] != '\0') {
            _snprintf(szBuf, sizeof(szBuf), "Biggest hit: %s (%s)",
                FormatThousands(iMaxHit).c_str(), pSkillName);
        } else {
            _snprintf(szBuf, sizeof(szBuf), "Biggest hit: %s",
                FormatThousands(iMaxHit).c_str());
        }
        szBuf[sizeof(szBuf) - 1] = '\0';
        m_strFooter = szBuf;
    } else {
        m_strFooter = "No combat data.  ( /dps to hide )";
    }

    m_Model.DirtyVariable("rows");
    m_Model.DirtyVariable("fight");
    m_Model.DirtyVariable("footer");
    m_Model.DirtyVariable("live");
}
