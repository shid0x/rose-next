#include "stdafx.h"

#include "RoseRmlCommunity.h"
#include "RoseRmlLayout.h"
#include "RoseRmlText.h"
#include "RoseRmlUi.h"
#include "RoseUi2.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>

#include "../CObjUSER.h"
#include "../Game.h"
#include "../System/CGame.h"
#include "../interface/it_mgr.h"
#include "../interface/interfacetype.h"
#include "../interface/typeresource.h"
#include "../interface/command/uicommand.h"
#include "../interface/Dlgs/CCommDlg.h"
#include "../interface/Dlgs/CAddFriendDlg.h"
#include "../interface/Dlgs/subclass/CFriendListItem.h"
#include "../interface/Dlgs/subclass/CChatRoomListItem.h"
#include "../gamedata/CChatRoom.h"
#include "../Network/CNetwork.h"
#include "tgamectrl/teditbox.h"

#include "rose/common/log.h"

#include <algorithm>
#include <math.h>

namespace {

/// A friend who can be talked to ( the classic list's "OnLine" group ).
bool
IsOnline(BYTE btStatus) {
    return btStatus != FRIEND_STATUS_OFFLINE && btStatus != FRIEND_STATUS_REFUSED
        && btStatus != FRIEND_STATUS_DELETED;
}

/// CFriendListItem::Draw's state words.
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

struct FriendRow {
    DWORD dwTag;
    BYTE btStatus;
    std::string strName;
};

/// Online first, then by name.
bool
FriendOrder(const FriendRow& a, const FriendRow& b) {
    const bool bA = IsOnline(a.btStatus), bB = IsOnline(b.btStatus);
    if (bA != bB)
        return bA;
    return _stricmp(a.strName.c_str(), b.strName.c_str()) < 0;
}

} // namespace

RoseRmlCommunity::RoseRmlCommunity():
    m_pContext(NULL),
    m_pDocument(NULL),
    m_pPanel(NULL),
    m_bOpen(false),
    m_bVisible(false),
    m_bFocusAdd(false),
    m_dwSelected(0),
    m_iOnline(0),
    m_iTotal(0),
    m_bHasSelection(false),
    m_bHasMessages(false),
    m_iTab(0),
    m_iRoomCount(0),
    m_bInRoom(false) {}

bool
RoseRmlCommunity::Initialise(Rml::Context* pContext, const std::string& strAssetDir) {
    if (pContext == NULL)
        return false;

    m_pContext = pContext;

    Rml::DataModelConstructor constructor = pContext->CreateDataModel("community");
    if (!constructor)
        return false;

    if (auto row = constructor.RegisterStruct<FriendVM>()) {
        row.RegisterMember("index", &FriendVM::index);
        row.RegisterMember("used", &FriendVM::used);
        row.RegisterMember("on", &FriendVM::on);
        row.RegisterMember("online", &FriendVM::online);
        row.RegisterMember("name", &FriendVM::name);
        row.RegisterMember("state", &FriendVM::state);
    }
    constructor.RegisterArray<std::vector<FriendVM>>();
    if (auto room = constructor.RegisterStruct<RoomVM>()) {
        room.RegisterMember("index", &RoomVM::index);
        room.RegisterMember("used", &RoomVM::used);
        room.RegisterMember("title", &RoomVM::title);
        room.RegisterMember("users", &RoomVM::users);
    }
    constructor.RegisterArray<std::vector<RoomVM>>();
    constructor.Bind("tab", &m_iTab);
    constructor.Bind("rooms", &m_Rooms);
    constructor.Bind("room_count", &m_iRoomCount);
    constructor.Bind("in_room", &m_bInRoom);
    constructor.Bind("room_title", &m_strRoomTitle);
    constructor.BindEventCallback("set_tab",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                SetTab(args[0].Get<int>());
        });
    constructor.BindEventCallback("refresh",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { RefreshRooms(); });
    constructor.BindEventCallback("join",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                OnJoin(args[0].Get<int>());
        });
    constructor.BindEventCallback("make",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnMake(); });
    constructor.BindEventCallback("make_edited",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<bool>("linebreak", false))
                OnMake();
        });
    constructor.BindEventCallback("open_room",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.OpenDialog(DLG_TYPE_CHATROOM, false);
        });

    constructor.Bind("friends", &m_Friends);
    constructor.Bind("online", &m_iOnline);
    constructor.Bind("total", &m_iTotal);
    constructor.Bind("has_selection", &m_bHasSelection);
    constructor.Bind("has_messages", &m_bHasMessages);

    constructor.BindEventCallback("select",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (args.empty())
                return;
            const int i = args[0].Get<int>();
            if (i >= 0 && i < (int)m_RowTags.size())
                m_dwSelected = m_RowTags[i];
        });
    constructor.BindEventCallback("talk",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            if (!args.empty())
                OnTalk(args[0].Get<int>());
        });
    constructor.BindEventCallback("add",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnAdd(); });
    /// Enter in the name field ( a "change" with linebreak ).
    constructor.BindEventCallback("add_edited",
        [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (ev.GetParameter<bool>("linebreak", false))
                OnAdd();
        });
    constructor.BindEventCallback("messages",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.OpenDialog(DLG_TYPE_PRIVATECHAT, false);
        });
    constructor.BindEventCallback("remove",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { OnRemove(); });
    constructor.BindEventCallback("close",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            g_itMGR.CloseDialog(DLG_TYPE_COMMUNITY);
        });

    m_Model = constructor.GetModelHandle();

    const std::string strDoc = strAssetDir + "community.rml";
    m_pDocument = pContext->LoadDocument(strDoc.c_str());
    if (m_pDocument == NULL) {
        LOG_WARN("[rmlui] could not load community document '{}'", strDoc.c_str());
        return false;
    }

    m_pPanel = m_pDocument->GetElementById("community");
    RoseRmlLayout::Track(m_pPanel, "community");

    LOG_INFO("[rmlui] community document loaded");
    return true;
}

void
RoseRmlCommunity::Shutdown() {
    /// The context owns the document; it is torn down with Rml::Shutdown().
    m_pDocument = NULL;
    m_pContext = NULL;
    m_pPanel = NULL;
    m_bVisible = false;
    m_bOpen = false;
}

CCommDlg*
RoseRmlCommunity::Dlg() const {
    return (CCommDlg*)g_itMGR.FindDlg(DLG_TYPE_COMMUNITY);
}

bool
RoseRmlCommunity::IsInWorld() const {
    return RoseUi2::IsActive() && g_pAVATAR != NULL
        && CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN;
}

void
RoseRmlCommunity::SetOpen(bool bOpen) {
    if (bOpen == m_bOpen)
        return;
    RoseUi2::PlayWindowSound(DLG_TYPE_COMMUNITY, bOpen);
    m_bOpen = bOpen;
    if (bOpen) {
        if (m_iTab == 1)
            RefreshRooms();
        Sample();
    }
}

void
RoseRmlCommunity::SetTab(int iTab) {
    if (iTab != 0 && iTab != 1)
        return;
    if (iTab != m_iTab) {
        m_iTab = iTab;
        m_Model.DirtyVariable("tab");
    }
    if (iTab == 1)
        RefreshRooms();
}

/// Ask the server for the open rooms ( into the hidden CCommDlg's list ).
void
RoseRmlCommunity::RefreshRooms() {
    CCommDlg* pDlg = Dlg();
    if (pDlg == NULL || g_pNet == NULL)
        return;
    pDlg->ClearChatRoomList();
    g_pNet->Send_cli_CHAT_ROOM_LIST(0, 0);
}

void
RoseRmlCommunity::OnJoin(int iIndex) {
    CCommDlg* pDlg = Dlg();
    CChatRoomListItem* pRoom = pDlg ? pDlg->GetChatRoomAt(iIndex) : NULL;
    if (pRoom == NULL)
        return;
    /// One room at a time ( CChatRoom only asks while outside one ).
    if (CChatRoom::GetInstance().GetState() != CChatRoom::STATE_DEACTIVATED) {
        g_itMGR.OpenMsgBox("Leave your chat room first.");
        return;
    }
    CChatRoom::GetInstance().SendReqJoinRoom(pRoom->GetRoomType(), pRoom->GetRoomID(), NULL);
}

void
RoseRmlCommunity::OnMake() {
    if (m_pDocument == NULL)
        return;
    Rml::Element* pTitle = m_pDocument->GetElementById("roomtitle");
    Rml::Element* pMax = m_pDocument->GetElementById("roommax");
    if (pTitle == NULL)
        return;
    Rml::String strTitle = pTitle->GetAttribute<Rml::String>("value", "");
    while (!strTitle.empty() && strTitle[0] == ' ')
        strTitle.erase(0, 1);
    if (strTitle.empty())
        return;
    if (CChatRoom::GetInstance().GetState() != CChatRoom::STATE_DEACTIVATED) {
        g_itMGR.OpenMsgBox("Leave your chat room first.");
        return;
    }
    /// CChatRoomDlg's form: 2-8 people ( 8 when left blank ), public.
    int iMax = pMax ? atoi(pMax->GetAttribute<Rml::String>("value", "8").c_str()) : 8;
    if (iMax < 2)
        iMax = (iMax <= 0) ? 8 : 2;
    if (iMax > 8)
        iMax = 8;
    CChatRoom::GetInstance().SendReqMakeRoom(0, (BYTE)iMax, (char*)strTitle.c_str(), NULL);
    pTitle->SetAttribute("value", Rml::String());
}

void
RoseRmlCommunity::FocusAddFriend() {
    SetOpen(true);
    m_bFocusAdd = true;
}

void
RoseRmlCommunity::SetVisible(bool bVisible) {
    if (m_pDocument == NULL || bVisible == m_bVisible)
        return;

    m_bVisible = bVisible;
    if (bVisible)
        m_pDocument->Show();
    else
        m_pDocument->Hide();
}

/// --- sampling -------------------------------------------------------------------

void
RoseRmlCommunity::Sample() {
    CCommDlg* pDlg = Dlg();
    if (pDlg == NULL)
        return;

    std::vector<FriendRow> rows;
    const int iCount = pDlg->GetFriendCount();
    rows.reserve(iCount);
    for (int i = 0; i < iCount; ++i) {
        CFriendListItem* pItem = pDlg->GetFriendAt(i);
        if (pItem == NULL)
            continue;
        FriendRow row;
        row.dwTag = pItem->GetUserTag();
        row.btStatus = pItem->GetStatus();
        row.strName = pItem->GetName() ? pItem->GetName() : "";
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), FriendOrder);

    /// A removed friend cannot stay selected.
    bool bSelectedFound = false;
    int iOnline = 0;
    m_RowTags.clear();
    std::vector<FriendVM> friends = m_Friends;
    if (friends.size() < rows.size())
        friends.resize(rows.size());
    for (size_t i = 0; i < friends.size(); ++i) {
        FriendVM& vm = friends[i];
        vm.index = (int)i;
        vm.used = i < rows.size();
        if (!vm.used) {
            vm.on = vm.online = false;
            vm.name.clear();
            vm.state.clear();
            continue;
        }
        const FriendRow& row = rows[i];
        m_RowTags.push_back(row.dwTag);
        vm.online = IsOnline(row.btStatus);
        vm.on = row.dwTag == m_dwSelected;
        vm.name = RoseRmlText::FromGame(row.strName.c_str());
        vm.state = StateText(row.btStatus);
        if (vm.online)
            ++iOnline;
        if (vm.on)
            bSelectedFound = true;
    }
    if (!bSelectedFound)
        m_dwSelected = 0;
    if (friends != m_Friends) {
        m_Friends.swap(friends);
        m_Model.DirtyVariable("friends");
    }
    if (iOnline != m_iOnline) {
        m_iOnline = iOnline;
        m_Model.DirtyVariable("online");
    }
    if ((int)rows.size() != m_iTotal) {
        m_iTotal = (int)rows.size();
        m_Model.DirtyVariable("total");
    }
    /// The open rooms ( grow-only rows ), and the one you are in.
    const int iRooms = pDlg->GetChatRoomCount();
    std::vector<RoomVM> rooms = m_Rooms;
    if ((int)rooms.size() < iRooms)
        rooms.resize(iRooms);
    for (size_t i = 0; i < rooms.size(); ++i) {
        RoomVM& vm = rooms[i];
        vm.index = (int)i;
        CChatRoomListItem* pRoom = ((int)i < iRooms) ? pDlg->GetChatRoomAt((int)i) : NULL;
        vm.used = pRoom != NULL;
        vm.title = pRoom ? RoseRmlText::FromGame(pRoom->GetTitle()) : Rml::String();
        vm.users = pRoom ? Rml::String(CStr::Printf("%d in", (int)pRoom->GetUserCount())) : Rml::String();
    }
    if (rooms != m_Rooms) {
        m_Rooms.swap(rooms);
        m_Model.DirtyVariable("rooms");
    }
    if (iRooms != m_iRoomCount) {
        m_iRoomCount = iRooms;
        m_Model.DirtyVariable("room_count");
    }
    const bool bInRoom = CChatRoom::GetInstance().GetState() == CChatRoom::STATE_ACTIVATED;
    if (bInRoom != m_bInRoom) {
        m_bInRoom = bInRoom;
        m_Model.DirtyVariable("in_room");
    }
    const Rml::String strRoomTitle =
        bInRoom ? RoseRmlText::FromGame(CChatRoom::GetInstance().GetTitle()) : Rml::String();
    if (strRoomTitle != m_strRoomTitle) {
        m_strRoomTitle = strRoomTitle;
        m_Model.DirtyVariable("room_title");
    }

    const bool bMessages = RoseRmlUi::HasMessages();
    if (bMessages != m_bHasMessages) {
        m_bHasMessages = bMessages;
        m_Model.DirtyVariable("has_messages");
    }

    const bool bSel = m_dwSelected != 0;
    if (bSel != m_bHasSelection) {
        m_bHasSelection = bSel;
        m_Model.DirtyVariable("has_selection");
    }
}

/// --- actions ----------------------------------------------------------------------

void
RoseRmlCommunity::OnAdd() {
    Rml::Element* pField = m_pDocument ? m_pDocument->GetElementById("addname") : NULL;
    if (pField == NULL)
        return;
    const Rml::String strName = pField->GetAttribute<Rml::String>("value", "");
    /// CAddFriendDlg's rules: an empty name or your own does nothing.
    if (CAddFriendDlg::RequestAddFriend(strName.c_str()))
        pField->SetAttribute("value", Rml::String());
}

void
RoseRmlCommunity::OnRemove() {
    CCommDlg* pDlg = Dlg();
    CFriendListItem* pItem = (pDlg && m_dwSelected) ? pDlg->FindFriend(m_dwSelected) : NULL;
    if (pItem == NULL)
        return;
    /// CCommDlg's Remove: the classic question ( routed to UI2 ), whose OK
    /// deletes on the server and from the list.
    g_itMGR.OpenMsgBox(CStr::Printf(F_STR_QUERY_DELETE_FRIEND, pItem->GetName()),
        CMsgBox::BT_OK | CMsgBox::BT_CANCEL,
        true,
        0,
        new CTCmdRemoveFriend(m_dwSelected),
        NULL);
}

void
RoseRmlCommunity::OnTalk(int iIndex) {
    if (iIndex < 0 || iIndex >= (int)m_RowTags.size())
        return;
    CCommDlg* pDlg = Dlg();
    CFriendListItem* pItem = pDlg ? pDlg->FindFriend(m_RowTags[iIndex]) : NULL;
    if (pItem == NULL)
        return;

    /// CFriendListItem's double-click, without the mail window.
    switch (pItem->GetStatus()) {
        case FRIEND_STATUS_DELETED:
            g_itMGR.OpenMsgBox(CStr::Printf(F_STR_FRIEND_DELETED, pItem->GetName()));
            break;
        case FRIEND_STATUS_REFUSED:
            g_itMGR.OpenMsgBox(CStr::Printf(F_STR_MESSANGER_BLOCKED, pItem->GetName()));
            break;
        case FRIEND_STATUS_OFFLINE:
            g_itMGR.OpenMsgBox(CStr::Printf("%s is offline.", pItem->GetName()));
            break;
        default:
            g_itMGR.OpenPrivateChatDlg(pItem->GetUserTag(), pItem->GetStatus(), pItem->GetName());
            break;
    }
}

void
RoseRmlCommunity::PlaceDefault() {
    /// Right of the middle, a little high -- only while no position is set.
    if (m_pPanel == NULL || m_pPanel->GetLocalProperty(Rml::PropertyId::Left) != NULL)
        return;
    const Rml::Vector2f size = m_pPanel->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f)
        return; /// not laid out yet

    const Rml::Vector2i view = m_pContext->GetDimensions();
    const float fLeft = floorf((float)view.x * 0.62f);
    const float fTop = floorf(((float)view.y - size.y) * 0.35f);
    m_pPanel->SetProperty(Rml::PropertyId::Left, Rml::Property(fLeft, Rml::Unit::PX));
    m_pPanel->SetProperty(Rml::PropertyId::Top, Rml::Property(fTop, Rml::Unit::PX));
}

void
RoseRmlCommunity::Update() {
    if (m_pDocument == NULL)
        return;

    if (m_bOpen && !IsInWorld())
        SetOpen(false);

    SetVisible(m_bOpen);
    if (!m_bVisible)
        return;

    Sample();
    PlaceDefault();

    /// The add-friend box's job: the caret in the name field.
    if (m_bFocusAdd) {
        m_bFocusAdd = false;
        if (Rml::Element* pField = m_pDocument->GetElementById("addname")) {
            pField->Focus();
            if (CTEditBox::s_pFocusEdit != NULL)
                CTEditBox::s_pFocusEdit->SetFocus(false);
        }
    }

    const Rml::Vector2i view = m_pContext->GetDimensions();
    RoseRmlLayout::Clamp(m_pPanel, view.x, view.y);
}
