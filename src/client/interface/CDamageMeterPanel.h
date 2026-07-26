#ifndef _CDAMAGEMETERPANEL_
#define _CDAMAGEMETERPANEL_

#include <windows.h>

#include "GameData/CDamageMeter.h"

/**
 * Damage meter overlay (view layer only — all data comes from
 * CDamageMeter::BuildSnapshot(), so this panel can be restyled or replaced
 * without touching the meter core).
 *
 * - Direct sprite/font drawing like CSummonInfoPanel — no XML resource.
 * - Toggled with the local "/dps" chat command (never sent to the server).
 * - Header buttons: [>] cycle view, [R] reset data, [X] close.
 * - Views: damage done (self + party), my skills, damage taken.
 * - Left-click drag moves the panel (position kept for the session only).
 */
class CDamageMeterPanel {
public:
    CDamageMeterPanel();

    void Toggle() { m_bVisible = !m_bVisible; }
    void Show() { m_bVisible = true; }
    void Hide() { m_bVisible = false; }
    bool IsVisible() const { return m_bVisible; }

    /// Called inside the HUD sprite block (CUIMediator::Draw).
    void Draw();

    /// Instant-path input (CGameStateMain::ProcWndMsgInstant). Screen pixels.
    /// Returns true when consumed.
    bool OnLButtonDown(int x, int y);
    bool OnMouseMove(int x, int y);
    bool OnLButtonUp(int x, int y);

    bool IsDragging() const { return m_bDragging; }

private:
    enum t_MeterView {
        VIEW_DAMAGE_DONE = 0,
        VIEW_MY_SKILLS,
        VIEW_DAMAGE_TAKEN,
        VIEW_COUNT,
    };

    void EnsureDefaultPosition();
    void RefreshSnapshot();
    bool GetPanelRect(RECT& rcOut) const;
    const std::vector<CDamageMeter::Row>& ActiveRows() const;
    const char* ViewName() const;

    bool m_bVisible;
    int m_iView;

    POINT m_ptPos;
    bool m_bPositionInit;
    bool m_bDragging;
    POINT m_ptDragGrab;

    CDamageMeter::FightSnapshot m_Snapshot;
    DWORD m_dwLastSnapshotTime;
    int m_iLastPanelH; ///< height drawn last frame, reused for hit-testing
};

#endif // _CDAMAGEMETERPANEL_
