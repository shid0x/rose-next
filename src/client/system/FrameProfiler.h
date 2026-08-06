#ifndef _FRAME_PROFILER_H_
#define _FRAME_PROFILER_H_

///
/// Per-frame wall-clock breakdown, in milliseconds.
///
/// Built because the render statistics on the debug HUD count *API calls*, and three
/// rounds of optimisation removed a lot of calls without moving the frame rate at all.
/// Call counts are not time. This measures time.
///
/// The split is chosen to answer one question: where do the milliseconds go?
///
///   logic   - client game logic: object Proc, AI, network, UI update, terrain streaming
///   scnupd  - engine scene update: transforms, frustum cull, skeletal animation
///   shadow  - beginScene(), which also runs the whole shadow map pass
///   render  - renderScene(), i.e. 3D draw submission
///   ui      - Render_GameMENU() + dev UI overlays
///   present - endScene() + swapBuffers()
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
namespace FrameProfiler {

enum Slot {
    SLOT_LOGIC = 0,
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
