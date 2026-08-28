#include "stdafx.h"

#include "FrameProfiler.h"

#include <windows.h>

namespace FrameProfiler {

namespace {

/// Frames averaged before publishing. Long enough to be stable, short enough that the
/// display still tracks a camera turn.
const int kWindowFrames = 30;

LONGLONG s_freq = 0;

LONGLONG s_slot_start[SLOT_COUNT];
double s_slot_accum[SLOT_COUNT];  // summed over the current window
double s_slot_frame[SLOT_COUNT];  // this frame only -- what the spike log reports
bool s_slot_open[SLOT_COUNT];

/// Spike log state. Threshold 0 means the whole feature is inert.
unsigned int s_spike_log_ms = 0;

/// Mirrors zz_manager::FLUSH_KIND_COUNT (terrain, mesh, texture, material, other).
/// Deliberately a local copy rather than a shared constant: the engine's enum is
/// the authority, and getImmediateFlushKind() bounds-checks its argument, so a
/// kind added there goes unreported here instead of reading past the array.
const int kFlushKindCount = 5;

/// The held candidate for the current rate-limit window.
///
/// The limiter keeps the **worst** frame of a window, not the first. Keeping the
/// first is the obvious implementation and it is actively misleading: the first
/// capture with this tool reported a 24 ms frame and discarded a >=104 ms one that
/// arrived 250 ms later, so the log's headline number was the *least* interesting
/// frame in the burst. Emission is therefore deferred to the end of the window.
struct SpikeSnapshot {
    double frame_ms;
    double accounted;
    double slot[SLOT_COUNT];
    float avg_ms;
    bool flush_sampled;
    float flush_ms;
    int flush_count;
    int flush_kind[kFlushKindCount];
    int pkt_count;
    double pkt_worst_ms;
    unsigned short pkt_worst_type;
    int spawn_count;
    double spawn_step[SPAWN_STEP_COUNT];
};
SpikeSnapshot s_worst;
bool s_worst_valid = false;
int s_window_spikes = 0;
DWORD s_window_start = 0;

/// Per-frame packet-drain detail: how many packets were handled, and the single
/// worst one. Fed by NotePacket() from CNetwork::Proc.
int s_pkt_count = 0;
double s_pkt_worst_ms = 0.0;
unsigned short s_pkt_worst_type = 0;

/// Per-frame character-spawn detail: how many CreateCHAR calls ran and where
/// their time went. Fed by NoteSpawn/NoteSpawnStep from CObjCHAR::CreateCHAR.
int s_spawn_count = 0;
double s_spawn_step[SPAWN_STEP_COUNT];

/// Engine immediate-flush counters, sampled by CaptureFlushStats() while they are
/// still valid. See the header for why EndFrame() cannot read them itself.
bool s_flush_sampled = false;
float s_flush_ms = 0.0f;
int s_flush_count = 0;
int s_flush_kind[kFlushKindCount];

/// Emitting a line costs a formatted log record and an FFI crossing, which is
/// itself long enough to matter on a frame that is already late. A hitch worth
/// investigating is not a 130 Hz event, so one line per 250 ms is plenty. The
/// window's other spikes are reported as a count, so a burst still states its
/// true size rather than looking like a single event.
const DWORD kSpikeLogWindowMs = 250;

LONGLONG s_frame_start = 0;
double s_frame_accum = 0.0;
double s_frame_worst = 0.0;
int s_frames = 0;

float s_published[SLOT_COUNT];
float s_published_total = 0.0f;
float s_published_max = 0.0f;
float s_published_accounted = 0.0f;

inline LONGLONG Now() {
    LARGE_INTEGER t;
    ::QueryPerformanceCounter(&t);
    return t.QuadPart;
}

inline double ToMs(LONGLONG ticks) {
    if (s_freq == 0)
        return 0.0;
    return (double)ticks * 1000.0 / (double)s_freq;
}

///
/// One line per hitch, carrying that frame's own phase split.
///
/// Read it in three steps:
///
///   1. Which phase is large? That is the answer, and the rest is detail.
///        netin   - window messages, packet drain, input. Split further by the
///                  netin[...] group: msg = the Win32 pump, gdat = g_GameDATA.Update,
///                  pkt = g_pNet->Proc draining the queue (packet handlers run
///                  synchronously in here, so a zone change is charged to it),
///                  inp = ProcInput.
///        logic   - client game logic; split further by the logic[...] group.
///        scnupd  - engine scene update: transforms, culling, skeletal animation.
///                  Scales with visible object count; no rendering change touches it.
///        shadow  - beginScene(), which runs the whole shadow-map pass, and is also
///                  where much of the immediate resource flush happens -- so check
///                  flush= before blaming the shadow pass itself.
///        render  - draw submission. High means CPU-bound on draw calls, unless
///                  flush= accounts for it: flushes land here too.
///        present - endScene() + swapBuffers(). This is where waiting for the GPU
///                  surfaces, because D3D9 buffers commands. With VSYNC=1 it is also
///                  where the frame-rate cap is paid, so it is expected to be large
///                  and is only interesting when a *spike* lands in it.
///        oth     - inside the frame but outside every bracket. Large in states other
///                  than CGameStateMain (nothing fills slots there), which is why
///                  zone-load frames report almost everything in oth. In-game it is a
///                  real finding: work no bracket covers.
///
///   2. Is flush= large? Then it is the streaming path, already covered by
///      [VIDEO] STREAM_SPIKE_LOG_MS and tuned with TERRAIN_INSERTS_PER_FRAME /
///      LOAD_BUDGET_US. It is a whole-frame total spread across scnupd, shadow and
///      render, so it says streaming was involved without saying which phase carried
///      it -- and it can legitimately exceed any single phase. `flush=unsampled`
///      means the frame never reached the sample point (lost focus, or the scene did
///      not begin) -- not that flushing was free.
///
///   3. Compare against avg=, the last published window mean. A 40 ms frame against
///      an 8 ms average is a hitch; against a 35 ms average it is just a slow scene,
///      and the fix is throughput, not spike-hunting. It reads 0.0 until the first
///      30-frame window has completed, which only affects the opening frames.
///
/// `others=N` is how many further frames crossed the threshold inside the same 250 ms
/// window. This line is the worst of them, not the first -- see SpikeSnapshot.
///
/// Zone-in and the first frames after a warp are legitimately enormous. Expect a
/// cluster there and judge steady-state play instead.
///
void EmitSpike(const SpikeSnapshot& s, int others) {
    LOG_INFO("Frame spike: {:.1f} ms (avg {:.1f}) | "
             "netin={:.1f} logic={:.1f} scnupd={:.1f} shadow={:.1f} render={:.1f} "
             "ui={:.1f} present={:.1f} oth={:.1f} | "
             "netin[msg={:.1f} gdat={:.1f} pkt={:.1f} inp={:.1f}] | "
             "pkt[n={} worst=0x{:04x}/{:.1f}ms] | "
             "spawn[n={} mdl={:.1f} parts={:.1f} bone={:.1f} rest={:.1f}] | "
             "logic[obj={:.1f} terr={:.1f} fx={:.1f} uiupd={:.1f}] | "
             "flush={} | others={}",
        s.frame_ms,
        s.avg_ms,
        s.slot[SLOT_NETINPUT],
        s.slot[SLOT_LOGIC],
        s.slot[SLOT_SCENE_UPDATE],
        s.slot[SLOT_SHADOW],
        s.slot[SLOT_RENDER],
        s.slot[SLOT_UI],
        s.slot[SLOT_PRESENT],
        s.frame_ms - s.accounted,
        s.slot[SLOT_NETIN_MSG],
        s.slot[SLOT_NETIN_GAMEDATA],
        s.slot[SLOT_NETIN_PACKET],
        s.slot[SLOT_NETIN_INPUT],
        s.pkt_count,
        s.pkt_worst_type,
        s.pkt_worst_ms,
        s.spawn_count,
        s.spawn_step[SPAWN_MODELNODE],
        s.spawn_step[SPAWN_PARTS],
        s.spawn_step[SPAWN_BONEFX],
        s.spawn_step[SPAWN_REST],
        s.slot[SLOT_LOGIC_OBJPROC],
        s.slot[SLOT_LOGIC_TERRAIN],
        s.slot[SLOT_LOGIC_EFFECTS],
        s.slot[SLOT_LOGIC_UIUPD],
        s.flush_sampled
            ? fmt::format("{:.1f}ms/{}n [terrain={} mesh={} tex={} mat={} other={}]",
                  s.flush_ms,
                  s.flush_count,
                  s.flush_kind[0],
                  s.flush_kind[1],
                  s.flush_kind[2],
                  s.flush_kind[3],
                  s.flush_kind[4])
            : std::string("unsampled"),
        others);
}

/// Hold this frame if it is the worst of the current window, opening a new window
/// when none is running.
void NoteSpike(double frame_ms, double frame_accounted, DWORD now) {
    if (!s_worst_valid) {
        s_window_start = now;
        s_window_spikes = 0;
    }
    ++s_window_spikes;

    if (s_worst_valid && frame_ms <= s_worst.frame_ms)
        return;

    s_worst.frame_ms = frame_ms;
    s_worst.accounted = frame_accounted;
    for (int i = 0; i < SLOT_COUNT; ++i)
        s_worst.slot[i] = s_slot_frame[i];
    s_worst.avg_ms = s_published_total;
    s_worst.flush_sampled = s_flush_sampled;
    s_worst.flush_ms = s_flush_ms;
    s_worst.flush_count = s_flush_count;
    for (int i = 0; i < kFlushKindCount; ++i)
        s_worst.flush_kind[i] = s_flush_kind[i];
    s_worst.pkt_count = s_pkt_count;
    s_worst.pkt_worst_ms = s_pkt_worst_ms;
    s_worst.pkt_worst_type = s_pkt_worst_type;
    s_worst.spawn_count = s_spawn_count;
    for (int i = 0; i < SPAWN_STEP_COUNT; ++i)
        s_worst.spawn_step[i] = s_spawn_step[i];
    s_worst_valid = true;
}

} // namespace

void BeginFrame() {
    if (s_freq == 0) {
        LARGE_INTEGER f;
        ::QueryPerformanceFrequency(&f);
        s_freq = f.QuadPart;
        for (int i = 0; i < SLOT_COUNT; ++i) {
            s_slot_accum[i] = 0.0;
            s_slot_open[i] = false;
            s_published[i] = 0.0f;
        }
        for (int i = 0; i < kFlushKindCount; ++i)
            s_flush_kind[i] = 0;
    }
    for (int i = 0; i < SLOT_COUNT; ++i)
        s_slot_frame[i] = 0.0;
    s_flush_sampled = false;
    s_flush_ms = 0.0f;
    s_flush_count = 0;
    for (int i = 0; i < kFlushKindCount; ++i)
        s_flush_kind[i] = 0;
    s_pkt_count = 0;
    s_pkt_worst_ms = 0.0;
    s_pkt_worst_type = 0;
    s_spawn_count = 0;
    for (int i = 0; i < SPAWN_STEP_COUNT; ++i)
        s_spawn_step[i] = 0.0;
    s_frame_start = Now();
}

void Begin(Slot slot) {
    if (slot < 0 || slot >= SLOT_COUNT)
        return;
    s_slot_start[slot] = Now();
    s_slot_open[slot] = true;
}

void End(Slot slot) {
    if (slot < 0 || slot >= SLOT_COUNT)
        return;
    if (!s_slot_open[slot]) // End without a matching Begin
        return;
    // Into the per-frame bucket, which EndFrame() folds into the window total.
    // SLOT_LOGIC is bracketed several times per frame, hence '+=' rather than '='.
    s_slot_frame[slot] += ToMs(Now() - s_slot_start[slot]);
    s_slot_open[slot] = false;
}

void SetSpikeLogMs(unsigned int ms) {
    s_spike_log_ms = ms;
}

bool IsSpikeLogEnabled() {
    return s_spike_log_ms > 0;
}

void NoteSpawn() {
    ++s_spawn_count;
}

void NoteSpawnStep(SpawnStep step, double ms) {
    if (step < 0 || step >= SPAWN_STEP_COUNT)
        return;
    s_spawn_step[step] += ms;
}

void NotePacket(unsigned short type, double ms) {
    ++s_pkt_count;
    if (ms > s_pkt_worst_ms) {
        s_pkt_worst_ms = ms;
        s_pkt_worst_type = type;
    }
}

void CaptureFlushStats() {
    if (s_spike_log_ms == 0) // inert unless the log is on
        return;
    s_flush_ms = ::getImmediateFlushMs();
    s_flush_count = ::getImmediateFlushCount();
    for (int i = 0; i < kFlushKindCount; ++i)
        s_flush_kind[i] = ::getImmediateFlushKind(i);
    s_flush_sampled = true;
}

void EndFrame() {
    if (s_freq == 0 || s_frame_start == 0)
        return;

    const double frame_ms = ToMs(Now() - s_frame_start);
    s_frame_accum += frame_ms;
    if (frame_ms > s_frame_worst)
        s_frame_worst = frame_ms;
    ++s_frames;

    // Per-frame buckets fold into the window totals here, then the spike check reads
    // them while they still hold this frame alone.
    double frame_accounted = 0.0;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        s_slot_accum[i] += s_slot_frame[i];
        // Sub-slots break down SLOT_LOGIC rather than adding to it -- counting them
        // would double-count and drive `oth` negative.
        if (i < SLOT_FIRST_SUB)
            frame_accounted += s_slot_frame[i];
    }

    if (s_spike_log_ms > 0) {
        const DWORD now = ::GetTickCount();
        if (frame_ms >= (double)s_spike_log_ms)
            NoteSpike(frame_ms, frame_accounted, now);
        // Checked every frame, not only on spikes: the held candidate must still be
        // emitted once the window closes even if nothing further crosses the
        // threshold, which is the common case for an isolated hitch.
        if (s_worst_valid && (DWORD)(now - s_window_start) >= kSpikeLogWindowMs) {
            EmitSpike(s_worst, s_window_spikes - 1);
            s_worst_valid = false;
        }
    }

    if (s_frames < kWindowFrames)
        return;

    const double inv = 1.0 / (double)s_frames;
    double window_accounted = 0.0;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        s_published[i] = (float)(s_slot_accum[i] * inv);
        // Sub-slots are a breakdown *inside* SLOT_LOGIC, so counting them here would
        // double-count that time and make "oth" go negative.
        if (i < SLOT_FIRST_SUB)
            window_accounted += s_slot_accum[i];
        s_slot_accum[i] = 0.0;
    }
    s_published_accounted = (float)(window_accounted * inv);
    s_published_total = (float)(s_frame_accum * inv);
    s_published_max = (float)s_frame_worst;

    s_frame_accum = 0.0;
    s_frame_worst = 0.0;
    s_frames = 0;
}

float GetMs(Slot slot) {
    if (slot < 0 || slot >= SLOT_COUNT)
        return 0.0f;
    return s_published[slot];
}

float GetTotalMs() {
    return s_published_total;
}

float GetMaxTotalMs() {
    return s_published_max;
}

float GetAccountedMs() {
    return s_published_accounted;
}

} // namespace FrameProfiler
