#ifndef _FRAME_PROFILER_H_
#define _FRAME_PROFILER_H_

///
/// Per-frame wall-clock breakdown, in milliseconds.
///
/// Built because the render statistics on the debug HUD count *API calls*, and three
/// rounds of optimisation removed a lot of calls without moving the frame rate at all.
/// Call counts are not time. This measures time.
///
/// The frame is bracketed in CGame::GameLoop, NOT in a game state's Update(). The loop
/// does real work either side of Update() -- window messages, g_pNet->Proc() draining the
/// whole packet queue synchronously, input dispatch, ProcCommand -- and bracketing only
/// Update() would leave that outside both the slots *and* the total, so `oth` could never
/// reveal it. A zone-in or spawn burst would then be a silent false negative in exactly
/// the `max` column that exists to catch hitches.
///
/// The split is chosen to answer one question: where do the milliseconds go?
///
///   netin   - window messages, packet queue drain, input dispatch
///   logic   - client game logic: object Proc, AI, UI update, terrain streaming
///   scnupd  - engine scene update: transforms, frustum cull, skeletal animation
///   shadow  - beginScene(), which also runs the whole shadow map pass
///   render  - renderScene(), i.e. 3D draw submission
///   ui      - Render_GameMENU() + dev UI overlays
///   present - endScene() + swapBuffers()
///
/// Slots are only filled by CGameStateMain; in other states everything lands in `oth`.
///
/// Reading it: a large `render` means CPU-bound on draw submission. A large `present`
/// means the GPU (or vsync) is the limit and submitting fewer draws will not help --
/// D3D9 buffers commands, so Present is where waiting for the GPU shows up. A large
/// `scnupd` means animation/culling, which no rendering change can touch.
///
/// Values are averaged over a fixed window and published at the end of it, so the HUD
/// reads a completed window rather than a single noisy frame. `max` is the worst single
/// frame total in that window -- hitches hide in the average.
///
/// That averaging is also why the spike log below exists. A hitch is one frame; it shows
/// up as `max` for a single 30-frame window and is then gone, which is unreadable while
/// playing and unrecoverable afterwards. SetSpikeLogMs() writes that frame's *own* phase
/// split to client.log, so a stutter names the phase that caused it instead of leaving
/// you to reproduce it while staring at a HUD row.
///
namespace FrameProfiler {

enum Slot {
    SLOT_NETINPUT = 0,
    SLOT_LOGIC,
    SLOT_SCENE_UPDATE,
    SLOT_SHADOW,
    SLOT_RENDER,
    SLOT_UI,
    SLOT_PRESENT,

    /// Breakdown *within* SLOT_LOGIC -- these overlap it rather than adding to it,
    /// because measurement showed logic is 61% of the cost of a crowded scene (0.4ms
    /// empty vs 13.0ms with 397 monsters) while all rendering phases together were 20%.
    /// Do not include them when summing phases; see GetAccountedMs().
    SLOT_LOGIC_OBJPROC,   // g_pObjMGR->ProcOBJECT() -- scales with object count
    SLOT_LOGIC_TERRAIN,   // g_pTerrain->SetCenterPosition() -- streaming
    SLOT_LOGIC_EFFECTS,   // effect list, bullets, SFX manager
    SLOT_LOGIC_UIUPD,     // g_UIMed.Update()

    /// Breakdown *within* SLOT_NETINPUT, same overlapping rule as the logic group.
    /// Added because netin turned out to be one of the largest spike phases in
    /// practice (201 ms on a solo server) while being four unrelated jobs sharing
    /// one bracket -- which named a phase without naming a cause.
    SLOT_NETIN_MSG,       // g_pCApp->GetMessage() -- Win32 message pump
    SLOT_NETIN_GAMEDATA,  // g_GameDATA.Update()
    SLOT_NETIN_PACKET,    // g_pNet->Proc() -- drains the whole packet queue
    SLOT_NETIN_INPUT,     // ProcInput() -- input dispatch

    SLOT_COUNT,

    /// First sub-slot; everything from here up is a breakdown, not a phase.
    SLOT_FIRST_SUB = SLOT_LOGIC_OBJPROC
};

/// Call once at the top of the frame.
void BeginFrame();

/// Bracket a phase. Nesting the same slot is not supported (and not needed).
void Begin(Slot slot);
void End(Slot slot);

/// Call once at the very end of the frame.
void EndFrame();

/// Threshold in ms for the whole-frame spike log, from [VIDEO] FRAME_SPIKE_LOG_MS.
/// 0 (the default) disables it entirely -- no sampling, no formatting, no cost.
///
/// This is deliberately separate from [VIDEO] STREAM_SPIKE_LOG_MS, which triggers on
/// immediate-flush time alone: that one is silent for any hitch the streaming path did
/// not cause, and a silent diagnostic is indistinguishable from a healthy frame.
///
/// Spikes are rate-limited, and the limiter keeps the **worst** frame of each window
/// rather than the first. That distinction is not cosmetic: the first implementation
/// kept the first and counted the rest, and in the very first capture it reported a
/// 24 ms frame while discarding a >=104 ms one that landed 250 ms later. A hitch log
/// that drops the biggest hitch is worse than none, because it looks like evidence.
void SetSpikeLogMs(unsigned int ms);

/// Sample the engine's per-frame immediate-flush counters into the current frame, so a
/// logged spike can say in one line whether the streaming path was responsible.
///
/// MUST be called after the scene has rendered and BEFORE swapBuffers(). The engine rolls
/// those counters over in zz_system::sleep(), which swapBuffers() calls -- so sampling
/// them from EndFrame(), which runs later still, would record a clean zero on precisely
/// the spike frames this log exists to explain. Frames where it is not called are
/// reported as `flush=unsampled` rather than as zero, because "no data" and "no flush
/// work" are different findings and printing the second for the first is a lie.
void CaptureFlushStats();

/// True when FRAME_SPIKE_LOG_MS is non-zero. Call sites that would pay for
/// instrumentation (a timer around every packet, say) check this first so the
/// diagnostic costs nothing when it is off.
bool IsSpikeLogEnabled();

/// Record the cost of handling one received packet.
///
/// netin split down to `pkt` (g_pNet->Proc) and stopped there, which named the
/// phase but not the work: the drain is a `while (Peek_Packet)` loop that runs
/// every handler synchronously, so one expensive handler and a thousand cheap
/// ones look identical from outside. The spike line reports the count and the
/// single worst packet type, which is what distinguishes "too many packets" from
/// "one packet does something enormous".
void NotePacket(unsigned short type, double ms);

/// Steps of one CObjCHAR::CreateCHAR, accumulated per frame.
///
/// GSV_NPC_CHAR / GSV_MOB_CHAR handlers measured at 5-27 ms for a single packet,
/// and a spawn is four unrelated jobs (skeleton + motion + model node, body
/// parts, bone effects, scene insert). Which one dominates decides the fix:
/// a per-type first-load cost wants the asset warmed ahead of time, a per-
/// instance cost wants the work itself made cheaper.
enum SpawnStep {
    SPAWN_MODELNODE = 0, // Get_SKELETON + Get_MOTION(0) + loadModel
    SPAWN_PARTS,         // CreatePARTS: meshes and materials for every body part
    SPAWN_BONEFX,        // CreateBoneEFFECT
    SPAWN_REST,          // InsertToScene, SetCMD_STOP, DropFromSky
    SPAWN_STEP_COUNT
};
void NoteSpawnStep(SpawnStep step, double ms);
void NoteSpawn();

/// Smoothed values from the last completed window. Safe to call at any time.
float GetMs(Slot slot);
float GetTotalMs();
float GetMaxTotalMs();

/// Total of the bracketed slots. Compare against GetTotalMs() -- a gap means real frame
/// time is going somewhere none of the brackets cover.
float GetAccountedMs();

/// RAII helper.
struct Scope {
    Slot m_slot;
    explicit Scope(Slot slot) : m_slot(slot) { Begin(slot); }
    ~Scope() { End(m_slot); }
};

} // namespace FrameProfiler

#endif // _FRAME_PROFILER_H_
