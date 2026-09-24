#ifndef _ROSE_RML_BUFF_BAR_H_
#define _ROSE_RML_BUFF_BAR_H_

/**
 * UI2 replacement for CEndurancePack::Draw ( RoseUi2::PIECE_BUFF_BAR ): the
 * avatar's status effects, the summon-capacity and cart-fuel gauges, and the
 * equipment worn below 5% durability.
 *
 * Same data and rules as the legacy strip -- icons flash through their last
 * 10 s, the goddess blessing never does and lists its bonuses instead -- plus a
 * remaining-time label under each icon, and a tooltip on hover. It hangs
 * under the status panel and follows it when that panel is dragged.
 *
 * Read-only: it enumerates CEndurancePack entities and item slots, and never
 * changes game state.
 */

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

class CObjCHAR;

namespace Rml {
class Context;
class Element;
class ElementDocument;
} // namespace Rml

class RoseRmlBuffBar {
public:
    RoseRmlBuffBar();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    /// The panel to hang under ( the status panel ). Positioned from it every
    /// frame, so dragging the status panel carries the buffs along.
    void SetAnchor(Rml::Element* pAnchor) { m_pAnchor = pAnchor; }

    /// Per frame. Rebuilds the view model and dirties only what changed, so a
    /// steady buff set costs one comparison per icon and no relayout -- except
    /// once a second, when the time labels tick.
    void Update();

    struct BuffVM {
        Rml::String src;
        Rml::String rect;
        Rml::String time; ///< short label under the icon: "45s", "12m", "2h"
        Rml::String name;
        Rml::String detail; ///< tooltip body: time remaining, or goddess bonuses
        bool expiring;

        bool operator==(const BuffVM& o) const {
            return src == o.src && rect == o.rect && time == o.time && name == o.name
                && detail == o.detail && expiring == o.expiring;
        }
        bool operator!=(const BuffVM& o) const { return !(*this == o); }
    };

    /// The status-effect icons on any character, in the legacy strip's order.
    /// Shared with the target frame, which shows its target's buffs the same way.
    static void CollectBuffs(CObjCHAR* pChar, std::vector<BuffVM>& out);

    /// Registers BuffVM with a data model, under the member names the .rml
    /// files use ( src, rect, time, name, detail, expiring ).
    static void RegisterBuffStruct(Rml::DataModelConstructor& constructor);

    struct WornVM {
        Rml::String src;
        Rml::String rect;
        Rml::String name;

        bool operator==(const WornVM& o) const {
            return src == o.src && rect == o.rect && name == o.name;
        }
        bool operator!=(const WornVM& o) const { return !(*this == o); }
    };

private:
    void SetVisible(bool bVisible);
    void Sample();
    void FollowAnchor();

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
    Rml::Element* m_pAnchor;
    float m_fPlacedX;
    float m_fPlacedY;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;

    /// --- bound to buffs.rml -----------------------------------------------
    std::vector<BuffVM> m_Buffs;
    std::vector<WornVM> m_Worn;
    bool m_bSummon;
    float m_fSummonWidth;
    Rml::String m_strSummon;
    Rml::String m_strSummonLabel;
    bool m_bFuel;
    float m_fFuelWidth;
    Rml::String m_strFuel;
    Rml::String m_strFuelLabel;
};

#endif /// _ROSE_RML_BUFF_BAR_H_
