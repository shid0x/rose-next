#ifndef _ROSE_RML_SKILL_WINDOW_H_
#define _ROSE_RML_SKILL_WINDOW_H_

/**
 * UI2 skill window: replaces CSkillDLG ( DLG_TYPE_SKILL ).
 *
 * A window the player opens and closes, unlike the HUD panels: IT_MGR's
 * OpenDialog / CloseDialog / IsDlgOpened hand DLG_TYPE_SKILL to RoseUi2 while
 * UI2 is on, so every opener ( menu, hotkey, menu shortcut on the skill bar )
 * and the Escape key's close / reopen work unchanged.
 *
 * Laid out fresh in the 667 style rather than copied: tabs with counts, one
 * card per skill ( icon with cooldown, name, level out of its maximum with a
 * bar, use costs ), a level-up button that says what the next level costs,
 * and the Skill Tree button. Same game actions underneath: the level-up
 * request and its checks are CSkillListItem's, skills drag onto the skill bar
 * with the same drop commands, a double-click uses the skill.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CDragItem;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlSkillWindow {
public:
    RoseRmlSkillWindow();
    ~RoseRmlSkillWindow();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// The player's intent ( OpenDialog / CloseDialog ), independent of
    /// whether it can be seen right now ( in the world, alive ).
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    struct SkillVM {
        int slot; ///< skill slot index ( CSkillSlot )
        Rml::String src;
        Rml::String rect;
        Rml::String name;
        int level;
        int max_level;
        float level_pct; ///< level / max, for the thin bar
        Rml::String costs; ///< "MP 51" ...
        float cd; ///< cooldown curtain, percent
        bool passive;
        bool can_up; ///< the + is live
        bool up_short; ///< not enough SP ( the cost shows red )
        Rml::String up_text; ///< "2 SP", or "Max"

        bool operator==(const SkillVM& o) const {
            return slot == o.slot && src == o.src && rect == o.rect && name == o.name
                && level == o.level && max_level == o.max_level && level_pct == o.level_pct
                && costs == o.costs && cd == o.cd && passive == o.passive
                && can_up == o.can_up && up_short == o.up_short && up_text == o.up_text;
        }
        bool operator!=(const SkillVM& o) const { return !(*this == o); }
    };

    /// CSkillListItem::IsValidLevelUp, as a function: can this slot's skill go
    /// up a level now? On false, *pstrWhy says why ( the legacy chat text ).
    static bool CanLevelUp(int iSlot, std::string* pstrWhy);

private:
    void SetVisible(bool bVisible);
    void Sample();
    void UpdateDragStart();
    void UpdateTooltip();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    /// Skills drag onto either skill bar row, as from the legacy window.
    CDragItem* m_pDragItem;
    int m_iPressSlot;
    int m_iPressX;
    int m_iPressY;

    /// --- bound to skills.rml -----------------------------------------------
    int m_iTab; ///< 0 basic, 1 active, 2 passive ( CSkillSlot pages )
    int m_iCountBasic;
    int m_iCountActive;
    int m_iCountPassive;
    int m_iSkillPoints;
    bool m_bHasTree; ///< Skill Tree button: only once you have a job
    std::vector<SkillVM> m_Skills;
};

#endif /// _ROSE_RML_SKILL_WINDOW_H_
