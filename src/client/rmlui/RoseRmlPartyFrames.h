#ifndef _ROSE_RML_PARTY_FRAMES_H_
#define _ROSE_RML_PARTY_FRAMES_H_

/**
 * UI2 party frames: replaces CPartyDlg ( DLG_TYPE_PARTY ).
 *
 * HUD, not a window: shown while you are in a party. A header with the
 * party level and its EXP, Options and Leave; under it one card per other
 * member ( you are on the status panel already ) -- name, level, HP, their
 * status effects, and whether they are near, far, elsewhere or offline.
 * Click a card to target that member. The leader gets Lead / Kick on each
 * card, with the legacy confirmations.
 *
 * The legacy CPartyDlg stays alive hidden: it is CParty's observer and still
 * writes the join / leave / leader chat lines.
 */

#include "RoseRmlBuffBar.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlPartyFrames {
public:
    RoseRmlPartyFrames();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void Update();

    struct MemberVM {
        int tag; ///< user tag: stable across zones ( the server index is not )
        Rml::String name;
        Rml::String level; ///< "213", or "" when not in sight
        bool leader;
        int state; ///< 0 near, 1 far, 2 elsewhere, 3 offline
        Rml::String state_text; ///< "", "Far", "Elsewhere", "Offline"
        Rml::String hp; ///< "5,210 / 6,158", or ""
        float hp_pct; ///< bar width, percent
        bool hp_low;
        std::vector<RoseRmlBuffBar::BuffVM> buffs;

        bool operator==(const MemberVM& o) const {
            return tag == o.tag && name == o.name && level == o.level && leader == o.leader
                && state == o.state && state_text == o.state_text && hp == o.hp
                && hp_pct == o.hp_pct && hp_low == o.hp_low && buffs == o.buffs;
        }
        bool operator!=(const MemberVM& o) const { return !(*this == o); }
    };

private:
    void SetVisible(bool bVisible);
    void Sample();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;

    /// --- bound to party.rml ------------------------------------------------
    int m_iLevel; ///< party level
    Rml::String m_strExpPct;
    float m_fExpWidth;
    bool m_bLeader; ///< you lead: Lead / Kick are offered
    std::vector<MemberVM> m_Members;
};

#endif /// _ROSE_RML_PARTY_FRAMES_H_
