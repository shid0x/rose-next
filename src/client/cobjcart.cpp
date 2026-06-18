#include "stdafx.h"

#include ".\cobjcart.h"
#include "rose/io/stb.h"
#include "CObjCHAR_Collision.h"

CObjCART::CObjCART(void) {
    m_pObjParent = NULL;
    m_iCartType = 0;

    m_iCurrentCartState = CART_STATE_STOP;
    m_iOldCartState = CART_STATE_STOP;

    //----------------------------------------------------------------
    //박지호
    m_pRideUser = NULL;

    //----------------------------------------------------------------
}

CObjCART::~CObjCART(void) {
    if (m_pObjParent != NULL) {
        D3DXVECTOR3 pos = Get_CurPOS();
        ::unlinkNode(m_pObjParent->GetZMODEL());
        m_pObjParent->ResetCUR_POS(pos);
        ///::savePrevPosition( m_pObjParent->GetZMODEL() );

        CopyCollisionInformation(false);

        ((CObjAVT*)m_pObjParent)->m_pObjCART = NULL;
        m_pObjParent = NULL;
    }

    // Stop every looping cart sound on teardown, not just the tracked state.
    // The move/engine loops are DSBPLAY_LOOPING buffers; if the active loop is
    // not stopped here it keeps playing forever. Relying on m_iOldCartState
    // leaked the move ("steps") loop whenever the cart was destroyed while in
    // CART_STATE_MOVE — e.g. dismount mid-combat, or cart re-creation that
    // bare-deletes the object (CObjAVT::CreateCart) and never runs UnLinkChild.
    // CART_STATE_MOVE stops PAT_MOVE_SOUND and CART_STATE_STOP stops
    // PAT_STOP_SOUND, which together cover every loop the cart can start.
    this->StopSound(CART_STATE_MOVE);
    this->StopSound(CART_STATE_STOP);

    //-----------------------------------------------------------------
    //박지호
    m_pRideUser = NULL;
    //-----------------------------------------------------------------
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 나에게 링크된 애들을 모두 분리
//----------------------------------------------------------------------------------------------------
void
CObjCART::UnLinkChild(int iStart) {

#if defined(_GBC)
    //----------------------------------------------------------------
    //박지호::카트에서 탑승한 유저를 링크해제 한다. (수정)
    D3DXVECTOR3 pos = Get_CurPOS();

    CObjCHAR* pClass[] = {m_pObjParent, m_pRideUser};

    int si = 0;

    if (iStart) {
        si = 1;
    }

    for (register int i = si; i < 2; i++) {
        if (pClass[i] == NULL)
            continue;

        ::unlinkVisibleWorld(pClass[i]->GetZMODEL());

        if (i == 0) {
            pClass[i]->ReleaseRideUser();
            pClass[i]->ResetCUR_POS(pos);
            pClass[i]->SetResetPosZ(true, pos.z);
        }
        //뒷에 탑승한 유저는 조금 뒤에 위치를 탑승한다.
        else {
            D3DXVECTOR3 ruDir;
            D3DXVECTOR3 ruPos = pos;

            ::getModelDirectionVector(this->GetZMODEL(), &ruDir.x);

            ruPos += ((ruDir * -1.0f) * 200.0f);

            pClass[i]->ResetCUR_POS(ruPos);
            // pClass[i]->DropFromSky(ruPos.x,ruPos.y);
            pClass[i]->SetResetPosZ(true, pos.z + 200);

            m_pRideUser = NULL;
        }

        pClass[i]->Set_STATE(CS_STOP);
        pClass[i]->SetCMD_STOP();

        CopyCollisionInformation(false);

        ///((CObjAVT*)m_pObjParent)->m_pObjCART = NULL;
        pClass[i] = NULL;

        this->StopSound(m_iCurrentCartState);
    }
#else
    if (m_pObjParent != NULL) {
        D3DXVECTOR3 pos = Get_CurPOS();

        ::unlinkVisibleWorld(m_pObjParent->GetZMODEL());
        m_pObjParent->ResetCUR_POS(pos);
        m_pObjParent->SetResetPosZ(true, pos.z);
        ///::savePrevPosition( m_pObjParent->GetZMODEL() );
        CopyCollisionInformation(false);

        ///((CObjAVT*)m_pObjParent)->m_pObjCART = NULL;
        m_pObjParent = NULL;

        this->StopSound(m_iCurrentCartState);
    }
#endif
}

void
CObjCART::SetPetParts(unsigned int iPartIDX, unsigned int iItemIDX) {
    if (iPartIDX >= MAX_RIDING_PART)
        return;

    m_nPartItemIDX[iPartIDX] = iItemIDX;
}

bool
CObjCART::Create(CObjCHAR* pParent, int iCartType, D3DVECTOR& Position) {
    if (pParent == NULL)
        return false;

    float fDir = ::getModelDirection(pParent->GetZMODEL());

    m_pCharMODEL = &m_CharMODEL;
    m_pObjParent = pParent;
    m_iCartType = iCartType;

    char* szName = CStr::Printf("%s_Cart", pParent->Get_NAME());

    CMODEL<CCharPART>* pPartMODEL = NULL;
    for (short nI = 0; nI < MAX_RIDING_PART; nI++) {
        pPartMODEL = g_DATA.m_pMD_CartPARTS->GetMODEL(m_nPartItemIDX[nI]);
        m_CharMODEL.SetPartMODEL(nI, pPartMODEL);
    }

    if (CObjCHAR::CreateCHAR(szName, &m_CharMODEL, MAX_RIDING_PART, Position)) {
        this->m_iHP = pParent->Get_HP();
        HNODE hChild = m_pObjParent->GetZMODEL();

        this->LinkDummy(hChild, 0);
        this->Set_ModelDIR(fDir, true);
        this->ResetCUR_POS(D3DXVECTOR3(Position.x, Position.y, Position.z));

        ::setUserData(this->GetZMODEL(), reinterpret_cast<HNODE>(this));

        SetEffect();
        CopyCollisionInformation(true);

        return true;
    }

    LogString(LOG_DEBUG_,
        "Cart create failed (name={} cartType={} parts={},{},{},{},{})\n",
        szName,
        iCartType,
        (int)m_nPartItemIDX[0],
        (int)m_nPartItemIDX[1],
        (int)m_nPartItemIDX[2],
        (int)m_nPartItemIDX[3],
        (int)m_nPartItemIDX[4]);

    /// 카트 및 캐슬기어 크기 1.2 배 확대..
    ::setScale(this->GetZMODEL(), 1.2f, 1.2f, 1.2f);

    return false;
}

//----------------------------------------------------------------------------------------------------
/// @박지호
/// @박지호::선택된 유저를 카트에 태운다.
//----------------------------------------------------------------------------------------------------
bool
CObjCART::Create(CObjCHAR* pTarget) {

    if (m_pObjParent == NULL)
        return false;

    m_pRideUser = pTarget;

    //위치 초기화
    pTarget->Set_CurPOS(D3DXVECTOR3(0, 0, 0));
    ::setPosition(pTarget->GetZMODEL(), 0, 0, 0);

    HNODE hChild = pTarget->GetZMODEL();

    this->LinkDummy(hChild, 10);

    //회전 및 모션 설정
    m_pRideUser->Set_ModelDIR(0.0f, 1);
    m_pRideUser->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_STOP1);

    return true;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Cart 의 경우는 무기에 따른 모션이 아니라 ARMS 테이블의 모션 타입에 의존한다.
//----------------------------------------------------------------------------------------------------
tagMOTION*
CObjCART::Get_MOTION(short nActionIdx) {

    // Battle carts / castle gears attach the weapon to RIDE_PART_ARMS
    // (slot 4 = m_nWeaponIDX) and attack motions live off that item's
    // PAT_RELATIVE_MOTION_POS row. Fall back to m_nAbilIDX (slot 3) so
    // non-battle carts that only carry an ability enhancement still animate.
    int iMotionItem = this->m_nWeaponIDX ? this->m_nWeaponIDX : this->m_nAbilIDX;
    short nFileIDX = FILE_MOTION(PAT_RELATIVE_MOTION_POS(iMotionItem), nActionIdx);

    if (0 == nFileIDX) {
        nFileIDX = FILE_MOTION(PAT_RELATIVE_MOTION_POS(iMotionItem), 0);
    }

    tagMOTION* pMOTION = g_MotionFILE.IDX_GetMOTION(nFileIDX, false);
    if (pMOTION) {
        pMOTION->m_nActionIdx = nActionIdx;
    }

    return pMOTION;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 카트에 이펙트 설정
///		   일반 부품의 효과는 항상 나오고, 랙의 부품효과는 이동시에만 나온다.
//----------------------------------------------------------------------------------------------------

void
CObjCART::SetEffect() {
    for (short nI = 0; nI < MAX_RIDING_PART; nI++) {
        if (nI != RIDE_PART_LEG)
            SetPartEffect(nI);
    }
    // SetPartEffect( RIDE_PART_ENGINE );
}

#define MAX_CART_EFFECT_DUMMIES 8
void
CObjCART::SetPartEffect(int iPart) {
    int iEffectNO = 0;
    for (int i = 0; i < MAX_CART_EFFECT_DUMMIES; i++) {
        iEffectNO = PAT_ITEM_ATTACHED_EFFECT(m_nPartItemIDX[iPart], i);

        /// 설정할 이펙트가 있을경우..
        if (iEffectNO) {
            CEffect* pEffect = g_pEffectLIST->Add_EffectWithIDX(iEffectNO, true);
            if (pEffect) {
                this->LinkDummy(pEffect->GetZNODE(), i);
                this->AddExternalEffect(pEffect);
                pEffect->SetParentCHAR(this);

                pEffect->InsertToScene();
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 이동중에만 나오는 이펙트. 혹은 그 반대.
//----------------------------------------------------------------------------------------------------
void
CObjCART::SetEffectByMoveState(bool bShow) {}

///
/// 카트에 사운드 설정
///
void
CObjCART::PlaySound(int iCurrentState) {
    for (short nI = 0; nI < MAX_RIDING_PART; nI++) {
        PlayPartSound(nI, iCurrentState);
    }
}

void
CObjCART::PlayPartSound(int iPart, int iCurrentState) {
    switch (iCurrentState) {
        case CART_STATE_STOP: {
            int iSoundIDX = PAT_STOP_SOUND(m_nPartItemIDX[iPart]);
            if (iSoundIDX) {
                D3DXVECTOR3 vecWorld = this->Get_CurPOS();
                g_pSoundLIST->IDX_PlaySound3DLoop(iSoundIDX, vecWorld, NULL);
            }
        } break;

        case CART_STATE_MOVE: {
            int iSoundIDX = PAT_MOVE_SOUND(m_nPartItemIDX[iPart]);
            if (iSoundIDX) {
                D3DXVECTOR3 TargetPos;
                TargetPos.x = this->m_PosGOTO.x;
                TargetPos.y = this->m_PosGOTO.y;
                TargetPos.z = 100.0f;

                D3DXVECTOR3 Velocity = TargetPos - Get_CurPOS(); // 1초 동안에 이동하는 벡터

                D3DXVECTOR3 vecWorld = this->Get_CurPOS();
                g_pSoundLIST->IDX_PlaySound3DLoop(iSoundIDX, vecWorld, &Velocity);
            }
        } break;
    }
}

void
CObjCART::StopSound(int iCurrentState) {
    for (short nI = 0; nI < MAX_RIDING_PART; nI++) {
        StopPartSound(nI, iCurrentState);
    }
}

void
CObjCART::UpdateStateSound() {
    // The move/engine sounds are looping buffers. Re-issuing the same cart
    // state (a fresh MOVE while already moving, repeated STOP while idle, etc.)
    // is common during continuous movement / chasing. Stopping and replaying
    // the loop in that case restarts it from frame 0 and produces an audible
    // stutter on small or continuous steps, so only swap loops when the state
    // genuinely changed. State transitions still pair Stop(old)+Play(new).
    if (m_iOldCartState == m_iCurrentCartState) {
        return;
    }

    StopSound(m_iOldCartState);
    PlaySound(m_iCurrentCartState);
}

void
CObjCART::StopPartSound(int iPart, int iCurrentState) {
    switch (iCurrentState) {
        case CART_STATE_STOP: {
            int iSoundIDX = PAT_STOP_SOUND(m_nPartItemIDX[iPart]);
            if (iSoundIDX) {
                g_pSoundLIST->IDX_StopSound3DLoop(iSoundIDX);
            }
        } break;

        case CART_STATE_MOVE: {
            int iSoundIDX = PAT_MOVE_SOUND(m_nPartItemIDX[iPart]);
            if (iSoundIDX) {
                g_pSoundLIST->IDX_StopSound3DLoop(iSoundIDX);
            }
        } break;
        case CART_STATE_ATTACK: {
            int iSoundIDX = PAT_STOP_SOUND(m_nPartItemIDX[iPart]);
            if (iSoundIDX) {
                g_pSoundLIST->IDX_StopSound3DLoop(iSoundIDX);
            }
        } break;
    }
}

int
CObjCART::GetRideAniPos() {
    return PAT_RELATIVE_MOTION_POS_AVATAR(m_nPartItemIDX[RIDE_PART_BODY]);
}

int
CObjCART::GetANI_Stop() {
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY]) + CART_ANI_STOP1;
}
int
CObjCART::GetANI_Move() {
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY]) + CART_ANI_MOVE;
}
int
CObjCART::GetANI_Attack() {
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY])
        + CART_ANI_ATTACK01; /*+ RANDOM(3);*/
}
int
CObjCART::GetANI_Die() {
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY]) + CART_ANI_DIE;
}
int
CObjCART::GetANI_Hit() {
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY]) + CART_ANI_STOP1;
}

//박지호::스킬 애니메이션
int
CObjCART::GetANI_Casting() {
    return SKILL_ANI_CASTING(m_nActiveSkillIDX);
}
int
CObjCART::GetANI_CastingRepeat() {
    return SKILL_ANI_CASTING_REPEAT(m_nActiveSkillIDX);
}
int
CObjCART::GetANI_Skill() {
    return SKILL_ANI_ACTION_TYPE(m_nActiveSkillIDX);
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------

bool
CObjCART::SetCMD_MOVE(const D3DVECTOR& PosTO, BYTE btRunMODE) {
    this->ClearPendingMountedAttackTarget();
    CObjAI::SetCMD_MOVE(PosTO, btRunMODE);

    //----------------------------------------------------------------------------------------------------
    /// 펫 탑승을 한 캐릭터의 모션을 교체한다.
    //----------------------------------------------------------------------------------------------------
    m_pObjParent->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_MOVE);

    //----------------------------------------------------------------------------------------------------
#if defined(_GBC)
    //박지호::
    if (m_pRideUser)
        m_pRideUser->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_MOVE);
#endif
    //----------------------------------------------------------------------------------------------------

    m_iOldCartState = m_iCurrentCartState;
    m_iCurrentCartState = CART_STATE_MOVE;

    UpdateStateSound();
    return true;
}

void
CObjCART::SetCMD_MOVE(WORD wSrvDIST, const D3DVECTOR& PosTO, int iServerTarget) {
    this->ClearPendingMountedAttackTarget();
    CObjAI::SetCMD_MOVE(wSrvDIST, PosTO, iServerTarget);

    //----------------------------------------------------------------------------------------------------
    /// 펫 탑승을 한 캐릭터의 모션을 교체한다.
    //----------------------------------------------------------------------------------------------------
    m_pObjParent->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_MOVE);

    //----------------------------------------------------------------------------------------------------
    //박지호::
#if defined(_GBC)
    if (m_pRideUser)
        m_pRideUser->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_MOVE);
#endif
    //----------------------------------------------------------------------------------------------------

    m_iOldCartState = m_iCurrentCartState;
    m_iCurrentCartState = CART_STATE_MOVE;

    UpdateStateSound();
}

bool
CObjCART::SetCMD_STOP(void) {
    this->ClearPendingMountedAttackTarget();
    CObjAI::SetCMD_STOP();

    //----------------------------------------------------------------------------------------------------
    /// 펫 탑승을 한 캐릭터의 모션을 교체한다.
    //----------------------------------------------------------------------------------------------------
    m_pObjParent->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_STOP1);

    //----------------------------------------------------------------------------------------------------
#if defined(_GBC)
    //박지호::
    if (m_pRideUser)
        m_pRideUser->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_STOP1);
#endif
    //----------------------------------------------------------------------------------------------------

    m_iOldCartState = m_iCurrentCartState;
    m_iCurrentCartState = CART_STATE_STOP;

    UpdateStateSound();
    return true;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief /*override*/virtual bool	SetCMD_ATTACK ( int iServerTarget ); 는 정의할 필요없다.
///			CObjAI::SetCMD_ATTACK 가 내부적으로 호출하는 함수이다.
//----------------------------------------------------------------------------------------------------

void
CObjCART::SetCMD_ATTACK(int iServerTarget, WORD wSrvDIST, const D3DVECTOR& PosGOTO) {
    this->SetPendingMountedAttackTarget(iServerTarget, g_GameDATA.GetGameTime());
    CObjCHAR::SetCMD_ATTACK(iServerTarget, wSrvDIST, PosGOTO);

    //----------------------------------------------------------------------------------------------------
    /// 펫 탑승을 한 캐릭터의 모션을 교체한다.
    //----------------------------------------------------------------------------------------------------

    //----------------------------------------------------------------------------------------------------
#if defined(_GBC)
    m_pObjParent->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_STOP1);
    //박지호::
    if (m_pRideUser)
        m_pRideUser->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_STOP1);
#else
    m_pObjParent->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_ATTACK01 + RANDOM(3));
#endif
    //----------------------------------------------------------------------------------------------------

    m_iOldCartState = m_iCurrentCartState;
    m_iCurrentCartState = CART_STATE_ATTACK;

    UpdateStateSound();
}

float
CObjCART::Get_fAttackSPEED() {
    short aspd = this->stats.attack_speed;
    if (aspd <= 0 && m_pObjParent) {
        aspd = m_pObjParent->stats.attack_speed;
    }
    if (aspd <= 0) {
        aspd = 100;
    }
    return (float)aspd / 100.0f;
}
int
CObjCART::Get_AttackRange() {

    int iWeaponItem = m_nWeaponIDX ? m_nWeaponIDX : m_nAbilIDX;
    return (int)(PAT_ITEM_ATK_RANGE(iWeaponItem) + (Get_SCALE() * 120));
}

/// 충돌에 필요한 위치정보를 복사한다.
void
CObjCART::CopyCollisionInformation(bool bRiding) {
    /// Parent 는 개념상의 부모 실제로는 아바타고 카트에 링크 되어 있다.
    /// 여기서 Parent 는 아바타
    CObjCHAR_Collision* pCollision = this->GetCollisionUtility();
    CObjCHAR_Collision* pParentCollision = m_pObjParent->GetCollisionUtility();

    /// 탈때는 아바타의 정보를 카트에 복사
    if (bRiding) {
        if (m_pObjParent) {
            if (pCollision && pParentCollision) {
                pCollision->m_vPrevious = pParentCollision->m_vPrevious;
                pCollision->m_vCurrent = pParentCollision->m_vCurrent;
                pCollision->m_vNext = pParentCollision->m_vNext;
            }
        }
    } else {
        /// 내릴때는 카트의 정보를 아바타에 복사
        if (m_pObjParent) {
            if (pCollision && pParentCollision) {
                pParentCollision->m_vPrevious = pCollision->m_vPrevious;
                pParentCollision->m_vPrevious.z = pCollision->m_vPrevious.z + 500.0f;
                pParentCollision->m_vCurrent = pCollision->m_vCurrent;
                pParentCollision->m_vNext = pCollision->m_vNext;
            }
        }
    }
}

///박지호::카트의 각 파트를 불투명도를 설정한다.
void
CObjCART::SetCartPartVisible(float fv) {

    CMODEL<CCharPART>* pCharPART = NULL;
    short iPart = 0;

    for (register int i = 0; i < MAX_RIDING_PART; i++) {
        iPart = m_nPartItemIDX[i];

        if (!iPart)
            continue;

        pCharPART = m_pCharMODEL->GetCharPART(iPart);

        if (pCharPART == NULL)
            continue;

        for (register int j = 0; j < pCharPART->m_nPartCNT; j++) {
            ::setVisibilityRecursive(m_phPartVIS[iPart][j], fv);
        }
    }
}
