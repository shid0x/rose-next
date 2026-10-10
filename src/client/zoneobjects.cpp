#include "stdafx.h"

#include "zoneobjects.h"

#include "cobjfixed.h"
#include "object.h"
#include "zz_interface.h"

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
