#include "stdafx.h"
#include "stunfeedback.h"

#include "Game.h"
#include "CApplication.h"
#include "JCommandState.h"
#include "Object.h"
#include "CObjUSER.h"
#include "cobjcart.h"
#include "Network/CNetwork.h"
#include "interface/CTDrawImpl.h"
#include "interface/IO_ImageRes.h"
#include "tgamectrl/resourcemgr.h"
#include "zz_interface.h"

namespace {

const D3DCOLOR kColText = D3DCOLOR_ARGB(255, 255, 196, 48); ///< amber fill, black outline
const int kFontSize = 32;
const int kTextW = 400;
const int kTextH = 44;
const float kTextTopRatio = 0.28f; ///< of the screen height: above the avatar, clear of it
const int kBarW = 180;
const int kBarH = 8;
const int kBarGap = 2;
const DWORD kPopMs = 150; ///< the word lands from 1.6x to its size
const float kPopScale = 0.6f;

/// A monster the attack key would attack (CSkillCommand, COMMON_COMMAND_ATTACK).
CObjCHAR*
AttackableMob(int iClientIDX) {
    if (iClientIDX <= 0)
        return NULL;
    CObjCHAR* pChar = g_pObjMGR->Get_CharOBJ(iClientIDX, true);
    if (pChar == NULL || !pChar->IsA(OBJ_MOB) || pChar->IsPresentedDead())
        return NULL;
    if (!CUserInputState::IsEnemy(pChar) || NPC_CAN_TARGET(pChar->Get_CharNO()) == 1)
        return NULL;
    return pChar;
}

/// The avatar, or the mount that carries a rider's attack (CObjCHAR::SetCMD_ATTACK).
CObjCHAR*
Fighter() {
    if (g_pAVATAR->GetPetMode() >= 0 && g_pAVATAR->m_pObjCART)
        return g_pAVATAR->m_pObjCART;
    return g_pAVATAR;
}

} // namespace

CStunFeedback&
CStunFeedback::Instance() {
    static CStunFeedback s_Instance;
    return s_Instance;
}

CStunFeedback::CStunFeedback():
    m_bWasFighting(false), m_iFightServerIDX(0), m_hFont(0), m_pShownStun(NULL),
    m_dwShownAt(0) {}

void
CStunFeedback::OnAvatarStunned() {
    if (g_pAVATAR == NULL)
        return;
    // A stun landing on a stun (AddEntity builds the new status before it drops the old)
    // finds the avatar already stopped: keep the fight the first one interrupted.
    if (g_pAVATAR->m_EndurancePack.GetStateFlag() & FLAG_ING_FAINTING)
        return;

    CObjCHAR* pFighter = Fighter();
    const int iServerIDX = pFighter->Get_TargetIDX();
    const bool bOnMob = AttackableMob(g_pObjMGR->Get_ClientObjectIndex(iServerIDX)) != NULL;
    m_iFightServerIDX = bOnMob ? iServerIDX : 0;
    switch (pFighter->Get_COMMAND()) {
        case CMD_ATTACK: // swinging
        case CMD_MOVE: // walking to swing: a walk with a monster target is an attack walk
            m_bWasFighting = bOnMob;
            break;
        case CMD_SKILL2OBJ:
        case CMD_SKILL2SELF:
        case CMD_SKILL2POS:
            // A skill used mid-attack (a buff, a ground skill) saves the attack to put back
            // after it (CommandToRestoreAfterSkill); a skill cast at a monster is a fight too.
            m_bWasFighting = pFighter->Get_BECOMMAND() == CMD_ATTACK
                || (pFighter->Get_COMMAND() == CMD_SKILL2OBJ && bOnMob);
            break;
        default:
            m_bWasFighting = false;
            break;
    }
}

void
CStunFeedback::OnAvatarStunEnded() {
    const bool bWasFighting = m_bWasFighting;
    const int iFightServerIDX = m_iFightServerIDX;
    m_bWasFighting = false;
    m_iFightServerIDX = 0;
    if (g_pAVATAR == NULL)
        return;

    const char* szWhyNot = NULL;
    CObjCHAR* pTarget = NULL;
    if (!bWasFighting) {
        szWhyNot = "not fighting when stunned";
    } else if (g_pAVATAR->Get_HP() <= 0 || g_pAVATAR->HasPendingAuthoritativeDeath()) {
        szWhyNot = "dead";
    } else if (g_pAVATAR->m_EndurancePack.GetStateFlag() & FLAG_ING_SLEEP) {
        szWhyNot = "asleep";
    } else if (Fighter()->Get_COMMAND() != CMD_STOP) {
        szWhyNot = "new order"; // anything the player ordered since (a move, a cast) stands
    } else if (g_pAVATAR->GetPetMode() >= 0 && !g_pAVATAR->CanAttackPetMode()) {
        szWhyNot = "mount cannot attack";
    } else {
        // The monster they have selected -- they may have picked another one while
        // stunned -- else the one they were fighting.
        pTarget = AttackableMob(g_UserInputSystem.GetCurrentTarget());
        if (pTarget == NULL)
            pTarget = AttackableMob(g_pObjMGR->Get_ClientObjectIndex(iFightServerIDX));
        if (pTarget == NULL)
            szWhyNot = "no live monster selected or fought";
    }
    if (szWhyNot) {
        LogString(LOG_DEBUG_,
            "CombatTrace stun ended, no attack resumed: %s (command %d)\n",
            szWhyNot,
            (int)Fighter()->Get_COMMAND());
        return;
    }

    LogString(LOG_DEBUG_,
        "CombatTrace stun ended, attack resumed: target %d\n",
        (int)pTarget->Get_INDEX());
    g_pNet->Send_cli_ATTACK(pTarget->Get_INDEX());
}

void
CStunFeedback::Draw() {
    CEnduranceProperty* pStun = NULL;
    if (m_hFont && g_pAVATAR && g_pAVATAR->Get_HP() > 0)
        pStun = g_pAVATAR->m_EndurancePack.GetEntityByStateType(ING_FAINTING);
    const DWORD dwNow = g_GameDATA.GetGameTime();
    if (pStun != m_pShownStun) {
        m_pShownStun = pStun;
        m_dwShownAt = dwNow;
    }
    if (pStun == NULL)
        return;

    // The server ends a player's stun when its duration is up (gameserver Proc_IngSTATUS)
    // and the status started when its packet arrived, so the bar ends with the stun.
    const DWORD dwAge = dwNow - (DWORD)pStun->GetStartTime();
    const int iTotalMs = pStun->GetEnduranceTimeMs();

    const float fCenterX = g_pCApp->GetWIDTH() * 0.5f;
    const float fTop = (float)(int)(g_pCApp->GetHEIGHT() * kTextTopRatio);

    float fScale = 1.f;
    const DWORD dwShown = dwNow - m_dwShownAt;
    if (dwShown < kPopMs) {
        const float fLeft = 1.f - (float)dwShown / kPopMs;
        fScale += kPopScale * fLeft * fLeft;
    }
    D3DXVECTOR2 scale(fScale, fScale);
    D3DXVECTOR2 center(kTextW * 0.5f, kTextH * 0.5f);
    D3DXVECTOR2 pos((float)(int)(fCenterX - kTextW * 0.5f), fTop);
    D3DXMATRIX mat;
    D3DXMatrixTransformation2D(&mat, &center, 0.f, &scale, NULL, 0.f, &pos);
    ::setTransformSprite(mat);
    RECT rc = {0, 0, kTextW, kTextH};
    ::drawFont(m_hFont, true, &rc, kColText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, "STUNNED");

    // The time left.
    if (iTotalMs <= 0)
        return;
    const int iLeftMs = (int)dwAge < iTotalMs ? iTotalMs - (int)dwAge : 0;
    CResourceMgr* pRes = CResourceMgr::GetInstance();
    const int iBarX = (int)(fCenterX - kBarW * 0.5f);
    const int iBarY = (int)fTop + kTextH + kBarGap;
    g_DrawImpl.DrawFit(iBarX, iBarY, IMAGE_RES_UI,
        pRes->GetImageNID(IMAGE_RES_UI, "UI00_GUAGE_BACKGROUND"), kBarW, kBarH,
        D3DCOLOR_ARGB(255, 255, 255, 255));
    const int iFillW = kBarW * iLeftMs / iTotalMs;
    if (iFillW > 0) {
        g_DrawImpl.DrawFit(iBarX, iBarY, IMAGE_RES_UI,
            pRes->GetImageNID(IMAGE_RES_UI, "UI00_GUAGE_RED"), iFillW, kBarH,
            D3DCOLOR_ARGB(255, 255, 255, 255));
    }
}

void
CStunFeedback::LoadFont(const char* szFont, int iCharSet) {
    m_hFont = ::loadFontOutline("FONT_OUTLINE_STUNNED",
        szFont,
        iCharSet,
        kFontSize,
        1,
        0,
        1,
        D3DCOLOR_ARGB(255, 255, 255, 255),
        D3DCOLOR_ARGB(255, 0, 0, 0));
}

void
CStunFeedback::UnloadFont() {
    if (m_hFont) {
        ::unloadFont(m_hFont);
        m_hFont = 0;
    }
}
