#ifndef _ROSE_RML_PARTY_INVITE_H_
#define _ROSE_RML_PARTY_INVITE_H_

/**
 * UI2 party invitation: replaces the CMsgBox Recv_gsv_PARTY_REQ opened
 * ( MSGTYPE_RECV_PARTY_REQ ) for "X invites you" and "X wants to form a
 * party".
 *
 * Who is asking and what, Accept / Decline, and a countdown: an invitation
 * nobody answers is declined after kTimeoutMs rather than sitting on screen
 * forever and blocking every later invite ( a pending one answers BUSY ).
 * The answers are the legacy commands' ( CTCmdAcceptPartyJoin /
 * CTCmdRejectPartyJoin ), so the server sees no difference. Draggable; the
 * spot is saved.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlPartyInvite {
public:
    RoseRmlPartyInvite();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// A request arrived. bMake: they want to form a new party with you;
    /// otherwise their party invites you. Returns false when UI2 cannot show
    /// it ( the caller falls back to the message box ).
    bool Show(unsigned short wFromObjSvrIdx, const char* pszFrom, bool bMake);

    /// An invitation is waiting for an answer.
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

    /// --- bound to partyinvite.rml ------------------------------------------
    Rml::String m_strFrom;
    bool m_bMake;
    float m_fTimeLeft; ///< countdown bar, percent
    int m_iSecondsLeft;
};

#endif /// _ROSE_RML_PARTY_INVITE_H_
