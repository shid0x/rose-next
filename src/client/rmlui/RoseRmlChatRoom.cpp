#include "stdafx.h"

#include "RoseRmlChatRoom.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../CObjUSER.h"
#include "../Game.h"
#include "../Network/CNetwork.h"
#include "../System/CGame.h"
#include "../gamedata/CChatRoom.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/Dlgs/CChatRoomDlg.h"
#include "tgamectrl/teditbox.h"

#include "rose/common/log.h"

#include <math.h>

namespace {

const size_t kMaxLines = 200;
const int LINE_OTHERS = 0;
const int LINE_MINE = 1;
const int LINE_NOTE = 2;

} // namespace

RoseRmlChatRoom::RoseRmlChatRoom():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_bFocusField(false),
    m_iScrollFrames(0),
    m_bMembersKnown(false),
    m_iMemberCount(0) {}

bool
RoseRmlChatRoom::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("chatroom");
    if (!constructor)
        return false;

    if (auto member = constructor.RegisterStruct<MemberVM>()) {
        member.RegisterMember("index", &MemberVM::index);
        member.RegisterMember("used", &MemberVM::used);
        member.RegisterMember("master", &MemberVM::master);
        member.RegisterMember("name", &MemberVM::name);
    }
    constructor.RegisterArray<std::vector<MemberVM>>();
    if (auto line = constructor.RegisterStruct<LineVM>()) {
        line.RegisterMember("index", &LineVM::index);
        line.RegisterMember("used", &LineVM::used);
        line.RegisterMember("kind", &LineVM::kind);
        line.RegisterMember("who", &LineVM::who);
        line.RegisterMember("text", &LineVM::text);
    }
    constructor.RegisterArray<std::vector<LineVM>>();

    constructor.Bind("title", &m_strTitle);
    constructor.Bind("members", &m_MemberVMs);
    constructor.Bind("lines", &m_LineVMs);
    constructor.Bind("member_count", &m_iMemberCount);

    constructor.BindEventCallback("send",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnSend(); });
    constructor.BindEventCallback("edited",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<bool>("linebreak", false))
                OnSend();
        });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_CHATROOM);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "chatroom.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load chat room document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("chatroom");
    RoseRmlLayout::Track(m_pPanel, "chatroom");

    LOG_INFO("[rmlui] chat room document loaded");
    return true;
}

void
RoseRmlChatRoom::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CChatRoomDlg*
RoseRmlChatRoom::Dlg() const {
    return (CChatRoomDlg*)g_itMGR.FindDlg(DLG_TYPE_CHATROOM);
}

bool
RoseRmlChatRoom::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlChatRoom::SetOpen(bool bOpen) {
    if (bOpen == m_bOpen)
        return;
    RoseUi2::PlayWindowSound(DLG_TYPE_CHATROOM, bOpen);
    /// Closed first: leaving notifies the dialog, which closes this again.
    m_bOpen = bOpen;
    if (bOpen) {
        m_Lines.clear();
        m_KnownMembers.clear();
        m_bMembersKnown = false;
        m_bFocusField = true;
        m_iScrollFrames = 2;
        return;
    }
    /// CChatRoomDlg::Hide: closing the window leaves the room ( not when the
    /// room is already gone: kicked, or a failed join ).
    if (CChatRoom::GetInstance().GetState() != CChatRoom::STATE_DEACTIVATED)
        CChatRoom::GetInstance().Leave();
}

/// --- lines ----------------------------------------------------------------------

void
RoseRmlChatRoom::AddLine(int iKind, const char* pszWho, const char* pszText) {
    Line line;
    line.iKind = iKind;
    line.strWho = pszWho ? pszWho : "";
    line.strText = pszText ? pszText : "";
    m_Lines.push_back(line);
    if (m_Lines.size() > kMaxLines)
        m_Lines.erase(m_Lines.begin());
    m_iScrollFrames = 2;
}

void
RoseRmlChatRoom::Receive(WORD wUserId, const char* pszMsg) {
    CChatRoomDlg* pDlg = Dlg();
    const char* pszWho = pDlg ? pDlg->GetMemberName(wUserId) : NULL;
    /// CChatRoomDlg::RecvChatMsg: a sender not in the room is dropped.
    if (pszWho == NULL)
        return;
    const bool bMine = g_pAVATAR != NULL && _stricmp(pszWho, g_pAVATAR->Get_NAME()) == 0;
    AddLine(bMine ? LINE_MINE : LINE_OTHERS, pszWho, pszMsg);
}

/// "X joined" / "X left", from the members the dialog keeps.
void
RoseRmlChatRoom::TrackMembers() {
    CChatRoomDlg* pDlg = Dlg();
    if (pDlg == NULL)
        return;
    std::set<std::string> now;
    const std::list<CChatMember>& Members = pDlg->GetMembers();
    for (std::list<CChatMember>::const_iterator it = Members.begin(); it != Members.end(); ++it) {
        CChatMember Member = *it;
        now.insert(Member.GetName() ? Member.GetName() : "");
    }
    if (m_bMembersKnown) {
        for (std::set<std::string>::iterator it = now.begin(); it != now.end(); ++it) {
            if (m_KnownMembers.find(*it) == m_KnownMembers.end())
                AddLine(LINE_NOTE, "", CStr::Printf("%s joined.", it->c_str()));
        }
        for (std::set<std::string>::iterator it = m_KnownMembers.begin(); it != m_KnownMembers.end(); ++it) {
            if (now.find(*it) == now.end())
                AddLine(LINE_NOTE, "", CStr::Printf("%s left.", it->c_str()));
        }
    }
    /// The first full list ( right after joining ) is the room as found.
    if (!now.empty())
        m_bMembersKnown = true;
    m_KnownMembers.swap(now);
}

void
RoseRmlChatRoom::OnSend() {
    if (m_pDocument == NULL || g_pNet == NULL)
        return;
    Rml::Element* pField = m_pDocument->GetElementById("roomsay");
    if (pField == NULL)
        return;
    const Rml::String strMsg = pField->GetAttribute<Rml::String>("value", "");
    if (strMsg.empty())
        return;
    /// CChatRoomDlg::SendChatMsg: only inside a room; the server echoes it.
    if (CChatRoom::GetInstance().GetState() == CChatRoom::STATE_ACTIVATED)
        g_pNet->Send_cli_CHATROOM_MSG((char*)strMsg.c_str());
    pField->SetAttribute("value", Rml::String());
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlChatRoom::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlChatRoom::Sample() {
    const Rml::String strTitle = RoseRmlText::FromGame(CChatRoom::GetInstance().GetTitle());
    if (strTitle != m_strTitle) {
        m_strTitle = strTitle;
        m_Model.DirtyVariable("title");
    }

    /// The members, the master first ( grow-only rows ).
    std::vector<MemberVM> members = m_MemberVMs;
    int iCount = 0;
    if (CChatRoomDlg* pDlg = Dlg()) {
        const std::list<CChatMember>& Members = pDlg->GetMembers();
        iCount = (int)Members.size();
        if ((int)members.size() < iCount)
            members.resize(iCount);
        int i = 0;
        for (std::list<CChatMember>::const_iterator it = Members.begin(); it != Members.end(); ++it, ++i) {
            CChatMember Member = *it;
            members[i].index = i;
            members[i].used = true;
            members[i].master = i == 0;
            members[i].name = RoseRmlText::FromGame(Member.GetName());
        }
    }
    for (size_t i = (size_t)iCount; i < members.size(); ++i) {
        members[i].index = (int)i;
        members[i].used = false;
        members[i].master = false;
        members[i].name.clear();
    }
    if (members != m_MemberVMs) {
        m_MemberVMs.swap(members);
        m_Model.DirtyVariable("members");
    }
    if (iCount != m_iMemberCount) {
        m_iMemberCount = iCount;
        m_Model.DirtyVariable("member_count");
    }

    std::vector<LineVM> lines = m_LineVMs;
    if (lines.size() < m_Lines.size())
        lines.resize(m_Lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        LineVM& vm = lines[i];
        vm.index = (int)i;
        vm.used = i < m_Lines.size();
        if (!vm.used) {
            vm.kind = LINE_OTHERS;
            vm.who.clear();
            vm.text.clear();
            continue;
        }
        vm.kind = m_Lines[i].iKind;
        vm.who = RoseRmlText::FromGame(m_Lines[i].strWho.c_str());
        vm.text = RoseRmlText::FromGame(m_Lines[i].strText.c_str());
    }
    if (lines != m_LineVMs) {
        m_LineVMs.swap(lines);
        m_Model.DirtyVariable("lines");
    }
}

void
RoseRmlChatRoom::PlaceDefault() {
    /// Left of the middle, a little high -- only while no position is set.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fLeft = floorf((float)view.x * 0.62f - size.x - 8.0f);
    const float fTop = floorf(((float)view.y - size.y) * 0.35f);
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft < 0.0f ? 0.0f : fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

void
RoseRmlChatRoom::Update() {
    if (m_pDocument == NULL)
        return;

    /// Out of the world: the room goes ( as the classic dialog's close did ).
    if (m_bOpen && !IsInWorld())
        SetOpen(false);

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    TrackMembers();
    Sample();
    PlaceDefault();

    if (m_bFocusField) {
        m_bFocusField = false;
        if (Rml::Element* pField = m_pDocument->GetElementById("roomsay")) {
            pField->Focus();
            if (CTEditBox::s_pFocusEdit != NULL)
                CTEditBox::s_pFocusEdit->SetFocus(false);
        }
    }

    if (m_iScrollFrames > 0 && --m_iScrollFrames == 0) {
        if (Rml::Element* pLog = m_pDocument->GetElementById("roomlog"))
            pLog->SetScrollTop(pLog->GetScrollHeight());
    }

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
