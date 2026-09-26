#ifndef _ROSE_RML_COMMUNITY_H_
#define _ROSE_RML_COMMUNITY_H_

/**
 * UI2 community window: replaces CCommDlg ( DLG_TYPE_COMMUNITY ) and the
 * add-friend box CAddFriendDlg ( DLG_TYPE_ADDFRIEND ).
 *
 * Rooms tab: the open chat rooms, which the classic client never asked the
 * server for ( Send_cli_CHAT_ROOM_LIST had no caller: the tab was always
 * empty ) -- requested when the tab opens and on Refresh, and paged in by
 * Recv_wsv_CHATROOM ( 15 a packet ) into the hidden CCommDlg's room list,
 * read from there. Double-click joins; the form makes a room ( public, 2-8
 * people -- classic forced every room public too ). The room itself is
 * RoseRmlChatRoom.
 *
 * Friends: the list ( online first, then by name ) with each friend's state,
 * a name field to add one ( Enter or Add ), Remove with the classic
 * confirmation. Double-click an online friend to talk ( the private chat );
 * an offline one says so -- the classic dialog opened the mail window, which
 * UI2 does not carry.
 *
 * **There is no friends-list model**: the list lives only as the list-box
 * items of the hidden CCommDlg, which every messenger packet updates
 * directly ( Recv_tag_MCMD_HEADER ) whether or not it is on screen. This
 * window reads them ( CCommDlg::GetFriendAt ) each frame. Adding goes through
 * CAddFriendDlg::RequestAddFriend, shared with the classic box; opening the
 * add-friend box under UI2 puts the caret in this window's field instead.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>
#include <string>
#include <vector>

class CCommDlg;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlCommunity {
public:
    RoseRmlCommunity();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog for DLG_TYPE_COMMUNITY.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    /// DLG_TYPE_ADDFRIEND: open, with the caret in the name field.
    void FocusAddFriend();

    void Update();

    struct RoomVM {
        int index;
        bool used;
        Rml::String title;
        Rml::String users; ///< how many are in it

        bool operator==(const RoomVM& o) const {
            return index == o.index && used == o.used && title == o.title && users == o.users;
        }
        bool operator!=(const RoomVM& o) const { return !(*this == o); }
    };

    struct FriendVM {
        int index;
        bool used; ///< rows are never removed, only hidden
        bool on; ///< selected
        bool online;
        Rml::String name;
        Rml::String state; ///< Online, Offline, Hunting ...

        bool operator==(const FriendVM& o) const {
            return index == o.index && used == o.used && on == o.on && online == o.online
                && name == o.name && state == o.state;
        }
        bool operator!=(const FriendVM& o) const { return !(*this == o); }
    };

private:
    CCommDlg* Dlg() const;
    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void Sample();
    void PlaceDefault();
    void OnAdd();
    void OnRemove();
    void OnTalk(int iIndex);
    void SetTab(int iTab);
    void RefreshRooms();
    void OnJoin(int iIndex);
    void OnMake();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    bool m_bFocusAdd; ///< put the caret in the name field once shown

    /// The rows' friends, by user tag ( the row order is sorted ).
    std::vector<DWORD> m_RowTags;
    DWORD m_dwSelected; ///< 0: none

    /// --- bound to community.rml ----------------------------------------------
    std::vector<FriendVM> m_Friends;
    int m_iOnline;
    int m_iTotal;
    bool m_bHasSelection;
    bool m_bHasMessages; ///< conversations to go back to
    int m_iTab; ///< 0 friends, 1 rooms
    std::vector<RoomVM> m_Rooms;
    int m_iRoomCount;
    bool m_bInRoom;
    Rml::String m_strRoomTitle;
};

#endif /// _ROSE_RML_COMMUNITY_H_
