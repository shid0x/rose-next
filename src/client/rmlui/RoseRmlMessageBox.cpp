#include "stdafx.h"

#include "RoseRmlMessageBox.h"
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
#include "tgamectrl/tcommand.h"

#include "rose/common/log.h"

#include <math.h>

namespace {

/// A notice closes itself after this long.
const DWORD kNoticeMs = 8 * 1000;

bool
InWorld() {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

} // namespace

RoseRmlMessageBox::RoseRmlMessageBox():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false),
    m_dwShownAt(0),
    m_bNotice(false),
    m_fTimeLeft(100.0f) {}

RoseRmlMessageBox::~RoseRmlMessageBox() {
    Shutdown();
}

bool
RoseRmlMessageBox::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("msgbox");
    if (!constructor)
        return false;

    constructor.Bind("title", &m_strTitle);
    constructor.Bind("text", &m_strText);
    constructor.Bind("ok", &m_strOk);
    constructor.Bind("cancel", &m_strCancel);
    constructor.Bind("notice", &m_bNotice);
    constructor.Bind("time_left", &m_fTimeLeft);

    constructor.BindEventCallback("answer",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            Answer(!args.empty() && args[0].Get<bool>());
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "msgbox.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load message box document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("msgbox");
    RoseRmlLayout::Track(m_pPanel, "msgbox");

    LOG_INFO("[rmlui] message box document loaded");
    return true;
}

void
RoseRmlMessageBox::Shutdown() {
    /// Unanswered questions die with the UI; their commands are ours to free.
    while (!m_Queue.empty()) {
        delete m_Queue.front().pOk;
        delete m_Queue.front().pCancel;
        m_Queue.pop_front();
    }
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
}

bool
RoseRmlMessageBox::CanShow() const {
    return m_pDocument != NULL && InWorld();
}

bool
RoseRmlMessageBox::Confirm(const char* pszTitle,
    const char* pszText,
    const char* pszOk,
    const char* pszCancel,
    CTCommand* pOk,
    CTCommand* pCancel) {
    if (!CanShow())
        return false;

    Entry entry;
    entry.title = pszTitle ? pszTitle : "";
    entry.text = pszText ? pszText : "";
    entry.ok = pszOk ? pszOk : "OK";
    entry.cancel = (pszCancel && pszCancel[0]) ? pszCancel : "Cancel";
    entry.pOk = pOk;
    entry.pCancel = pCancel;
    Push(entry);
    return true;
}

bool
RoseRmlMessageBox::Notice(const char* pszTitle, const char* pszText) {
    if (!CanShow())
        return false;

    Entry entry;
    entry.title = pszTitle ? pszTitle : "";
    entry.text = pszText ? pszText : "";
    entry.ok = "OK";
    entry.pOk = NULL;
    entry.pCancel = NULL;
    Push(entry);
    return true;
}

void
RoseRmlMessageBox::Push(const Entry& entry) {
    m_Queue.push_back(entry);
    if (m_Queue.size() == 1)
        Present();
}

void
RoseRmlMessageBox::Present() {
    if (m_Queue.empty())
        return;

    const Entry& entry = m_Queue.front();
    m_strTitle = entry.title;
    m_strText = entry.text;
    m_strOk = entry.ok;
    m_strCancel = entry.cancel;
    m_bNotice = entry.cancel.empty();
    m_fTimeLeft = 100.0f;
    m_dwShownAt = GetTickCount();

    m_Model.DirtyVariable("title");
    m_Model.DirtyVariable("text");
    m_Model.DirtyVariable("ok");
    m_Model.DirtyVariable("cancel");
    m_Model.DirtyVariable("notice");
    m_Model.DirtyVariable("time_left");
}

void
RoseRmlMessageBox::Answer(bool bOk) {
    if (m_Queue.empty())
        return;

    Entry entry = m_Queue.front();
    m_Queue.pop_front();

    /// Run the chosen command the way CMsgBox does ( IT_MGR executes and
    /// frees it ); free the other.
    CTCommand* pRun = bOk ? entry.pOk : entry.pCancel;
    CTCommand* pDrop = bOk ? entry.pCancel : entry.pOk;
    delete pDrop;
    if (pRun)
        g_itMGR.AddTCommand(0, pRun);

    Present();
}

void
RoseRmlMessageBox::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlMessageBox::Update() {
    if (m_pDocument == NULL)
        return;

    /// Out of the world: questions are answered "no", notices dropped.
    if (!InWorld()) {
        while (!m_Queue.empty())
            Answer(false);
    }

    if (!m_Queue.empty() && m_bNotice) {
        const DWORD dwElapsed = GetTickCount() - m_dwShownAt;
        if (dwElapsed >= kNoticeMs) {
            Answer(true);
        } else {
            const float fLeft =
                floorf(1000.0f * (float)(kNoticeMs - dwElapsed) / (float)kNoticeMs) / 10.0f;
            if (fLeft != m_fTimeLeft) {
                m_fTimeLeft = fLeft;
                m_Model.DirtyVariable("time_left");
            }
        }
    }

    SetVisible(!m_Queue.empty());
    if (m_bVisible) {
        const Rml::Vector2i view = m_pContext->GetDimensions();
        RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
    }
}
