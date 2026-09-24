#ifndef _ROSE_RML_TARGET_FRAME_H_
#define _ROSE_RML_TARGET_FRAME_H_

/**
 * UI2 target frame: what you have selected, next to the status panel. A new
 * panel, not a conversion -- the retail client shows its target only as the
 * name tag and bar over its head ( CNameBox ), which stays as it is.
 *
 * Shows the target's name ( coloured by level difference, as the overhead tag
 * is ), its type mark and level for monsters, an HP bar for monsters and for
 * yourself, and the target's status effects with the buff strip's icons and
 * tooltips. The HP numbers follow Options > Play > "Show monster HP".
 *
 * Other players' HP is not shown: outside a party the client has no
 * trustworthy value for it.
 *
 * Read-only against game state. Draggable; the spot is saved.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

#include "RoseRmlBuffBar.h"
#include "RoseRmlEasedBar.h"

class CObjCHAR;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlTargetFrame {
public:
    RoseRmlTargetFrame();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// Per frame: resolves the current target, samples it, and dirties only
    /// what changed.
    void Update();

private:
    void SetVisible(bool bVisible);
    void Sample(CObjCHAR* pTarget);

    template <typename T>
    void Assign(T& field, const T& value, const char* pszName) {
        if (field != value) {
            field = value;
            m_Model.DirtyVariable(pszName);
        }
    }

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;
    int m_iLastTarget; ///< client object index, to snap the bar on a change

    RoseRmlEasedBar m_HpBar;

    /// --- bound to target.rml -----------------------------------------------
    Rml::String m_strName;
    Rml::String m_strNameColor; ///< "#rrggbb"
    Rml::String m_strLevel; ///< empty when not shown
    Rml::String m_strKind; ///< subtitle: NPC job, "Summon", "Player", "You"
    bool m_bMark;
    Rml::String m_strMarkSrc;
    Rml::String m_strMarkRect;
    bool m_bHp;
    Rml::String m_strHp; ///< "cur / max", empty when the option hides numbers
    Rml::String m_strHpPct;
    float m_fHpWidth;
    std::vector<RoseRmlBuffBar::BuffVM> m_Buffs;
};

#endif /// _ROSE_RML_TARGET_FRAME_H_
