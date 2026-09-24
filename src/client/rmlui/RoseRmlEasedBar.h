#ifndef _ROSE_RML_EASED_BAR_H_
#define _ROSE_RML_EASED_BAR_H_

/**
 * A gauge fill that eases from where it is drawn now towards a new target over
 * 400 ms, restarting from its current position whenever the target moves --
 * the 667 CTGuage behaviour ( Get_PercentValue, 0x190 ms ). Numbers shown next
 * to the bar stay exact and immediate; only the fill eases.
 *
 * Call Reset() when the bar starts showing something else ( another target, a
 * panel coming back into view ) so it snaps instead of sliding from stale data.
 */

#include <windows.h>

struct RoseRmlEasedBar {
    static const DWORD kEaseMs = 400;

    float fFrom;
    float fTo;
    float fShown;
    DWORD dwStart;
    bool bPrimed;

    RoseRmlEasedBar(): fFrom(0.0f), fTo(0.0f), fShown(0.0f), dwStart(0), bPrimed(false) {}

    void Reset() { bPrimed = false; }

    /// Advances towards fTarget ( 0..100 ). Returns true when fShown changed,
    /// i.e. when the bound width needs dirtying.
    bool Step(float fTarget, DWORD dwNow) {
        if (!bPrimed) {
            fFrom = fTo = fShown = fTarget;
            dwStart = dwNow;
            bPrimed = true;
            return true;
        }

        if (fTarget != fTo) {
            fFrom = fShown;
            fTo = fTarget;
            dwStart = dwNow;
        }

        float t = (float)(dwNow - dwStart) / (float)kEaseMs;
        if (t > 1.0f)
            t = 1.0f;
        const float fNext = fFrom + (fTo - fFrom) * t;
        if (fNext == fShown)
            return false;

        fShown = fNext;
        return true;
    }
};

#endif /// _ROSE_RML_EASED_BAR_H_
