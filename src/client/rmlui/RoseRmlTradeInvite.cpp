#include "stdafx.h"

#include "RoseRmlTradeInvite.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
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

/// An unanswered request is declined after this long.
const DWORD kTimeoutMs = 30 * 1000;

} // namespace

RoseRmlTradeInvite::RoseRmlTradeInvite():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false),
    m_bPending(false),
    m_wFromObjSvrIdx(0),
    m_dwShownAt(0),
    m_fTimeLeft(1.0f),
    m_iSecondsLeft(0) {}

bool
RoseRmlTradeInvite::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("tradeinvite");
    if (!constructor)
        return false;

    constructor.Bind("from", &m_strFrom);
    constructor.Bind("time_left", &m_fTimeLeft);
    constructor.Bind("seconds", &m_iSecondsLeft);

    constructor.BindEventCallback("accept",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Answer(true); });
    constructor.BindEventCallback("decline",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Answer(false); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "tradeinvite.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load trade invite document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("tradeinvite");
    RoseRmlLayout::Track(m_pPanel, "tradeinvite");

    LOG_INFO("[rmlui] trade invite document loaded");
    return true;
}

void
RoseRmlTradeInvite::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bPending = false;
}

bool
RoseRmlTradeInvite::Show(unsigned short wFromObjSvrIdx, const char* pszFrom) {
    if (m_pDocument == NULL || !RoseUi2::IsActive() || m_bPending)
        return false;

    m_bPending = true;
    m_wFromObjSvrIdx = wFromObjSvrIdx;
    m_dwShownAt = GetTickCount();

    m_strFromGame = pszFrom ? pszFrom : "";
    m_strFrom = RoseRmlText::FromGame(pszFrom);
    m_fTimeLeft = 1.0f;
    m_iSecondsLeft = (int)(kTimeoutMs / 1000);
    m_Model.DirtyVariable("from");
    m_Model.DirtyVariable("time_left");
    m_Model.DirtyVariable("seconds");
    return true;
}

void
RoseRmlTradeInvite::Answer(bool bAccept) {
    if (!m_bPending)
        return;
    m_bPending = false;

    /// The legacy message box's own commands, so the reply is identical.
    /// Accept starts the trade and opens the trade window and the bag.
    if (bAccept) {
        CTCmdAcceptTradeReq cmd(m_wFromObjSvrIdx);
        cmd.Exec(NULL);
    } else {
        CTCmdRejectTradeReq cmd(m_wFromObjSvrIdx);
        cmd.Exec(NULL);
    }
}

void
RoseRmlTradeInvite::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlTradeInvite::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bPending) {
        /// Leaving the world or switching back to classic with a request up
        /// declines it, rather than leaving the other player waiting.
        const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
            && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
        const DWORD dwElapsed = GetTickCount() - m_dwShownAt;

        if (!bInWorld) {
            Answer(false);
        } else if (dwElapsed >= kTimeoutMs) {
            const std::string strFrom = m_strFromGame;
            Answer(false);
            g_itMGR.AppendChatMsg(
                CStr::Printf("Trade request from %s declined (no answer).", strFrom.c_str()),
                IT_MGR::CHAT_TYPE_SYSTEM);
        } else {
            /// Hundredths: the bar moves by transform, so a change costs no
            /// layout, but there is no need to dirty it every frame either.
            const float fLeft =
                floorf(100.0f * (float)(kTimeoutMs - dwElapsed) / (float)kTimeoutMs) / 100.0f;
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
