#include "stdafx.h"

#include "zoneobjects.h"

#include "cobjfixed.h"
#include "object.h"
#include "zz_interface.h"
#include "CApplication.h"
#include "stunfeedback.h"
#include "CObjUSER.h"
#include "IO_Terrain.h"
#include "interface/CTDrawImpl.h"
#include "interface/IO_ImageRes.h"
#include "tgamectrl/resourcemgr.h"

namespace {
const unsigned long FADE_MS = 2000;
} // namespace

CZoneObjects&
CZoneObjects::Instance() {
    static CZoneObjects s_Instance;
    return s_Instance;
}

bool
CZoneObjects::Valid(const Piece& piece) const {
    return piece.pObj && g_pObjMGR && g_pObjMGR->Get_OBJECT(piece.nObjIdx) == piece.pObj;
}

void
CZoneObjects::Destroy(Group& group) {
    for (const Piece& piece: group.pieces) {
        if (this->Valid(piece))
            g_pObjMGR->Del_Object(piece.nObjIdx);
    }
    group.pieces.clear();
}

void
CZoneObjects::Apply(unsigned char btGroup, bool bOn, const std::vector<Spec>& specs) {
    CMazeMap::Instance().SetWalls(btGroup, bOn, specs); // a crypt's walls are also its map
    std::map<unsigned char, Group>::iterator it = m_Groups.find(btGroup);
    if (!bOn) {
        if (it == m_Groups.end() || it->second.bFading)
            return;
        it->second.bFading = true;
        it->second.dwFadeStart = ::GetTickCount();
        LogString(LOG_NORMAL, "zone objects: group %d down, %d pieces fading\n", btGroup, (int)it->second.pieces.size());
        return;
    }
    if (it != m_Groups.end()) {
        if (!it->second.bFading)
            return; // already up
        this->Destroy(it->second); // up again mid-fade: rebuild at full strength
        m_Groups.erase(it);
    }
    Group group;
    group.bFading = false;
    group.dwFadeStart = 0;
    for (const Spec& s: specs) {
        D3DVECTOR pos = {s.fX, s.fY, s.fZ};
        D3DXQUATERNION rot;
        D3DXVECTOR3 axis(0.0f, 0.0f, 1.0f);
        D3DXQuaternionRotationAxis(&rot, &axis, D3DXToRadian((float)s.wRotDeg));
        D3DVECTOR scale = {s.wScaleX ? s.wScaleX / 100.0f : 1.0f,
            s.wScaleY ? s.wScaleY / 100.0f : 1.0f,
            s.wScaleZ ? s.wScaleZ / 100.0f : 1.0f};
        // Kind 1 is the map's own invisible collision panel (IFO lump 11 objects):
        // nothing to see, everything to bump into.
        const short nIdx = (short)(s.btKind == 1
                ? g_pObjMGR->Add_CollisionBox(0, pos, rot, scale)
                : g_pObjMGR->Add_GndTREE((short)s.wObjID, pos, rot, scale));
        if (nIdx <= 0) {
            LogString(LOG_NORMAL, "zone objects: group %d: deco %d could not be made\n", btGroup, (int)s.wObjID);
            continue;
        }
        CGameOBJ* pObj = g_pObjMGR->Get_OBJECT(nIdx);
        if (!pObj) {
            continue;
        }
        pObj->InsertToScene();
        Piece piece = {nIdx, pObj};
        group.pieces.push_back(piece);
    }
    LogString(LOG_NORMAL, "zone objects: group %d up, %d of %d pieces\n", btGroup, (int)group.pieces.size(), (int)specs.size());
    m_Groups[btGroup] = group;
}

void
CZoneObjects::Update() {
    CMazeMap::Instance().Update();
    if (m_Groups.empty())
        return;
    const unsigned long now = ::GetTickCount();
    for (std::map<unsigned char, Group>::iterator it = m_Groups.begin(); it != m_Groups.end();) {
        Group& group = it->second;
        if (!group.bFading) {
            ++it;
            continue;
        }
        const unsigned long age = now - group.dwFadeStart;
        if (age >= FADE_MS) {
            this->Destroy(group);
            it = m_Groups.erase(it);
            continue;
        }
        const float fVisibility = 1.0f - (float)age / (float)FADE_MS;
        for (const Piece& piece: group.pieces) {
            if (this->Valid(piece))
                ::setVisibilityRecursive(((CObjFIXED*)piece.pObj)->GetRootZNODE(), fVisibility);
        }
        ++it;
    }
}

void
CZoneObjects::Forget() {
    m_Groups.clear();
    CMazeMap::Instance().Forget();
}

//-------------------------------------------------------------------------------------------------
namespace {
// The server's (catacomb_maze.h, catacombs.cpp): keep in step.
const short kCryptZones[2] = {90, 91};
const float kMazeSW = 504500.f; // the grid's south-west corner, both axes
const float kMazeCell = 1500.f;
const int kRoomFirst = 6; // the boss hall: cells 6..9 both ways (ROOM_MIN .. ROOM_MAX)
const unsigned char kWallGroupFirst = 10, kWallGroupLast = 13;
const unsigned long kWallsSettleMs = 1500; // the four groups arrive one after the other

bool
InCrypt() {
    if (g_pTerrain == NULL)
        return false;
    const short nZone = g_pTerrain->GetZoneNO();
    return nZone == kCryptZones[0] || nZone == kCryptZones[1];
}

bool
IsRoom(int iX, int iY) {
    return iX >= kRoomFirst && iY >= kRoomFirst;
}
} // namespace

CMazeMap&
CMazeMap::Instance() {
    static CMazeMap s_Map;
    return s_Map;
}

CMazeMap::CMazeMap(): m_nWalls(0), m_uWallHash(0), m_uSeenHash(0), m_dwWallsAt(0), m_uVersion(1) {
    memset(m_bWallV, 0, sizeof(m_bWallV));
    memset(m_bWallH, 0, sizeof(m_bWallH));
    memset(m_bSeen, 0, sizeof(m_bSeen));
}

void
CMazeMap::Bounds(float& fWest, float& fSouth, float& fEast, float& fNorth) {
    fWest = fSouth = kMazeSW;
    fEast = fNorth = kMazeSW + N * kMazeCell;
}

void
CMazeMap::SetWalls(unsigned char btGroup, bool bOn, const std::vector<CZoneObjects::Spec>& specs) {
    if (btGroup < kWallGroupFirst || btGroup > kWallGroupLast)
        return;
    if (bOn)
        m_Groups[btGroup] = specs;
    else
        m_Groups.erase(btGroup);
    this->Rebuild();
}

void
CMazeMap::Forget() {
    m_Groups.clear();
    this->Rebuild();
}

/// The closed edges, from the walls' places: a wall stands on a grid line, in the middle
/// of the edge it closes. Which way it is turned is not needed.
void
CMazeMap::Rebuild() {
    memset(m_bWallV, 0, sizeof(m_bWallV));
    memset(m_bWallH, 0, sizeof(m_bWallH));
    m_nWalls = 0;
    unsigned int uHash = 2166136261u;
    for (std::map<unsigned char, std::vector<CZoneObjects::Spec> >::const_iterator it = m_Groups.begin();
         it != m_Groups.end(); ++it) {
        for (size_t i = 0; i < it->second.size(); ++i) {
            const CZoneObjects::Spec& s = it->second[i];
            if (s.btKind != 0)
                continue;
            const float gx = (s.fX - kMazeSW) / kMazeCell, gy = (s.fY - kMazeSW) / kMazeCell;
            const int iLineX = (int)floorf(gx + 0.5f), iLineY = (int)floorf(gy + 0.5f);
            int iCode = -1;
            if (fabsf(gx - iLineX) < 0.1f) { // on a north-south line, beside row floor(gy)
                const int iRow = (int)floorf(gy);
                if (iLineX >= 0 && iLineX <= N && iRow >= 0 && iRow < N) {
                    m_bWallV[iLineX][iRow] = true;
                    iCode = iLineX * 64 + iRow;
                }
            } else if (fabsf(gy - iLineY) < 0.1f) { // on an east-west line, under column floor(gx)
                const int iCol = (int)floorf(gx);
                if (iLineY >= 0 && iLineY <= N && iCol >= 0 && iCol < N) {
                    m_bWallH[iCol][iLineY] = true;
                    iCode = 4096 + iCol * 64 + iLineY;
                }
            }
            if (iCode >= 0) {
                m_nWalls++;
                uHash += (unsigned int)iCode * 2654435761u; // order-free: the groups come in any order
            }
        }
    }
    m_uWallHash = uHash ^ (g_pTerrain ? (unsigned int)g_pTerrain->GetZoneNO() * 40503u : 0u);
    m_dwWallsAt = ::GetTickCount();
    m_uVersion++;
}

bool
CMazeMap::Active() const {
    return m_nWalls > 0 && InCrypt();
}

/// Whether the way from cell (iX, iY) one step (iDX, iDY) is shut: a wall, or the maze's edge.
bool
CMazeMap::Closed(int iX, int iY, int iDX, int iDY) const {
    const int nX = iX + iDX, nY = iY + iDY;
    if (nX < 0 || nY < 0 || nX >= N || nY >= N)
        return true;
    if (iDX != 0)
        return m_bWallV[iDX > 0 ? iX + 1 : iX][iY];
    return m_bWallH[iX][iDY > 0 ? iY + 1 : iY];
}

void
CMazeMap::See(int iX, int iY) {
    if (iX < 0 || iY < 0 || iX >= N || iY >= N || m_bSeen[iX][iY])
        return;
    m_bSeen[iX][iY] = true;
    m_uVersion++;
    if (IsRoom(iX, iY)) { // a hall is seen whole
        for (int x = kRoomFirst; x < N; ++x) {
            for (int y = kRoomFirst; y < N; ++y)
                m_bSeen[x][y] = true;
        }
    }
}

void
CMazeMap::Update() {
    if (!this->Active() || g_pAVATAR == NULL)
        return;
    if (::GetTickCount() - m_dwWallsAt < kWallsSettleMs)
        return; // the maze is still arriving
    if (m_uSeenHash != m_uWallHash) {
        // another maze than the one that was walked: it starts dark
        m_uSeenHash = m_uWallHash;
        memset(m_bSeen, 0, sizeof(m_bSeen));
        m_uVersion++;
    }
    const int iX = (int)floorf((g_pAVATAR->Get_CurPOS().x - kMazeSW) / kMazeCell);
    const int iY = (int)floorf((g_pAVATAR->Get_CurPOS().y - kMazeSW) / kMazeCell);
    if (iX < 0 || iY < 0 || iX >= N || iY >= N)
        return;
    this->See(iX, iY);
    // and what it opens onto: the next cell down each open way
    static const int kStep[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int k = 0; k < 4; ++k) {
        if (!this->Closed(iX, iY, kStep[k][0], kStep[k][1]))
            this->See(iX + kStep[k][0], iY + kStep[k][1]);
    }
}

void
CMazeMap::List(std::vector<Rect>& floors, std::vector<Rect>& walls) const {
    floors.clear();
    walls.clear();
    if (m_uSeenHash != m_uWallHash)
        return; // nothing of this maze has been seen yet
    Rect r;
    // the floor: runs of seen cells along each row, the hall apart
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N;) {
            if (!m_bSeen[x][y]) {
                ++x;
                continue;
            }
            const bool bRoom = IsRoom(x, y);
            const int x0 = x;
            while (x < N && m_bSeen[x][y] && IsRoom(x, y) == bRoom)
                ++x;
            r.x0 = kMazeSW + x0 * kMazeCell;
            r.x1 = kMazeSW + x * kMazeCell;
            r.y0 = kMazeSW + y * kMazeCell;
            r.y1 = r.y0 + kMazeCell;
            r.bRoom = bRoom;
            floors.push_back(r);
        }
    }
    // the walls: every shut side of a seen cell, runs along each grid line
    r.bRoom = false;
    for (int iLine = 0; iLine <= N; ++iLine) {
        // east-west line under row iLine: the north side of row iLine - 1, the south side of row iLine
        for (int x = 0; x < N;) {
            const bool bBelow = iLine > 0 && m_bSeen[x][iLine - 1] && this->Closed(x, iLine - 1, 0, 1);
            const bool bAbove = iLine < N && m_bSeen[x][iLine] && this->Closed(x, iLine, 0, -1);
            if (!bBelow && !bAbove) {
                ++x;
                continue;
            }
            const int x0 = x;
            while (x < N
                   && ((iLine > 0 && m_bSeen[x][iLine - 1] && this->Closed(x, iLine - 1, 0, 1))
                       || (iLine < N && m_bSeen[x][iLine] && this->Closed(x, iLine, 0, -1))))
                ++x;
            r.x0 = kMazeSW + x0 * kMazeCell;
            r.x1 = kMazeSW + x * kMazeCell;
            r.y0 = r.y1 = kMazeSW + iLine * kMazeCell;
            walls.push_back(r);
        }
        // north-south line west of column iLine
        for (int y = 0; y < N;) {
            const bool bWest = iLine > 0 && m_bSeen[iLine - 1][y] && this->Closed(iLine - 1, y, 1, 0);
            const bool bEast = iLine < N && m_bSeen[iLine][y] && this->Closed(iLine, y, -1, 0);
            if (!bWest && !bEast) {
                ++y;
                continue;
            }
            const int y0 = y;
            while (y < N
                   && ((iLine > 0 && m_bSeen[iLine - 1][y] && this->Closed(iLine - 1, y, 1, 0))
                       || (iLine < N && m_bSeen[iLine][y] && this->Closed(iLine, y, -1, 0))))
                ++y;
            r.y0 = kMazeSW + y0 * kMazeCell;
            r.y1 = kMazeSW + y * kMazeCell;
            r.x0 = r.x1 = kMazeSW + iLine * kMazeCell;
            walls.push_back(r);
        }
    }
}

//-------------------------------------------------------------------------------------------------
namespace {
const D3DCOLOR kAlertDanger = D3DCOLOR_ARGB(255, 255, 120, 56); ///< as the floor's red shapes
const D3DCOLOR kAlertSoak = D3DCOLOR_ARGB(255, 96, 255, 152); ///< as its green ones
const int kAlertW = 900; ///< room for a line of 32 px letters
const int kAlertH = 44;
const float kAlertTopRatio = 0.13f; ///< of the screen height: over the STUNNED alert (0.28)
const int kAlertBarW = 220;
const int kAlertBarH = 8;
const int kAlertBarGap = 2;
const int kAlertStep = kAlertH + kAlertBarGap + kAlertBarH + 8;
const unsigned long kAlertPopMs = 180; ///< the words land from 1.7x to their size
const float kAlertPopScale = 0.7f;
const size_t kAlertMax = 3;
} // namespace

CZoneAlerts&
CZoneAlerts::Instance() {
    static CZoneAlerts s_Alerts;
    return s_Alerts;
}

void
CZoneAlerts::Add(const char* szText, unsigned long dwMS, unsigned char btStyle, short nSound) {
    if (szText == NULL || !szText[0] || dwMS == 0)
        return;
    Alert a;
    a.text = szText;
    a.dwStart = ::GetTickCount();
    a.dwMS = dwMS;
    a.btStyle = btStyle;
    // the same words again replace the ones showing (a mechanic re-announced)
    for (size_t i = 0; i < m_Alerts.size();) {
        if (m_Alerts[i].text == a.text)
            m_Alerts.erase(m_Alerts.begin() + i);
        else
            ++i;
    }
    m_Alerts.insert(m_Alerts.begin(), a);
    if (m_Alerts.size() > kAlertMax)
        m_Alerts.resize(kAlertMax);
    if (nSound > 0 && g_pSoundLIST)
        g_pSoundLIST->IDX_PlaySound(nSound);
}

void
CZoneAlerts::Draw() {
    const unsigned long now = ::GetTickCount();
    for (size_t i = 0; i < m_Alerts.size();) {
        if (now - m_Alerts[i].dwStart >= m_Alerts[i].dwMS)
            m_Alerts.erase(m_Alerts.begin() + i);
        else
            ++i;
    }
    const unsigned int hFont = CStunFeedback::Instance().Font();
    if (m_Alerts.empty() || !hFont || g_pCApp == NULL)
        return;

    const float fCenterX = g_pCApp->GetWIDTH() * 0.5f;
    CResourceMgr* pRes = CResourceMgr::GetInstance();
    for (size_t i = 0; i < m_Alerts.size(); ++i) {
        const Alert& a = m_Alerts[i];
        const bool bSoak = a.btStyle == ZONE_WARN_SOAK;
        const unsigned long dwAge = now - a.dwStart;
        const float fTop = (float)((int)(g_pCApp->GetHEIGHT() * kAlertTopRatio) + (int)i * kAlertStep);

        float fScale = 1.f;
        if (dwAge < kAlertPopMs) {
            const float fLeft = 1.f - (float)dwAge / kAlertPopMs;
            fScale += kAlertPopScale * fLeft * fLeft;
        }
        D3DXVECTOR2 scale(fScale, fScale);
        D3DXVECTOR2 center(kAlertW * 0.5f, kAlertH * 0.5f);
        D3DXVECTOR2 pos((float)(int)(fCenterX - kAlertW * 0.5f), fTop);
        D3DXMATRIX mat;
        D3DXMatrixTransformation2D(&mat, &center, 0.f, &scale, NULL, 0.f, &pos);
        ::setTransformSprite(mat);
        RECT rc = {0, 0, kAlertW, kAlertH};
        ::drawFont(hFont, true, &rc, bSoak ? kAlertSoak : kAlertDanger, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
            a.text.c_str());

        // the time left to do it
        const int iBarX = (int)(fCenterX - kAlertBarW * 0.5f);
        const int iBarY = (int)fTop + kAlertH + kAlertBarGap;
        g_DrawImpl.DrawFit(iBarX, iBarY, IMAGE_RES_UI, pRes->GetImageNID(IMAGE_RES_UI, "UI00_GUAGE_BACKGROUND"),
            kAlertBarW, kAlertBarH, D3DCOLOR_ARGB(255, 255, 255, 255));
        const int iFillW = (int)(kAlertBarW * (float)(a.dwMS - dwAge) / (float)a.dwMS);
        if (iFillW > 0) {
            g_DrawImpl.DrawFit(iBarX, iBarY, IMAGE_RES_UI,
                pRes->GetImageNID(IMAGE_RES_UI, bSoak ? "UI00_GUAGE_GREEN" : "UI00_GUAGE_RED"), iFillW, kAlertBarH,
                D3DCOLOR_ARGB(255, 255, 255, 255));
        }
    }
}

void
CZoneAlerts::Forget() {
    m_Alerts.clear();
}

//-------------------------------------------------------------------------------------------------
CZoneTimers&
CZoneTimers::Instance() {
    static CZoneTimers s_Timers;
    return s_Timers;
}

void
CZoneTimers::Set(unsigned char btID, const char* szLabel, int iSeconds) {
    if (iSeconds <= 0) {
        m_Timers.erase(btID);
        return;
    }
    Timer& timer = m_Timers[btID];
    timer.label = szLabel ? szLabel : "";
    timer.dwEndTick = ::GetTickCount() + (unsigned long)iSeconds * 1000;
}

void
CZoneTimers::List(std::vector<Entry>& out) {
    out.clear();
    const unsigned long now = ::GetTickCount();
    for (std::map<unsigned char, Timer>::iterator it = m_Timers.begin(); it != m_Timers.end();) {
        const long lLeftMS = (long)(it->second.dwEndTick - now);
        if (lLeftMS <= 0) {
            it = m_Timers.erase(it);
            continue;
        }
        Entry e;
        e.btID = it->first;
        e.label = it->second.label;
        e.iSecondsLeft = (int)((lLeftMS + 999) / 1000); // 0:01 until the last second is over
        out.push_back(e);
        ++it;
    }
}

void
CZoneTimers::Forget() {
    m_Timers.clear();
}
