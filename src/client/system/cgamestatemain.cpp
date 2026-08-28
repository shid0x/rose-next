#include "stdafx.h"

#include "system/cgame.h"
#include "system/cgamestatemain.h"
#include "system/FrameProfiler.h"

#include "util/clipboardutil.h"

#include "bullet.h"
#include "BoneEffectBudget.h"
#include "rmlui/RoseRmlUi.h"
#include "ccamera.h"
#include "cclientstorage.h"
#include "cskydome.h"
#include "cviewmsg.h"
#include "jcommandstate.h"
#include "object.h"
#include "systemprocscript.h"
#include "network/cnetwork.h"

#include "game.h"
#include "gamecommon/skill.h"
#include "gamecommon/item.h"
#include "gamedata/cclan.h"
#include "gamedata/cparty.h"
#include "gameproc/cdaynnightproc.h"
#include "gameproc/delayedexp.h"
#include "gameproc/livecheck.h"
#include "gameproc/preventduplicatedcommand.h"
#include "gameproc/skillcommanddelay.h"
#include "gameproc/targetmanager.h"
#include "gameproc/useitemdelay.h"

#include "interface/CHelpMgr.h"
#include "interface/CTFontImpl.h"
#include "interface/CUIMediator.h"
#include "interface/ClanMarkManager.h"
#include "interface/Dlgs/ChattingDlg.h"
#include "interface/ExternalUI/ExternalUILobby.h"
#include "interface/controls/EffectString.h"
#include "interface/cursor/CCursor.h"

#include "sfx/isfx.h"
#include "sfx/sfxFont.h"
#include "sfx/sfxManager.h"
#include "tgamectrl/tcontrolmgr.h"
#include "tgamectrl/teditbox.h"
#include "tgamectrl/time2.h"

#include "tutorial/tutorialeventmanager.h"

#include <crtdbg.h>

#define CAMERA_MOVE_SPEED 10

static CEffect* s_pEF = NULL;
static short s_nEffectIDX = 29;
// static tPOINT16 s_PosClick;

#define SCREEN_LEFT 0x0001
#define SCREEN_RIGHT 0x0002
#define SCREEN_UP 0x0004
#define SCREEN_DOWN 0x0008
#define MOUSE_STOP_CHECK_FRAME 5

const int SKILLINDEX_JUMP = 13;
const int SKILLINDEX_PICKUP = 12;
const int SKILLINDEX_SIT = 11;

CGameStateMain::CGameStateMain(int iID) {
    m_iStateID = iID;

    m_bPickedPOS = false;
    m_iPickedOBJ = 0;

    CGame::GetInstance().ResetCheckFrame();
    //    CGame::GetInstance().m_bUseUserMouse = true;
}

CGameStateMain::~CGameStateMain(void) {}

int
CGameStateMain::Update(bool bLostFocus) {

    /// BeginFrame/EndFrame are in CGame::GameLoop -- see FrameProfiler.h. This state
    /// only fills slots.
    FrameProfiler::Begin(FrameProfiler::SLOT_LOGIC);

    if (g_pCApp->IsActive()) {
        CD3DSOUND::UpdateListener(g_pCamera);
    }

#ifdef __VIRTUAL_SERVER
    g_pTerrain->Proc_RegenAREA();
#endif
    FrameProfiler::Begin(FrameProfiler::SLOT_LOGIC_EFFECTS);
    g_pEffectLIST->Proc();
    g_pBltMGR->ProcBULLET();
    FrameProfiler::End(FrameProfiler::SLOT_LOGIC_EFFECTS);

    g_pCamera->Update();

    FrameProfiler::Begin(FrameProfiler::SLOT_LOGIC_UIUPD);
    g_UIMed.Update();
    FrameProfiler::End(FrameProfiler::SLOT_LOGIC_UIUPD);
    g_DayNNightProc.Proc();
    g_UseItemDelay.Proc();
    g_UseSkillDelay.Proc();
    g_SoloSkillDelayTick.Proc();
    g_SoloUseItemDelayTick.Proc();

    CParty::GetInstance().Update();
    CSkillCommandDelay::GetSingleton().Proc();
    // processing  ...

    /// SFX
    FrameProfiler::Begin(FrameProfiler::SLOT_LOGIC_EFFECTS);
    CSFXManager::GetSingleton().Update();
    FrameProfiler::End(FrameProfiler::SLOT_LOGIC_EFFECTS);

    /// Tutorial event check..
    CTutorialEventManager::GetSingleton().Proc();

    if (g_ClientStorage.m_VideoOption.background_render || !bLostFocus) {
        UpdateCameraPositionByMouse();
        UpdateCheckFrame();
        CTargetManager::GetSingleton().Proc();
    }

    CLiveCheck::GetSingleton().Check();
    CDelayedExp::GetSingleton().Proc();
    CPreventDuplicatedCommand::GetSingleton().Proc();

    CClanMarkManager::GetSingleton().UpdatePool();

    FrameProfiler::End(FrameProfiler::SLOT_LOGIC);
    FrameProfiler::Begin(FrameProfiler::SLOT_SCENE_UPDATE);
    ::updateSceneTransform(); // �̵� �ִϸ��̼� ó��...
    ::updateSceneEx(); // ���ϸ��̼� ó��...
    FrameProfiler::End(FrameProfiler::SLOT_SCENE_UPDATE);
    FrameProfiler::Begin(FrameProfiler::SLOT_LOGIC);

    FrameProfiler::Begin(FrameProfiler::SLOT_LOGIC_OBJPROC);
    g_pObjMGR->ProcOBJECT();
    FrameProfiler::End(FrameProfiler::SLOT_LOGIC_OBJPROC);

    D3DVECTOR vPos = g_pAVATAR->GetWorldPos();

    FrameProfiler::Begin(FrameProfiler::SLOT_LOGIC_TERRAIN);
    g_pTerrain->SetCenterPosition(vPos.x, vPos.y);
    FrameProfiler::End(FrameProfiler::SLOT_LOGIC_TERRAIN);

    FrameProfiler::End(FrameProfiler::SLOT_LOGIC);
    FrameProfiler::Begin(FrameProfiler::SLOT_SCENE_UPDATE);
    ::updateSceneExAfter();
    FrameProfiler::End(FrameProfiler::SLOT_SCENE_UPDATE);
    FrameProfiler::Begin(FrameProfiler::SLOT_LOGIC);

    CBoneEffectBudget::Instance().Update();

    //�� ��ȣ : ���ż�ȯ ������Ʈ
    goddessMgr.Update();

    if (g_ClientStorage.m_VideoOption.background_render || !bLostFocus) {
        /*		UpdateCameraPositionByMouse();
                UpdateCheckFrame();
                CTargetManager::GetSingleton().Proc();
                CSkillCommandDelay::GetSingleton().Proc();   */

        FrameProfiler::End(FrameProfiler::SLOT_LOGIC);

        /// beginScene() also runs the entire shadow map pass before returning.
        FrameProfiler::Begin(FrameProfiler::SLOT_SHADOW);
        const bool scene_began = ::beginScene();
        FrameProfiler::End(FrameProfiler::SLOT_SHADOW);

        if (scene_began) {
            FrameProfiler::Begin(FrameProfiler::SLOT_RENDER);
            ::clearScreen();
            ::renderScene();
            FrameProfiler::End(FrameProfiler::SLOT_RENDER);

            FrameProfiler::Begin(FrameProfiler::SLOT_UI);
            if (!g_GameDATA.m_bNoUI) {
                Render_GameMENU();
            }

            this->render_dev_ui();
            FrameProfiler::End(FrameProfiler::SLOT_UI);

            /// Sample the immediate-flush counters for the frame-spike log while
            /// they are still valid: swapBuffers() -> zz_system::sleep() rolls
            /// them over at end of frame, and FrameProfiler::EndFrame() runs
            /// after that, so this cannot be deferred into the profiler itself.
            ///
            /// Sampled here, at the end of everything that can force a load, so
            /// the figure is a whole-frame total. Flushes are spread across three
            /// phases -- the octree's within-50 m distance pass in updateSceneEx()
            /// (scnupd), zz_terrain_block::before_render() under beginScene()
            /// (shadow), and the render itself -- so a large flush= says streaming
            /// was involved without saying which phase carried it. Read it against
            /// the phase values rather than instead of them.
            ///
            /// No-ops unless [VIDEO] FRAME_SPIKE_LOG_MS is set.
            FrameProfiler::CaptureFlushStats();

            /// D3D9 buffers commands, so waiting for the GPU surfaces here.
            FrameProfiler::Begin(FrameProfiler::SLOT_PRESENT);
            FrameProfiler::Begin(FrameProfiler::SLOT_PRESENT_ENDSCENE);
            ::endScene();
            FrameProfiler::End(FrameProfiler::SLOT_PRESENT_ENDSCENE);
            /// swapBuffers() also runs zz_system::sleep(), i.e. the resource
            /// amortiser and the software frame cap -- see the present[] group.
            FrameProfiler::Begin(FrameProfiler::SLOT_PRESENT_SWAP);
            ::swapBuffers();
            FrameProfiler::End(FrameProfiler::SLOT_PRESENT_SWAP);
            FrameProfiler::End(FrameProfiler::SLOT_PRESENT);
        }
    } else {
        FrameProfiler::End(FrameProfiler::SLOT_LOGIC);
        Sleep(30);
    }

    g_pObjMGR->ClearViewObjectList();

    return 0;
}

int
CGameStateMain::Enter(int iPrevStateID) {
    ::SetOceanSFXOnOff(true);
    /// [VIDEO] MAP_PREFETCH=0 turns the chunk cache-warming worker off, which is
    /// the A/B switch for the boundary-crossing hitch. SetMapPrefetchEnabled logs
    /// the transition -- check the log rather than assuming, a toggle that
    /// silently does nothing is how the previous version of this stayed broken.
    g_pTerrain->SetMapPrefetchEnabled(g_ClientStorage.GetMapPrefetch());

    CGame::GetInstance().ClearWndMsgQ();
    g_pNet->Send_cli_JOIN_ZONE(g_pAVATAR->GetWeightRate());

    ///���� ��Ŷ �����ִ�.
    g_HelpMgr.Update();

    g_itMGR.ChangeState(IT_MGR::STATE_NORMAL);
    if (CGame::REPAIR_NONE != CGame::GetInstance().GetRepairMode())
        CGame::GetInstance().ResetRepairMode();

    if (ISFX* p = CSFXManager::GetSingleton().FindSFXWithType(SFX_FONT)) {
        CSFXFont* sfx_font = (CSFXFont*)p;

        POINT draw_position;
        int draw_width;

        /// Zone Name
        SIZE size = getFontTextExtent(g_GameDATA.m_hFONT[FONT_OUTLINE_18_BOLD],
            ZONE_NAME(g_pTerrain->GetZoneNO()));
        draw_width = size.cx;
        draw_position.x = g_pCApp->GetWIDTH() / 2 - size.cx / 2;
        draw_position.y = 150; ///�ϴ� ����

        CEffectString* child = new CEffectString;
        child->SetType(CSFXFont::TYPE_ZONE_NAME);
        child->SetString(FONT_OUTLINE_18_BOLD,
            (char*)ZONE_NAME(g_pTerrain->GetZoneNO()),
            draw_position,
            draw_width,
            6 * 1000);
        sfx_font->AddEffectString(child);

        /// Zone Description
        draw_width = 300;
        draw_position.x = g_pCApp->GetWIDTH() / 2 - 300 / 2;
        draw_position.y = 250;
        child = new CEffectString;
        child->SetType(CSFXFont::TYPE_ZONE_DESC);
        child->SetString(FONT_OUTLINE_14_BOLD,
            (char*)ZONE_DESC(g_pTerrain->GetZoneNO()),
            draw_position,
            draw_width,
            6 * 1000);
        sfx_font->AddEffectString(child);
    }

    if (g_pTerrain->is_clan_zone())
        g_itMGR.CloseDialog(DLG_TYPE_MINIMAP);
    else
        g_itMGR.OpenDialog(DLG_TYPE_MINIMAP, false);

#ifdef DISCORD
    g_pCApp->update_discord_status(g_pAVATAR);
#endif

    return 0;
}

int
CGameStateMain::Leave(int iNextStateID) {
    g_pTerrain->SetMapPrefetchEnabled(false);

    g_pCamera->Detach();

    ::SetOceanSFXOnOff(false);

    if (ISFX* p = CSFXManager::GetSingleton().FindSFXWithType(SFX_FONT)) {
        CSFXFont* sfx_font = (CSFXFont*)p;
        sfx_font->RemoveEffectStringsByType(CSFXFont::TYPE_ZONE_NAME);
        sfx_font->RemoveEffectStringsByType(CSFXFont::TYPE_ZONE_DESC);
    }

#ifdef DISCORD
    g_pCApp->update_discord_status(nullptr);
#endif

    return 0;
}

void
CGameStateMain::ServerDisconnected() {
    g_itMGR.ServerDisconnected();
}

#include "../GameProc/UseItemDelay.h"

void
CGameStateMain::Render_GameMENU() {

    ::beginSprite(D3DXSPRITE_ALPHABLEND);

    g_pViewMSG->Draw();
    /// Screen message display
    g_UIMed.Draw();

    ::endSprite();

    ::drawSpriteSFX();

    ::beginSprite(D3DXSPRITE_ALPHABLEND);

    g_itMGR.Update();

    /// UI display
    //	g_pViewMSG->Draw ();

    CTargetManager::GetSingleton().Draw();

    CTIme::GetInstance().Draw();

    /// SFX
    CSFXManager::GetSingleton().Draw();
    // CTutorialEventManager::GetSingleton().Draw();
    ::endSprite();

    /// Streaming spike diagnostic. OFF by default -- enable with
    /// [VIDEO] STREAM_SPIKE_LOG_MS=4 (the threshold in ms doubles as the switch).
    ///
    /// Runs independently of the debug HUD, because a chunk-display stall lasts
    /// one frame at 130 fps and no HUD row can be read while it happens; writing
    /// to client.log lets the evidence be collected by just playing and reviewed
    /// afterwards. That also makes it far too chatty to leave on, hence opt-in.
    ///
    /// Rate-limited because the zone-in burst would otherwise flood the log (and
    /// the async logger flushes per record). Logs the MapIO figures alongside so
    /// a display stall can be told apart from a chunk-load stall in one line.
    const UINT nSpikeLogMs = g_ClientStorage.GetStreamSpikeLogMs();
    if (nSpikeLogMs > 0) {
        static DWORD s_dwLastSpikeLogTick = 0;
        const float fFlushMs = ::getImmediateFlushMs();
        const DWORD dwNow = ::GetTickCount();
        if (fFlushMs >= (float)nSpikeLogMs && (DWORD)(dwNow - s_dwLastSpikeLogTick) >= 250) {
            s_dwLastSpikeLogTick = dwNow;
            /// terrain/mesh/tex/mat splits the count by owning manager, which is
            /// what distinguishes terrain-block creation from object mesh and
            /// texture loading -- 256 nodes is ambiguous otherwise, since a chunk
            /// is both 16x16=256 patches and ~260 static objects.
            /// ins/rem are patch scene toggles: rem>0 alongside ins>0 means
            /// patches are churning in and out rather than genuinely appearing.
            /// lazyq/lazyterr: entrance-line depth, but sampled HERE, i.e. AFTER
            /// the render phase already force-flushed the queue. It therefore
            /// reads ~0 during exactly the spikes it looks like it should
            /// explain -- do not read that as "the nodes were never queued".
            /// (That misreading cost a whole debugging round.) Use leadavg /
            /// leadmax instead: they are measured at the flush itself.
            ///
            /// leadavg is the decisive field. ~1 frame = the work was needed
            /// almost immediately, so no amount of pre-loading can help and the
            /// fix belongs upstream at the insert (TERRAIN_INSERTS_PER_FRAME).
            /// Hundreds of frames = the amortiser had slack and wasted it, which
            /// LOAD_BUDGET_US fixes. Terrain measured 1; textures measured 200+.
            LOG_INFO("Flush spike: {:.1f} ms over {} nodes "
                     "[terrain={} mesh={} tex={} mat={} other={}] "
                     "(last map load {:.1f} ms, queued={} ins={} rem={} "
                     "lazyq={} lazyterr={} "
                     "loadq={} loadimm={} fq={} fdirect={} delayed={} "
                     "leadavg={} leadmax={} budget={}us)",
                fFlushMs,
                ::getImmediateFlushCount(),
                ::getImmediateFlushKind(0),
                ::getImmediateFlushKind(1),
                ::getImmediateFlushKind(2),
                ::getImmediateFlushKind(3),
                ::getImmediateFlushKind(4),
                CTERRAIN::s_MapIoStats.m_fLastLoadMs,
                g_pTerrain ? g_pTerrain->GetQueuedMapLoadCount() : 0,
                CMAP_PATCH::s_nInsertThisFrame,
                CMAP_PATCH::s_nRemoveThisFrame,
                ::getLazyQueueDepth(),
                ::getLazyTerrainQueueDepth(),
                ::getLoadPathCount(0),
                ::getLoadPathCount(1),
                ::getLoadPathCount(2),
                ::getLoadPathCount(3),
                ::getUseDelayedLoad(),
                ::getLoadPathCount(4),
                ::getLoadPathCount(5),
                ::getLoadBudgetPerFrameUsec());
        }
    }

    if (g_GameDATA.m_bDisplayDebugInfo) {
        /// Debug HUD layout: single yellow column well clear of the
        /// top-left UI (char panel ends near x=230). 16 px row stride.
        /// Korean strings from the original client were removed because
        /// drawFontf does not handle CP949 in this build — only English.
        const int kDebugX = 450;
        const int kDebugRowStride = 16;
        int nRowY = 15;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "%d FPS, Patch[ %d, %d ], Pos[ %.1f, %.1f, %.1f ]",
            g_pCApp->DisplayFrameRate(),
            g_GameDATA.m_PosPATCH.x,
            g_GameDATA.m_PosPATCH.y,
            g_pAVATAR->Get_CurPOS().x,
            g_pAVATAR->Get_CurPOS().y,
            g_pAVATAR->Get_CurPOS().z);
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "TotOBJ:%d Cnst:%d Item:%d Tree:%d Mob:%d Fx:%d Patch:%d",
            g_pObjMGR->Get_ObjectCount(),
            g_pObjMGR->Get_ObjectCount(OBJ_CNST),
            g_pObjMGR->Get_ObjectCount(OBJ_ITEM),
            g_pObjMGR->Get_ObjectCount(OBJ_GROUND),
            g_pObjMGR->Get_ObjectCount(OBJ_MOB),
            g_pEffectLIST->GetCount(),
            CTERRAIN::m_RegistedPatchCnt);
        nRowY += kDebugRowStride;

        /// Terrain streaming pipeline depth. loads = maps queued for disk
        /// load, unload = maps deferred pending hysteresis, dirty = freed
        /// CMAPs waiting recycle, prefetch = depth of the OS-page-cache
        /// prefetcher thread. All four should return to 0 shortly after
        /// stopping movement; a non-zero steady-state means streaming is
        /// still catching up and you are paying InsertToScene bursts.
        ///
        /// pfHit = files the prefetch worker actually opened / files it tried.
        /// This is the honesty check on the prefetcher: it used to fopen loose
        /// map paths, which do not exist in a packed deployment, so it warmed
        /// nothing while still looking alive on the `prefetch=` counter.
        unsigned int nPrefetchAttempted = 0;
        unsigned int nPrefetchSatisfied = 0;
        g_pTerrain->GetPrefetchHitStats(nPrefetchAttempted, nPrefetchSatisfied);

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "Stream: loads=%u unload=%u dirty=%u prefetch=%u cold22=%u pfHit=%u/%u",
            g_pTerrain->GetQueuedMapLoadCount(),
            g_pTerrain->GetPendingUnloadCount(),
            g_pTerrain->GetDirtyMapCount(),
            g_pTerrain->GetPrefetchQueueDepth(),
            g_pTerrain->GetProximityColdCount(),
            nPrefetchSatisfied,
            nPrefetchAttempted);
        nRowY += kDebugRowStride;

        /// Wall-clock cost of the streaming work itself, which the averaged
        /// `terr=` on the Logic: line cannot show: chunk loads are gated to one
        /// per 150 ms, so at 60 fps most frames contain none at all and a single
        /// 30 ms spike vanishes into a 30-frame mean.
        ///
        /// load/unload are the most recent occurrence, worst is the worst since
        /// entering the zone. The bracketed phases break down the last load:
        /// him = heightfield + lightmap material, til = tile materials,
        /// ifo = static object construction, lit = per-part material cloning,
        /// quad = quadtree rebuild.
        ///
        /// Read this together with `pfHit` above. pfHit satisfied==0 while
        /// attempted>0 means the prefetch worker is resolving nothing, and every
        /// load number here is a cold-cache measurement regardless of the
        /// MAP_PREFETCH setting.
        const tagMAPIO_STATS& mapio = CTERRAIN::s_MapIoStats;
        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "MapIO: load=%.1f (worst %.1f) [him=%.1f til=%.1f ifo=%.1f lit=%.1f quad=%.1f] "
            "unload=%.1f (worst %.1f) n=%u",
            mapio.m_fLastLoadMs,
            mapio.m_fWorstLoadMs,
            mapio.m_fHimMs,
            mapio.m_fTilMs,
            mapio.m_fIfoMs,
            mapio.m_fLitMs,
            mapio.m_fQuadMs,
            mapio.m_fLastUnloadMs,
            mapio.m_fWorstUnloadMs,
            mapio.m_nLoadCount);
        nRowY += kDebugRowStride;

        /// Per-frame scene-toggle activity. ins/rem = CMAP_PATCH
        /// InsertToScene/RemoveFromScene calls this frame; resPatch =
        /// patches currently held in-scene (grace + proximity + frustum).
        /// A sustained non-zero ins on a stable camera means terrain is
        /// thrashing — correlate with camera angle to validate the
        /// frustum-rotation hypothesis.
        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "Scene: ins=%u rem=%u ring=%u sub=%u resPatch=%u drawPatch=%d",
            CMAP_PATCH::s_nInsertThisFrame,
            CMAP_PATCH::s_nRemoveThisFrame,
            CPatchManager::s_nRingStampsThisFrame,
            g_pTerrain->m_PatchManager.GetFrustumPatchCount(),
            g_pTerrain->m_PatchManager.GetResidentPatchCount(),
            g_pTerrain->m_PatchManager.m_nDrawingPatch);
        nRowY += kDebugRowStride;

        const BoneEffectBudgetStats& boneFx = CBoneEffectBudget::Instance().GetStats();
        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "BoneFx: groups=%d effects=%d emitters=%d active=%d cap=%d emit=%.0f tiers F/R/M/O=%d/%d/%d/%d topNpc=%d",
            boneFx.groups,
            boneFx.effects,
            boneFx.emitters,
            boneFx.activeParticles,
            boneFx.runtimeCapacity,
            boneFx.configuredEmitRate,
            boneFx.tierCounts[PARTICLE_EFFECT_TIER_FULL],
            boneFx.tierCounts[PARTICLE_EFFECT_TIER_REDUCED],
            boneFx.tierCounts[PARTICLE_EFFECT_TIER_MINIMAL],
            boneFx.tierCounts[PARTICLE_EFFECT_TIER_OFF],
            boneFx.topNpc);
        nRowY += kDebugRowStride;

        /// Spike 1 exit criterion #4: the RmlUi pass must be measurable, not
        /// assumed cheap. Only shown while the overlay is actually enabled.
        if (RoseRmlUi::IsEnabled()) {
            ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
                false,
                kDebugX,
                nRowY,
                g_dwYELLOW,
                "RmlUi: draws=%d",
                RoseRmlUi::GetDrawCallCount());
            nRowY += kDebugRowStride;
        }

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "PartBatch: groups=%d particles=%d draws=%d fallback=%d saved=%d",
            ::getParticleBatchGroups(),
            ::getParticleBatchParticles(),
            ::getParticleBatchDrawCalls(),
            ::getParticleBatchFallback(),
            ::getParticleBatchSavedDrawCalls());
        nRowY += kDebugRowStride;

        /// Resource loads forced to run synchronously at first-render time
        /// instead of being amortised by the engine's lazy entrance line --
        /// mesh generation, vertex-buffer creation and texture upload for
        /// anything that just became visible.
        ///
        /// This is the cost of *displaying* new terrain, as opposed to reading
        /// its files (that is the MapIO: row). It lands inside beginScene(), so
        /// it shows up in the Time: line's `shadow=` slot, not in `logic=`.
        /// A spike here on the frame a new chunk appears -- or while the intro
        /// camera sweeps toward the character, where nothing is being loaded
        /// from disk at all -- means the hitch is the immediate-flush path.
        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "Flush: now=%.1fms n=%d | recent %.1fms over %d (%.1fs ago) | peak %.1fms over %d | "
            "lazyq=%d/%d budget=%dus",
            ::getImmediateFlushMs(),
            ::getImmediateFlushCount(),
            ::getImmediateFlushRecentMs(),
            ::getImmediateFlushRecentCount(),
            ::getImmediateFlushRecentAgeMs() / 1000.0f,
            ::getImmediateFlushWorstMs(),
            ::getImmediateFlushWorstCount(),
            ::getLazyTerrainQueueDepth(),
            ::getLazyQueueDepth(),
            ::getLoadBudgetPerFrameUsec());
        nRowY += kDebugRowStride;

        /// Where the milliseconds actually go. Averaged over 30 frames; max is the worst
        /// single frame in that window. See FrameProfiler.h for how to read it -- briefly:
        /// render high = CPU-bound submitting draws; present high = GPU/vsync bound, so
        /// fewer draw calls will not help; scnupd high = animation/culling, which no
        /// rendering change can touch. `oth` is total minus the bracketed phases.
        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "Time: %.1fms (max %.1f) netin=%.1f logic=%.1f scnupd=%.1f shadow=%.1f "
            "render=%.1f ui=%.1f present=%.1f oth=%.1f",
            FrameProfiler::GetTotalMs(),
            FrameProfiler::GetMaxTotalMs(),
            FrameProfiler::GetMs(FrameProfiler::SLOT_NETINPUT),
            FrameProfiler::GetMs(FrameProfiler::SLOT_LOGIC),
            FrameProfiler::GetMs(FrameProfiler::SLOT_SCENE_UPDATE),
            FrameProfiler::GetMs(FrameProfiler::SLOT_SHADOW),
            FrameProfiler::GetMs(FrameProfiler::SLOT_RENDER),
            FrameProfiler::GetMs(FrameProfiler::SLOT_UI),
            FrameProfiler::GetMs(FrameProfiler::SLOT_PRESENT),
            FrameProfiler::GetTotalMs() - FrameProfiler::GetAccountedMs());
        nRowY += kDebugRowStride;

        /// Breakdown *inside* the logic phase above (these do not add to it). obj is
        /// g_pObjMGR->ProcOBJECT(), which is the part that scales with object count.
        {
            const float fLogic = FrameProfiler::GetMs(FrameProfiler::SLOT_LOGIC);
            const float fObj = FrameProfiler::GetMs(FrameProfiler::SLOT_LOGIC_OBJPROC);
            const float fTerr = FrameProfiler::GetMs(FrameProfiler::SLOT_LOGIC_TERRAIN);
            const float fFx = FrameProfiler::GetMs(FrameProfiler::SLOT_LOGIC_EFFECTS);
            const float fUi = FrameProfiler::GetMs(FrameProfiler::SLOT_LOGIC_UIUPD);
            ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
                false,
                kDebugX,
                nRowY,
                g_dwYELLOW,
                "Logic: %.1fms = obj=%.1f terr=%.1f fx=%.1f uiupd=%.1f rest=%.1f",
                fLogic,
                fObj,
                fTerr,
                fFx,
                fUi,
                fLogic - (fObj + fTerr + fFx + fUi));
            nRowY += kDebugRowStride;
        }

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "AtkSpd:%d MoveSpd:%.1f BaseSpd:%.1f",
            g_pAVATAR->stats.attack_speed,
            (g_pAVATAR->GetPetMode() < 0) ? g_pAVATAR->adjusted_move_speed
                                          : g_pAVATAR->m_pObjCART->adjusted_move_speed,
            (g_pAVATAR->GetPetMode() < 0) ? g_pAVATAR->stats.move_speed
                                          : g_pAVATAR->m_pObjCART->stats.move_speed);
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "WorldTime:%d ZoneTime:%d Blend:%.2f",
            g_DayNNightProc.GetWorldTime(),
            g_DayNNightProc.GetZoneTime(),
            g_DayNNightProc.GetBlendFactor());
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "WorldRATE:%d WorldPROD:%d CountryCode:%d",
            Get_WorldRATE(),
            Get_WorldPROD(),
            0);
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "bCastingStart:%d DoingSkill:%d Command:%d",
            g_pAVATAR->m_bCastingSTART,
            g_pAVATAR->m_nDoingSkillIDX,
            g_pAVATAR->Get_COMMAND());
        nRowY += kDebugRowStride;

        if (g_pAVATAR->GetCur_SummonCNT() > 0) {
            ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
                false,
                kDebugX,
                nRowY,
                g_dwYELLOW,
                "Summons: count=%d used=%d max=%d",
                g_pAVATAR->GetCur_SummonCNT(),
                g_pAVATAR->GetCur_SummonUsedCapacity(),
                g_pAVATAR->GetCur_SummonMaxCapacity());
            nRowY += kDebugRowStride;
        }

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "Stamina:%d SkillDelay:%d",
            g_pAVATAR->m_GrowAbility.m_nSTAMINA,
            CSkillCommandDelay::GetSingleton().GetSkillCommandDelayProgressRatio());
        nRowY += kDebugRowStride;

        ::drawFontf(g_GameDATA.m_hFONT[FONT_NORMAL],
            false,
            kDebugX,
            nRowY,
            g_dwYELLOW,
            "UseItemDelay  HP:%d MP:%d Other:%d",
            g_UseItemDelay.GetUseItemDelay(USE_ITEM_HP),
            g_UseItemDelay.GetUseItemDelay(USE_ITEM_MP),
            g_UseItemDelay.GetUseItemDelay(USE_ITEM_OTHERS));
    }
}

int
CGameStateMain::ProcMouseInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    CGame& refGame = CGame::GetInstance();

    POINT ptMouse = {LOWORD(lParam), HIWORD(lParam)};
    ;

    if (!g_GameDATA.m_bNoUI && g_itMGR.MsgProc(uiMsg, wParam, lParam))
        return 1;

    m_ScreenPOS.m_nX = (short)ptMouse.x;
    m_ScreenPOS.m_nY = (short)ptMouse.y;

    switch (uiMsg) {
        case WM_LBUTTONDOWN: {
            // set screen mouse position & world position
            Pick_POSITION();
            On_WM_LBUTTONDOWN(wParam, lParam);
            break;
        }
            // Dagnarus
        case WM_RBUTTONDOWN: {
            On_WM_RBUTTONDOWN(wParam, lParam);
        } break;

        case WM_MOUSEWHEEL:
            On_WM_MOUSEWHEEL(wParam, lParam);
            break;

        case WM_LBUTTONDBLCLK:
            On_WM_LBUTTONDBCLICK(wParam, lParam);
            break;

        default:
            break;
    }

    return true;
}

#include "System/System_FUNC.h"
int
CGameStateMain::ProcKeyboardInput(UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    int iRet = 0;

    switch (uiMsg) {
        case WM_KEYUP: {
            unsigned int oemScan = int(lParam & (0xff << 16)) >> 16;
            UINT vk = MapVirtualKey(oemScan, 1);
            switch (vk) {
                case VK_CONTROL:
                    g_GameDATA.m_bShowDropItemInfo = false;
                    break;

                case 0x5A: // z : �ɱ�/���� ���
                    if (CTControlMgr::GetInstance()->GetKeyboardInputType()
                            == CTControlMgr::INPUTTYPE_NORMAL
                        && NULL == CTEditBox::s_pFocusEdit) {
                        assert(g_pAVATAR);
                        CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
                        assert(pSkillSlot);

                        CSkill* pSkill = pSkillSlot->GetSkillBySkillIDX(SKILLINDEX_SIT);
                        assert(pSkill);
                        if (pSkill)
                            pSkill->Execute();
                    }
                    break;
                case VK_SPACE:
                    if (CTControlMgr::GetInstance()->GetKeyboardInputType()
                            == CTControlMgr::INPUTTYPE_NORMAL
                        && NULL == CTEditBox::s_pFocusEdit) {
                        assert(g_pAVATAR);
                        CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
                        assert(pSkillSlot);

                        CSkill* pSkill = pSkillSlot->GetSkillBySkillIDX(SKILLINDEX_JUMP);
                        if (pSkill)
                            pSkill->Execute();
                    }
                    break;
                case 192: //` : �ݱ�
                    break;
                case VK_SHIFT: //
                    if (CTControlMgr::GetInstance()->GetKeyboardInputType()
                            == CTControlMgr::INPUTTYPE_NORMAL
                        && NULL == CTEditBox::s_pFocusEdit) {
                        g_UserInputSystem.SetTargetSelf();
                    }
                default:
                    break;
            }
        } break;

        case WM_SYSKEYDOWN: {
            unsigned int oemScan = int(lParam & (0xff << 16)) >> 16;
            UINT vk = MapVirtualKey(oemScan, 1);
            if (GetAsyncKeyState(VK_SHIFT) < 0) {
                switch (vk) {
                    // case 0x4B://K
                    //	{
                    //		if( CTControlMgr::GetInstance()->GetKeyboardInputType() ==
                    // CTControlMgr::INPUTTYPE_AUTOENTER )
                    //		{
                    //			it_SetKeyboardInputType( CTControlMgr::INPUTTYPE_NORMAL );
                    //		}
                    //		else
                    //		{
                    //			it_SetKeyboardInputType( CTControlMgr::INPUTTYPE_AUTOENTER );
                    //			if( CTDialog* pDlg = g_itMGR.FindDlg( DLG_TYPE_CHAT ) )
                    //				pDlg->Show();
                    //		}
                    //	}
                    //	break;
                    case 0x52: /// R
                        if (CGame::GetInstance().GetRight() >= CHEAT_GM)
                            CGame::GetInstance().ToggleAutoRun();

                        break;
                    default:
                        break;
                }
            }
        } break;

        default:
            break;
    }

    if (g_itMGR.MsgProc(uiMsg, wParam, lParam))
        return 1;

    switch (uiMsg) {

        case WM_CHAR:
            return On_WM_CHAR(wParam, lParam);

        case WM_KEYDOWN:
            return On_WM_KEYDOWN(wParam, lParam);

        case WM_SYSKEYDOWN: {
            unsigned int oemScan = int(lParam & (0xff << 16)) >> 16;
            UINT vk = MapVirtualKey(oemScan, 1);

            switch (vk) {
                ///'2'
                case 0x32:
                    //{
                    // CGame::GetInstance().ToggleUserMouseFlag();
                    // CGame::GetInstance().SetUserCursor( CGame::CURSOR_DEFAULT );
                    //	CCursor::GetInstance().ChangeNextState();
                    //}
                    /// ScreenFadeInStart( 25, 0, 0, 0, 0, 0 );
                    break;

                /// �Կ���� ��ȯ..
                case 0x35: // 5
                {
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
                        g_GameDATA.m_bFilmingMode = !g_GameDATA.m_bFilmingMode;
                        if (g_GameDATA.m_bFilmingMode)
                            ::setVisibilityRecursive(g_pAVATAR->GetZMODEL(), 0.0f);
                        else
                            ::setVisibilityRecursive(g_pAVATAR->GetZMODEL(), 1.0f);
                    }
                } break;

                /// ���콺 Ŀ�� �����
                case 0x36: // 6
                {
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
                        g_GameDATA.m_bShowCursor = !g_GameDATA.m_bShowCursor;
                    }
                } break;

                /// 'K'
                case 0x4B: {
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
                        g_DayNNightProc.SetWorldTime(g_pTerrain->GetZoneNO(),
                            g_DayNNightProc.GetWorldTime() + 10);
                    }
                } break;

                /// '9'
                case 0x39: {
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
                        int iClientObjectIndex = g_UserInputSystem.GetCurrentTarget();
                        CObjCHAR* pChar = g_pObjMGR->Get_ClientCharOBJ(
                            g_pObjMGR->Get_ServerObjectIndex(iClientObjectIndex),
                            false);
                        if (pChar) {
                            switch (pChar->Get_TYPE()) {
                                case OBJ_MOB:
                                    g_pNet->Send_cli_CHAT("/get tg");
                                    break;
                                case OBJ_AVATAR: {
                                    std::string cheat = "/get hp ";
                                    cheat.append(pChar->Get_NAME());
                                    g_pNet->Send_cli_CHAT((char*)cheat.c_str());
                                } break;
                                case OBJ_USER:
                                    g_pNet->Send_cli_CHAT("/get hp");
                                    break;
                                default:
                                    break;
                            }
                        } else {
                            g_pNet->Send_cli_CHAT("/get hp");
                        }
                    }
                } break;

                /// 'j'
                case 0x4A:
                    if (CGame::GetInstance().GetRight() >= CHEAT_GM) {
#ifdef _DEBUG
                        g_GameDATA.m_bShowCurPos = !g_GameDATA.m_bShowCurPos;
#endif
                    }
                    break;

                /// 'H'
                // case 0x48:
                //	if( CGame::GetInstance().GetRight() )
                //	{
                //		g_GameDATA.m_bNoUI = !g_GameDATA.m_bNoUI;
                //	}
                //	break;

                /// 'D'
                case 0x44:
                    /*char* d_right;
                    sprintf( d_right, "Right: %i", CGame::GetInstance().GetRight() );
                    ::OutputDebugString(d_right);*/
                    if (CGame::GetInstance().GetRight()) {
                        g_GameDATA.m_bDisplayDebugInfo = !g_GameDATA.m_bDisplayDebugInfo;
                    }
                    break;
                case 0x31: {
#ifdef _DEBUG
                    if (CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_CHAT)) {
                        CChatDLG* pChatDlg = (CChatDLG*)pDlg;
                        pChatDlg->SendChatMsgRepeat();
                    }
#else

                    if (CGame::GetInstance().GetRight() >= CHEAT_DEV) {
                        if (CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_CHAT)) {
                            CChatDLG* pChatDlg = (CChatDLG*)pDlg;
                            pChatDlg->SendChatMsgRepeat();
                        }
                    }

#endif
                    break;
                }

#if defined(_DEBUG) || defined(_D_RELEASE)
                    /// '7'
                case 0x37:
                    /// g_ClientStorage.SetUseRoughMap( !g_ClientStorage.GetUseRoughMap() );
                    {
                        int iFace = g_pAVATAR->GetCharExpression();
                        iFace++;
                        if (iFace > 6)
                            iFace = 0;

                        g_pAVATAR->SetCharExpression(iFace);
                        g_pAVATAR->Update();
                    }
                    break;

                ///'8'
                case 0x38: {
                    if (!s_pEF) {
                        s_pEF = g_pEffectLIST->Add_EffectWithIDX(s_nEffectIDX);
                        if (s_pEF) {
                            s_pEF->LinkNODE(g_pAVATAR->GetZMODEL());
                            s_pEF->InsertToScene();
                        }

                        s_nEffectIDX = (s_nEffectIDX + 1) % g_pEffectLIST->GetFileCNT();
                    } else {
                        /// g_pEffectLIST->Del_EFFECT( s_pEF );
                        SAFE_DELETE(s_pEF);
                        s_pEF = NULL;
                    }
                } break;

                ///'0'
                case 0x30:
                    g_GameDATA.m_bObserverCameraMode = !g_GameDATA.m_bObserverCameraMode;
                    SetObserverCameraOnOff();
                    break;

#endif
                default:
                    return false;
            }
            return true;
        }
        default:
            break;
    }
    return false;
    ;
}

///*-------------------------------------------------------------------------------------*/
#ifdef _DEBUG
    #include "../ObjFixedEvent.h"
#endif //_DEBUG
bool
CGameStateMain::On_WM_KEYDOWN(WPARAM wParam, LPARAM lParam) {
    /// ���ɿ� ���õ� �޼��� ó��...
    g_UserInputSystem.OnKeyDown(wParam, lParam);

    static float s_fScale = 1.0f;

    if (lParam & 0x40000000) {
        // ������ ���� �ִ� Ű��....
        return false;
    }

    switch (wParam) {
#ifdef _DEBUG
        case '8': {
            CObjFixedEvent* pObj = g_pObjMGR->GetEventObject(1);
            if (pObj == NULL)
                return 0;
            pObj->ExecEventScript(0);
        } break;
        case '9': {
            CObjFixedEvent* pObj = g_pObjMGR->GetEventObject(1);
            if (pObj == NULL)
                return 0;
            pObj->ExecEventScript(1);
        }
#endif //_DEBUG
        break;
            /*case VK_UP:
                g_pAVATAR->m_fHeightOfGround += 10.0f;
                break;

            case VK_DOWN:
                g_pAVATAR->m_fHeightOfGround -= 10.0f;
                break;*/

        case VK_CONTROL: {
            g_GameDATA.m_bShowDropItemInfo = true;
        } break;

        case VK_TAB: {
            if ((g_pAVATAR->Get_STATE() != CS_SIT) && (g_pAVATAR->Get_STATE() != CS_SITTING))
                g_pNet->Send_cli_TOGGLE(TOGGLE_TYPE_RUN);
        }
            return true;

        case 'C':
        case 'c': {

            if (GetAsyncKeyState(VK_CONTROL) < 0) {
                CTEditBox* pEditBox;
                pEditBox = g_itMGR.GetFocusEditBox();
                if (pEditBox != NULL) {
                    char* ptext = pEditBox->get_text();
                    if (ptext != NULL) {
                        CClipboardUtil::CopyStringtoClibboard(std::string(ptext));
                    }
                }
            }
        }
            return true;
        case 'V':
        case 'v': {
            if (GetAsyncKeyState(VK_CONTROL) < 0) {
                CTEditBox* pEditBox;
                pEditBox = g_itMGR.GetFocusEditBox();
                if (pEditBox != NULL) {
                    std::string ptext = CClipboardUtil::GetStringFromClibboard();
                    if (ptext.c_str() != NULL) {
                        pEditBox->Insert(ptext.c_str());
                    }
                }
            }
        }
            return true;

        default: {

        } break;
    }
    return false;
}

bool
CGameStateMain::On_WM_CHAR(WPARAM wParam, LPARAM lParam) {
    switch (wParam) {
        case VK_ESCAPE: {
            g_UserInputSystem.ClearMouseState();
        } break;
        case VK_RETURN:
            LogString(LOG_NORMAL, "VK_RETURN \n");

#ifdef __USE_IME
            m_IME.ClearString();
            m_IME.SetActive(true);
            m_IME.ToggleInputMode(true); // ������ �ѱ۷� ��ȯ ��Ų��.
#endif
            return true;
    }
    return false;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------

bool
CGameStateMain::On_WM_LBUTTONDOWN(WPARAM wParam, LPARAM lParam) {
    if (NULL == g_pAVATAR)
        return true;

    if (g_pAVATAR->Get_HP() <= 0)
        return true;

    if (!this->m_bPickedPOS)
        return true;

    /// �Է��� ������ ����ʹ� �������.
    if (g_pAVATAR->bCanUserInput()) {
        g_UserInputSystem.ClickObject(this->m_iPickedOBJ, this->m_PosPICK, wParam);
    } else {
        g_itMGR.AppendChatMsg(STR_DOING_SKILL_ACTION, IT_MGR::CHAT_TYPE_SYSTEM);
    }

    CGame::GetInstance().ResetAutoRun();
    return true;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief WM_RBUTTONDOWN
//    9/14 ���� �������.. (ī�޶� ��ũ�� �浹 )
//----------------------------------------------------------------------------------------------------

bool
CGameStateMain::On_WM_RBUTTONDOWN(WPARAM wParam, LPARAM lParam) {
    if (NULL == g_pAVATAR) {
        return true;
    }

    if (g_pAVATAR->Get_HP() <= 0) {
        return true;
    }

    if (!this->m_bPickedPOS) {
        return true;
    }

    if (!g_pAVATAR->bCanUserInput()) {
        return true;
    }

    /// �Է��� ������ ����ʹ� �������.
    g_UserInputSystem.RButtonDown(this->m_iPickedOBJ, this->m_PosPICK, wParam);
    CGame::GetInstance().ResetAutoRun();
    return true;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------

bool
CGameStateMain::On_WM_LBUTTONDBCLICK(WPARAM wParam, LPARAM lParam) {
    if (NULL == g_pAVATAR)
        return true;

    if (g_pAVATAR->Get_HP() <= 0)
        return true;

    if (!this->m_bPickedPOS)
        return true;

    /// �Է��� ������ ����ʹ� �������.
    if (g_pAVATAR->bCanUserInput()) {
        g_UserInputSystem.DBClickObject(this->m_iPickedOBJ, this->m_PosPICK, wParam);
    } else {
        g_itMGR.AppendChatMsg(STR_DOING_SKILL_ACTION, g_dwRED);
    }
    CGame::GetInstance().ResetAutoRun();
    return true;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------

bool
CGameStateMain::On_WM_MOUSEWHEEL(WPARAM wParam, LPARAM lParam) {
    short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

#ifdef _DEBUG
    if (g_GameDATA.m_bObserverCameraMode) {
        ObserverCameraZoomInOut(static_cast<float>(-zDelta));
        return true;
    }
#endif

    g_pCamera->Add_Distance(static_cast<float>(-zDelta));
    return true;
}
//-------------------------------------------------------------------------------------------

void
CGameStateMain::Pick_POSITION(/*LPARAM	lParam*/) {
    D3DXVECTOR3 RayOrig;
    D3DXVECTOR3 RayDir;

    ::getRay(m_ScreenPOS.m_nX,
        m_ScreenPOS.m_nY,
        &RayOrig.x,
        &RayOrig.y,
        &RayOrig.z,
        &RayDir.x,
        &RayDir.y,
        &RayDir.z);

    CGame::GetInstance().SetRayDir(RayDir);
    CGame::GetInstance().SetRayOrig(RayOrig);

    D3DXVECTOR3 PosPICKTerrain;

    float fDistanceTerrain = g_fMaxDistance;
    float fDistanceObject = g_fMaxDistance;

    fDistanceTerrain = g_pTerrain->Pick_POSITION(PosPICKTerrain);

    {
        m_iPickedOBJ = g_pTerrain->Pick_OBJECT(m_PosPICK, fDistanceObject);
        if (m_iPickedOBJ) {
            m_bPickedPOS = true;
            /// ������ �浹�߰�, �������� �浹�Ÿ��� ������Ʈ���� �浹�Ÿ����� �� �����ٸ�..
            if ((fDistanceTerrain > 0) && (fDistanceObject > fDistanceTerrain)) {
                m_PosPICK = PosPICKTerrain;
                m_iPickedOBJ = 0;
            }
        } else {
            /// ������Ʈ�ʹ� �浹�� ���� �������� �浹�� ����.
            if (fDistanceTerrain > 0) {
                m_PosPICK = PosPICKTerrain;
                m_bPickedPOS = true;
            } else {
                /// SKY ���� �浹 üũ
                HNODE hSky = CSkyDOME::GetHNODE();
                float fDistanceSky = g_fMaxDistance;
                const float fDefaultSkyDistance = 3000.0f;

                m_iPickedOBJ = 0;

                if (g_pAVATAR && hSky && CGameOBJ::IsIntersect(hSky, fDistanceSky)) {
                    m_bPickedPOS = true;
                    CGameOBJ::AdjustPickPOSITION(fDefaultSkyDistance);
                    m_PosPICK = CGameOBJ::Get_PickPOSITION();
                } else {
                    m_bPickedPOS = false;
                }
            }
        }
    }
}

void
CGameStateMain::UpdateCheckFrame() {
    CGame& refGame = CGame::GetInstance();
    CCursor& refCursor = CCursor::GetInstance();

    // if( !refGame.GetUseUserMouseFlag() )
    //	return;

    if ((refCursor.GetCurrCursorType() == CCursor::CURSOR_DEFAULT
            && refGame.GetCheckFrame() >= MOUSE_STOP_CHECK_FRAME)
        || (refCursor.GetCurrCursorType() != CCursor::CURSOR_DEFAULT
            && refGame.GetCheckFrame() >= MOUSE_STOP_CHECK_FRAME / 2)) {
        Pick_POSITION(/*this->m_sCurMsg.lParam */);

        CTargetManager::GetSingleton().SetMouseTargetObject(0);

        /// ���� ���콺 Ŀ���� �����Ѵ�.

        // ��ŷ�� ����� ����������� �Բ� üũ.
        // this->m_iPickedOBJ �� 0�̶�� ��ȿ���� ���� �ǰ�?
        // - 2004.01.17.zho
        // - 2004.02.25.nAvy : ���� ���콺�� �������̽����� ������϶��� Default
        // - 2004.07.13 nAvy : ��������ϰ�� �׻� ����Ŀ����
        if (refGame.GetRepairMode()) {
            refCursor.SetCursorType(CCursor::CURSOR_REPAIR);
        } else if (g_itMGR.GetState() == IT_MGR::STATE_APPRAISAL) {
            refCursor.SetCursorType(CCursor::CURSOR_APPRAISAL);
        } else if (GetAsyncKeyState(VK_RBUTTON) < 0) {
            // Dagnarus
            POINT ptMouse;
            refGame.Get_MousePos(ptMouse);
            int iCheckFlag = CheckMouseRegion(ptMouse.x, ptMouse.y);

            if (!g_itMGR.IsMouseOnInterface())
                refCursor.SetCursorType(CCursor::CURSOR_WHEEL);

            else
                refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
        } else {

            POINT ptMouse;
            refGame.Get_MousePos(ptMouse);
            int iCheckFlag = CheckMouseRegion(ptMouse.x, ptMouse.y);

            if (iCheckFlag & SCREEN_LEFT)
                refCursor.SetCursorType(CCursor::CURSOR_LEFT);
            else if (iCheckFlag & SCREEN_RIGHT)
                refCursor.SetCursorType(CCursor::CURSOR_RIGHT);
            else if (g_itMGR.IsMouseOnInterface())
                refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
            else if (m_bPickedPOS && (m_iPickedOBJ > 0)) {
                CGameOBJ* pObj = g_pObjMGR->m_pOBJECTS[this->m_iPickedOBJ];
                if (pObj) {
                    switch (pObj->Get_TYPE()) {
                        case OBJ_MOB: {
                            CObjCHAR* character = reinterpret_cast<CObjCHAR*>(pObj);
                            if (character->CanClickable()) {
                                if (character->is_pvp_enabled()) {
                                    refCursor.SetCursorType(CCursor::CURSOR_ATTACK);
                                    {
                                        CTargetManager::GetSingleton().SetMouseTargetObject(
                                            this->m_iPickedOBJ);
                                    }
                                } else {
                                    refCursor.SetCursorType(CCursor::CURSOR_NPC);
                                }
                            }
                        } break;
                        case OBJ_NPC: {
                            CObjCHAR* character = reinterpret_cast<CObjCHAR*>(pObj);
                            if (character->CanClickable()) {
                                if (character->is_pvp_enabled()) {
                                    refCursor.SetCursorType(CCursor::CURSOR_ATTACK);
                                } else {
                                    refCursor.SetCursorType(CCursor::CURSOR_NPC);
                                }
                            }
                        } break;
                        case OBJ_AVATAR: {
                            CObjCHAR* character = reinterpret_cast<CObjCHAR*>(pObj);
                            if (character->CanClickable()) {
                                if (g_pAVATAR->is_pvp_enabled()
                                    && CUserInputState::IsEnemy(character)) {
                                    refCursor.SetCursorType(CCursor::CURSOR_ATTACK);
                                } else {
                                    refCursor.SetCursorType(CCursor::CURSOR_USER);
                                }
                            }
                        } break;
                        case OBJ_ITEM: {
                            CInfo MouseInfo;
                            MouseInfo.Clear();
                            CObjITEM* pItem = (CObjITEM*)g_pObjMGR->m_pOBJECTS[this->m_iPickedOBJ];
                            MouseInfo.AddString(pItem->Get_NAME(),
                                CItem::GetItemNameColor(pItem->m_ITEM.GetTYPE(),
                                    pItem->m_ITEM.GetItemNO()));
                            refCursor.SetCursorType(CCursor::CURSOR_ITEM_PICK, &MouseInfo);
                            break;
                        }
                        case OBJ_GROUND:
                        case OBJ_CNST:
                            refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
                            break;
                        default: {
                            refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
                        } break;
                    }
                }
            } else {
                refCursor.SetCursorType(CCursor::CURSOR_DEFAULT);
            }
        }
        refGame.ResetCheckFrame();
        return;
    }

    refGame.IncreseCheckFrame();
}
#define SCREEN_CHECK_WIDTH 1
int
CGameStateMain::CheckMouseRegion(int x, int y) {
    int iCheckFlag = 0;

    /// ����
    if (x < SCREEN_CHECK_WIDTH)
        iCheckFlag |= SCREEN_LEFT;

    /// ������
    if (x > (g_pCApp->GetWIDTH() - 1 - SCREEN_CHECK_WIDTH))
        iCheckFlag |= SCREEN_RIGHT;

    /*
    /// ����
    if( y < SCREEN_CHECK_WIDTH )
        iCheckFlag |= SCREEN_UP;

    /// �Ʒ���
    if( y > ( g_GameDATA.m_nScrHeight - SCREEN_CHECK_WIDTH ) )
        iCheckFlag |= SCREEN_DOWN;
    */

    return iCheckFlag;
}

void
CGameStateMain::UpdateCameraPositionByMouse() {
    POINT ptCurrMouse;
    CGame::GetInstance().Get_MousePos(ptCurrMouse);
    int iCheckFlag = CheckMouseRegion(ptCurrMouse.x, ptCurrMouse.y);

    if (iCheckFlag & SCREEN_LEFT) {
        g_pCamera->Add_YAW(-CAMERA_MOVE_SPEED);
        return;
    }

    if (iCheckFlag & SCREEN_RIGHT) {
        g_pCamera->Add_YAW(CAMERA_MOVE_SPEED);
        return;
    }
    /*
        if( iCheckFlag & SCREEN_UP )
            g_pCamera->Add_PITCH( -CAMERA_MOVE_SPEED );

        if( iCheckFlag & SCREEN_DOWN )
            g_pCamera->Add_PITCH( CAMERA_MOVE_SPEED );*/
}

int
CGameStateMain::ProcWndMsgInstant(unsigned uiMsg, WPARAM wParam, LPARAM lParam) {
    if (CGameState::ProcWndMsgInstant(uiMsg, wParam, lParam)) {
        return 1;
    }

    POINT ptMouse = {LOWORD(lParam), HIWORD(lParam)};
    switch (uiMsg) {
        case WM_LBUTTONDOWN: {
            // 아이템 미리보기 패널( 맨 위에 그려짐 ) — 클릭 전부 소비( 닫기 / 드래그 시작 )
            if (g_UIMed.ItemPreviewLButtonDown(ptMouse.x, ptMouse.y))
                return 1;
            // 몬스터 인스펙터 패널 위 클릭은 전부 소비( 닫기 / 드래그 시작 ) —
            // 클릭이 월드로 새어나가 이동/공격하지 않게 한다.
            if (g_UIMed.MonsterInspectorLButtonDown(ptMouse.x, ptMouse.y))
                return 1;
            // 데미지 미터 패널 — 버튼( 뷰 전환/리셋/닫기 ) 또는 드래그 시작, 클릭 전부 소비
            if (g_UIMed.DamageMeterLButtonDown(ptMouse.x, ptMouse.y))
                return 1;
            // 소환몹 정보 패널을 잡으면 드래그를 시작하고 클릭을 소비해
            // 아바타 이동/UI 클릭으로 새어나가지 않게 한다.
            if (g_UIMed.SummonPanelLButtonDown(ptMouse.x, ptMouse.y))
                return 1;
            break;
        }
        case WM_LBUTTONUP: {
            if (g_UIMed.ItemPreviewLButtonUp(ptMouse.x, ptMouse.y))
                return 1;
            if (g_UIMed.MonsterInspectorLButtonUp(ptMouse.x, ptMouse.y))
                return 1;
            if (g_UIMed.DamageMeterLButtonUp(ptMouse.x, ptMouse.y))
                return 1;
            if (g_UIMed.SummonPanelLButtonUp(ptMouse.x, ptMouse.y))
                return 1;
            break;
        }
        case WM_MOUSEMOVE: {
            // 몬스터 인스펙터: hover 좌표 추적( 드랍 아이콘 이름 표시용, 소비하지 않음 ).
            // 드래그 중일 때만 이동을 소비한다.
            if (g_UIMed.ItemPreviewMouseMove(ptMouse.x, ptMouse.y))
                return 1;
            if (g_UIMed.MonsterInspectorMouseMove(ptMouse.x, ptMouse.y))
                return 1;
            // 데미지 미터 패널 드래그 중이면 좌버튼 이동을 패널 이동으로 소비한다.
            if ((wParam & MK_LBUTTON) && g_UIMed.IsDamageMeterDragging()) {
                g_UIMed.DamageMeterMouseMove(ptMouse.x, ptMouse.y);
                return 1;
            }
            // 소환몹 패널 드래그 중이면 좌버튼 이동을 패널 이동으로 소비한다.
            if ((wParam & MK_LBUTTON) && g_UIMed.IsSummonPanelDragging()) {
                g_UIMed.SummonPanelMouseMove(ptMouse.x, ptMouse.y);
                return 1;
            }
#ifdef _DEBUG
            if (g_GameDATA.m_bObserverCameraMode) {
                if ((wParam & MK_RBUTTON)) {
                    ObserverCameraTransform((short)(ptMouse.x - m_PosRButtonClick.m_nX),
                        (short)(ptMouse.y - m_PosRButtonClick.m_nY));
                    /*if ( ptMouse.x - m_PosRButtonClick.m_nX )
                        g_pCamera->Add_YAW( (short)(ptMouse.x - m_PosRButtonClick.m_nX) );
                    if ( ptMouse.y - m_PosRButtonClick.m_nY )
                        g_pCamera->Add_PITCH( (short)(ptMouse.y - m_PosRButtonClick.m_nY) );*/

                    m_PosRButtonClick.m_nX = (short)ptMouse.x;
                    m_PosRButtonClick.m_nY = (short)ptMouse.y;
                }
            } else
#endif
            {
                if ((wParam & MK_RBUTTON)) {
                    if (ptMouse.x - m_PosRButtonClick.m_nX)
                        g_pCamera->Add_YAW((short)(ptMouse.x - m_PosRButtonClick.m_nX));
                    if (ptMouse.y - m_PosRButtonClick.m_nY)
                        g_pCamera->Add_PITCH((short)(ptMouse.y - m_PosRButtonClick.m_nY));

                    m_PosRButtonClick.m_nX = (short)ptMouse.x;
                    m_PosRButtonClick.m_nY = (short)ptMouse.y;

                    // Consume right-drag mousemoves synchronously so they never
                    // enter m_WndMsgQ. Windows delivers WM_MOUSEMOVE at 100-500 Hz
                    // during active drag; each queued message would otherwise walk
                    // every open dialog + icon for hit-testing in ProcMouseInput ->
                    // g_itMGR.MsgProc -> CITStateNormal::Process. No UI reacts to
                    // mousemove while RMB is held for camera control, so dropping
                    // the message is safe (m_ptCurrMouse is already updated in
                    // AddWndMsgQ before this call).
                    return 1;
                }
            }
            break;
        }
        case WM_RBUTTONDOWN:
            m_PosRButtonClick.m_nX = (short)ptMouse.x;
            m_PosRButtonClick.m_nY = (short)ptMouse.y;
            break;
        default:
            break;
    }
    return 0;
}
