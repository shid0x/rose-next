#ifndef _ROSE_RML_ZONE_TIMERS_H_
#define _ROSE_RML_ZONE_TIMERS_H_

/**
 * UI2 zone timers: the countdowns the server shows the players of a zone
 * ( GSV_ZONE_TIMER, CZoneTimers in zoneobjects.h ). First use: the Karkia
 * catacombs' "The crypt seals in 1:42:10"; the boss's return comes next.
 *
 * New in UI2: the classic interface has no such panel and relies on the
 * zone's announcements. One row per timer, a label and the time left, counted
 * down here from what the server sent. Shown only while a timer runs; placed
 * under the minimap the first time, movable ( the whole panel is the handle )
 * and saved.
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

class RoseRmlZoneTimers {
public:
    RoseRmlZoneTimers();

    bool Initialise(Rml::Context* pContext, const std::string& strAssetDir);
    void Shutdown();

    void Update();

    struct RowVM {
        Rml::String label;
        Rml::String time; ///< "1:42:10", "12:31", "0:45"
        bool soon; ///< under a minute: drawn in the accent colour
        bool used; ///< rows are never removed, only hidden

        bool operator==(const RowVM& o) const {
            return label == o.label && time == o.time && soon == o.soon && used == o.used;
        }
        bool operator!=(const RowVM& o) const { return !(*this == o); }
    };

private:
    bool IsInWorld() const;
    void SetVisible(bool bVisible);
    void PlaceDefault();

    Rml::Context* m_pContext;
    Rml::ElementDocument* m_pDocument;
    Rml::Element* m_pPanel;
    Rml::DataModelHandle m_Model;
    bool m_bVisible;

    /// --- bound to timers.rml -------------------------------------------------
    std::vector<RowVM> m_Rows;
};

#endif /// _ROSE_RML_ZONE_TIMERS_H_
