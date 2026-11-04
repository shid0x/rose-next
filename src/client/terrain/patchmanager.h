#pragma once

#include "terrain/mapwindow.h"

#define PATCH_COUNT_PER_MAP_AXIS 16

/// Patches per axis across the largest map-cell window (5 cells x 16 = 80).
#define PATCH_COUNT_PER_WINDOW_AXIS (MAP_WINDOW_SPAN_MAX * PATCH_COUNT_PER_MAP_AXIS)

class CQuadPatchManager;
class CMAP_PATCH;
class CMAP;


class CPatchManager {
private:
    CMAP_PATCH* m_ppPATCH[PATCH_COUNT_PER_WINDOW_AXIS][PATCH_COUNT_PER_WINDOW_AXIS];

    CMAP_PATCH* m_ppSubPATCH[PATCH_COUNT_PER_MAP_AXIS * PATCH_COUNT_PER_MAP_AXIS * MAP_WINDOW_CELLS_MAX * 2];
    short m_nSubPATCH;

    CMAP_PATCH* m_CameraPATCH[PATCH_COUNT_PER_MAP_AXIS * PATCH_COUNT_PER_MAP_AXIS * 10];
    short m_nCameraPatch;

    CMAP_PATCH* m_PickPATCH[PATCH_COUNT_PER_MAP_AXIS * PATCH_COUNT_PER_MAP_AXIS * MAP_WINDOW_CELLS_MAX * 2];
    short m_nPickPATCH;

    WORD m_wViewFRAME;
    /// GetTickCount() for this frame's Update_VisiblePatchManager; every stamp
    /// and the eviction grace use it.
    DWORD m_dwViewTIME;
    float m_ViewFrsutumEq[6][4];

    std::vector<CMAP_PATCH*> patches;
    /// Scratch: first-time far frustum inserts over this frame's cap, as
    /// (squared distance to the camera, patch), so the cap goes nearest-first.
    std::vector<std::pair<float, CMAP_PATCH*>> m_DeferredFar;
    int patch_lod[31][31];

public:
    CPatchManager(void);
    ~CPatchManager(void);
    void Reset();

    CQuadPatchManager* m_ppQuadPatchManager[MAP_WINDOW_SPAN_MAX][MAP_WINDOW_SPAN_MAX];
    bool m_isUse[MAP_WINDOW_SPAN_MAX][MAP_WINDOW_SPAN_MAX];
    int m_nDrawingPatch;
    bool lod_onoff;

    void ResetViewFrame() { m_wViewFRAME = 1; }
    /// m_wLastViewFRAME == 0 means "not in the scene", so the frame counter must
    /// skip 0 when it wraps. (It used to land on 0 for one frame every 65,536:
    /// every patch stamped that frame looked cold and was pushed into the
    /// resident list a second time.)
    void IncreaseViewFrame() {
        m_wViewFRAME++;
        if (m_wViewFRAME == 0)
            m_wViewFRAME = 1;
    }

    void SetPATCH(int iX, int iY, CMAP_PATCH* pPATCH);

    void Update_VisiblePatch(short nMappingX, short nMappingY);
    void Insert_VisiblePatch(CMAP_PATCH* pPATCH);
    /// Marks an in-scene patch as seen this frame (frame and time).
    void Stamp(CMAP_PATCH* pPATCH);
    void Clear_VisiblePatch(void);
    void Delete_UnvisiblePatch(void);
    /// Takes a map cell's patches out of the resident set; call before the cell
    /// is freed. See the definition.
    void ForgetPatchesOf(CMAP_PATCH* pFirst, int iCount);

    float Pick_POSITION(D3DXVECTOR3& PosPICK);
    void ClearAllQuadPatchManager();
    void AddQuadPatchManager(CMAP* pMap, short y, short x);
    void CalculateViewFrustumCulling();
    void AddVisiblePatch(CQuadPatchManager* pQuadPatch);
    void Update_VisiblePatchManager(short nMappingX, short nMappingY);
    void SetPatchType();
    void ExecutePatchTpye();
    void InsertCameraPatch(CMAP_PATCH* pPatch);
    void TestDrawAABB();
    void AddPickPatch(CQuadPatchManager* pQuadPatch);
    void CalculatePickingPATCH();

    /// for mob collision test
    void DrawCollisionCylinder();

    /// Instrumentation: number of patches currently held in the in-scene set
    /// (frustum-visible + proximity keep-alive + within eviction grace).
    unsigned int GetResidentPatchCount() const { return (unsigned int)patches.size(); }

    /// Instrumentation: count patches in the proximity ring around
    /// (nMappingX, nMappingY) whose m_wLastViewFRAME == 0 — i.e. not yet
    /// warmed up. A non-zero value while the player is stationary means the
    /// 4/frame proximity budget is still catching up (typical post-warp /
    /// post-teleport symptom).
    unsigned int GetColdProximityCount(short nMappingX, short nMappingY) const;

    /// Instrumentation: number of patches the proximity keep-alive pass
    /// touched this frame (stamp + budgeted first-time insert). Should be
    /// ≈ non-null patches inside the kProximityRingRadius disc. Reset in
    /// CTERRAIN::SetCenterPosition alongside CMAP_PATCH::s_nInsertThisFrame.
    static uint32_t s_nRingStampsThisFrame;

    /// Instrumentation: patches selected by the quadtree frustum pass this
    /// frame (m_nSubPATCH after CalculateViewFrustumCulling). Lets the HUD
    /// distinguish the real frustum set from resPatch (which also includes
    /// proximity keep-alive + eviction-grace overhang).
    unsigned int GetFrustumPatchCount() const { return (unsigned int)m_nSubPATCH; }
};
