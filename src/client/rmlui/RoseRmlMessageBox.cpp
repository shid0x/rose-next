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

/// Plain text as RML: markup characters escaped, line breaks kept.
Rml::String
EscapeRml(const std::string& strText) {
    Rml::String out;
    out.reserve(strText.size() + 16);
    for (size_t i = 0; i < strText.size(); ++i) {
        const char c = strText[i];
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\n':
                out += "<br/>";
                break;
            case '\r':
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

} // namespace

RoseRmlMessageBox::RoseRmlMessageBox():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bVisible(false),
    m_dwShownAt(0),
    m_bSingle(false),
    m_bTimed(false),
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
    constructor.Bind("single", &m_bSingle);
    constructor.Bind("timed", &m_bTimed);
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
    /// Unanswered entries die with the UI; their commands are ours to free.
    while (!m_Queue.empty()) {
        delete m_Queue.front().req.pOk;
        delete m_Queue.front().req.pCancel;
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
RoseRmlMessageBox::Show(const Request& request) {
    if (!CanShow())
        return false;

    Entry entry;
    entry.req = request;
    if (entry.req.ok.empty())
        entry.req.ok = "OK";
    entry.rml = request.bRml ? Rml::String(request.text) : EscapeRml(request.text);

    if (request.bUrgent && !m_Queue.empty()) {
        m_Queue.push_front(entry);
        Present(); /// the shown one steps back and waits
    } else {
        m_Queue.push_back(entry);
        if (m_Queue.size() == 1)
            Present();
    }
    return true;
}

bool
RoseRmlMessageBox::Confirm(const char* pszTitle,
    const char* pszText,
    const char* pszOk,
    const char* pszCancel,
    CTCommand* pOk,
    CTCommand* pCancel) {
    Request req;
    req.title = pszTitle ? pszTitle : "";
    req.text = pszText ? pszText : "";
    req.ok = pszOk ? pszOk : "OK";
    req.cancel = (pszCancel && pszCancel[0]) ? pszCancel : "Cancel";
    req.pOk = pOk;
    req.pCancel = pCancel;
    return Show(req);
}

bool
RoseRmlMessageBox::Notice(const char* pszTitle, const char* pszText) {
    Request req;
    req.title = pszTitle ? pszTitle : "";
    req.text = pszText ? pszText : "";
    req.dwTimeoutMs = kNoticeMs;
    req.bTimeoutOk = true;
    return Show(req);
}

bool
RoseRmlMessageBox::IsTypePending(int iType) const {
    if (iType == 0)
        return false;
    for (size_t i = 0; i < m_Queue.size(); ++i) {
        if (m_Queue[i].req.iType == iType)
            return true;
    }
    return false;
}

void
RoseRmlMessageBox::CloseType(int iType) {
    if (iType == 0)
        return;
    const bool bFrontGoes = !m_Queue.empty() && m_Queue.front().req.iType == iType;
    for (std::deque<Entry>::iterator it = m_Queue.begin(); it != m_Queue.end();) {
        if (it->req.iType == iType) {
            delete it->req.pOk;
            delete it->req.pCancel;
            it = m_Queue.erase(it);
        } else {
            ++it;
        }
    }
    if (bFrontGoes)
        Present();
}

void
RoseRmlMessageBox::SetTypeText(int iType, const char* pszText) {
    if (iType == 0)
        return;
    const Rml::String rml = EscapeRml(pszText ? pszText : "");
    for (size_t i = 0; i < m_Queue.size(); ++i) {
        if (m_Queue[i].req.iType != iType)
            continue;
        m_Queue[i].req.text = pszText ? pszText : "";
        m_Queue[i].req.bRml = false;
        m_Queue[i].rml = rml;
        if (i == 0 && rml != m_strText) {
            m_strText = rml;
            m_Model.DirtyVariable("text");
        }
    }
}

void
RoseRmlMessageBox::Present() {
    if (m_Queue.empty())
        return;

    const Entry& entry = m_Queue.front();
    m_strTitle = entry.req.title;
    m_strText = entry.rml;
    m_strOk = entry.req.ok;
    m_strCancel = entry.req.cancel;
    m_bSingle = entry.req.cancel.empty();
    m_bTimed = entry.req.dwTimeoutMs > 0;
    m_fTimeLeft = 100.0f;
    m_dwShownAt = GetTickCount();

    m_Model.DirtyVariable("title");
    m_Model.DirtyVariable("text");
    m_Model.DirtyVariable("ok");
    m_Model.DirtyVariable("cancel");
    m_Model.DirtyVariable("single");
    m_Model.DirtyVariable("timed");
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
    CTCommand* pRun = bOk ? entry.req.pOk : entry.req.pCancel;
    CTCommand* pDrop = bOk ? entry.req.pCancel : entry.req.pOk;
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

    /// Out of the world: requests are declined, questions answered "no",
    /// one-button entries dropped without running.
    if (!InWorld()) {
        while (!m_Queue.empty())
            Answer(false);
    }

    if (!m_Queue.empty()) {
        const Request& req = m_Queue.front().req;
        if (req.valid && !req.valid()) {
            /// The request lost its point ( a cart ride whose carts drifted
            /// apart ): declined, as the classic box did.
            Answer(false);
        } else if (req.dwTimeoutMs > 0) {
            const DWORD dwElapsed = GetTickCount() - m_dwShownAt;
            if (dwElapsed >= req.dwTimeoutMs) {
                const std::string strChat = req.timeoutChat;
                Answer(req.bTimeoutOk);
                if (!strChat.empty())
                    g_itMGR.AppendChatMsg(strChat.c_str(), IT_MGR::CHAT_TYPE_SYSTEM);
            } else {
                const float fLeft = floorf(1000.0f * (float)(req.dwTimeoutMs - dwElapsed)
                                        / (float)req.dwTimeoutMs)
                    / 10.0f;
                if (fLeft != m_fTimeLeft) {
                    m_fTimeLeft = fLeft;
                    m_Model.DirtyVariable("time_left");
                }
            }
        }
    }

    SetVisible(!m_Queue.empty());
    if (m_bVisible) {
        const Rml::Vector2i view = m_pContext->GetDimensions();
        RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
    }
}
