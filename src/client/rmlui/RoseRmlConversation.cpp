#include "stdafx.h"

#include "RoseRmlConversation.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../Game.h"
#include "../CObjUSER.h"
#include "../Object.h"
#include "../IO_Terrain.h"
#include "../System/CGame.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Dlgs/CDialogDlg.h"

#include "rose/common/log.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>

namespace {

/// CDialogDlg::Update's reach: past this ( cm ) from its NPC the dialog closes.
const float kCloseDistance = 1000.0f;

/// Room left under the panel for the skill bar, in dp, when it is placed by
/// default ( bottom centre, above the bar ).
const float kBottomGap = 120.0f;

/// CJStringParser's {FC=n} palette, lifted for dark glass: the classic dialog
/// drew dark text on parchment, and its black, dark red or dark blue would
/// vanish here. Same hues, in the classic order.
const char* const kPalette[19] = {
    "#e6e6e6", ///<  0 black -> the normal text colour
    "#ff7a6a", ///<  1 dark red
    "#7ad04a", ///<  2 dark green
    "#7aa8ff", ///<  3 dark blue
    "#e0c050", ///<  4 dark yellow
    "#d080ff", ///<  5 dark purple
    "#60d0d0", ///<  6 dark cyan
    "#a0a0a0", ///<  7 dark grey
    "#c8c8c8", ///<  8 light grey
    "#b0e0b0", ///<  9 pale green
    "#b8b8f0", ///< 10 pale blue-violet
    "#a6caf0", ///< 11 light blue
    "#ff5a4a", ///< 12 red
    "#5aff5a", ///< 13 green
    "#6a8aff", ///< 14 blue
    "#ffe050", ///< 15 yellow
    "#50ffff", ///< 16 cyan
    "#fff4e0", ///< 17 cream
    "#ffffff", ///< 18 white
};

void
AppendEscaped(Rml::String& out, const Rml::String& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        switch (text[i]) {
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
            default:
                out += text[i];
                break;
        }
    }
}

} // namespace

Rml::String
RoseRmlConversation::MarkupToRml(const char* pszText) {
    /// Converted first: the tags are ASCII, so they survive, and every byte of
    /// text that follows is already UTF-8.
    const Rml::String strText = RoseRmlText::FromGame(pszText);

    Rml::String out;
    Rml::String run;
    int iOpenColour = 0; ///< open <span> for colours ( CJStringParser has no nesting, a new colour replaces )
    bool bBold = false;

    size_t i = 0;
    while (i < strText.size()) {
        if (strText[i] != '{') {
            run += strText[i++];
            continue;
        }
        const size_t iClose = strText.find('}', i);
        if (iClose == Rml::String::npos) {
            /// CJStringParser swallows an unterminated tag; so do we.
            break;
        }
        AppendEscaped(out, run);
        run.clear();

        /// The tag, upper-cased and without spaces ( CJStringParser ignores them ).
        Rml::String tag;
        for (size_t k = i + 1; k < iClose; ++k) {
            if (strText[k] != ' ')
                tag += (char)toupper((unsigned char)strText[k]);
        }
        i = iClose + 1;

        const bool bEnd = !tag.empty() && tag[0] == '/';
        if (tag.find("FC") != Rml::String::npos) {
            if (bEnd) {
                if (iOpenColour > 0) {
                    out += "</span>";
                    --iOpenColour;
                }
            } else {
                const size_t iEq = tag.find('=');
                const int iColour = (iEq != Rml::String::npos) ? atoi(tag.c_str() + iEq + 1) : -1;
                if (iColour >= 0 && iColour < 19) {
                    out += "<span style=\"color: ";
                    out += kPalette[iColour];
                    out += "\">";
                    ++iOpenColour;
                }
            }
        } else if (!bEnd && tag.find("BR") != Rml::String::npos) {
            out += "<br/>";
        } else if (tag.find('B') != Rml::String::npos) {
            if (bEnd && bBold) {
                out += "</span>";
                bBold = false;
            } else if (!bEnd && !bBold) {
                out += "<span class=\"b\">";
                bBold = true;
            }
        }
        /// Anything else: dropped, as CJStringParser drops it.
    }
    AppendEscaped(out, run);

    if (bBold)
        out += "</span>";
    while (iOpenColour-- > 0)
        out += "</span>";
    return out;
}

RoseRmlConversation::RoseRmlConversation():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_iOwnerClientIdx(-1),
    m_iZoneAtOpen(-1),
    m_iMode(MODE_NPC) {}

bool
RoseRmlConversation::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("conversation");
    if (!constructor)
        return false;

    if (auto answer = constructor.RegisterStruct<AnswerVM>()) {
        answer.RegisterMember("index", &AnswerVM::index);
        answer.RegisterMember("rml", &AnswerVM::rml);
    }
    constructor.RegisterArray<std::vector<AnswerVM>>();

    constructor.Bind("mode", &m_iMode);
    constructor.Bind("role", &m_strRole);
    constructor.Bind("name", &m_strName);
    constructor.Bind("text", &m_strText);
    constructor.Bind("answers", &m_Answers);

    /// choose(index): the answer's own handler ( CEvent::Click_ITEM ). The
    /// handler is copied out first -- it usually closes this conversation and
    /// opens the next line, rewriting the handler list while it runs.
    constructor.BindEventCallback("choose",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int iIndex = args[0].Get<int>() - 1;
            if (iIndex < 0 || iIndex >= (int)m_Handlers.size())
                return;
            const Handler handler = m_Handlers[iIndex];
            if (handler.fpHandler)
                handler.fpHandler(handler.iEventID);
        });

    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { Close(); });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "conversation.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load conversation document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("conversation");
    RoseRmlLayout::Track(m_pPanel, "conversation");

    LOG_INFO("[rmlui] conversation document loaded");
    return true;
}

void
RoseRmlConversation::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
    m_Handlers.clear();
}

void
RoseRmlConversation::Begin(int iMode, const char* pszText, int iOwnerClientIdx) {
    m_iMode = iMode;
    m_iOwnerClientIdx = iOwnerClientIdx;
    /// Copied now: the engine formats every line into one static buffer.
    m_strText = MarkupToRml(pszText);
    m_Answers.clear();
    m_Handlers.clear();
    m_Model.DirtyVariable("mode");
    m_Model.DirtyVariable("text");
    m_Model.DirtyVariable("answers");
    if (iMode == MODE_NPC)
        SampleName();
    else if (!m_strName.empty() || !m_strRole.empty()) {
        m_strName.clear();
        m_strRole.clear();
        m_Model.DirtyVariable("name");
        m_Model.DirtyVariable("role");
    }
}

void
RoseRmlConversation::AddAnswer(const char* pszText, int iEventID, void (*fpHandler)(int iEventID)) {
    Handler handler;
    handler.iEventID = iEventID;
    handler.fpHandler = fpHandler;
    m_Handlers.push_back(handler);

    AnswerVM vm;
    vm.index = (int)m_Handlers.size();
    vm.rml = MarkupToRml(pszText);
    m_Answers.push_back(vm);
    m_Model.DirtyVariable("answers");
}

void
RoseRmlConversation::SetOpen(int iMode, bool bOpen) {
    if (iMode != m_iMode)
        return; /// Begin set the look before the engine opened it
    if (!bOpen) {
        Close();
        return;
    }
    m_bOpen = true;
    m_iZoneAtOpen = g_pTerrain ? g_pTerrain->GetZoneNO() : -1;
}

void
RoseRmlConversation::Close() {
    m_bOpen = false;
    /// The handlers point into the conversation's CEvent: never keep them past
    /// the conversation ( a zone load deletes every CEvent ).
    if (!m_Handlers.empty() || !m_Answers.empty()) {
        m_Handlers.clear();
        m_Answers.clear();
        m_Model.DirtyVariable("answers");
    }
}

void
RoseRmlConversation::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlConversation::SampleName() {
    /// The name CDialogDlg keeps: set from the target NPC, renamed by scripts
    /// ( QF_ChangetalkName ). "[Weapon Merchant] Raffle" splits in two.
    Rml::String strName, strRole;
    if (CDialogDlg* pDlg = (CDialogDlg*)g_itMGR.FindDlg(DLG_TYPE_DIALOG)) {
        strName = RoseRmlText::FromGame(pDlg->GetNpcName().c_str());
        const size_t iClose = strName.find(']');
        if (!strName.empty() && strName[0] == '[' && iClose != Rml::String::npos) {
            strRole = strName.substr(1, iClose - 1);
            strName = strName.substr(iClose + 1);
            while (!strName.empty() && strName[0] == ' ')
                strName.erase(0, 1);
        }
    }
    if (strName != m_strName || strRole != m_strRole) {
        m_strName = strName;
        m_strRole = strRole;
        m_Model.DirtyVariable("name");
        m_Model.DirtyVariable("role");
    }
}

void
RoseRmlConversation::PlaceDefault() {
    /// Bottom centre, above the skill bar -- only while no position is set
    /// ( dragged, saved or reset ).
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet
    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fRatio = RoseRmlLayout::GetScaleRatio();
    m_pPanel->SetProperty(Rml::PropertyId::Left,
        Rml::Property(floorf(((float)view.x - size.x) * 0.5f), Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top,
        Rml::Property(floorf((float)view.y - size.y - kBottomGap * fRatio), Rml::Unit::PX));
}

void
RoseRmlConversation::Update() {
    if (m_pDocument == NULL)
        return;

    const bool bInWorld = RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;

    if (m_bOpen) {
        if (!bInWorld)
            Close();
        else if (g_pTerrain && g_pTerrain->GetZoneNO() != m_iZoneAtOpen)
            Close(); /// the zone load deleted this conversation's CEvent
        else if (m_iOwnerClientIdx >= 0 && m_iMode != MODE_EVENT) {
            /// CDialogDlg / CSelectEventDlg::Update: the owner gone or too far.
            CGameOBJ* pObj = g_pObjMGR->Get_OBJECT((short)m_iOwnerClientIdx);
            if (pObj == NULL || g_pAVATAR->Get_DISTANCE(pObj->Get_CurPOS()) >= kCloseDistance)
                Close();
        }
    }

    /// Hero quests hide the talk for a while ( QF_NpcTalkinterfaceHide ); the
    /// classic dialog counted it down in its Draw, which no longer runs.
    bool bHeroHidden = false;
    if (m_bOpen && m_iMode == MODE_NPC) {
        if (CDialogDlg* pDlg = (CDialogDlg*)g_itMGR.FindDlg(DLG_TYPE_DIALOG)) {
            const float fHide = pDlg->GetNpctalkinterfaceHide();
            if (fHide > 0.0f) {
                pDlg->SetNpctalkinterfaceHide(fHide - (float)g_GameDATA.GetElapsedFrameTime());
                bHeroHidden = true;
            }
        }
    }

    SetVisible(m_bOpen && bInWorld && !bHeroHidden && g_pAVATAR->Get_HP() > 0);
    if (!m_bVisible)
        return;

    if (m_iMode == MODE_NPC)
        SampleName();
    PlaceDefault();

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
