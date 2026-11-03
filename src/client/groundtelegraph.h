#pragma once

#include <unordered_map>
#include <vector>

#include <d3dx9math.h>

class CObjCHAR;

/// Boss attack telegraphs (2026-10-18): while a monster casts a skill flagged in
/// LIST_SKILL col 83 (SKILL_TELEGRAPH), the area it is about to hit is drawn on the
/// ground -- a translucent red area with a bright rim, and an inner fill that grows to
/// the rim as the cast runs, so a player sees both where it lands and when. When the
/// skill fires the area flashes and fades; an interrupted cast (stun, death, cancel)
/// just goes.
///
/// Client-only, from data the client already has: the cast start
/// (CObjAI::ProcSkillCastingAction, the same moment the cast clip starts), the scope
/// (io_skill.h SKILL_SCOPE_*) and the cast length (one play of the cast clip at its
/// casting speed, as the server times it). Three shapes, as the server measures them
/// (CObjCHAR::Skill_DamageToAROUND):
///   - a circle of SKILL_SCOPE_RADIUS around the caster: a type-17 skill, or any area
///     cast on a target that has no aim (the server resets that aim to the caster);
///   - a cone (SKILL_SCOPE_CONE) or a frontal strip (SKILL_SCOPE_FRONTAL) from the
///     caster toward the spot the server aimed it at when the cast began, sent with
///     GSV_SKILL_START (CObjAI::m_PosSkillAIM). The fill runs out from the caster.
///   - a fan of strips (SKILL_FAN, 2026-11-03): that strip once per line, turned round
///     the caster, the middle one on the aim.
/// Monsters do not move while casting, so the shape stays where the cast began.
///
/// The geometry follows the terrain (CTERRAIN::GetHeight per vertex, lifted a little)
/// and goes to the engine's ground overlay (::setGroundOverlay) every frame, drawn
/// after the opaque map objects and characters: z-tested, so rocks hide it and characters
/// stand over it, and it shows on a bridge deck. The player's aim, and any shape whose
/// centre stands on a deck, follows the walkable map objects too (Height).
class CGroundTelegraphs {
public:
    static CGroundTelegraphs& Instance();

    /// pCaster has just started casting nSkillIDX; dwCastMS is its cast clip's length.
    /// Its aim, if any, is pCaster->m_PosSkillAIM while m_bSkillAimActive.
    void OnCastStart(CObjCHAR* pCaster, short nSkillIDX, unsigned long dwCastMS);
    /// The aim reached a cast that had already started without it (Recv_gsv_SKILL_START):
    /// its telegraph turns from the fallback circle into the cone or strip, timing kept.
    void OnAimArrived(CObjCHAR* pCaster);
    /// Per frame (CObjectMANAGER::ProcOBJECT): ends the finished casts, sends the geometry.
    void Update();
    /// CObjectMANAGER::Clear deleted every object: drop them all.
    void Forget();

    /// The player's own area skill while it is aimed (CSkillAim, 2026-10-19), set every
    /// frame until ClearPreview: its area at posAim -- or, for a strip or cone, from
    /// posCaster toward posAim -- in a friendly colour, and a faint ring at its range.
    void SetPreview(const D3DXVECTOR3& posCaster, const D3DXVECTOR3& posAim, short nSkillIDX);
    void ClearPreview() { m_bPreview = false; }

    /// A map object a character can stand on, as CTERRAIN::GetHeightTop counts them: a
    /// collision level other than none, not "not moveable", not a character or an item. With
    /// bNotMoveable, "not moveable" ones too: the avatar's own foot collision stands on them
    /// (the Shady Jungle rope bridges are flag 12), but so are tree crowns and cave ceilings, so
    /// a caller takes them only near a character that stands on an object itself.
    static bool IsWalkableNode(unsigned int hNode, bool bNotMoveable = false);
    /// Whether a node is "not moveable" (ZZ_CL_NOTMOVEABLE).
    static bool IsNotMoveableNode(unsigned int hNode);

private:
    enum Shape { SHAPE_CIRCLE, SHAPE_CONE, SHAPE_STRIP };

    struct Telegraph {
        short nObjIdx;
        CObjCHAR* pCaster; // what the slot held at the cast start
        short nSkillIDX;
        Shape shape;
        float fX, fY, fZ; // the caster at the cast start, world cm (fZ: where there is no terrain)
        float fDirX, fDirY; // cone, strip: unit vector toward the aim
        float fLength; // cm: circle radius, cone or strip length
        float fHalfAngle; // cone, radians
        float fHalfWidth; // strip, cm
        unsigned long dwStart;
        unsigned long dwCastMS;
        unsigned long dwFiredAt; // 0 while casting; else when it fired (the flash)
        bool bSurfaces; // follow the map objects as well as the terrain (the player's aim)
    };

    /// The tops of the walkable map objects at one point of a 1 m world lattice: a ray
    /// down through each object found there. Cached for the zone (map objects stay put).
    struct SurfacePoint {
        float fTop[4];
        unsigned char n;
        unsigned char notMoveable; ///< bit i: fTop[i] is a "not moveable" object's (flag 12)
    };

    CGroundTelegraphs() {}
    /// A cone or a strip toward the caster's aim; without one, the circle the server uses.
    static void Aim(Telegraph& t, CObjCHAR* pCaster);
    /// t's cone or strip pointed at aimX, aimY, if its skill has one and the spot is not at
    /// t's own position; else it stays a circle.
    static void PointAt(Telegraph& t, float aimX, float aimY);
    /// The area, a bright rim over its outline (and a cone's or strip's sides).
    void AddOutline(const Telegraph& t, unsigned long rgb, unsigned long alphaArea, unsigned long alphaRim);
    bool StillCasting(const Telegraph& t) const;
    /// The height a shape's point is drawn at (lifted): the terrain; or, for the player's
    /// aim and for a shape centred on a deck, the surface nearest its centre's height.
    float Height(const Telegraph& t, float fX, float fY);
    /// The blend of the surfaces chosen at the four lattice points round fX, fY. False when
    /// they are all the terrain (drape it as before) or one is not sampled yet.
    bool SurfaceHeight(const Telegraph& t, bool bRaised, float fX, float fY, float& fOut);
    /// The lattice point's object tops, sampled now if this frame's budget allows.
    bool SurfacePointAt(int iX, int iY, SurfacePoint& out);

    /// A grid of (nRows + 1) x (nCols + 1) points on the terrain, pos(fRow, fCol) giving
    /// each point's world x, y for fractions 0..1; its colour goes from c0 on row 0 to c1
    /// on the last row (ARGB).
    template <class PosFn>
    void AddGrid(const Telegraph& t, int nRows, int nCols, PosFn pos, unsigned long c0, unsigned long c1);
    /// Part of a disc around the caster: radii r0..r1 (colour c0 -> c1), angles a0..a1
    /// (radians, from the aim; a full circle is -pi..pi).
    void AddSector(const Telegraph& t, float r0, float r1, float a0, float a1, unsigned long c0, unsigned long c1);
    /// Part of a strip: from a0 to a1 along the aim (colour c0 -> c1), s0..s1 across it.
    void AddRect(const Telegraph& t, float a0, float a1, float s0, float s1, unsigned long c0, unsigned long c1);
    /// A band along the edge x0,y0 -> x1,y1, nx,ny pointing inward: cEdge on the edge,
    /// cInner fWidth inside.
    void AddEdge(const Telegraph& t, float x0, float y0, float x1, float y1, float nx, float ny, float fWidth,
        unsigned long cEdge, unsigned long cInner);
    /// The whole shape out to fReach along it (the fill), one colour.
    void AddArea(const Telegraph& t, float fFrom, float fReach, unsigned long c0, unsigned long c1);
    /// A cast's area; a fan (SKILL_FAN) as one strip per line.
    void AddTelegraph(const Telegraph& t, unsigned long now);
    void AddTelegraphShape(const Telegraph& t, unsigned long now);

    std::vector<Telegraph> m_List;
    std::vector<float> m_XYZ; // this frame's triangles
    std::vector<unsigned long> m_Colors;
    bool m_bSent = false; // the engine holds a non-empty overlay
    bool m_bPreview = false;
    Telegraph m_Preview; // the aimed area
    Telegraph m_PreviewRange; // the ring at the skill's range, around the player
    std::unordered_map<long long, SurfacePoint> m_Surfaces; // lattice point -> object tops
    int m_iSurfaceBudget = 0; // lattice points still allowed this frame (each is a few rays)
};
