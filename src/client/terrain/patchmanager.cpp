#include "stdafx.h"

#include "terrain/patchmanager.h"

#include "game.h"
#include "ccamera.h"
#include "cclientstorage.h"
#include "system/FrameProfiler.h"

CPatchManager::CPatchManager(void) {
    ZeroMemory(m_ppPATCH, sizeof(m_ppPATCH));
    ZeroMemory(m_ppSubPATCH, sizeof(m_ppSubPATCH));
    ZeroMemory(m_CameraPATCH, sizeof(m_CameraPATCH));
    ZeroMemory(m_PickPATCH, sizeof(m_PickPATCH));
    ZeroMemory(m_ppQuadPatchManager, sizeof(m_ppQuadPatchManager));
    ZeroMemory(m_isUse, sizeof(m_isUse));
    m_nSubPATCH = 0;
    m_nCameraPatch = 0;
    m_nPickPATCH = 0;
    m_wViewFRAME = 0;
    m_nDrawingPatch = 0;

    lod_onoff = FALSE;
    CFileSystem* pFileSystem = (CVFSManager::GetSingleton()).GetFileSystem();

    if (pFileSystem->OpenFile("3DData\\Terrain\\LODType\\PatchType.lod", OPEN_READ_BIN)) {

        int i, j;
        char name[50];

        pFileSystem->ReadStringByNull(name);

        for (i = 0; i < 31; i += 1) {
            for (j = 0; j < 31; j += 1)
                pFileSystem->ReadInt32(&patch_lod[i][j]);
        }

        pFileSystem->CloseFile();
    }

    (CVFSManager::GetSingleton()).ReturnToManager(pFileSystem);
}

CPatchManager::~CPatchManager(void) {}

void
CPatchManager::Reset() {
    Clear_VisiblePatch();
    ClearAllQuadPatchManager();

    ZeroMemory(m_ppPATCH, sizeof(m_ppPATCH));
    ZeroMemory(m_ppSubPATCH, sizeof(m_ppSubPATCH));
    ZeroMemory(m_CameraPATCH, sizeof(m_CameraPATCH));
    ZeroMemory(m_PickPATCH, sizeof(m_PickPATCH));
    ZeroMemory(m_isUse, sizeof(m_isUse));

    m_nSubPATCH = 0;
    m_nCameraPatch = 0;
    m_nPickPATCH = 0;
    m_wViewFRAME = 0;
    m_nDrawingPatch = 0;
}

void
CPatchManager::SetPATCH(int iX, int iY, CMAP_PATCH* pPATCH) {
    m_ppPATCH[iY][iX] = pPATCH;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Visible patch리스트 갱신
//----------------------------------------------------------------------------------------------------

/// Free-look (right-click drag) rotates yaw + pitch together, which reshapes
/// the frustum faster than a yaw-only edge-pan. With purely frustum-based
/// eviction, whipping the camera back after the grace window forces a burst
/// of InsertToScene calls (each walks m_FixObjLIST + m_EffectLIST). Keep a
/// proximity ring around the player in-scene so rotation alone never evicts
/// nearby terrain. Budget first-time inserts per frame so teleport warm-up
/// cannot re-introduce the same burst we're preventing.
static const int kMaxProximityInsertsPerFrame = 4;

/// Proximity keep-alive radius (in patch units, via CRange lookup). Measured
/// frustum ground footprint in a wide-open zone is ~10.5 patches; 12 leaves
/// ~1.5 patches of margin to cover yaw+pitch camera flicks. Earlier value
/// was 22 which iterated 1513 ring points / frame and stamped 66% of the
/// 48x48 grid — the proximity stamp always kept m_wLastViewFRAME at the
/// current frame, so Delete_UnvisiblePatch's grace check never fired and
/// resident patches pinned at 2304 (FPS ~18 in Mount Eruca). Radius 12 yields
/// ~452 ring patches, letting frustum-only patches age out of the 30-frame
/// grace window in open zones.
static const int kProximityRingRadius = 12;

/// Per-frame ring-stamp counter (patches touched by Update_VisiblePatch).
/// Reset alongside CMAP_PATCH::s_nInsertThisFrame in CTERRAIN::SetCenterPosition.
/// Surfaced on the Scene: HUD line so the active ring size is observable.
uint32_t CPatchManager::s_nRingStampsThisFrame = 0;

void
CPatchManager::Update_VisiblePatch(short nMappingX, short nMappingY) {
    short nIndex, nY, nX;
    int remaining_inserts = kMaxProximityInsertsPerFrame;

    /// 실제로 엔진에서 사용될 패치들을 등록..
    for (nIndex = g_pCRange->GetStartIndex(0);
         nIndex < g_pCRange->GetStartIndex(kProximityRingRadius);
         nIndex++) {
        nX = nMappingX + g_pCRange->m_pPOINTS[nIndex].x;
        nY = nMappingY + g_pCRange->m_pPOINTS[nIndex].y;

        if (nX < 0 || nX >= PATCH_COUNT_PER_MAP_AXIS * 3 || nY < 0
            || nY >= PATCH_COUNT_PER_MAP_AXIS * 3)
            continue;

        CMAP_PATCH* pPATCH = m_ppPATCH[nY][nX];
        if (!pPATCH) {
            continue;
        }

        if (pPATCH->m_wLastViewFRAME != 0) {
            /// Already live — just refresh the frame stamp so the grace
            /// window does not expire. No InsertToScene work.
            pPATCH->m_wLastViewFRAME = this->m_wViewFRAME;
            pPATCH->PlaySOUND();
            ++s_nRingStampsThisFrame;
        } else if (remaining_inserts > 0) {
            /// Fresh insert — budget-gated so teleport / zone warp cannot
            /// land > kMaxProximityInsertsPerFrame new patches in one frame.
            this->Insert_VisiblePatch(pPATCH);
            pPATCH->PlaySOUND();
            --remaining_inserts;
            ++s_nRingStampsThisFrame;
        }
        /// Budget exhausted: skip this patch. Next frame will retry — still
        /// well within the 30-frame eviction grace, so nothing expires.
    }
}

unsigned int
CPatchManager::GetColdProximityCount(short nMappingX, short nMappingY) const {
    unsigned int cold = 0;
    for (short nIndex = g_pCRange->GetStartIndex(0);
         nIndex < g_pCRange->GetStartIndex(kProximityRingRadius);
         nIndex++) {
        short nX = nMappingX + g_pCRange->m_pPOINTS[nIndex].x;
        short nY = nMappingY + g_pCRange->m_pPOINTS[nIndex].y;

        if (nX < 0 || nX >= PATCH_COUNT_PER_MAP_AXIS * 3 || nY < 0
            || nY >= PATCH_COUNT_PER_MAP_AXIS * 3)
            continue;

        CMAP_PATCH* pPATCH = m_ppPATCH[nY][nX];
        if (!pPATCH)
            continue;

        if (pPATCH->m_wLastViewFRAME == 0)
            ++cold;
    }
    return cold;
}

void
CPatchManager::Insert_VisiblePatch(CMAP_PATCH* patch) {
    if (!patch) {
        return;
    }
    if (patch->m_wLastViewFRAME == 0) {
        patch->InsertToScene();
        this->patches.push_back(patch);
    }
    patch->m_wLastViewFRAME = this->m_wViewFRAME;
}

void
CPatchManager::Clear_VisiblePatch(void) {
    for (CMAP_PATCH* patch: this->patches) {
        if (!patch) {
            continue;
        }
        patch->m_PreDrawingType = -1;
        patch->RemoveFromScene();
    }

    this->patches.clear();
}

/// Patches leave the visible set the moment the camera pans away. Without a
/// grace window, curving back ~0.5 s later forces a fresh InsertToScene on
/// dozens of patches at once — visible as a hitch. Keep patches in-scene for
/// this many view-frames after they last appeared; uint16_t arithmetic
/// handles IncreaseViewFrame's wrap naturally. 30 frames (~0.5 s at 60 fps)
/// absorbs frustum/ring boundary oscillation during motion. The earlier
/// "2304 saturation" argument for a shorter grace no longer applies once the
/// proximity ring (kProximityRingRadius = 12) only covers ~452 patches.
static const uint16_t kPatchEvictGraceFrames = 30;

void
CPatchManager::Delete_UnvisiblePatch(void) {
    uint16_t view_frame = this->m_wViewFRAME;

    auto remove =
        std::remove_if(this->patches.begin(), this->patches.end(), [view_frame](CMAP_PATCH* patch) {
            if (!patch) {
                return true;
            }
            uint16_t age = (uint16_t)(view_frame - patch->m_wLastViewFRAME);
            if (age < kPatchEvictGraceFrames) {
                return false;
            }
            patch->m_PreDrawingType = -1;
            patch->RemoveFromScene();
            patch->m_wLastViewFRAME = 0;
            return true;
        });

    this->patches.erase(remove, this->patches.end());
    this->m_nDrawingPatch = this->patches.size();
}

float
CPatchManager::Pick_POSITION(D3DXVECTOR3& PosPICK) {
    float fMinDis = -1;
    float fCurDis = g_fMaxDistance;

    for (CMAP_PATCH* patch: this->patches) {
        if (patch->IsIntersect(fCurDis, PosPICK)) {
            fMinDis = fCurDis;
        }
    }

    return fMinDis;
}

void
CPatchManager::ClearAllQuadPatchManager() {

    int i, j;

    m_nSubPATCH = 0;
    m_nCameraPatch = 0;

    for (i = 0; i < 3; i += 1) {
        for (j = 0; j < 3; j += 1) {

            m_ppQuadPatchManager[i][j] = NULL;
        }
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief  PatchManager에 (y,x)위치의 *m_ppQuadPatchManager[y][x]를 참조
//----------------------------------------------------------------------------------------------------
void
CPatchManager::AddQuadPatchManager(CMAP* pMap, short y, short x) {

    m_ppQuadPatchManager[y][x] = pMap->GetQaudManager();
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief  참조된 각 *m_ppQuadPatchManager의 CMAP이 유용하면 ViewFrustumCulling을 해서
/// VisiblePatch를 얻음
//----------------------------------------------------------------------------------------------------
void
CPatchManager::CalculateViewFrustumCulling() {
    int i, j;
    HNODE camera_node;
    float buffer_viewFrustumEq[6][4], buffer_pos[3];
    float buffer_length, camera_length;
    camera_node = g_pCamera->GetZHANDLE();

    getCameraViewfrustum(camera_node, buffer_viewFrustumEq);
    getCameraEye(camera_node, buffer_pos);

    t_OptionVideo option;
    g_ClientStorage.GetVideoOption(option);
    //	g_ClientStorage.ApplyCameraOption( option.iCamera );

    buffer_length = buffer_viewFrustumEq[1][0] * buffer_pos[0]
        + buffer_viewFrustumEq[1][1] * buffer_pos[1] + buffer_viewFrustumEq[1][2] * buffer_pos[2]
        + buffer_viewFrustumEq[1][3];
    camera_length =
        CAMERA_MAX_RANGE(option.iCamera + ZONE_CAMERA_TYPE(g_pTerrain->GetZoneNO())) * 100.0f;
    buffer_viewFrustumEq[1][3] += (-buffer_length - camera_length);

    for (i = 0; i < 3; i += 1) {

        for (j = 0; j < 3; j += 1) {

            if (m_isUse[i][j]) {
                m_ppQuadPatchManager[i][j]->GetViewFrustumEq(buffer_viewFrustumEq);
                m_ppQuadPatchManager[i][j]->CalculateViewFrustumCulling();
                AddVisiblePatch(m_ppQuadPatchManager[i][j]);
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 참조된 각 *m_ppQuadPatchManager의 VisiblePatch를 CPatchManager에 합병합
//----------------------------------------------------------------------------------------------------
void
CPatchManager::AddVisiblePatch(CQuadPatchManager* pQuadPatch) {
    int i;

    for (i = 0; i < pQuadPatch->m_nPATCH; i += 1) {

        m_ppSubPATCH[m_nSubPATCH] = pQuadPatch->m_ppPATCH[i];
        m_nSubPATCH += 1;
    }

    for (i = 0; i < pQuadPatch->m_nExPATCH; i += 1) {

        m_ppSubPATCH[m_nSubPATCH] = pQuadPatch->m_ppExPATCH[i];
        m_nSubPATCH += 1;
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief VisiblePatch를 엔진에 등록
//----------------------------------------------------------------------------------------------------
/// Per-frame cap on *first-time* frustum inserts that are far from the camera.
///
/// Measured cause of the chunk-display hitch: a whole map chunk's 256 patches
/// entered the scene in one frame, each spawning a zz_mesh_terrain, and the next
/// frame's render force-built all 256 (procedural vertex generation +
/// CreateVertexBuffer + Lock/Unlock in D3DPOOL_DEFAULT) for 12-46 ms.
///
/// Measured lead time between a terrain mesh being queued and being force-loaded
/// was **1 frame**, every time -- so the engine's lazy entrance line can never
/// pre-load this work no matter how fast it drains. The only place left to
/// spread the cost is here, where the burst is created.
///
/// Already-resident patches are unaffected (Insert_VisiblePatch only does work
/// when m_wLastViewFRAME == 0; otherwise it just restamps), so this throttles
/// exclusively the newly-appearing set. 24/frame turns a 256-patch burst into
/// ~11 frames (~85 ms at 130 fps) of a few ms each.
/// Tunable via [VIDEO] TERRAIN_INSERTS_PER_FRAME (0 = uncapped/legacy). It is
/// effectively the "how big can a chunk-display spike get" dial: what actually
/// costs the frame is the *textures* an inserted batch drags in (~2.8 ms each),
/// not the terrain meshes (~0.1 ms each), so halving this roughly halves the
/// worst-case spike at the price of slower horizon fill-in.
static const int kMaxFarFrustumInsertsPerFrame = 24;

/// Patches nearer than this are never deferred -- deferring something at the
/// player's feet would be a visible hole rather than distant pop-in. A newly
/// loaded map cell is >= 160 m away by construction (it is the adjacent cell),
/// so the budget only ever bites on the far edge, where fog covers it.
/// World units: 1000 = 10 m, so this is 60 m.
static const float kNearInsertRadius = 6000.0f;

void
CPatchManager::Update_VisiblePatchManager(short nMappingX, short nMappingY) {
    short i;

    /// 실제로 엔진에서 사용될 패치들을 등록..

    /// Sub-pass timing for the frame-spike log. This function is the whole of
    /// logic[terr=], which was the largest single cost left after the motion,
    /// mipmap and mesh fixes. Inert unless [VIDEO] FRAME_SPIKE_LOG_MS is set.
    const bool bTimeTerr = FrameProfiler::IsSpikeLogEnabled();
    LARGE_INTEGER qpfT, t0, t1;
    if (bTimeTerr) {
        ::QueryPerformanceFrequency(&qpfT);
        ::QueryPerformanceCounter(&t0);
    }
    #define ZZ_NOTE_TERR(step)                                                                if (bTimeTerr) {                                                                          ::QueryPerformanceCounter(&t1);                                                       FrameProfiler::NoteTerrainStep(FrameProfiler::step,                                       (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)qpfT.QuadPart);            t0 = t1;                                                                          }

    CalculateViewFrustumCulling();
    ZZ_NOTE_TERR(TERRAIN_CULL);

    float camera_eye[3] = {0.0f, 0.0f, 0.0f};
    bool have_camera = false;
    if (g_pCamera) {
        HNODE camera_node = g_pCamera->GetZHANDLE();
        if (camera_node) {
            ::getCameraEye(camera_node, camera_eye);
            have_camera = true;
        }
    }
    const float near_radius_square = kNearInsertRadius * kNearInsertRadius;
    const int configured_budget = (int)g_ClientStorage.GetTerrainInsertsPerFrame();
    /// 0 means "uncapped" -- restores the original unbudgeted behaviour for A/B.
    const bool budget_inserts = (configured_budget > 0);
    int far_inserts_left = budget_inserts ? configured_budget : kMaxFarFrustumInsertsPerFrame;

    for (i = 0; i < m_nSubPATCH; i += 1) {
        CMAP_PATCH* pPATCH = m_ppSubPATCH[i];
        if (!pPATCH) {
            continue;
        }

        /// Already in-scene: restamping is free, never budget it.
        if (pPATCH->m_wLastViewFRAME != 0) {
            this->Insert_VisiblePatch(pPATCH);
            pPATCH->PlaySOUND();
            continue;
        }

        bool bNear = true;
        if (have_camera) {
            /// Measure to the patch's own terrain footprint, not to its object
            /// AABB. m_AABBMin starts at +1e8 and is only ever lowered by
            /// objects, so an object-less patch read as infinitely far; and now
            /// that the object AABB comes from real geometry, one large object
            /// drags the min corner hundreds of metres away from the patch we
            /// are actually deciding to insert. m_aabb is that 10m cell and is
            /// always valid.
            const float dx =
                (0.5f * (pPATCH->m_aabb.x[0] + pPATCH->m_aabb.x[1])) - camera_eye[0];
            const float dy =
                (0.5f * (pPATCH->m_aabb.y[0] + pPATCH->m_aabb.y[1])) - camera_eye[1];
            bNear = ((dx * dx + dy * dy) <= near_radius_square);
        }

        if (!bNear && budget_inserts) {
            if (far_inserts_left <= 0) {
                /// Deferred to a later frame. The patch stays outside the scene
                /// for now; it is still inside the frustum next frame, so it
                /// gets another chance immediately.
                continue;
            }
            --far_inserts_left;
        }

        this->Insert_VisiblePatch(pPATCH);
        pPATCH->PlaySOUND();
    }

    for (i = 0; i < m_nCameraPatch; i += 1) {

        this->Insert_VisiblePatch(m_CameraPATCH[i]);
        m_CameraPATCH[i]->PlaySOUND();
    }
    ZZ_NOTE_TERR(TERRAIN_INSERT);

    /// Proximity keep-alive — see Update_VisiblePatch comment. Runs after the
    /// frustum pass so already-visible patches are frame-stamped and skipped.
    Update_VisiblePatch(nMappingX, nMappingY);
    ZZ_NOTE_TERR(TERRAIN_PROXIMITY);

    SetPatchType();
    ExecutePatchTpye();
    ZZ_NOTE_TERR(TERRAIN_TYPE);

    Delete_UnvisiblePatch();
    ZZ_NOTE_TERR(TERRAIN_DELETE);

    #undef ZZ_NOTE_TERR
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief VisiblePatch의 Drawing Type을 정함
//----------------------------------------------------------------------------------------------------
void
CPatchManager::SetPatchType() {

    int i, buffer_x, buffer_y, patch_center[2];
    float position[3];
    HNODE camera_node;

    for (i = 0; i < m_nCameraPatch; i += 1) {

        m_CameraPATCH[i]->m_DrawingType = 0;
    }

    camera_node = g_pCamera->GetZHANDLE();
    getCameraEye(camera_node, position);

    patch_center[0] = (int)(position[0] / 1000.0f);
    patch_center[1] = (int)(position[1] / 1000.0f);

    for (i = 0; i < m_nSubPATCH; i += 1) {

        buffer_y = (int)(m_ppSubPATCH[i]->m_aabb.y[0] / 1000.0f) - patch_center[1] + 15;
        ;
        buffer_x = (int)(m_ppSubPATCH[i]->m_aabb.x[0] / 1000.0f) - patch_center[0] + 15;
        ;

        if (lod_onoff) {
            if (0 <= buffer_x && buffer_x < 31 && 0 <= buffer_y && buffer_y < 31)
                m_ppSubPATCH[i]->m_DrawingType = patch_lod[buffer_y][buffer_x];
            else
                m_ppSubPATCH[i]->m_DrawingType = 10;
        } else {
            m_ppSubPATCH[i]->m_DrawingType = 0;
        }
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief VisiblePatch의 Drawing Type을 엔진에 알려줌
//----------------------------------------------------------------------------------------------------
void
CPatchManager::ExecutePatchTpye() {

    int i;

    for (i = 0; i < m_nSubPATCH; i += 1) {

        if (m_ppSubPATCH[i]->m_PreDrawingType != m_ppSubPATCH[i]->m_DrawingType)

            ///::setTerrainBlockIndexOrder( m_ppSubPATCH[i]->GetVISIBLE(),
            /// m_ppSubPATCH[i]->m_DrawingType );

            m_ppSubPATCH[i]->m_PreDrawingType = m_ppSubPATCH[i]->m_DrawingType;
    }

    for (i = 0; i < m_nCameraPatch; i += 1) {

        if (m_CameraPATCH[i]->m_DrawingType == 0) {
            ///::setTerrainBlockIndexOrder( m_CameraPATCH[i]->GetVISIBLE(), 0);
            m_CameraPATCH[i]->m_PreDrawingType = 0;
        }
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief VisiblePatch중 카메라 주위의 Patch를 얻음
//----------------------------------------------------------------------------------------------------
void
CPatchManager::InsertCameraPatch(CMAP_PATCH* pPatch) {

    m_CameraPATCH[m_nCameraPatch] = pPatch;
    m_nCameraPatch += 1;
}

void
CPatchManager::TestDrawAABB() {

    int i, j, k;
    CMAP_PATCH* patch;
    float buffer_min[3], buffer_max[3];

    for (i = 0; i < 3; i += 1) {
        for (j = 0; j < 3; j += 1) {

            if (m_ppQuadPatchManager[i][j] != NULL) {
                for (k = 0; k < m_ppQuadPatchManager[i][j]->m_nExPATCH; k += 1) {

                    patch = m_ppQuadPatchManager[i][j]->m_ppExPATCH[k];
                    buffer_min[0] = patch->m_AABBMin.x;
                    buffer_min[1] = patch->m_AABBMin.y;
                    buffer_min[2] = patch->m_AABBMin.z;
                    buffer_max[0] = patch->m_AABBMax.x;
                    buffer_max[1] = patch->m_AABBMax.y;
                    buffer_max[2] = patch->m_AABBMax.z;

                    //	 drawAABB(buffer_min,buffer_max,D3DCOLOR_COLORVALUE(1.0f,1.0f,1.0f,1.0f));
                }
            }
        }
    }
}

void
CPatchManager::AddPickPatch(CQuadPatchManager* pQuadPatch) {
    int i;

    for (i = 0; i < pQuadPatch->m_nPickPATCH; i += 1) {

        m_PickPATCH[m_nPickPATCH] = pQuadPatch->m_ppPickPATCH[i];
        m_nPickPATCH += 1;
    }
}

void
CPatchManager::CalculatePickingPATCH() {

    int i, j;
    static float seta = 1.570796f, phi = 0.0f;
    static bool PositiveOnOff = true;
    float Position[3], Direction[3];
    float Position2[3];

    D3DXVECTOR3 Pos;
    if (g_pAVATAR != NULL) {

        Pos = g_pAVATAR->Get_CurPOS();
        Position[0] = Pos.x;
        Position[1] = Pos.y;
        Position[2] = Pos.z + 100.0f;

        Direction[0] = sinf(seta) * cosf(phi);
        Direction[1] = sinf(seta) * sinf(phi);
        Direction[2] = cosf(seta);

        Position2[0] = Position[0] + 100000.0f * Direction[0];
        Position2[1] = Position[1] + 100000.0f * Direction[1];
        Position2[2] = Position[2] + 100000.0f * Direction[2];

        ::ResetSceneLine();
        ::InputSceneLine(Position, Position2);

        phi += 3.141592f / 100.0f;
        if (phi > (3.141592f * 2.0f))
            phi = 0.0f;

        if (PositiveOnOff) {
            seta += 3.141592f / 1000.0f;
            if (seta > (3.141592f * 0.5f)) {
                seta = 3.141592f * 0.5f;
                PositiveOnOff = false;
            }
        } else {
            seta -= 3.141592f / 1000.0f;
            if (seta < (3.141592f * 0.35f)) {
                seta = 3.141592f * 0.35f;
                PositiveOnOff = true;
            }
        }

        m_nPickPATCH = 0;

        for (i = 0; i < 3; i += 1) {

            for (j = 0; j < 3; j += 1) {

                if (m_isUse[i][j]) {
                    m_ppQuadPatchManager[i][j]->GetPickingRay(Position, Direction);
                    m_ppQuadPatchManager[i][j]->CalculatePickingPATCH();
                    AddPickPatch(m_ppQuadPatchManager[i][j]);
                }
            }
        }

        float vMin[3], vMax[3];

        for (i = 0; i < m_nPickPATCH; i += 1) {
            vMin[0] = m_PickPATCH[i]->m_aabb.x[0];
            vMin[1] = m_PickPATCH[i]->m_aabb.y[0];
            vMin[2] = m_PickPATCH[i]->m_aabb.z[0];
            vMax[0] = m_PickPATCH[i]->m_aabb.x[1];
            vMax[1] = m_PickPATCH[i]->m_aabb.y[1];
            vMax[2] = m_PickPATCH[i]->m_aabb.z[1];

            ::InputSceneAABB(vMin, vMax, 0);
        }
    }
}

/// for mob collision test
void
CPatchManager::DrawCollisionCylinder() {
    for (CMAP_PATCH* patch: this->patches) {
        classDLLNODE<tagCYLINDERINFO>* pCurPatchNode = patch->m_CylinderLIST.GetHeadNode();
        while (pCurPatchNode) {
            InputSceneCylinder(pCurPatchNode->DATA.m_Position.x,
                pCurPatchNode->DATA.m_Position.y,
                pCurPatchNode->DATA.m_Position.z,
                1000.0,
                pCurPatchNode->DATA.m_fRadius);

            pCurPatchNode = patch->m_CylinderLIST.GetNextNode(pCurPatchNode);

            float vMin[3] = {0.0f, 0.0f, 0.0f};
            float vMax[3] = {0.0f, 0.0f, 0.0f};

            vMin[0] = patch->m_aabb.x[0];
            vMin[1] = patch->m_aabb.y[0];
            vMin[2] = patch->m_aabb.z[0];
            vMax[0] = patch->m_aabb.x[1];
            vMax[1] = patch->m_aabb.y[1];
            vMax[2] = patch->m_aabb.z[1];

            ::InputSceneAABB(vMin, vMax, 0);
        }
    }
}
