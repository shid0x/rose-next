#ifndef _ROSE_RML_TRADE_INVITE_H_
#define _ROSE_RML_TRADE_INVITE_H_

/**
 * UI2 trade request: replaces the CMsgBox Recv_gsv_TRADE_P2P opened for
 * "X wants to trade with you" ( MSGTYPE_RECV_TRADE_REQ ) -- a typed message
 * box, which the plain-notice routing in IT_MGR::OpenMsgBox leaves classic,
 * and which drew under the UI2 windows.
 *
 * Who is asking, Accept / Decline, and a countdown: a request nobody answers
 * is declined after kTimeoutMs ( the classic box waited forever, and while it
 * was up every other request was answered BUSY ). The answers are the legacy
 * commands' ( CTCmdAcceptTradeReq / CTCmdRejectTradeReq ), so the server and
 * the other player see no difference. A separate window from the party
 * invitation: both can be waiting at once. Draggable; the spot is saved.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlTradeInvite {
public:
    RoseRmlTradeInvite();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// A request arrived. False when UI2 cannot show it ( the caller falls
    /// back to the message box ).
    bool Show(unsigned short wFromObjSvrIdx, const char* pszFrom);

    /// A request is waiting for an answer.
    bool IsPending() const { return m_bPending; }

    void Update();

private:
    void SetVisible(bool bVisible);
    void Answer(bool bAccept);

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;

    bool m_bPending;
    unsigned short m_wFromObjSvrIdx;
    unsigned long m_dwShownAt;
    std::string m_strFromGame; ///< the name in the game's code page, for the chat line

    /// --- bound to tradeinvite.rml --------------------------------------------
    Rml::String m_strFrom;
    float m_fTimeLeft; ///< countdown bar, 0-1 ( a scaleX, not a width: no layout )
    int m_iSecondsLeft;
};

#endif /// _ROSE_RML_TRADE_INVITE_H_
