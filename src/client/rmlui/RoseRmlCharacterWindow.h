#ifndef _ROSE_RML_CHARACTER_WINDOW_H_
#define _ROSE_RML_CHARACTER_WINDOW_H_

/**
 * UI2 character window: replaces CCharacterDLG ( DLG_TYPE_CHAR ).
 *
 * Opened and closed through IT_MGR like the skill window, so the menu, the
 * hotkey and Escape work unchanged.
 *
 * Two tabs instead of the legacy three, under a shared header: who you are
 * ( name, class, clan, level ), EXP and stamina. Stats: the six attributes
 * beside the eight combat
 * stats. An attribute shows its total with the part gear and passives add,
 * and a raise button that says what the next point costs; a spent point
 * flashes its row. Every row explains itself on hover with the same text as
 * the classic window ( StatDescriptions ). A second tab shows your union and
 * the points you hold with each of them.
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

class RoseRmlCharacterWindow {
public:
    RoseRmlCharacterWindow();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// The player's intent ( OpenDialog / CloseDialog ), independent of
    /// whether it can be seen right now ( in the world, alive ).
    void SetOpen(bool bOpen);
    bool IsOpen() const { return m_bOpen; }

    void Update();

    /// One of the six attributes ( BA_* order ).
    struct AttrVM {
        int id; ///< BA_*
        Rml::String abbr; ///< "STR"
        Rml::String name; ///< "Strength"
        int total; ///< what the game uses ( GetCur_* )
        int bonus; ///< total - base: gear and passives
        bool can_up; ///< the + is live
        bool up_short; ///< not enough points ( the cost shows red )
        Rml::String up_text; ///< "3 pts", or "Max"
        /// 0, or which of two identical flash animations plays: alternating
        /// restarts it when points go in faster than it lasts.
        int flash;

        bool operator==(const AttrVM& o) const {
            return id == o.id && abbr == o.abbr && name == o.name && total == o.total
                && bonus == o.bonus && can_up == o.can_up && up_short == o.up_short
                && up_text == o.up_text && flash == o.flash;
        }
        bool operator!=(const AttrVM& o) const { return !(*this == o); }
    };

    /// One of the eight derived combat stats.
    struct CombatVM {
        int id; ///< index into g_DerivedStatDescriptions
        Rml::String name;
        Rml::String value;

        bool operator==(const CombatVM& o) const {
            return id == o.id && name == o.name && value == o.value;
        }
        bool operator!=(const CombatVM& o) const { return !(*this == o); }
    };

    /// One union ( LIST_UNION row ) and your points with it.
    struct UnionVM {
        int id;
        Rml::String name;
        Rml::String color; ///< "#rrggbb"
        Rml::String points;
        bool mine; ///< the union you belong to

        bool operator==(const UnionVM& o) const {
            return id == o.id && name == o.name && color == o.color && points == o.points
                && mine == o.mine;
        }
        bool operator!=(const UnionVM& o) const { return !(*this == o); }
    };

private:
    void SetVisible(bool bVisible);
    void Sample();
    void SampleUnions();
    void UpdateTooltip();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bOpen;
    bool m_bVisible;

    /// --- bound to character.rml ----------------------------------------------
    Rml::String m_strName;
    Rml::String m_strSubtitle; ///< "Magician  -  Clan"
    int m_iLevel;
    Rml::String m_strExp; ///< "12,345 / 67,890"
    Rml::String m_strExpPct;
    float m_fExpPct;
    Rml::String m_strStamina;
    float m_fStaminaPct;
    int m_iPoints; ///< unspent stat points
    std::vector<AttrVM> m_Attrs;
    std::vector<CombatVM> m_Combat;
    int m_iTab; ///< 0 stats, 1 union
    Rml::String m_strUnion; ///< your union's name, or "None"
    Rml::String m_strUnionColor;
    std::vector<UnionVM> m_Unions;

    /// Flash bookkeeping: last seen base value ( -1 = not yet ) and when each
    /// row's flash ends.
    int m_iPrevBase[6];
    int m_iFlash[6];
    DWORD m_dwFlashEnd[6];
};

#endif /// _ROSE_RML_CHARACTER_WINDOW_H_
