#ifndef _ROSE_RML_CONVERSATION_H_
#define _ROSE_RML_CONVERSATION_H_

/**
 * UI2 conversation window: replaces the three classic conversation dialogs --
 * CDialogDlg ( NPCs, DLG_TYPE_DIALOG ), CSelectEventDlg ( choice-only: warp
 * gates, event items, DLG_TYPE_SELECTEVENT ) and CEventDialog ( event objects,
 * DLG_TYPE_EVENTDIALOG ) -- with one panel in three looks.
 *
 * NOT a view over the classic dialogs: their Hide() wipes the text and the
 * answers, callbacks included, so a hidden classic dialog cannot be the
 * model. The conversation engine ( CEvent ) talks to the UI through three
 * IT_MGR calls; with UI2 on, OpenQueryDLG and QueryDLG_AppendExam hand the
 * text and each answer here ( Begin / AddAnswer ), and CloseQueryDlg /
 * OpenDialog / IsDlgOpened reach this window through the UI2 window routing.
 * A click calls the answer's own callback ( CEvent::Click_ITEM ), so every
 * script action -- quest triggers, shops, warps, bank, clan, repair -- runs
 * exactly as it did.
 *
 * What the classic dialog did beside drawing, kept: the NPC's name ( read
 * from CDialogDlg, which scripts can rename -- QF_ChangetalkName ) and the
 * hero-quest hide timer ( QF_NpcTalkinterfaceHide ); closing when you walk
 * away from the NPC or it disappears. Added: closing on a zone change or on
 * leaving the world -- the zone load deletes every CEvent, and a held answer
 * would point into freed memory.
 *
 * The NPC text's classic markup ( {FC=n} colour, {B} bold, {BR} line break )
 * is translated to RML, the palette lifted for dark glass.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlConversation {
public:
    /// The three classic dialogs this window stands in for.
    enum Mode {
        MODE_NPC = 0, ///< CDialogDlg: name, text, answers
        MODE_SELECT = 1, ///< CSelectEventDlg: a title and choices
        MODE_EVENT = 2, ///< CEventDialog: text and answers, no name
    };

    RoseRmlConversation();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// A new line from the engine ( OpenQueryDLG ). iOwnerClientIdx is the
    /// object whose distance closes the window ( the NPC, the warp gate, the
    /// avatar for an event item ); -1 for none.
    void Begin(int iMode, const char* pszText, int iOwnerClientIdx);
    /// One answer ( QueryDLG_AppendExam ).
    void AddAnswer(const char* pszText, int iEventID, void (*fpHandler)(int iEventID));

    /// The window routing ( OpenDialog / CloseDialog / IsDlgOpened ) for any
    /// of the three dialog types.
    /// Only for the look it is showing: closing the warp dialog must not end
    /// an NPC conversation, and "is the NPC dialog open?" means an NPC one.
    void SetOpen(int iMode, bool bOpen);
    bool IsOpen(int iMode) const { return m_bOpen && m_iMode == iMode; }

    void Update();

    /// Classic text markup -> RML: {FC=n}..{/FC}, {B}..{/B}, {BR}; the text
    /// escaped and converted from the game code page.
    static Rml::String MarkupToRml(const char* pszText);

    struct AnswerVM {
        int index; ///< 1-based, shown
        Rml::String rml;

        bool operator==(const AnswerVM& o) const { return index == o.index && rml == o.rml; }
        bool operator!=(const AnswerVM& o) const { return !(*this == o); }
    };

private:
    void SetVisible(bool bVisible);
    void Close();
    void SampleName();
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    int m_iOwnerClientIdx;
    int m_iZoneAtOpen; ///< a zone change closes it

    /// The engine's handlers, by answer index ( 0-based ).
    struct Handler {
        int iEventID;
        void (*fpHandler)(int iEventID);
    };
    std::vector<Handler> m_Handlers;

    /// --- bound to conversation.rml -------------------------------------------
    int m_iMode;
    Rml::String m_strRole; ///< "Weapon Merchant"
    Rml::String m_strName; ///< "Raffle"
    Rml::String m_strText; ///< RML
    std::vector<AnswerVM> m_Answers;
};

#endif /// _ROSE_RML_CONVERSATION_H_
