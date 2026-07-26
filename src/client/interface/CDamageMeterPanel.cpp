#include "stdafx.h"

#include "CDamageMeterPanel.h"

#include "..\\Game.h"
#include "../CApplication.h"
#include "../Object.h"
#include "../CObjUSER.h"
#include "Common/IO_Skill.h"

#include "OverlayPanelUtil.h"
#include "CTDrawImpl.h"
#include "IO_ImageRes.h"
#include "tgamectrl/resourcemgr.h"

namespace {

/// Layout + style constants (grouped here for easy modding).
const int kPanelW = 252;
const int kPad = 8;
const int kHeaderH = 22;
const int kInfoH = 16;
const int kRowH = 16;
const int kFooterH = 16;
const int kMaxRows = 10;
const int kBtnW = 18;   ///< header buttons [>][R][X]
const int kBtnH = 16;

const DWORD kSnapshotRefreshMs = 500;

const D3DCOLOR kColBg = D3DCOLOR_ARGB(215, 255, 255, 255);      ///< panel bg tint
const D3DCOLOR kColTitle = D3DCOLOR_ARGB(255, 255, 255, 255);
const D3DCOLOR kColInfo = D3DCOLOR_ARGB(255, 180, 180, 180);
const D3DCOLOR kColRow = D3DCOLOR_ARGB(255, 235, 235, 235);
const D3DCOLOR kColSelf = D3DCOLOR_ARGB(255, 255, 210, 80);     ///< avatar's own row
const D3DCOLOR kColBtn = D3DCOLOR_ARGB(255, 200, 200, 200);
const D3DCOLOR kColBar = D3DCOLOR_ARGB(90, 255, 255, 255);      ///< row bar (UI00_GUAGE_RED tint)
const D3DCOLOR kColLive = D3DCOLOR_ARGB(255, 120, 255, 110);
const D3DCOLOR kColIdle = D3DCOLOR_ARGB(255, 160, 160, 160);

} // namespace

CDamageMeterPanel::CDamageMeterPanel()
    : m_bVisible(false)
    , m_iView(VIEW_DAMAGE_DONE)
    , m_bPositionInit(false)
    , m_bDragging(false)
    , m_dwLastSnapshotTime(0)
    , m_iLastPanelH(kHeaderH + kInfoH + kRowH + kFooterH + kPad) {
    m_ptPos.x = 0;
    m_ptPos.y = 0;
    m_ptDragGrab.x = 0;
    m_ptDragGrab.y = 0;
}

void
CDamageMeterPanel::EnsureDefaultPosition() {
    if (m_bPositionInit)
        return;

    /// Default: left side, below the top HUD elements.
    m_ptPos.x = 16;
    m_ptPos.y = 220;
    m_bPositionInit = true;
}

void
CDamageMeterPanel::RefreshSnapshot() {
    const DWORD dwNow = g_GameDATA.GetGameTime();
    if (m_dwLastSnapshotTime != 0 && (dwNow - m_dwLastSnapshotTime) < kSnapshotRefreshMs)
        return;

    m_dwLastSnapshotTime = dwNow;
    CDamageMeter::GetInstance().BuildSnapshot(m_Snapshot);
}

const std::vector<CDamageMeter::Row>&
CDamageMeterPanel::ActiveRows() const {
    switch (m_iView) {
        case VIEW_MY_SKILLS:
            return m_Snapshot.OutgoingSkillsSelf;
        case VIEW_DAMAGE_TAKEN:
            return m_Snapshot.IncomingByAttacker;
        case VIEW_DAMAGE_DONE:
        default:
            return m_Snapshot.OutgoingByAttacker;
    }
}

const char*
CDamageMeterPanel::ViewName() const {
    switch (m_iView) {
        case VIEW_MY_SKILLS:
            return "My Skills";
        case VIEW_DAMAGE_TAKEN:
            return "Damage Taken";
        case VIEW_DAMAGE_DONE:
        default:
            return "Damage Done";
    }
}

bool
CDamageMeterPanel::GetPanelRect(RECT& rcOut) const {
    if (!m_bVisible)
        return false;

    rcOut.left = m_ptPos.x;
    rcOut.top = m_ptPos.y;
    rcOut.right = m_ptPos.x + kPanelW;
    rcOut.bottom = m_ptPos.y + m_iLastPanelH;
    return true;
}

void
CDamageMeterPanel::Draw() {
    if (!m_bVisible || g_pAVATAR == NULL)
        return;

    EnsureDefaultPosition();
    RefreshSnapshot();

    const std::vector<CDamageMeter::Row>& rows = ActiveRows();
    const int iRowCount = min((int)rows.size(), kMaxRows);
    const int iBodyRows = max(iRowCount, 1); ///< keep one line for the empty note
    m_iLastPanelH = kHeaderH + kInfoH + iBodyRows * kRowH + kFooterH + kPad;

    const int x = m_ptPos.x;
    const int y = m_ptPos.y;

    OverlayPanel::DrawPanelBg(x, y, kPanelW, m_iLastPanelH, kColBg);

    /// --- header: title + [>][R][X] -----------------------------------------
    char szBuf[128];
    _snprintf(szBuf, sizeof(szBuf), "DPS - %s", ViewName());
    szBuf[sizeof(szBuf) - 1] = '\0';
    OverlayPanel::DrawTextAt(x + kPad, y + 3, kPanelW - kPad * 2 - kBtnW * 3, kBtnH, kColTitle,
        DT_LEFT | DT_VCENTER, FONT_NORMAL_BOLD, szBuf);

    const int iBtnY = y + 3;
    const int iBtnX0 = x + kPanelW - kPad - kBtnW * 3; ///< [>]
    const int iBtnX1 = x + kPanelW - kPad - kBtnW * 2; ///< [R]
    const int iBtnX2 = x + kPanelW - kPad - kBtnW;     ///< [X]
    OverlayPanel::DrawTextAt(iBtnX0, iBtnY, kBtnW, kBtnH, kColBtn, DT_CENTER | DT_VCENTER,
        FONT_NORMAL_BOLD, ">");
    OverlayPanel::DrawTextAt(iBtnX1, iBtnY, kBtnW, kBtnH, kColBtn, DT_CENTER | DT_VCENTER,
        FONT_NORMAL_BOLD, "R");
    OverlayPanel::DrawTextAt(iBtnX2, iBtnY, kBtnW, kBtnH, kColBtn, DT_CENTER | DT_VCENTER,
        FONT_NORMAL_BOLD, "X");

    /// --- info line: fight duration + live/ended ----------------------------
    const DWORD dwDurMs = m_Snapshot.DurationMs();
    const int iDurSec = (int)(dwDurMs / 1000);
    _snprintf(szBuf, sizeof(szBuf), "Fight %d:%02d", iDurSec / 60, iDurSec % 60);
    szBuf[sizeof(szBuf) - 1] = '\0';
    OverlayPanel::DrawTextAt(x + kPad, y + kHeaderH, kPanelW / 2, kInfoH, kColInfo,
        DT_LEFT | DT_VCENTER, FONT_NORMAL, szBuf);
    OverlayPanel::DrawTextAt(x + kPanelW / 2, y + kHeaderH, kPanelW / 2 - kPad, kInfoH,
        m_Snapshot.bActive ? kColLive : kColIdle, DT_RIGHT | DT_VCENTER, FONT_NORMAL,
        m_Snapshot.bActive ? "LIVE" : "ended");

    /// --- rows ---------------------------------------------------------------
    const int iRowsY = y + kHeaderH + kInfoH;

    if (iRowCount == 0) {
        OverlayPanel::DrawTextAt(x + kPad, iRowsY, kPanelW - kPad * 2, kRowH, kColInfo,
            DT_LEFT | DT_VCENTER, FONT_NORMAL, "No combat data.  ( /dps to hide )");
    } else {
        __int64 iViewTotal = 0;
        __int64 iTopTotal = 1;
        for (int i = 0; i < (int)rows.size(); ++i)
            iViewTotal += rows[i].iTotal;
        if (iViewTotal < 1)
            iViewTotal = 1;
        if (!rows.empty() && rows[0].iTotal > 0)
            iTopTotal = rows[0].iTotal;

        const double dDurSec = (dwDurMs > 0) ? (dwDurMs / 1000.0) : 1.0;

        CResourceMgr* pRes = CResourceMgr::GetInstance();
        const int iRedNID = pRes->GetImageNID(IMAGE_RES_UI, "UI00_GUAGE_RED");

        const char* pSelfName = g_pAVATAR->Get_NAME();

        for (int i = 0; i < iRowCount; ++i) {
            const CDamageMeter::Row& row = rows[i];
            const int iRowY = iRowsY + i * kRowH;

            /// proportional bar behind the row text
            const int iBarW =
                (int)((__int64)(kPanelW - kPad * 2) * row.iTotal / iTopTotal);
            if (iBarW > 0 && iRedNID >= 0)
                g_DrawImpl.DrawFit(x + kPad, iRowY + 2, IMAGE_RES_UI, iRedNID, iBarW, kRowH - 4,
                    kColBar);

            const bool bSelfRow = (m_iView == VIEW_DAMAGE_DONE && pSelfName != NULL
                && row.strName == pSelfName);

            OverlayPanel::DrawTextAt(x + kPad + 2, iRowY, kPanelW - kPad * 2 - 4, kRowH,
                bSelfRow ? kColSelf : kColRow, DT_LEFT | DT_VCENTER, FONT_NORMAL,
                row.strName.c_str());

            const int iPct = (int)(row.iTotal * 100 / iViewTotal);
            const int iDps = (int)(row.iTotal / dDurSec);
            if (m_iView == VIEW_DAMAGE_DONE) {
                _snprintf(szBuf, sizeof(szBuf), "%I64d (%d/s, %d%%)", row.iTotal, iDps, iPct);
            } else {
                _snprintf(szBuf, sizeof(szBuf), "%I64d (x%d, %d%%)", row.iTotal, row.iHits, iPct);
            }
            szBuf[sizeof(szBuf) - 1] = '\0';
            OverlayPanel::DrawTextAt(x + kPad + 2, iRowY, kPanelW - kPad * 2 - 4, kRowH,
                bSelfRow ? kColSelf : kColRow, DT_RIGHT | DT_VCENTER, FONT_NORMAL, szBuf);
        }
    }

    /// --- footer: biggest hit of the view ------------------------------------
    int iMaxHit = 0;
    int iMaxHitSkill = 0;
    std::string strMaxOwner;
    for (int i = 0; i < (int)rows.size(); ++i) {
        if (rows[i].iMaxHit > iMaxHit) {
            iMaxHit = rows[i].iMaxHit;
            iMaxHitSkill = rows[i].iMaxHitSkill;
            strMaxOwner = rows[i].strName;
        }
    }

    if (iMaxHit > 0) {
        const char* pSkillName =
            (iMaxHitSkill > 0) ? SKILL_NAME(iMaxHitSkill) : NULL;
        if (pSkillName != NULL && pSkillName[0] != '\0') {
            _snprintf(szBuf, sizeof(szBuf), "Best: %d - %s (%s)", iMaxHit, strMaxOwner.c_str(),
                pSkillName);
        } else {
            _snprintf(szBuf, sizeof(szBuf), "Best: %d - %s", iMaxHit, strMaxOwner.c_str());
        }
        szBuf[sizeof(szBuf) - 1] = '\0';
        OverlayPanel::DrawTextAt(x + kPad, y + m_iLastPanelH - kFooterH - 2, kPanelW - kPad * 2,
            kFooterH, kColInfo, DT_LEFT | DT_VCENTER, FONT_NORMAL, szBuf);
    }
}

bool
CDamageMeterPanel::OnLButtonDown(int x, int y) {
    RECT rc;
    if (!GetPanelRect(rc))
        return false;

    if (x < rc.left || x > rc.right || y < rc.top || y > rc.bottom)
        return false;

    /// header buttons
    const int iBtnY = m_ptPos.y + 3;
    const int iBtnX0 = m_ptPos.x + kPanelW - kPad - kBtnW * 3;
    const int iBtnX1 = m_ptPos.x + kPanelW - kPad - kBtnW * 2;
    const int iBtnX2 = m_ptPos.x + kPanelW - kPad - kBtnW;

    if (y >= iBtnY && y <= iBtnY + kBtnH) {
        if (x >= iBtnX2 && x <= iBtnX2 + kBtnW) {
            m_bVisible = false;
            return true;
        }
        if (x >= iBtnX1 && x <= iBtnX1 + kBtnW) {
            CDamageMeter::GetInstance().Reset();
            m_dwLastSnapshotTime = 0; ///< force refresh on next draw
            return true;
        }
        if (x >= iBtnX0 && x <= iBtnX0 + kBtnW) {
            m_iView = (m_iView + 1) % VIEW_COUNT;
            return true;
        }
    }

    /// anywhere else on the panel: start dragging + consume so the click
    /// never leaks into avatar movement / world targeting
    m_bDragging = true;
    m_ptDragGrab.x = x - m_ptPos.x;
    m_ptDragGrab.y = y - m_ptPos.y;
    return true;
}

bool
CDamageMeterPanel::OnMouseMove(int x, int y) {
    if (!m_bDragging)
        return false;

    m_ptPos.x = x - m_ptDragGrab.x;
    m_ptPos.y = y - m_ptDragGrab.y;

    const int iScrW = g_pCApp->GetWIDTH();
    const int iScrH = g_pCApp->GetHEIGHT();
    if (m_ptPos.x < 0)
        m_ptPos.x = 0;
    if (m_ptPos.y < 0)
        m_ptPos.y = 0;
    if (m_ptPos.x > iScrW - kPanelW)
        m_ptPos.x = iScrW - kPanelW;
    if (m_ptPos.y > iScrH - kHeaderH)
        m_ptPos.y = iScrH - kHeaderH;

    return true;
}

bool
CDamageMeterPanel::OnLButtonUp(int /*x*/, int /*y*/) {
    if (!m_bDragging)
        return false;

    m_bDragging = false;
    return true;
}
