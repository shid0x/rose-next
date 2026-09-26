#include "stdafx.h"

#include "RoseRmlMessages.h"
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
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/typeresource.h"
#include "../interface/Dlgs/CCommDlg.h"
#include "../interface/Dlgs/subclass/CFriendListItem.h"
#include "tgamectrl/teditbox.h"

#include "rose/common/log.h"

#include <math.h>

namespace {

/// A conversation keeps its last lines only.
const size_t kMaxLines = 200;

/// Line kinds ( LineVM::kind ).
const int LINE_THEIRS = 0;
const int LINE_MINE = 1;
const int LINE_NOTE = 2;

bool
IsOnline(BYTE btStatus) {
    return btStatus != FRIEND_STATUS_OFFLINE && btStatus != FRIEND_STATUS_REFUSED
        && btStatus != FRIEND_STATUS_DELETED;
}

Rml::String
StateText(BYTE btStatus) {
    switch (btStatus) {
        case FRIEND_STATUS_HUNT:
            return RoseRmlText::FromGame(STR_MYSTATE_IN_HUNT);
        case FRIEND_STATUS_STORE:
            return RoseRmlText::FromGame(STR_MYSTATE_IN_TRADE);
        case FRIEND_STATUS_QUEST:
            return RoseRmlText::FromGame(STR_MYSTATE_IN_QUEST);
        case FRIEND_STATUS_EAT:
            return RoseRmlText::FromGame(STR_MYSTATE_IN_EAT);
        case FRIEND_STATUS_REST:
            return RoseRmlText::FromGame(STR_MYSTATE_IN_BREAK);
        case FRIEND_STATUS_REFUSED:
            return "Blocked you";
        case FRIEND_STATUS_DELETED:
            return "Removed you";
        case FRIEND_STATUS_OFFLINE:
            return "Offline";
        default:
            return "Online";
    }
}

/// The friend's list item ( the hidden CCommDlg is the friends list ).
CFriendListItem*
FindFriend(DWORD dwUserTag) {
    CCommDlg* pDlg = (CCommDlg*)g_itMGR.FindDlg(DLG_TYPE_COMMUNITY);
    return pDlg ? pDlg->FindFriend(dwUserTag) : NULL;
}

} // namespace

RoseRmlMessages::RoseRmlMessages():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_bFocusField(false),
    m_iScrollFrames(0),
    m_iActive(-1),
    m_bHasConv(false),
    m_bOnline(false) {}

bool
RoseRmlMessages::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("messages");
    if (!constructor)
        return false;

    if (auto tab = constructor.RegisterStruct<TabVM>()) {
        tab.RegisterMember("index", &TabVM::index);
        tab.RegisterMember("used", &TabVM::used);
        tab.RegisterMember("on", &TabVM::on);
        tab.RegisterMember("unread", &TabVM::unread);
        tab.RegisterMember("online", &TabVM::online);
        tab.RegisterMember("name", &TabVM::name);
    }
    constructor.RegisterArray<std::vector<TabVM>>();
    if (auto line = constructor.RegisterStruct<LineVM>()) {
        line.RegisterMember("index", &LineVM::index);
        line.RegisterMember("used", &LineVM::used);
        line.RegisterMember("kind", &LineVM::kind);
        line.RegisterMember("who", &LineVM::who);
        line.RegisterMember("text", &LineVM::text);
    }
    constructor.RegisterArray<std::vector<LineVM>>();

    constructor.Bind("tabs", &m_Tabs);
    constructor.Bind("lines", &m_Lines);
    constructor.Bind("has_conv", &m_bHasConv);
    constructor.Bind("with", &m_strWith);
    constructor.Bind("state", &m_strState);
    constructor.Bind("online", &m_bOnline);

    constructor.BindEventCallback("set_tab",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                SelectTab(args[0].Get<int>());
        });
    constructor.BindEventCallback("close_tab",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
            ev.StopPropagation(); /// not also a click on the tab
            if (!args.empty())
                CloseTab(args[0].Get<int>());
        });
    constructor.BindEventCallback("send",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnSend(); });
    /// Enter in the field ( a "change" with linebreak ).
    constructor.BindEventCallback("edited",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<bool>("linebreak", false))
                OnSend();
        });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_PRIVATECHAT);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "messages.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load messages document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("messages");
    RoseRmlLayout::Track(m_pPanel, "messages");

    LOG_INFO("[rmlui] messages document loaded");
    return true;
}

void
RoseRmlMessages::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

bool
RoseRmlMessages::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlMessages::SetOpen(bool bOpen) {
    if (bOpen == m_bOpen)
        return;
    RoseUi2::PlayWindowSound(DLG_TYPE_PRIVATECHAT, bOpen);
    m_bOpen = bOpen;
    if (bOpen) {
        m_iScrollFrames = 2;
        Sample();
    }
}

/// --- conversations ------------------------------------------------------------

int
RoseRmlMessages::FindConversation(DWORD dwUserTag) const {
    for (size_t i = 0; i < m_Convs.size(); ++i) {
        if (m_Convs[i].dwTag == dwUserTag)
            return (int)i;
    }
    return -1;
}

int
RoseRmlMessages::AddConversation(DWORD dwUserTag, const char* pszName) {
    int i = FindConversation(dwUserTag);
    if (i >= 0)
        return i;
    Conversation conv;
    conv.dwTag = dwUserTag;
    conv.strName = pszName ? pszName : "";
    conv.bUnread = false;
    m_Convs.push_back(conv);
    return (int)m_Convs.size() - 1;
}

void
RoseRmlMessages::AddLine(int iConv, int iKind, const char* pszWho, const char* pszText) {
    if (iConv < 0 || iConv >= (int)m_Convs.size())
        return;
    Line line;
    line.iKind = iKind;
    line.strWho = pszWho ? pszWho : "";
    line.strText = pszText ? pszText : "";
    std::vector<Line>& lines = m_Convs[iConv].lines;
    lines.push_back(line);
    if (lines.size() > kMaxLines)
        lines.erase(lines.begin());
    if (iConv == m_iActive)
        m_iScrollFrames = 2;
}

void
RoseRmlMessages::SelectTab(int iConv) {
    if (iConv < 0 || iConv >= (int)m_Convs.size())
        return;
    m_iActive = iConv;
    m_Convs[iConv].bUnread = false;
    m_iScrollFrames = 2;
}

void
RoseRmlMessages::CloseTab(int iConv) {
    if (iConv < 0 || iConv >= (int)m_Convs.size())
        return;
    m_Convs.erase(m_Convs.begin() + iConv);
    if (m_Convs.empty()) {
        m_iActive = -1;
        g_itMGR.CloseDialog(DLG_TYPE_PRIVATECHAT);
        return;
    }
    if (m_iActive >= (int)m_Convs.size() || m_iActive == iConv)
        m_iActive = (iConv < (int)m_Convs.size()) ? iConv : (int)m_Convs.size() - 1;
    else if (m_iActive > iConv)
        --m_iActive;
    m_Convs[m_iActive].bUnread = false;
    m_iScrollFrames = 2;
}

void
RoseRmlMessages::OpenConversation(DWORD dwUserTag, const char* pszName) {
    const int iConv = AddConversation(dwUserTag, pszName);
    SelectTab(iConv);
    g_itMGR.OpenDialog(DLG_TYPE_PRIVATECHAT, false);
    m_bFocusField = true;
}

void
RoseRmlMessages::Receive(DWORD dwUserTag, const char* pszName, const char* pszMsg) {
    const bool bWasOpen = m_bOpen;
    const int iConv = AddConversation(dwUserTag, pszName);
    AddLine(iConv, LINE_THEIRS, pszName, pszMsg);

    /// Classic opened a window for it; here the window opens on it if it was
    /// closed, and otherwise marks its tab. Never the keyboard.
    if (!bWasOpen || m_iActive < 0) {
        SelectTab(iConv);
        g_itMGR.OpenDialog(DLG_TYPE_PRIVATECHAT, false);
    } else if (iConv != m_iActive) {
        m_Convs[iConv].bUnread = true;
    }
}

void
RoseRmlMessages::OnSend() {
    if (m_iActive < 0 || m_iActive >= (int)m_Convs.size() || m_pDocument == NULL || g_pNet == NULL
        || g_pAVATAR == NULL)
        return;
    Rml::Element* pField = m_pDocument->GetElementById("say");
    if (pField == NULL)
        return;
    const Rml::String strMsg = pField->GetAttribute<Rml::String>("value", "");
    if (strMsg.empty())
        return;

    Conversation& conv = m_Convs[m_iActive];
    /// CPrivateChatDlg::SendChatMsg: the friend as the list has them now.
    CFriendListItem* pItem = FindFriend(conv.dwTag);
    if (pItem == NULL) {
        AddLine(m_iActive, LINE_NOTE, "", CStr::Printf("%s is no longer on your friends list: not sent.",
                                                  conv.strName.c_str()));
        return;
    }
    switch (pItem->GetStatus()) {
        case FRIEND_STATUS_REFUSED:
            AddLine(m_iActive, LINE_NOTE, "", CStr::Printf("%s blocks your messages: not sent.",
                                                      conv.strName.c_str()));
            return;
        case FRIEND_STATUS_DELETED:
            AddLine(m_iActive, LINE_NOTE, "", CStr::Printf("%s removed you from their friends: not sent.",
                                                      conv.strName.c_str()));
            return;
        case FRIEND_STATUS_OFFLINE:
            AddLine(m_iActive, LINE_NOTE, "", CStr::Printf("%s is offline: not sent.",
                                                      conv.strName.c_str()));
            return;
        default:
            break;
    }

    g_pNet->Send_cli_MESSENGER_CHAT(conv.dwTag, (char*)strMsg.c_str());
    AddLine(m_iActive, LINE_MINE, g_pAVATAR->Get_NAME(), strMsg.c_str());
    pField->SetAttribute("value", Rml::String());
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlMessages::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

void
RoseRmlMessages::Sample() {
    /// The tabs ( grow-only rows ), each with its friend's light.
    std::vector<TabVM> tabs = m_Tabs;
    if (tabs.size() < m_Convs.size())
        tabs.resize(m_Convs.size());
    for (size_t i = 0; i < tabs.size(); ++i) {
        TabVM& vm = tabs[i];
        vm.index = (int)i;
        vm.used = i < m_Convs.size();
        if (!vm.used) {
            vm.on = vm.unread = vm.online = false;
            vm.name.clear();
            continue;
        }
        CFriendListItem* pItem = FindFriend(m_Convs[i].dwTag);
        vm.on = (int)i == m_iActive;
        vm.unread = m_Convs[i].bUnread;
        vm.online = pItem != NULL && IsOnline(pItem->GetStatus());
        vm.name = RoseRmlText::FromGame(m_Convs[i].strName.c_str());
    }
    if (tabs != m_Tabs) {
        m_Tabs.swap(tabs);
        m_Model.DirtyVariable("tabs");
    }

    const bool bHasConv = m_iActive >= 0 && m_iActive < (int)m_Convs.size();
    if (bHasConv != m_bHasConv) {
        m_bHasConv = bHasConv;
        m_Model.DirtyVariable("has_conv");
    }

    /// The active conversation: who, their state now, and the lines
    /// ( grow-only rows ).
    Rml::String strWith, strState;
    bool bOnline = false;
    const std::vector<Line>* pLines = NULL;
    if (bHasConv) {
        const Conversation& conv = m_Convs[m_iActive];
        strWith = RoseRmlText::FromGame(conv.strName.c_str());
        CFriendListItem* pItem = FindFriend(conv.dwTag);
        if (pItem != NULL) {
            strState = StateText(pItem->GetStatus());
            bOnline = IsOnline(pItem->GetStatus());
        } else {
            strState = "Not a friend";
        }
        pLines = &conv.lines;
    }
    if (strWith != m_strWith) {
        m_strWith = strWith;
        m_Model.DirtyVariable("with");
    }
    if (strState != m_strState) {
        m_strState = strState;
        m_Model.DirtyVariable("state");
    }
    if (bOnline != m_bOnline) {
        m_bOnline = bOnline;
        m_Model.DirtyVariable("online");
    }

    const size_t nLines = pLines ? pLines->size() : 0;
    std::vector<LineVM> lines = m_Lines;
    if (lines.size() < nLines)
        lines.resize(nLines);
    for (size_t i = 0; i < lines.size(); ++i) {
        LineVM& vm = lines[i];
        vm.index = (int)i;
        vm.used = i < nLines;
        if (!vm.used) {
            vm.kind = LINE_THEIRS;
            vm.who.clear();
            vm.text.clear();
            continue;
        }
        const Line& line = (*pLines)[i];
        vm.kind = line.iKind;
        vm.who = RoseRmlText::FromGame(line.strWho.c_str());
        vm.text = RoseRmlText::FromGame(line.strText.c_str());
    }
    if (lines != m_Lines) {
        m_Lines.swap(lines);
        m_Model.DirtyVariable("lines");
    }
}

void
RoseRmlMessages::PlaceDefault() {
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
RoseRmlMessages::Update() {
    if (m_pDocument == NULL)
        return;

    /// Out of the world ( logout, character select ): the conversations go.
    if (!IsInWorld()) {
        m_bOpen = false;
        if (!m_Convs.empty()) {
            m_Convs.clear();
            m_iActive = -1;
        }
    }

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    Sample();
    PlaceDefault();

    if (m_bFocusField) {
        m_bFocusField = false;
        if (Rml::Element* pField = m_pDocument->GetElementById("say")) {
            pField->Focus();
            if (CTEditBox::s_pFocusEdit != NULL)
                CTEditBox::s_pFocusEdit->SetFocus(false);
        }
    }

    /// The newest line in view, once the new rows are laid out.
    if (m_iScrollFrames > 0 && --m_iScrollFrames == 0) {
        if (Rml::Element* pLog = m_pDocument->GetElementById("log"))
            pLog->SetScrollTop(pLog->GetScrollHeight());
    }

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
