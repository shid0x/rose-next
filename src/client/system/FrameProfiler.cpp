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
DWORD s_spike_last_tick = 0;
int s_spike_suppressed = 0;

/// Mirrors zz_manager::FLUSH_KIND_COUNT (terrain, mesh, texture, material, other).
/// Deliberately a local copy rather than a shared constant: the engine's enum is
/// the authority, and getImmediateFlushKind() bounds-checks its argument, so a
/// kind added there goes unreported here instead of reading past the array.
const int kFlushKindCount = 5;

/// Engine immediate-flush counters, sampled by CaptureFlushStats() while they are
/// still valid. See the header for why EndFrame() cannot read them itself.
bool s_flush_sampled = false;
float s_flush_ms = 0.0f;
int s_flush_count = 0;
int s_flush_kind[kFlushKindCount];

/// Emitting a line costs a formatted log record and an FFI crossing, which is
/// itself long enough to matter on a frame that is already late. A hitch worth
/// investigating is not a 130 Hz event, so one line per 250 ms is plenty -- and
/// the suppressed count is carried into the next line rather than dropped, so a
/// burst still reports its true size.
const DWORD kSpikeLogMinIntervalMs = 250;

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
///        netin   - packet drain / window messages / input. A spawn burst or a
///                  large inventory update lands here.
///        logic   - client game logic; use the logic[...] breakdown to split it.
///        scnupd  - engine scene update: transforms, culling, skeletal animation.
///                  Scales with visible object count; no rendering change touches it.
///        shadow  - beginScene(), which runs the whole shadow-map pass, and is also
///                  where the immediate resource flush happens -- so check flush=
///                  before blaming the shadow pass itself.
///        render  - draw submission. High means CPU-bound on draw calls.
///        present - endScene() + swapBuffers(). This is where waiting for the GPU
///                  surfaces, because D3D9 buffers commands. With VSYNC=1 it is also
///                  where the frame-rate cap is paid, so it is expected to be large
///                  and is only interesting when a *spike* lands in it.
///        oth     - inside the frame but outside every bracket. A large `oth` means
///                  the phases do not cover the cost and the brackets need extending;
///                  it is a real finding, not noise.
///
///   2. Is flush= large? Then it is the streaming path, already covered by
///      [VIDEO] STREAM_SPIKE_LOG_MS and tuned with TERRAIN_INSERTS_PER_FRAME /
///      LOAD_BUDGET_US. `flush=unsampled` means the frame never reached the sample
///      point (lost focus, or the scene did not begin) -- not that flushing was free.
///
///   3. Compare against avg=, the last published window mean. A 40 ms frame against
///      an 8 ms average is a hitch; against a 35 ms average it is just a slow scene,
///      and the fix is throughput, not spike-hunting. It reads 0.0 until the first
///      30-frame window has completed, which only affects the opening frames.
///
/// Zone-in and the first frames after a warp are legitimately enormous. Expect a
/// cluster there and judge steady-state play instead.
///
void LogSpike(double frame_ms, double frame_accounted) {
    const DWORD now = ::GetTickCount();
    if ((DWORD)(now - s_spike_last_tick) < kSpikeLogMinIntervalMs) {
        ++s_spike_suppressed;
        return;
    }
    s_spike_last_tick = now;

    const int suppressed = s_spike_suppressed;
    s_spike_suppressed = 0;

    LOG_INFO("Frame spike: {:.1f} ms (avg {:.1f}) | "
             "netin={:.1f} logic={:.1f} scnupd={:.1f} shadow={:.1f} render={:.1f} "
             "ui={:.1f} present={:.1f} oth={:.1f} | "
             "logic[obj={:.1f} terr={:.1f} fx={:.1f} uiupd={:.1f}] | "
             "flush={} | suppressed={}",
        frame_ms,
        s_published_total,
        s_slot_frame[SLOT_NETINPUT],
        s_slot_frame[SLOT_LOGIC],
        s_slot_frame[SLOT_SCENE_UPDATE],
        s_slot_frame[SLOT_SHADOW],
        s_slot_frame[SLOT_RENDER],
        s_slot_frame[SLOT_UI],
        s_slot_frame[SLOT_PRESENT],
        frame_ms - frame_accounted,
        s_slot_frame[SLOT_LOGIC_OBJPROC],
        s_slot_frame[SLOT_LOGIC_TERRAIN],
        s_slot_frame[SLOT_LOGIC_EFFECTS],
        s_slot_frame[SLOT_LOGIC_UIUPD],
        s_flush_sampled
            ? fmt::format("{:.1f}ms/{}n [terrain={} mesh={} tex={} mat={} other={}]",
                  s_flush_ms,
                  s_flush_count,
                  s_flush_kind[0],
                  s_flush_kind[1],
                  s_flush_kind[2],
                  s_flush_kind[3],
                  s_flush_kind[4])
            : std::string("unsampled"),
        suppressed);
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

    if (s_spike_log_ms > 0 && frame_ms >= (double)s_spike_log_ms)
        LogSpike(frame_ms, frame_accounted);

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
