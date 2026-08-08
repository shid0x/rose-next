/*
    $Header: /Client/CObjCHAR.cpp 652   05-10-20 2:46p Gioend $
*/
#include "stdAFX.h"

#include "Game.h"
#include "OBJECT.h"
#include "BULLET.h"
#include "IO_Event.h"
#include "CViewMSG.h"
#include "Network\CNetwork.h"
#include "calculation.h"
#include "Game_FUNC.h"
#include "Interface/CUIMediator.h"
#include "Interface/TypeResource.h"
#include "Interface/Dlgs/ChattingDlg.h"
#include "Interface/CClanMarkUserDefined.h"
#include "GameCommon/Skill.h"
#include "Event/Quest_FUNC.h"
#include "CCamera.h"
#include "Game_FUNC.h"

#include "GameProc/ChangeVisibility.h"
#include "CObjCART.h"
#include "CObjCHAR_Collision.h"

#include "GameCommon/Item.h"
#include "GameData/CParty.h"
#include "GameData/CDamageMeter.h"
#include "rose/io/stb.h"
#include "Misc/GameUtil.h"
#include "CommandFilter.h"
#include "../GameProc/DelayedExp.h"
#include "CInventory.h"
#include "system/System_Func.h"

#include "rose/combat/skill_presentation.h"
#include "rose/common/status_effect/status_effect_flag.h"

using namespace Rose::Common;

extern CCamera* g_pCamera;

extern CAI_OBJ* AI_FindFirstOBJ(CAI_OBJ* pBaseOBJ, int iDistance);
extern CAI_OBJ* AI_FindNextOBJ();

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param int iDistance 거리
/// @brief  : AI 동작을 위해 첫번재 오브젝트를 찾는다..
//--------------------------------------------------------------------------------

CAI_OBJ*
CObjCHAR::AI_FindFirstOBJ(int iDistance) {
    return ::AI_FindFirstOBJ(this, iDistance);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  :AI_FindFirstOBJ 호출후 호출하여 리스트상의 다음 오브젝트를 꺼집어 냄
//--------------------------------------------------------------------------------

CAI_OBJ*
CObjCHAR::AI_FindNextOBJ() {
    return ::AI_FindNextOBJ();
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param wSrvDIST 서버에서 계산되어 날아온 목표위치와 현재위치와의 2차원적 거리. 단위 cm
/// @param PosGOTO 서버에서 날아온 이동할 목표위치. 현재로서 Z 값은 의미없다.
/// @brief  : 이동 속도 보정. 매 프레임마다 갱신됨. 서버와 클라이언트의 속도차가 나는 경우에,
///           강제로 속도를 증가시킨다. 혹은 너무 많은 차이가 날 때에, 강제로 이동시킨다.
///
//--------------------------------------------------------------------------------

void
CObjCHAR::Adj_MoveSPEED(WORD wSrvDIST, const D3DVECTOR& PosGOTO) {
    int iClientDIST;
    float fCurSpeed, fNewSpeed, fNeedTime;

    fCurSpeed = this->stats.move_speed;

    if (0 == fCurSpeed) {
        this->adjusted_move_speed = 0;
        return;
    }

    fNeedTime = float(wSrvDIST) / fCurSpeed;

    // 클라이언트의 현재-목표 거리를 계산한다.
    iClientDIST =
        CD3DUtil::distance((int)m_PosCUR.x, (int)m_PosCUR.y, (int)PosGOTO.x, (int)PosGOTO.y);

    assert(iClientDIST >= 0);

    if (0 == iClientDIST) {
        this->adjusted_move_speed = 0;
        return;
    } else if (iClientDIST <= wSrvDIST) {
        this->adjusted_move_speed = fCurSpeed;
        return; 
    } else {
        fNewSpeed = float(iClientDIST) / fNeedTime;

        if (this->IsA(OBJ_USER) == false) {
            int iDiffDistance =
                iClientDIST - wSrvDIST;

            float fNeedTimeDiff =
                float(iDiffDistance) / fNewSpeed;

            if (fNeedTimeDiff > 1.0f) {
                fNewSpeed = fCurSpeed;

                D3DXVECTOR3 vDir = (D3DXVECTOR3)PosGOTO - m_PosCUR;
                float fRatio = (float)iDiffDistance / (float)iClientDIST;

                D3DXVECTOR3 vPosCur = m_PosCUR + (vDir * fRatio);
                vPosCur.z = m_PosCUR.z;

                this->ResetCUR_POS(vPosCur);
            }
        }
    }

    this->adjusted_move_speed = fCurSpeed;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 공격속도 조정..
//--------------------------------------------------------------------------------

void
CObjCHAR::Adj_AniSPEED(float fAniSpeed) {
    ::setAnimatableSpeed(this->m_hNodeMODEL, fAniSpeed);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param int iServerTarget 오브젝트 서버인덱스
/// @param tPOINTF &PosFROM
/// @param tPOINTF &PosGOTO
/// @brief  : 공격명령 설정
//--------------------------------------------------------------------------------

void
CObjCHAR::SetCMD_ATTACK(int iServerTarget, WORD wSrvDIST, const D3DVECTOR& PosGOTO) {
    if (this->IsA(OBJ_USER)) {
        CObjAttackCommand* pObjCommand =
            (CObjAttackCommand*)g_CommandFilter.GetCommandObject(OBJECT_COMMAND_ATTACK);
        pObjCommand->SetCMD_ATTACK(iServerTarget, wSrvDIST, PosGOTO);

        g_CommandFilter.SetPrevCommand(pObjCommand);
    }

    // Mounted combat is driven by the cart / castle gear child, not the rider.
    // Route the attack command to the mount before checking the rider's own
    // command gate so the first post-mount attack cannot get stuck queued on the
    // avatar while the server already starts processing damage.
    int iPetMode = this->GetPetMode();
    if (iPetMode >= 0) {
        if (this->CanAttackPetMode()) {
            if (this->m_pObjCART) {
                this->m_pObjCART->SetPendingMountedAttackTarget(
                    iServerTarget, g_GameDATA.GetGameTime());
            }
            SetCMD_PET_ATTACK(iServerTarget, wSrvDIST, PosGOTO);
        }
//박지호::펫모드일 경우 아바타도 공격속성을 설정 하도록 한다.
#ifndef _GBC
        return;
#endif
    }

    /// 현재 명령이 들어갈수 있나?
    if (this->CanApplyCommand() == false) {
        this->PushCommandAttack(iServerTarget, wSrvDIST, PosGOTO);
        return;
    }

    /// 서버에서 받은 공격 패킷 처리...
    this->Adj_MoveSPEED(wSrvDIST, PosGOTO);

    this->m_PosGOTO = PosGOTO;

    CObjAI::SetCMD_ATTACK(iServerTarget);
}

void
CObjCHAR::SetCombatAttackIntent(int iServerTarget, WORD wSrvDIST, const D3DVECTOR& PosGOTO) {
    this->SetCMD_MOVE(wSrvDIST, PosGOTO, iServerTarget);
}

void
CObjCHAR::StartConfirmedCombatSwing(int iServerTarget,
    WORD wSrvDIST,
    const D3DVECTOR& PosGOTO,
    const Rose::Combat::DamageEvent& event) {
    CObjCHAR* pTarget = g_pObjMGR->Get_ClientCharOBJ(iServerTarget, true);
    if (pTarget) {
        pTarget->PushCombatDamageEvent(event);
        m_dwPendingCombatSwingEventId = event.event_id;
        m_iPendingCombatSwingDefenderIndex = pTarget->Get_INDEX();
        m_bPendingCombatSwingProjectile =
            event.presentation_kind == Rose::Combat::DamagePresentationKind::ProjectileImpact;
        m_bPendingCombatSwingProjectileSpawned = false;
    } else {
        ClearPendingCombatSwingPresentation();
    }

    this->SetCMD_ATTACK(iServerTarget, wSrvDIST, PosGOTO);
}

void
CObjCHAR::MarkPendingCombatSwingProjectileSpawned() {
    if (m_dwPendingCombatSwingEventId == 0 || !m_bPendingCombatSwingProjectile) {
        return;
    }

    m_bPendingCombatSwingProjectileSpawned = true;
}

void
CObjCHAR::ClearPendingCombatSwingPresentation(uint32_t eventId) {
    if (eventId != 0 && m_dwPendingCombatSwingEventId != eventId) {
        return;
    }

    m_dwPendingCombatSwingEventId = 0;
    m_iPendingCombatSwingDefenderIndex = 0;
    m_bPendingCombatSwingProjectile = false;
    m_bPendingCombatSwingProjectileSpawned = false;
}

void
CObjCHAR::CancelInterruptedCombatSwingPresentation(const char* reason) {
    if (m_dwPendingCombatSwingEventId == 0 || m_iPendingCombatSwingDefenderIndex == 0) {
        return;
    }

    if (m_bPendingCombatSwingProjectile && m_bPendingCombatSwingProjectileSpawned) {
        LogString(LOG_DEBUG_,
            "CombatTrace interrupted swing kept for spawned projectile: attacker %d defender %d event %u reason %s\n",
            this->Get_INDEX(),
            m_iPendingCombatSwingDefenderIndex,
            m_dwPendingCombatSwingEventId,
            reason ? reason : "");
        return;
    }

    const uint32_t eventId = m_dwPendingCombatSwingEventId;
    CObjCHAR* pDefender = g_pObjMGR->Get_CharOBJ(m_iPendingCombatSwingDefenderIndex, true);
    ClearPendingCombatSwingPresentation();

    if (!pDefender) {
        return;
    }

    pDefender->DiscardQueuedCombatDamageEvent(eventId, this, reason);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : Constructor
//--------------------------------------------------------------------------------

CObjCHAR::CObjCHAR(): m_EndurancePack(this), m_ChangeActionMode(this), m_ObjVibration(this, 200) {
#ifdef __VIRTUAL_SERVER
    m_pRegenPOINT = NULL;
    m_pRegenListNODE = NULL;
#endif

    m_fScale = 1.0f;

    m_fHeightOfGround = 0.0f;
    m_hNodeMODEL = NULL;

    m_ppBoneEFFECT = NULL;
    m_hR_TRAIL = m_hL_TRAIL = NULL;

    for (short nP = 0; nP < MAX_BODY_PART; nP++) {
        m_phPartVIS[nP] = NULL;
        m_pppEFFECT[nP] = NULL;
        m_nEffectPointCNT[nP] = 0;
    }

    m_bProcEffectedSkill = false;
    m_iEffectedSkillIndex = 0;
    m_EffectedSkillList.clear();

    m_fAltitude = 0.0;

    m_iHP = 1;
    m_iMP = 1;
    m_iMaxHP = 1;
    m_iMaxMP = 1;

    m_dwLastRecoveryUpdateTime = g_GameDATA.GetGameTime();
    m_dwElapsedTime = 0;
    m_dwFrameElapsedTime = 0;

    m_bDead = false;

    m_pChangeVisibility = NULL;
    m_bStopDead = false;

    m_bIsVisible = true;

    m_hNodeGround = NULL;

    m_pCollision = new CObjCHAR_Collision; /// will be deleted in DeletCHAR()

    memset(&m_SummonMob, 0, sizeof(gsv_MOB_CHAR));
    m_bHaveSummonedMob = false; /// 소환해야될 몹이 있는가?

    m_bUseResetPosZ = false;
    m_fResetPosZ = 0;
    m_fModelSPEED = 0;

    m_iLastCastingSkill = 0;

    m_dwClanID = 0;
    m_wClanMarkBack = 0;
    m_wClanMarkCenter = 0;

    m_bFrameING = 0;
    m_ClanMarkUserDefined = 0;

    m_ReviseHP = 0;
    m_ReviseMP = 0;
    m_iAuthoritativeHP = 0;
    m_bHasAuthoritativeHP = false;
    m_dwLastAuthoritativeDamageSeq = 0;
    m_dwLastAuthoritativeSyncSeq = 0;
    m_dwLastPresentedDamageEventId = 0;
    m_dwLastPresentedDamageSeq = 0;
    m_iPendingCombatHPCorrection = 0;
    m_bPendingAuthoritativeDeath = false;
    m_dwPendingAuthoritativeDeathTime = 0;
    m_dwPendingCombatSwingEventId = 0;
    m_iPendingCombatSwingDefenderIndex = 0;
    m_bPendingCombatSwingProjectile = false;
    m_bPendingCombatSwingProjectileSpawned = false;
    m_iPendingMountedAttackTarget = 0;
    m_dwPendingMountedAttackTime = 0;

    //-------------------------------------------------------------------------------
    //조성현
    m_bDisguise = false;

    //--------------------------------------------------------------------------------
    ///박지호
    //카트 변수들 초기화
    m_iPetType = -1;
    m_pObjCART = NULL;
    m_pRideUser = NULL;

    m_bUseCartSkill = FALSE;
    m_IsRideUser = FALSE;

    m_saveSpeed = 0.0f;

    m_iPetType = -1;
    m_iRideIDX = 0;
    m_skCartIDX = 0;

    //아로아 상태변수 초기화
    m_IsAroa = 0;
    m_IsCartVA = 0;
    //--------------------------------------------------------------------------------
    m_AruaAddMoveSpeed = 0;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : Destructor
//--------------------------------------------------------------------------------

CObjCHAR::~CObjCHAR() {
    // 엔진에 등록된 HNODE들 삭제.
    this->DeleteCHAR();

    ClearExternalEffect();
    m_EndurancePack.ClearEntityPack();
    SAFE_DELETE(m_pCollision);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 회복을 위한 타이머 리셋
//--------------------------------------------------------------------------------
void
CObjCHAR::ClearTimer() {
    m_dwLastRecoveryUpdateTime = g_GameDATA.GetGameTime();
    m_dwElapsedTime = 0;
    m_dwFrameElapsedTime = 0;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 죽거나, 파괴시에 모든 내부의 리스트 정리
//--------------------------------------------------------------------------------

void
CObjCHAR::ClearAllEntityList() {
    /// proc all effect of skill
    ProcEffectedSkill();

    /// clear all damage list
    ClearAllDamage();

    /// drop field item list
    DropFieldItemFromList();

    /// clear all command
    ClearAllCommand();
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 외부에서 등록시킨 이펙트 리스트들 정리
//--------------------------------------------------------------------------------

void
CObjCHAR::ClearExternalEffect() {
    classDLLNODE<CEffect*>* pNode;
    pNode = m_ExternalEffectLIST.GetHeadNode();
    while (pNode) {
        /// 이펙트만 지우고 이펙트의 부모처리는 안한다. 왜냐? 내가 부모니까..
        g_pEffectLIST->Del_EFFECT(pNode->DATA, false);

        m_ExternalEffectLIST.DeleteNFree(pNode);
        pNode = m_ExternalEffectLIST.GetHeadNode();
    }

    m_ExternalEffectLIST.ClearList();

    DeleteWeatherEffect();
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param CEffect* pEffect 지울 이펙트
/// @brief  : 외부 이펙트 등록
//--------------------------------------------------------------------------------

void
CObjCHAR::AddExternalEffect(CEffect* pEffect) {
    m_ExternalEffectLIST.AllocNAppend(pEffect);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  CEffect* pEffect 지울 이펙트
/// @brief  : 외부에서 등록된 이펙트 지움
//--------------------------------------------------------------------------------

void
CObjCHAR::DeleteExternalEffect(CEffect* pEffect) {
    if (pEffect == NULL)
        return;

    classDLLNODE<CEffect*>* pNode;
    pNode = m_ExternalEffectLIST.GetHeadNode();
    while (pNode) {
        if (pNode->DATA == pEffect) {
            m_ExternalEffectLIST.DeleteNFree(pNode);
            return;
        }
        pNode = m_ExternalEffectLIST.GetNextNode(pNode);
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  D3DVECTOR &PosSCR 얻어올 스크린좌표( 아웃풋 )
/// @brief  : 현재 캐릭터 위치의 스크린 좌표를 얻어옴
//--------------------------------------------------------------------------------

void
CObjCHAR::GetScreenPOS(D3DVECTOR& PosSCR) {
    // 모델의 좌표에 키를 더한 위치를 이름출력 위치로 설정
    ::worldToScreen(m_PosCUR.x,
        m_PosCUR.y,
        getPositionZ(m_hNodeMODEL) + m_fStature,
        &PosSCR.x,
        &PosSCR.y,
        &PosSCR.z);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  HNODE hLinkNODE 링크할 대상의 노드 핸들
/// @param  short nDummyIDX 링크할 더미 인덱스
/// @brief  : 입력받은 노드를 입력받은 더미인덱스에 해당하는 더미에 링크시킨다.
//--------------------------------------------------------------------------------

bool
CObjCHAR::LinkDummy(HNODE hLinkNODE, short nDummyIDX) {
    int iDummyCnt = ::getNumDummies(m_hNodeMODEL);
    if (iDummyCnt >= nDummyIDX)
        return (0 != ::linkDummy(m_hNodeMODEL, hLinkNODE, nDummyIDX));

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  HNODE hLinkNODE 링크할 대상의 노드 핸들
/// @brief  : 입력받은 노드를 마지막 더미에 링크 시킴
//--------------------------------------------------------------------------------

bool
CObjCHAR::Link2LastDummy(HNODE hLinkNODE) {
    /*
        int iDummyCnt = ::getNumDummies( m_hNodeMODEL );
        // 마지막 더미에...
        if ( iDummyCnt > 0 )
            m_iLastDummyIDX = iDummyCnt - 1;
    */
    return (0 != ::linkDummy(m_hNodeMODEL, hLinkNODE, m_iLastDummyIDX));
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 현재 캐릭터를 씬에 넣는다. ( 모든 하위 오브젝트 포함 )
//--------------------------------------------------------------------------------

void
CObjCHAR::InsertToScene(void) {
    if (m_bIsVisible || !m_hNodeMODEL || !m_pCharMODEL)
        return;

    m_bIsVisible = true;

    ::insertToScene(m_hNodeMODEL); // CObjCHAR::InsertToScene

    //----------------------------------------------------------------------------------------------------
    /// @brief CHILD의InsertToScene제거
    //----------------------------------------------------------------------------------------------------
    // for (short nP=0; nP<MAX_BODY_PART; nP++)
    //{
    //	CMODEL<CCharPART> *pCharPART = m_pCharMODEL->GetCharPART( nP );

    //	if ( pCharPART  )
    //	{
    //		short nI;

    //		if ( m_pppEFFECT[ nP ] )
    //		{
    //			for (nI=0; nI<pCharPART->m_nDummyPointCNT; nI++)
    //			{
    //				if ( m_pppEFFECT[ nP ][ nI ] )
    //				{
    //					m_pppEFFECT[ nP ][ nI ]->InsertToScene ();
    //				}
    //			}
    //		}

    //		for (nI=0; nI<pCharPART->m_nPartCNT; nI++)
    //		{
    //			if ( m_phPartVIS[ nP ][ nI ] )
    //			{
    //				::insertToScene( m_phPartVIS[ nP ][ nI ] );		// CObjCHAR::InsertToScene
    //			}
    //		}
    //	}
    //}

    //
    //// 뼈대 효과.
    // if ( m_ppBoneEFFECT )
    //{
    //	for (nP=0; nP<m_pCharMODEL->GetBoneEffectCNT(); nP++)
    //		m_ppBoneEFFECT[ nP ]->InsertToScene ();
    //}

    //// 검잔상 효과.
    // for (nP=0; nP<2; nP++)
    //{
    //	if ( m_hTRAIL[ nP ] )
    //		::insertToScene( m_hTRAIL[ nP ] );
    //}
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  bool bIncludeEFFECT 이펙트가 포함되었는가?
/// @brief  : 씬에서 제거한다. ( 하위 오브젝트 포함 )
//--------------------------------------------------------------------------------

void
CObjCHAR::RemoveFromScene(bool bIncludeEFFECT) {
    if (!m_bIsVisible)
        return;

    m_bIsVisible = false;
    ::removeFromScene(m_hNodeMODEL); // CObjCHAR::RemoveFromScene

    //----------------------------------------------------------------------------------------------------
    /// @brief CHILD의removeFromScene제거
    //----------------------------------------------------------------------------------------------------
    // for (short nP=0; nP<MAX_BODY_PART; nP++)
    //{
    //	CMODEL<CCharPART> *pCharPART = m_pCharMODEL->GetCharPART( nP );

    //	if ( pCharPART  )
    //	{
    //		short nI;
    //		if ( bIncludeEFFECT && m_pppEFFECT[ nP ] )
    //		{
    //			for (nI=0; nI<pCharPART->m_nDummyPointCNT; nI++)
    //				if ( m_pppEFFECT[ nP ][ nI ] ) {
    //					m_pppEFFECT[ nP ][ nI ]->RemoveFromScene ();
    //				}
    //		}

    //		for (nI=0; nI<pCharPART->m_nPartCNT; nI++) {
    //			if ( m_phPartVIS[ nP ][ nI ] ) {
    //				::removeFromScene( m_phPartVIS[ nP ][ nI ] );	// CObjCHAR::RemoveFromScene
    //			}
    //		}
    //	}
    //}

    // if ( bIncludeEFFECT )
    //{
    //	// 뼈대 효과.
    //	if ( m_ppBoneEFFECT )
    //	{
    //		for (nP=0; nP<m_pCharMODEL->GetBoneEffectCNT(); nP++)
    //		{
    //			if( m_ppBoneEFFECT[ nP ] )
    //				m_ppBoneEFFECT[ nP ]->RemoveFromScene ();
    //		}
    //	}

    //	for (nP=0; nP<2; nP++) {
    //		if ( m_hTRAIL[ nP ] )
    //			::removeFromScene( m_hTRAIL[ nP ] );
    //	}
    //}
}

/*override*/ D3DXVECTOR3&
CObjCHAR::Get_CurPOS() {
    m_PosCUR.z = ::getPositionZ(m_hNodeMODEL);

    return m_PosCUR;
}

/*override*/ void
CObjCHAR::SetEffectedSkillFlag(bool bResult) {
    m_bProcEffectedSkill = bResult;
}

/*override*/ bool
CObjCHAR::bCanActionActiveSkill() {
    return m_bProcEffectedSkill;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  CEffect *pEffect 링크할 이펙트
/// @param  short nPartIDX	 링크할 캐릭터 파트
/// @param  short nPointIDX  링크할 캐릭터 파트의 포인터 인덱스
/// @brief  : 이펙트를 캐릭터의 특정 파트에 속한 특정 포인터오브젝트에 링크한다.
//--------------------------------------------------------------------------------

bool
CObjCHAR::LinkEffectToPOINT(CEffect* pEffect, short nPartIDX, short nPointIDX) {
    CMODEL<CCharPART>* pCharPART;
    pCharPART = m_pCharMODEL->GetCharPART(nPartIDX);

    if (pCharPART && (!pCharPART->m_pDummyPoints || nPointIDX >= pCharPART->m_nDummyPointCNT)) {
        _ASSERT(FALSE);

        return false;
    }

    pEffect->Transform(pCharPART->m_pDummyPoints[nPointIDX].m_Transform);
    pEffect->LinkNODE(m_phPartVIS[nPartIDX][pCharPART->m_pDummyPoints[nPointIDX].m_nParent]);

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  CEffect *pEffect 링크할 이펙트
/// @param  int iPointNO  링크할 더미 포인터 인덱스
/// @brief  : 이펙트를 캐릭터의 특정 더미에 링크
//--------------------------------------------------------------------------------

bool
CObjCHAR::LinkEffectToDUMMY(CEffect* pEffect, short nDummyIDX) {
    int iDummyCnt = ::getNumDummies(m_hNodeMODEL);
    if (iDummyCnt < nDummyIDX)
        return false;

    pEffect->LinkDUMMY(this->GetZMODEL(), nDummyIDX);

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  CMODEL<CCharPART> *pCharPART 캐릭터의 특정파트( 오른손이나, 왼손 무기가 되겠지? )
/// @param  nPartIDX 바디 파트 인덱스( 오른손 무기, 왼손무기 의 인덱스만 들어와야된다 )
/// @param  bool bLinkBONE		본에 링크 할것인가?
/// @param  int iColorNO		검잔상 칼라 번호
/// @param  int iDuration		지속시간?
/// @param  int iBaseDummyIDX	오른손, 왼손 구분을 위한 베이스 본 인덱스
/// @brief  : 양손에 무기를 들수 있으므로 두개설정 가능하다.
//--------------------------------------------------------------------------------

void
CObjCHAR::LoadTRAIL(CMODEL<CCharPART>* pCharPART,
    short nPartIDX,
    bool bLinkBONE,
    int iColorNO,
    int iDuration,
    int iBaseDummyIDX) {
    if (pCharPART->m_nDummyPointCNT < 3)
        return;

    short nTrailIDX = nPartIDX - BODY_PART_WEAPON_R;
    _ASSERT(nTrailIDX >= 0 && nTrailIDX < 2);

    m_hTRAIL[nTrailIDX] = ::loadTrail(NULL, // ZSTRING pName,
        10, // float fDistancePerPoint,
        iDuration, // int iDurationInMilliSecond,
        1, // int bUseCurve,
        g_GameDATA.m_TrailTexture.Get(), // ZSTRING pTexturePath,
        g_dwCOLOR[iColorNO], // D3DCOLOR,
        // 0, 0, 0,								// float fSP_X, float fSP_Y, float fSP_Z,
        pCharPART->m_pDummyPoints[0 + iBaseDummyIDX].m_Transform,
        // 0, 0, 100 );							// float fEP_X, float fEP_Y, float fEP_Z );
        pCharPART->m_pDummyPoints[1 + iBaseDummyIDX].m_Transform);

    if (m_hTRAIL[nTrailIDX]) {
        ::controlTrail(m_hTRAIL[nTrailIDX], 0); // stop !!

        //----------------------------------------------------------------------------------------------------
        /// @brief CHILD의InsertToScene제거
        //----------------------------------------------------------------------------------------------------
        // if ( m_bIsVisible )		// 현재 보이면..
        //	::insertToScene( m_hTRAIL[ nTrailIDX ] );
    }

    if (bLinkBONE) {
        ::linkDummy(m_hNodeMODEL, m_hTRAIL[nTrailIDX], nTrailIDX + DUMMY_IDX_R_HAND);
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  nPartIDX 바디 파트 인덱스( 오른손 무기, 왼손무기 의 인덱스만 들어와야된다 )
/// @brief  : 검잔상 해제
//--------------------------------------------------------------------------------

void
CObjCHAR::UnloadTRAIL(short nPartIDX) {
    short nTrailIDX = nPartIDX - BODY_PART_WEAPON_R;

    _ASSERT(nTrailIDX >= 0 && nTrailIDX < 2);

    if (m_hTRAIL[nTrailIDX]) {
        ::controlTrail(m_hTRAIL[nTrailIDX], 0); // stop !!
        ::unlinkNode(m_hTRAIL[nTrailIDX]);
        ::unloadTrail(m_hTRAIL[nTrailIDX]);
        m_hTRAIL[nTrailIDX] = NULL;
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 캐릭터에 붙는 효과( 모델제작상 캐릭터 툴에서 제작된)를 링크
//--------------------------------------------------------------------------------

void
CObjCHAR::Link_EFFECT(void) {
    short nP;

    for (nP = 0; nP < MAX_BODY_PART; nP++) {
        if (NULL == m_pppEFFECT[nP])
            continue;

        CMODEL<CCharPART>* pCharPART = m_pCharMODEL->GetCharPART(nP);
        _ASSERT(pCharPART);

        for (short nI = 0; nI < pCharPART->m_nDummyPointCNT; nI++) {
            if (NULL != m_pppEFFECT[nP][nI])
                m_pppEFFECT[nP][nI]->LinkNODE(
                    m_phPartVIS[nP][pCharPART->m_pDummyPoints[nI].m_nParent]);
        }
    }

    for (nP = 0; nP < 2; nP++) {
        if (m_hTRAIL[nP]) {
            ::linkDummy(m_hNodeMODEL, m_hTRAIL[nP], nP + DUMMY_IDX_R_HAND);
        }
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 캐릭터에 붙는 효과( 모델제작상 캐릭터 툴에서 제작된)를 언 링크
//--------------------------------------------------------------------------------

void
CObjCHAR::Unlink_EFFECT(void) {
    short nP;

    for (nP = 0; nP < 2; nP++) {
        if (m_hTRAIL[nP]) {
            ::unlinkNode(m_hTRAIL[nP]);
        }
    }
    for (nP = 0; nP < MAX_BODY_PART; nP++) {
        if (NULL == m_pppEFFECT[nP])
            continue;

        // 아이템이 바뀌면서 이펙트가 먼저 생성된다.
        // set part model이 호출되기전에 pCharPART가 바뀐 아이템으로
        // 설정되지 않아 뻑~
        for (short nI = 0; nI < m_nEffectPointCNT[nI]; nI++) {
            if (NULL != m_pppEFFECT[nP][nI])
                m_pppEFFECT[nP][nI]->UnlinkNODE();
        }
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  CMODEL<CCharPART> *pCharPART 이펙트를 추가할 캐릭터의 파트
/// @param  short nPartIDX	   파트 인덱스
/// @param  short nPointIDX	   파트의 포인터 인덱스
/// @param  t_HASHKEY HashEffectFILE  이펙트파일의 해쉬키
/// @param  bool bLinkNODE			  링크할것인가?
/// @brief  : 캐릭터에 붙는 효과( 모델제작상 캐릭터 툴에서 제작된)를 추가
///	@bug    : 04/4/28 g_pEffectLIST->Add_EFFECT(.., false ) 였는데.. g_pEffectLIST->Del_EFFECT 로
///삭제를 의뢰했다..
/// @bug    : 04/4/28 링크할때 Rotation은 안하네?
//--------------------------------------------------------------------------------

void
CObjCHAR::Add_EFFECT(CMODEL<CCharPART>* pCharPART,
    short nPartIDX,
    short nPointIDX,
    t_HASHKEY HashEffectFILE,
    bool bLinkNODE) {
    if (NULL == pCharPART)
        return;

    if (nPointIDX >= pCharPART->m_nDummyPointCNT)
        return;

    if (m_pppEFFECT[nPartIDX]) {
        // 기존 포인트의 효과 삭제...
        /// g_pEffectLIST->Del_EFFECT( m_pppEFFECT[ nPartIDX ][ nPointIDX ] );
        SAFE_DELETE(m_pppEFFECT[nPartIDX][nPointIDX]);
        m_pppEFFECT[nPartIDX][nPointIDX] = NULL;
    } else {
        m_nEffectPointCNT[nPartIDX] = pCharPART->m_nDummyPointCNT;
        m_pppEFFECT[nPartIDX] = new CEffect*[pCharPART->m_nDummyPointCNT];
        for (short nI = 0; nI < pCharPART->m_nDummyPointCNT; nI++)
            m_pppEFFECT[nPartIDX][nI] = NULL;
    }

    m_pppEFFECT[nPartIDX][nPointIDX] = g_pEffectLIST->Add_EFFECT(HashEffectFILE);
    if (m_pppEFFECT[nPartIDX][nPointIDX]) {
#ifdef _DEBUG
        if (!pCharPART->m_pDummyPoints || nPointIDX >= pCharPART->m_nDummyPointCNT) {
            _ASSERT(FALSE);
        }
#endif

        m_pppEFFECT[nPartIDX][nPointIDX]->Transform(
            pCharPART->m_pDummyPoints[nPointIDX].m_Transform);

        if (m_bIsVisible)
            m_pppEFFECT[nPartIDX][nPointIDX]->InsertToScene();

        if (bLinkNODE && m_phPartVIS[nPartIDX])
            m_pppEFFECT[nPartIDX][nPointIDX]->LinkNODE(
                m_phPartVIS[nPartIDX][pCharPART->m_pDummyPoints[nPointIDX].m_nParent]);
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  short nPartIDX	   파트 인덱스
/// @param  short nPointIDX	   파트의 포인터 인덱스
/// @param  t_HASHKEY HashEffectFILE  이펙트파일의 해쉬키
/// @brief  : 캐릭터에 붙는 효과( 모델제작상 캐릭터 툴에서 제작된)를 추가
///	@bug    : 내부에서 Add_EFFECT( CMODEL<CCharPART> *pCharPART, short nPartIDX, short nPointIDX,
/// t_HASHKEY HashEffectFILE, bool bLinkNODE ) 함수 호출
//--------------------------------------------------------------------------------

void
CObjCHAR::Add_EFFECT(short nPartIDX, short nPointIDX, t_HASHKEY HashEffectFILE) {
    CMODEL<CCharPART>* pCharPART;
    pCharPART = m_pCharMODEL->GetCharPART(nPartIDX);
    this->Add_EFFECT(pCharPART, nPartIDX, nPointIDX, HashEffectFILE);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  short nPartIDX	   파트 인덱스
/// @brief  : 캐릭터에 붙는 효과( 모델제작상 캐릭터 툴에서 제작된)를 삭제
///	@bug    : /// @Bug AddEffect( .. , false ) 버그..
//--------------------------------------------------------------------------------

void
CObjCHAR::Del_EFFECT(short nPartIDX) {
    CMODEL<CCharPART>* pCharPART;
    pCharPART = m_pCharMODEL->GetCharPART(nPartIDX);
    if (pCharPART) {
        if (m_pppEFFECT[nPartIDX]) {
            for (short nT = 0; nT < pCharPART->m_nDummyPointCNT; nT++) {
                /// g_pEffectLIST->Del_EFFECT( m_pppEFFECT[ nPartIDX ][ nT ] );
                if (m_pppEFFECT[nPartIDX][nT]) {
                    delete m_pppEFFECT[nPartIDX][nT];
                    m_pppEFFECT[nPartIDX][nT] = NULL;
                }
                m_pppEFFECT[nPartIDX][nT] = NULL;
            }
        }
    }

    SAFE_DELETE_ARRAY(m_pppEFFECT[nPartIDX]);

    switch (nPartIDX) {
        case BODY_PART_WEAPON_R:
        case BODY_PART_WEAPON_L:
            this->UnloadTRAIL(nPartIDX);
            break;
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  short nPartIDX	   파트 인덱스
/// @param  short nItemNo	   파트의 아이템 인덱스
/// @param  bool bLinkNODE	   링크할꺼냐?
/// @brief  : 캐릭터에 붙는 효과를 생성
///				무기의 잔상, 또한 특별한 옵션에 의해 무기또는 특정 부위에 효과를 붙여야할때..
//--------------------------------------------------------------------------------

void
CObjCHAR::New_EFFECT(short nPartIdx, short nItemNo, bool bLinkNODE) {
    // 아이템에 붙은 기본 효과 삭제.
    this->Del_EFFECT(nPartIdx);

    CMODEL<CCharPART>* pCharPART = g_DATA.Get_CharPartMODEL(nPartIdx,
        nItemNo,
        this->IsFemale()); // m_pMD_CharPART[ nPartIdx ]->GetMODEL( nItemNo );
    if (NULL == pCharPART)
        return;

    short nEffectIDX = 0;
    switch (nPartIdx) {
        case BODY_PART_WEAPON_R: {
            nEffectIDX = WEAPON_DEFAULT_EFFECT(nItemNo);

            if (EFFECT_TRAIL_NORMAL(nEffectIDX)) {
                this->LoadTRAIL(pCharPART,
                    BODY_PART_WEAPON_R,
                    bLinkNODE,
                    EFFECT_TRAIL_NORMAL(nEffectIDX),
                    EFFECT_TRAIL_DURATION(nEffectIDX));

                /// 양손 이도류 무기일경우..
                if ((pCharPART->m_nDummyPointCNT > 2)
                    && ((WEAPON_TYPE(nItemNo) == 251) || (WEAPON_TYPE(nItemNo) == 252)))
                    this->LoadTRAIL(pCharPART,
                        BODY_PART_WEAPON_L,
                        bLinkNODE,
                        EFFECT_TRAIL_NORMAL(nEffectIDX),
                        EFFECT_TRAIL_DURATION(nEffectIDX),
                        2);
            }
        } break;
        case BODY_PART_WEAPON_L:
            // nEffectIDX = SUBWPN_DEFAULT_EFFECT( nItemNo );
            if (EFFECT_TRAIL_NORMAL(nEffectIDX)) {
                this->LoadTRAIL(pCharPART,
                    BODY_PART_WEAPON_L,
                    bLinkNODE,
                    EFFECT_TRAIL_NORMAL(nEffectIDX),
                    EFFECT_TRAIL_DURATION(nEffectIDX));
            }
            break;
        default:
            return;
    }

    if (nEffectIDX) {
        t_HASHKEY EffectHASH;
        for (short nP = 0; nP < EFFECT_POINT_CNT; nP++) {
            EffectHASH = g_pEffectLIST->Get_IndexHashKEY(EFFECT_POINT(nEffectIDX, nP));
            if (EffectHASH)
                this->Add_EFFECT(pCharPART, nPartIdx, nP, EffectHASH, bLinkNODE);
        }
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  char *szName 이름( 내부세어 생성 오더번호를 붙여 고유한 이름을 생성한다. )
/// @param  int iPartIDX 파트 인덱스
/// @brief  : 특정 부위 생성
//--------------------------------------------------------------------------------

void
CObjCHAR::CreateSpecificPART(char* szName, int iPartIDX) {
    m_phPartVIS[iPartIDX] = m_pCharMODEL->CreatePART(szName, m_hNodeMODEL, iPartIDX);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  int iPartIDX 파트 인덱스
/// @param  HNODE *pVIS  파트를 구성하는 부분들의 visiable 노드 배열
/// @brief  : 특정 부위 삭제
//--------------------------------------------------------------------------------

void
CObjCHAR::DeleteSpecificPART(short nPartIdx, HNODE* pVIS) {
    m_pCharMODEL->DeletePART(nPartIdx, pVIS);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  char *szName 이름
/// @brief  : 캐릭터 각파트 생성, 내부에서 각파트별로 CreateSpecificPART 생성
//--------------------------------------------------------------------------------

void
CObjCHAR::CreatePARTS(char* szName) {
    m_pCharMODEL->ClearRenderUnitParts();

    // npc 무기땜에 MAX_BODY_PART까지 ...
    for (short nP = 0; nP < MAX_BODY_PART; nP++) {
        CreateSpecificPART(szName, nP);
        // m_phPartVIS[ nP ] = m_pCharMODEL->CreatePART( szName, m_hNodeMODEL, nP );
    }

    // 케릭터 신장.
    m_fStature = ::getModelHeight(this->m_hNodeMODEL);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  bool bDelEFFECT 이펙트도 지우냐?
/// @brief  : 캐릭터 각 파트 삭제
//--------------------------------------------------------------------------------

void
CObjCHAR::DeletePARTS(bool bDelEFFECT) {
    this->RemoveFromScene(bDelEFFECT);

    if (this->m_hNodeMODEL) {
        // addRenderUnit된것들 삭제.
        ::clearRenderUnit(this->m_hNodeMODEL);
    }

    // loadVisible된것들 삭제.
    for (short nP = 0; nP < MAX_BODY_PART; nP++) {
        DeleteSpecificPART(nP, m_phPartVIS[nP]);
        // m_pCharMODEL->DeletePART( nP, m_phPartVIS[ nP ] );
        m_phPartVIS[nP] = NULL;
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  char *szName 모델 이름
/// @brief  : 캐릭터의 엔진모델노드 생성.
//--------------------------------------------------------------------------------

bool
CObjCHAR::LoadModelNODE(char* szName) {
    HNODE hSkel = this->Get_SKELETON();
    if (hSkel == NULL) {
        LogString(LOG_DEBUG_, "failed creat char.. SKEL == NULL !! \n");
        return false;
    }

    if (!this->Set_CurMOTION(this->Get_MOTION(0))) { // default stop motion ..
        LogString(LOG_DEBUG_, "failed creat char.. MOTION == NULL !! \n");
        return false;
    }

    m_hNodeMODEL = ::loadModel(szName, hSkel, this->Get_ZMOTION(), 1.0f);
    if (m_hNodeMODEL) {
        ::setCollisionLevel(m_hNodeMODEL, 4);

        // 기본 지형에서 캐릭터 중심점 높이
        m_fHeightOfGround = ::getPositionZ(this->m_hNodeMODEL);
        m_iLastDummyIDX = ::getNumDummies(m_hNodeMODEL) - 1;

        ::setScale(m_hNodeMODEL, m_fScale, m_fScale, m_fScale);
        ::setPosition(m_hNodeMODEL, m_PosCUR.x, m_PosCUR.y, m_PosCUR.z);

        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 캐릭터의 엔진모델노드 삭제. 외부에서 등록된 모든 이펙트 들도 삭제
//--------------------------------------------------------------------------------

void
CObjCHAR::UnloadModelNODE() {
    /// 외부에서 등록된 이펙트들도 다 삭제..
    ClearExternalEffect();

    if (m_hNodeMODEL) {
        ::unloadModel(m_hNodeMODEL);
        m_hNodeMODEL = NULL;
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param char *szName			이름
/// @param CCharMODEL *pMODEL	캐릭터 모델 정보를 관리할.. 모델클래스
/// @param short nCharPartCNT	캐릭터 파트 카운트
/// @param D3DVECTOR &Position	생성할 캐릭터 위치
/// @brief  : 캐릭터 생성
///				1. 캐릭터 모델노드 생성
///				2. 캐릭터 파트들 생성
///				3. 본 이펙트( 캐릭터 툴에서 설정된 ) 생성
//--------------------------------------------------------------------------------

bool
CObjCHAR::CreateCHAR(char* szName,
    CCharMODEL* pMODEL,
    short nCharPartCNT,
    const D3DVECTOR& Position) {
    m_bIsVisible = false;

    m_PosBORN = Position;
    m_PosCUR = Position;
    m_pCharMODEL = pMODEL;

    if (pMODEL == NULL || nCharPartCNT <= 0)
        return false;

    if (this->LoadModelNODE(szName)) {
        this->CreatePARTS(szName);

        m_ppBoneEFFECT = m_pCharMODEL->CreateBoneEFFECT(m_hNodeMODEL, this);
        this->InsertToScene();

        this->SetCMD_STOP();

        DropFromSky(Position.x, Position.y);
        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 캐릭터 삭제
///				1. 본 이펙트( 캐릭터 툴에서 설정된 ) 삭제
///				2. 캐릭터 파트들 삭제
///				3. 캐릭터 모델노드 삭제
///				4. 엔진 객체 삭제( visiable )
//--------------------------------------------------------------------------------

void
CObjCHAR::DeleteCHAR(void) {
    this->RemoveFromScene();

    m_pCharMODEL->DeleteBoneEFFECT(m_ppBoneEFFECT);

    short nP;
    for (nP = 0; nP < MAX_BODY_PART; nP++)
        this->Del_EFFECT(nP);

    this->DeletePARTS();
    this->UnloadModelNODE();

    for (short nI = 0; nI < MAX_BODY_PART; nI++) {
        SAFE_DELETE_ARRAY(m_phPartVIS[nI]);
    }

    if (m_ClanMarkUserDefined) {
        m_ClanMarkUserDefined->Release();
        m_ClanMarkUserDefined = NULL;
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  t_HASHKEY HashMOTION 모션의 해쉬키
/// @param  float fMoveSpeed	 이동 속도
/// @param  int iRepeatCnt		 반복카운트
/// @brief  : 유져모션 세팅
//--------------------------------------------------------------------------------

void
CObjCHAR::Set_UserMOITON(t_HASHKEY HashMOTION, float fMoveSpeed, int iRepeatCnt) {
    tagMOTION* pMotion = g_MotionFILE.KEY_GetMOTION(HashMOTION);

    Set_UserMOITON(pMotion, fMoveSpeed, iRepeatCnt);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  tagMOTION* pMotion   모션.
/// @param  float fMoveSpeed	 이동 속도
/// @param  int iRepeatCnt		 반복카운트
/// @brief  : 유져모션 세팅
//--------------------------------------------------------------------------------

void
CObjCHAR::Set_UserMOITON(tagMOTION* pMotion, float fMoveSpeed, int iRepeatCnt) {
    // 현재 진행중인 모션과 같을경우 모션이 업데이트 되지 않으므로
    // 이동 속도 설정을 Chg_CurMOTION밖으로 꺼냄..
    this->Set_ModelSPEED(fMoveSpeed);

    if (this->Chg_CurMOTION(pMotion)) {

#ifndef __VIRTUAL_SERVER
        _ASSERT(fMoveSpeed >= 0.f && fMoveSpeed < 2000.f);
#endif

        ::attachMotion(this->m_hNodeMODEL, this->Get_ZMOTION());
        ::setAnimatableSpeed(this->m_hNodeMODEL, 1.0f);
        ::setRepeatCount(this->GetZMODEL(), iRepeatCnt);

        // 본 애니가 없는 경우에도 메쉬 애니가 있을수 있나????
        this->m_pCharMODEL->SetMeshMOTION(m_phPartVIS, this->Get_ActionIDX());
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  short nActionIdx 모션의 해쉬키
/// @param  float fMoveSpeed	 이동 속도
/// @param  float fAniSpeed		 반복카운트
/// @param  float bool bAttackMotion	 반복카운트
/// @brief  : 모션 세팅
/// @bug 첨에 여기에 공격시작 사운드 넣었다가.. 프레임에 정보를 넣고 정확한 프레임에서 시작하게
/// 옮겼다.
//--------------------------------------------------------------------------------

void
CObjCHAR::Set_MOTION(short nActionIdx,
    float fMoveSpeed,
    float fAniSpeed,
    bool bAttackMotion,
    int iRepeatCnt) {

    if (!bAttackMotion && m_bAttackSTART) {
        Attack_END();
    }

    if (this->Chg_CurMOTION(this->Get_MOTION(nActionIdx))) {

#ifndef __VIRTUAL_SERVER
//		_ASSERT( fMoveSpeed >= 0.f && fMoveSpeed < 2000.f );
#endif

        this->Set_ModelSPEED(fMoveSpeed);

        ::attachMotion(this->m_hNodeMODEL, this->Get_ZMOTION());
        ::setAnimatableSpeed(this->m_hNodeMODEL, fAniSpeed);
        ::setRepeatCount(this->m_hNodeMODEL, iRepeatCnt);

        // 본 애니가 없는 경우에도 메쉬 애니가 있을수 있나????
        this->m_pCharMODEL->SetMeshMOTION(m_phPartVIS, this->Get_ActionIDX());
    }

    ::controlAnimatable(this->m_hNodeMODEL, 0);
    ::controlAnimatable(this->m_hNodeMODEL, 1);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  D3DXVECTOR3& Pos 갱신할 위치
/// @brief  : 현재위치 갱신
//--------------------------------------------------------------------------------

void
CObjCHAR::ResetCUR_POS(D3DXVECTOR3& Pos) {
    CGameOBJ::Set_CurPOS(Pos);
    ::setPositionVec3(m_hNodeMODEL, m_PosCUR);
}

bool
CObjCHAR::Skill_START(CObjCHAR* pTarget) {
    if (m_nActiveSkillIDX) {
        /// 근접 즉시 발동 스킬은 검잔상 발동 )
        if (SKILL_TYPE(m_nActiveSkillIDX) == SKILL_ACTION_IMMEDIATE) {
            // 검잔상 시작.
            for (short nI = 0; nI < 2; nI++) {
                if (m_hTRAIL[nI]) {
                    ::controlTrail(m_hTRAIL[nI], 0); // stop & clear
                    ::controlTrail(m_hTRAIL[nI], 1); // start
                }
            }
        }

        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  CObjCHAR *pTarget 타겟 오브젝트
/// @brief  : 공격 시작.. 검잔상 애니매이션 시작..
//--------------------------------------------------------------------------------

bool
CObjCHAR::Attack_START(CObjCHAR* pTarget) {
    /*
    // 무기 붙이기 / 검잔상...
    CObjCHAR *pTarget = g_pObjMGR->Get_CharOBJ( this->m_iTargetObject );
    int iBulletIDX = Get_BulletNO ();
    if ( pTarget && iBulletIDX ) {
        g_pBltMGR->Add_BULLET( this, pTarget, iBulletIDX );
    }
    */

    // 검잔상 시작.
    for (short nI = 0; nI < 2; nI++) {
        if (m_hTRAIL[nI]) {
            ::controlTrail(m_hTRAIL[nI], 0); // stop & clear
            ::controlTrail(m_hTRAIL[nI], 1); // start
        }
    }

    m_bAttackSTART = true;
    m_iActiveObject = g_pObjMGR->Get_ClientObjectIndex(m_iServerTarget);

    _ASSERT(m_iServerTarget == g_pObjMGR->Get_ServerObjectIndex(m_iActiveObject));

    /// 지속 속성중 공격시작하면 풀려야 되는것들
    m_EndurancePack.ClearStateByAttack();

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  bool bStopTrail 검잔상 정지?
/// @brief  : 공격 끝.
//--------------------------------------------------------------------------------

void
CObjCHAR::Attack_END(bool bStopTrail) {
    // 기본 애니메이션 속도 복귀.
    ::setAnimatableSpeed(GetZMODEL(), 1.0);

    // 검잔상 효과 모션정지..
    if (bStopTrail) {
        for (short nI = 0; nI < 2; nI++) {
            if (m_hTRAIL[nI]) {
                ::controlTrail(m_hTRAIL[nI], 3); // no spawn
            }
        }
    }

    m_bAttackSTART = false;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  CObjCHAR *pTarget 타겟 오브젝트
/// @brief  : 캐스팅 동작시작..( 스킬의 시작을 표시한다. ) 아주 중요../
///				유져일경우는 타이머를 세팅한다. 스킬의 딜레이가 있기때문에.
//--------------------------------------------------------------------------------

bool
CObjCHAR::Casting_START(CObjCHAR* pTarget) {
    SetCastingState(true);
    /// m_bProcEffectedSkill = false;

    //--------------------------------------------------------------------------------
    /// 유져일경우는 타이머를 세팅한다.
    //--------------------------------------------------------------------------------
    // if( this->IsA( OBJ_USER ) )
    //{
    //	/// 타이머 설정
    //	CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
    //	CSkill* pSkill = pSkillSlot->GetSkillBySkillIDX( m_nToDoSkillIDX );
    //	if( pSkill )
    //	{
    //		pSkill->SetSkillDelayTime( SKILL_RELOAD_TIME( m_nToDoSkillIDX ) * 200 );
    //	}
    //}

    /// 지속 속성중 공격시작하면 풀려야 되는것들
    if (SKILL_TYPE(m_nToDoSkillIDX) == SKILL_ACTION_IMMEDIATE
        || SKILL_TYPE(m_nToDoSkillIDX) == SKILL_ACTION_FIRE_BULLET
        || SKILL_TYPE(m_nToDoSkillIDX) == SKILL_ACTION_SELF_DAMAGE) {
        m_EndurancePack.ClearStateByAttack();
    }

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 캐스팅 동작끝..( 스킬의 끝을 표시한다. ) 아주 중요../
//--------------------------------------------------------------------------------

void
CObjCHAR::Casting_END() {
    /// 현재 활성화된 스킬이 있다면.. 캐스팅 상태 유지..
    SetCastingState(false);
    m_nActiveSkillIDX = 0;
    m_nToDoSkillIDX = 0;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 무브 시작
//--------------------------------------------------------------------------------
void
CObjCHAR::MoveStart() {
    /// 이동과 동시에 자동으로 풀려야 하는것들..
    m_EndurancePack.ClearStateByMove();
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 총알번호를 구한다.
//--------------------------------------------------------------------------------

int
CObjCHAR::Get_BulletNO() {
    short nWeaponItem = Get_R_WEAPON();
    if (nWeaponItem)
        return WEAPON_BULLET_EFFECT(nWeaponItem);

    return 0;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 죽는 처리..( 강제로 캐스팅을 끝내는 루틴이 들어갔다.. 이것도 상당히 중요 )
///				m_nActiveSkillIDX 도 리셋.. 죽었을경우.. 이 플래그가 세팅되어 있으면. Casting_End 가
///무효해진다.
//--------------------------------------------------------------------------------

void
CObjCHAR::Dead() {
    if (Get_HP() <= DEAD_HP) {
        SetCastingState(false);
        return;
    }

    // This character is about to be interrupted into its death motion, so any
    // attack it had in flight will never reach a hit frame. Drain the combat
    // damage it already queued on the avatar so the server-applied HP folds into
    // the avatar's next presented hit instead of leaving the bars desynced.
    // (See client CLAUDE.md — Combat Presentation System.)
    if (g_pAVATAR && this != g_pAVATAR) {
        g_pAVATAR->DrainQueuedCombatDamageFromAttacker(this);
    }

    //----------------------------------------------------------------------------------------------------
    /// 아바타가 죽을경우
    //----------------------------------------------------------------------------------------------------
    if (this->Is_AVATAR()) {
        CObjAVT* pAVT = (CObjAVT*)this;
        /// Pet 상태라면 내린다.
        if (GetPetMode() >= 0) {
            pAVT->DeleteCart(false);

            //----------------------------------------------------------------------------------------------------
            /// 죽을때 팻의 파괴 이펙트 출력..
            //----------------------------------------------------------------------------------------------------
            if (pAVT->IsVisible()) {
                int iEffectNO = PAT_DEAD_EFFECT(pAVT->m_sBodyIDX.m_nItemNo);
                int iSoundNO = PAT_DEAD_SOUND(pAVT->m_sBodyIDX.m_nItemNo);

                this->ShowEffectOnCharByIndex(iEffectNO, iSoundNO);
            }

            m_btMoveMODE = MOVE_MODE_RUN;
            pAVT->Update_SPEED();
        } else {
            //----------------------------------------------------------------------------------------------------
            /// 캐릭터 죽을때의 이펙트 출력
            //----------------------------------------------------------------------------------------------------
            SE_CharDie(this->Get_INDEX());
        }

        /// 개인상점 관련 리셋..
        if (pAVT->IsPersonalStoreMode()) {
            pAVT->SetPersonalStoreTitle(NULL);
            g_UIMed.SubPersonalStoreIndex(this->Get_INDEX());
        }

        /// 만약 내가 죽은 거라면..
        if (this->IsA(OBJ_USER)) {
            ((CObjUSER*)pAVT)->ClearSummonedMob();
            g_itMGR.ChangeState(IT_MGR::STATE_DEAD);
        }
    }

    //----------------------------------------------------------------------------------------------------
    /// 몹이 죽을경우 이펙트 출력
    //----------------------------------------------------------------------------------------------------
    if (this->IsA(OBJ_MOB)) {
        /// SE_MobDie( this->Get_INDEX() );
        int iMobDeadEffect = NPC_DEAD_EFFECT(this->Get_CharNO());
        int iSoundIDX = NPC_DIE_SOUND(this->Get_CharNO());
        this->ShowEffectOnCharByIndex(iMobDeadEffect, iSoundIDX);
    }

    Set_HP(DEAD_HP);
    SetCMD_DIE();

    m_nActiveSkillIDX = 0;
    SetCastingState(false);

    m_EndurancePack.ClearEntityPack();
    ClearAllEntityList();

    // 루프방지.
    ::setRepeatCount(m_hNodeMODEL, 1);
    // 죽은넘은 클릭 안되게...

    if (this->Is_AVATAR() == false) {
        /// 2004-11-26 죽은놈도 클릭되게...
        ::setCollisionLevel(m_hNodeMODEL, 0);
    }

    ::setModelBlinkCloseMode(this->GetZMODEL(), true);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param int iSkillIDX 스킬 인덱스
/// @param gsv_DAMAGE_OF_SKILL EffectedSkill 스킬 결과 정보
/// @param bool bDamageOfSkill	데미지 결과인가?( 범위데미지 스킬의 경우.. )
/// @brief  : 스킬결과를 액션프레임에 수행하기 위해 짱박아 둔다.
//--------------------------------------------------------------------------------

void
CObjCHAR::PushEffectedSkillToList(int iSkillIDX,
    gsv_DAMAGE_OF_SKILL EffectedSkill,
    int iCasterINT,
    bool bDamageOfSkill,
    bool bDamageEventAlreadyQueued,
    bool bWaitForProjectileImpact) {
    stEFFECT_OF_SKILL steffectOfSkill;

    steffectOfSkill.m_dwCreateTime = g_GameDATA.GetGameTime();
    steffectOfSkill.iSkillIDX = iSkillIDX;
    steffectOfSkill.bDamageOfSkill = bDamageOfSkill;
    steffectOfSkill.bDamageEventAlreadyQueued = bDamageEventAlreadyQueued;
    steffectOfSkill.bWaitForProjectileImpact = bWaitForProjectileImpact;
    steffectOfSkill.iCasterINT = iCasterINT;
    // Captured at packet receive (this call runs synchronously from the recv
    // handler), so the deferred conversion at the caster's action frame keeps the
    // source packet's arrival order rather than the later present-time order.
    steffectOfSkill.arrival_seq = NextHPAuthoritySeq();

    steffectOfSkill.EffectOfSkill = EffectedSkill;

    if (bWaitForProjectileImpact) {
        for (std::vector<stEFFECT_OF_SKILL>::iterator it = m_EffectedSkillList.begin();
             it != m_EffectedSkillList.end();
             ++it) {
            if (it->iSkillIDX != iSkillIDX
                || it->EffectOfSkill.m_wObjectIDX != EffectedSkill.m_wObjectIDX
                || !it->bWaitForProjectileImpact) {
                continue;
            }

            if (bDamageOfSkill || !it->bDamageOfSkill) {
                *it = steffectOfSkill;
                LogString(LOG_DEBUG_,
                    "CombatTrace projectile payload replaced: caster %d target %d skill %d damage_queued %d damage %d success_bits %d\n",
                    this->Get_INDEX(),
                    static_cast<int>(EffectedSkill.m_wObjectIDX),
                    iSkillIDX,
                    bDamageEventAlreadyQueued ? 1 : 0,
                    EffectedSkill.m_wDamage,
                    static_cast<int>(EffectedSkill.m_btSuccessBITS));
            } else {
                // The damage payload arrived first with success_bits cleared:
                // type-3 (SKILL_ACTION_IMMEDIATE) gun skills send the status in
                // a SEPARATE EFFECT_OF_SKILL packet (Skill_START_03_04_05 ->
                // Skill_ChangeIngSTATUS). Fold its success bits into the queued
                // damage entry so the bullet-impact drain
                // (ProcEffectOfSkillInDamageOfSkill -> ApplyEffectOfSkill) still
                // calls AddEnduranceEntity and builds the status aura. Without
                // this the status payload was dropped and Poison Fang showed DoT
                // ticks but no green poison aura.
                it->EffectOfSkill.m_btSuccessBITS |= EffectedSkill.m_btSuccessBITS;
                if (it->iCasterINT == 0)
                    it->iCasterINT = iCasterINT;
                LogString(LOG_DEBUG_,
                    "CombatTrace projectile status folded into damage payload: caster %d target %d skill %d success_bits %d\n",
                    this->Get_INDEX(),
                    static_cast<int>(EffectedSkill.m_wObjectIDX),
                    iSkillIDX,
                    static_cast<int>(it->EffectOfSkill.m_btSuccessBITS));
            }
            return;
        }
    }

    m_EffectedSkillList.push_back(steffectOfSkill);

    if (bWaitForProjectileImpact) {
        LogString(LOG_DEBUG_,
            "CombatTrace projectile payload queued: caster %d target %d skill %d damage_of_skill %d damage_queued %d damage %d success_bits %d\n",
            this->Get_INDEX(),
            static_cast<int>(EffectedSkill.m_wObjectIDX),
            iSkillIDX,
            bDamageOfSkill ? 1 : 0,
            bDamageEventAlreadyQueued ? 1 : 0,
            EffectedSkill.m_wDamage,
            static_cast<int>(EffectedSkill.m_btSuccessBITS));
    }
}

void
CObjCHAR::RegisterPendingProjectileSkill(int iServerTarget, int iSkillIDX) {
    if (iServerTarget <= 0 || iSkillIDX <= 0) {
        return;
    }

    for (std::vector<stPENDING_PROJECTILE_SKILL>::iterator it = m_PendingProjectileSkillList.begin();
         it != m_PendingProjectileSkillList.end();
         ++it) {
        if (it->iServerTarget == iServerTarget && it->iSkillIDX == iSkillIDX) {
            it->m_dwCreateTime = g_GameDATA.GetGameTime();
            return;
        }
    }

    stPENDING_PROJECTILE_SKILL pending;
    pending.m_dwCreateTime = g_GameDATA.GetGameTime();
    pending.iServerTarget = iServerTarget;
    pending.iSkillIDX = iSkillIDX;
    m_PendingProjectileSkillList.push_back(pending);

    LogString(LOG_DEBUG_,
        "CombatTrace projectile cast registered: caster %d target %d skill %d bullet %d\n",
        this->Get_INDEX(),
        iServerTarget,
        iSkillIDX,
        SKILL_BULLET_NO(iSkillIDX));
}

bool
CObjCHAR::ConsumePendingProjectileSkill(int iServerTarget, int iSkillIDX) {
    for (std::vector<stPENDING_PROJECTILE_SKILL>::iterator it = m_PendingProjectileSkillList.begin();
         it != m_PendingProjectileSkillList.end();
         ++it) {
        if (it->iServerTarget == iServerTarget && it->iSkillIDX == iSkillIDX) {
            m_PendingProjectileSkillList.erase(it);
            return true;
        }
    }
    return false;
}

void
CObjCHAR::ClearPendingProjectileSkill(int iServerTarget, int iSkillIDX) {
    for (std::vector<stPENDING_PROJECTILE_SKILL>::iterator it = m_PendingProjectileSkillList.begin();
         it != m_PendingProjectileSkillList.end();) {
        if ((iServerTarget == 0 || it->iServerTarget == iServerTarget)
            && (iSkillIDX == 0 || it->iSkillIDX == iSkillIDX)) {
            it = m_PendingProjectileSkillList.erase(it);
        } else {
            ++it;
        }
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 타임아웃 시간이 지난 스킬결과를 처리한다.
//--------------------------------------------------------------------------------
const int SKILL_PROC_LIMIT = 1000 * 10;
void
CObjCHAR::ProcTimeOutEffectedSkill() {
    stEFFECT_OF_SKILL* pEffectOfSkill = NULL;
    DWORD dwElapsedTime = 0;

    std::vector<stEFFECT_OF_SKILL>::iterator begin = m_EffectedSkillList.begin();
    for (; begin != m_EffectedSkillList.end();) {
        pEffectOfSkill = &(*begin);

        dwElapsedTime = g_GameDATA.GetGameTime() - pEffectOfSkill->m_dwCreateTime;
        if (dwElapsedTime > SKILL_PROC_LIMIT) {
            const bool bTimedOutWaitingForProjectile = pEffectOfSkill->bWaitForProjectileImpact;
            if (bTimedOutWaitingForProjectile) {
                int iObjIDX = pEffectOfSkill->EffectOfSkill.m_wObjectIDX;
                CObjCHAR* pChar = g_pObjMGR->Get_ClientCharOBJ(iObjIDX, true);
                if (pChar) {
                    pChar->DiscardQueuedCombatDamageFromAttacker(this);
                }
                ClearPendingProjectileSkill(iObjIDX, pEffectOfSkill->iSkillIDX);
                LogString(LOG_DEBUG_,
                    "CombatTrace projectile timeout discard: caster %d target %d skill %d damage_queued %d damage %d\n",
                    this->Get_INDEX(),
                    iObjIDX,
                    pEffectOfSkill->iSkillIDX,
                    pEffectOfSkill->bDamageEventAlreadyQueued ? 1 : 0,
                    pEffectOfSkill->EffectOfSkill.m_wDamage);
            } else {
                ProcOneEffectedSkill(pEffectOfSkill);
            }
            begin = m_EffectedSkillList.erase(begin);
#ifdef _DEBUG

            if (!bTimedOutWaitingForProjectile) {
                sprintf(g_MsgBuf, "ProcTimeOutEffectedSkill [ 대상 : %s ] ", this->Get_NAME());
                /// assert( 0 && Buf );
                MessageBox(NULL, g_MsgBuf, "WARNING", MB_OK);
            }
#endif //_DEBUG
        } else
            ++begin;
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief /// Damage of skill 에 실려오는 스킬적용결과 반영..
//----------------------------------------------------------------------------------------------------

void
CObjCHAR::ProcEffectOfSkillInDamageOfSkill(int iSkillIDX,
    int iObjIDX,
    CObjCHAR* pChar,
    stEFFECT_OF_SKILL* pEffectOfSkill) {
    int iSkillType = SKILL_TYPE(iSkillIDX);

    switch (iSkillType) {
        case SKILL_ACTION_SELF_AND_TARGET: {
            if (this->IsA(OBJ_USER)) {
                for (int i = 0; i < SKILL_INCREASE_ABILITY_CNT; i++) {
                    if ((0x01 << i) & pEffectOfSkill->EffectOfSkill.m_btSuccessBITS) {
                        int iAbilityType = SKILL_INCREASE_ABILITY(iSkillIDX, i);
                        switch (iAbilityType) {
                            case AT_HP:
                                g_pAVATAR->Add_HP(SKILL_INCREASE_ABILITY_VALUE(iSkillIDX, i));
                                break;
                            case AT_MP:
                                g_pAVATAR->Add_MP(SKILL_INCREASE_ABILITY_VALUE(iSkillIDX, i));
                                break;

                            case AT_STAMINA:
                                g_pAVATAR->AddCur_STAMINA(
                                    SKILL_INCREASE_ABILITY_VALUE(iSkillIDX, i));
                                break;
                        }
                    }
                }
            }
        } break;

        default:
            ApplyEffectOfSkill(iSkillIDX, iObjIDX, pChar, pEffectOfSkill);
            break;
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 실제로 캐릭터에 스킬 적용..
//----------------------------------------------------------------------------------------------------

void
CObjCHAR::ApplyEffectOfSkill(int iSkillIDX,
    int iObjIDX,
    CObjCHAR* pEffectedChar,
    stEFFECT_OF_SKILL* pEffectOfSkill) {
    if (pEffectOfSkill->EffectOfSkill.m_btSuccessBITS
        == 0) /// 적용 효과후 바로 삭제..즉 스킬 적용 실패다
    {
        /// 내가 적용한 스킬일경우에만 스킬 적용 실패 메시지를 표시한다.
        int iClientObjIndex =
            g_pObjMGR->Get_ClientObjectIndex(pEffectOfSkill->EffectOfSkill.m_wSpellObjIDX);
        if (iClientObjIndex == g_pAVATAR->Get_INDEX()) {
            /// AddMsgToChatWND( STR_SKILL_APPLY_FAILED, g_dwRED ,CChatDLG::CHAT_TYPE_SYSTEM);
            ;
        }
    } else {
        ///스킬이 지속형일 경우 최대 2개의 상태까지 바뀔수 있으므로
        for (int i = 0; i < 2; i++) {
            if ((0x01 << i) & pEffectOfSkill->EffectOfSkill.m_btSuccessBITS) {
                int iStateIndex = 0;

                /// 지속형이 아닌 단순 능력치 상승형..
                /*if( SKILL_TYPE( iSkillIDX ) != SKILL_ACTION_SELF_BOUND &&
                    SKILL_TYPE( iSkillIDX ) != SKILL_ACTION_TARGET_BOUND )*/
                {
                    iStateIndex = SKILL_STATE_STB(iSkillIDX, i);

                    /// 유리상태 해지, 불리상태 해지 등의 상태 해제 스킬들
                    if (iStateIndex && STATE_TYPE(iStateIndex) > ING_CHECK_END) {
                        pEffectedChar->ProcFlushStateSkill(iStateIndex);
                        continue;
                    }
                }

                if (iStateIndex != 0)
                /// 지속형 스킬이라면..
                /*if( SKILL_TYPE( iSkillIDX ) == SKILL_ACTION_SELF_BOUND_DURATION ||
                    SKILL_TYPE( iSkillIDX ) == SKILL_ACTION_TARGET_BOUND_DURATION ||
                    SKILL_TYPE( iSkillIDX ) == SKILL_ACTION_SELF_STATE_DURATION ||
                    SKILL_TYPE( iSkillIDX ) == SKILL_ACTION_TARGET_STATE_DURATION )*/
                {
                    /// 일단 유져일경우만 속성객체 추가..
                    // if( pChar->IsA( OBJ_USER ) )
                    pEffectedChar->AddEnduranceEntity(iSkillIDX,
                        iStateIndex,
                        SKILL_DURATION(iSkillIDX),
                        ENDURANCE_TYPE_SKILL);

                    /// 상태 타입..
                    int iStateType = STATE_TYPE(iStateIndex);
                    /// 상태 번호가 1,2 번인경우에는 LIST_STATUS.STB 의 값을 참고하고
                    if (iStateType == ING_INC_HP || iStateType == ING_INC_MP
                        || iStateType == ING_POISONED)
                        pEffectedChar->m_EndurancePack.SetStateValue(iStateType,
                            STATE_APPLY_ABILITY_VALUE(iStateIndex, i));
                    else {
                        int iIncValue = 0;
                        /// 04/4/24
                        if (pEffectedChar->IsA(OBJ_USER)) {
                            iIncValue = CCal::Get_SkillAdjustVALUE(pEffectedChar,
                                iSkillIDX,
                                i,
                                pEffectOfSkill->iCasterINT);
                        } else {
                            iIncValue = 1;

                            /// 유져가 아닐경우 알수가 없다. 몬스터일경우에는 공속, 이속만
                            /// 계산해본다.
                            if (pEffectedChar->IsA(OBJ_MOB)) {
                                int iAbilityValue = 0;
                                switch (SKILL_INCREASE_ABILITY(iSkillIDX, i)) {
                                    case AT_SPEED:
                                        iAbilityValue = pEffectedChar->stats.move_speed;
                                        break;
                                    case AT_ATK_SPD:
                                        iAbilityValue = pEffectedChar->stats.attack_speed;
                                        break;
                                }

                                iIncValue = (short)(iAbilityValue
                                        * SKILL_CHANGE_ABILITY_RATE(iSkillIDX, i) / 100.f
                                    + SKILL_INCREASE_ABILITY_VALUE(iSkillIDX, i));
                            }

                            //--------------------------------------------------------------------------------------------
                            /// 다른 아바타일경우.. MAX_HP는 고려를 한다..
                            if (pEffectedChar->IsA(OBJ_AVATAR)) {
                                int iAbilityValue = 0;
                                switch (SKILL_INCREASE_ABILITY(iSkillIDX, i)) {
                                    case AT_MAX_HP:
                                        iAbilityValue = pEffectedChar->Get_MaxHP();
                                        break;
                                }

                                iIncValue = (short)(iAbilityValue
                                        * SKILL_CHANGE_ABILITY_RATE(iSkillIDX, i) / 100.f
                                    + SKILL_INCREASE_ABILITY_VALUE(iSkillIDX, i)
                                        * (pEffectOfSkill->iCasterINT + 300) / 315.f);
                            }
                            //--------------------------------------------------------------------------------------------
                        }

                        pEffectedChar->m_EndurancePack.SetStateValue(iStateType, iIncValue);

                        /// 상태스킬이 걸릴경우 AVATAR 의 경우는 능력치 업데이트..
                        if (pEffectedChar->IsA(OBJ_USER)) {
                            ((CObjUSER*)pEffectedChar)->UpdateAbility();
                        }
                    }

                } else if (SKILL_TYPE(iSkillIDX) == SKILL_ACTION_SELF_BOUND
                    || SKILL_TYPE(iSkillIDX) == SKILL_ACTION_TARGET_BOUND) {
                    /// hp 는 계산식 적용..
                    int iIncValue = CCal::Get_SkillAdjustVALUE(pEffectedChar,
                        iSkillIDX,
                        i,
                        pEffectOfSkill->iCasterINT);

                    switch (SKILL_INCREASE_ABILITY(iSkillIDX, i)) {
                        case AT_HP:
                            pEffectedChar->Add_HP(iIncValue);
                            break;
                        case AT_MP:
                            pEffectedChar->Add_MP(iIncValue);
                            break;
                        case AT_STAMINA:
                            /// 상태스킬이 걸릴경우 AVATAR 의 경우는 능력치 업데이트..
                            if (pEffectedChar->IsA(OBJ_USER)) {
                                ((CObjUSER*)pEffectedChar)
                                    ->AddCur_STAMINA(SKILL_INCREASE_ABILITY_VALUE(iSkillIDX, i));
                            }
                            break;
                        default:
                            g_itMGR.AppendChatMsg("몰르는거네 추가해라..",
                                IT_MGR::CHAT_TYPE_SYSTEM);
                            break;
                    }
                }
            }
        }
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param stEFFECT_OF_SKILL*	pEffectOfSkill 스킬 결과 하나 처리..
/// @brief  : 스킬 결과 처리
//--------------------------------------------------------------------------------

void
CObjCHAR::ProcOneEffectedSkill(stEFFECT_OF_SKILL* pEffectOfSkill) {
    int iSkillIDX = pEffectOfSkill->iSkillIDX;
    int iObjIDX = pEffectOfSkill->EffectOfSkill.m_wObjectIDX;

    CObjCHAR* pChar = g_pObjMGR->Get_ClientCharOBJ(iObjIDX, true);

    if (pChar == NULL)
        return;

    /*
     *	범위 마법의 경우 m_nResultVALUE 가 데미지 이다.
     */
    if (pEffectOfSkill->bDamageOfSkill) {
        D3DXVECTOR3 pos = pChar->Get_CurPOS();

        if (!pEffectOfSkill->bDamageEventAlreadyQueued) {
            pChar->ConvertDamageOfSkillToDamage(pEffectOfSkill->EffectOfSkill,
                pEffectOfSkill->arrival_seq);
            pChar->PresentImmediateCombatDamage(this);
        } else {
            LogString(LOG_DEBUG_,
                "CombatTrace projectile status/effect applied after queued damage: caster %d target %d skill %d damage %d success_bits %d\n",
                this->Get_INDEX(),
                iObjIDX,
                iSkillIDX,
                pEffectOfSkill->EffectOfSkill.m_wDamage,
                static_cast<int>(pEffectOfSkill->EffectOfSkill.m_btSuccessBITS));
        }

        /// Damage of skill 에 실려오는 스킬적용결과 반영..
        ProcEffectOfSkillInDamageOfSkill(iSkillIDX, iObjIDX, pChar, pEffectOfSkill);

        /// 스킬 타격 이펙트
        // 타격 효과.
        if (pChar->IsVisible()) {
            int iEffectIDX = SKILL_HIT_EFFECT(iSkillIDX);
            if (iEffectIDX) {
                CEffect* pHitEFT = g_pEffectLIST->Add_EffectWithIDX(iEffectIDX, true);
                if (pHitEFT) {
                    if (SKILL_HIT_EFFECT_LINKED_POINT(iSkillIDX) == INVALID_DUMMY_POINT_NUM) {
                        pHitEFT->LinkNODE(pChar->GetZMODEL());
                    } else {
                        pChar->LinkDummy(pHitEFT->GetZNODE(),
                            SKILL_HIT_EFFECT_LINKED_POINT(iSkillIDX));
                    }

                    pHitEFT->SetParentCHAR(pChar);
                    pChar->AddExternalEffect(pHitEFT);

                    pHitEFT->UnlinkVisibleWorld();
                    pHitEFT->InsertToScene();
                }

                if (SKILL_HIT_SOUND(iSkillIDX)) {
                    if (pChar->IsUSER()) { // 자기 아바타인 경우에는 안3D 모드로 출력.
                        g_pSoundLIST->IDX_PlaySound(SKILL_HIT_SOUND(iSkillIDX));
                    } else {
                        g_pSoundLIST->IDX_PlaySound3D(SKILL_HIT_SOUND(iSkillIDX),
                            pChar->Get_CurPOS());
                    }
                }
            }
        }

    } else /// 지속성이거나.. 상태를 바꾸는 스킬..
    {
        ApplyEffectOfSkill(iSkillIDX, iObjIDX, pChar, pEffectOfSkill);
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 보관된 모든 스킬결과들을 이벤트 프레임에 처리한다.
//--------------------------------------------------------------------------------

bool
CObjCHAR::ProcEffectedSkill(bool bProjectileImpact,
    int iProjectileSkillIDX,
    int iProjectileTargetObjIDX) {
    bool bResult = false;

    std::vector<stEFFECT_OF_SKILL>::iterator begin = m_EffectedSkillList.begin();
    for (; begin != m_EffectedSkillList.end();) {
        stEFFECT_OF_SKILL* pEffectOfSkill = &(*begin);

        if (bProjectileImpact) {
            if (!pEffectOfSkill->bWaitForProjectileImpact) {
                ++begin;
                continue;
            }
            if (iProjectileSkillIDX && pEffectOfSkill->iSkillIDX != iProjectileSkillIDX) {
                ++begin;
                continue;
            }
            if (iProjectileTargetObjIDX
                && pEffectOfSkill->EffectOfSkill.m_wObjectIDX != iProjectileTargetObjIDX) {
                ++begin;
                continue;
            }
        } else {
            if (pEffectOfSkill->bWaitForProjectileImpact) {
                ++begin;
                continue;
            }
        }

        ProcOneEffectedSkill(pEffectOfSkill);
        if (bProjectileImpact && pEffectOfSkill->bWaitForProjectileImpact) {
            ClearPendingProjectileSkill(pEffectOfSkill->EffectOfSkill.m_wObjectIDX,
                pEffectOfSkill->iSkillIDX);
            LogString(LOG_DEBUG_,
                "CombatTrace projectile impact consumed skill payload: caster %d target %d skill %d damage_queued %d\n",
                this->Get_INDEX(),
                pEffectOfSkill->EffectOfSkill.m_wObjectIDX,
                pEffectOfSkill->iSkillIDX,
                pEffectOfSkill->bDamageEventAlreadyQueued ? 1 : 0);
        }
        bResult = true;
        begin = m_EffectedSkillList.erase(begin);
    }

    if (m_EffectedSkillList.empty()) {
        SetEffectedSkillFlag(false);
    }
    return bResult;
}

// Process-wide arrival-order counter for HP-authoritative packets. The client is
// single-threaded for packet processing, so a strictly increasing value assigned in
// processing order reflects causal (TCP) arrival order across all defenders.
static uint32_t s_dwHPAuthoritySeq = 0;

uint32_t
CObjCHAR::NextHPAuthoritySeq() {
    return ++s_dwHPAuthoritySeq;
}

void
CObjCHAR::PushCombatDamageEvent(const Rose::Combat::DamageEvent& event) {
    Rose::Combat::DamageEvent queuedEvent = event;
    queuedEvent.queued_at_ms = g_GameDATA.GetGameTime();
    // Immediate paths (FlatBuffer DamageEvent/CombatSwing, legacy melee) queue at
    // receive time, so stamp the arrival order here. Deferred skill hits set
    // arrival_seq at receive (carried through ConvertDamageOfSkillToDamage) and are
    // left untouched.
    if (queuedEvent.arrival_seq == 0) {
        queuedEvent.arrival_seq = NextHPAuthoritySeq();
    }
    m_CombatDamageQueue.push(queuedEvent);

    // Damage-meter tap: observation only. The meter copies what it needs and
    // never mutates the event, the queue, or any presentation state.
    CDamageMeter::GetInstance().OnCombatDamageEvent(queuedEvent, this);

    // The server has already committed a lethal hit on the local avatar. If the
    // presentation animation is destroyed before its hit frame (dropped swing
    // command, lost projectile, despawned attacker), no future event will fold
    // this death in -- arm the Proc() pending-death backstop now. Normal hit
    // frame presentation clears the flag well before the timeout fires.
    if (queuedEvent.lethal || queuedEvent.hp_after <= DEAD_HP) {
        if (this == g_pAVATAR) {
            MarkPendingAuthoritativeDeath("lethal event queued");
        } else {
            m_bDead = true;
        }
    }
}

Rose::Combat::PresentationResult
CObjCHAR::PopCombatDamageEvent(int iAttacker, Rose::Combat::DamageEvent& event) {
    if (!m_CombatDamageQueue.pop_for_attacker(iAttacker, event)) {
        LogString(LOG_DEBUG_,
            "CombatTrace queued presentation miss: attacker %d target %d queue %d\n",
            iAttacker,
            this->Get_INDEX(),
            static_cast<int>(m_CombatDamageQueue.size()));
        return Rose::Combat::PresentationResult::NoEvent;
    }
    LogString(LOG_DEBUG_,
        "CombatTrace queued presentation pop: attacker %d target %d event %u seq %u kind %d damage %d hp_after %d\n",
        iAttacker,
        this->Get_INDEX(),
        event.event_id,
        event.defender_seq,
        static_cast<int>(event.presentation_kind),
        event.damage_value,
        event.hp_after);
    return Rose::Combat::CombatPresentationQueue::result_for(event);
}

void
CObjCHAR::SetAuthoritativeHP(int hp) {
    m_iAuthoritativeHP = hp;
    m_bHasAuthoritativeHP = true;
}

void
CObjCHAR::SetAuthoritativeHPFromDamageEvent(const Rose::Combat::DamageEvent& event,
    bool bAllowRaise) {
    // Damage checkpoints normally only ever *lower* the shadow HP. hp_after is an
    // absolute snapshot from the instant the server applied the hit, and a queued
    // event's presentation is deferred to an animation/impact frame, so a stale
    // checkpoint that was allowed to raise HP would undo real damage -- and the
    // out-of-order drain-on-death path relies on lower-only to stay safe.
    //
    // The caller opts out of that rule only for a checkpoint it knows is fresh
    // (presented synchronously at receive, no deferral window). Without an upward
    // path the shadow HP cannot follow server-side healing that carries no sync of
    // its own -- potions, most visibly -- and every subsequent tick folds the
    // visible bar back down to the pre-heal value.
    const bool bRaises = m_bHasAuthoritativeHP && event.hp_after > m_iAuthoritativeHP;
    if (bRaises && !bAllowRaise) {
        return;
    }

    SetAuthoritativeHP(event.hp_after);
    m_dwLastAuthoritativeDamageSeq = event.defender_seq;

    if (!bRaises) {
        return;
    }

    // A checkpoint that actually *raised* HP proves the server healed us after
    // everything that arrived before it, so it supersedes those older checkpoints
    // exactly the way Reconcile_HP does -- otherwise an already-queued hit still
    // waiting on its animation frame passes the staleness guard untouched and drags
    // the bar back down to its own pre-heal hp_after. Stamp the event's own arrival
    // order rather than a fresh counter so supersession stays exact regardless of
    // when the raise is presented, and never move the stamp backwards.
    if (event.arrival_seq > m_dwLastAuthoritativeSyncSeq) {
        m_dwLastAuthoritativeSyncSeq = event.arrival_seq;
    }
}

void
CObjCHAR::SetVisibleHPFromPresentation(int hp) {
    this->Set_HP(hp);
}

void
CObjCHAR::MarkPendingAuthoritativeDeath(const char* reason) {
    if (this->Get_HP() <= DEAD_HP) {
        m_bPendingAuthoritativeDeath = false;
        m_dwPendingAuthoritativeDeathTime = 0;
        return;
    }

    // Stamp only on the rising edge. The server can re-assert HP=0 every frame
    // (repeated SET_HPnMP while dead); resetting the clock on each call would keep
    // pushing the backstop timeout out forever and it would never fire.
    if (!m_bPendingAuthoritativeDeath) {
        m_dwPendingAuthoritativeDeathTime = g_GameDATA.GetGameTime();
    }
    m_bPendingAuthoritativeDeath = true;
    m_iPendingCombatHPCorrection = 0;
    LogString(LOG_DEBUG_,
        "Combat HP death correction deferred (%s): target %d visible hp %d authoritative hp %d queue %d last event %u seq %u\n",
        reason ? reason : "unknown",
        this->Get_INDEX(),
        this->Get_HP(),
        m_iAuthoritativeHP,
        static_cast<int>(m_CombatDamageQueue.size()),
        m_dwLastPresentedDamageEventId,
        m_dwLastPresentedDamageSeq);
}

void
CObjCHAR::ClearPendingAuthoritativeDeath() {
    m_bPendingAuthoritativeDeath = false;
    m_dwPendingAuthoritativeDeathTime = 0;
}

void
CObjCHAR::PresentPendingAuthoritativeDeath(CObjCHAR* pAtkOBJ, const char* reason) {
    if (this->Get_HP() <= DEAD_HP) {
        ClearPendingAuthoritativeDeath();
        return;
    }

    if (!m_bPendingAuthoritativeDeath && (!m_bHasAuthoritativeHP || m_iAuthoritativeHP > DEAD_HP)) {
        return;
    }

    uniDAMAGE rawDamage;
    rawDamage.m_wDamage = 0;
    rawDamage.m_wACTION |= DMG_ACT_DEAD;

    LogString(LOG_DEBUG_,
        "Combat HP death correction presented (%s): target %d attacker %d visible hp %d authoritative hp %d\n",
        reason ? reason : "unknown",
        this->Get_INDEX(),
        pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0,
        this->Get_HP(),
        m_iAuthoritativeHP);

    ClearPendingAuthoritativeDeath();
    m_iPendingCombatHPCorrection = 0;
    ApplyPresentedCombatFeedback(pAtkOBJ, rawDamage.m_wDamage, 0, true);
}

bool
CObjCHAR::ShouldSuppressOutgoingDamageForPendingDeath(CObjCHAR* pFromOBJ) const {
    if (!g_pAVATAR || !g_pAVATAR->HasPendingAuthoritativeDeath() || !pFromOBJ) {
        return false;
    }

    if (pFromOBJ == g_pAVATAR) {
        return true;
    }

    if (pFromOBJ->IsPET()) {
        CObjCART* pCart = (CObjCART*)pFromOBJ;
        return pCart->GetParent() == g_pAVATAR;
    }

    return false;
}

void
CObjCHAR::ApplyPresentedCombatFeedback(CObjCHAR* pAtkOBJ,
    uint32_t rawDamage,
    int damageValue,
    bool lethal) {
    if (this->Get_HP() <= DEAD_HP) {
        return;
    }

    if (IsA(OBJ_USER)) {
        g_pAVATAR->SetBattleTime(g_GameDATA.GetGameTime());
    }

    if (pAtkOBJ && pAtkOBJ->IsA(OBJ_USER)) {
        g_pAVATAR->SetBattleTime(g_GameDATA.GetGameTime());
    }

    uniDAMAGE Damage;
    Damage.m_wDamage = rawDamage;
    Damage.m_wVALUE = max(0, damageValue);

    if (lethal) {
        Damage.m_wACTION |= DMG_ACT_DEAD;
        this->Do_DeadAI(pAtkOBJ, Damage.m_wVALUE);

        m_bStopDead = (pAtkOBJ == this);
        this->Dead();
        if (m_bStopDead && (this->IsA(OBJ_USER) == false)) {
            m_pChangeVisibility = new CChangeVisibility(this, 5000, true);
            m_bStopDead = false;
        }

        if (pAtkOBJ) {
            if (g_pAVATAR && g_pAVATAR->Get_INDEX() == pAtkOBJ->Get_INDEX()) {
                switch (this->Get_TYPE()) {
                    case OBJ_MOB:
                        sprintf(g_MsgBuf, F_STR_SUCCESS_HUNT, this->Get_NAME());
                        break;

                    case OBJ_AVATAR:
                        sprintf(g_MsgBuf, F_STR_WIN_PVP, this->Get_NAME());
                        break;
                }

                g_itMGR.AppendChatMsg(g_MsgBuf, IT_MGR::CHAT_TYPE_SYSTEM);
            }

            if (pAtkOBJ->m_iServerTarget
                == g_pObjMGR->Get_ServerObjectIndex(this->Get_INDEX())) {
                pAtkOBJ->m_iServerTarget = 0;
            }

            pAtkOBJ->Do_KillAI(this, Damage.m_wVALUE);

#if defined(_GBC)
            if (GetPetMode() < 0) {
                Set_ModelDIR(pAtkOBJ->Get_CurPOS());
            }
#else
            Set_ModelDIR(pAtkOBJ->Get_CurPOS());
#endif

            if (this->IsA(OBJ_USER) == true && pAtkOBJ->IsA(OBJ_MOB)) {
                if (!(pAtkOBJ->m_EndurancePack.GetStateFlag() & ING_DEC_LIFE_TIME)) {
                    g_pAVATAR->Set_PenalEXP(3);
                }
            }
        }

        CDelayedExp::GetSingleton().GetExp(this->Get_INDEX());

        if (!m_FieldItemList.empty()) {
            DropFieldItemFromList();
        }

        return;
    }

    this->Do_DamagedAI(pAtkOBJ, Damage.m_wVALUE);

    if (pAtkOBJ && (Damage.m_wDamage & DMG_BIT_HITTED)) {
        if (this->GetPetMode() <= 0 && !(this->Get_STATE() & CS_BIT_INT2)) {
            this->Set_MOTION(this->GetANI_Hit());
            this->Set_STATE(CS_HIT);
        }
    }
}

void
CObjCHAR::ApplyPresentedCombatDamage(CObjCHAR* pAtkOBJ, Rose::Combat::DamageEvent& event) {
    // Heal-in-flight guard. This event's hp_after was the authoritative HP at the
    // instant the server applied the hit, but presentation is deferred to the
    // animation/impact frame. If a later authoritative sync (heal, potion, regen)
    // arrived in that window AND raised HP above the checkpoint, the checkpoint is
    // stale: dropping the bar to it would overshoot down and snap back up on the
    // next sync. Detect via arrival order (TCP-ordered, single-threaded receive) and
    // honor the fresher authoritative HP instead. Only fires when the fresher value
    // is higher than the checkpoint, so normal "no sync yet" hits are untouched and
    // later-and-lower syncs keep flowing through the existing checkpoint fold.
    const bool bStaleHealedCheckpoint =
        m_bHasAuthoritativeHP
        && event.arrival_seq != 0
        && m_dwLastAuthoritativeSyncSeq > event.arrival_seq
        && m_iAuthoritativeHP > event.hp_after;

    // StatusTick is the one checkpoint that may raise the shadow HP. It is produced
    // only by Give_STATUS_DAMAGE and presented synchronously in recv_damage_event,
    // so it never waits on an animation frame and is fresh by construction. Letting
    // it raise pins the bar to server truth in both directions during a DoT, which
    // is what keeps potion healing visible while poisoned: the server applies potion
    // HP between syncs, and poison also suppresses the natural-regen sync that used
    // to be the only thing carrying that healing to the client.
    const bool bFreshCheckpoint =
        event.presentation_kind == Rose::Combat::DamagePresentationKind::StatusTick;

    if (!bStaleHealedCheckpoint) {
        SetAuthoritativeHPFromDamageEvent(event, bFreshCheckpoint);
    } else {
        LogString(LOG_DEBUG_,
            "Combat HP heal-in-flight guard: target %d event %u seq %u arrival %u last sync %u stale hp_after %d fresher authoritative hp %d visible hp %d\n",
            this->Get_INDEX(),
            event.event_id,
            event.defender_seq,
            event.arrival_seq,
            m_dwLastAuthoritativeSyncSeq,
            event.hp_after,
            m_iAuthoritativeHP,
            this->Get_HP());
    }
    m_dwLastPresentedDamageEventId = event.event_id;
    m_dwLastPresentedDamageSeq = event.defender_seq;

    const int visibleBefore = this->Get_HP();
    const int displayDamage = max(0, static_cast<int>(event.damage_value));
    bool lethal = event.lethal || event.hp_after <= DEAD_HP
        || ((m_bPendingAuthoritativeDeath || (m_bHasAuthoritativeHP && m_iAuthoritativeHP <= DEAD_HP))
            && displayDamage > 0);

    if (displayDamage <= 0 && !lethal) {
        return;
    }

    int hpDelta = displayDamage;
    int hpAfterDelta = visibleBefore;
    if (lethal) {
        hpDelta = max(hpDelta, visibleBefore - DEAD_HP);
        m_iPendingCombatHPCorrection = 0;
    } else if (hpDelta > 0) {
        hpAfterDelta = max(DEAD_HP + 1, visibleBefore - hpDelta);
    }

    if (!m_CombatDamageQueue.has_pending_damage()) {
        // For a stale-healed checkpoint the freshest truth is the later sync's HP,
        // not the event's outdated hp_after. The overshoot-clamp below then raises
        // the visible bar to it (digit still shows the full hit).
        int authoritativeTargetHP = bStaleHealedCheckpoint ? m_iAuthoritativeHP : event.hp_after;
        if (m_bHasAuthoritativeHP) {
            authoritativeTargetHP = min(authoritativeTargetHP, m_iAuthoritativeHP);
        }

        if (authoritativeTargetHP <= DEAD_HP) {
            if (!lethal) {
                LogString(LOG_DEBUG_,
                    "Combat HP correction folded lethal checkpoint: visible hp %d post delta hp %d authoritative hp %d event %u seq %u\n",
                    visibleBefore,
                    hpAfterDelta,
                    authoritativeTargetHP,
                    event.event_id,
                    event.defender_seq);
            }
            lethal = true;
            hpDelta = max(hpDelta, visibleBefore - DEAD_HP);
            m_iPendingCombatHPCorrection = 0;
        } else if (!lethal && hpDelta > 0) {
            if (hpAfterDelta > authoritativeTargetHP) {
                const int foldedCorrection = hpAfterDelta - authoritativeTargetHP;
                hpDelta += foldedCorrection;
                hpAfterDelta = authoritativeTargetHP;
                m_iPendingCombatHPCorrection = 0;
                LogString(LOG_DEBUG_,
                    "Combat HP correction folded checkpoint: visible hp %d post delta hp %d authoritative hp %d correction %d event %u seq %u\n",
                    visibleBefore,
                    hpAfterDelta,
                    authoritativeTargetHP,
                    foldedCorrection,
                    event.event_id,
                    event.defender_seq);
            } else if (hpAfterDelta < authoritativeTargetHP) {
                const int overshoot = authoritativeTargetHP - hpAfterDelta;
                hpAfterDelta = authoritativeTargetHP;
                m_iPendingCombatHPCorrection = 0;
                LogString(LOG_DEBUG_,
                    "Combat HP correction clamped overshoot: visible hp %d post delta hp %d authoritative hp %d overshoot %d event %u seq %u\n",
                    visibleBefore,
                    hpAfterDelta,
                    authoritativeTargetHP,
                    overshoot,
                    event.event_id,
                    event.defender_seq);
            } else {
                m_iPendingCombatHPCorrection = 0;
            }
        }
    }

    // Heal-in-flight visible floor (independent of queue state). The overshoot-clamp
    // above only raises the bar to the fresher authoritative HP when the queue is
    // otherwise empty. With another hit still in flight (multi-hit skill, or a melee
    // swing queued alongside the skill) that block is skipped, so a stale-healed
    // checkpoint would still dip the bar by this digit and snap back up on the next
    // reconcile -- the exact artifact the guard exists to prevent. The guard already
    // proved a later sync superseded this checkpoint and raised HP above it, so hold
    // the visible bar at the fresher authoritative HP now. Bounded by visibleBefore so
    // a damage presentation never visibly heals; digit is unchanged; lethal already
    // forced death above.
    if (bStaleHealedCheckpoint && !lethal && m_bHasAuthoritativeHP) {
        hpAfterDelta = max(hpAfterDelta, min(visibleBefore, m_iAuthoritativeHP));
    }

    uniDAMAGE displayedDamage;
    displayedDamage.m_wDamage = event.raw_damage;
    displayedDamage.m_wVALUE = displayDamage;
    if (lethal) {
        displayedDamage.m_wACTION |= DMG_ACT_DEAD;
    }
    event.raw_damage = displayedDamage.m_wDamage;
    event.lethal = lethal;

    this->ApplyPresentedCombatFeedback(pAtkOBJ, event.raw_damage, displayDamage, lethal);

    if (!lethal) {
        this->SetVisibleHPFromPresentation(hpAfterDelta);
    } else {
        ClearPendingAuthoritativeDeath();
    }

    DeferCombatHPDriftIfIdle("damage presentation");
}

Rose::Combat::PresentationResult
CObjCHAR::PresentImmediateCombatDamage(CObjCHAR* pAtkOBJ) {
    Rose::Combat::DamageEvent event;
    if (!m_CombatDamageQueue.pop_immediate(event)) {
        LogString(LOG_DEBUG_,
            "CombatTrace immediate presentation skipped: attacker %d target %d queue %d\n",
            pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0,
            this->Get_INDEX(),
            static_cast<int>(m_CombatDamageQueue.size()));
        return Rose::Combat::PresentationResult::NoEvent;
    }

    if (event.presentation_kind == Rose::Combat::DamagePresentationKind::ProjectileImpact) {
        PushCombatDamageEvent(event);
        LogString(LOG_DEBUG_,
            "CombatTrace immediate presentation blocked projectile event: attacker %d target %d event %u damage %d\n",
            pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0,
            this->Get_INDEX(),
            event.event_id,
            event.damage_value);
        return Rose::Combat::PresentationResult::NoEvent;
    }

    LogString(LOG_DEBUG_,
        "CombatTrace immediate presentation pop: attacker %d target %d event %u seq %u kind %d damage %d hp_after %d\n",
        pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0,
        this->Get_INDEX(),
        event.event_id,
        event.defender_seq,
        static_cast<int>(event.presentation_kind),
        event.damage_value,
        event.hp_after);
    ApplyPresentedCombatDamage(pAtkOBJ, event);
    CreateImmediateDigitEffect(event.raw_damage);
    return Rose::Combat::CombatPresentationQueue::result_for(event);
}

Rose::Combat::PresentationResult
CObjCHAR::PresentQueuedCombatDamageFromAttacker(CObjCHAR* pAtkOBJ) {
    Rose::Combat::DamageEvent event;
    const int iAttacker = pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0;
    if (!m_CombatDamageQueue.pop_for_attacker(iAttacker, event)) {
        LogString(LOG_DEBUG_,
            "CombatTrace confirmed presentation miss: attacker %d target %d queue %d\n",
            iAttacker,
            this->Get_INDEX(),
            static_cast<int>(m_CombatDamageQueue.size()));
        return Rose::Combat::PresentationResult::NoEvent;
    }

    LogString(LOG_DEBUG_,
        "CombatTrace confirmed presentation pop: attacker %d target %d event %u seq %u kind %d damage %d hp_after %d\n",
        iAttacker,
        this->Get_INDEX(),
        event.event_id,
        event.defender_seq,
        static_cast<int>(event.presentation_kind),
        event.damage_value,
        event.hp_after);
    if (pAtkOBJ) {
        pAtkOBJ->ClearPendingCombatSwingPresentation(event.event_id);
    }
    ApplyPresentedCombatDamage(pAtkOBJ, event);
    CreateImmediateDigitEffect(event.raw_damage);
    return Rose::Combat::CombatPresentationQueue::result_for(event);
}

Rose::Combat::PresentationResult
CObjCHAR::DiscardQueuedCombatDamageFromAttacker(CObjCHAR* pAtkOBJ) {
    Rose::Combat::DamageEvent event;
    const int iAttacker = pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0;
    if (!m_CombatDamageQueue.discard_for_attacker(iAttacker, &event)) {
        return Rose::Combat::PresentationResult::NoEvent;
    }

    if (pAtkOBJ) {
        pAtkOBJ->ClearPendingCombatSwingPresentation(event.event_id);
    }

    if (this != g_pAVATAR
        && this->Get_HP() > DEAD_HP
        && (event.lethal || event.hp_after <= DEAD_HP)) {
        LogString(LOG_DEBUG_,
            "CombatTrace lethal queued projectile damage presented on discard: attacker %d target %d kind %d damage %d hp_after %d event %u seq %u\n",
            iAttacker,
            this->Get_INDEX(),
            static_cast<int>(event.presentation_kind),
            event.damage_value,
            event.hp_after,
            event.event_id,
            event.defender_seq);
        ApplyPresentedCombatDamage(pAtkOBJ, event);
        CreateImmediateDigitEffect(event.raw_damage);
        return Rose::Combat::CombatPresentationQueue::result_for(event);
    }

    SetAuthoritativeHPFromDamageEvent(event);
    DeferCombatHPDriftIfIdle("projectile damage discarded");
    LogString(LOG_DEBUG_,
        "CombatTrace queued projectile damage discarded without impact: attacker %d target %d kind %d damage %d hp_after %d event %u seq %u\n",
        iAttacker,
        this->Get_INDEX(),
        static_cast<int>(event.presentation_kind),
        event.damage_value,
        event.hp_after,
        event.event_id,
        event.defender_seq);
    return Rose::Combat::CombatPresentationQueue::result_for(event);
}

Rose::Combat::PresentationResult
CObjCHAR::DiscardQueuedCombatDamageEvent(uint32_t eventId, CObjCHAR* pAtkOBJ, const char* reason) {
    Rose::Combat::DamageEvent event;
    if (!m_CombatDamageQueue.discard_event(eventId, &event)) {
        LogString(LOG_DEBUG_,
            "CombatTrace queued damage discard missed event: attacker %d target %d event %u reason %s queue %d\n",
            pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0,
            this->Get_INDEX(),
            eventId,
            reason ? reason : "",
            static_cast<int>(m_CombatDamageQueue.size()));
        return Rose::Combat::PresentationResult::NoEvent;
    }

    if (pAtkOBJ) {
        pAtkOBJ->ClearPendingCombatSwingPresentation(event.event_id);
    }

    if (this == g_pAVATAR && (event.lethal || event.hp_after <= DEAD_HP)) {
        SetAuthoritativeHPFromDamageEvent(event);
        MarkPendingAuthoritativeDeath(reason ? reason : "queued lethal damage discarded");
        PresentPendingAuthoritativeDeath(pAtkOBJ,
            reason ? reason : "queued lethal damage discarded");
        LogString(LOG_DEBUG_,
            "CombatTrace lethal avatar queued damage presented on exact discard: attacker %d target %d kind %d damage %d hp_after %d event %u seq %u reason %s\n",
            pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0,
            this->Get_INDEX(),
            static_cast<int>(event.presentation_kind),
            event.damage_value,
            event.hp_after,
            event.event_id,
            event.defender_seq,
            reason ? reason : "");
        return Rose::Combat::CombatPresentationQueue::result_for(event);
    }

    if (this != g_pAVATAR
        && this->Get_HP() > DEAD_HP
        && (event.lethal || event.hp_after <= DEAD_HP)) {
        LogString(LOG_DEBUG_,
            "CombatTrace lethal queued damage presented on exact discard: attacker %d target %d kind %d damage %d hp_after %d event %u seq %u reason %s\n",
            pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0,
            this->Get_INDEX(),
            static_cast<int>(event.presentation_kind),
            event.damage_value,
            event.hp_after,
            event.event_id,
            event.defender_seq,
            reason ? reason : "");
        ApplyPresentedCombatDamage(pAtkOBJ, event);
        CreateImmediateDigitEffect(event.raw_damage);
        return Rose::Combat::CombatPresentationQueue::result_for(event);
    }

    SetAuthoritativeHPFromDamageEvent(event);
    DeferCombatHPDriftIfIdle(reason ? reason : "queued damage discarded");
    LogString(LOG_DEBUG_,
        "CombatTrace queued damage discarded without presentation: attacker %d target %d kind %d damage %d hp_after %d event %u seq %u reason %s\n",
        pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0,
        this->Get_INDEX(),
        static_cast<int>(event.presentation_kind),
        event.damage_value,
        event.hp_after,
        event.event_id,
        event.defender_seq,
        reason ? reason : "");
    return Rose::Combat::CombatPresentationQueue::result_for(event);
}

void
CObjCHAR::DrainQueuedCombatDamageFromAttacker(CObjCHAR* pAtkOBJ) {
    const int iAttacker = pAtkOBJ ? pAtkOBJ->Get_INDEX() : 0;
    Rose::Combat::DamageEvent event;
    int iDrained = 0;
    bool bLethalOrphan = false;

    // An attacker that dies mid-swing never reaches its hit frame, so the server
    // damage it already queued on us would orphan in the queue forever. A lingering
    // entry keeps has_pending_damage() true, which gates off BOTH reconciliation
    // paths (DeferCombatHPDriftIfIdle and the checkpoint fold in
    // ApplyPresentedCombatDamage). Remove the orphan(s) so the next presented hit
    // can fold the lost HP back in.
    while (m_CombatDamageQueue.discard_for_attacker(iAttacker, &event)) {
        ++iDrained;
        SetAuthoritativeHPFromDamageEvent(event);
        if (event.lethal || event.hp_after <= DEAD_HP) {
            bLethalOrphan = true;
        }
        LogString(LOG_DEBUG_,
            "CombatTrace queued damage drained on attacker death: attacker %d target %d kind %d damage %d hp_after %d event %u seq %u\n",
            iAttacker,
            this->Get_INDEX(),
            static_cast<int>(event.presentation_kind),
            event.damage_value,
            event.hp_after,
            event.event_id,
            event.defender_seq);
    }

    if (iDrained == 0) {
        return;
    }

    // Mutual death: the dead attacker's orphaned hit was lethal to the local
    // avatar. The attacker is being interrupted into its death motion this very
    // frame and will never reach a hit frame, so no future incoming hit can fold
    // our death in. Present the mutual death now (we already hold the authoritative
    // lethal event and the killer object) instead of stranding the avatar
    // pending-dead / frozen. If this is somehow not reached, the Proc() backstop
    // timeout still catches it.
    if (bLethalOrphan && this == g_pAVATAR) {
        MarkPendingAuthoritativeDeath("attacker died mid-swing");
        PresentPendingAuthoritativeDeath(pAtkOBJ, "attacker died mid-swing");
    } else {
        DeferCombatHPDriftIfIdle("attacker died mid-swing");
    }
}

void
CObjCHAR::DeferCombatHPDriftIfIdle(const char* reason) {
    if (!m_bHasAuthoritativeHP || m_CombatDamageQueue.has_pending_damage()) {
        return;
    }

    const int visibleHP = this->Get_HP();
    if (visibleHP <= m_iAuthoritativeHP) {
        return;
    }

    const int correctionValue = visibleHP - m_iAuthoritativeHP;
    if (correctionValue <= 0) {
        return;
    }

    if (m_iPendingCombatHPCorrection == correctionValue) {
        return;
    }

    m_iPendingCombatHPCorrection = correctionValue;

    LogString(LOG_DEBUG_,
        "Combat HP correction deferred (%s): visible hp %d authoritative hp %d pending correction %d queue %d last event %u seq %u\n",
        reason ? reason : "unknown",
        visibleHP,
        m_iAuthoritativeHP,
        m_iPendingCombatHPCorrection,
        static_cast<int>(m_CombatDamageQueue.size()),
        m_dwLastPresentedDamageEventId,
        m_dwLastPresentedDamageSeq);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 모든 데이지 처리( 죽거나.. 뭐 그런 상황에서.. 정리 )
//--------------------------------------------------------------------------------

void
CObjCHAR::ClearAllDamage() {
    m_CombatDamageQueue.clear();
    m_iPendingCombatHPCorrection = 0;
    ClearPendingAuthoritativeDeath();
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  WORD wDamage 적용 데미지
/// @brief  : 타격치 바로 표시
//--------------------------------------------------------------------------------

void
CObjCHAR::CreateImmediateDigitEffect(int wDamage) {
    /// 타격수치 바로적용.. 적당한 장소가 없다 일단 여기에..
    uniDAMAGE Damage;
    Damage.m_wDamage = wDamage;
    D3DXVECTOR3 pos = this->Get_CurPOS();
    g_UIMed.CreateDamageDigit(Damage.m_wVALUE,
        pos.x,
        pos.y,
        pos.z + this->m_fStature,
        this->IsA(OBJ_USER));
}

void
CObjCHAR::SetPendingMountedAttackTarget(int iServerTarget, DWORD dwTime) {
    m_iPendingMountedAttackTarget = iServerTarget;
    m_dwPendingMountedAttackTime = dwTime;
}

void
CObjCHAR::ClearPendingMountedAttackTarget() {
    m_iPendingMountedAttackTarget = 0;
    m_dwPendingMountedAttackTime = 0;
}

bool
CObjCHAR::HasPendingMountedAttackTarget(int iServerTarget, DWORD dwNow, DWORD dwWindow) const {
    if (m_iPendingMountedAttackTarget != iServerTarget || m_dwPendingMountedAttackTime == 0) {
        return false;
    }

    return (dwNow - m_dwPendingMountedAttackTime) < dwWindow;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  gsv_DAMAGE_OF_SKILL stDamageOfSkill damage of kill 정보
/// @brief  : Damage_of_Skill => 일반 데미지로 전환저장..
//--------------------------------------------------------------------------------

void
CObjCHAR::ConvertDamageOfSkillToDamage(gsv_DAMAGE_OF_SKILL stDamageOfSkill, uint32_t arrivalSeq) {
    uniDAMAGE Damage;
    Damage.m_wDamage = stDamageOfSkill.m_wDamage;

    CObjCHAR* pAtkOBJ;

    pAtkOBJ = g_pObjMGR->Get_ClientCharOBJ(stDamageOfSkill.m_wSpellObjIDX, false);

    if (pAtkOBJ) {
        static uint32_t s_LegacySkillDamageEventId = 1;

        Rose::Combat::DamageEvent event;
        event.event_id = s_LegacySkillDamageEventId++;
        event.defender_seq = event.event_id;
        event.attacker_id = g_pObjMGR->Get_ClientObjectIndex(stDamageOfSkill.m_wSpellObjIDX);
        event.defender_id = this->Get_INDEX();
        event.skill_id = stDamageOfSkill.m_nSkillIDX;
        event.raw_damage = stDamageOfSkill.m_wDamage;
        event.damage_value = Damage.m_wVALUE;
        event.hp_after = (Damage.m_wACTION & DMG_ACT_DEAD) ? DEAD_HP : stDamageOfSkill.m_iHP_AFTER;
        // arrivalSeq != 0 for deferred hits (captured at receive); 0 lets
        // PushCombatDamageEvent stamp it now for immediate skill paths.
        event.arrival_seq = arrivalSeq;
        event.presentation_kind =
            IsProjectilePresentedSkillDamage(stDamageOfSkill.m_nSkillIDX)
            ? Rose::Combat::DamagePresentationKind::ProjectileImpact
            : Rose::Combat::DamagePresentationKind::Immediate;
        event.lethal = (Damage.m_wACTION & DMG_ACT_DEAD) != 0;
        PushCombatDamageEvent(event);
        LogString(LOG_DEBUG_,
            "CombatTrace skill damage queued: caster %d target %d skill %d type %d bullet %d kind %d damage %d hp_after %d event %u\n",
            static_cast<int>(stDamageOfSkill.m_wSpellObjIDX),
            static_cast<int>(stDamageOfSkill.m_wObjectIDX),
            static_cast<int>(stDamageOfSkill.m_nSkillIDX),
            SKILL_TYPE(stDamageOfSkill.m_nSkillIDX),
            SKILL_BULLET_NO(stDamageOfSkill.m_nSkillIDX),
            static_cast<int>(event.presentation_kind),
            event.damage_value,
            event.hp_after,
            event.event_id);
        return;
    }
}

bool
CObjCHAR::IsProjectilePresentedSkillDamage(int iSkillIDX) {
    // Target-bound (instant + duration) and target-state-duration skills —
    // e.g. Fire Ring (TYPE_09 defense-down), single-target buffs/debuffs —
    // are drained at the caster's action frame in cobjchar_actionframe.cpp
    // (case 25: SKILL_ACTION_TARGET_BOUND* → ProcEffectedSkill() without
    // bProjectileImpact). BULLET_NO on those rows refers to the effect
    // graphic ID, not a tracked CBulletDIRECTION — no bullet impact event
    // is ever produced, so an entry queued with bWaitForProjectileImpact=true
    // is skipped at action frame and orphaned forever, and the debuff /
    // buff never lands client-side.
    //
    // Restrict projectile-impact gating to skills that genuinely launch a
    // tracked projectile: FIRE_BULLET / ENFORCE_BULLET, SELF_AND_TARGET
    // (rare cross-applies that do fire), and the IMMEDIATE / Twin-Shot edge
    // case where the melee fire animation has no action frame 25 hit moment.
    const int iSkillType = SKILL_TYPE(iSkillIDX);
    return Rose::Combat::is_projectile_presented_skill(iSkillType, SKILL_BULLET_NO(iSkillIDX));
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 아이템 드랍..
//--------------------------------------------------------------------------------

void
CObjCHAR::DropFieldItemFromList() {
    for (int i = 0; i < m_FieldItemList.size(); i++) {
        // gsv_ADD_FIELDITEM& ItemInfo = m_FieldItemList[ i ];

        int iItemOBJ = g_pObjMGR->Add_GndITEM(m_FieldItemList[i].m_wServerItemIDX,
            m_FieldItemList[i].m_ITEM,
            m_FieldItemList[i].m_PosCUR,
            this->m_PosCUR.z,
            true);

        if (iItemOBJ) {
            CObjITEM* pITEM = (CObjITEM*)g_pObjMGR->Get_OBJECT(iItemOBJ);
            pITEM->m_wOwnerServerObjIDX = m_FieldItemList[i].m_wOwnerObjIDX;
            pITEM->m_wRemainTIME = ITEM_OBJ_LIVE_TIME;

            pITEM->InsertToScene();

            // m_pRecvPacket->m_gsv_ADD_FIELDITEM.m_wOwnerObjIDX;
            // m_pRecvPacket->m_gsv_ADD_FIELDITEM.m_wRemainTIME;

            tagITEM sITEM = m_FieldItemList[i].m_ITEM;

            /*if ( sITEM.m_cType != ITEM_TYPE_MONEY )
                AddMsgToChatWND(CStr::Printf ("아이템 %s 드롭 Type: %d, NO: %d ",
                                                        ITEM_NAME( sITEM.m_cType, sITEM.m_nItemNo ),
                                                        sITEM.m_cType, sITEM.m_nItemNo ), g_dwBLUE
            ,CChatDLG::CHAT_TYPE_SYSTEM); else AddMsgToChatWND(CStr::Printf ("돈 드롭 %d ",
            sITEM.m_nItemNo), g_dwBLUE ,CChatDLG::CHAT_TYPE_SYSTEM);*/
        }
    }

    m_FieldItemList.clear();
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 총알에 맞았을때 스킬처리..
//--------------------------------------------------------------------------------

bool
CObjCHAR::ProcessSkillHit(CObjCHAR* pFromOBJ, int iSkillIDX) {
    if (pFromOBJ == NULL)
        return false;

    const int iServerTarget = g_pObjMGR->Get_ServerObjectIndex(this->Get_INDEX());
    const bool bHadRegisteredCast = pFromOBJ->ConsumePendingProjectileSkill(iServerTarget,
        iSkillIDX);
    LogString(LOG_DEBUG_,
        "CombatTrace projectile impact: caster %d target %d skill %d registered_cast %d\n",
        pFromOBJ->Get_INDEX(),
        iServerTarget,
        iSkillIDX,
        bHadRegisteredCast ? 1 : 0);
    return pFromOBJ->ProcEffectedSkill(true, iSkillIDX, iServerTarget);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 몹 소환.. ( 현재는 소환몹은 프레임에 않맞추고 바로 생성.. 서버와 명령 동기화 문제
///				서버에서는 생성된 몹에 공격명령을 내릴수 있는데 클라이언트에서는 아직생성 되지
///않았으므로..
//--------------------------------------------------------------------------------

void
CObjCHAR::SetSummonMobInfo(gsv_MOB_CHAR& MobInfo) {
    memcpy(&m_SummonMob, &MobInfo, sizeof(gsv_MOB_CHAR));
    m_bHaveSummonedMob = true;

    SetEffectedSkillFlag(true);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param
/// @brief  : 몹 소환.. ( 현재는 소환몹은 프레임에 않맞추고 바로 생성.. 서버와 명령 동기화 문제
///				서버에서는 생성된 몹에 공격명령을 내릴수 있는데 클라이언트에서는 아직생성 되지
///않았으므로..
//--------------------------------------------------------------------------------

void
CObjCHAR::SummonMob() {
    if (m_bHaveSummonedMob) {
        D3DVECTOR PosCUR;

        PosCUR.x = m_SummonMob.m_PosCUR.x;
        PosCUR.y = m_SummonMob.m_PosCUR.y;
        PosCUR.z = 0.0f;

        short nCObj;

        if (NPC_TYPE(m_SummonMob.m_nCharIdx) != 999) {
            nCObj = g_pObjMGR->Add_MobCHAR(m_SummonMob.m_wObjectIDX,
                m_SummonMob.m_nCharIdx,
                PosCUR,
                m_SummonMob.m_nQuestIDX,
                m_SummonMob.m_btMoveMODE);
            if (g_pNet->Recv_tag_ADD_CHAR(nCObj, &m_SummonMob)) {
                short nOffset = sizeof(gsv_MOB_CHAR);
                g_pNet->Recv_tag_ADJ_STATUS(nOffset, &m_SummonMob);
            }
        }
    }

    m_bHaveSummonedMob = false;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @param  CObjCHAR *pFromOBJ 공격자
/// @param  int iEffectIDX		총알번호
/// @param  int iSkillIDX		스킬일경우.. 스킬번호
/// @param  bool bIsSkillEffect	이것이 스킬에 의한 매직인가?
/// @param  bool bIsBulletHit
/// @brief  : 맞았을때의 행동 처리.. 데미지를 준다던지. 이펙트를 출력한다던지.
/// @todo Reg_DAMAGE를 검색해서 타격의 종류를 판단해야...
//--------------------------------------------------------------------------------

bool
CObjCHAR::Hitted(CObjCHAR* pFromOBJ,
    int iEffectIDX,
    int iSkillIDX,
    bool bIsSkillEffect,
    bool bIsBulletHit,
    bool bJustEffect) {
    if (NULL == pFromOBJ)
        return true;

//-------------------------------------------------------------------------
///박지호::공격자가 유저인자를 판단한다.
#define HIT_AROA_EFF 1613

    BOOL IsAcceptAroa = FALSE;

    D3DXVECTOR3 pos = this->Get_CurPOS();

    bool bAllowHitFeedback = bJustEffect;
    uniDAMAGE stDmage;
    stDmage.m_wDamage = 0;

    if (!bJustEffect) {
        if (bIsSkillEffect) {
            if (!IsProjectilePresentedSkillDamage(iSkillIDX)) {
                /// 총알이 맞았을경우.. bIsSkillEffect가 세팅되어 아래를 처리해준다.
                ProcessSkillHit(pFromOBJ, iSkillIDX);
                return true;
            }
        }

        Rose::Combat::DamageEvent damageEvent;
        Rose::Combat::PresentationResult presentation =
            PopCombatDamageEvent(pFromOBJ->Get_INDEX(), damageEvent);

        if (presentation == Rose::Combat::PresentationResult::NoEvent) {
            return true;
        }

        pFromOBJ->ClearPendingCombatSwingPresentation(damageEvent.event_id);

        const bool bSuppressOutgoingForPendingDeath =
            ShouldSuppressOutgoingDamageForPendingDeath(pFromOBJ);
        const bool bMutualDeathWhilePending =
            bSuppressOutgoingForPendingDeath
            && (damageEvent.lethal || damageEvent.hp_after <= DEAD_HP);
        if (bSuppressOutgoingForPendingDeath && !bMutualDeathWhilePending) {
            LogString(LOG_DEBUG_,
                "CombatTrace outgoing damage suppressed by pending authoritative death: attacker %d target %d event %u damage %d hp_after %d\n",
                pFromOBJ->Get_INDEX(),
                this->Get_INDEX(),
                damageEvent.event_id,
                damageEvent.damage_value,
                damageEvent.hp_after);

            g_UIMed.CreateDamageDigit(0,
                pos.x,
                pos.y,
                pos.z + m_fStature,
                this->IsA(OBJ_USER));
            return true;
        }

        if (bIsSkillEffect) {
            ProcessSkillHit(pFromOBJ, iSkillIDX);
        }

        m_EndurancePack.ClearStateByHitted();
        bAllowHitFeedback = presentation != Rose::Combat::PresentationResult::PresentedMiss;
        ApplyPresentedCombatDamage(pFromOBJ, damageEvent);
        stDmage.m_wDamage = damageEvent.raw_damage;
        stDmage.m_wVALUE = max(0, damageEvent.damage_value);

        g_UIMed.CreateDamageDigit(stDmage.m_wVALUE,
            pos.x,
            pos.y,
            pos.z + m_fStature,
            this->IsA(OBJ_USER));

        /// 타격시 흔들림..
        if (stDmage.m_wVALUE) {
            if (this->GetPetMode() < 0)
                this->StartVibration();
            else {
                //박 지호::펫 모드에서 흔들림 설정
                m_IsCartVA = GetPetMode() ? TRUE : FALSE;
            }
        }

        if (bMutualDeathWhilePending && g_pAVATAR) {
            g_pAVATAR->PresentPendingAuthoritativeDeath(this, "mutual death");
        }

        if (stDmage.m_wACTION & DMG_ACT_CRITICAL) {
            /// 카메라 진동..
            if (IsA(OBJ_USER)) {
                D3DXVECTOR3 vMin(-10, -10, -10), vMax(10, 10, 50);
                ::shakeCamera(g_pCamera->GetZHANDLE(),
                    vMin,
                    vMax,
                    200); // 카메라 흔들림.
            }

            if (iEffectIDX) {
                int iCriticalEffect = EFFECT_HITTED_CRITICAL(iEffectIDX);

                ///아로아 이펙트
                ChangeHittedEffect(pFromOBJ, IsAcceptAroa, iCriticalEffect);

                CEffect* pHitEFT =
                    g_pEffectLIST->Add_EffectWithIDX(iCriticalEffect, true);
                if (pHitEFT) {
                    pHitEFT->SetPOSITION(this->Get_CurPOS());
                    pHitEFT->InsertToScene();
                }
            }
        }
    }

    ///
    ///	Miss 일경우는 찍지 않는다.
    ///

    // if( stDmage.m_wVALUE <= 0 )
    //	return true;

    // 타격 효과.
    if (!bAllowHitFeedback) {
        return true;
    }

    if (this->IsVisible()) {
        // short nEffectIDX = WEAPON_DEFAULT_EFFECT( pFromOBJ->Get_R_WEAPON() );
        if (iEffectIDX) {
            int iHitEffect = iEffectIDX;

            /// skill 로부터의 타격은 직접 file_effect 에서
            /// 일반공격은 무기테이블.. 즉 List_Effect 에서..
            if (!bIsSkillEffect)
                iHitEffect = EFFECT_HITTED_NORMAL(iEffectIDX);

            /// 총알아 맞아서 터질경우....
            if (bIsSkillEffect && bIsBulletHit) {
                iHitEffect = SKILL_HIT_EFFECT(iSkillIDX);

                if (iHitEffect) {
                    ///아로아 이펙트
                    ChangeHittedEffect(pFromOBJ, IsAcceptAroa, iHitEffect);

                    CEffect* pHitEFT = g_pEffectLIST->Add_EffectWithIDX(iHitEffect, true);
                    if (pHitEFT) {
                        if (SKILL_HIT_EFFECT_LINKED_POINT(iSkillIDX) == INVALID_DUMMY_POINT_NUM)
                            pHitEFT->LinkNODE(this->GetZMODEL());
                        else
                            this->LinkDummy(pHitEFT->GetZNODE(),
                                SKILL_HIT_EFFECT_LINKED_POINT(iSkillIDX));

                        /// pHitEFT->SetParentCHAR( this );
                        /// AddExternalEffect( pHitEFT );

                        pHitEFT->UnlinkVisibleWorld();
                        pHitEFT->InsertToScene();
                    }
                }
            } else
            // 일반 적인경우..
            {
                if (iHitEffect) {
                    ///아로아 이펙트
                    ChangeHittedEffect(pFromOBJ, IsAcceptAroa, iHitEffect);

                    CEffect* pHitEFT = g_pEffectLIST->Add_EffectWithIDX(iHitEffect, true);
                    if (pHitEFT) {
                        pHitEFT->LinkNODE(this->GetZMODEL());

                        /// pHitEFT->SetParentCHAR( this );
                        /// AddExternalEffect( pHitEFT );

                        pHitEFT->UnlinkVisibleWorld();
                        pHitEFT->InsertToScene();
                    }
                }
            }

            //----------------------------------------------------------------------------------------------------
            /// @brief Hit sound 출력
            //----------------------------------------------------------------------------------------------------
            int iHitSound = 0;
            if (!bIsSkillEffect) {
                iHitSound = EFFFCT_HIT_SND_IDX(iEffectIDX);
            } else {
                iHitSound = SKILL_HIT_SOUND(iSkillIDX);
            }

            if (iHitSound) {
                g_pSoundLIST->IDX_PlaySound3D(iHitSound, Get_CurPOS());
            }
            //----------------------------------------------------------------------------------------------------
        }
    }

#ifdef __VIRTUAL_SERVER
    g_pNet->Send_cli_DAMAGE(pFromOBJ, this, stDmage.m_wDamage);
#endif
    return true;
}

//--------------------------------------------------------------------------------
/// class : ChangeHittedEffect
/// 박지호: 아로아 및 카트 스킬 이펙트를 설정한다.
//--------------------------------------------------------------------------------
void
CObjCHAR::ChangeHittedEffect(CObjCHAR* pAttackObj, BOOL bA, int& hitIDX) {

    /*
        //카트 스킬 이펙트
        if(pAttackObj->GetUseCartSkill())
        {
            hitIDX = SKILL_HIT_EFFECT(pAttackObj->GetCartSKIDX());
            pAttackObj->GetUseCartSkill() = FALSE;
        }
    */
    //아로아 이펙트
    if (bA) {
        CObjCHAR* pTg = pAttackObj;

        //펫이라면...
        if (pAttackObj->IsPET()) {
            CObjCART* pCart = (CObjCART*)pAttackObj;
            if (pCart)
                pTg = pCart->GetParent();
        }

        if (pTg && bA && pTg->SetAroaState())
            hitIDX = HIT_AROA_EFF;
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 공격 거리를 구한다. 스킬에 의한공격이라면 스킬테이블에서 무기에 의한 일반 공격이라면
/// 무기테이블에서
//--------------------------------------------------------------------------------

int
CObjCHAR::Get_AttackRange() {
    /// 스킬에 공격 거리가 입력되어 있다면 스킬거리 아니면 무기 거리..
    if (this->m_nToDoSkillIDX > 0) {
        if (this->m_nToDoSkillIDX < g_SkillList.Get_SkillCNT()) {
            if (SKILL_DISTANCE(this->m_nToDoSkillIDX)) {
                return SKILL_DISTANCE(this->m_nToDoSkillIDX);
            }
        } else
            assert(0 && "Get_AttackRange Failed[ ToDoSkillIDX is invalid ]");
    }

    if (Get_R_WEAPON()) {
        return WEAPON_ATTACK_RANGE(Get_R_WEAPON()) + (Get_SCALE() * 120);
    }

    // Default attack range...
    return (Def_AttackRange() + (Get_SCALE() * 120));
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

void
CObjCHAR::Set_EMOTION(short nEmotionIDX) {
    ;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

void
CObjCHAR::Special_ATTACK() {
    // CObjCHAR *pTarget = Get_TARGET ();

    ;
    ;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 워프.. 현재 위치를 갱신하고 워프.. 지형갱신..
//--------------------------------------------------------------------------------

void
CObjCHAR::Warp_POSITION(float fX, float fY) {
    SetCMD_STOP();

    m_PosCUR.x = fX;
    m_PosCUR.y = fY;

    g_pTerrain->SetCenterPosition(fX, fY);

    DropFromSky(fX, fY);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 하늘과 충돌.. 그위치로..
//--------------------------------------------------------------------------------

void
CObjCHAR::DropFromSky(float fX, float fY) {
    m_PosCUR.x = fX;
    m_PosCUR.y = fY;
    m_PosCUR.z = ::g_pTerrain->GetHeightTop(fX, fY);

    ::setPosition(m_hNodeMODEL, m_PosCUR.x, m_PosCUR.y, m_PosCUR.z);
    ::savePrevPosition(m_hNodeMODEL);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 모델을 특정방향으로 회전.
//--------------------------------------------------------------------------------

void
CObjCHAR::Set_ModelDIR(t_POSITION& PosToView, bool bImmediate) {
    ::setModelDirectionByPosition(this->m_hNodeMODEL, PosToView.x, PosToView.y);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 모델을 특정방향으로 회전.
//--------------------------------------------------------------------------------

void
CObjCHAR::Set_ModelDIR(float fAngleDegree, bool bImmediate) {
    ::setModelDirection(this->m_hNodeMODEL, fAngleDegree, bImmediate);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 모델을 특정방향으로 회전.
//--------------------------------------------------------------------------------

void
CObjCHAR::Add_ModelDIR(float fAngleDegree) {
    float fDir = ::getModelDirection(this->m_hNodeMODEL);

    ::setModelDirection(this->m_hNodeMODEL, fAngleDegree + fDir, true);
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 이동 마무리..
//--------------------------------------------------------------------------------

void
CObjCHAR::Move_COMPLETED() {
    m_PosGOTO.z = m_PosCUR.z;
    m_PosCUR = m_PosGOTO;

    ::setPosition(this->m_hNodeMODEL, m_PosCUR.x, m_PosCUR.y, m_PosCUR.z);

    this->adjusted_move_speed = this->stats.move_speed;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief : 이동시 시작 위치의 높이값을 서버에서 받아온 값으로 설정하여 높이를 맞춘다.
//--------------------------------------------------------------------------------

void
CObjCHAR::Reset_Position() {
    return;

    /// m_PosCUR.z = m_PosGOTO.z; // 목표 지점의 z 값이 실은 시작 위치의 높이 값이다.
    ///::setPositionVec3( this->m_hNodeMODEL, m_PosCUR );
}

void
CObjCHAR::RecoverHP(short nRecoverMODE) {
    int iRecoverHP = Get_RecoverHP(nRecoverMODE);
    int iAruaAddHP = (m_IsAroa) ? iRecoverHP >> 1 : 0;

    Add_HP(iRecoverHP + iAruaAddHP);

    //	_RPT2( _CRT_WARN,"RecoverHP:%d, AruaAddHP:%d\n", iRecoverHP, iAruaAddHP );

    int iReviseConstHP =
        iRecoverHP; // Get_MaxHP() / 30;///매틱마다 서버와의 차이를 줄이기 위한 보정값
    if (iReviseConstHP < 10)
        iReviseConstHP = 10;

    if (m_ReviseHP > 0) {
        if (m_ReviseHP > iReviseConstHP) {
            Add_HP(iReviseConstHP);
            _RPT1(_CRT_WARN, "Add Revise HP %d\n", iReviseConstHP);
            m_ReviseHP -= iReviseConstHP;
        } else {
            Add_HP(m_ReviseHP);
            _RPT1(_CRT_WARN, "Add Revise HP %d\n", m_ReviseHP);
            m_ReviseHP = 0;
        }
    } else if (m_ReviseHP < 0) {
        if (abs(m_ReviseHP) > iReviseConstHP) {
            Add_HP(-iReviseConstHP);
            _RPT1(_CRT_WARN, "Add Revise HP %d\n", -iReviseConstHP);
            m_ReviseHP += iReviseConstHP;
        } else {
            Add_HP(m_ReviseHP);
            _RPT1(_CRT_WARN, "Add Revise HP %d\n", m_ReviseHP);
            m_ReviseHP = 0;
        }
    }

    int iMaxHP = Get_MaxHP();
    if (Get_HP() > Get_MaxHP())
        Set_HP(Get_MaxHP());
}

void
CObjCHAR::RecoverMP(short nRecoverMODE) {
    int iRecoverMP = Get_RecoverMP(nRecoverMODE);
    int iAruaAddMP = (m_IsAroa) ? iRecoverMP >> 1 : 0;

    Add_MP(iRecoverMP + iAruaAddMP);

    int iReviseConstMP =
        iRecoverMP; // Get_MaxMP() / 30;///매틱마다 서버와의 차이를 줄이기 위한 보정값
    if (iReviseConstMP < 10)
        iReviseConstMP = 10;

    if (m_ReviseMP > 0) {
        if (m_ReviseMP > iReviseConstMP) {
            Add_MP(iReviseConstMP);
            m_ReviseMP -= iReviseConstMP;
        } else {
            Add_MP(m_ReviseMP);
            m_ReviseMP = 0;
        }
    } else if (m_ReviseMP < 0) {
        if (abs(m_ReviseMP) > iReviseConstMP) {
            Add_MP(-iReviseConstMP);
            m_ReviseMP += iReviseConstMP;
        } else {
            Add_MP(m_ReviseMP);
            m_ReviseMP = 0;
        }
    }

    if (Get_MP() > Get_MaxMP())
        Set_MP(Get_MaxMP());
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 캐릭터 높이 갱신. 각종 충돌이 여기서 처리된다.
//--------------------------------------------------------------------------------

void
CObjCHAR::Adjust_HEIGHT() {
    m_pCollision->UpdateHeight(this);
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 특정 액션 중간에 장비를 교체했을시( 특히 무기 ) 바뀐 장비에 맞는 모션으로 교체
//----------------------------------------------------------------------------------------------------

void
CObjCHAR::UpdateMotionByChangingEquip() {
    if (this->IsA(OBJ_AVATAR) || this->IsA(OBJ_USER)) {
        CObjAVT* pAvt = (CObjAVT*)this;
        /// 무기를 바꿀께 있다면..
        if (pAvt->GetUpdateMotionFlag() || pAvt->GetChangeWeaponR() || pAvt->GetChangeWeaponL()) {
            /// 새로운 모션 세팅을 위한 세로운 명령 세팅
            switch (pAvt->Get_COMMAND()) {
                case CMD_STOP: {
                    pAvt->SetCMD_STOP();
                } break;
                case CMD_MOVE: {
                    // pAvt->SetCMD_MOVE( pAvt->m_PosGOTO.x, pAvt->m_PosGOTO.y, pAvt->m_bRunMODE );
                    pAvt->Set_MOTION(pAvt->GetANI_Move(),
                        pAvt->m_fCurMoveSpeed,
                        pAvt->m_fRunAniSPEED);
                } break;
                case CMD_ATTACK: {
                    pAvt->SetCMD_ATTACK(pAvt->m_iServerTarget);
                    // pAvt->Set_MOTION( pAvt->GetANI_Attack(), pAvt->m_fCurMoveSpeed,
                    // pAvt->m_fRunAniSPEED );
                } break;
                case CMD_DIE:
                    break;
                case CMD_PICK_ITEM:
                    break;
                case CMD_SKILL2SELF:
                    break;
                case CMD_SKILL2OBJ:
                    break;
                case CMD_SKILL2POS:
                    break;
                case CMD_RUNAWAY:
                    break;
            }

            // pAvt->Update ();

            /// if pCHAR is my avatar, update ability
            if (pAvt->IsA(OBJ_USER)) {
                ((CObjUSER*)pAvt)->UpdateAbility();
            }

            pAvt->SetChangeWeaponR(0);
            pAvt->SetChangeWeaponL(0);

            pAvt->SetUpdateMotionFlag(false);
        }
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 캐릭터 처리함수
/// @todo 테스트 코드 빼라...//if (this->Get_TYPE() != OBJ_MOB )
/// @todo View list 고쳐라..
//--------------------------------------------------------------------------------

int
CObjCHAR::Proc(void) {
    /*
     *
     * for debugging...
     *
     */

    // if( m_iCastingStartTime != NULL )
    //{
    //	if( ( g_GameDATA.GetGameTime() - m_iCastingStartTime ) > SKILL_PROC_LIMIT )
    //	{
    //		///Casting_END();
    //		assert( 0 && "Casting time expired" );
    //	}
    //}

#if defined(_GBC)
    //--------------------------------------------------------------------------------
    //박지호::카트 뒷자석에 유저를 탑승 시켰다면 카트위치를 2인승 유저에게 업데이터 한다.
    if (this->GetPetMode() > -1) {
        if (m_pObjCART->m_pRideUser) {
            float Pos1[3] = {0};

            // 2인승 더미 위치값을 가져온다.
            ::getDummyPosition(m_pObjCART->GetZMODEL(), 10, Pos1);
            // 2인승 유저 위치 업데이터
            m_pObjCART->m_pRideUser->Set_CurPOS(D3DXVECTOR3(Pos1[0], Pos1[1], Pos1[2]));
        }
    }
//--------------------------------------------------------------------------------
#endif

    //--------------------------------------------------------------------------------
    /// 오브젝트 바이브레이션 업데이트.~~ 흔들자~~
    //--------------------------------------------------------------------------------
    m_ObjVibration.Proc();

    //--------------------------------------------------------------------------------
    /// 외부 데코레이션들 업데이트
    //--------------------------------------------------------------------------------
    m_EndurancePack.Update();

    //--------------------------------------------------------------------------------
    /// visibility 변화
    /// 캐릭터 사망시 아예 모션이 업데이트 안되는경우가 있다.. 이것이 아래 코드와 연관이 있는가?
    /// -04/5/25
    //--------------------------------------------------------------------------------
    if (m_pChangeVisibility != NULL) {
        /// if expired?
        if (m_pChangeVisibility->ProcessVisibility(g_GameDATA.GetElapsedFrameTime()) == false) {
            delete m_pChangeVisibility;
            m_pChangeVisibility = NULL;

            return 0;
        }

        assert(!(this->IsA(OBJ_USER)) && " 뭐냥 유젼데 왠 비져비리티? ");

        return 1;
    }

    //--------------------------------------------------------------------------------
    /// Update check frame
    //--------------------------------------------------------------------------------
    DWORD dwCurrentTime = g_GameDATA.GetGameTime();
    m_dwFrameElapsedTime = dwCurrentTime
        - m_dwLastRecoveryUpdateTime; /// 이전프레임에서 현재 프레임 사이에 흐른시간을 더해준다.
    m_dwLastRecoveryUpdateTime = dwCurrentTime;

    // Pending-authoritative-death backstop. The avatar can be flagged dead by the
    // server (Reconcile_HP(DEAD_HP)) or by a mid-swing kill, yet present nothing if
    // no incoming hit ever arrives to fold the death in -- e.g. a no-attacker kill
    // (DoT, fall damage) or an attacker that despawned/went out of range. Layer 1
    // (mutual-death-on-kill in DrainQueuedCombatDamageFromAttacker) handles the
    // common case instantly; this is the catch-all so the player is never left
    // alive-client / dead-server and frozen. Wrap-safe DWORD subtraction.
    static const DWORD kPendingAuthoritativeDeathTimeoutMs = 1500;
    if (this == g_pAVATAR
        && m_bPendingAuthoritativeDeath
        && m_dwPendingAuthoritativeDeathTime != 0
        && this->Get_HP() > DEAD_HP
        && (dwCurrentTime - m_dwPendingAuthoritativeDeathTime)
            >= kPendingAuthoritativeDeathTimeoutMs) {
        PresentPendingAuthoritativeDeath(NULL, "pending death timeout");
    }

    // Slow attack motions (cart / castle gear weapons, low attack speed) put the
    // killer's hit frame legitimately later than the stale grace window. While the
    // attacker is alive, still in its attack command, and still holds this exact
    // event as its pending confirmed swing, the hit frame is coming -- defer the
    // stale pop instead of killing the defender mid-swing. Mounted swings key the
    // event to the cart but track the pending swing on the rider, so check both.
    // The hard cap keeps the fallback alive for swings whose consumer never fires.
    static const DWORD kStaleLethalSwingHardCapMs = 6000;
    auto fnSwingStillPending = [](const Rose::Combat::DamageEvent& event) -> bool {
        CObjCHAR* pAtkOBJ = g_pObjMGR->Get_CharOBJ(event.attacker_id, true);
        if (!pAtkOBJ || pAtkOBJ->Get_HP() <= DEAD_HP
            || pAtkOBJ->Get_COMMAND() != CMD_ATTACK) {
            return false;
        }
        if (pAtkOBJ->HasPendingCombatSwingEvent(event.event_id)) {
            return true;
        }
        if (pAtkOBJ->IsPET()) {
            CObjCHAR* pRider = ((CObjCART*)pAtkOBJ)->GetParent();
            if (pRider && pRider->HasPendingCombatSwingEvent(event.event_id)) {
                return true;
            }
        }
        return false;
    };

    if (this != g_pAVATAR && this->Get_HP() > DEAD_HP) {
        Rose::Combat::DamageEvent staleDeathEvent;
        if (m_CombatDamageQueue.pop_stale_lethal(dwCurrentTime,
                kPendingAuthoritativeDeathTimeoutMs,
                kStaleLethalSwingHardCapMs,
                DEAD_HP,
                fnSwingStillPending,
                staleDeathEvent)) {
            CObjCHAR* pAtkOBJ = g_pObjMGR->Get_CharOBJ(staleDeathEvent.attacker_id, true);
            LogString(LOG_DEBUG_,
                "CombatTrace stale lethal melee event presented: attacker %d target %d event %u seq %u damage %d hp_after %d queue %d\n",
                staleDeathEvent.attacker_id,
                this->Get_INDEX(),
                staleDeathEvent.event_id,
                staleDeathEvent.defender_seq,
                staleDeathEvent.damage_value,
                staleDeathEvent.hp_after,
                static_cast<int>(m_CombatDamageQueue.size()));
            ApplyPresentedCombatDamage(pAtkOBJ, staleDeathEvent);
            CreateImmediateDigitEffect(staleDeathEvent.raw_damage);
        }
    }

    g_pObjMGR->AddViewObject(m_nIndex);

    //--------------------------------------------------------------------------------
    /// 거리에 따른 오브젝트들의 관리( 엔진에 등록 혹은 뺀다.. )
    /// 2004/3/17 클라이언트 차원에서 거리에 따른 모델의 관리는 불필요하다. 서버에서 난라온건 다
    /// 관리해준다. 필요할경우 LOD 적용이 반영된다. 2004/7/8 InsertToScene/removeFromScene 등은
    /// 필요없다 서버에서 받는데로 추가하고 삭제하라..
    //--------------------------------------------------------------------------------
    bool bInViewfrustum = false;
    /// int iDistance = CD3DUtil::distance ((int)g_GameDATA.m_PosCENTER.x,
    /// (int)g_GameDATA.m_PosCENTER.y, (int)m_PosCUR.x, (int)m_PosCUR.y); if ( iDistance <
    /// 8*4*nGRID_SIZE )
    {
        /// this->InsertToScene ();

        // char name & chatting message
        if (::inViewfrustum(this->GetZMODEL())) // 뷰프러스텀 안에 있으면 참, 없으면 거짓
        {
            bInViewfrustum = true;
            if (this->Get_TYPE() != OBJ_MOB) {
                g_pViewMSG->AddObjIDX(m_nIndex);
            }
        }
    } /*else
    {
        this->RemoveFromScene ();
    }*/

    //--------------------------------------------------------------------------------
    /// 뷰프러스텀 안에 있는 오브젝트들만 높이값 변경(부하 많이 먹기 때문)
    //--------------------------------------------------------------------------------
    if (bInViewfrustum) // 뷰프러스텀 안에 있는 오브젝트들만 높이값 변경(부하 많이 먹기 때문)
    {
        /// 내가 누군가에에 링크되어있다면.. 충돌처리 안함..
        //조성현 캐릭터 변신할때...
        // if(m_bDisguise)
        //{
        //	::setScale(this->m_hNodeMODEL, 1.0f, 1.0f, 1.0f);
        //}

        if (this->IsChild() == false)
            Adjust_HEIGHT();

        //조성현 캐릭터 변신할때...
        // if(m_bDisguise)
        //{
        //	::setLightingRecursive(this->GetZMODEL(), g_GameDATA.m_hCharacterLight2);
        //	::setVisibilityRecursive(this->GetZMODEL(), 0.15f);
        //	::setScale(this->GetZMODEL(), 0.7f, 0.7f, 0.7f);
        //	::setVisibleGlowRecursive( this->GetZMODEL(), 2, 0.2f, 0.5f, 0.945f );
        //}

    } else {

#if (1) /// 현재, 이동 시작시 높이 보정이 되기 때문에, 높이 보정 필요 없음.
        ::getPosition(this->m_hNodeMODEL, (float*)m_PosCUR);
        // m_PosCUR.z = g_pAVATAR->Get_CurPOS().z; // 안보이는 캐릭터의 높이는 주인공 아바타의
        // 높이에 맞춤.

        /// 아바타 이거나( 카트를 탄상태가 아닌), 내가 카드일경우에만..
        /// if ( ( this->Is_AVATAR() && ( this->GetPetMode() < 0 ) ) ||
        ///	this->IsPET() )
        {
            D3DXVECTOR3 vAvatarPos = g_pAVATAR->Get_CurPOS();
            float fDistanceSquare2D = (vAvatarPos.x - m_PosCUR.x) * (vAvatarPos.x - m_PosCUR.x)
                + (vAvatarPos.y - m_PosCUR.y) * (vAvatarPos.y - m_PosCUR.y);
            const float fMinDistanceSquare2D = 4000.0f * 4000.0f;
            // 아주 가까운 거리에 있을 경우에만 높이 조정
            if (fDistanceSquare2D < fMinDistanceSquare2D) {
                Adjust_HEIGHT();
                //	// 이동 중일 거라는 가정이 성립한다면, 현재 위치가 바른 위치이다.
                //	//m_PosCUR.z = g_pTerrain->GetHeightTop(m_PosCUR.x, m_PosCUR.y);
                //	::setPosition (m_hNodeMODEL, m_PosCUR.x, m_PosCUR.y, m_PosCUR.z);
            }
        }
#endif
    }

    //--------------------------------------------------------------------------------
    /// 모션 프레임이 끝났는가?
    //--------------------------------------------------------------------------------
    m_bFrameING = this->ProcMotionFrame();

    //--------------------------------------------------------------------------------
    // 모션 루프가 끝났다.
    //--------------------------------------------------------------------------------
    if (!m_bFrameING) {
        //--------------------------------------------------------------------------------
        /// 항상 ActionSkillIDX 를 스킬 액션 시작시 DoingSkill 에 등록시키고 모션이 끝나면 리셋..(
        /// 모션프레임에서 스킬번호를 참조해야하기 때문에 )
        //--------------------------------------------------------------------------------
        m_nDoingSkillIDX = 0;

//박지호::카트 공격시 캐릭터를 않힌다.
#if defined(_GBC)
        Set_SitMode();
#endif
        // { added by zho 2003-12-17
        // 검잔상 중지
        for (short nI = 0; nI < 2; nI++) {
            if (m_hTRAIL[nI]) {
                ::controlTrail(m_hTRAIL[nI], 3); // 점차 사라짐.
            }
        }

        //--------------------------------------------------------------------------------
        /// 장비교체에 따른 적당한 모션 교체..
        //--------------------------------------------------------------------------------
        UpdateMotionByChangingEquip();

    } else if (Get_STATE() & CS_BIT_INT) // 이동작중에는 암거두 할수 없다.
    {
        return 1;
    }

    //--------------------------------------------------------------------------------
    /// 큐에 쌓인 명령 소진...
    /// CanApplyCommand 가 false 일때는 적용이 불가능하다.
    /// 현재는 m_bCastingSTART = true 일때만이다.
    //--------------------------------------------------------------------------------
    ProcQueuedCommand();

    //--------------------------------------------------------------------------------
    /// 캐스팅을 위한 틀별한 처리
    /// Skill_START 에서 m_bCastingSTART = true 로 만든다.
    /// 그러나 마지막 스킬액션이 끝난 시점을 알수 없으므로.. 일단
    /// 이것에 의존한다.
    /// 04/5/28 - Skill 시전( DO_SKILL 함수 ) 를 리팩토링 하면서 없어짐.( 필요가 없게 되었다. )
    //--------------------------------------------------------------------------------
    /// Casting_END ();

    //--------------------------------------------------------------------------------
    /// Effect_Of_Skill 타임아웃 체크
    //--------------------------------------------------------------------------------
    ProcTimeOutEffectedSkill();

    /// if current hp <= 0 do CMD die
    if (Get_HP() == 0) {
        _ASSERT(0 && "/// if current hp <= 0 do CMD die");
        this->SetCMD_DIE();
    }

    switch (Get_COMMAND()) {
        case CMD_DIE:
            return this->IsUSER();

        case CMD_SIT:
            return ProcCMD_SIT();

        case CMD_STOP:
            return ProcCMD_STOP();

        case CMD_MOVE:
            return ProcCMD_MOVE();

        case CMD_PICK_ITEM:
            return ProcCMD_PICK_ITEM();

        case CMD_ATTACK:
            return ProcCMD_ATTACK();

        case CMD_SKILL2SELF:
            return ProcCMD_Skill2SELF();

        case CMD_SKILL2POS:
            return ProcCMD_Skill2POSITION();

        case CMD_SKILL2OBJ:
            return ProcCMD_Skill2OBJECT();
    }

    return 1;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// 박지호: Set_SitMode()
///       : 카트가 공격할때 아바타를 앉친다
///
//--------------------------------------------------------------------------------
void
CObjCHAR::Set_SitMode(void) {

    if (this->GetPetMode() > 0) {
        //		if(this->m_pObjCART->Get_COMMAND() == CMD_ATTACK)
        this->Set_MOTION(this->m_pObjCART->GetRideAniPos());
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// 박지호: ProcCMD_Skill2OBJECT_PET()
///       : 펫 상태의 타켓스킬의 설정한다.
///
//--------------------------------------------------------------------------------
int
CObjCHAR::ProcCMD_Skill2OBJECT_PET() {

    CObjCHAR* pTarget = CSkillManager::GetSkillTarget(m_iServerTarget,
        (this->m_nToDoSkillIDX) ? this->m_nToDoSkillIDX : this->m_nActiveSkillIDX);

    if (pTarget) {
        //-----------------------------------------------------------------------------------------
        /// 아직 스킬 캐스팅을 시작하지 않았다면..
        //-----------------------------------------------------------------------------------------
        if (!m_bCastingSTART) {
            int iAttackRange = this->Get_AttackRange();

            //타켓 정보를 설정한다.
            m_pObjCART->m_PosGOTO = pTarget->Get_CurPOS();
            m_pObjCART->Set_TargetIDX(m_iServerTarget);

            if (m_pObjCART->Goto_TARGET(pTarget, iAttackRange) == false) {
                m_pObjCART->SetCMD_MOVE(pTarget->Get_CurPOS(), TRUE);
                return 1;
            } else {
                m_pObjCART->Set_STATE(CS_STOP);
                m_pObjCART->m_fCurMoveSpeed = 0;

                Set_STATE(CS_STOP);
                m_fCurMoveSpeed = 0;
            }

            if (!bCanStartSkill()) {
                // Pet
                m_pObjCART->Set_STATE(CS_STOP);
                m_pObjCART->m_fCurMoveSpeed = 0;

                //아바타
                Set_STATE(CS_STOP);
                m_fCurMoveSpeed = 0;
                /// this->Set_MOTION( this->GetANI_Casting() );
                return 1;
            }

            m_pObjCART->Set_ModelDIR(pTarget->m_PosCUR);
        }

        m_pObjCART->Set_ModelDIR(pTarget->m_PosCUR);

        //-----------------------------------------------------------------------------------------
        /// 타겟이 죽어버렸다...
        //-----------------------------------------------------------------------------------------
        if (pTarget->m_bDead) {
            SetEffectedSkillFlag(true);
        }

        //스킬 타입을 가져와서 카트가 스킬을 발동할지  체크한다.
        int sType = SKILL_TYPE(this->m_nToDoSkillIDX);

        ///카트::캐스팅 / 실제동작 적용...
        if (sType == -1) {
            m_pObjCART->m_nToDoSkillIDX = this->m_nToDoSkillIDX;

            if (m_pObjCART->m_SkillActionState == 0)
                m_pObjCART->m_SkillActionState = 1;

            m_pObjCART->Do_SKILL(this->Get_TargetIDX(), pTarget);
        }
        ///아바타::캐스팅 / 실제동작 적용...
        else {
            this->Do_SKILL(this->Get_TargetIDX(), pTarget);
        }

    }

    else {
        SetEffectedSkillFlag(true);
        m_nActiveSkillIDX = 0;
        Casting_END();
    }

    return 1;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// 박지호: SetNewCommandAfterSkill_PET()
///       : 펫 상태의 타켓스킬의 설정한다.
///
//--------------------------------------------------------------------------------
void
CObjCHAR::SetNewCommandAfterSkill_PET(int iSkillNO) {

    switch (SKILL_ACTION_MODE(iSkillNO)) {
        case SA_STOP: {
            m_pObjCART->Set_COMMAND(CMD_STOP);
            this->Set_COMMAND(CMD_STOP);

            break;
        }

        case SA_ATTACK: {
            CObjCHAR* pTarget = reinterpret_cast<CObjCHAR*>(this->Get_TargetOBJ());
            // g_pObjMGR->Get_ClientCharOBJ(this->m_iServerTarget,false);

            if (pTarget) {
                /// 나일경우 PVP존이 아닌존에서 유져공격명령은 취소한다.
                if (this->IsA(OBJ_USER) && pTarget->IsUSER()) {
                    if (!g_pTerrain->is_pvp_zone() || !g_pAVATAR->is_pvp_enabled()
                        || !pTarget->is_pvp_enabled()) {
                        m_pObjCART->Set_COMMAND(CMD_STOP);
                        this->Set_COMMAND(CMD_STOP);
                    }
                } else {
                    // 죽을때 까지 공격 !!!
                    m_pObjCART->Start_ATTACK(pTarget);
                    m_pObjCART->Set_COMMAND(CMD_ATTACK);
                    this->Set_COMMAND(CMD_ATTACK);
                }
            } else {
                m_pObjCART->Set_COMMAND(CMD_STOP);
                this->Set_COMMAND(CMD_STOP);
            }

            break;
        }

        case SA_RESTORE: {
            //이전 공격으로 설정
            this->Set_COMMAND(this->Get_BECOMMAND());
            this->Set_BECOMMAND(CMD_STOP);

            if (m_pObjCART->Get_COMMAND() == CMD_ATTACK) {
                CObjCHAR* pTarget = (CObjCHAR*)(this->Get_TargetOBJ());
                // g_pObjMGR->Get_ClientCharOBJ(this->m_iServerTarget,false);

                if (pTarget) {
                    /// 나일경우 PVP존이 아닌존에서 유져공격명령은 취소한다.
                    if (this->IsA(OBJ_USER) && pTarget->IsUSER()) {
                        if (!g_pTerrain->is_pvp_zone() || !g_pAVATAR->is_pvp_enabled()
                            || !pTarget->is_pvp_enabled()) {
                            m_pObjCART->Set_COMMAND(CMD_STOP);
                            this->Set_COMMAND(CMD_STOP);
                        }
                    }
                    // 아니면 공격을 시도한다.
                    else {
                        m_pObjCART->Start_ATTACK(pTarget);
                        m_pObjCART->Set_COMMAND(CMD_ATTACK);
                        this->Set_COMMAND(CMD_ATTACK);
                    }
                }
            }
        }
    }
}

///--------------------------------------------------------------------------------
/// class : CObjCHAR
/// 박지호: SetRideUser()
///       : 2인승을 한다.
///
//--------------------------------------------------------------------------------
BOOL
CObjCHAR::SetRideUser(WORD irideIDX) {
    //펫모드...
    if (this->GetPetMode() < 0)
        return FALSE;

    //카트생성...
    if (this->m_pObjCART == NULL)
        return FALSE;

    m_iRideIDX = g_pObjMGR->Get_ClientObjectIndex(irideIDX);

    CObjAVT* pTarget = g_pObjMGR->Get_CharAVT(m_iRideIDX, false);
    if (pTarget == NULL)
        return FALSE;

    //펫 모드 설정
    pTarget->SetPetType(this->GetPetMode());
    pTarget->m_pObjCART = this->m_pObjCART;
    pTarget->m_IsRideUser = TRUE;

    //카트에 탑승을 한다.
    this->m_pObjCART->Create(pTarget);

    pTarget->Set_COMMAND(CMD_STOP);
    pTarget->Set_STATE(CS_STOP);

    //아바타 무기 및 날개는 안보이도록 설정
    int iVisibilityPart[3] = {BODY_PART_KNAPSACK, BODY_PART_WEAPON_R, BODY_PART_WEAPON_L};

    for (register int i = 0; i < 3; i++) {
        CMODEL<CCharPART>* pCharPART = pTarget->m_pCharMODEL->GetCharPART(iVisibilityPart[i]);
        if (pCharPART) {
            short nI;

            for (nI = 0; nI < pCharPART->m_nPartCNT; nI++) {
                if (pTarget->m_phPartVIS[iVisibilityPart[i]][nI]) {
                    ::setVisibilityRecursive(pTarget->m_phPartVIS[iVisibilityPart[i]][nI], 0.0f);
                }
            }
        }
    }

    return TRUE;
}

///--------------------------------------------------------------------------------
/// class : CObjCHAR
/// 박지호: ReleaseRideUser()
///       : 2인승을 해제한다.
///
//--------------------------------------------------------------------------------
void
CObjCHAR::ReleaseRideUser(void) {

    if (m_iRideIDX == 0)
        return;

    CObjAVT* pTarget = g_pObjMGR->Get_CharAVT(m_iRideIDX, false);
    if (pTarget == NULL)
        return;

    m_iRideIDX = 0;

    //펫 모드 설정
    pTarget->SetPetType(-1);
    pTarget->m_pObjCART = NULL;
    pTarget->m_IsRideUser = FALSE;

    pTarget->Set_STATE(CS_STOP);
    pTarget->SetCMD_STOP();

    //아바타 무기 및 날개는 보이도록 설정
    int iVisibilityPart[3] = {BODY_PART_KNAPSACK, BODY_PART_WEAPON_R, BODY_PART_WEAPON_L};

    for (register int i = 0; i < 3; i++) {
        CMODEL<CCharPART>* pCharPART = pTarget->m_pCharMODEL->GetCharPART(iVisibilityPart[i]);
        if (pCharPART) {
            short nI;

            for (nI = 0; nI < pCharPART->m_nPartCNT; nI++) {
                if (pTarget->m_phPartVIS[iVisibilityPart[i]][nI]) {
                    ::setVisibilityRecursive(pTarget->m_phPartVIS[iVisibilityPart[i]][nI], 1.0f);
                }
            }
        }
    }
}

///--------------------------------------------------------------------------------
/// class : CObjCHAR
/// 박지호: ReleaseRideUser()
///       : 카트 보조에 탑승 했다면 자신은 뛰어 내린다.
///
//--------------------------------------------------------------------------------
void
CObjCHAR::ReleaseRideUser(WORD irideIDX) {

    // m_iRideIDX = g_pObjMGR->Get_ClientObjectIndex(irideIDX);
    CObjAVT* pTarget = g_pObjMGR->Get_CharAVT(irideIDX, false);
    if (pTarget == NULL)
        return;

    pTarget->m_pObjCART->UnLinkChild(1);

    //부모의 연결해제
    pTarget->m_pObjCART->GetParent()->m_iRideIDX = 0;
    pTarget->m_pObjCART->GetParent()->m_pRideUser = NULL;
    //펫 모드 설정
    pTarget->SetPetType(-1);
    pTarget->m_pObjCART = NULL;
    pTarget->m_IsRideUser = FALSE;

    pTarget->Set_STATE(CS_STOP);
    // pTarget->SetCMD_STOP();

    //뛰어 내리는 모션을 설정한다.
    pTarget->Set_MOTION(SKILL_ANI_ACTION_TYPE(27), 0, 1, false, 1);

    //아바타 무기 및 날개는 보이도록 설정
    int iVisibilityPart[3] = {BODY_PART_KNAPSACK, BODY_PART_WEAPON_R, BODY_PART_WEAPON_L};

    for (register int i = 0; i < 3; i++) {
        CMODEL<CCharPART>* pCharPART = pTarget->m_pCharMODEL->GetCharPART(iVisibilityPart[i]);
        if (pCharPART) {
            short nI;

            for (nI = 0; nI < pCharPART->m_nPartCNT; nI++) {
                if (pTarget->m_phPartVIS[iVisibilityPart[i]][nI]) {
                    ::setVisibilityRecursive(pTarget->m_phPartVIS[iVisibilityPart[i]][nI], 1.0f);
                }
            }
        }
    }
}

///--------------------------------------------------------------------------------
/// class : CObjCHAR
/// 박지호: Ride_Cansel_Motion()
///       : 탑승 거부 모션을 설정한다.
///
//--------------------------------------------------------------------------------
void
CObjCHAR::Ride_Cansel_Motion(void) {

    this->Set_COMMAND(CMD_STOP);
    this->SetCMD_STOP();

    this->Set_MOTION(453, 0, 1, false, 1);
}

///--------------------------------------------------------------------------------
/// class : CObjCHAR
/// class : Stop_Cart
/// 박지호: 카트를 정지 시킨다.
///
//--------------------------------------------------------------------------------
void
CObjCHAR::Stop_Cart(void) {

    if (!m_pObjCART)
        return;

    m_pObjCART->Set_COMMAND(CMD_STOP);
    m_pObjCART->SetCMD_STOP();
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : queuing the command
///				현재 명령을 적용할수 있는가?
///				현재의 명령을 세팅할수 없는 상태를 파악하라..
//--------------------------------------------------------------------------------

bool
CObjCHAR::CanApplyCommand() {
    /// 캐스팅이 시작되었다면 적용할수 없음..
    /// 또 result_of_skill 을 받았다면.. 결국 현재 시전할 스킬이 끝나기 전에는 모든 명령을
    /// 큐에넣는다. 이미 서버에선 결과가 적용된거기때문에 클라이언트도 무조건 스킬을 시전해야된다.
    if (this->m_bCastingSTART && bCanActionActiveSkill()) {
        return false;
    }

    /// 현재 수행되어야할 명령큐가 비어있지 않다면 먼저 큐의 명령을 수행해야하므로..
    if (this->m_CommandQueue.IsEmpty() == false) {
        return false;
    }

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  : 쌓여진 명령들을 수행한다.
///			  매프레임 수행해야하는가?
//--------------------------------------------------------------------------------

void
CObjCHAR::ProcQueuedCommand() {
    if (m_CommandQueue.IsEmpty())
        return;

    /// 현재 스킬관련 명령이 수행중이거나 수행해야될 스킬이 세팅되어 있다면 기다려라..
    if (this->m_bCastingSTART && bCanActionActiveSkill()) {
        return;
    }

    /// 일단 제일 마지막껏만 수행
    bool bSkillCommand = false;
    CObjCommand* pCommand = m_CommandQueue.PopLastCommand(bSkillCommand);

    if (pCommand) {
        pCommand->Execute(this);

        /// Skill 명령이고 이미 Result 를 받은 명령이라면..
        if (bSkillCommand) {
            if (pCommand->bGetResultOfSkil()) {
                SetEffectedSkillFlag(true);
            }
        }
    }
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandSit() {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_SIT);

    if (pCommand) {
        ((CObjSitCommand*)pCommand)->SetCMD_SIT();

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandStand() {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_STAND);

    if (pCommand) {
        ((CObjStandCommand*)pCommand)->SetCMD_STAND();

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandStop() {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_STOP);

    if (pCommand) {
        ((CObjStopCommand*)pCommand)->SetCMD_STOP();

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandMove(const D3DVECTOR& PosTO, BYTE btRunMODE) {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_MOVE);

    if (pCommand) {
        ((CObjMoveCommand*)pCommand)->SetCMD_MOVE(PosTO, btRunMODE);

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandMove(WORD wSrvDIST, const D3DVECTOR& PosTO, int iServerTarget) {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_MOVE);

    if (pCommand) {
        ((CObjMoveCommand*)pCommand)->SetCMD_MOVE(wSrvDIST, PosTO, iServerTarget);

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandAttack(int iServerTarget, WORD wSrvDIST, const D3DVECTOR& PosTO) {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_ATTACK);

    if (pCommand) {
        ((CObjAttackCommand*)pCommand)->SetCMD_ATTACK(iServerTarget, wSrvDIST, PosTO);

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandDie() {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_DIE);

    if (pCommand) {
        ((CObjDieCommand*)pCommand)->SetCMD_DIE();

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandToggle(BYTE btTYPE) {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_TOGGLE);

    if (pCommand) {
        ((CObjToggleCommand*)pCommand)->SetCMD_TOGGLE(btTYPE);

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandSkill2Self(short nSkillIDX) {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_Skill2SELF);

    if (pCommand) {
        ((CObjSkill2SelfCommand*)pCommand)->SetCMD_Skill2SELF(nSkillIDX);

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandSkill2Obj(WORD wSrvDIST,
    const D3DVECTOR& PosTO,
    int iServerTarget,
    short nSkillIDX) {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_Skill2OBJ);

    if (pCommand) {
        ((CObjSkill2ObjCommand*)pCommand)
            ->SetCMD_Skill2OBJ(wSrvDIST, PosTO, iServerTarget, nSkillIDX);

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

/*override*/ void
CObjCHAR::PushCommandSkill2Pos(const D3DVECTOR& PosGOTO, short nSkillIDX) {
    CObjCommand* pCommand = m_CommandQueue.GetObjCommand(OBJECT_COMMAND_Skill2POS);

    if (pCommand) {
        ((CObjSkill2PosCommand*)pCommand)->SetCMD_Skill2POS(PosGOTO, nSkillIDX);

        m_CommandQueue.PushCommand(pCommand);
    } else
        assert(0 && " GetObjCommand failed ");
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Start vibration
//----------------------------------------------------------------------------------------------------

void
CObjCHAR::StartVibration() {
    m_ObjVibration.StartVibration();
}

//--------------------------------------------------------------------------------
/// class : CObjCHAR
/// @brief  :
//--------------------------------------------------------------------------------

void
CObjCHAR::Set_HP(int iHP) {
    if (iHP == DEAD_HP)
        m_iHP = iHP;
    else {
        if (iHP <= 0)
            m_iHP = 1;
        else
            m_iHP = iHP;
    }
}
void
CObjCHAR::Set_MP(int iMP) {
    m_iMP = iMP;
}

int
CObjCHAR::Add_HP(int iAdd) {
    m_iHP += iAdd;

    if (m_iHP <= 0) {
        LogString(LOG_DEBUG_, "Caution : HP < 0 @CObjCHAR::Add_HP");
        m_iHP = 1;
    }

    if (m_iHP > Get_MaxHP())
        m_iHP = Get_MaxHP();
    return m_iHP;
}

int
CObjCHAR::Sub_HP(int iSub) {
    m_iHP -= iSub;
    if (m_iHP <= 0)
        m_iHP = 1;

    return m_iHP;
}

//----------------------------------------------------------------------------------------------------
/// @brief 캐릭터 위에 이펙트를 붙인다.
//----------------------------------------------------------------------------------------------------
void
CObjCHAR::ShowEffectOnCharByIndex(int iEffectIDX, int iSountIDX, bool bWeatherEffect) {
    if (iEffectIDX) {
        if (this->IsVisible()) {
            CEffect* pEffect = g_pEffectLIST->Add_EffectWithIDX(iEffectIDX, true);
            if (pEffect) {
                pEffect->LinkNODE(this->GetZMODEL());

                pEffect->SetParentCHAR(this);

                if (!bWeatherEffect)
                    this->AddExternalEffect(pEffect);
                else
                    this->AddWeatherEffect(pEffect);

                pEffect->InsertToScene();
            }
        }
    }

    if (iSountIDX) {
        g_pSoundLIST->IDX_PlaySound3D(iSountIDX, Get_CurPOS());
    }
}

void
CObjCHAR::ShowEffectOnCharByHash(int iEffectHash, int iSoundIDX, bool bWeatherEffect) {
    if (iEffectHash) {
        if (this->IsVisible()) {
            CEffect* pEffect = g_pEffectLIST->Add_EFFECT((t_HASHKEY)iEffectHash, true);
            pEffect->LinkNODE(this->GetZMODEL());

            pEffect->SetParentCHAR(this);

            if (!bWeatherEffect)
                this->AddExternalEffect(pEffect);
            else
                this->AddWeatherEffect(pEffect);

            pEffect->InsertToScene();
        }
    }

    if (iSoundIDX) {
        g_pSoundLIST->IDX_PlaySound3D(iSoundIDX, Get_CurPOS());
    }
}

void
CObjCHAR::AddWeatherEffect(CEffect* pEffect) {
    m_WeatherEffectLIST.AllocNAppend(pEffect);
}

void
CObjCHAR::DeleteWeatherEffect() {
    classDLLNODE<CEffect*>* pNode;
    pNode = m_WeatherEffectLIST.GetHeadNode();
    while (pNode) {
        /// 이펙트만 지우고 이펙트의 부모처리는 안한다. 왜냐? 내가 부모니까..
        g_pEffectLIST->Del_EFFECT(pNode->DATA, false);

        m_WeatherEffectLIST.DeleteNFree(pNode);
        pNode = m_WeatherEffectLIST.GetHeadNode();
    }

    m_WeatherEffectLIST.ClearList();
}

void
CObjCHAR::SetClanMark(WORD wMarkBack, WORD wMarkCenter) {
    m_wClanMarkBack = wMarkBack;
    m_wClanMarkCenter = wMarkCenter;
    if (m_ClanMarkUserDefined) {
        m_ClanMarkUserDefined->Release();
        m_ClanMarkUserDefined = NULL;
    }
}

void
CObjCHAR::SetClan(DWORD dwClanID,
    WORD wMarkBack,
    WORD wMarkCenter,
    const char* pszName,
    int iLevel,
    BYTE btClanPos) {
    // assert( pszName && dwClanID );
    if (pszName && dwClanID) {

        m_dwClanID = dwClanID;
        m_wClanMarkBack = wMarkBack;
        m_wClanMarkCenter = wMarkCenter;
        m_iClanLevel = iLevel;
        m_strClanName = pszName;
        m_strClanName.erase(remove(m_strClanName.begin(), m_strClanName.end(), ' '),
            m_strClanName.end());
        m_btClanPos = btClanPos;
    }
}

BYTE
CObjCHAR::GetClanPos() {
    return m_btClanPos;
}

void
CObjCHAR::SetClanPos(BYTE btPos) {
    m_btClanPos = btPos;
}

DWORD
CObjCHAR::GetClanID() {
    return m_dwClanID;
}

WORD
CObjCHAR::GetClanMarkBack() {
    return m_wClanMarkBack;
}

WORD
CObjCHAR::GetClanMarkCenter() {
    return m_wClanMarkCenter;
}

const char*
CObjCHAR::GetClanName() {
    return m_strClanName.c_str();
}

void
CObjCHAR::ResetClan() {
    m_dwClanID = 0;
    m_btClanPos = 0;
    m_wClanMarkBack = 0;
    m_wClanMarkCenter = 0;

    if (m_ClanMarkUserDefined) {
        m_ClanMarkUserDefined->Release();
        m_ClanMarkUserDefined = NULL;
    }
}
void
CObjCHAR::SetClanLevel(int iLevel) {
    m_iClanLevel = iLevel;
}
int
CObjCHAR::GetClanLevel() {
    return m_iClanLevel;
}

void
CObjCHAR::SetUserDefinedClanMark(CClanMarkUserDefined* pUserDefinedClanMark) {
    assert(pUserDefinedClanMark);
    m_ClanMarkUserDefined = pUserDefinedClanMark;
    pUserDefinedClanMark->AddRef();
}

///현재 서버와 클라이언트와 HP양이 틀린경우 그 값을 저장한다.
void
CObjCHAR::SetReviseHP(int hp) {
    m_ReviseHP = hp;
}

void
CObjCHAR::Reconcile_HP(int hp) {
    // Every authoritative HP sync (UpdateStats / GSV_SET_HPnMP) advances the arrival
    // stamp. A damage event queued before this sync now has an older arrival_seq and
    // its hp_after checkpoint is treated as superseded in ApplyPresentedCombatDamage.
    m_dwLastAuthoritativeSyncSeq = NextHPAuthoritySeq();
    SetAuthoritativeHP(hp);
    if (hp >= Get_HP()) {
        m_iPendingCombatHPCorrection = 0;
        ClearPendingAuthoritativeDeath();
        Set_HP(hp);
        return;
    }

    if (hp <= DEAD_HP && g_pAVATAR == this) {
        MarkPendingAuthoritativeDeath("hp reconciliation");
        return;
    }

    DeferCombatHPDriftIfIdle("hp reconciliation");
}

///현재 서버와 클라이언트와 MP양이 틀린경우 그 값을 저장한다.
void
CObjCHAR::SetReviseMP(int mp) {
    m_ReviseMP = mp;
}
//-----------------------------------------------------------------------------
/// @brief 아루아 상태일경우 추가 능력치 계산관련: 2005/7/13 - nAvy
//-----------------------------------------------------------------------------
void
CObjCHAR::Calc_AruaAddAbility() {}

//-----------------------------------------------------------------------------
/// @brief 엔진에 공격속도변경시 이용되는 메쏘드			: 2005/7/13 - nAvy
//-----------------------------------------------------------------------------
float
CObjCHAR::Get_fAttackSPEED() {
    int iR = GetOri_ATKSPEED() + m_EndurancePack.GetStateValue(ING_INC_ATK_SPD)
        - m_EndurancePack.GetStateValue(ING_DEC_ATK_SPD);

    // Goddess effect doesn't stack with other buffs
    auto goddess_effect = m_EndurancePack.get_goddess_effect();
    if (goddess_effect) {
        iR += max(0, goddess_effect->attack_speed - m_EndurancePack.GetStateValue(ING_INC_ATK_SPD));
    }

    return (iR > 30) ? (iR / 100.f) : 0.3f;
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @brief  : 몹 생성 순서를 .. static
//--------------------------------------------------------------------------------

DWORD CObjMOB::m_dwCreateOrder = 0;

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @brief  : constructor
//--------------------------------------------------------------------------------

CObjMOB::CObjMOB() {
    m_nQuestIDX = 0;

    m_iMobAniSkill = MOB_ANI_CASTION01;
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @brief  : destructor
//--------------------------------------------------------------------------------

CObjMOB::~CObjMOB() {
    /*
    if ( m_nQuestIDX ) {
        g_pEventLIST->Del_EVENT (xxx);
    }
    */
    //	LogString (LOG_DEBUG_, "   CObjMOB::~CObjMOB ( charNo:%d, %s ), Obj:%d  \n", m_nCharIdx,
    // Get_NAME(), m_nIndex );
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @param CGameOBJ *pSourOBJ
/// @param short nEventIDX
/// @brief  : 몹에 설정된 이벤트 처리..( NPC의 대화 이벤트 등 )
//--------------------------------------------------------------------------------

bool
CObjMOB::Check_EVENT(CGameOBJ* pSourOBJ, short nEventIDX) {
    CGameOBJ* pGameObj = pSourOBJ;
    /// Pet 라면 부모를 체크
    if (pSourOBJ->IsPET()) {
        pGameObj = (CGameOBJ*)((CObjCART*)pSourOBJ)->GetParent();
    }

    if (pGameObj->IsA(OBJ_USER) && m_nQuestIDX) {
        return g_pEventLIST->Run_EVENT(this->Get_INDEX(), m_nQuestIDX, nEventIDX);
    }

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @param  : CObjCHAR *pAtk
/// @brief  : 사망시 발생 이벤트..
//--------------------------------------------------------------------------------

void
CObjMOB::Do_DeadEvent(CObjCHAR* pAtk) {
    QF_doQuestTrigger(NPC_DESC(m_nCharIdx));
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @param  :
/// @brief  : 몹 생성
//--------------------------------------------------------------------------------

bool
CObjMOB::Create(short nCharIdx, const D3DVECTOR& Position, short nQuestIDX, bool bRunMODE) {
    char* szName = CStr::Printf(NAME_MOB_MODEL, NPC_NAME(nCharIdx), this->m_dwCreateOrder++);

    CCharMODEL* pMODEL = g_MOBandNPC.GetMODEL(nCharIdx);

    m_nCharIdx = nCharIdx;
    m_fScale = NPC_SCALE(nCharIdx) / 100.f;
    if (CObjCHAR::CreateCHAR(szName, pMODEL, pMODEL->GetPartCNT(), Position)) {
        this->m_iHP = NPC_HP(m_nCharIdx);
        this->m_iMaxHP = NPC_HP(m_nCharIdx) * NPC_LEVEL(m_nCharIdx);

        if (this->m_iHP < 0)
            g_pCApp->ErrorBOX("ERROR:: HP <= 0 !!!", (char*)NPC_NAME(nCharIdx));

        this->m_bRunMODE = bRunMODE;
        this->m_fRunAniSPEED = 1.0f;
        //		this->m_fAtkAniSPEED  = (NPC_ATK_SPEED( m_nCharIdx ) / 100.f);

        // this->Adjust_HEIGHT ();

        // EVENT ...
        m_nQuestIDX = nQuestIDX;

        this->New_EFFECT(BODY_PART_WEAPON_R, NPC_R_WEAPON(m_nCharIdx));
        this->New_EFFECT(BODY_PART_WEAPON_L, NPC_L_WEAPON(m_nCharIdx));

        //----------------------------------------------------------------------------------------------------
        /// Glow effect
        //----------------------------------------------------------------------------------------------------
        unsigned int iColor = NPC_GLOW_COLOR(nCharIdx);
        if (iColor) {
            D3DXCOLOR color = CGameUtil::GetRGBFromString(iColor);
            ::setVisibleGlowRecursive(this->GetZMODEL(), 2, color.r, color.g, color.b);
        }

        this->stats.move_speed = NPC_RUN_SPEED(m_nCharIdx);
        this->stats.attack_speed = NPC_ATK_SPEED(m_nCharIdx);

        this->stats.attack_power = NPC_ATK(m_nCharIdx);
        this->stats.hit_rate = NPC_HIT(m_nCharIdx);

        return true;
    }

    LogString(LOG_DEBUG_, "MOB Char create failed .. %d: %s \n", nCharIdx, NPC_NAME(nCharIdx));

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @param  :
/// @brief  : 도망..
//--------------------------------------------------------------------------------

void
CObjMOB::Run_AWAY(int iDistance) {
    D3DVECTOR pos;

    pos.x = RANDOM(iDistance * 2) - iDistance;
    pos.y = RANDOM(iDistance * 2) - iDistance;
    pos.x += m_PosBORN.x;
    pos.y += m_PosBORN.y;

    this->SetCMD_MOVE(pos, true);
    this->Set_COMMAND(CMD_RUNAWAY);
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @param  :
/// @brief  : 캐릭터 변경..( 그래서 모델노드는 안지우나? )
//--------------------------------------------------------------------------------

bool
CObjMOB::Change_CHAR(int nCharIDX) {
    this->DeleteCHAR();

    D3DVECTOR PosBORN = Get_BornPOSITION();

    if (!this->Create(nCharIDX, m_PosCUR, m_nQuestIDX, m_bRunMODE)) {
        return false;
    }

    m_PosBORN = PosBORN;
    return true;
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @param  :
/// @brief  : 구현사항 없음
//--------------------------------------------------------------------------------

bool
CObjMOB::Create_PET(int nCharIDX) {
#ifdef __VIRTUAL_SERVER
    return (g_pObjMGR->Add_MobCHAR(0, nCharIDX, m_PosCUR, 0, true) ? true : false);
#else
    return false;
#endif
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @param  :
/// @brief  : 인공지능 메세지..
//--------------------------------------------------------------------------------

void
CObjMOB::Say_MESSAGE(char* szMessage) {
    // AddMsgToChatWND (CStr::Printf("%s> %s", this->Get_NAME (), szMessage ), g_dwBLACK );
    g_UIMed.AddChatMsg(g_pObjMGR->Get_ServerObjectIndex(this->Get_INDEX()), szMessage, g_dwRED);
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @param  :
/// @brief  : 지속형의 변경수치 적용을 위해서 현재 적용되어있는 능력수치( 패시브 스킬 포함 )
//--------------------------------------------------------------------------------

int
CObjMOB::Get_DefaultAbilityValue(int iType) {
    switch (iType) {
        case AT_SPEED:
        case AT_ATK_SPD:
            return 1;

        case AT_MAX_HP: {
            return this->m_iMaxHP;
        } break;

        case AT_MAX_MP: {
            return this->m_iMaxMP;
        } break;

        default:
            return 1;
    }

    return 1;
}

/// NPC의 경우 STB에 강제로 높이가 들어가 있으면 그 높이를 사용한다.
void
CObjMOB::GetScreenPOS(D3DVECTOR& PosSCR) {
    float fStature = NPC_HEIGHT(this->m_nCharIdx);

    if (fStature != 0) {
        ::worldToScreen(m_PosCUR.x,
            m_PosCUR.y,
            getPositionZ(m_hNodeMODEL) + fStature,
            &PosSCR.x,
            &PosSCR.y,
            &PosSCR.z);
        return;
    }

    // 모델의 좌표에 키를 더한 위치를 이름출력 위치로 설정
    ::worldToScreen(m_PosCUR.x,
        m_PosCUR.y,
        getPositionZ(m_hNodeMODEL) + m_fStature,
        &PosSCR.x,
        &PosSCR.y,
        &PosSCR.z);
}

//--------------------------------------------------------------------------------
/// class : CObjMOB
/// @param  :
/// @brief  : 공격 거리.. 스킬사용중이면 스킬테이블에서 아니면 NPC 테이블이겠지..
//--------------------------------------------------------------------------------

int
CObjMOB::Get_AttackRange() {
    /// 스킬에 공격 거리가 입력되어 있다면 스킬거리 아니면 무기 거리..
    if (this->m_nToDoSkillIDX > 0) {
        if (this->m_nToDoSkillIDX < g_SkillList.Get_SkillCNT()) {
            if (SKILL_DISTANCE(this->m_nToDoSkillIDX)) {
                return SKILL_DISTANCE(this->m_nToDoSkillIDX);
            }
        } else
            assert(0 && "Get_AttackRange Failed[ ToDoSkillIDX is invalid ]");
    }

    /// 스킬에 공격 거리가 입력되어 있다면 스킬거리 아니면 무기 거리..
    // if ( this->m_nToDoSkillIDX && SKILL_DISTANCE( this->m_nToDoSkillIDX ) )
    //{
    //	return SKILL_DISTANCE( this->m_nToDoSkillIDX );
    //}

    // Default attack range...
    return (Def_AttackRange() + (Get_SCALE() * 120));
}

//------------------------------------------------------------------------------------------------
/// 몬스터 정지시 사운드 출력..
//------------------------------------------------------------------------------------------------
void
CObjMOB::PlayStopSound() {
    int iIndex = NPC_NORMAL_EFFECT_SOUND(this->Get_CharNO());
    g_pSoundLIST->IDX_PlaySound3D(iIndex, this->Get_CurPOS());
}

/*override*/ int
CObjMOB::Proc() {
    int iResult = CObjCHAR::Proc();

    //--------------------------------------------------------------------------------
    // 모션 루프가 끝났다.
    //--------------------------------------------------------------------------------
    if (this->Get_COMMAND() == CMD_STOP) {
        if (!m_bFrameING) {
            if (RANDOM(100) < 20)
                PlayStopSound();
        }
    }

    return iResult;
}

//--------------------------------------------------------------------------------
// 05.05.19 icarus:: WOW방식의 퀘스트 노출 시스템 적용을 위해 추가..
CObjNPC::CObjNPC() {
    m_nQuestSignal = -1;
    m_dwQuestSignalTIME = 0;
}

void
CObjNPC::SetEventValue(int iEventValue) {
    m_iEventSTATUS = iEventValue;
}

int
CObjNPC::GetEventValue() {
    return m_iEventSTATUS;
}

// Overhead quest icon refresh: quest packets flip g_pAVATAR->m_bQuestUpdate
// (reset after the char loop in ProcOBJECT), while the periodic tick catches
// conditions with no packet of their own (fetch-item counts, level checks).
#define QUEST_SIGNAL_REFRESH_MS 2000

int
CObjNPC::Proc() {
    if (m_nQuestIDX > 0 && g_pAVATAR) {
        DWORD dwNow = g_GameDATA.GetGameTime();

        if (m_nQuestSignal < 0 || g_pAVATAR->m_bQuestUpdate
            || dwNow - m_dwQuestSignalTIME >= QUEST_SIGNAL_REFRESH_MS) {
            m_nQuestSignal = g_pEventLIST->GetNpcQuestSignal(m_nQuestIDX);
            m_dwQuestSignalTIME = dwNow;
        }
    }

    return CObjMOB::Proc();
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : constructor
//--------------------------------------------------------------------------------

CObjAVT::CObjAVT() {
    //	m_fScale  = 0.5;
    m_dwSitTIME = 0;
    m_bIsFemale = false;
    m_bRunMODE = true;

    m_pCharMODEL = &m_CharMODEL; // Init ..

    m_iExpression = 0;

    m_iDoChangeWeaponR = 0;
    m_iDoChangeWeaponL = 0;
    m_iCon = 1;
    m_iRecoverHP = 1;
    m_iRecoverMP = 1;

    m_iLevel = 1;

    ::ZeroMemory(m_sPartItemIDX, sizeof(tagPartITEM) * MAX_BODY_PART);

    ///
    /// Personal store
    ///
    m_bPersonalStoreMode = false;
    m_pObjPersonalStore = NULL;

    ///
    /// pet
    ///
    ::ZeroMemory(m_sPetPartItemIDX, sizeof(tagPartITEM) * MAX_RIDING_PART);
    // m_iPetType			= 0;
    // m_pObjCART			= NULL;
    m_btWeightRate = 0;

    m_pWeaponJemEffect = 0;
    m_nStamina = 0; ///최소값으로 셋팅
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : destructor
//--------------------------------------------------------------------------------

CObjAVT::~CObjAVT() {
    DeleteCart();

    //--------------------------------------------------------------------------------
    /// 재밍,재련관련 효과 지우기.
    //--------------------------------------------------------------------------------
    DeleteGemmingEffect();
    DeleteGreadEffect();

    if (m_pObjPersonalStore) {
        delete m_pObjPersonalStore;
        m_pObjPersonalStore = NULL;
    }
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 파트 조각 세팅( 세팅만 )
//--------------------------------------------------------------------------------

void
CObjAVT::SetPartITEM(short nPartIdx, short nItemNo) {
    this->New_EFFECT(nPartIdx, nItemNo, false);

    m_sPartItemIDX[nPartIdx].m_nItemNo = nItemNo;

    /// 아이템이 비워지는거라면 모든 데이터 클리어.
    if (nItemNo == 0) {
        m_sPartItemIDX[nPartIdx].m_bHasSocket = false;
        m_sPartItemIDX[nPartIdx].m_cGrade = 0;
        m_sPartItemIDX[nPartIdx].m_nGEM_OP = 0;
    }

    // g_pSoundLIST->IDX_PlaySound( SOUND_EQUIP_ITEM  );
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : ** 엔진 좌표가 넘어와야 한다.
///				아바자 생성
//--------------------------------------------------------------------------------

bool
CObjAVT::Create(const D3DVECTOR& Position, BYTE btCharRACE) {
    m_btRace = btCharRACE;
    m_bIsFemale = btCharRACE & 0x01;
    m_CharMODEL.Set_SEX(m_bIsFemale);

    for (short nItemIDX, nI = 0; nI < MAX_BODY_PART; nI++) {
        nItemIDX = m_sPartItemIDX[nI].m_nItemNo;

        /// 머리카락이라면 모자와 어울리는 머리카락으로 교체.
        if (nI == BODY_PART_HAIR) {
            nItemIDX += HELMET_HAIR_TYPE(m_sPartItemIDX[BODY_PART_HELMET].m_nItemNo);
        }

        /// 얼굴이라면 현재의 표정 얼굴로 교체..
        if (nI == BODY_PART_FACE) {
            nItemIDX += GetCharExpression();
        }

        this->m_CharMODEL.SetPartMODEL(nI, nItemIDX);
        // this->New_EFFECT( nI, nItemIDX );
    }

    D3DVECTOR charPos = Position;
    charPos.z = g_pTerrain->GetHeightTop(Position.x, Position.y);

    if (CObjCHAR::CreateCHAR((char*)m_Name.c_str(), &m_CharMODEL, MAX_BODY_PART, charPos)) {
        this->stats.attack_speed = 1500.f / (WEAPON_ATTACK_SPEED(BODY_PART_WEAPON_R) + 5);

        m_iHP = 100;

        // SetPartITEM에서 생성된 효과 link
        this->Link_EFFECT();

        // 케릭터 신장.
        m_fStature = ::getModelHeight(this->m_hNodeMODEL);

        this->New_EFFECT(BODY_PART_WEAPON_R, this->GetPartITEM(BODY_PART_WEAPON_R));
        this->New_EFFECT(BODY_PART_WEAPON_L, this->GetPartITEM(BODY_PART_WEAPON_L));

        CreateGemmingEffect();
        CreateGradeEffect();

        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 클래스 내부에 설정된 아바타 정보를 이용해서, 업데이트 한다.
//--------------------------------------------------------------------------------

void
CObjAVT::Update(bool bUpdateBONE) {
    // 0. 효과 unlink  :: DeletePARTS에서 삭제되면서 엔진에서 자동으로 unlink ??
    this->Unlink_EFFECT();

    // 1. 지우기
    // addRenderUnit 된것들은 clearRenderUnit ( HNODE hVisible ); 로 삭제
    // loadVisible 된것등은 unloadVisible로 삭제...
    this->DeletePARTS(false);

    //--------------------------------------------------------------------------------
    /// 재밍,재련관련 효과 지우기.
    //--------------------------------------------------------------------------------
    DeleteGemmingEffect();
    DeleteGreadEffect();

    if (bUpdateBONE) {
        m_pCharMODEL->UnlinkBoneEFFECT(m_ppBoneEFFECT);

        this->UnloadModelNODE();
        this->LoadModelNODE((char*)m_Name.c_str());
        if (m_bIsVisible) {
            ::insertToScene(m_hNodeMODEL);
            ::setPosition(this->m_hNodeMODEL, m_PosCUR.x, m_PosCUR.y, m_PosCUR.z);
        }

        m_pCharMODEL->LinkBoneEFFECT(m_hNodeMODEL, m_ppBoneEFFECT);
    }

    // 2. 데이타 설정.
    for (short nItemIDX, nI = 0; nI < MAX_BODY_PART; nI++) {
        nItemIDX = m_sPartItemIDX[nI].m_nItemNo;

        if (nI == BODY_PART_HAIR) {
            nItemIDX += HELMET_HAIR_TYPE(m_sPartItemIDX[BODY_PART_HELMET].m_nItemNo);
        }

        /// 얼굴이라면 현재의 표정 얼굴로 교체..
        if (nI == BODY_PART_FACE) {
            nItemIDX += GetCharExpression();
        }

        this->m_CharMODEL.SetPartMODEL(nI, nItemIDX);
    }

    // 4. 모델 설정.
    this->CreatePARTS((char*)m_Name.c_str());

    this->InsertToScene();

    // 5. 효과 relink
    this->Link_EFFECT();

    //----------------------------------------------------------------------------------------------------
    /// @brief 재밍, 재련관련 이펙트 생성
    //----------------------------------------------------------------------------------------------------

    // 2005. 06. 15 박 지호
    Set_RareITEM_Glow();

    CreateGemmingEffect();
    CreateGradeEffect();

// 6. 무기가 바뀌어 현재 진행중 모션이 틀려 질경우...
#pragma message("TODO:: change motion ..." __FILE__)

    // 케릭터 신장. // 여긴 이상함 검잔상이나 기타 오브젝트의 높이도 구함.. InsertToScene 안으로
    // 옮기자..
    m_fStature = ::getModelHeight(this->m_hNodeMODEL);

    /// 카트를 타고 있다면..
    if (GetPetMode() >= 0) {
        this->UpdatePet();
    }

    /// CheckVisibiliey
    m_EndurancePack.UpdateVisibility();
}

//----------------------------------------------------------------------------------------------------
/// @brief 재밍, 재련관련 이펙트..
//----------------------------------------------------------------------------------------------------
const int iWeaponAttachedEffectDummyNO = 2;
void
CObjAVT::CreateGemmingEffect() {
    //----------------------------------------------------------------------------------------------------
    /// @brief 재밍이나 재련관련.. 붙었다면 효과를 붙여라..
    //----------------------------------------------------------------------------------------------------

    for (int i = BODY_PART_WEAPON_R; i <= BODY_PART_WEAPON_L; i++) {
        int iPartIdx = i;
        int iItemIDX = m_sPartItemIDX[iPartIdx].m_nItemNo;
        if (iItemIDX == 0)
            continue;

        if (m_sPartItemIDX[iPartIdx].m_bHasSocket && m_sPartItemIDX[iPartIdx].m_nGEM_OP > 300) {
            int iEffectIDX = 0;

            iEffectIDX = GEMITEM_ATTACK_EFFECT(m_sPartItemIDX[iPartIdx].m_nGEM_OP);
            if (iEffectIDX) {
                /// List_Effect 에는 4개까지 효과를 박을수 있지만.. 하나만..
                int iStartPointNO = 0;
                switch (iPartIdx) {
                    case BODY_PART_WEAPON_R:
                        iStartPointNO = WEAPON_GEMMING_POSITION(iItemIDX);
                        break;

                    case BODY_PART_WEAPON_L:
                        iStartPointNO = SUBWPN_GEMMING_POSITION(iItemIDX);
                        break;
                }

                for (int i = 0; i < 1; i++) {
                    int iEffect = EFFECT_POINT(iEffectIDX, i);

                    m_pWeaponJemEffect = g_pEffectLIST->Add_EffectWithIDX(iEffect);
                    if (m_pWeaponJemEffect) {
                        CMODEL<CCharPART>* pCharPART =
                            g_DATA.Get_CharPartMODEL(iPartIdx, iItemIDX, this->IsFemale());

                        if (pCharPART && m_phPartVIS[iPartIdx]
                            && iStartPointNO < pCharPART->m_nDummyPointCNT
                            && m_phPartVIS[iPartIdx][pCharPART->m_pDummyPoints[iStartPointNO + i]
                                                         .m_nParent]) {
                            m_pWeaponJemEffect->Rotation(
                                pCharPART->m_pDummyPoints[iStartPointNO + i].m_Rotate);
                            m_pWeaponJemEffect->Transform(
                                pCharPART->m_pDummyPoints[iStartPointNO + i].m_Transform);
                            m_pWeaponJemEffect->LinkNODE(
                                m_phPartVIS[iPartIdx][pCharPART->m_pDummyPoints[iStartPointNO + i]
                                                          .m_nParent]);
                        } else {
                            g_pEffectLIST->Del_EFFECT(m_pWeaponJemEffect);
                        }
                    }
                }

                /*m_pWeaponJemEffect = g_pEffectLIST->Add_EffectWithIDX( iEffectIDX );
                if ( m_pWeaponJemEffect )
                {
                    CMODEL<CCharPART> *pCharPART = g_DATA.Get_CharPartMODEL( iPartIdx, iItemIDX,
                this->IsFemale() );

                    if( iWeaponAttachedEffectDummyNO < pCharPART->m_nDummyPointCNT )
                    {
                        m_pWeaponJemEffect->Transform( pCharPART->m_pDummyPoints[
                iWeaponAttachedEffectDummyNO ].m_Transform ); m_pWeaponJemEffect->Rotation(
                pCharPART->m_pDummyPoints[ iWeaponAttachedEffectDummyNO ].m_Rotate );
                        m_pWeaponJemEffect->LinkNODE( m_phPartVIS[ iPartIdx ][
                pCharPART->m_pDummyPoints[ iWeaponAttachedEffectDummyNO ].m_nParent ] ); }else
                    {
                        g_pEffectLIST->Del_EFFECT( m_pWeaponJemEffect );
                    }
                }*/
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------
/// @brief 재밍, 재련관련 이펙트..
//----------------------------------------------------------------------------------------------------
void
CObjAVT::DeleteGemmingEffect() {
    if (m_pWeaponJemEffect) {
        m_pWeaponJemEffect->UnlinkNODE();

        g_pEffectLIST->Del_EFFECT(m_pWeaponJemEffect);
        m_pWeaponJemEffect = NULL;
    }
}

void
CObjAVT::Set_RareITEM_Glow(void) {
    return;
}

void
CObjAVT::CreateGradeEffect() {
    CMODEL<CCharPART>* pCharPART = NULL;

    for (int i = 0; i < MAX_BODY_PART; i++) {
        int iItemIDX = m_sPartItemIDX[i].m_nItemNo;
        if (iItemIDX == 0)
            continue;

        if (m_sPartItemIDX[i].m_cGrade != 0) {
            //----------------------------------------------------------------------------------------------------
            /// Glow effect
            //----------------------------------------------------------------------------------------------------
            unsigned int iColor = ITEMGRADE_GLOW_COLOR(m_sPartItemIDX[i].m_cGrade);
            if (iColor) {
                D3DXCOLOR color = CGameUtil::GetRGBFromString(iColor);

                /// Skinning 되는 오브젝트가 아니라면..
                if (m_pCharMODEL) {
                    if (m_pCharMODEL->m_RenderUnitPart[i].empty()) {
                        pCharPART = g_DATA.Get_CharPartMODEL(i, iItemIDX, this->IsFemale());
                        if (pCharPART) {
                            for (int j = 0; j < pCharPART->m_nPartCNT; j++) {
                                if (m_phPartVIS[i] && m_phPartVIS[i][j])
                                    ::setVisibleGlow(m_phPartVIS[i][j],
                                        ZZ_GLOW_TEXTURE,
                                        color.r,
                                        color.g,
                                        color.b);
                            }
                        }
                    } else {
                        /// 스키닝 되는 오브젝트일 경우..
                        std::list<int>::iterator begin = m_pCharMODEL->m_RenderUnitPart[i].begin();
                        for (; begin != m_pCharMODEL->m_RenderUnitPart[i].end(); ++begin) {
                            int iRenderUnitIndex = *begin;
                            ::setVisibleRenderUnitGlow(this->GetZMODEL(),
                                iRenderUnitIndex,
                                ZZ_GLOW_TEXTURE,
                                color.r,
                                color.g,
                                color.b);
                            //::setVisibleGlow( this->GetZMODEL(), ZZ_GLOW_SIMPLE, 1.0f, 1.0f, 1.0f
                            //);
                        }
                    }
                }
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------
/// @brief 재밍, 재련관련 이펙트..
//----------------------------------------------------------------------------------------------------
void
CObjAVT::DeleteGreadEffect() {}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 총알 번호를 구한다. 장거리 무기의 경우.. 현재 세팅된 총알에 따라서 판단
//--------------------------------------------------------------------------------

/*override*/ int
CObjAVT::Get_BulletNO() {
    tagITEM sItem;
    sItem.m_cType = ITEM_TYPE_WEAPON;
    sItem.m_nItemNo = Get_R_WEAPON();

    int iBulletNo = 0;
    int iShotType = sItem.GetShotTYPE();

    /// 총알소모 안하는 마법 무기
    if (iShotType >= MAX_SHOT_TYPE) {
        switch (WEAPON_TYPE(sItem.m_nItemNo)) {
            case WEAPON_ITEM_NOT_USE_BULLET: {
                iBulletNo = WEAPON_BULLET_EFFECT(sItem.m_nItemNo);
                return iBulletNo;
            } break;
        }
    }

    if (iShotType < MAX_SHOT_TYPE && m_ShotData[iShotType].m_nItemNo)
        iBulletNo = NATURAL_BULLET_NO(m_ShotData[iShotType].m_nItemNo);

    return iBulletNo;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 현재 액션과 무기에 맞는 모션을 구한다.
//--------------------------------------------------------------------------------

tagMOTION*
CObjAVT::Get_MOTION(short nActionIdx) {
    int iActionIdx = m_ChangeActionMode.GetAdjustedActionIndex(nActionIdx);

    // 오른손 무기 종류에따라...
    short nWeaponTYPE = WEAPON_MOTION_TYPE(this->m_sRWeaponIDX.m_nItemNo);

    short nFileIDX = FILE_MOTION(nWeaponTYPE, iActionIdx);

    if (0 == nFileIDX) {
        // 모션이 없으면 맨손 모션으로 대체..
        nFileIDX = FILE_MOTION(0, nActionIdx);
    }

    tagMOTION* pMOTION = g_MotionFILE.IDX_GetMOTION(nFileIDX, m_bIsFemale);
    if (pMOTION) {
        pMOTION->m_nActionIdx = nActionIdx;
    }
    return pMOTION;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 앉기 서기보드 토글
//--------------------------------------------------------------------------------

bool
CObjAVT::ToggleSitMODE() {
    if (this->Get_COMMAND() == CMD_SIT) {
        this->SetCMD_STAND();
    } else {
        // 클라이언트는 무조건 앉힌다.
        m_dwSitTIME = 0;
        this->SetCMD_SIT();
    }
    ::setRepeatCount(m_hNodeMODEL, 1);

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  : float fAdjRate : 현재 보정된 이동속도의 디폴트 속도에 대한 비율..
/// @brief  : 뛰기 걷기 모드 토글
///				m_bRunMODE 변수의 상태에 따라 속도 계산시 default speed가 틀려진다.
///             이걸 이전에는 함수 내부에서 구현했으나 이제는 밖에서..
//--------------------------------------------------------------------------------

bool
CObjAVT::ToggleRunMODE() {
    if (Get_STATE() == CS_MOVE) {
        this->adjusted_move_speed = this->stats.move_speed;

        this->Set_CurMOTION(this->Get_MOTION(this->GetANI_Move()));
        this->Set_ModelSPEED(this->adjusted_move_speed);

        ::attachMotion(this->m_hNodeMODEL, this->Get_ZMOTION());
        ::setAnimatableSpeed(this->m_hNodeMODEL, (m_bRunMODE) ? this->m_fRunAniSPEED : 1.0f);

        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 현재 펫모드
//--------------------------------------------------------------------------------

int
CObjAVT::GetPetMode() {
    if (m_pObjCART)
        return m_pObjCART->GetCartType();
    return -1;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 현재 펫모드
//--------------------------------------------------------------------------------

bool
CObjAVT::CanAttackPetMode() {
    int iPetMode = this->GetPetMode();
    if (iPetMode > 0) {
        /// Pet mode 일 경우에는..
        /*switch( iPetMode )
        {
            case PET_TYPE_CART01:
                return false;
            case PET_TYPE_CASTLE_GEAR01:
                return true;
        }*/
        //----------------------------------------------------------------------------------------------------
        /// @brief Pet 의 타입이 아니라 공격거리가 있냐 없냐로 공격가능여부 판정..
        //----------------------------------------------------------------------------------------------------

        // Battle carts / castle gears equip their weapon to RIDE_PART_ARMS
        // (slot 4 = m_sWeaponIDX). The legacy non-_GBC branch read slot 3
        // (m_sAbilIDX = ability enhancement), which is never a weapon, so
        // CanAttackPetMode always returned false and the attack command
        // fell through to a plain move.
        if (PAT_ITEM_ATK_RANGE(this->m_sWeaponIDX.m_nItemNo) <= 0)
            return false;
    }

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  :
//--------------------------------------------------------------------------------

void
CObjAVT::SetCMD_PET_MOVE(const D3DVECTOR& PosTO, BYTE btRunMODE) {
    if (m_pObjCART) {
        // Since CObjCART handles movement when in a cart, we need to ensure it
        // uses the correct movement speed.
        m_pObjCART->stats.move_speed = this->stats.move_speed;
        m_pObjCART->SetCMD_MOVE(PosTO, btRunMODE);
    }
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  :
//--------------------------------------------------------------------------------

void
CObjAVT::SetCMD_PET_MOVE(WORD wSrvDIST, const D3DVECTOR& PosTO, int iServerTarget) {
    if (m_pObjCART) {
        // Since CObjCART handles movement when in a cart, we need to ensure it
        // uses the correct movement speed.
        m_pObjCART->stats.move_speed = this->stats.move_speed;
        m_pObjCART->SetCMD_MOVE(wSrvDIST, PosTO, iServerTarget);
    }
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  :
//--------------------------------------------------------------------------------

void
CObjAVT::SetCMD_PET_STOP(void) {
    if (m_pObjCART) {
        m_pObjCART->SetCMD_STOP();
    }
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  :
//--------------------------------------------------------------------------------

void
CObjAVT::SetCMD_PET_ATTACK(int iServerTarget, WORD wSrvDIST, const D3DVECTOR& PosTO) {
    if (m_pObjCART) {
        m_pObjCART->stats.move_speed = this->stats.move_speed;
        m_pObjCART->adjusted_move_speed = this->stats.move_speed;
        m_pObjCART->m_bRunMODE = true;
        m_pObjCART->m_btMoveMODE = MOVE_MODE_DRIVE;
        m_pObjCART->SetCMD_ATTACK(iServerTarget, wSrvDIST, PosTO);
    }
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  :
//--------------------------------------------------------------------------------

void
CObjAVT::RideCartToggle(bool bRide) {
    if (bRide == false) {
        if (GetPetMode() >= 0) {
            /// 내리기
            DeleteCart(true);
        } else {
            /// 타기
            CreateCartFromMyData(true);
        }

    } else {
        /// 현재 카트를 탄상태가 아닐경우에만 타기
        if (GetPetMode() < 0)
            CreateCartFromMyData(true);
    }

    //버프 지움.
    this->m_EndurancePack.ClearStateByDriveCart();
}

//-------------------------------------------------------------------------------------------------
#define CHECK_TIME 1500

int
CObjAVT::Proc() {
    m_dwElapsedTime += m_dwFrameElapsedTime;

    if (GetPetMode() && m_pObjCART && SetCartVA()) {
        m_pObjCART->m_ObjVibration.StartVibration();
        SetCartVA() = FALSE;
    }

    if (GetPetMode() && m_pObjCART)
        m_pObjCART->m_ObjVibration.Proc();

    { m_ChangeActionMode.Proc(); }

    return CObjCHAR::Proc();
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 총알 데이터 설정
//--------------------------------------------------------------------------------

void
CObjAVT::SetShotData(int i, int iItemNo) {
    if (i < 0 || i >= MAX_SHOT_TYPE)
        return;

    m_ShotData[i].m_nItemNo = iItemNo;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 지속형의 변경수치 적용을 위해서 현재 적용되어있는 능력수치( 패시브 스킬 포함 )
//--------------------------------------------------------------------------------

int
CObjAVT::Get_DefaultAbilityValue(int iType) {
    switch (iType) {
        case AT_SPEED: {
            return Rose::GameStaticConfig::DEFAULT_WALK_SPEED;
        } break;

        case AT_ATK_SPD: {
            return this->GetOri_ATKSPEED();
        } break;

        case AT_MAX_HP: {
            return this->m_iMaxHP;
        } break;

        case AT_MAX_MP: {
            return this->m_iMaxMP;
        } break;

        default:
            return 1;
    }

    return 1;
}

/*override*/ int
CObjAVT::GetANI_Stop() {
    if (m_pObjCART)
        return GetANI_Ride();
    return AVT_ANI_STOP1;
}
/*override*/ int
CObjAVT::GetANI_Move() {
    return m_bRunMODE ? AVT_ANI_RUN : AVT_ANI_WALK;
}
/*override*/ int
CObjAVT::GetANI_Attack() {
    return (AVT_ANI_ATTACK + RANDOM(3));
}
/*override*/ int
CObjAVT::GetANI_Die() {
    if (m_bStopDead)
        return AVT_ANI_STOP1;
    return AVT_ANI_DIE;
}
/*override*/ int
CObjAVT::GetANI_Hit() {
    return AVT_ANI_HIT;
}
/*override*/ int
CObjAVT::GetANI_Casting() {
    return SKILL_ANI_CASTING(m_nActiveSkillIDX);
}
/*override*/ int
CObjAVT::GetANI_CastingRepeat() {
    return SKILL_ANI_CASTING_REPEAT(m_nActiveSkillIDX);
} /// 루프동작은 캐스팅동작으로 사용..
/*override*/ int
CObjAVT::GetANI_Skill() {
    return SKILL_ANI_ACTION_TYPE(m_nActiveSkillIDX);
}
/*override*/ int
CObjAVT::GetANI_Sitting() {
    return AVT_ANI_SITTING;
}
/*override*/ int
CObjAVT::GetANI_Standing() {
    return AVT_ANI_STANDUP;
}
/*override*/ int
CObjAVT::GetANI_Sit() {
    return AVT_ANI_SIT;
}
/*override*/ int
CObjAVT::GetANI_Ride() {
    return (m_pObjCART) ? m_pObjCART->GetRideAniPos() : 0;
}
/*override*/ int
CObjAVT::GetANI_PickITEM() {
    return AVT_ANI_PICKITEM;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------
WORD
CObjAVT::GetPetState() {
    if (m_pObjCART)
        return m_pObjCART->Get_STATE();

    return 0;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 현재 내부에 설정된 데이터로 카트 생성
//--------------------------------------------------------------------------------

bool
CObjAVT::CreateCartFromMyData(bool bShowEffect) {
    if (m_sEngineIDX.m_nItemNo && PAT_ITEM_TYPE(m_sEngineIDX.m_nItemNo) == TUNING_PART_BODY_MOUNT) {
        this->SetPetType(PAT_ITEM_PART_TYPE(m_sEngineIDX.m_nItemNo));
    } else if (m_sBodyIDX.m_nItemNo) {
        this->SetPetType(PAT_ITEM_PART_TYPE(m_sBodyIDX.m_nItemNo));
    } else {
        return false;
    }
    
    bool bResult = CreateCart(m_iPetType,
        m_sEngineIDX.m_nItemNo,
        m_sBodyIDX.m_nItemNo,
        m_sLegIDX.m_nItemNo,
        m_sAbilIDX.m_nItemNo,
        m_sWeaponIDX.m_nItemNo);

    int iVisibilityPart[3] = {BODY_PART_KNAPSACK, BODY_PART_WEAPON_R, BODY_PART_WEAPON_L};
    for (int i = 0; i < 3; i++) {
        CMODEL<CCharPART>* pCharPART = m_pCharMODEL->GetCharPART(iVisibilityPart[i]);
        if (pCharPART) {
            short nI;

            for (nI = 0; nI < pCharPART->m_nPartCNT; nI++) {
                if (m_phPartVIS[iVisibilityPart[i]][nI]) {
                    ::setVisibilityRecursive(m_phPartVIS[iVisibilityPart[i]][nI], 0.0f);
                }
            }
        }
    }

    if (bShowEffect) {
        int iEffectNO = PAT_RIDE_EFFECT(m_sBodyIDX.m_nItemNo);
        int iSoundNO = PAT_RIDE_SOUND(m_sBodyIDX.m_nItemNo);

        this->ShowEffectOnCharByIndex(iEffectNO, iSoundNO);
    }

    return bResult;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 카드 생성. 인자로 각 파트 정보를 받는다.
//--------------------------------------------------------------------------------
bool
CObjAVT::CreateCart(unsigned int iPetType,
    int iEnginePart,
    int iBodyPart,
    int iLegPart,
    int iAbilIPart,
    int iWeaponPart) {
    if (iEnginePart == 0) {
        return false;
    }

    switch (iPetType) {
        case PET_TYPE_CART01:
        case PET_TYPE_CASTLE_GEAR01:
            if (iBodyPart == 0 || iLegPart == 0) {
                return false;
            }
            break;
    }
    
    m_ObjVibration.EndVibration();

    m_iPetType = iPetType;

    if (m_pObjCART != NULL) {
        delete m_pObjCART;
        m_pObjCART = NULL;
    }

    m_pObjCART = g_pObjMGR->Add_CartCHAR(iPetType, this, 0);
    if (m_pObjCART == NULL) {
        assert(0 && "Create cart failed");
        return false;
    }
    
    SetPetParts(RIDE_PART_BODY, iEnginePart, false);
    SetPetParts(RIDE_PART_ENGINE, iBodyPart, false);
    SetPetParts(RIDE_PART_LEG, iLegPart, false);
    SetPetParts(RIDE_PART_ABIL, iAbilIPart, false);
    SetPetParts(RIDE_PART_ARMS, iWeaponPart, false);

    if (m_pObjCART->Create(this, m_iPetType, this->Get_CurPOS()) == false) {
        g_pObjMGR->Del_Object(m_pObjCART);
        return false;
    }
    
    D3DXVECTOR3 pos(0.0f, 0.0f, 0.0f);
    ResetCUR_POS(pos);
    ::savePrevPosition(m_hNodeMODEL);

    m_pObjCART->stats.move_speed = this->stats.move_speed;
    m_pObjCART->adjusted_move_speed = this->stats.move_speed;
    m_pObjCART->m_bRunMODE = true;
    m_pObjCART->m_btMoveMODE = MOVE_MODE_DRIVE;

    this->Set_ModelDIR(0.0f);

    SetCMD_STOP();

    this->m_btMoveMODE = MOVE_MODE_DRIVE;
    
    return true;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 카트 제거
//--------------------------------------------------------------------------------

void
CObjAVT::DeleteCart(bool bShowEffect) {
    if (m_pObjCART) {
        float fDir = ::getModelDirection(m_pObjCART->GetZMODEL());
        this->Set_ModelDIR(fDir);

//박지호
#if defined(_GBC)
        //운전자 (2인승 중이라면)
        if (GetRideUserIndex()) {
            // 20050901 홍근 2인승 카트 보조석 탑승자가 내렸을 경우 State를 Normal로 바꿔준다.
            CObjAVT* oSrc =
                g_pObjMGR->Get_ClientCharAVT(g_pObjMGR->Get_ServerObjectIndex(GetRideUserIndex()),
                    true);
            if (oSrc) {
                if (!strcmp(oSrc->Get_NAME(), g_pAVATAR->Get_NAME())) {
                    g_pAVATAR->Set_Block_CartRide(false);
                }
            }

            m_pObjCART->UnLinkChild();

            //실제 오브젝트 매니져 리스트에서 카트 객체를 삭제함.
            if (m_pObjCART) {
                g_pObjMGR->Del_Object(m_pObjCART);
                m_pObjCART = NULL;
            }

            SetCMD_STOP();

            goto CHAR_VISIBLE;
        }
        // 2인승 탑승자
        else if (IsRideUser()) {
            if (m_pObjCART) {
                ReleaseRideUser(m_pObjCART->GetParent()->GetRideUserIndex());

                // 20050901 홍근 2인승 카트 보조석 탑승자가 내렸을 경우 State를 Normal로 바꿔준다.
                if (!strcmp(this->Get_NAME(), g_pAVATAR->Get_NAME())) {
                    g_pAVATAR->Set_Block_CartRide(false);
                }

                return;
            }
        }
        //혼자 타고 있다면...
        else {
            m_pObjCART->UnLinkChild();
        }
#else
        m_pObjCART->UnLinkChild();
#endif

        if (m_pObjCART) {
            g_pObjMGR->Del_Object(m_pObjCART);
            m_pObjCART = NULL;
        }

        SetCMD_STOP();

        this->m_btMoveMODE = this->m_bRunMODE;

        //----------------------------------------------------------------------------------------------------
        /// 카트를 탈때는 무기, 날개는 안보이게
        //----------------------------------------------------------------------------------------------------
        /*if( m_phPartVIS[ BODY_PART_KNAPSACK ] )
            ::setVisibility( *m_phPartVIS[ BODY_PART_KNAPSACK ], 1.0f );
        if( m_phPartVIS[ BODY_PART_WEAPON_R ] )
            ::setVisibility( *m_phPartVIS[ BODY_PART_WEAPON_R ], 1.0f );
        if( m_phPartVIS[ BODY_PART_WEAPON_L ] )
            ::setVisibility( *m_phPartVIS[ BODY_PART_WEAPON_L ], 1.0f );*/
    CHAR_VISIBLE:
        int iVisibilityPart[3] = {BODY_PART_KNAPSACK, BODY_PART_WEAPON_R, BODY_PART_WEAPON_L};
        for (int i = 0; i < 3; i++) {
            CMODEL<CCharPART>* pCharPART = m_pCharMODEL->GetCharPART(iVisibilityPart[i]);
            if (pCharPART) {
                short nI;

                for (nI = 0; nI < pCharPART->m_nPartCNT; nI++) {
                    if (m_phPartVIS[iVisibilityPart[i]][nI]) {
                        ::setVisibilityRecursive(m_phPartVIS[iVisibilityPart[i]][nI], 1.0f);
                    }
                }
            }
        }

        //----------------------------------------------------------------------------------------------------
        /// 내릴때의 효과출력..
        //----------------------------------------------------------------------------------------------------
        if (bShowEffect) {
            int iEffectNO = PAT_GETOFF_EFFECT(m_sBodyIDX.m_nItemNo);
            int iSoundNO = PAT_GETOFF_SOUND(m_sBodyIDX.m_nItemNo);

            this->ShowEffectOnCharByIndex(iEffectNO, iSoundNO);
        }
    }
}

///--------------------------------------------------------------------------------
/// class : CObjCHAR
/// 박지호: Process_JOIN_RIDEUSER()
///       : 운전자가 존 워프시 2인승 탑승자를 해제한다.
///
//--------------------------------------------------------------------------------
void
CObjAVT::Process_JOIN_RIDEUSER(void) {

    if (this->GetPetMode() < 0)
        return;

    DeleteCart(true);
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 카트 파트정보세팅
//--------------------------------------------------------------------------------

void
CObjAVT::SetPetParts(unsigned int iPetPartIDX, unsigned int iItemIDX, bool bJustInfo) {
    if (iPetPartIDX >= MAX_RIDING_PART)
        return;

    m_sPetPartItemIDX[iPetPartIDX].m_nItemNo = iItemIDX;

    if (bJustInfo == false) {
        if (m_pObjCART == NULL)
            return;

        m_pObjCART->SetPetParts(iPetPartIDX, iItemIDX);
    }
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  :
/// @brief  : 카트 파트 변경등 발생시 카트 업데이트
//--------------------------------------------------------------------------------

void
CObjAVT::UpdatePet() {
    if (m_pObjCART) {
        DeleteCart();
        CreateCartFromMyData();
    }
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @param  : 새로운 소지 아이템 무게비율
//--------------------------------------------------------------------------------

void
CObjAVT::SetWeightRate(BYTE btWeightRate) {
    m_btWeightRate = btWeightRate;
}

//--------------------------------------------------------------------------------
/// class : CObjAVT
/// @return : 현재 소지 아이템 비율
//--------------------------------------------------------------------------------

BYTE
CObjAVT::GetWeightRate() {
    return m_btWeightRate;
}

//-------------------------------------------------------------------------------------------
/// @brief 모든 개인상점을 열고 닫는 시작점..
/// 개인상점 전용 모델을 보여주기위해 일련의 작업들을 한다.
//-------------------------------------------------------------------------------------------
void
CObjAVT::SetPersonalStoreTitle(char* strTitle, int iPersonalStoreType) {
    if (strTitle == NULL) {
        if (m_phPartVIS[0] == NULL)
            m_pCharMODEL->ClearRenderUnitParts();

        // loadVisible된것들 삭제.
        for (short nP = 0; nP < MAX_BODY_PART; nP++) {
            if (m_phPartVIS[nP] == NULL)
                CreateSpecificPART((char*)m_Name.c_str(), nP);
        }

        this->CreateGemmingEffect();
        this->CreateGradeEffect();

        m_bPersonalStoreMode = false;

        if (m_pObjPersonalStore) {
            m_pObjPersonalStore->UnlinkVisibleWorld();
            /// m_pObjPersonalStore->RemoveFromScene();
            delete m_pObjPersonalStore;
            m_pObjPersonalStore = NULL;
        }
        return;
    }

    m_pObjPersonalStore = new CObjTREE();

    /// D3DXVECTOR3 vPos = this->Get_CurPOS();
    D3DXVECTOR3 vPos(0.0f, 0.0f, 0.0f);
    const int iPersonalStoreNO = 260;
    if (m_pObjPersonalStore->Create(
            g_DATA.m_ModelFieldITEM.GetMODEL(iPersonalStoreNO + iPersonalStoreType),
            vPos)) {
        //-------------------------------------------------------------------------------------------
        /// 모든 캐릭터 파트들 삭제
        //-------------------------------------------------------------------------------------------
        ::clearRenderUnit(this->m_hNodeMODEL);
        for (short nP = 0; nP < MAX_BODY_PART; nP++) {
            DeleteSpecificPART(nP, m_phPartVIS[nP]);
            // m_pCharMODEL->DeletePART( nP, m_phPartVIS[ nP ] );
            m_phPartVIS[nP] = NULL;
        }

        m_pObjPersonalStore->LinkToModel(this->GetZMODEL());
    } else {
        delete m_pObjPersonalStore;
        m_pObjPersonalStore = NULL;
    }

    m_bPersonalStoreMode = true;
    m_strPersonalStoreTitle = std::string(strTitle);
}

const char*
CObjAVT::GetPersonalStoreTitle() {
    return m_strPersonalStoreTitle.c_str();
}

bool
CObjAVT::IsPersonalStoreMode() {
    return m_bPersonalStoreMode;
}

/// 장비 교체시 장비에 걸려있던 속성 해제..
void
CObjAVT::ClearRWeaponSkillEffect() {
    this->m_EndurancePack.ClearRWeaponSkillEffect();
}

void
CObjAVT::ClearLWeaponSkillEffect() {
    this->m_EndurancePack.ClearLWeaponSkillEffect();
}

//----------------------------------------------------------------------------------------------------
/// @brief 팻모드 상태일때는 팻의 좌표를 리턴하고, 일반적일때는 내 좌표를 리턴함
//----------------------------------------------------------------------------------------------------
const D3DVECTOR&
CObjAVT::GetWorldPos() {
    if (GetPetMode() > 0) {
        if (m_pObjCART) {
            return m_pObjCART->Get_CurPOS();
        }
    }

    return Get_CurPOS();
}

//----------------------------------------------------------------------------------------------------
/// @brief 모델에 등록된 renderUnit외의것들에 대한 충돌판정.. (아바타일경우만)
//----------------------------------------------------------------------------------------------------
bool
CObjAVT::IsIntersectAccessory(float& fCurDistance) {
    /// 개인상점 대표모델
    if (this->m_pObjPersonalStore) {
        return m_pObjPersonalStore->IsIntersectForCamera(fCurDistance);
    }
    return false;
}
//----------------------------------------------------------------------------------------------------
/// @brief 파티멤버의 자동 HP회복을 위한 Stamina 관련 Method
///		   CObjUSER의 경우 CUserDATA::GetCur_STAMINA를 호출한다. 주의할것
//----------------------------------------------------------------------------------------------------
short
CObjAVT::GetStamina() {
    return m_nStamina;
}
void
CObjAVT::SetStamina(short nStamina) {
    m_nStamina = nStamina;
}

//----------------------------------------------------------------------------------------------------
/// @brief m_dwSubFLAG 플래그에 따란 특수한 상태로의 전환
//----------------------------------------------------------------------------------------------------

void
CObjAVT::ChangeSpecialState(DWORD dwSubFLAG) {
    m_dwSubFLAG = dwSubFLAG;

    if (m_dwSubFLAG & FLAG_SUB_HIDE) {
        ::setVisibilityRecursive(this->GetZMODEL(), 0.0f);
    }
    //곽홍근::투명
    else if (!(m_dwSubFLAG & FLAG_SUB_HIDE)) {
        ::setVisibilityRecursive(this->GetZMODEL(), 1.0f);
    }

    if (m_dwSubFLAG & static_cast<uint32_t>(StatusEffectFlag::Goddess)) {
        m_EndurancePack.add_goddess_entity();
    } else {
        m_EndurancePack.remove_goddess_entity();
    }

    if (this->IsA(OBJ_USER)) {
        static_cast<CObjUSER*>(this)->UpdateAbility();
    }
}

//----------------------------------------------------------------------------------------------------
/// @brief 클릭할수 있는 객체인가?
//----------------------------------------------------------------------------------------------------
bool
CObjAVT::CanClickable() {
    if (m_dwSubFLAG & FLAG_SUB_HIDE)
        return false;

    if (::getVisibility(this->GetZMODEL()) < 0.1)
        return false;

    return true;
}

//----------------------------------------------------------------------------------------------------
/// @brief virtual From CObjAI   : 추가  2005/7/13 - nAvy
//----------------------------------------------------------------------------------------------------
int
CObjAVT::GetOri_MaxHP() {
    return m_iMaxHP;
}

///////////////////////////////////////////////////////////////////////////////////////////
// 2005. 6. 20	박 지호
//
// 여신소환 연출 클래스
///////////////////////////////////////////////////////////////////////////////////////////

CGoddessMgr goddessMgr;
//생성자
CGoddessMgr::CGoddessMgr() {

    Init();
}

//소멸자
CGoddessMgr::~CGoddessMgr() {

    //	Release();
}

//초기화
BOOL
CGoddessMgr::Init(void) {

    m_IsUse = FALSE;
    m_IsAvata = FALSE;

    m_dwAvata = 0;
    m_Count = 0;

    return TRUE;
}

//메모리 해제
void
CGoddessMgr::Release(void) {

    //리스트 해제
    //-----------------------------------------------------------------------------------
    GODDESSSTR* pGds = NULL;
    for (GODLIST itr = m_list.begin(); itr != m_list.end(); itr++) {
        pGds = &(itr->second);
        if (pGds == NULL)
            continue;

        Release_List(pGds);
    }

    m_list.clear();
    m_Count = 0;
    //------------------------------------------------------------------------------------

    Init();
}

//하나의 리스트만 삭제한다.
void
CGoddessMgr::Release_List(GODDESSSTR* pGDS) {

    delete pGDS->pGODModel;
    delete pGDS->pSprModel;

    for (register int i = 0; i < 2; i++) {
        delete pGDS->pEffect[i];
        pGDS->pEffect[i] = NULL;
        pGDS->hParticle[i] = NULL;
    }

    pGDS->pGODModel = NULL;
    pGDS->pSprModel = NULL;

    pGDS->hGoddess = NULL;
    pGDS->hSprite = NULL;
}

//허용국가 체크
BOOL
CGoddessMgr::Permission_Country(void) {
    return TRUE;
}

//여신,요정 객체를 로드한다.
BOOL
CGoddessMgr::Register_God_And_Sprite(void) {

    if (m_IsUse)
        return TRUE;

    return (m_IsUse = TRUE);
}

//랜더링 오브젝트 객체를 등록한다.
BOOL
CGoddessMgr::Register_RenderObj(GODDESSSTR& gds) {

    //아바타 객체를 가져온다.
    CObjAVT* pCHAR = g_pObjMGR->Get_ClientCharAVT(gds.idxMaster, TRUE);
    if (pCHAR == NULL)
        return FALSE;

    //아바타 상태 설정
    pCHAR->SetAroaState() = gds.bEnable;

    //
    pCHAR->Calc_AruaAddAbility();

    //자신이라면 아바타 정보를 저장한다.
    m_IsAvata = (lstrcmp(pCHAR->Get_NAME(), g_pAVATAR->Get_NAME()) ? FALSE : TRUE);

    char* ParticlName[] = {"3Ddata\\Effect\\_arua_ghost01.eft",
        "3Ddata\\Effect\\end_effect_02.eft"};

    for (register int i = 0; i < 2; i++) {
        if (gds.pEffect[i] != NULL) {
            delete gds.pEffect[i];
            gds.pEffect[i] = NULL;
        }

        DWORD hKey = CStr::GetHASH(ParticlName[i]);

        gds.pEffect[i] = g_pEffectLIST->Add_EFFECT(hKey);
        if (gds.pEffect[i] == NULL) {
            _ASSERT(gds.pEffect[i]);
            return FALSE;
        }

        gds.pEffect[i]->LinkNODE(pCHAR->GetZMODEL());
        gds.hParticle[i] = gds.pEffect[i]->GetZNODE();
    }

    //여신 로드
    if (gds.pGODModel == NULL) {
        gds.pGODModel = new CObjMOB;
        if (gds.pGODModel == NULL) {
            _ASSERT(gds.pGODModel);
            return FALSE;
        }

        D3DXVECTOR3 cpos = pCHAR->Get_CurPOS();
        if (!gds.pGODModel->Create(939, cpos, 0, TRUE)) {
            LogString(LOG_DEBUG_, "Create Goddess Model Data\n");
            delete gds.pGODModel;
            gds.pGODModel = NULL;
        }

        gds.hGoddess = gds.pGODModel->GetZMODEL();
        ::setRepeatCount(gds.hGoddess, 1);
        ::setAnimatableFrame(gds.hGoddess, 0);
        ::controlAnimatable(gds.hGoddess, 0);
    }

    //요정 로드
    if (gds.pSprModel == NULL) {
        gds.pSprModel = new CObjMOB;
        if (gds.pSprModel == NULL) {
            _ASSERT(gds.pSprModel);
            return FALSE;
        }

        D3DXVECTOR3 cpos = pCHAR->Get_CurPOS();

        if (!gds.pSprModel->Create(940, cpos, 0, FALSE)) {
            LogString(LOG_DEBUG_, "Create Sprite Model Data\n");
            delete gds.pSprModel;
            gds.pSprModel = NULL;
        }

        gds.hSprite = gds.pSprModel->GetZMODEL();
    }

    ::controlAnimatable(gds.hGoddess, 0);
    ::setVisibilityRecursive(gds.hGoddess, 0.0f);
    ::setVisibilityRecursive(gds.hSprite, 0.0f);

    return TRUE;
}

void
CGoddessMgr::Set_GDSData(GODDESSSTR& gds, BOOL bonOff, BOOL bPrograss) {

    if (bPrograss) {
        if (bonOff)
            gds.god_State = GOD_SPRITE_EFFECT;
        else
            gds.god_State = GOD_NONE;
    } else {
        if (bonOff)
            gds.god_State = GOD_APPEAR_PARTCLE;
        else
            gds.god_State = GOD_END_EFFECT;
    }
}

//여신 소환 on/off 를 처리한다.
BOOL
CGoddessMgr::SetProcess(DWORD bonOff, WORD idx, BOOL bPrograss) {

    //국가 코드  체크
    if (!Permission_Country())
        return TRUE;

    GODDESSSTR Gds, *pGds = NULL;

    BOOL bOnOff = bonOff ? 1 : 0;

    // Old list
    //-------------------------------------------------------------------
    GODLIST itr = m_list.find(idx);

    if (itr != m_list.end()) {
        itr->second.bEnable = bOnOff;
        itr->second.sTick = GetTickCount();

        Set_GDSData(itr->second, bOnOff, bPrograss);

        if (!Register_RenderObj(itr->second))
            return FALSE;

        pGds = &(itr->second);

        goto SET_EFF;
    }
    //--------------------------------------------------------------------

    // New list
    //--------------------------------------------------------------------
    //처음에 OFF 라면 리스트를 생성할 필요가 없음.
    if (bOnOff == FALSE)
        return TRUE;

    Gds.Init();

    Gds.bEnable = bOnOff;
    Gds.sTick = GetTickCount();
    Gds.idxMaster = idx;

    Set_GDSData(Gds, bOnOff, bPrograss);

    if (!Register_RenderObj(Gds))
        return FALSE;

    m_list[idx] = Gds;
    m_Count = m_list.size();

    pGds = &Gds;
//--------------------------------------------------------------------

//이펙트 랜더링 세팅
SET_EFF:

    if (m_IsAvata)
        m_dwAvata = bonOff;

    //요정 모드라면 보이게 만든다.
    if (pGds->god_State == GOD_SPRITE_EFFECT) {
        pGds->fviewSpr = 1.0f;
        ::setVisibilityRecursive(pGds->hSprite, 1.0f);
    }

    //파티클 애니메이션만 설정 설정
    if (pGds->god_State == GOD_APPEAR_PARTCLE) {
        pGds->pEffect[0]->StartEffect();
        pGds->fviewGODD = 0.0f;
    } else
        pGds->pEffect[0]->StopEffect();

    if (pGds->god_State == GOD_END_EFFECT) {
        pGds->pEffect[1]->StartEffect();
        pGds->fviewSpr = 1.0f;
    } else
        pGds->pEffect[1]->StopEffect();

    return TRUE;
}

void
CGoddessMgr::Update(void) {

    //국가 코드  체크
    if (!Permission_Country())
        return;

#define TIME_GOD_ACCEPT 9100
#define TIME_APPEAR_GODDESS 3000

    if (m_Count == 0)
        return;

    CObjAVT* pCHAR = NULL;
    DWORD curTick = 0, tempTick = 0;
    D3DXVECTOR3 tPos = D3DXVECTOR3(0, 0, 0);

    BOOL IsBeginList = FALSE;
    GODLIST preNode;

    float fRot[4] = {0};
    int t1 = 0, t2 = 0;

    for (GODLIST itr = m_list.begin(); itr != m_list.end(); ++itr) {

        GODDESSSTR* gds = &(itr->second);
        if (gds == NULL)
            continue;

        //	if(gds->god_State == GOD_NONE)
        //		continue;

        //아바타 객체를 가져온다.
        pCHAR = g_pObjMGR->Get_ClientCharAVT(gds->idxMaster, TRUE);
        if ((pCHAR == NULL) || (gds->god_State == GOD_NONE)) {
            Release_List(&(itr->second));
            if (itr != m_list.begin()) {
                preNode = --itr;
                ++itr;
                IsBeginList = FALSE;
            } else
                IsBeginList = TRUE;

            m_list.erase(itr);

            if (!(m_Count = m_list.size()))
                break;

            //리스트의 구간이 아니라면 Begin 리스트 이다
            if (IsBeginList)
                itr = m_list.begin();
            else
                itr = preNode;

            continue;
        }

        //아바타의 좌표
        D3DXVECTOR3 cPos = tPos = pCHAR->Get_CurPOS();

        //아바타 회전
        ::getRotationQuad(pCHAR->GetZMODEL(), fRot);

        // step1 :  파티클 이펙트 출력
        if (gds->god_State == GOD_APPEAR_PARTCLE) {
            curTick = GetTickCount();
            tempTick = (curTick - gds->sTick);

            //여신 캐릭터 출력 유무
            if (tempTick >= TIME_APPEAR_GODDESS) {
                if (gds->fviewGODD == 0) {
                    ::setRepeatCount(gds->hGoddess, 1);
                    ::setAnimatableFrame(gds->hGoddess, 0);
                    ::controlAnimatable(gds->hGoddess, 1);
                }

                //여신 캐릭터를 나타나게 한다.
                if (ProcessVisible(gds->fviewGODD, 0.0008f) == 1)
                    gds->god_State = GOD_APPEAR_GODDESS;

                ::setRotationQuat(gds->hGoddess, fRot);
                ::setPosition(gds->hGoddess, tPos.x, tPos.y, tPos.z);
                ::setVisibilityRecursive(gds->hGoddess, gds->fviewGODD);
            }
        }

        // step2 : 파티클 & 여신 이펙트 출력
        if (gds->god_State == GOD_APPEAR_GODDESS) {
            curTick = GetTickCount();
            tempTick = (curTick - gds->sTick);

            //일정한 시전이펙트가 끝나면 요정 트래킹모드로 전환한다.
            if (tempTick >= TIME_GOD_ACCEPT) {
                //여신 캐릭터를 나타나게 한다.
                t1 = ProcessVisible(gds->fviewGODD, -0.0009f);
                t2 = ProcessVisible(gds->fviewSpr, 0.001f);

                if ((t1 == 0) && (t2 == 1)) {
                    gds->god_State = GOD_SPRITE_EFFECT;
                    ::controlAnimatable(gds->hGoddess, 0);
                }

                //요정
                ::setRotationQuat(gds->hSprite, fRot);
                ::setPosition(gds->hSprite, tPos.x, tPos.y, tPos.z);
                ::setVisibilityRecursive(gds->hSprite, gds->fviewSpr);
            }

            //여신
            ::setRotationQuat(gds->hGoddess, fRot);
            ::setPosition(gds->hGoddess, tPos.x, tPos.y, tPos.z);
            ::setVisibilityRecursive(gds->hGoddess, gds->fviewGODD);
        }

        // step3 : 요정 트래킹 모드
        if (gds->god_State == GOD_SPRITE_EFFECT) {
            ::setRotationQuat(gds->hSprite, fRot);
            ::setPosition(gds->hSprite, tPos.x, tPos.y, tPos.z);
        }

        // step4 ; 요정을 사라지게 한다.
        if (gds->god_State == GOD_END_EFFECT) {

            if (ProcessVisible(gds->fviewSpr, -0.001f) == 0)
                gds->god_State = GOD_NONE;

            ::setRotationQuat(gds->hSprite, fRot);
            ::setPosition(gds->hSprite, tPos.x, tPos.y, tPos.z);
            ::setVisibilityRecursive(gds->hSprite, gds->fviewSpr);
        }
    }
}

// Visible 처리를 한다.
int
CGoddessMgr::ProcessVisible(float& fv, float fseq) {

    float fdelta = g_GameDATA.GetElapsedFrameTime();

    fv += (fseq * fdelta);

    if (fv > 1.0f) {
        fv = 1.0f;
        return 1;
    }

    if (fv < 0.0f) {
        fv = 0.0f;
        return 0;
    }

    return -1;
}

bool
CObjCHAR::is_pvp_enabled() {
    switch (this->pvp_state) {
        case PvpState::AllExceptClan:
        case PvpState::AllExceptParty:
        case PvpState::All:
            return true;
    }

    return false;
}
