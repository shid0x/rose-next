#ifndef _ROSE_RML_QUEST_JOURNAL_H_
#define _ROSE_RML_QUEST_JOURNAL_H_

/**
 * UI2 quest journal: replaces CQuestDlg ( DLG_TYPE_QUEST ).
 *
 * A window ( routed through IT_MGR: menu, hotkey, Escape ). The quests you
 * carry on the left, the selected one on the right: its description, the
 * countdown of a timed quest, the quest items it holds ( hover for name and
 * description ) and Abandon.
 *
 * Reads CUserDATA::m_Quests directly every frame, so it needs none of the
 * refresh calls the classic dialog gets ( CQuestDlg::UpdateQuestList from the
 * quest packets ). Abandon asks through the UI2 message box and sends the
 * classic CTCmdAbandonQuest; as in the classic dialog it is refused while an
 * NPC dialog is open, and the window says why. The classic minimise /
 * maximise and iconize are dropped.
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

class RoseRmlQuestJournal {
public:
    RoseRmlQuestJournal();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// The player's intent ( OpenDialog / CloseDialog ).
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    struct QuestVM {
        int slot; ///< CUserDATA quest slot ( 0 .. QUEST_PER_PLAYER - 1 )
        Rml::String name;
        bool has_icon;
        Rml::String src;
        Rml::String rect;
        bool timed;
        bool selected;

        bool operator==(const QuestVM& o) const {
            return slot == o.slot && name == o.name && has_icon == o.has_icon && src == o.src
                && rect == o.rect && timed == o.timed && selected == o.selected;
        }
        bool operator!=(const QuestVM& o) const { return !(*this == o); }
    };

    struct ItemVM {
        int index; ///< quest item slot ( 0 .. QUEST_ITEM_PER_QUEST - 1 )
        bool filled;
        Rml::String src;
        Rml::String rect;
        int count; ///< stack, 0 = not stackable

        bool operator==(const ItemVM& o) const {
            return index == o.index && filled == o.filled && src == o.src && rect == o.rect
                && count == o.count;
        }
        bool operator!=(const ItemVM& o) const { return !(*this == o); }
    };

private:
    void SetVisible(bool bVisible);
    void Sample();
    void UpdateTooltip();
    int SelectedSlot() const; ///< the selected quest's slot, or -1

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    /// The selected quest, by id: a slot can be reused by another quest.
    int m_iSelectedID;

    /// --- bound to quests.rml -----------------------------------------------
    std::vector<QuestVM> m_Quests;
    int m_iQuestCount;
    int m_iQuestMax;
    bool m_bHasSelection;
    Rml::String m_strName;
    Rml::String m_strDesc;
    bool m_bTimed;
    bool m_bTimedOut;
    Rml::String m_strTimer;
    std::vector<ItemVM> m_Items;
    bool m_bHasItems;
    bool m_bCanAbandon; ///< no NPC dialog open
};

#endif /// _ROSE_RML_QUEST_JOURNAL_H_
