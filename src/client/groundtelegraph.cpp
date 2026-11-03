#include "stdafx.h"

#include "groundtelegraph.h"

#include "cobjchar.h"
#include "io_terrain.h"
#include "object.h"
#include "common/io_skill.h"
#include "zz_interface.h"

#include <math.h>

namespace {
const float PI = 3.14159265f;
const float LIFT_CM = 20.f; // over the terrain: GetHeight is bilinear, the mesh is triangles
const float SEGMENT_CM = 80.f; // rim length per segment
const float STEP_CM = 100.f; // spacing of the points the terrain is sampled on
// A shape whose centre stands this far over the terrain is on a deck or a platform.
const float FLAT_CM = 150.f;
// Map-object surfaces (Height): sampled on a 1 m world lattice, a surface counted when it is
// within this much above or below the shape's centre (the server's vertical band base is
// 5 m), a few lattice points a frame (each is a ray through every object there).
const float SURF_CELL_CM = 100.f;
const float SURF_UP_CM = 300.f;
const float SURF_DOWN_CM = 500.f;
const float SURF_RAY_CM = 100000.f; // the rays start this high
const int SURF_POINTS_PER_FRAME = 40;
const size_t SURF_CACHE_MAX = 40000;
const int MIN_SEGMENTS = 4;
const int MIN_CIRCLE_SEGMENTS = 24;
const int MAX_SEGMENTS = 96;
const unsigned long WAIT_MS = 3000; // past the clip, waiting for the server to fire it
const unsigned long FLASH_MS = 350; // the fired flash's fade
const unsigned long DEFAULT_CAST_MS = 1000; // a cast with no clip

const unsigned long RGB_AREA = 0xFF4020; // red-orange
const unsigned long RGB_FILL = 0xFF6A20;
const unsigned long RGB_FLASH = 0xFF9A50;
const unsigned long ALPHA_AREA = 0x30;
const unsigned long ALPHA_RIM = 0xA0; // over the area: ~0xB0 at the edge
const unsigned long ALPHA_FILL = 0x38;
const unsigned long ALPHA_FILL_EDGE = 0x90;
const float RIM_CM = 60.f;
const float EDGE_CM = 40.f;

// the player's own aim (SetPreview): blue-white, never mistaken for a boss's red
const unsigned long RGB_AIM = 0x70C8FF;
const unsigned long ALPHA_AIM_AREA = 0x28;
const unsigned long ALPHA_AIM_RIM = 0xA0;
const unsigned long ALPHA_RANGE = 0x50; // the ring at the skill's range
const float RANGE_RING_CM = 30.f;

unsigned long
Argb(unsigned long alpha, unsigned long rgb) {
    return (alpha << 24) | (rgb & 0xFFFFFF);
}

unsigned long
Lerp(unsigned long c0, unsigned long c1, float f) {
    unsigned long out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float a = (float)((c0 >> shift) & 0xFF);
        const float b = (float)((c1 >> shift) & 0xFF);
        out |= ((unsigned long)(a + (b - a) * f + 0.5f) & 0xFF) << shift;
    }
    return out;
}

int
Steps(float fLength, float fStep, int iMin, int iMax) {
    int n = (int)ceilf(fLength / fStep);
    if (n < iMin)
        n = iMin;
    if (n > iMax)
        n = iMax;
    return n;
}
} // namespace

CGroundTelegraphs&
CGroundTelegraphs::Instance() {
    static CGroundTelegraphs s_Instance;
    return s_Instance;
}

void
CGroundTelegraphs::OnCastStart(CObjCHAR* pCaster, short nSkillIDX, unsigned long dwCastMS) {
    if (!pCaster || nSkillIDX <= 0)
        return;
    const float fLength = (float)SKILL_SCOPE_RADIUS(nSkillIDX);
    if (fLength <= 0.f)
        return;

    Telegraph t;
    t.nObjIdx = pCaster->Get_INDEX();
    t.pCaster = pCaster;
    t.nSkillIDX = nSkillIDX;
    t.shape = SHAPE_CIRCLE;
    t.fX = pCaster->Get_CurPOS().x;
    t.fY = pCaster->Get_CurPOS().y;
    t.fZ = pCaster->Get_CurPOS().z;
    t.fDirX = 1.f;
    t.fDirY = 0.f;
    t.fLength = fLength;
    t.fHalfAngle = PI;
    t.fHalfWidth = 0.f;
    t.dwStart = ::GetTickCount();
    t.dwCastMS = dwCastMS ? dwCastMS : DEFAULT_CAST_MS;
    t.dwFiredAt = 0;
    t.bSurfaces = false;

    Aim(t, pCaster);

    for (Telegraph& old: m_List) {
        if (old.nObjIdx == t.nObjIdx) { // a new cast replaces the caster's last one
            old = t;
            return;
        }
    }
    m_List.push_back(t);
}

void
CGroundTelegraphs::Aim(Telegraph& t, CObjCHAR* pCaster) {
    // A cone or a strip points where the server aimed it; without an aim (or aimed at the
    // caster's own feet) the server measures a circle of its length, and so do we.
    if (pCaster->m_bSkillAimActive)
        PointAt(t, pCaster->m_PosSkillAIM.x, pCaster->m_PosSkillAIM.y);
}

void
CGroundTelegraphs::PointAt(Telegraph& t, float aimX, float aimY) {
    const short nSkillIDX = t.nSkillIDX;
    if (!SKILL_SCOPE_AIMED(nSkillIDX))
        return;
    const float dx = aimX - t.fX;
    const float dy = aimY - t.fY;
    const float fAim = sqrtf(dx * dx + dy * dy);
    const int iAngle = SKILL_SCOPE_ANGLE(nSkillIDX);
    if (fAim < 1.f || (SKILL_SCOPE_CONE(nSkillIDX) && iAngle >= 360))
        return;
    t.fDirX = dx / fAim;
    t.fDirY = dy / fAim;
    if (SKILL_SCOPE_CONE(nSkillIDX)) {
        t.shape = SHAPE_CONE;
        t.fHalfAngle = iAngle * 0.5f * PI / 180.f;
    } else {
        t.shape = SHAPE_STRIP;
        t.fHalfWidth = SKILL_SCOPE_WIDTH(nSkillIDX) * 0.5f;
    }
}

void
CGroundTelegraphs::OnAimArrived(CObjCHAR* pCaster) {
    for (Telegraph& t: m_List) {
        if (t.pCaster == pCaster && t.nObjIdx == pCaster->Get_INDEX() && !t.dwFiredAt
            && t.nSkillIDX == pCaster->m_nActiveSkillIDX) {
            Aim(t, pCaster);
            return;
        }
    }
}

bool
CGroundTelegraphs::StillCasting(const Telegraph& t) const {
    // Casting_END clears m_nActiveSkillIDX for every way a cast ends: the action
    // starting, a stop, a stun or sleep, death, a cancel. A cast replaced without it
    // (SetCMD_MOVE) is caught by the time limit.
    return t.pCaster->m_nActiveSkillIDX == t.nSkillIDX && t.pCaster->Get_HP() > 0
           && ::GetTickCount() - t.dwStart < t.dwCastMS + WAIT_MS;
}

void
CGroundTelegraphs::Update() {
    const unsigned long now = ::GetTickCount();
    m_XYZ.clear();
    m_Colors.clear();
    m_iSurfaceBudget = SURF_POINTS_PER_FRAME;

    for (size_t i = 0; i < m_List.size();) {
        Telegraph& t = m_List[i];
        const bool bAlive = g_pObjMGR && g_pObjMGR->Get_OBJECT(t.nObjIdx) == t.pCaster;
        bool bKeep = true;
        if (t.dwFiredAt) {
            bKeep = now - t.dwFiredAt < FLASH_MS;
        } else if (!bAlive) {
            bKeep = false;
        } else if (!this->StillCasting(t)) {
            // Fired: the action took over (ProcSkillAction sets m_nDoingSkillIDX in the
            // frame Casting_END runs). Anything else was an interruption.
            if (t.pCaster->m_nDoingSkillIDX == t.nSkillIDX) {
                t.dwFiredAt = now;
            } else {
                bKeep = false;
            }
        }
        if (!bKeep) {
            m_List.erase(m_List.begin() + i);
            continue;
        }
        this->AddTelegraph(t, now);
        ++i;
    }

    if (m_bPreview) {
        // the player's own aim: a faint ring at the range, the area in the friendly colour
        const float R = m_PreviewRange.fLength;
        this->AddSector(m_PreviewRange, R - RANGE_RING_CM, R, -PI, PI, Argb(0, RGB_AIM),
            Argb(ALPHA_RANGE, RGB_AIM));
        this->AddOutline(m_Preview, RGB_AIM, ALPHA_AIM_AREA, ALPHA_AIM_RIM);
    }

    if (!m_XYZ.empty()) {
        ::setGroundOverlay(&m_XYZ[0], &m_Colors[0], (int)m_Colors.size());
        m_bSent = true;
    } else if (m_bSent) {
        ::setGroundOverlay(NULL, NULL, 0);
        m_bSent = false;
    }
}

void
CGroundTelegraphs::Forget() {
    m_List.clear();
    m_Surfaces.clear();
    if (m_bSent) {
        ::setGroundOverlay(NULL, NULL, 0);
        m_bSent = false;
    }
}

void
CGroundTelegraphs::SetPreview(const D3DXVECTOR3& posCaster, const D3DXVECTOR3& posAim, short nSkillIDX) {
    // A new aim samples the map objects afresh: the cache may hold points sampled before
    // the objects there had streamed in.
    if (!m_bPreview)
        m_Surfaces.clear();

    Telegraph t = {};
    t.nObjIdx = -1;
    t.pCaster = NULL;
    t.nSkillIDX = nSkillIDX;
    t.bSurfaces = true;
    t.shape = SHAPE_CIRCLE;
    t.fDirX = 1.f;
    t.fDirY = 0.f;
    t.fHalfAngle = PI;
    t.fLength = (float)SKILL_SCOPE_RADIUS(nSkillIDX);
    if (SKILL_SCOPE_AIMED(nSkillIDX)) {
        // a strip or cone starts at the caster and points at the aim
        t.fX = posCaster.x;
        t.fY = posCaster.y;
        t.fZ = posCaster.z;
        PointAt(t, posAim.x, posAim.y);
    } else {
        // a circle lands on the aim
        t.fX = posAim.x;
        t.fY = posAim.y;
        t.fZ = posAim.z;
    }
    m_Preview = t;

    Telegraph r = t;
    r.shape = SHAPE_CIRCLE;
    r.fX = posCaster.x;
    r.fY = posCaster.y;
    r.fZ = posCaster.z;
    r.fDirX = 1.f;
    r.fDirY = 0.f;
    r.fLength = (float)SKILL_DISTANCE(nSkillIDX);
    m_PreviewRange = r;
    m_bPreview = t.fLength > 0.f && r.fLength > 0.f;
}

bool
CGroundTelegraphs::IsNotMoveableNode(unsigned int hNode) {
    return hNode && ZZ_IS_NOTMOVEABLE(::getCollisionLevel(hNode));
}

bool
CGroundTelegraphs::IsWalkableNode(unsigned int hNode, bool bNotMoveable) {
    const HNODE h = hNode;
    if (!h || ::isA(h, ZZ_TYPE_TERRAIN) || ::isA(h, ZZ_TYPE_OCEAN) || ::isA(h, ZZ_TYPE_MODEL)
        || ::isDescendantOfType(h, ZZ_TYPE_MODEL))
        return false;
    const int level = ::getCollisionLevel(h);
    if ((level & 0x7) == ZZ_CL_NONE || (ZZ_IS_NOTMOVEABLE(level) && !bNotMoveable))
        return false;
    if (HNODE ud = ::getUserData(h)) {
        if (reinterpret_cast<CGameOBJ*>(ud)->IsA(OBJ_ITEM))
            return false;
    }
    return true;
}

// The terrain alone drew a shape aimed on a bridge deck on the ground far below, and drew
// any shape under the planks of a platform. The player's aim, and a shape whose centre
// stands on a deck, take the surface nearest the centre's height among the terrain and
// the walkable map objects; anything else drapes over the terrain as before.
float
CGroundTelegraphs::Height(const Telegraph& t, float fX, float fY) {
    bool bNotFound = false;
    const float fTerrain = g_pTerrain ? g_pTerrain->GetHeight(fX, fY, &bNotFound) : t.fZ;
    if (!g_pTerrain || bNotFound)
        return t.fZ + LIFT_CM;
    bool bNF = false;
    const float fCentre = g_pTerrain->GetHeight(t.fX, t.fY, &bNF);
    const bool bRaised = bNF || t.fZ > fCentre + FLAT_CM; // centred on a deck or a platform
    float fZ;
    if ((t.bSurfaces || bRaised) && this->SurfaceHeight(t, bRaised, fX, fY, fZ))
        return fZ + LIFT_CM;
    // the terrain; on a deck not sampled yet, flat at the centre's height away from it
    if (bRaised && fabsf(fTerrain - t.fZ) > FLAT_CM)
        return t.fZ + LIFT_CM;
    return fTerrain + LIFT_CM;
}

bool
CGroundTelegraphs::SurfaceHeight(const Telegraph& t, bool bRaised, float fX, float fY, float& fOut) {
    const float gx = fX / SURF_CELL_CM, gy = fY / SURF_CELL_CM;
    const int ix = (int)floorf(gx), iy = (int)floorf(gy);
    const float fx = gx - ix, fy = gy - iy;
    float h[4];
    bool bObject = false;
    for (int k = 0; k < 4; ++k) {
        const int px = ix + (k & 1), py = iy + (k >> 1);
        SurfacePoint p;
        if (!this->SurfacePointAt(px, py, p))
            return false;
        bool bNF = false;
        const float fTerrain = g_pTerrain->GetHeight(px * SURF_CELL_CM, py * SURF_CELL_CM, &bNF);
        // the surface nearest the centre's height, within reach of it
        float fBest = 0.f, fBestD = 1.0e30f;
        bool bTop = false;
        if (!bNF && fTerrain <= t.fZ + SURF_UP_CM && fTerrain >= t.fZ - SURF_DOWN_CM) {
            fBest = fTerrain;
            fBestD = fabsf(fTerrain - t.fZ);
        }
        for (int i = 0; i < p.n; ++i) {
            // a "not moveable" top (a rope bridge, but also a tree crown) only for a shape that
            // stands on an object itself
            if (((p.notMoveable >> i) & 1) && !bRaised)
                continue;
            const float z = p.fTop[i];
            const float d = fabsf(z - t.fZ);
            if (z <= t.fZ + SURF_UP_CM && z >= t.fZ - SURF_DOWN_CM && d < fBestD) {
                fBest = z;
                fBestD = d;
                bTop = true;
            }
        }
        if (fBestD > 1.0e29f) {
            // nothing in reach: past a deck's edge the shape hangs flat; on the ground it
            // stays on the terrain
            if (bRaised) {
                fBest = t.fZ;
                bTop = true;
            } else {
                fBest = bNF ? t.fZ : fTerrain;
            }
        }
        h[k] = fBest;
        bObject = bObject || bTop;
    }
    if (!bObject)
        return false;
    fOut = (h[0] * (1.f - fx) + h[1] * fx) * (1.f - fy) + (h[2] * (1.f - fx) + h[3] * fx) * fy;
    return true;
}

bool
CGroundTelegraphs::SurfacePointAt(int iX, int iY, SurfacePoint& out) {
    const long long key = ((long long)iX << 32) | (unsigned int)iY;
    const std::unordered_map<long long, SurfacePoint>::const_iterator it = m_Surfaces.find(key);
    if (it != m_Surfaces.end()) {
        out = it->second;
        return true;
    }
    if (m_iSurfaceBudget <= 0)
        return false;
    --m_iSurfaceBudget;
    if (m_Surfaces.size() >= SURF_CACHE_MAX)
        m_Surfaces.clear();

    SurfacePoint p = {};
    const float x = iX * SURF_CELL_CM, y = iY * SURF_CELL_CM;
    float vMin[3] = {x - 1.f, y - 1.f, -SURF_RAY_CM};
    float vMax[3] = {x + 1.f, y + 1.f, SURF_RAY_CM};
    const int n = ::collectByMinMaxVec3(vMin, vMax);
    for (int i = 0; i < n && p.n < 4; ++i) {
        const HNODE h = ::getCollectNode(i);
        if (!IsWalkableNode(h, true))
            continue;
        float cx, cy, cz, dist;
        if (::intersectRay(h, x, y, SURF_RAY_CM, 0.f, 0.f, -1.f, &cx, &cy, &cz, &dist)) {
            if (IsNotMoveableNode(h))
                p.notMoveable |= (unsigned char)(1u << p.n);
            p.fTop[p.n++] = cz;
        }
    }
    m_Surfaces[key] = p;
    out = p;
    return true;
}

template <class PosFn>
void
CGroundTelegraphs::AddGrid(const Telegraph& t, int nRows, int nCols, PosFn pos, unsigned long c0, unsigned long c1) {
    // The points, once each, the terrain sampled at every one.
    static std::vector<float> s_Grid; // x, y, z
    const int nStride = nCols + 1;
    s_Grid.resize((size_t)(nRows + 1) * nStride * 3);
    for (int r = 0; r <= nRows; ++r) {
        for (int c = 0; c <= nCols; ++c) {
            float* p = &s_Grid[((size_t)r * nStride + c) * 3];
            pos((float)r / nRows, (float)c / nCols, p[0], p[1]);
            p[2] = this->Height(t, p[0], p[1]);
        }
    }

    for (int r = 0; r < nRows; ++r) {
        const unsigned long cA = Lerp(c0, c1, (float)r / nRows);
        const unsigned long cB = Lerp(c0, c1, (float)(r + 1) / nRows);
        for (int c = 0; c < nCols; ++c) {
            const float* a = &s_Grid[((size_t)r * nStride + c) * 3];
            const float* b = &s_Grid[((size_t)r * nStride + c + 1) * 3];
            const float* d = &s_Grid[((size_t)(r + 1) * nStride + c) * 3];
            const float* e = &s_Grid[((size_t)(r + 1) * nStride + c + 1) * 3];
            const float* tri[6] = {a, d, e, a, e, b};
            const unsigned long col[6] = {cA, cB, cB, cA, cB, cA};
            for (int v = 0; v < 6; ++v) {
                m_XYZ.push_back(tri[v][0]);
                m_XYZ.push_back(tri[v][1]);
                m_XYZ.push_back(tri[v][2]);
                m_Colors.push_back(col[v]);
            }
        }
    }
}

void
CGroundTelegraphs::AddSector(const Telegraph& t, float r0, float r1, float a0, float a1, unsigned long c0,
    unsigned long c1) {
    if (r0 < 0.f)
        r0 = 0.f;
    if (r1 <= r0 || a1 <= a0)
        return;
    const bool bFull = (a1 - a0) >= 2.f * PI - 0.001f;
    const int nRows = Steps(r1 - r0, STEP_CM, 1, 1000);
    const int nCols =
        Steps((a1 - a0) * t.fLength, SEGMENT_CM, bFull ? MIN_CIRCLE_SEGMENTS : MIN_SEGMENTS, MAX_SEGMENTS);
    const float fBase = atan2f(t.fDirY, t.fDirX);
    this->AddGrid(
        t, nRows, nCols,
        [&](float fr, float fc, float& x, float& y) {
            const float r = r0 + (r1 - r0) * fr;
            const float a = fBase + a0 + (a1 - a0) * fc;
            x = t.fX + r * cosf(a);
            y = t.fY + r * sinf(a);
        },
        c0, c1);
}

void
CGroundTelegraphs::AddRect(const Telegraph& t, float a0, float a1, float s0, float s1, unsigned long c0,
    unsigned long c1) {
    if (a0 < 0.f)
        a0 = 0.f;
    if (a1 <= a0 || s1 <= s0)
        return;
    const int nRows = Steps(a1 - a0, STEP_CM, 1, 1000);
    const int nCols = Steps(s1 - s0, STEP_CM, 1, 1000);
    this->AddGrid(
        t, nRows, nCols,
        [&](float fr, float fc, float& x, float& y) {
            const float a = a0 + (a1 - a0) * fr;
            const float s = s0 + (s1 - s0) * fc;
            x = t.fX + t.fDirX * a - t.fDirY * s;
            y = t.fY + t.fDirY * a + t.fDirX * s;
        },
        c0, c1);
}

void
CGroundTelegraphs::AddEdge(const Telegraph& t, float x0, float y0, float x1, float y1, float nx, float ny,
    float fWidth, unsigned long cEdge, unsigned long cInner) {
    const float fLen = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    if (fLen < 1.f)
        return;
    this->AddGrid(
        t, 1, Steps(fLen, STEP_CM, 1, 1000),
        [&](float fr, float fc, float& x, float& y) {
            x = x0 + (x1 - x0) * fc + nx * fWidth * fr;
            y = y0 + (y1 - y0) * fc + ny * fWidth * fr;
        },
        cEdge, cInner);
}

void
CGroundTelegraphs::AddArea(const Telegraph& t, float fFrom, float fReach, unsigned long c0, unsigned long c1) {
    switch (t.shape) {
        case SHAPE_CIRCLE:
            this->AddSector(t, fFrom, fReach, -PI, PI, c0, c1);
            break;
        case SHAPE_CONE:
            this->AddSector(t, fFrom, fReach, -t.fHalfAngle, t.fHalfAngle, c0, c1);
            break;
        case SHAPE_STRIP:
            this->AddRect(t, fFrom, fReach, -t.fHalfWidth, t.fHalfWidth, c0, c1);
            break;
    }
}

void
CGroundTelegraphs::AddOutline(const Telegraph& t, unsigned long rgb, unsigned long alphaArea,
    unsigned long alphaRim) {
    const float L = t.fLength;
    this->AddArea(t, 0.f, L, Argb(alphaArea, rgb), Argb(alphaArea, rgb));
    this->AddArea(t, L - RIM_CM, L, Argb(0, rgb), Argb(alphaRim, rgb));
    if (t.shape == SHAPE_CONE) {
        // the two sides, from the point to the arc; inward = toward the aim
        for (int side = -1; side <= 1; side += 2) {
            const float a = atan2f(t.fDirY, t.fDirX) + side * t.fHalfAngle;
            const float ex = cosf(a), ey = sinf(a);
            this->AddEdge(t, t.fX, t.fY, t.fX + ex * L, t.fY + ey * L, side * ey, -side * ex, RIM_CM,
                Argb(alphaRim, rgb), Argb(0, rgb));
        }
    } else if (t.shape == SHAPE_STRIP) {
        for (int side = -1; side <= 1; side += 2) {
            const float px = -t.fDirY * side * t.fHalfWidth, py = t.fDirX * side * t.fHalfWidth;
            this->AddEdge(t, t.fX + px, t.fY + py, t.fX + px + t.fDirX * L, t.fY + py + t.fDirY * L,
                t.fDirY * side, -t.fDirX * side, RIM_CM, Argb(alphaRim, rgb), Argb(0, rgb));
        }
    }
}

void
CGroundTelegraphs::AddTelegraph(const Telegraph& t, unsigned long now) {
    // A fan (SKILL_FAN, 2026-11-03): the strip once per line, turned round the caster, the
    // middle one on the aim -- as the server measures it.
    const int nLines = t.shape == SHAPE_STRIP ? SKILL_FAN_LINES(t.nSkillIDX) : 0;
    if (nLines < 2) {
        this->AddTelegraphShape(t, now);
        return;
    }
    const float fStep = SKILL_FAN_DEGREES(t.nSkillIDX) * PI / 180.f;
    for (int i = 0; i < nLines; ++i) {
        const float fTurn = (i - (nLines - 1) * 0.5f) * fStep;
        const float c = cosf(fTurn), s = sinf(fTurn);
        Telegraph line = t;
        line.fDirX = t.fDirX * c - t.fDirY * s;
        line.fDirY = t.fDirX * s + t.fDirY * c;
        this->AddTelegraphShape(line, now);
    }
}

void
CGroundTelegraphs::AddTelegraphShape(const Telegraph& t, unsigned long now) {
    const float L = t.fLength;

    if (t.dwFiredAt) { // the hit: a flash that fades
        const float f = 1.f - (float)(now - t.dwFiredAt) / FLASH_MS;
        const unsigned long alpha = (unsigned long)(0x90 * (f > 0.f ? f : 0.f));
        this->AddArea(t, 0.f, L, Argb(alpha, RGB_FLASH), Argb(alpha, RGB_FLASH));
        return;
    }

    float p = (float)(now - t.dwStart) / t.dwCastMS;
    if (p > 1.f)
        p = 1.f;

    this->AddOutline(t, RGB_AREA, ALPHA_AREA, ALPHA_RIM);

    // The fill: grows from the caster to the rim over the cast, a brighter leading edge.
    const float fFill = L * p;
    this->AddArea(t, 0.f, fFill - EDGE_CM, Argb(ALPHA_FILL, RGB_FILL), Argb(ALPHA_FILL, RGB_FILL));
    this->AddArea(t, fFill - EDGE_CM, fFill, Argb(ALPHA_FILL, RGB_FILL), Argb(ALPHA_FILL_EDGE, RGB_FILL));
}
