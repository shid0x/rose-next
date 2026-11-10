#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace Nav {

/// No height known (a character the grid has not placed yet, a zone without a grid).
constexpr short NO_Z = -32768;

/// The avatar's step (UpdateFootHeight_Avatar casts down from foot + 135 cm).
constexpr float STEP_CM = 135.f;
/// The riser an object floor allows between neighbouring cells: the client stops the avatar on a
/// mesh face steeper than 54 degrees that rises above about 0.7 of its centre of mass over the
/// feet (scripts/mapgen/navbake.py, rule 4). 45 cm until 2026-10-29; 60 lets ordinary-mesh stairs
/// (risers up to 53 cm) link. Keep it equal to the bake's. A map's grid may carry its own (lnav
/// version 3, the bake's OBJ_STEP_BY_ZONE): Grid::ObjStepCm.
constexpr float OBJ_STEP_CM = 60.f;

enum SpanFlag : unsigned char {
    SPAN_WALK = 1, ///< nothing 30-180 cm above it: a body fits
    SPAN_REACHED = 2, ///< connected to the zone's start, revive, warp, regen or NPC points
    SPAN_TERRAIN = 4,
    SPAN_UP = 8, ///< an up-facing floor (or the terrain)
    SPAN_HAZARD = 16, ///< the client would draw a monster here 3 m+ higher (a roof)
    SPAN_STAIRS = 32, ///< a floor on HEIGHTONLY parts: links at the terrain's step, not 45 cm
};

/// One floor at a spot.
struct Span {
    float z;
    /// Its surface's lowest and highest point in the cell: a ramp or a flight of steps rises
    /// through its cell (lnav version 2). z for the terrain and for version 1 grids.
    float lo, hi;
    unsigned char flags;
    unsigned char clearance; ///< 1 next to an obstacle, 2 with 1.5 m clear, 3 with 2.5 m
    unsigned short component; ///< 0 = unreached
};

/// The bake's link rule between floors in neighbouring cells (scripts/mapgen/navbake.py, rule 4):
/// within STEP_CM on the terrain and HEIGHTONLY stairs; elsewhere within OBJ_STEP_CM, or, on a
/// ramp or a flight steeper than that (Muris' great stairs: 78 cm a metre), within STEP_CM when
/// the higher floor starts within a riser of the lower one's top.
inline bool
StepLinks(const Span& a, const Span& b, float objStep = OBJ_STEP_CM) {
    constexpr unsigned char GENTLE = SPAN_TERRAIN | SPAN_STAIRS;
    const float dz = a.z > b.z ? a.z - b.z : b.z - a.z;
    if ((a.flags & GENTLE) && (b.flags & GENTLE))
        return dz <= STEP_CM;
    if (dz <= objStep)
        return true;
    const float gap = a.z <= b.z ? b.lo - a.hi : a.lo - b.hi;
    return dz <= STEP_CM && gap <= objStep;
}

/// A zone's layered walk grid, <ZONE FOLDER>.lnav, baked by scripts/nav-bake.py (format:
/// scripts/rosefmt/lnav.py; design: doc/pathfinding-and-z-plan.md, section 3.1). Read-only
/// once loaded, so any thread may query it; the one thing that changes is the block mask
/// (SetBlockMask), swapped whole by the zone's thread.
///
/// 1 m cells in 160 x 160 tiles, one per map chunk. Every cell has its terrain floor; a
/// "complex" cell lists every floor (decks, platforms, stairs, Z-only floors) lowest first,
/// and may carry the height the client draws a monster at (GetHeightTop).
class Grid {
public:
    /// Load zoneDir + folder + ".lnav". Null (and why, in err) when there is none or it
    /// does not parse. staleInputs: the bake's inputs that changed or vanished since
    /// (chunk files are read from zoneDir, the rest from baseDir).
    static std::unique_ptr<Grid> Load(const std::string& zoneDir,
        const std::string& baseDir,
        std::string& err,
        int& staleInputs);

    ~Grid();

    /// Terrain height at a world position (bilinear over the 2.5 m vertices, like the
    /// client's CMAP::GetHEIGHT), or NO_Z off the grid.
    float TerrainZ(float x, float y) const;

    /// Every floor at a world position, lowest first; returns the count (0 off the grid).
    int SpansAt(float x, float y, Span* out, int iMax) const;

    /// Cells blocked on top of the bake: walls the server puts up at run time (the Karkia
    /// crypts' daily maze). w x h 1 m cells from (x0, y0) world cm, row 0 south, non-zero =
    /// blocked. SpansAt reports a blocked cell's floors without SPAN_WALK, and LineWalk, the
    /// path search, FollowZ and NearestSpan all read the grid through SpansAt: every AI
    /// move, spawn re-roll, sight line and /nav readout sees the wall at once. Replaces the
    /// last mask; cells == nullptr clears it. One writer, the zone's own thread.
    void SetBlockMask(float x0, float y0, int w, int h, const unsigned char* cells);

    /// Where today's client draws a monster standing here (GetHeightTop), or NO_Z.
    float VisualTopZ(float x, float y) const;

    /// The height a walker reaches going from 'from' at fromZ to 'to', cell by cell: at
    /// each cell the highest floor no more than STEP_CM above its current height, or the
    /// lowest floor when every one is higher (it falls). The avatar's own rule.
    float FollowZ(float fromZ, float fromX, float fromY, float toX, float toY) const;

    /// The floor nearest z within 'within' cm at a world position whose flags include all of
    /// 'mask'; false if none.
    bool NearestSpan(float x, float y, float z, float within, Span& out, unsigned char mask = 0) const;

    /// Walk the straight line from (x0, y0) at height z0 to (x1, y1) cell by cell over reached,
    /// walkable floors, with the bake's step rule (StepLinks). The start may be any floor (a walker steps out of a blocked
    /// cell). True if it gets there; either way pLastX/pLastY (if given) receive the last cell
    /// centre it reached, or the end point, and pLastZ the floor it stands on there.
    bool LineWalk(float x0, float y0, float z0, float x1, float y1, float* pLastX = nullptr,
        float* pLastY = nullptr, float* pLastZ = nullptr) const;

    /// The floor a regen point's monsters belong to, as the bake resolved it (the point's
    /// authored height, or where the client drops a spawn), or NO_Z for a position that is
    /// not a baked regen point.
    float AnchorZ(float x, float y) const;

    /// This map's object step (OBJ_STEP_CM unless its grid says otherwise: the Starlight Lounge's
    /// stairways start with 61-64 cm risers).
    float ObjStepCm() const { return m_fObjStep; }

    size_t Bytes() const { return m_Bytes; }
    int TileCount() const { return m_iTiles; }

private:
    struct Tile;
    Grid();
    const Tile* TileAt(float x, float y, int& cell) const;
    float CellZ(const Tile& t, int cell, float x, float y, float stored, unsigned char flags) const;
    float Pick(const Span* spans, int n, float curZ) const;
    void Masked(float x, float y, Span* out, int n) const;

    struct BlockMask {
        float x0, y0;
        int w, h;
        std::vector<unsigned char> cells;
    };
    std::atomic<const BlockMask*> m_pMask{nullptr};
    std::vector<std::unique_ptr<BlockMask>> m_Masks; // every one set: a reader may still hold an old one

    struct Anchor {
        float x, y;
        short z;
        unsigned short flags;
    };
    std::vector<Anchor> m_Anchors; // sorted by x

    Tile* m_pTiles[64][64];
    int m_iTiles = 0;
    size_t m_Bytes = 0;
    float m_fObjStep = OBJ_STEP_CM;
};

} // namespace Nav
