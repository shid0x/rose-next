#pragma once

#include <vector>

namespace Nav {

class Grid;
class ZoneMeter;

/// A zone tick's allowance for path searches (doc/pathfinding-and-z-plan.md, section 3.2,
/// "Budgets"): a number of searches, of expanded cells, and of wall-clock time. The zone
/// thread resets it at the top of each tick; every search on that thread draws from it. A
/// caller refused a search keeps what it was doing and asks again next tick.
class TickBudget {
public:
    void Reset();
    /// Whether another search may start this tick.
    bool CanSearch() const;
    int ExpansionsLeft() const { return m_iExpansions; }
    void Spend(int iExpansions, LONGLONG llTicks);

private:
    int m_iSearches = 0;
    int m_iExpansions = 0;
    LONGLONG m_llTicksLeft = 0;
};

struct PathQuery {
    float x0, y0, z0; ///< the start: where the walker stands, on the floor it walks on
    float x1, y1, z1; ///< the goal, on the goal's floor (NO_Z: the floor nearest z0, at any height)
    int iPrefClass = 1; ///< the clearance class the walker prefers (1-3): narrower costs 3x
    int iGoalSnapCells = 3; ///< how far round the goal a walkable floor is looked for (m)
};

enum class PathResult {
    Found, ///< to the goal
    Partial, ///< as far as the search got, at least 2 m closer to the goal
    Failed, ///< no way found within the search window and the cells one search may expand
    Unreachable, ///< start and goal are on floors the bake never connected: no search ran
    NoFloor, ///< no walkable floor near the start or near the goal
    Deferred, ///< this tick's budget is spent: ask again next tick
};

const char* PathResultName(PathResult r);

/// A zone's own way round its walls, asked before the search (CZoneTHREAD::m_pNavRouter):
/// the Karkia crypts' maze knows its doorways, and a route across it is longer than the
/// search's window and budget allow. True fills 'out' like FindPath does (waypoints, the
/// goal last, the start left out); false leaves the pair to FindPath. Called on the zone
/// thread.
class Router {
public:
    virtual ~Router() {}
    virtual bool Route(float x0, float y0, float x1, float y1, std::vector<tPOINTF>& out) = 0;
};

/// A* from the start to the goal over the grid's reached walkable floors (8 neighbours, the
/// bake's step rule between floors, a 128 x 128-cell window), pulled straight into at most 16
/// waypoints. The start is not in 'out'; the last waypoint is the goal itself when found.
/// Runs on the zone thread: its search arrays belong to the calling thread.
PathResult FindPath(const Grid& grid, const PathQuery& q, TickBudget& budget, ZoneMeter& meter,
    std::vector<tPOINTF>& out, int* pExpansions = nullptr);

} // namespace Nav
