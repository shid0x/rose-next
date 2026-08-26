#include "stdafx.h"

#include ".\objcastlegear.h"
#include "rose/io/stb.h"

CObjCastleGear::CObjCastleGear(void) {}

CObjCastleGear::~CObjCastleGear(void) {}

int
CObjCastleGear::GetANI_Stop() {
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY]) + CASTLE_GEAR_ANI_STOP1;
}
int
CObjCastleGear::GetANI_Move() {
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY]) + CASTLE_GEAR_ANI_MOVE;
}
int
CObjCastleGear::GetANI_Attack() {
    // The server only ever simulates PAT_ANI_ATTACK (= ATTACK01), so rolling the
    // motion here desyncs the hit frame from the swing the server timed. All three
    // castle-gear attack motions are 51 frames, but their action points sit at
    // frames 21-35 -- up to 0.47 s of jitter with no server counterpart. On foot
    // the same RANDOM(3) is invisible because its variants land within ~2 frames
    // of each other; CObjCART::GetANI_Attack already has it disabled.
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY]) + CASTLE_GEAR_ANI_ATTACK01;
}
int
CObjCastleGear::GetANI_Die() {
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY]) + CASTLE_GEAR_ANI_DIE;
}
int
CObjCastleGear::GetANI_Hit() {
    return PAT_RELATIVE_MOTION_POS(m_nPartItemIDX[RIDE_PART_BODY]) + CASTLE_GEAR_ANI_STOP1;
}
int
CObjCastleGear::GetANI_Casting() {
    return 0;
}
int
CObjCastleGear::GetANI_Skill() {
    return 0;
}

bool
CObjCastleGear::Create(CObjCHAR* pParent, int iCartType, D3DVECTOR& Position) {
    if (CObjCART::Create(pParent, iCartType, Position)) {
        ///::setScale( this->GetZMODEL(), 1.2f, 1.2f, 1.2f );
        return true;
    }

    return false;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief /*override*/virtual bool	SetCMD_ATTACK ( int iServerTarget ); 는 정의할 필요없다.
///			CObjAI::SetCMD_ATTACK 가 내부적으로 호출하는 함수이다.
//----------------------------------------------------------------------------------------------------

void
CObjCastleGear::SetCMD_ATTACK(int iServerTarget, WORD wSrvDIST, const D3DVECTOR& PosGOTO) {
    this->SetPendingMountedAttackTarget(iServerTarget, g_GameDATA.GetGameTime());
    CObjCHAR::SetCMD_ATTACK(iServerTarget, wSrvDIST, PosGOTO);

    //----------------------------------------------------------------------------------------------------
    /// 펫 탑승을 한 캐릭터의 모션을 교체한다.
    //----------------------------------------------------------------------------------------------------
    m_pObjParent->Set_MOTION(this->GetRideAniPos() + PETMODE_AVATAR_ANI_ATTACK01 + +RANDOM(3));

    m_iOldCartState = m_iCurrentCartState;
    m_iCurrentCartState = CART_STATE_ATTACK;

    UpdateStateSound();
}
