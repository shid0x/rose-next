#ifndef _ROSE_RML_CHAT_ROOM_H_
#define _ROSE_RML_CHAT_ROOM_H_

/**
 * UI2 chat room: replaces CChatRoomDlg ( DLG_TYPE_CHATROOM ) while you are
 * in a room ( rooms are listed, made and joined in the community window's
 * Rooms tab ).
 *
 * CChatRoom is the model; CChatRoomDlg, kept hidden, is its only observer and
 * keeps the members ( the master first ), which name a message's sender.
 * **Its Hide() leaves the room** and it used to Show() itself when a room was
 * made or joined: under UI2 it opens this window through IT_MGR instead, and
 * closes it when the room is gone ( left, or kicked -- classic left the
 * window open ). Closing this window leaves the room. Room messages
 * ( Recv_wsv_CHATROOM_MSG ) come here; the server echoes your own.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>
#include <set>
#include <string>
#include <vector>

class CChatRoomDlg;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlChatRoom {
public:
    RoseRmlChatRoom();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog. Closing leaves the room.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    /// A room message ( game code page ).
    void Receive(WORD wUserId, const char* pszMsg);

    void Update();

    struct MemberVM {
        int index;
        bool used;
        bool master;
        Rml::String name;

        bool operator==(const MemberVM& o) const {
            return index == o.index && used == o.used && master == o.master && name == o.name;
        }
        bool operator!=(const MemberVM& o) const { return !(*this == o); }
    };

    struct LineVM {
        int index;
        bool used;
        int kind; ///< 0 others, 1 mine, 2 a note ( joined / left )
        Rml::String who;
        Rml::String text;

        bool operator==(const LineVM& o) const {
            return index == o.index && used == o.used && kind == o.kind && who == o.who
                && text == o.text;
        }
        bool operator!=(const LineVM& o) const { return !(*this == o); }
    };

private:
    struct Line {
        int iKind;
        std::string strWho; ///< game code page
        std::string strText;
    };

    CChatRoomDlg* Dlg() const;
    bool IsInWorld() const;
    void AddLine(int iKind, const char* pszWho, const char* pszText);
    void TrackMembers();
    void OnSend();
    void SetVisible(bool bVisible);
    void Sample();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    bool m_bFocusField;
    int m_iScrollFrames;

    std::vector<Line> m_Lines;
    std::set<std::string> m_KnownMembers; ///< for the joined / left notes
    bool m_bMembersKnown; ///< the first list is not "joined"

    /// --- bound to chatroom.rml ------------------------------------------------
    Rml::String m_strTitle;
    std::vector<MemberVM> m_MemberVMs;
    std::vector<LineVM> m_LineVMs;
    int m_iMemberCount;
};

#endif /// _ROSE_RML_CHAT_ROOM_H_
