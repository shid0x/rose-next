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
bool s_slot_open[SLOT_COUNT];

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
    }
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
    s_slot_accum[slot] += ToMs(Now() - s_slot_start[slot]);
    s_slot_open[slot] = false;
}

void EndFrame() {
    if (s_freq == 0 || s_frame_start == 0)
        return;

    const double frame_ms = ToMs(Now() - s_frame_start);
    s_frame_accum += frame_ms;
    if (frame_ms > s_frame_worst)
        s_frame_worst = frame_ms;
    ++s_frames;

    if (s_frames < kWindowFrames)
        return;

    const double inv = 1.0 / (double)s_frames;
    double accounted = 0.0;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        s_published[i] = (float)(s_slot_accum[i] * inv);
        // Sub-slots are a breakdown *inside* SLOT_LOGIC, so counting them here would
        // double-count that time and make "oth" go negative.
        if (i < SLOT_FIRST_SUB)
            accounted += s_slot_accum[i];
        s_slot_accum[i] = 0.0;
    }
    s_published_accounted = (float)(accounted * inv);
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
