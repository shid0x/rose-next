#ifndef __IO_TERRAIN_H
#define __IO_TERRAIN_H

#include "IO_Basic.h"
#include "IO_Material.h"
#include "IO_Effect.h"

#include "ceconomy.h"
#include "object.h"
#include "common/cregenarea.h"
#include "rose/io/stb.h"
#include "sound/io_sound.h"

#include "terrain/patchmanager.h"
#include "terrain/terraindef.h"
#include "util/cfileSystem.h"

#include <list>

class CQuadPatchManager;
class CMAP_PATCH;
class CMAP;
class CTERRAIN;

//----------------------------------------------------------------------------------------------------
/// struct tagMAPSND
//----------------------------------------------------------------------------------------------------

struct tagMAPSND {
    t_HASHKEY m_HashKEY;
    D3DVECTOR m_Position;
    DWORD m_dwLastPlayTIME;
    unsigned int m_uiInterval;
    unsigned int m_uiRange;
};

//----------------------------------------------------------------------------------------------------
/// struct tagMAPEFT
//----------------------------------------------------------------------------------------------------

struct tagMAPEFT {
    //	CStrVAR			m_EffectFile;
    t_HASHKEY m_HashKEY;
    CEffect* m_pEffect;
    D3DXVECTOR3 m_Positon;
};

//----------------------------------------------------------------------------------------------------
/// Cylinder information for mob collision
//----------------------------------------------------------------------------------------------------

struct tagCYLINDERINFO {
    D3DXVECTOR3 m_Position;
    float m_fRadius;
};

struct aabbBox {

    float x[2];
    float y[2];
    float z[2];
};

struct QuadPatch {

    short level;
    short index_x[2];
    short index_y[2];
    short iquad_size;
    float fquad_size;
    float nQuadXFromW, nQuadYFromW;
    int nQuadXFromMap, nQuadYFromMap;
    aabbBox aabb;
};

//----------------------------------------------------------------------------------------------------
///
/// class CQuadPatchManager
///
//----------------------------------------------------------------------------------------------------

struct PatchIndex {

    int x, y;
};

class CQuadPatchManager {

public:
    CMAP* m_pMap;

    QuadPatch m_quadPATCH[85];
    classDLLIST<PatchIndex> m_ExPatchList[86];
    CMAP_PATCH* m_ppPATCH[PATCH_COUNT_PER_MAP_AXIS * PATCH_COUNT_PER_MAP_AXIS];
    CMAP_PATCH* m_ppExPATCH[PATCH_COUNT_PER_MAP_AXIS * PATCH_COUNT_PER_MAP_AXIS];
    CMAP_PATCH* m_ppPickPATCH[PATCH_COUNT_PER_MAP_AXIS * PATCH_COUNT_PER_MAP_AXIS];

    short m_nPATCH, m_nExPATCH, m_nPickPATCH;

    float m_ViewFrsutumEq[6][4];

    float m_RayPosition[3];
    float m_RayDirection[3];

    CQuadPatchManager(void);
    ~CQuadPatchManager(void);
    void InitializeQuadPatch();
    void InputQuadPatchElement(short index, short level, int iquad_size, short nMapX, short nMapY);
    void LoadQuadPatch(CMAP* pMap);
    void CalculateViewFrustumCulling();
    void CalculateQuadPatchCulling(short index);
    void InsertMemoQuadPatch(short index);
    void CalculateBottomNode(short index);
    short ViewCullingFunc(aabbBox* box);
    short ViewCullingFunc2(D3DVECTOR* min, D3DVECTOR* max);
    void SetExPatch(int index, CMAP_PATCH* pPatch, int patch_x, int patch_y);
    void SetExPatchBottom(int index);
    void SetExPatchTotal();
    void InsertMemoQuadExPatch(short index);
    void InsertMemoQuadExPatch2(short index);
    void CalculateQaudExPatchCullingEx();
    void GetViewFrustumEq(float viewEquation[6][4]);
    void CalculateRayCulling(short index);
    bool IntersectRayOnOff(aabbBox* box);
    void CalculateRayCullingBottomNode(short index);
    void CalculatePickingPATCH();
    void GetPickingRay(float Position[3], float Direction[3]);
};

//----------------------------------------------------------------------------------------------------
///
/// class CMAP_PATCH
///
//----------------------------------------------------------------------------------------------------

class CMAP_PATCH {
public:
    /// Per-frame scene-visibility toggle counters for streaming instrumentation.
    /// Reset at the end of each frame (see CTERRAIN::SetCenterPosition).
    /// Walking m_FixObjLIST/m_EffectLIST during InsertToScene is the dominant
    /// cost of patch visibility toggles, so a non-zero count on a stable frame
    /// means terrain is churning.
    static unsigned int s_nInsertThisFrame;
    static unsigned int s_nRemoveThisFrame;

private:
    static float m_fHEIGHT[MAX_VERTEX_COUNT_PER_PATCH_AXIS * MAX_VERTEX_COUNT_PER_PATCH_AXIS];

    HNODE m_hTerrainBlock;

    short m_nTileNO;
    short m_nDnTileTEX;
    short m_nUpTileTEX;

    CMAP* m_pMAP;

    bool m_bIsRegistered;
    bool m_bIsVisible;

    bool Register(short nWorldX, short nWorldY);

public:
    classDLLIST<CGameOBJ*> m_FixObjLIST;
    classDLLIST<tagMAPSND> m_SoundLIST;
    classDLLIST<tagMAPEFT> m_EffectLIST;
    classDLLIST<tagCYLINDERINFO> m_CylinderLIST;

    short m_nPatchXFromMAP;
    short m_nPatchYFromMAP;

    /// AABB
    D3DVECTOR m_AABBMin;
    D3DVECTOR m_AABBMax;

    CMAP_PATCH();

    void InitMember();

    HNODE GetVISIBLE() { return m_hTerrainBlock; }
    CMAP* GetParentMAP(void) { return m_pMAP; }

    void SetMATERIAL(short nTileNO);
    void SetPOSITION(CMAP* pMAP, short nPatchXFromMap, short nPatchYFromMap);
    bool RegisterToNZIN(void);
    bool UnregisterFromNZIN(void);

    WORD m_wLastViewFRAME;

    void InsertToScene();
    void RemoveFromScene();

    bool IsIntersect(float& fCurMinDist, D3DXVECTOR3& PosOUT);
    bool IsIntersect_test(float& fCurMinDist,
        D3DXVECTOR3& PosOUT,
        float Position[3],
        float Direction[3]);
    void PlaySOUND();
    bool
    AddObject(CGameOBJ* pObject, D3DVECTOR& Position, D3DXQUATERNION& Rotate, D3DVECTOR& Scale);
    //	float	GetHEIGHT (float fWorldX, float fWorldY, HNODE hCheckNODE);

    int GetUpTileNO() { return m_nUpTileTEX; }

    aabbBox m_aabb;
    int m_DrawingType; //  추가
    int m_PreDrawingType; //  추가
    bool ExPatchEnable;
    bool ObjectOnOff;

    bool MakeAABBFromObject(CGameOBJ* pObject,
        D3DVECTOR& Position,
        D3DXQUATERNION& Rotate,
        D3DVECTOR& Scale);
    void TransformOBB2AABB(float* obb_min,
        float* obb_max,
        D3DXMATRIX* object_m,
        float* bb_min,
        float* bb_max);
    void CompareSizePath2Obj();
};

//-------------------------------------------------------------------------------------------------
enum MAP_LUMP_TYPE {
    LUMP_MAPINFO = 0,
    LUMP_TERRAIN_OBJECT,
    LUMP_TERRAIN_MOB,
    LUMP_TERRAIN_CNST,
    LUMP_TERRAIN_SOUND,
    LUMP_TERRAIN_EFFECT,
    LUMP_TERRAIN_MORPH,
    LUMP_TERRAIN_WATER,
    LUMP_TERRAIN_REGEN,
    LUMP_TERRAIN_OCEAN,
    LUMP_TERRAIN_WARP,
    LUMP_TERRAIN_COLLISION,
    LUMP_TERRAIN_EVENT_OBJECT,
    LUMP_MAP_MAX
};
enum { MAP_EMPTY = 0, MAP_USING, MAP_DIRTY };

class CMAP {
public:
    CQuadPatchManager m_QuadManager;
    std::list<CMAP_PATCH*> m_PatchUpdateList;

public:
    CMAP();
    ~CMAP();

    short m_nZoneMapXIDX;
    short m_nZoneMapYIDX;

    void SetUseMODE(short nMode) { m_nUseMODE = nMode; }
    bool IsEmpty() { return (m_nUseMODE == MAP_EMPTY); }
    bool IsUsing() { return (m_nUseMODE == MAP_USING); }
    bool IsDirty() { return (m_nUseMODE == MAP_DIRTY); }

    HNODE LightMAP() { return m_hLightMAP; }

    CMAP* Load(char* szFileName, short nZoneMapXIDX, short nZoneMapYIDX);
    void Free(void);
    void ClearFromNZIN(void);

    float GetVertexHEIGHT(short nMapX, short nMapY) { return m_ppHeight[nMapY][nMapX]; }

    float GetHEIGHT(float fWorldX, float fWorldY);
    float GetSlope(float fWorldX, float fWorldY);
    void GetNormal(float fWorldX, float fWorldY, D3DXVECTOR3& vNormal);
    void MappingToZONE(short nXFrom3x3, short nYFrom3x3);

    CMAP_PATCH* GetPATCH(int iX, int iY) { return &m_PATCH[iY][iX]; }
    CQuadPatchManager* GetQaudManager() { return &m_QuadManager; }

    bool
    AddObject(int iObjectIndex, D3DXVECTOR3& Position, D3DXQUATERNION& Rotate, D3DXVECTOR3& Scale);

    void
    AddSound(D3DVECTOR& Position, char* szSoundFile, unsigned int uiInterval, unsigned uiRange);
    void AddEffect(D3DVECTOR& Position, char* szEffectFile);

    CMAP_PATCH* GetPatchFromWorld(float fWorldX, float fWorldY);
    void CompareSizePath2ObjAll();

private:
    void ReadMapINFO(CFileSystem* pFileSystem, long lOffset);
    void ReadOceanINFO(CFileSystem* pFileSystem, long lOffset);
    void ReadObjINFO(CFileSystem* pFileSystem, long lOffset, int iType);
    bool LoadLightMapINFO(int iLumpType, char* szLightFileINF, char* szPath);

    void GetHeightsByCoordinates(float fWorldX,
        float fWorldY,
        float fHeights[4],
        float* pfWeightHoriz = NULL,
        float* pfWeightVert = NULL);
    POINT GetMapIndexFromWorld(float fWorldX, float fWorldY);

private:
    static int* m_pObjectIndex[LUMP_MAP_MAX];
    HNODE m_hLightMAP;
    short m_nUseMODE;
    short m_nXPatchCnt;
    short m_nYPatchCnt;

    CMAP_PATCH m_PATCH[PATCH_COUNT_PER_MAP_AXIS][PATCH_COUNT_PER_MAP_AXIS];

    float** m_ppHeight; //[ VERTEX_COUNT_PER_MAP_AXIS ][ VERTEX_COUNT_PER_MAP_AXIS ];

    float m_fPatchHeightMN[PATCH_COUNT_PER_MAP_AXIS * PATCH_COUNT_PER_MAP_AXIS][2]; //    추가
    float m_fQuadPatchHeightMN[85][2]; //    추가

    classDLLIST<HNODE> m_OceanLIST;
    classDLLIST<HNODE> m_OceanMatLIST;
};

//-------------------------------------------------------------------------------------------------

#define TILETBL_LAYER1(I) g_pTerrain->brushes[I].texture1_id
#define TILETBL_LAYER2(I) g_pTerrain->brushes[I].texture2_id
#define TILETBL_DNOFF(I) g_pTerrain->brushes[I].texture1_offset
#define TILETBL_UPOFF(I) g_pTerrain->brushes[I].texture2_offset
#define TILETBL_ISBLENDING(I) g_pTerrain->brushes[I].is_blending
#define TILETBL_PUTTYPE(I) g_pTerrain->brushes[I].orientation

enum ZONE_LUMP_TYPE {
    LUMP_ZONE_INFO = 0,
    LUMP_EVENT_OBJECT,
    LUMP_ZONE_TILE,
    LUMP_BRUSHES,
    LUMP_ECONOMY,
    LUMP_ZONE_MAX
};

struct tagEVENT_POS {
    D3DVECTOR m_Position;
    CStrVAR m_Name;
};

/// 한프레임에 하나의 맵만을 읽기위한 구조.
struct tagLOAD_ONE_MAP_DATA {
    short m_nCenterX;
    short m_nCenterY;
    short m_nUpdateIndex;
    bool m_bImmediateLoad;
};

/// Deferred map unload record. Written by SetCenterPosition when the player
/// crosses a zone-map boundary; processed once the grace period expires or
/// the player is far enough away that a curve-back revive is impossible.
struct tagPENDING_UNLOAD {
    short m_nZoneMapX;
    short m_nZoneMapY;
    DWORD m_dwQueuedTick;
};

/// Wall-clock instrumentation for terrain streaming, surfaced as the debug HUD's
/// "MapIO:" row.
///
/// FrameProfiler's SLOT_LOGIC_TERRAIN already covers SetCenterPosition, but it
/// publishes a 30-frame *average* -- which is precisely where a single 30 ms
/// chunk-load spike disappears. Chunk loads are also rate-limited to one per
/// 150 ms, so at 60 fps at most one frame in nine even contains one. Hence
/// separate last/worst tracking, and a phase breakdown so the number is
/// attributable rather than just alarming.
///
/// Written from CMAP::Load (phases) and CTERRAIN::SetCenterPosition (totals);
/// read by CGameStateMain::Render_GameMENU.
struct tagMAPIO_STATS {
    float m_fLastLoadMs;
    float m_fWorstLoadMs;
    float m_fLastUnloadMs;
    float m_fWorstUnloadMs;
    unsigned int m_nLoadCount;

    /// Phase breakdown of the most recent CMAP::Load.
    float m_fHimMs; ///< .HIM heightfield + lightmap material
    float m_fTilMs; ///< .TIL tile materials
    float m_fIfoMs; ///< .IFO lumps, i.e. static object construction
    float m_fLitMs; ///< the two .lit files, i.e. per-part material cloning
    float m_fQuadMs; ///< quadtree build (LoadQuadPatch / CompareSizePath2ObjAll / SetExPatchTotal)

    void Reset() { ::ZeroMemory(this, sizeof(*this)); }
};

struct Brush {
    int texture1_id;
    int texture2_id;
    int texture1_offset;
    int texture2_offset;
    int is_blending;
    int orientation;
    int type;
};

/*
    CTERRAIN represents a single map in the game.

    Each map is broken down into a grid of chunks with each chunk being
    handled independently. The data for each chunk is saved to disk in the
    TIL, HIM, IFO formats using the coordinates of the chunk (e.g. 30_30.TIL).
    Each chunk also has a directory containing the lightmaps.

    Each chunk is broken down into a grid of tiles (16x16). Each tile uses
    a predefined brush to paint textures to that tile. Blending is handled by
    the shader.

    CTERRAIN: A single map
    CMAP: A singe chunk
    CMAP_PATCH: A single tile in a chunk
    CPatchManager: Manages inserting/removing patches to/from the scene
    CQuadPatchManger: Manages view culling??
*/
#define MAX_MAP_BUFFER (3 * 3 + 6)

class CTERRAIN {
public:
    std::vector<Brush> brushes;

    PvpState pvp_state = PvpState::NoPvp;

    bool is_pvp_zone();
    bool is_clan_zone() { return this->pvp_state == PvpState::ClanZone; }

private:
    CRITICAL_SECTION m_csZONE;

    short m_nZoneNO;
    CStrVAR m_ZoneDIR;
    CStrVAR m_ZoneNAME;
    CStrVAR m_MusicFile;
    CStrVAR m_SkyNAME;
    CStrVAR m_MapFileName;
    int m_iIsDungeon;

    //	STBDATA		m_TileTBL;
    CMatLIST* m_pTILE;

    short m_nGridPerPATCH;
    int m_iMapSIZE;
    float m_fPatchSIZE;
    short m_nPatchSIZE;

    float m_fGridSIZE;
    short m_nGridSIZE;

    WORD m_wViewFRAME;

    std::list<CMAP*> m_DirtyMapList;
    CMAP* m_pMapBUFFER[MAX_MAP_BUFFER];
    CMAP* m_pMAPS[MAP_COUNT_PER_ZONE_AXIS][MAP_COUNT_PER_ZONE_AXIS];

    classDLLIST<CRegenAREA> m_RegenLIST;

    short m_nCenterMapXIDX;
    short m_nCenterMapYIDX;

    tagEVENT_POS* m_pEventPOS;
    int m_iEventPosCNT;

    void ReadZoneINFO(CFileSystem* pFileSystem, long lOffset);
    void ReadEventObjINFO(CFileSystem* pFileSystem, long lOffset);
    void ReadTileINFO(CFileSystem* pFileSystem, long lOffset);
    void read_brushes(CFileSystem* file_system, long offset);
    bool ReadECONOMY(CFileSystem* pFileSystem, long lOffset);

    char* GetMapFILE(short nMapX, short nMapY);

    CMAP* GetEmptyMAP();
    CMAP* FindLoadedMAP(short nZoneMapXIDX, short nZoneMapYIDX);
    void FreeDirtyMAP();
    void ResetStreamingState();

    /// Map-unload hysteresis. See SetCenterPosition / DrainPendingUnloads.
    std::list<tagPENDING_UNLOAD> m_PendingUnloadList;
    void DeferSubMAP(WORD wUpdateFLAG);
    /// Commits at most one map teardown per kMinDirtyFreeIntervalMs. Takes the
    /// caller's tick so it shares one GetTickCount() with the rest of the frame.
    /// Returns true only when a teardown actually ran, so the caller can time it
    /// without recording the (frequent) early-return frames.
    bool DrainPendingUnloads(DWORD dwNow);
    void DoActualUnload(short nZoneMapX, short nZoneMapY);
    bool CancelPendingUnload(short nZoneMapX, short nZoneMapY);

    /// Tick-based throttles for main-thread streaming work.
    DWORD m_dwLastMapLoadTick;
    DWORD m_dwLastDirtyFreeTick;
    /// Throttles DrainPendingUnloads to one map teardown per interval. Separate
    /// from m_dwLastDirtyFreeTick so a load-side burst cannot starve unloads (and
    /// vice versa); see the comment on DrainPendingUnloads for why the existing
    /// FreeDirtyMAP throttle does not cover this.
    DWORD m_dwLastUnloadTick;

    /// Cached last mapping coords passed to Update_VisiblePatchManager so the
    /// HUD can query cold-proximity count without re-deriving the center.
    short m_nLastMappingX;
    short m_nLastMappingY;

    /// m_PatchManager.m_ppPATCH[48x48] is populated once by ReOrginazationPatch
    /// at zone init and does NOT track subsequent map-slot rotations. Any code
    /// that reads it directly (e.g. the proximity keep-alive pass) would hit
    /// stale pointers into freed CMAPs. This flag is set true whenever m_pMAPS
    /// changes so the per-frame path refreshes the index before it is read.
    bool m_bPatchIndexDirty;

    /// 한번에는 하나의 맵만 읽기 위한 ....
    std::list<tagLOAD_ONE_MAP_DATA*> m_LoadOneMapData;
    tagLOAD_ONE_MAP_DATA*
    GetLodingMapFromList(short nCenterMapXIDX, short nCenterMapYIDX, WORD wUpdateIndex);
    bool AddOneMap(short nCenterMapXIDX, short nCenterMapYIDX, WORD wUpdateIndex);

    bool AddMAP(short nZoneMapX,
        short nZoneMapY,
        WORD wUpdateFLAG,
        bool bAllUpdate = true,
        bool bImmediateLoad = false);
    void SubMAP(WORD wUpdateFLAG);
    CMAP* GetMapPTR(short nZoneMapX, short nZoneMapY);
    void SetMapPTR(short nZoneMapX, short nZoneMapY, CMAP* pMAP);

    void ReOrginazationPatch(short nZoneMapX, short nZoneMapY);

    static CTERRAIN* m_pInstance;

    CMAP* GetMapPtrByWorldCoordinates(float fWorldX, float fWorldY);

    /// Data loading mode change
    /// bool	m_bLoadImmediate;

    CTERRAIN();
    ~CTERRAIN() {
        if (m_pInstance)
            m_pInstance->FreeZONE();
    }

public:
    CPatchManager m_PatchManager;

    CEconomy m_Economy;
    int m_iObjCreateOrder;

    static CTERRAIN* Instance();
    void Destroy() { SAFE_DELETE(m_pInstance); }

    void Lock() { ; }
    void Unlock() { ; }

    /// 2004/3/31:인자 추가 nAvy
    ///임시로 로그인화면에서 사용되는 Zone Load시에는 해당 BGM을 Play하지 않기 위하여 bPlayBGM 추가
    bool LoadZONE(short nZoneNO, bool bPlayBGM = true);
    void FreeZONE();
    bool InitZONE(float fCenterX, float fCenterY);
    void SetMapPrefetchEnabled(bool enabled);
    /// Zone-load cache warming budget, from [VIDEO] CACHE_WARM_MB. 0 = off.
    void SetCacheWarmMb(unsigned int mb);
    void GetCacheWarmStats(unsigned int& files, unsigned __int64& bytes);
    bool IsMapPrefetchEnabled() const;
    unsigned int GetQueuedMapLoadCount() const;
    unsigned int GetDirtyMapCount() const;
    unsigned int GetPrefetchQueueDepth() const;
    /// Files the prefetch worker tried to warm vs. actually opened. A non-zero
    /// attempted with a zero satisfied means the prefetcher resolves nothing --
    /// the exact failure that made the old loose-fopen version a silent no-op.
    void GetPrefetchHitStats(unsigned int& attempted, unsigned int& satisfied) const;
    unsigned int GetPendingUnloadCount() const;

    /// Streaming wall-clock stats for the debug HUD. Public and static because
    /// CMAP::Load writes the phase fields directly.
    static tagMAPIO_STATS s_MapIoStats;
    /// Last (nMappingX, nMappingY) passed to Update_VisiblePatchManager, used
    /// by the HUD to compute cold-proximity count against the current center.
    unsigned int GetProximityColdCount() const;

    short GridCountPerPatchAXIS() { return m_nGridPerPATCH; }
    int iMap_SIZE() { return m_iMapSIZE; }
    float fPatch_SIZE() { return m_fPatchSIZE; }
    short nPatch_SIZE() { return m_nPatchSIZE; }

    float fGrid_SIZE() { return m_fGridSIZE; }
    short nGrid_SIZE() { return m_nGridSIZE; }

    void Add_RegenAREA(classDLLNODE<CRegenAREA>* pNode);
    void Del_RegenAREA(classDLLNODE<CRegenAREA>* pNode);
    void Proc_RegenAREA(void);
    void Proc_Ecomony(void);

    tPOINTF Get_EventPOS(char* szName);
    tPOINTF Get_StartPOS();
    tPOINTF Get_RevivePOS();

    HNODE GetMATERIAL(short nIndex) { return m_pTILE->IDX_HNODE(nIndex); }
    HNODE GetLIGHTMAP(char* szFullPathName);

    void SetPATCH(int iX, int iY, CMAP_PATCH* pPATCH) { m_PatchManager.SetPATCH(iX, iY, pPATCH); }
    CMAP_PATCH* GetPATCH(int iZoneX, int iZoneY);

    void SetCenterPosition(float fWorldX, float fWorldY);
    // float	GetHEIGHT (float fWorldX, float fWorldY, bool &bStepOnObject, HNODE
    // hCheckNODE=NULL); float GetTerrainHeight (float fWorldX, float fWorldY, bool& bNotFound);

    /// 지형의 단순 높이를 리턴함.
    /// @bNotFound 해당 좌표에 지형이 세팅되어 있지 않은 경우에는 FALSE 값이 설정.
    float GetHeight(float fWorldX, float fWorldY, bool* pNotFound = NULL);

    /// 현재 좌표의 가장 높은 오브젝트의 위치를 반환.
    /// 오브젝트가 하나도 없는 경우에는 pNotFound 가 TRUE이며, 지형 높이가 리턴됨.
    /// 오브젝트의 높이가 지형보다 낮은 경우에는 지형의 높이가 선택됨.
    /// @bNotFound 해당 좌표에 지형이 세팅되어 있지 않은 경우에는 TRUE 값이 설정.
    float GetHeightTop(float fWorldX, float fWorldY, bool* pNotFound = NULL);

    /// 현재 좌표의 지형에 대한 기울기를 반환. 0(평면)에서 1(45도)사이의 값.
    /// @bNotFound 해당 좌표에 지형이 세팅되어 있지 않은 경우에는 TRUE 값이 설정.
    float GetSlope(float fWorldX, float fWorldY, bool* bNotFound);

    /// 현재 좌표의 지형에 대한 노말 벡터를 계산.
    /// @vNormal 정규화된 노말 벡터. 평면이라면, (0, 0, 1)
    /// @return 해당 좌표에 대한 지형이 세팅되어 있지 않은 경우에는 false 를 리턴.
    bool GetNormal(float fWorldX, float fWorldY, D3DXVECTOR3& vNormal);

    float Pick_POSITION(D3DXVECTOR3& PosPICK);
    float Pick_POSITION_Test(D3DXVECTOR3& PosPICK);
    short Pick_NearObject(D3DXVECTOR3& PosPICK, float& fDistance);
    short Pick_OBJECT(D3DXVECTOR3& PosPICK, float& fDistance);

    /// Get/Set loading mode
    /// void	SetLoadingMode( bool bImmediateMode ){ m_bLoadImmediate = bImmediateMode; };
    /// bool	IsLoadingImmediateMode( ){ return m_bLoadImmediate; };

    int GetZoneNO() { return m_nZoneNO; }

    ///
    /// 현재 월드 위치로 해당 패치를 구하기
    ///
    CMAP_PATCH* GetPatchFromWorld(float fWorldX, float fWorldY);

    void UpdatePatchManager(short nCenterMapXIDX, short nCenterMapYIDX);
    void InsertCameraPatch(float xChar, float yChar);

    static int m_RegistedPatchCnt;
};

extern CTERRAIN* g_pTerrain;

//-------------------------------------------------------------------------------------------------
#endif
