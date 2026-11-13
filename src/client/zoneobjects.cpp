#include "stdafx.h"

#include "zoneobjects.h"

#include "cobjfixed.h"
#include "object.h"
#include "zz_interface.h"
#include "CApplication.h"
#include "stunfeedback.h"
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
