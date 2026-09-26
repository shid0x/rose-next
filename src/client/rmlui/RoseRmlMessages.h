#ifndef _ROSE_RML_MESSAGES_H_
#define _ROSE_RML_MESSAGES_H_

/**
 * UI2 private messages: replaces the CPrivateChatDlg windows
 * ( DLG_TYPE_PRIVATECHAT, one classic window per friend ).
 *
 * One window, a tab per conversation. Opened by double-clicking an online
 * friend ( IT_MGR::OpenPrivateChatDlg ) or by an incoming message
 * ( Recv_wsv_MESSENGER_CHAT ) -- both branch here under UI2, so no classic
 * window is created: those showed themselves directly ( no IT_MGR routing )
 * and their Hide() deleted them, history and all. Here a conversation's
 * history lasts the session; closing the window keeps it, closing a tab
 * drops it. An incoming message opens the window without taking the
 * keyboard, and marks its tab when another one is in front.
 *
 * Sending follows CPrivateChatDlg::SendChatMsg: the friend is looked up in
 * the hidden CCommDlg each time ( gone: not sent ); offline, blocking you or
 * having removed you: not sent -- classic dropped it silently, here a line
 * says why. Otherwise Send_cli_MESSENGER_CHAT and a local echo.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <windows.h>
#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlMessages {
public:
    RoseRmlMessages();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// IT_MGR's OpenDialog / CloseDialog for DLG_TYPE_PRIVATECHAT.
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }
    bool HasConversations() const { return !m_Convs.empty(); }

    /// A friend double-clicked: their tab, in front, with the caret.
    void OpenConversation(DWORD dwUserTag, const char* pszName);
    /// An incoming message ( game code page ).
    void Receive(DWORD dwUserTag, const char* pszName, const char* pszMsg);

    void Update();

    struct TabVM {
        int index;
        bool used;
        bool on;
        bool unread;
        bool online;
        Rml::String name;

        bool operator==(const TabVM& o) const {
            return index == o.index && used == o.used && on == o.on && unread == o.unread
                && online == o.online && name == o.name;
        }
        bool operator!=(const TabVM& o) const { return !(*this == o); }
    };

    struct LineVM {
        int index;
        bool used;
        int kind; ///< 0 theirs, 1 mine, 2 a note
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
    struct Conversation {
        DWORD dwTag;
        std::string strName; ///< game code page
        std::vector<Line> lines;
        bool bUnread;
    };

    int FindConversation(DWORD dwUserTag) const;
    int AddConversation(DWORD dwUserTag, const char* pszName);
    void AddLine(int iConv, int iKind, const char* pszWho, const char* pszText);
    void SelectTab(int iConv);
    void CloseTab(int iConv);
    void OnSend();
    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void Sample();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;
    bool m_bFocusField; ///< the caret in the field once shown
    int m_iScrollFrames; ///< scroll the log to the end in this many frames ( after layout )

    std::vector<Conversation> m_Convs;
    int m_iActive; ///< -1: none

    /// --- bound to messages.rml ------------------------------------------------
    std::vector<TabVM> m_Tabs;
    std::vector<LineVM> m_Lines;
    bool m_bHasConv;
    Rml::String m_strWith; ///< the active friend's name
    Rml::String m_strState; ///< and state
    bool m_bOnline;
};

#endif /// _ROSE_RML_MESSAGES_H_
