#ifndef _ROSE_RML_MESSAGE_BOX_H_
#define _ROSE_RML_MESSAGE_BOX_H_

/**
 * UI2 message box: a question with two answers, or a notice.
 *
 * Stands in for CMsgBox where UI2 code asks something ( leave the party?
 * hand over the lead? ) or a UI2-era event tells the player something ( an
 * invitation was declined ). Not every CMsgBox: callers opt in one by one,
 * and fall back to IT_MGR::OpenMsgBox when this returns false.
 *
 * - A confirm names its buttons ( "Leave" / "Stay" ) and runs the legacy
 *   CTCommand behind each, so the game action is the message box's own.
 * - A notice has one button and closes itself after kNoticeMs, with a bar
 *   draining toward that.
 *
 * Requests arriving while one is shown wait their turn. Leaving the world
 * answers a pending confirm with its cancel command.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <deque>
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

    /// A question. Takes ownership of both commands ( either may be NULL ) --
    /// but only when it returns true; on false the caller still owns them.
    bool Confirm(const char* pszTitle,
        const char* pszText,
        const char* pszOk,
        const char* pszCancel,
        CTCommand* pOk,
        CTCommand* pCancel);

    /// Something to read; closes itself.
    bool Notice(const char* pszTitle, const char* pszText);

    void Update();

private:
    struct Entry {
        std::string title;
        std::string text;
        std::string ok;
        std::string cancel; ///< empty: a notice
        CTCommand* pOk;
        CTCommand* pCancel;
    };

    bool CanShow() const;
    void Push(const Entry& entry);
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
    Rml::String m_strText;
    Rml::String m_strOk;
    Rml::String m_strCancel;
    bool m_bNotice;
    float m_fTimeLeft; ///< a notice's bar, percent
};

#endif /// _ROSE_RML_MESSAGE_BOX_H_
