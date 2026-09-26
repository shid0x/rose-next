#ifndef _ROSE_RML_MESSAGE_BOX_H_
#define _ROSE_RML_MESSAGE_BOX_H_

/**
 * UI2 message box: every CMsgBox-style popup UI2 shows. Classic boxes draw
 * under the UI2 windows, so while UI2 is on IT_MGR::OpenMsgBox routes here
 * whatever it can ( a notice, an OK/Cancel question, an OK that runs a
 * command -- anything without a message type or an invoking dialog ), and
 * the other popups call in by name: requests from other players, the death
 * window, the logout countdown, the tutorial's notices.
 *
 * One entry is shown at a time; the rest wait their turn ( an urgent one --
 * the death window, the countdown -- goes to the front ). An entry is:
 *
 * - its text ( RML: plain text is escaped, the tutorial's markup converted );
 * - one or two buttons, each running its legacy CTCommand the way CMsgBox
 *   runs them ( IT_MGR executes and frees it ), the other one freed;
 * - optionally a type, which the game can ask about ( "is a clan invitation
 *   already waiting?" ), close without an answer, or re-word ( the countdown );
 * - optionally a timeout, with a draining bar: a notice closes itself, a
 *   request from another player is declined rather than waiting forever;
 * - optionally a validity check, polled while it is shown: a cart ride
 *   invitation is declined once the two carts drift apart.
 *
 * Leaving the world answers what is left with its second button ( a request
 * is declined, a one-button entry is dropped without running ).
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <deque>
#include <functional>
#include <string>

class CTCommand;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlMessageBox {
public:
    RoseRmlMessageBox();
    ~RoseRmlMessageBox();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// Everything one entry can be. Texts are UTF-8; ok is required,
    /// cancel empty makes a one-button box.
    struct Request {
        std::string title;
        std::string text; ///< plain text ( escaped ), or RML when bRml
        bool bRml;
        std::string ok;
        std::string cancel;
        CTCommand* pOk; ///< owned once Show returns true
        CTCommand* pCancel;
        int iType; ///< 0: none
        unsigned long dwTimeoutMs; ///< 0: waits for an answer
        bool bTimeoutOk; ///< what the timeout answers ( a notice OK, a request no )
        std::string timeoutChat; ///< a chat line when it times out ( game code page )
        std::function<bool()> valid; ///< empty: always valid
        bool bUrgent; ///< in front of the queue

        Request():
            bRml(false),
            pOk(NULL),
            pCancel(NULL),
            iType(0),
            dwTimeoutMs(0),
            bTimeoutOk(false),
            bUrgent(false) {}
    };

    /// False when UI2 cannot show it ( not in the world ); the caller keeps
    /// the commands and falls back to the classic box.
    bool Show(const Request& request);

    /// A question. Takes ownership of both commands ( either may be NULL ) --
    /// but only when it returns true.
    bool Confirm(const char* pszTitle,
        const char* pszText,
        const char* pszOk,
        const char* pszCancel,
        CTCommand* pOk,
        CTCommand* pCancel);

    /// Something to read; closes itself.
    bool Notice(const char* pszTitle, const char* pszText);

    /// An entry of this type is shown or waiting.
    bool IsTypePending(int iType) const;
    /// Drop every entry of this type without running its commands.
    void CloseType(int iType);
    /// Re-word every entry of this type ( plain text ).
    void SetTypeText(int iType, const char* pszText);

    void Update();

private:
    struct Entry {
        Request req;
        Rml::String rml;
    };

    bool CanShow() const;
    void Present(); ///< binds the front entry
    void Answer(bool bOk);
    void SetVisible(bool bVisible);

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;

    std::deque<Entry> m_Queue;
    unsigned long m_dwShownAt;

    /// --- bound to msgbox.rml -----------------------------------------------
    Rml::String m_strTitle;
    Rml::String m_strText; ///< RML
    Rml::String m_strOk;
    Rml::String m_strCancel;
    bool m_bSingle; ///< one button
    bool m_bTimed; ///< shows the draining bar
    float m_fTimeLeft; ///< the bar, percent
};

#endif /// _ROSE_RML_MESSAGE_BOX_H_
