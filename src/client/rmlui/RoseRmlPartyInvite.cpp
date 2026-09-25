#include "stdafx.h"

#include "RoseRmlPartyInvite.h"
#include "RoseRmlLayout.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../System/CGame.h"
#include "../interface/it_mgr.h"
#include "../interface/Command/CTCmdHotExec.h"

#include "rose/common/log.h"

#include <math.h>

namespace {

/// An unanswered invitation is declined after this long.
const DWORD kTimeoutMs = 30 * 1000;

} // namespace

RoseRmlPartyInvite::RoseRmlPartyInvite():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false),
    m_bPending(false),
    m_wFromObjSvrIdx(0),
    m_dwShownAt(0),
    m_bMake(false),
    m_fTimeLeft(100.0f),
    m_iSecondsLeft(0) {}

bool
RoseRmlPartyInvite::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("partyinvite");
    if (!constructor)
        return false;

    constructor.Bind("from", &m_strFrom);
    constructor.Bind("make", &m_bMake);
    constructor.Bind("time_left", &m_fTimeLeft);
    constructor.Bind("seconds", &m_iSecondsLeft);

    constructor.BindEventCallback("accept",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Answer(true); });
    constructor.BindEventCallback("decline",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Answer(false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "partyinvite.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load party invite document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("partyinvite");
    RoseRmlLayout::Track(m_pPanel, "partyinvite");

    LOG_INFO("[rmlui] party invite document loaded");
    return true;
}

void
RoseRmlPartyInvite::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bPending = false;
}

bool
RoseRmlPartyInvite::Show(unsigned short wFromObjSvrIdx, const char* pszFrom, bool bMake) {
    if (m_pDocument == NULL || !RoseUi2::IsActive() || m_bPending)
        return false;

    m_bPending = true;
    m_wFromObjSvrIdx = wFromObjSvrIdx;
    m_dwShownAt = GetTickCount();

    m_strFrom = pszFrom ? pszFrom : "";
    m_bMake = bMake;
    m_fTimeLeft = 100.0f;
    m_iSecondsLeft = (int)(kTimeoutMs / 1000);
    m_Model.DirtyVariable("from");
    m_Model.DirtyVariable("make");
    m_Model.DirtyVariable("time_left");
    m_Model.DirtyVariable("seconds");
    return true;
}

void
RoseRmlPartyInvite::Answer(bool bAccept) {
    if (!m_bPending)
        return;
    m_bPending = false;

    /// The legacy message box's own commands, so the reply is identical.
    if (bAccept) {
        CTCmdAcceptPartyJoin cmd(m_wFromObjSvrIdx);
        cmd.Exec(NULL);
    } else {
        CTCmdRejectPartyJoin cmd(m_wFromObjSvrIdx);
        cmd.Exec(NULL);
    }
}

void
RoseRmlPartyInvite::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlPartyInvite::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bPending) {
        /// Leaving the world or switching back to classic with an invitation
        /// open answers it, rather than leaving the inviter waiting.
        const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
            && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
        const DWORD dwElapsed = GetTickCount() - m_dwShownAt;

        if (!bInWorld) {
            Answer(false);
        } else if (dwElapsed >= kTimeoutMs) {
            Answer(false);
            g_itMGR.AppendChatMsg(
                CStr::Printf("Party invitation from %s declined (no answer).", m_strFrom.c_str()),
                IT_MGR::CHAT_TYPE_SYSTEM);
        } else {
            const float fLeft = floorf(1000.0f * (float)(kTimeoutMs - dwElapsed) / (float)kTimeoutMs) / 10.0f;
            if (fLeft != m_fTimeLeft) {
                m_fTimeLeft = fLeft;
                m_Model.DirtyVariable("time_left");
            }
            const int iSeconds = (int)((kTimeoutMs - dwElapsed + 999) / 1000);
            if (iSeconds != m_iSecondsLeft) {
                m_iSecondsLeft = iSeconds;
                m_Model.DirtyVariable("seconds");
            }
        }
    }

    SetVisible(m_bPending);
    if (m_bVisible) {
        const Rml::Vector2i view = m_pContext->GetDimensions();
        RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
    }
}
