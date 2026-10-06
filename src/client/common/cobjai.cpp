/*
    ** ���� ���� !!!
    common/*.cpp, *.h ���ϵ��� ������ �ҽ��� ����������
    Ŭ���̾�Ʈ�� �����ؼ� ��ġ�� ����� �ȵ� !!!! ***

    $Header: /Client/Common/CObjAI.cpp 234   05-09-30 10:57a Gioend $

    ** 2004/4/28 **
    �������� �ڵ� �и���.. CObjAI �� ���� Ŭ���̾�Ʈ������ �����..
*/
#include "stdAFX.h"

#include "CObjCHAR.h"
#include "OBJECT.h"
#include "..\Network\CNetwork.h"
#include "../GameCommon/Skill.h"
#include "../CommandFilter.h"

enum SKILL_STATE {
    SKILL_NONE_STATE = 0,
    SKILL_CASTING_STATE = 1,
    SKILL_CASTING_LOOP_STATE = 2,
    SKILL_ACTION_STATE = 3,
};

int
Global_GetWorldTIME() {
    return ::Get_WorldTIME();
}

CAI_OBJ*
CObjTARGET::Get_TargetOBJ() {
    return (m_iServerTarget) ? g_pObjMGR->Get_ClientCharOBJ(m_iServerTarget, true) : NULL;
}

CObjAI::CObjAI(): stats({}) {
    m_pCurMOTION = NULL;
    m_iCurMotionFRAME = 0;

    m_wState = 0;
    m_wCommand = 0;

    m_bAttackSTART = false;
    m_iActiveObject = 0;

    m_bCastingSTART = false;
    m_dwRemoteCastWaitSince = 0;
    SetCastingState(false);

    m_bRunMODE = false; // �⺻�� �ȱ� ���...
    m_btMoveMODE = 0;

    m_fRunAniSPEED = 1.0f;
    //	m_fAtkAniSPEED  = 1.0f;
    m_fCurMoveSpeed = 0;

    m_SkillActionState = SKILL_NONE_STATE;

    m_nToDoSkillIDX = 0;
    m_nActiveSkillIDX = 0;
    m_nDoingSkillIDX = 0;

    m_iWaitLoopCnt = 0;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param
/// @brief  : Destructor
//--------------------------------------------------------------------------------

CObjAI::~CObjAI() {}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief 	���� ���� �ð��� �����Ѵ�...
//----------------------------------------------------------------------------------------------------
int
CObjAI::Get_WorldTIME(void) {
    return ::Global_GetWorldTIME();
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param tagMOTION* pMotion
/// @brief  : ���ο� ����� ���� ������� ����, ������ ����.
///
//--------------------------------------------------------------------------------

bool
CObjAI::Set_CurMOTION(tagMOTION* pMotion) {
    m_pCurMOTION = pMotion;
    m_iCurMotionFRAME = 0;

    return (NULL != m_pCurMOTION);
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param tagMOTION* pMotion
/// @brief  : ���� ����� �ٲ۴�.( ���� ����̶� ������ �׳� �н� )
//--------------------------------------------------------------------------------

bool
CObjAI::Chg_CurMOTION(tagMOTION* pMotion) {
    if (pMotion && m_pCurMOTION != pMotion) {
        m_pCurMOTION = pMotion;
        m_iCurMotionFRAME = 0;
        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param CObjCHAR *pTarget Ÿ�� ������Ʈ
/// @brief  : ���ݽ���.
//--------------------------------------------------------------------------------

void
CObjAI::Start_ATTACK(CObjCHAR* pTarget) {
    _ASSERT(pTarget);

//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
#if defined(_GBC)
    //����ȣ::�����϶��� ȸ���� ��Ű�� �ʴ´�.
    if ((GetPetMode() < 0) && pTarget)
        Set_ModelDIR(pTarget->m_PosCUR);
#else
    Set_ModelDIR(pTarget->m_PosCUR);
#endif
    //---------------------------------------------------------------------------------

    /// ���� �����߿� ����� ��ų�� �ִ°� ???
    /*if ( this->Do_SKILL( Get_TargetIDX(), pTarget ) )
        return;*/

    /// �Ϲ� ���ݽ��� ���� ����.
    m_wState = CS_ATTACK;
    m_iActiveObject = g_pObjMGR->Get_ClientObjectIndex(Get_TargetIDX());

    _ASSERT(m_iServerTarget == g_pObjMGR->Get_ServerObjectIndex(m_iActiveObject));

    if (Attack_START(pTarget)) {
        // Repeat count 0 -- Set_MOTION's default -- is the engine's "loop forever",
        // and that loop is what animates swings the server never sent. A remote
        // attacker gets exactly one play per confirmed swing; the local player's own
        // attacks keep the legacy self-looping motion. See CanStartConfirmedSwing().
        const int iAttackRepeatCNT =
            static_cast<CObjCHAR*>(this)->IsLocalAvatarAttacker() ? 0 : 1;
        const float fAttackRate = this->Get_fAttackSPEED();
        this->Set_MOTION(this->GetANI_Attack(), 0, fAttackRate, true, iAttackRepeatCNT);

        // One line per swing start, like "combat swing received". Pairing this with the
        // server's "CombatTrace server combat swing" cadence is what shows whether this
        // client animates an attacker at the rate the server actually swings; before
        // 2026-09-19 observers ran every player at 1.00 (see CObjAVT::Create and
        // CObjCHAR::Get_fAttackSPEED).
        LogString(LOG_DEBUG_,
            "CombatTrace attack motion start: obj %d rate %.2f synced %d repeat %d\n",
            static_cast<CObjCHAR*>(this)->Get_INDEX(),
            fAttackRate,
            (int)this->stats.attack_speed,
            iAttackRepeatCNT);

#if defined(_DEBUG) && !defined(__SERVER)
        if (m_pCurMOTION->m_nActionPointCNT <= 0) {
            char* szMsg = CStr::Printf("%s ���� Ÿ�� ������ ���� �ʿ�!!!", Get_NAME());
            g_pCApp->ErrorBOX(szMsg, "ERROR");
            LogString(LOG_DEBUG_, szMsg);
        }
#endif
    }
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param int iServerTarget Ÿ���� ���� �ε���
/// @param CObjCHAR *pTarget Ÿ�� ������Ʈ
/// @brief  : casting
//--------------------------------------------------------------------------------

char
CObjAI::Do_SKILL(int iServerTarget, CObjCHAR* pTarget) {
    /// ĳ���� �߿��� ��� Ÿ���� �ٶ󺻴�...
    /// Ÿ�� �������� ���� ������.
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
#if defined(_GBC)
    //����ȣ::�����϶��� ȸ���� ��Ű�� �ʴ´�.
    if ((GetPetMode() < 0) && pTarget)
        Set_ModelDIR(pTarget->m_PosCUR);
#else
    if (pTarget)
        Set_ModelDIR(pTarget->m_PosCUR);
#endif
    //---------------------------------------------------------------------------------

    switch (m_SkillActionState) {
        case SKILL_CASTING_STATE:
            /// ���������� ĳ���� ������ �����ߴٸ� ĳ���� ���� �������� ����
            /// 5�� ������ ��¡�� ���� ���� ��.. ^^;
            if (ProcSkillCastingAction(iServerTarget, pTarget) == 5) {
                /// ���� ���͸��� ���� ����� ���� Ŭ����
                g_CommandFilter.SetPrevCommand(NULL);
                /// ĳ���� ������ �ִµ� ������ ������..
            }
            /// ĳ���� ������ �ƿ� ���ų�.. Ȥ�� �����Ͱ� ������.
            m_SkillActionState = SKILL_CASTING_LOOP_STATE;
            return 1;

        case SKILL_CASTING_LOOP_STATE:
            if (ProcSkillCastingLoop(pTarget) == 5) {
                m_SkillActionState = SKILL_ACTION_STATE;
            }
            return 1;

        case SKILL_ACTION_STATE:
            ProcSkillAction(pTarget);
            /// �ѹ� �׼� �����Ŀ��� ��� ��ų���� ����..
            m_SkillActionState = SKILL_NONE_STATE;

            Casting_END();
            return 1;
    }

    Casting_END();

    return 0;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief ��ų�� ĳ���� ������ ó��..
//----------------------------------------------------------------------------------------------------

int
CObjAI::ProcSkillCastingAction(int iServerTarget, CObjCHAR* pTarget) {
    if (m_nToDoSkillIDX) {
        /// ������ ��ų�� �ִ�..
        if (!Casting_START(NULL))
            return 0;
        //----------------------------------------------------------------------------------------------------
        /// ��ų �ֹ� ���� !!!
        /// Casting ���� ���� ����.
        /// ��ų �����߿��� �´� ���۵� ���� �ȵȴ�.
        //----------------------------------------------------------------------------------------------------
        this->m_wState = CS_CASTING;

        m_iActiveObject = g_pObjMGR->Get_ClientObjectIndex(iServerTarget);
        _ASSERT(iServerTarget == g_pObjMGR->Get_ServerObjectIndex(m_iActiveObject));

        m_nActiveSkillIDX = m_nToDoSkillIDX;
        m_nToDoSkillIDX = 0;

        if (this->GetANI_Casting() || this->IsA(OBJ_MOB)) {
            this->Set_MOTION(this->GetANI_Casting(),
                0,
                g_SkillList.Get_CastingAniSPEED(m_nActiveSkillIDX),
                false,
                1);
            return 5;
        } else
            assert(0 && "Invalid Casting animation");
    }

    return 0;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief ĳ���� ���۰� ���� ���ۻ����� ������ ä��� ������ ó���Ѵ�.
//----------------------------------------------------------------------------------------------------

int
CObjAI::ProcSkillCastingLoop(CObjCHAR* pTarget) {
    /// ������ ��ų�� ���� ����� ������ ���� ����Դٸ� �׼����� �ƴ϶�� ����
    if (SKILL_TYPE(m_nActiveSkillIDX) >= SKILL_ACTION_FIRE_BULLET
        || SKILL_TYPE(m_nActiveSkillIDX) == SKILL_ACTION_IMMEDIATE)
    //&& SKILL_TYPE( m_nActiveSkillIDX ) <= SKILL_ACTION_TARGET_STATE_DURATION )
    {

        /*
         *  Ÿ���� ������ �ٷ� ĳ���� �������� ����..
         *	��ų������ ĳ���� ����� �����ϴ� ���װ� �̰Ŷ� ������ �ִ°�?
         *   ��·�� ���� �״��� ���� �״����� ���װ� �ִ� - 04/5/25
         */

        /// Ÿ���� �����Ǿ�߸� �ϴ� ��ų�ε� Ÿ���� ���ٸ�..
        if ((CSkillManager::GetSkillTargetType(m_nActiveSkillIDX) != SKILL_TARGET_NONE)
            && (pTarget == NULL)) {
            SetEffectedSkillFlag(true);
        } else {
            if ((!bCanActionActiveSkill()) || /// �����κ��� ����� ���޾Ұų�
                (SKILL_ANI_CASTING_REPEAT_CNT(m_nActiveSkillIDX)
                    != 0)) /// ĳ���� ���� ������ �����������..
            {
                if ((m_iWaitLoopCnt < 10)) {
                    if (!bCanActionActiveSkill()) //|| ( m_iWaitLoopCnt <
                                                  // SKILL_ANI_CASTING_REPEAT_CNT( m_nActiveSkillIDX
                                                  //) ) )
                    {
                        m_iWaitLoopCnt++;
                        if (this->GetANI_CastingRepeat()) {
                            /// ��ų �ֹ� ���� !!!
                            /// Casting ���� ���� ����.
                            /// ��ų �����߿��� �´� ���۵� ���� �ȵȴ�.
                            this->m_wState = CS_CASTING;
                            this->Set_MOTION(this->GetANI_CastingRepeat(),
                                0,
                                g_SkillList.Get_CastingAniSPEED(m_nActiveSkillIDX),
                                false,
                                1);
                            return 1;
                        }
                    }
                } else {
                    assert(0 && "Not received result of skill");
                }
            }
        }
    }

    m_iWaitLoopCnt = 0;

    return 5;
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief  ĳ���� �Ϸ�� ��ų�� ���� ���� ���...
//----------------------------------------------------------------------------------------------------

int
CObjAI::ProcSkillAction(CObjCHAR* pTarget) {
    if (m_nActiveSkillIDX) {
        if (this->Skill_START(pTarget)) {
            /// ��ų �����߿��� �´� ���۵� ���� �ȵȴ�.
            m_wState = CS_NEXT_STOP2;
            this->Set_MOTION(this->GetANI_Skill(),
                0,
                g_SkillList.Get_ActionAniSPEED(m_nActiveSkillIDX),
                true,
                1);

            SetNewCommandAfterSkill(m_nActiveSkillIDX);

            /// Casting_END �� �̵�.. ��� ��ų�� ������ ���µȴ�.
            m_nDoingSkillIDX = m_nActiveSkillIDX;

            SetEffectedSkillFlag(false);
            // Normally this consumes the start of the cast now playing. A start armed
            // for a cast still queued behind it would be eaten here too, so remote
            // casters log it as a timeline marker for "remote cast abandoned".
            if (bCanStartSkill() && !static_cast<CObjCHAR*>(this)->IsLocalAvatarAttacker()) {
                LogString(LOG_DEBUG_,
                    "CombatTrace skill start consumed by action: caster %d skill %d todo %d\n",
                    this->Get_INDEX(),
                    m_nActiveSkillIDX,
                    m_nToDoSkillIDX);
            }
            SetStartSkill(false);

            return 2;
        }

        /*m_nActiveSkillIDX = 0;
        SetEffectedSkillFlag( false );*/
    }

    return 0;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param
/// @brief  : ��ų������ ���ο� ���� ����
/// @todo ���Ƿ� 17��Ÿ���� ����� ���� �������.
//--------------------------------------------------------------------------------

void
CObjAI::SetNewCommandAfterSkill(int iSkillNO) {

//--------------------------------------------------------------------------
#if defined(_GBC)
    //����ȣ::�����϶� ��ų ������ īƮ�� ������ �������� ó���Ѵ�.
    if (GetPetMode() >= 0) {
        SetNewCommandAfterSkill_PET(iSkillNO);
        return;
    }

#endif
    //--------------------------------------------------------------------------

    switch (SKILL_ACTION_MODE(iSkillNO)) {
        case SA_STOP: {
            m_wCommand = CMD_STOP;
        } break;
        case SA_ATTACK: {
            /// ���ݼ����� �Ǿ�����, Ÿ���� ���ٸ� STOP~
            if (this->Get_TargetOBJ() != NULL) {
                m_wCommand = CMD_ATTACK;

                CObjCHAR* pDestCHAR = (CObjCHAR*)(this->Get_TargetOBJ());
                /// ���ϰ�� PVP���� �ƴ������� �������ݸ����� ����Ѵ�.
                if (this->IsA(OBJ_USER) && pDestCHAR->IsUSER()) {
                    if (!g_pTerrain->is_pvp_zone() || g_pAVATAR->pvp_state == PvpState::NoPvp) {
                        g_pNet->Send_cli_STOP(g_pAVATAR->Get_CurPOS());
                        m_wCommand = CMD_STOP;
                        return;
                    }
                }

            } else
                m_wCommand = CMD_STOP;
        } break;
        case SA_RESTORE: {
            m_wCommand = m_wBeforeCMD;
            m_wBeforeCMD = CMD_STOP;

            /// ���ݸ��� ������ų���� Ÿ�� ����..
            if (m_wCommand == CMD_ATTACK) {
                CObjCHAR* pDestCHAR = (CObjCHAR*)(this->Get_TargetOBJ());

                if (pDestCHAR) {
                    /// ���ϰ�� PVP���� �ƴ������� �������ݸ����� ����Ѵ�.
                    if (this->IsA(OBJ_USER) && pDestCHAR->IsUSER()) {
                        if (!g_pTerrain->is_pvp_zone() || g_pAVATAR->pvp_state == PvpState::NoPvp) {
                            g_pNet->Send_cli_STOP(g_pAVATAR->Get_CurPOS());
                            m_wCommand = CMD_STOP;
                            return;
                        }
                    }
                }
            }
        } break;
    }
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param
/// @brief  : Move vector reset
///				1. Normalize DirVEC, 2. DirVEC * Move Speed
///				���� �߿��� �̵��ӵ� 0�ΰ� ����..
//--------------------------------------------------------------------------------

void
CObjAI::Reset_MoveVEC() {
    tPOINTF DirVEC;

    this->Set_ModelDIR(m_PosGOTO);
    this->Set_ModelSPEED(m_fCurMoveSpeed);
    DirVEC.x = m_PosGOTO.x - m_PosCUR.x;
    DirVEC.y = m_PosGOTO.y - m_PosCUR.y;

    float fLength = DirVEC.Length() * 1000.f;
    if (m_fCurMoveSpeed > 0 && fLength != 0.0f) {
        m_MoveVEC = (DirVEC * m_fCurMoveSpeed) / fLength; // Normalized dirvec * move speed
        _ASSERT(m_MoveVEC.x != 0 || m_MoveVEC.y != 0);
    } else {
        // �̵��� �ʿ䰡 ���µ�...
        m_MoveVEC.x = 0;
        m_MoveVEC.y = 0;
    }
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param float fSpeed �̵��ӵ�
/// @brief  :  Start move
///				���� �̵����̸� �Ÿ� ����ϰ�, MoveVEC �� ����.
///				�̵����� �ƴ϶�� ��� ���� �� �̵� ���� ���� ����.
//--------------------------------------------------------------------------------

void
CObjAI::Start_MOVE(float fSpeed) {

    m_fCurMoveSpeed = fSpeed;
    m_PosMoveSTART = m_PosCUR;

    m_iMoveDistance =
        CD3DUtil::distance((int)m_PosCUR.x, (int)m_PosCUR.y, (int)m_PosGOTO.x, (int)m_PosGOTO.y);
    if (m_iMoveDistance <= 0) {
        m_wState = CS_STOP;
        this->Set_MOTION(this->GetANI_Stop());
        return;
    }

    this->Reset_MoveVEC();
    if (Get_STATE() != CS_MOVE) {
        // �̵����� �ƴϴ�.
        m_wState = CS_MOVE;
        this->Set_MOTION(this->GetANI_Move(), m_fCurMoveSpeed, this->Get_MoveAniSPEED());

        Reset_Position(); // �̵� ���� ���� ����. �������� ���� ��ǥ�� ���⿡�� �����.
    }

    this->MoveStart();
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param t_POSITION &PosGOTO �̵� ������
/// @brief  : Restart move
///			������ǥ�� �ٲ��� �ʾҴٸ� �״�� ��.
///			������ǥ�� �����Ǿ��ٸ�, �ٽ� ��ŸƮ.
//--------------------------------------------------------------------------------

void
CObjAI::Restart_MOVE(t_POSITION& PosGOTO) {
    if (Get_STATE() == CS_MOVE && (abs(PosGOTO.x - m_PosGOTO.x) < 0.0001)
        && (abs(PosGOTO.x - m_PosGOTO.x) < 0.0001)) {
        // ������ ��ǥ�� �ٲ��� �ʾҴ�.
        return;
    }

    m_PosGOTO = PosGOTO;

    this->Start_MOVE(this->adjusted_move_speed);
}

void
CObjAI::Restart_MOVE_AL(t_POSITION& PosGOTO) {

    m_PosGOTO = PosGOTO;

    this->Start_MOVE(this->adjusted_move_speed);
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param
/// @brief  : �ɱ� ���� ����
//--------------------------------------------------------------------------------

void
CObjAI::SetCMD_SIT(void) {
    if (this->IsA(OBJ_USER)) {
        CObjSitCommand* pObjCommand =
            (CObjSitCommand*)g_CommandFilter.GetCommandObject(OBJECT_COMMAND_SIT);
        pObjCommand->SetCMD_SIT();

        g_CommandFilter.SetPrevCommand(pObjCommand);
    }

    /// ���� ������ ���� �ֳ�?
    if (this->CanApplyCommand() == false) {
        this->PushCommandSit();
        return;
    }

    m_wState = CS_SITTING;
    m_wCommand = CMD_SIT;
    m_fCurMoveSpeed = 0;

    this->Set_MOTION(this->GetANI_Sitting());
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param
/// @brief  : ���� ���� ����
//--------------------------------------------------------------------------------

void
CObjAI::SetCMD_STAND(void) {
    if (this->IsA(OBJ_USER)) {
        CObjStandCommand* pObjCommand =
            (CObjStandCommand*)g_CommandFilter.GetCommandObject(OBJECT_COMMAND_STAND);
        pObjCommand->SetCMD_STAND();

        g_CommandFilter.SetPrevCommand(pObjCommand);
    }

    /// ���� ������ ���� �ֳ�?
    if (this->CanApplyCommand() == false) {
        this->PushCommandStand();
        return;
    }

    m_wState = CS_STANDING;
    m_wCommand = CMD_STOP;
    m_fCurMoveSpeed = 0;

    this->Set_MOTION(this->GetANI_Standing());
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param
/// @brief  : Stop command ����.
///				I must set "m_iTargetObject" to 0??? if My state is STOP
//--------------------------------------------------------------------------------

bool
CObjAI::SetCMD_STOP(void) {
    if (this->IsA(OBJ_USER)) {
        CObjStopCommand* pObjCommand =
            (CObjStopCommand*)g_CommandFilter.GetCommandObject(OBJECT_COMMAND_STOP);
        pObjCommand->SetCMD_STOP();

        g_CommandFilter.SetPrevCommand(pObjCommand);
    }

    /// ���� ������ ���� �ֳ�?
    if (this->CanApplyCommand() == false) {
        this->PushCommandStop();
        return false;
    }

    /// ĳ������.. ��ų��ҳ� ��Ÿ ������ ���ؼ� STOP������ �����ɰ�쿡�� ĳ������ �����Ѵ�.
    this->Casting_END();

//------------------------------------------------------------------------------------
#if defined(_GBC)
    //����ȣ::īƮ ��ų ��� �� �ƹ�Ÿ ������ ���ؼ�...
    /// If this char is under pet mode, pass the command to pet.
    if (this->GetPetMode() >= 0) {
        SetCMD_PET_STOP();
        // return;
    }
#endif
    //------------------------------------------------------------------------------------
    m_wState = CS_STOP;
    m_wCommand = CMD_STOP;
    m_fCurMoveSpeed = 0;

    this->Set_MOTION(this->GetANI_Stop());

    ChangeActionMode(AVATAR_NORMAL_MODE);

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param float fPosX �̵������� X ��ǥ
/// @param float fPosY �̵������� Y ��ǥ
/// @param BYTE btRunMODE
/// @brief  : Move command ����.
//--------------------------------------------------------------------------------

bool
CObjAI::SetCMD_MOVE2D(float fPosX, float fPosY, BYTE btRunMODE) {
    D3DVECTOR PosTO;
    PosTO.x = fPosX;
    PosTO.y = fPosY;
    PosTO.z = this->m_PosCUR.z;

    CObjAI::SetCMD_MOVE(PosTO, btRunMODE);

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param const D3DVECTOR& PosTO �̵������� ��ǥ
/// @param BYTE btRunMODE
/// @brief  : Move command ����.
///				SetCMD_MOVE (tPOINTF &PosCUR, tPOINTF &PosTO, int iTargetObjIDX) ȣ��
//--------------------------------------------------------------------------------

bool
CObjAI::SetCMD_MOVE(const D3DVECTOR& PosTO, BYTE btRunMODE) {
    if (this->IsA(OBJ_USER)) {
        CObjMoveCommand* pObjCommand =
            (CObjMoveCommand*)g_CommandFilter.GetCommandObject(OBJECT_COMMAND_MOVE);
        pObjCommand->SetCMD_MOVE(PosTO, btRunMODE);

        g_CommandFilter.SetPrevCommand(pObjCommand);
    }

    /// ���� ������ ���� �ֳ�?
    if (this->CanApplyCommand() == false) {
        this->PushCommandMove(PosTO, btRunMODE);
        return false;
    }

    /// If this char is under pet mode, pass the command to pet.
    if (GetPetMode() >= 0) {
        SetCMD_PET_MOVE(PosTO, btRunMODE);
        return false;
    }

    this->m_bRunMODE = (btRunMODE != 0);

    WORD wServeDist = CD3DUtil::distance((int)this->m_PosCUR.x,
        (int)this->m_PosCUR.y,
        (int)PosTO.x,
        (int)PosTO.y);

    if (this->IsPET()) {
        LogString(LOG_NORMAL, "CMD_MOVE:[ %f, %f ]\n", PosTO.x, PosTO.y);
    }

    CObjAI::SetCMD_MOVE(wServeDist, PosTO, 0);

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param tPOINTF &PosFROM �̵� �����
/// @param tPOINTF &PosTO   �̵� ������
/// @param int iServerTarget Ÿ���� ���� �ε���
/// @brief  : Move command ����.
//--------------------------------------------------------------------------------

void
CObjAI::SetCMD_MOVE(WORD wSrvDIST, const D3DVECTOR& PosTO, int iServerTarget) {
    if (this->IsA(OBJ_USER)) {
        CObjMoveCommand* pObjCommand =
            (CObjMoveCommand*)g_CommandFilter.GetCommandObject(OBJECT_COMMAND_MOVE);
        pObjCommand->SetCMD_MOVE(wSrvDIST, PosTO, iServerTarget);

        g_CommandFilter.SetPrevCommand(pObjCommand);
    }

    /// ���� ������ ���� �ֳ�?
    if (this->CanApplyCommand() == false) {
        this->PushCommandMove(wSrvDIST, PosTO, iServerTarget);
        return;
    }

    /// If this char is under pet mode, pass the command to pet.
    if (GetPetMode() >= 0) {
        SetCMD_PET_MOVE(wSrvDIST, PosTO, iServerTarget);
        return;
    }

    // TODO:: adjust current position ..
    // m_PosCUR = PosCUR;
    m_wCommand = CMD_MOVE;

    CGameOBJ* pDestOBJ = g_pObjMGR->Get_ClientOBJECT(iServerTarget);
    /// Ÿ�� ������Ʈ�� �ִٸ�. Ÿ�� ������Ʈ�� �̵�.
    if (pDestOBJ) {
        /// ����� �������̶�� CMD_PICK_ITEM ����.
        if (pDestOBJ->Get_TYPE() == OBJ_ITEM)
            m_wCommand = CMD_PICK_ITEM;

        m_PosGOTO = pDestOBJ->Get_CurPOS();
        this->Set_TargetIDX(iServerTarget);
    } else {
        // m_pRecvPacket->m_gsv_MOUSECMD.m_PosCUR;
        m_PosGOTO = PosTO;
        this->Set_TargetIDX(0);
    }

    // Ŭ���̾�Ʈ������ Ÿ�� ����..
    m_iServerTarget = iServerTarget;
    this->Adj_MoveSPEED(wSrvDIST, PosTO);

    if (CS_BIT_INT & m_wState) {
        m_wState = CS_NEXT_STOP;
    } else {
        this->Start_MOVE(this->adjusted_move_speed);
    }
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param int iServerTarget Ÿ���� ���� �ε���
/// @brief  : Attack command ����.
///				Ÿ���� ���� ����?( �׾���? )
//--------------------------------------------------------------------------------

bool
CObjAI::SetCMD_ATTACK(int iServerTarget) {

    /// ���� ������ ���� �ֳ�?
    /// �Ʒ� �κе� CObjCHAR::SetCMD_Attack( .., .., .. ) �� ����.
    /*if( this->CanApplyCommand() == false )
    {
        this->PushCommandAttack( iServerTarget );
        return false;
    }*/

    /// SetCMD_ATTACK �� CObjAI �� �ƴ϶� ���� ������
    /// CObjCHAR �� SetCMD_ATTACK( .., .., .. );  �� �޴´�.

    this->Casting_END();

    if (CS_BIT_INT & this->m_wState)
        m_wState = CS_NEXT_STOP;
    else
        m_wState = CS_STOP;

    CObjCHAR* pDestCHAR = g_pObjMGR->Get_ClientCharOBJ(iServerTarget, true);
    if (pDestCHAR) {
        m_wCommand = CMD_ATTACK;
        m_PosGOTO = pDestCHAR->m_PosCUR;

        this->Set_TargetIDX(iServerTarget);

        /// ���ϰ�� PVP���� �ƴ������� �������ݸ����� ����Ѵ�.
        if (this->IsA(OBJ_USER) && pDestCHAR->IsUSER()) {
            if (!g_pTerrain->is_pvp_zone() || g_pAVATAR->pvp_state == PvpState::NoPvp) {
                g_pNet->Send_cli_STOP(g_pAVATAR->Get_CurPOS());
                m_wCommand = CMD_STOP;
                return false;
            }
        }

    } else {
        /// Ŭ���̾�Ʈ������ Ÿ���� ��� ���� ���ɰ� ���� Ÿ���� �����Ѵ�.
        m_wCommand = CMD_ATTACK;
        m_iServerTarget = iServerTarget;
    }

    ChangeActionMode(AVATAR_ATTACK_MODE);

    /// ���¿� ��ȭ�� �����.
    return true;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param
/// @brief  : Die command ����.
///			���� ������ ���� �ֳ�?
///			�״°� �ٷ� �����غ���.( Command ť�� ���� ���� ) ( 04/4/28 )
//--------------------------------------------------------------------------------

void
CObjAI::SetCMD_DIE() {
    if (this->IsA(OBJ_USER)) {
        g_CommandFilter.SetPrevCommand(NULL);
    }

    /// ���� ������ ���� �ֳ�?
    /// �״°� �ٷ� �����غ���
    /*if( this->CanApplyCommand() == false )
    {
        this->PushCommandDie( );
        return;
    }*/

    this->Set_TargetIDX(0);

    m_wState = CS_DIE;
    m_wCommand = CMD_DIE;
    this->Set_MOTION(this->GetANI_Die());

    this->Casting_END();
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param BYTE btTYPE ���� ��� Ÿ��
/// @brief  : ���� ���( �ٱ�, �ȱ� ���.. �ɱ� ���� ��� )
//--------------------------------------------------------------------------------

bool
CObjAI::SetCMD_TOGGLE(BYTE btTYPE) {
    if (this->IsA(OBJ_USER)) {
        g_CommandFilter.SetPrevCommand(NULL);
    }

    /// ���� ������ ���� �ֳ�?
    if (this->CanApplyCommand() == false) {
        this->PushCommandToggle(btTYPE);
        return false;
    }

    /// Move Mode �� ��ȭ�� �ִ�..
    if (btTYPE >= TOGGLE_TYPE_DRIVE) {
        btTYPE -= TOGGLE_TYPE_DRIVE;
        this->m_btMoveMODE = btTYPE;

        switch (btTYPE) {
            case MOVE_MODE_WALK: {
                if (this->GetPetMode() >= 0) {
                    this->RideCartToggle(false);
                }

                if (!(Get_STATE() == CS_SIT || Get_STATE() == CS_SITTING)) {
                    this->m_bRunMODE = false;
                    this->ToggleRunMODE();
                }
            } break;
            case MOVE_MODE_RUN: {
                if (this->GetPetMode() >= 0) {
                    this->RideCartToggle(false);
                }

                if (!(Get_STATE() == CS_SIT || Get_STATE() == CS_SITTING)) {
                    this->m_bRunMODE = true;
                    this->ToggleRunMODE();
                }
            } break;
            case MOVE_MODE_DRIVE: {
                this->RideCartToggle(true);
            } break;
        }

    } else {
        switch (btTYPE) {
                /*case TOGGLE_TYPE_RUN :
                    this->ToggleRunMODE ();
                    break;*/

            case TOGGLE_TYPE_SIT:
                if (!this->ToggleSitMODE()) {
                    return false;
                }
                break;
                /*case TOGGLE_TYPE_DRIVE:
                    {
        #ifndef __SERVER
                        this->RideCartToggle();
        #endif
                    }
                    break;*/
        }
    }

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @brief  : ��ų �����̸� �����Ѵ�.
///
//--------------------------------------------------------------------------------
void
CObjAI::SetSkillDelay(int iSkillIdx) {
    //--------------------------------------------------------------------------------
    /// �����ϰ��� Ÿ�̸Ӹ� �����Ѵ�.
    //--------------------------------------------------------------------------------
    if (this->IsA(OBJ_USER)) {
        /// Ÿ�̸� ����
        CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
        CSkill* pSkill = pSkillSlot->GetSkillBySkillIDX(iSkillIdx);
        if (pSkill) {
            pSkill->SetSkillDelayTime(SKILL_RELOAD_TIME(iSkillIdx) * 200);
        }
    }
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param short nSkillIDX ��ų�ε���
/// @brief  : SetCMD_Skill2SELF
///				4/28 �ɷ�ġ �Ҹ� Result_Of_skill ���� ó���ϰ� �ű�
//--------------------------------------------------------------------------------

void
CObjAI::SetCMD_Skill2SELF(short nSkillIDX) {
    /// 2005/7/25 CObjUSER�� �̵� : nAvy
    // if( this->IsA( OBJ_USER ) )
    //{
    //	CObjSkill2SelfCommand* pObjCommand =
    //(CObjSkill2SelfCommand*)g_CommandFilter.GetCommandObject( OBJECT_COMMAND_Skill2SELF );
    //	pObjCommand->SetCMD_Skill2SELF( nSkillIDX );

    //	g_CommandFilter.SetPrevCommand( pObjCommand );
    //}

    /// ���� ������ ���� �ֳ�?
    if (this->CanApplyCommand() == false) {
        this->PushCommandSkill2Self(nSkillIDX);
        return;
    }

    // This command is about to replace the attack motion. If a confirmed swing is
    // still owed its hit frame, the server has already applied it: present it now
    // instead of letting the Proc() sweep fold it away silently. The local avatar
    // (and its mount) keep the old behaviour -- their skill flow starts at click
    // time, before any server confirmation.
    // CObjCHAR is the only CObjAI in the client; cast rather than widen the base
    // vtable, which would force a clean rebuild of every character object.
    if (!static_cast<CObjCHAR*>(this)->IsLocalAvatarAttacker()) {
        // Prefer holding the cast behind the owed swing so the swing plays out
        // with its animation; the queue pops it after the hit frame (or after the
        // hold expires), and GSV_SKILL_START validates it meanwhile. Only if the
        // swing is past the hold window is it presented at once instead.
        if (static_cast<CObjCHAR*>(this)->OwesConfirmedSwingHitFrame(g_GameDATA.GetGameTime())) {
            LogString(LOG_DEBUG_,
                "CombatTrace skill command held for owed swing: attacker %d event %u age %u reason %s\n",
                this->Get_INDEX(),
                static_cast<CObjCHAR*>(this)->GetPendingCombatSwingEventId(),
                (unsigned int)(g_GameDATA.GetGameTime() - static_cast<CObjCHAR*>(this)->GetPendingCombatSwingTime()),
                "self skill command");
            this->PushCommandSkill2Self(nSkillIDX);
        return;
        }
        static_cast<CObjCHAR*>(this)->PresentPreemptedCombatSwing("self skill command");
    }

    SetEffectedSkillFlag(false);

    if (CS_BIT_INT & this->m_wState)
        m_wState = CS_NEXT_STOP;
    else
        m_wState = CS_STOP;

    m_nToDoSkillIDX = nSkillIDX;

    SetSkillDelay(m_nToDoSkillIDX);

    //----------------------------------------------------------------------------------------------------
    /// @brief ������ ���� ����
    //----------------------------------------------------------------------------------------------------
    switch (m_wCommand) {
        case CMD_SKILL2SELF:
        case CMD_SKILL2OBJ:
        case CMD_SKILL2POS:
            m_wBeforeCMD = CMD_STOP;
            break;
        default:
            m_wBeforeCMD = m_wCommand;
            break;
    }

    m_wCommand = CMD_SKILL2SELF;

    /// �ϴ� ���⼭ ����..�Ҹ�ġ
    /// ������ �����Ҷ� �����?
    /// CSkillManager::UpdateUseProperty( this, nSkillIDX );

    //-----------------------------------------------------------------------------------------
    /// ó�� ���ۻ��´� ĳ���� ���º��� ����..
    //-----------------------------------------------------------------------------------------
    m_SkillActionState = SKILL_CASTING_STATE;
    static_cast<CObjCHAR*>(this)->ResetOwedSwingBeforeCast();
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param int iServerTarget Ÿ���� ���� �ε���
/// @param short nSkillIDX	 ��ų �ε���
/// @brief  : ������ƮŸ�� ��ų
//--------------------------------------------------------------------------------
bool
CObjAI::SetCMD_Skill2OBJ(WORD wSrvDIST,
    const D3DVECTOR& PosTO,
    int iServerTarget,
    short nSkillIDX) {
    /// 2005/7/25 CObjUSER�� �̵� : nAvy
    // if( this->IsA( OBJ_USER ) )
    //{
    //	CObjSkill2ObjCommand* pObjCommand = (CObjSkill2ObjCommand*)g_CommandFilter.GetCommandObject(
    // OBJECT_COMMAND_Skill2OBJ ); 	pObjCommand->SetCMD_Skill2OBJ( wSrvDIST, PosTO, iServerTarget,
    // nSkillIDX );

    //	g_CommandFilter.SetPrevCommand( pObjCommand );
    //}

    //-----------------------------------------------------------------------------------------
    /// ���� ������ ���� �ֳ�?
    //-----------------------------------------------------------------------------------------
    if (this->CanApplyCommand() == false) {
        this->PushCommandSkill2Obj(wSrvDIST, PosTO, iServerTarget, nSkillIDX);
        return false;
    }

    // This command is about to replace the attack motion. If a confirmed swing is
    // still owed its hit frame, the server has already applied it: present it now
    // instead of letting the Proc() sweep fold it away silently. The local avatar
    // (and its mount) keep the old behaviour -- their skill flow starts at click
    // time, before any server confirmation.
    // CObjCHAR is the only CObjAI in the client; cast rather than widen the base
    // vtable, which would force a clean rebuild of every character object.
    if (!static_cast<CObjCHAR*>(this)->IsLocalAvatarAttacker()) {
        // Prefer holding the cast behind the owed swing so the swing plays out
        // with its animation; the queue pops it after the hit frame (or after the
        // hold expires), and GSV_SKILL_START validates it meanwhile. Only if the
        // swing is past the hold window is it presented at once instead.
        if (static_cast<CObjCHAR*>(this)->OwesConfirmedSwingHitFrame(g_GameDATA.GetGameTime())) {
            LogString(LOG_DEBUG_,
                "CombatTrace skill command held for owed swing: attacker %d event %u age %u reason %s\n",
                this->Get_INDEX(),
                static_cast<CObjCHAR*>(this)->GetPendingCombatSwingEventId(),
                (unsigned int)(g_GameDATA.GetGameTime() - static_cast<CObjCHAR*>(this)->GetPendingCombatSwingTime()),
                "target skill command");
            this->PushCommandSkill2Obj(wSrvDIST, PosTO, iServerTarget, nSkillIDX);
        return false;
        }
        static_cast<CObjCHAR*>(this)->PresentPreemptedCombatSwing("target skill command");
    }

    SetEffectedSkillFlag(false);

    //-----------------------------------------------------------------------------------------
    /// �ϴ� ���⼭ ����..�Ҹ�ġ
    /// ������ �����Ҷ� �����?

    /// 04/4/27 ��ų ó�� ����� �޾����� ó��...
    // CSkillManager::UpdateUseProperty( this, nSkillIDX );
    //-----------------------------------------------------------------------------------------

    if (CS_BIT_INT & this->m_wState)
        m_wState = CS_NEXT_STOP;
    // else
    //	m_wState = CS_STOP;

    //----------------------------------------------------------------------------------------------------
    /// @brief ������ ���� ����
    //----------------------------------------------------------------------------------------------------
    switch (m_wCommand) {
        case CMD_SKILL2SELF:
        case CMD_SKILL2OBJ:
        case CMD_SKILL2POS:
            m_wBeforeCMD = CMD_STOP;
            break;
        default:
            m_wBeforeCMD = m_wCommand;
            break;
    }

    /// CObjCHAR *pDestCHAR = g_pObjMGR->Get_ClientCharOBJ( iServerTarget, true );
    CObjCHAR* pDestCHAR = CSkillManager::GetSkillTarget(iServerTarget, nSkillIDX);

    if (pDestCHAR) {
        m_nToDoSkillIDX = nSkillIDX;

        SetSkillDelay(m_nToDoSkillIDX);

        /*if ( SA_TARGET_ATTACK == SKILL_ACTION_MODE( nSkillIDX ) )
            m_wCommand = CMD_ATTACK;
        else*/
        m_wCommand = CMD_SKILL2OBJ;

        m_PosGOTO = pDestCHAR->m_PosCUR;

        this->Set_TargetIDX(iServerTarget);

        LogString(LOG_DEBUG_,
            "SetCMD_Skill2OBJ[ Skill:%d ] %s==>%s \n",
            nSkillIDX,
            this->Get_NAME(),
            pDestCHAR->Get_NAME());

    } else {
        //-----------------------------------------------------------------------------------------
        // TODO:: Ÿ���� ��ã������...
        /// Ŭ���̾�Ʈ������ Ÿ���� ��� ���� ���ɰ� ���� Ÿ���� �����Ѵ�.
        //-----------------------------------------------------------------------------------------
        m_wCommand = CMD_SKILL2OBJ;
        m_iServerTarget = iServerTarget;

        m_PosGOTO = PosTO;
    }

    this->Adj_MoveSPEED(wSrvDIST, m_PosGOTO);

    //-----------------------------------------------------------------------------------------
    /// ó�� ���ۻ��´� ĳ���� ���º��� ����..
    //-----------------------------------------------------------------------------------------
    m_SkillActionState = SKILL_CASTING_STATE;
    static_cast<CObjCHAR*>(this)->ResetOwedSwingBeforeCast();

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param tPOINTF &PosGOTO	 Ÿ���� ��ġ
/// @param short nSkillIDX	 ��ų �ε���
/// @brief  : ���� Ÿ�� ��ų
//--------------------------------------------------------------------------------

void
CObjAI::SetCMD_Skill2POS(const D3DVECTOR& PosGOTO, short nSkillIDX) {
    /// 2005/7/25 CObjUSER�� �̵� : nAvy
    // if( this->IsA( OBJ_USER ) )
    //{
    //	CObjSkill2PosCommand* pObjCommand = (CObjSkill2PosCommand*)g_CommandFilter.GetCommandObject(
    // OBJECT_COMMAND_Skill2POS ); 	pObjCommand->SetCMD_Skill2POS( PosGOTO, nSkillIDX );

    //	g_CommandFilter.SetPrevCommand( pObjCommand );
    //}

    /// ���� ������ ���� �ֳ�?
    if (this->CanApplyCommand() == false) {
        this->PushCommandSkill2Pos(PosGOTO, nSkillIDX);
        return;
    }

    // This command is about to replace the attack motion. If a confirmed swing is
    // still owed its hit frame, the server has already applied it: present it now
    // instead of letting the Proc() sweep fold it away silently. The local avatar
    // (and its mount) keep the old behaviour -- their skill flow starts at click
    // time, before any server confirmation.
    // CObjCHAR is the only CObjAI in the client; cast rather than widen the base
    // vtable, which would force a clean rebuild of every character object.
    if (!static_cast<CObjCHAR*>(this)->IsLocalAvatarAttacker()) {
        // Prefer holding the cast behind the owed swing so the swing plays out
        // with its animation; the queue pops it after the hit frame (or after the
        // hold expires), and GSV_SKILL_START validates it meanwhile. Only if the
        // swing is past the hold window is it presented at once instead.
        if (static_cast<CObjCHAR*>(this)->OwesConfirmedSwingHitFrame(g_GameDATA.GetGameTime())) {
            LogString(LOG_DEBUG_,
                "CombatTrace skill command held for owed swing: attacker %d event %u age %u reason %s\n",
                this->Get_INDEX(),
                static_cast<CObjCHAR*>(this)->GetPendingCombatSwingEventId(),
                (unsigned int)(g_GameDATA.GetGameTime() - static_cast<CObjCHAR*>(this)->GetPendingCombatSwingTime()),
                "position skill command");
            this->PushCommandSkill2Pos(PosGOTO, nSkillIDX);
        return;
        }
        static_cast<CObjCHAR*>(this)->PresentPreemptedCombatSwing("position skill command");
    }

    SetEffectedSkillFlag(false);

    /// �ϴ� ���⼭ ����..�Ҹ�ġ
    /// ������ �����Ҷ� �����?
    /// CSkillManager::UpdateUseProperty( this, nSkillIDX );

    if (CS_BIT_INT & this->m_wState)
        m_wState = CS_NEXT_STOP;
    else
        m_wState = CS_STOP;

    //----------------------------------------------------------------------------------------------------
    /// @brief ������ ���� ����
    //----------------------------------------------------------------------------------------------------
    switch (m_wCommand) {
        case CMD_SKILL2SELF:
        case CMD_SKILL2OBJ:
        case CMD_SKILL2POS:
            m_wBeforeCMD = CMD_STOP;
            break;
        default:
            m_wBeforeCMD = m_wCommand;
            break;
    }

    m_nToDoSkillIDX = nSkillIDX;

    SetSkillDelay(m_nToDoSkillIDX);

    m_wCommand = CMD_SKILL2POS;
    m_PosGOTO = PosGOTO;

    this->Set_TargetIDX(0);

    //-----------------------------------------------------------------------------------------
    /// ó�� ���ۻ��´� ĳ���� ���º��� ����..
    //-----------------------------------------------------------------------------------------
    m_SkillActionState = SKILL_CASTING_STATE;
    static_cast<CObjCHAR*>(this)->ResetOwedSwingBeforeCast();
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief ������ ��ų ��ŸƮ Ÿ�� ���..
//----------------------------------------------------------------------------------------------------

void
CObjAI::SetCastingState(bool bStart) {
    if (bStart) {
        m_bCastingSTART = true;
        m_iCastingStartTime = g_GameDATA.GetGameTime();
    } else {
        m_bCastingSTART = false;
        m_iCastingStartTime = 0;

        m_SkillActionState = SKILL_CASTING_STATE;
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief
//----------------------------------------------------------------------------------------------------

bool
CObjAI::ProcOneActionFrame(int iIndex) {
    /// �̰� �ʿ���°� ������..
    if (m_pCurMOTION && m_pCurMOTION->m_nActionPointCNT) {
        if (m_pCurMOTION->m_pFrameEvent[iIndex]) {
            /// @bug :: vd_Client�ϰ�� �ȿ��� AI Action�� ���� m_pCurMOTION�� �ٲ�� ���� ��찡
            /// ���� ��~~~~~~~
            //    ������ �ٲ��...
            ActionEVENT(m_pCurMOTION->m_pFrameEvent[iIndex]);

            return true;
        }
    }

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param tPOINTF &PosGOTO	 Ÿ���� ��ġ
/// @param short nSkillIDX	 ��ų �ε���
/// @brief  : �ִϸ��̼� ����..
/// @bug -->�ȿ��� AI Action�� ���� m_pCurMOTION�� �ٲ�� ���� ��찡 ���� ��~~~~~~~
//--------------------------------------------------------------------------------

bool
CObjAI::ProcMotionFrame(void) {
    int iFrame = this->GetCurrentFrame();
    if (iFrame < 0) {
        _ASSERT(0);
        m_iCurMotionFRAME = 0;
        m_wState &= ~CS_BIT_INT;
        return false;
    }

    _ASSERT(iFrame <= m_pCurMOTION->m_wTotalFrame);
    _ASSERT(m_iCurMotionFRAME >= 0);

    /// CS_BIT_CHK �� ���� üũ�Ȱ��� ���Ŀ� MOV �� �̺�Ʈ üũ�� �ϸ� �ȵȴ�.
    bool bCheckedActionFrame = false;

    /// ������������ üũ �ؾߵȴٸ�..
    if (m_wState & CS_BIT_CHK) {
        if (m_pCurMOTION && m_pCurMOTION->m_nActionPointCNT) {

            //----------------------------------------------------------------------------------------------------
            /// @brief iFrame < m_iCurMotionFRAME �ϰ�� ����� ���װ� �ִ�.
            ///        �׷��� �� ��츦 �и��ؼ� ó��
            //----------------------------------------------------------------------------------------------------
            if (iFrame >= m_iCurMotionFRAME) {
                /// check frame action index.
                for (int iL = m_iCurMotionFRAME; iL < iFrame && iL < m_pCurMOTION->Get_TotalFRAME();
                     iL++) {
                    bCheckedActionFrame = ProcOneActionFrame(iL);
                }
            } else {
                /// ���� �������ӱ��� ������ �Ŀ�.
                for (int iL = m_iCurMotionFRAME; iL < m_pCurMOTION->Get_TotalFRAME(); iL++) {
                    bCheckedActionFrame = ProcOneActionFrame(iL);
                }

                /// ù�����Ӻ��� iFrame ���� ó��
                for (int iL = 0; iL < iFrame; iL++) {
                    bCheckedActionFrame = ProcOneActionFrame(iL);
                }
            }
        }

        /// �ѹ��� üũ
        if (m_wState & CS_BIT_ONE) {
            /// iFrame < m_iCurMotionFRAME �ִϸ��̼��� �ѹٱ� ���� iFrame�� �����������
            if (iFrame < m_iCurMotionFRAME
                || m_iCurMotionFRAME >= (m_pCurMOTION->m_wTotalFrame - 1)) {
                // ����� �Ϸ� �ƴ�.
                m_iCurMotionFRAME = 0;
                m_wState &= ~CS_BIT_INT;
                return false;
            }
        }
    }

    //--------------------------------------------------------------------------------
    ///// ���ڱ� �Ҹ������� �̵��ÿ��� ������ üũ
    //--------------------------------------------------------------------------------
    if (!bCheckedActionFrame && m_wState & CS_MOVE) {
        //----------------------------------------------------------------------------------------------------
        /// @brief iFrame < m_iCurMotionFRAME �ϰ�� ����� ���װ� �ִ�.
        ///        �׷��� �� ��츦 �и��ؼ� ó��
        //----------------------------------------------------------------------------------------------------
        if (iFrame >= m_iCurMotionFRAME) {
            /// check frame action index.
            for (int iL = m_iCurMotionFRAME; iL < iFrame && iL < m_pCurMOTION->Get_TotalFRAME();
                 iL++) {
                bCheckedActionFrame = ProcOneActionFrame(iL);
            }
        } else {
            /// ���� �������ӱ��� ������ �Ŀ�.
            for (int iL = m_iCurMotionFRAME; iL < m_pCurMOTION->Get_TotalFRAME(); iL++) {
                bCheckedActionFrame = ProcOneActionFrame(iL);
            }

            /// ù�����Ӻ��� iFrame ���� ó��
            for (int iL = 0; iL < iFrame; iL++) {
                bCheckedActionFrame = ProcOneActionFrame(iL);
            }
        }
    }

    /// @todo m_iCurMotionFRAME = iFrame;	  �̰� �־��µ� ���..
    /// ����� ������.
    /// iFrame < m_iCurMotionFRAME �ִϸ��̼��� �ѹٱ� ���� iFrame�� �����������
    if (iFrame < m_iCurMotionFRAME || m_iCurMotionFRAME >= (m_pCurMOTION->m_wTotalFrame - 1)) {
        m_iCurMotionFRAME = iFrame;
        return false;
    }

    m_iCurMotionFRAME = iFrame;

    return true;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param CObjCHAR *pTarget	Ÿ�� ������Ʈ
/// @param int iRange			����( �Ÿ� )
/// @brief  : Ÿ������ �̵�
//--------------------------------------------------------------------------------

bool
CObjAI::Goto_TARGET(CObjCHAR* pTarget, int iRange) {
    if (IsInRANGE(pTarget, iRange)) {
        /// �Ÿ��ȿ� ��� �Դ�...
        this->m_PosGOTO = this->m_PosCUR;
        this->Move_COMPLETED();
        return true;
    }

    // ���� ��ġ�� �ٲ������ �̵� ���� �ٽ� ���...
    // �̵����� �ƴ� ���¸�...�̵����� �ʴ´�.
    this->Restart_MOVE(pTarget->m_PosCUR);

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @param int iRange			����( �Ÿ� )
/// @brief  :  �̰� ���ϴµ� ����ϴ°���?
//--------------------------------------------------------------------------------

bool
CObjAI::Goto_POSITION(int iRange) {
    if (!(Get_STATE() & CS_BIT_MOV)) {
        // �̵����� �ƴϸ� �̵� ����...
        this->Start_MOVE(this->adjusted_move_speed);
    }

    int iDistance = CD3DUtil::distance((int)m_PosCUR.x,
        (int)m_PosCUR.y,
        (int)m_PosMoveSTART.x,
        (int)m_PosMoveSTART.y);
    if (iDistance + iRange >= m_iMoveDistance) {
        /// �Ÿ��ȿ� ��� �Դ�...
        this->m_PosGOTO = this->m_PosCUR;
        this->Move_COMPLETED();
        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @brief  : sit �ĸǵ� ó�� �Լ�
//--------------------------------------------------------------------------------

int
CObjAI::ProcCMD_SIT() {
    if (this->Get_STATE() != CS_SIT) {
        this->Set_STATE(CS_SIT);
        this->Set_MOTION(this->GetANI_Sit());
    }

    return 1;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @brief  : Stop command ó�� �Լ�
///			������ ������¿����� Do_StopAI ȣ���� ���ǹ� �ϴ�..
//--------------------------------------------------------------------------------

int
CObjAI::ProcCMD_STOP() {
    if (Get_STATE() != CS_STOP) {
        CObjAI::SetCMD_STOP();
    }

    this->Do_StopAI();
    return 1;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @brief  : �̵� ���μ���..
///				�̵��� ����� NPC�̰ų�, ITEM �� ��� ó��..
//--------------------------------------------------------------------------------
#include "../Interface/Dlgs/CAvatarStoreDlg.h"
int
CObjAI::ProcCMD_MOVE() {

    CObjCHAR* pTarget = (CObjCHAR*)this->Get_TARGET();

    /*
    if ( this->IsA( OBJ_AVATAR ) ) {
        LogString (LOG_DEBUG_, "%s Move (%.0f,%.0f) ==> (%.0f,%.0f) \n", this->Get_NAME(),
    this->m_PosCUR.x, this->m_PosCUR.y, this->m_PosGOTO.x, this->m_PosGOTO.y );
    }
    */

    if (pTarget && this->IsUSER()) {
        if (pTarget->Is_AVATAR()) {
            // Walking up to a player is a 10 m "interact" approach (trade, personal
            // store). Where both sides are PVP-flagged the walk is an attack chase
            // (Recv_gsv_ATTACK -> SetCombatAttackIntent -> CMD_MOVE with target), and
            // stopping at the interact ring parked the attacker 10 m short until the
            // server's first CombatSwing restarted it. Chase to attack range instead,
            // as the non-USER branch below does.
            const bool bCombatChase = 
                ((CObjCHAR*)this)->is_pvp_enabled() && pTarget->is_pvp_enabled();
            const int iStopRange =
                bCombatChase ? this->Get_AttackRange() : AVT_CLICK_EVENT_RANGE;
            /// ����� ����ڶ��
            if (Goto_TARGET(pTarget, iStopRange)) {

                if (this->IsA(OBJ_USER)) {
                    CObjAVT* pAvt = (CObjAVT*)pTarget;
                    if (pAvt->IsPersonalStoreMode()) {
                        g_pNet->Send_cli_P_STORE_LIST_REQ(
                            g_pObjMGR->Get_ServerObjectIndex(pAvt->Get_INDEX()));
                        CTDialog* pDlg = g_itMGR.FindDlg(DLG_TYPE_AVATARSTORE);
                        if (pDlg) {
                            CAvatarStoreDlg* pStoreDlg = (CAvatarStoreDlg*)pDlg;
                            pStoreDlg->SetMasterSvrObjIdx(
                                g_pObjMGR->Get_ServerObjectIndex(pAvt->Get_INDEX()));
                            pStoreDlg->SetTitle(pAvt->GetPersonalStoreTitle());
                        }
                    }
                }

                SetCMD_STOP();
            }

            return 1;

        } else if (pTarget->IsA(OBJ_NPC)) {
            /// ����� NPC���
            if (Goto_TARGET(pTarget, NPC_CLICK_EVENT_RANGE)) {
                // �����ߴ�... ��ȭ !
                pTarget->Check_EVENT(this);
                SetCMD_STOP();
            }

            return 1;
        }
    } else if (pTarget) {
        // AI character (monster, NPC, summon) chasing a target — track the
        // target's live position each tick so the chase follows the player
        // instead of walking to a stale snapshot. Stop at attack range so the
        // visual halt lines up with where the server starts a swing.
        if (Goto_TARGET(pTarget, this->Get_AttackRange())) {
            SetCMD_STOP();
        }
        return 1;
    }

    /// m_PosGOTO�� �̵�..
    if (this->Goto_POSITION()) {
        SetCMD_STOP();
    }

    return 1;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @brief  : �̵��� �������� ����ϰ���� ó��..
///			��� �������̶� �̸� �پ�����.. Command �� CMD_PICK_ITEM �� �ƴϸ� �Ϲ� ����.
//--------------------------------------------------------------------------------

int
CObjAI::ProcCMD_PICK_ITEM() {
    /// m_PosGOTO�� �̵�..
    if (this->Goto_POSITION()) {
        // �����ߴ�.
        if (Get_COMMAND() == CMD_PICK_ITEM) {
            SetCMD_STOP();

            // ������ ���⸦ ������ ��û.
            if (g_pNet->Send_cli_GET_FIELDITEM_REQ(this, m_iServerTarget)) {
                this->Set_MOTION(this->GetANI_PickITEM());
                this->SetMotionRepeatCount(1);
            }
        }
    } else {
        //----------------------------------------------------------------------------------------------------
        /// ������ �ݱ��ǰ�쿡�� ���� �Ÿ��ȿ� ������ �������� �ʾƵ� �ݱ� ��û..
        //----------------------------------------------------------------------------------------------------
        const int iPickItemRange = 150;
        int iDistance = CD3DUtil::distance((int)m_PosCUR.x,
            (int)m_PosCUR.y,
            (int)m_PosMoveSTART.x,
            (int)m_PosMoveSTART.y);
        if (iDistance + iPickItemRange >= m_iMoveDistance) {
            // �����ߴ�.
            if (Get_COMMAND() == CMD_PICK_ITEM) {
                SetCMD_STOP();

                // ������ ���⸦ ������ ��û.
                if (g_pNet->Send_cli_GET_FIELDITEM_REQ(this, m_iServerTarget)) {
                    this->Set_MOTION(this->GetANI_PickITEM());
                    this->SetMotionRepeatCount(1);
                }
            }
        }
    }

    return 1;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @brief  : Attack command ó��
///
//--------------------------------------------------------------------------------

/// May this attacker start another swing animation?
///
/// Client attack motions are self-repeating *and* self-looping: ProcCMD_ATTACK
/// re-arms Start_ATTACK every time the motion ends, and the motion is attached
/// with repeat count 0 so the engine loops it forever regardless, which is why the
/// repeat count above is part of this fix and skipping the re-arm alone is not. For a
/// attacker that is pure invention -- every real swing arrives as a CombatSwing
/// packet, and a swing with no packet behind it reaches a hit frame with an empty
/// queue, which presents nothing at all: no digit, no effect, no sound. The server
/// also goes silent for the whole of a chase (its re-close through Start_MOVE
/// broadcasts nothing), so "keep swinging until told otherwise" is exactly wrong
/// there.
///
/// Those invented swings are also what desyncs the monster, which is the part that
/// took three attempts to see. An attack motion freezes movement (CS_BIT_INT), so
/// every phantom swing holds our copy still for a full motion while the server's
/// copy keeps running after the player. Measured over one chase: our copy of a
/// monster ended up 15.4 m from where the server had it, while the server's own
/// position agreed with our avatar to within attack range the entire time. The
/// drift is a *consequence* of this loop -- do not reach for a position snap,
/// which teleports the monster onto the player and reads far worse.
///
/// The local avatar and its mount keep the old behaviour: their attack command is
/// player-driven through g_CommandFilter and starts before any round-trip.
// Avatar attack trace (Debug): the attack command walks instead of swinging
// because this client has the target out of range. Logged once per walk start.
// The server decides range from its own positions and keeps swinging, so each
// of these can cost the avatar a swing the server ran -- the suspect for the
// avatar animating fewer swings than the server (2026-09-29).
static void
TraceAvatarAttackWalk(CObjAI* pAI, CObjCHAR* pTarget, const char* szWhere) {
    CObjCHAR* pOBJ = static_cast<CObjCHAR*>(pAI);
    if (pOBJ != (CObjCHAR*)g_pAVATAR || !Log::level_enabled(Rose::Common::LogLevel::Debug)) {
        return;
    }
    const D3DXVECTOR3 vSelf = pOBJ->Get_CurPOS();
    const D3DXVECTOR3 vTarget = pTarget->Get_CurPOS();
    LogString(LOG_DEBUG_,
        "CombatTrace avatar attack walks (%s): dist %d range %d target %d state 0x%x\n",
        szWhere,
        CD3DUtil::distance((int)vSelf.x, (int)vSelf.y, (int)vTarget.x, (int)vTarget.y),
        pOBJ->Get_AttackRange(),
        pTarget->Get_INDEX(),
        (unsigned int)pOBJ->Get_STATE());
}

// Any attack action point at or after iFrom? Same event set io_motion counts in
// m_wTatalAttackFrame (melee, bow, gun, skill hits, repeat hits).
static bool
HasAttackActionFrom(const tagMOTION* pMotion, int iFrom) {
    if (!pMotion || !pMotion->m_pFrameEvent) {
        return false;
    }
    for (int iL = iFrom < 0 ? 0 : iFrom; iL < pMotion->m_wTotalFrame; iL++) {
        switch (pMotion->m_pFrameEvent[iL]) {
            case 21:
            case 22:
            case 23:
            case 24:
            case 25:
            case 26:
            case 27:
            case 28:
            case 10:
            case 20:
            case 56:
            case 66:
            case 57:
            case 67:
                return true;
        }
    }
    return false;
}

// A remote attacker plays one attack motion per confirmed swing, at the server's
// own rate, so a swing that arrives while the previous motion is still running
// waits for it to end (CS_NEXT_STOP) -- and since both sides run at the same
// cadence, that wait is never made up: it is how the client falls a swing behind
// the server for the rest of a fight. Once the motion's last action frame has
// played, what is left is follow-through with nothing to present, so the next
// swing may cut it and start at once. Frames before m_iCurMotionFRAME have been
// processed (ProcMotionFrame walks [m_iCurMotionFRAME, iFrame)).
bool
CObjAI::StandDownSpentAttackMotion() {
    if (!(m_wState & CS_BIT_ATTACK) || !(m_wState & CS_BIT_INT) || !m_pCurMOTION) {
        return false;
    }
    if (HasAttackActionFrom(m_pCurMOTION, m_iCurMotionFRAME)) {
        return false;
    }

    LogString(LOG_DEBUG_,
        "CombatTrace spent attack motion cut for next swing: obj %d frame %d of %d\n",
        static_cast<CObjCHAR*>(this)->Get_INDEX(),
        m_iCurMotionFRAME,
        (int)m_pCurMOTION->m_wTotalFrame);

    // As the refusal branch of ProcCMD_ATTACK: CS_STOP also clears frame checking,
    // and Attack_END() is what Set_MOTION's prologue would have run.
    m_wState = CS_STOP;
    static_cast<CObjCHAR*>(this)->Attack_END();
    return true;
}

// Any frame event at all at or after iFrom? A skill release carries its payload on
// many event kinds (launch, hit, effect, sound), so none of them may be cut.
static bool
HasFrameEventFrom(const tagMOTION* pMotion, int iFrom) {
    if (!pMotion || !pMotion->m_pFrameEvent) {
        return false;
    }
    for (int iL = iFrom < 0 ? 0 : iFrom; iL < pMotion->m_wTotalFrame; iL++) {
        if (pMotion->m_pFrameEvent[iL]) {
            return true;
        }
    }
    return false;
}

// The skill siblings of StandDownSpentAttackMotion (Evil Fairy 2725, 2026-10-06):
//  - Release. skill_01 is 201 frames with its last event at 45, 5 s of tail, and
//    every order the server sent meanwhile waits behind it -- in m_CommandQueue, or
//    already applied as CS_NEXT_STOP (an attack that arrives mid-motion keeps the
//    motion and waits for its end; m_nDoingSkillIDX says it is still the release).
//  - Casting repeat. A monster loops its casting clip until RESULT_OF_SKILL arrives
//    (ProcSkillCastingLoop), and a loop that started just before it plays out in
//    full: 3.4 s more for the fairy, on half of its casts.
// Ends the motion exactly as ProcMotionFrame does at its last frame (frame reset,
// CS_BIT_INT cleared), so everything that runs at a natural end runs here too.
bool
CObjAI::EndSpentSkillMotion(bool bCommandQueued) {
    if (!m_pCurMOTION) {
        return false;
    }
    const char* szWhat;
    if (m_nDoingSkillIDX && (m_wState == CS_NEXT_STOP2 || m_wState == CS_NEXT_STOP)) {
        // CS_NEXT_STOP is any order applied mid-motion; only an attack counts. A cast
        // applied that way leaves the same state (fairy 687, 21:00:33: cut at frame
        // 51, the server's start came 6 s later, the cast was abandoned unanimated).
        const bool bAttackWaits = m_wState == CS_NEXT_STOP && m_wCommand == CMD_ATTACK;
        if (!bCommandQueued && !bAttackWaits) {
            return false; // nothing waits: an idle caster keeps its full animation
        }
        szWhat = "skill action";
    } else if (m_wState == CS_CASTING && m_iWaitLoopCnt > 0 && bCanActionActiveSkill()) {
        szWhat = "casting repeat";
    } else {
        return false;
    }
    if (HasFrameEventFrom(m_pCurMOTION, m_iCurMotionFRAME)) {
        return false;
    }

    LogString(LOG_DEBUG_,
        "CombatTrace spent %s cut: obj %d frame %d of %d\n",
        szWhat,
        static_cast<CObjCHAR*>(this)->Get_INDEX(),
        m_iCurMotionFRAME,
        (int)m_pCurMOTION->m_wTotalFrame);

    m_iCurMotionFRAME = 0;
    m_wState &= ~CS_BIT_INT;
    return true;
}

static bool
CanStartConfirmedSwing(CObjAI* pAI) {
    CObjCHAR* pOBJ = static_cast<CObjCHAR*>(pAI);

    if (pOBJ->IsLocalAvatarAttacker()) {
        return true;
    }

    return pOBJ->GetTrackedCombatSwingEventId() != 0;
}

int
CObjAI::ProcCMD_ATTACK() {
//-------------------------------------------------------------------------------
#if defined(_GBC)
    //����ȣ::......
    if (GetPetMode() >= 0)
        return 1;
#endif
    //-------------------------------------------------------------------------------

    CObjCHAR* pTarget = (CObjCHAR*)this->Get_TARGET();
    if (pTarget) {
        if (Get_STATE() & CS_BIT_ATTACK) {
            /// �������̴�..
            if (!(Get_STATE() & CS_BIT_INT)) {
                // TODO :: ���� ���� �ٸ� ��ġ�� ���� �ߴٸ�...
                if (!IsInRANGE(pTarget, this->Get_AttackRange())) {
                    TraceAvatarAttackWalk(this, pTarget, "between swings");
                    // ������ ���� ������ �̵�...
                    m_wState = CS_STOP;
                    m_PosGOTO = pTarget->m_PosCUR;
                    this->Start_MOVE(this->adjusted_move_speed);
                    return 1;
                }

                // ������ ���� ���� !!!
                if (!CanStartConfirmedSwing(this)) {
                    // No server-authorised swing behind this one. Drop the attack
                    // state so the next tick runs the move branch -- which is what
                    // the server is doing -- instead of inventing another swing.
                    //
                    // CS_STOP also clears CS_BIT_CHK, and that part is load-bearing:
                    // ProcMotionFrame walks m_iCurMotionFRAME -> iFrame itself and
                    // the completion branch resets m_iCurMotionFRAME to 0, so a
                    // motion left holding its last frame with frame checking still
                    // on re-fires every action point on the following tick.
                    //
                    // The motion is deliberately NOT replaced. Swapping in
                    // GetANI_Stop() here was tried and reverted twice: the attacker
                    // visibly freezes and then slides across the terrain to catch
                    // up, because the move branch drives position from the next tick
                    // while the idle motion is still attached. The repeat count of 1
                    // above is what stops the loop; nothing else needs to.
                    //
                    // Attack_END() is called by hand because Set_MOTION's prologue
                    // normally fires it and we are deliberately not calling
                    // Set_MOTION -- without it m_bAttackSTART, the weapon trail and
                    // the attack-speed animatable rate all stay latched.
                    m_wState = CS_STOP;
                    static_cast<CObjCHAR*>(this)->Attack_END();
                    return 1;
                }

                this->Start_ATTACK(pTarget);
            }

            return 1;
        }

        /// Ÿ������ �̵�...
        if (this->Goto_TARGET(pTarget, this->Get_AttackRange())) {
            // ���� ������ ���� ���� !!!
            if (CanStartConfirmedSwing(this)) {
                this->Start_ATTACK(pTarget);
            } else if (m_wState == (CS_NEXT_STOP & ~CS_BIT_INT)) {
                // A CombatSwing that arrives mid-motion applies SetCMD_ATTACK as
                // CS_NEXT_STOP, which keeps frame checking (CS_BIT_ONE) but not
                // CS_BIT_ATTACK -- so when the motion completes this branch runs,
                // not the refusal branch above. Normally the swing is still
                // pending by then and Start_ATTACK replaces the state; when it was
                // settled first (folded into an earlier hit -- see
                // CObjCHAR::FoldLaggingHits), nothing did, and the motion held on
                // its last frame re-fired every action point every other tick
                // until the next swing arrived: 6-19 silent hit frames and attack
                // sounds in a second (Hebarn, 2026-10-06). Stand it down exactly
                // like the refusal branch. Matched on the exact leftover state:
                // CS_NEXT_STOP2 (a skill action) also carries CS_BIT_ONE without
                // CS_BIT_INT and must never be cut here.
                m_wState = CS_STOP;
                static_cast<CObjCHAR*>(this)->Attack_END();
            }
        } else {
            if (!(Get_STATE() & CS_BIT_MOV)) {
                TraceAvatarAttackWalk(this, pTarget, "closing in");
                this->Start_MOVE(this->adjusted_move_speed);
            } else
                this->Do_AttackMoveAI(pTarget); /// MOB ���� �̵��� �ΰ����� ó��..
        }

    } else {
        /// Ÿ���� ����..
        ChangeActionMode(AVATAR_NORMAL_MODE);

        /// �� �ƹ�Ÿ�ϰ��� Ÿ���� ������� ���������� ����.. STOP ������ ����
        this->Set_TargetIDX(0);
        m_wCommand = CMD_STOP;

        pTarget = g_pObjMGR->Get_ClientCharOBJ(m_iServerTarget, false);
        if (pTarget) {
            this->m_iServerTarget = 0;
            this->SetCMD_STOP();
        } else {
            int iDistance = CD3DUtil::distance((int)m_PosCUR.x,
                (int)m_PosCUR.y,
                (int)m_PosMoveSTART.x,
                (int)m_PosMoveSTART.y);
            if (iDistance < m_iMoveDistance) {
                // Ŭ���̾�Ʈ������ �������� ���� ���� ��ǥ���� �̵��� ���� ����...
                if (this->Goto_POSITION()) {
                    m_wState = CS_STOP;

                    this->Set_MOTION(this->GetANI_Stop());
                    this->Set_ModelSPEED(0.0f);
                }
            } else {
                m_wState = CS_STOP;

                this->Set_MOTION(this->GetANI_Stop());
                this->Set_ModelSPEED(0.0f);
            }
        }
    }

    return 1;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @brief  : ������ų command ó��
///				���� ������ �ٷ� ó���� ��ų --> ���� ���·� �ȴ�.
//--------------------------------------------------------------------------------

// A skill used mid-attack on the local avatar can owe a swing the server already
// applied; CObjCHAR::PlayOwedSwingBeforeCast plays it first. Asked only while the
// cast has not begun: the command set a skill to do and casting has not started.
static bool
HoldCastForOwedSwing(CObjAI* pAI) {
    if (pAI->m_bCastingSTART || !pAI->m_nToDoSkillIDX
        || pAI->m_SkillActionState != SKILL_CASTING_STATE) {
        return false;
    }
    return static_cast<CObjCHAR*>(pAI)->PlayOwedSwingBeforeCast(pAI->m_nToDoSkillIDX);
}

int
CObjAI::ProcCMD_Skill2SELF() {
    if (HoldCastForOwedSwing(this)) {
        return 1;
    }

    if (1 != this->Do_SKILL(0)) {
        ;
    }
    return 1;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @brief  : �������� �̵��� ��ų ó�� --> ���� ���·� �ȴ�.
///
//--------------------------------------------------------------------------------

int
CObjAI::ProcCMD_Skill2POSITION() {
    //-----------------------------------------------------------------------------------------
    /// ���� ��ų ĳ������ �������� �ʾҴٸ�..
    //-----------------------------------------------------------------------------------------
    if (!m_bCastingSTART) {
        if (HoldCastForOwedSwing(this)) {
            return 1;
        }

        t_POSITION posTemp = this->m_PosGOTO;
        // ��ҷ� �̵�... & ��ų ����
        if (this->Goto_POSITION(this->Get_AttackRange()) == false) {
            return 1;
        }
        ///
        /// Goto_POSITION ���ο��� ������������ �����Ұ�� PosGOTO�� ������ġ�μ����Ѵ�.
        /// �׷��� ������ ��ų�� ��� ���� ����Ʈ�� PosGOTO�� ��µǹǷ� ���µǸ� �ȵȴ�.
        ///
        this->m_PosGOTO = posTemp;
    }

    /// ĳ���� �Ǵ� �������� ����...
    if (1 != this->Do_SKILL(0)) {
        // casting: 1, cancel: 0, active: 2
        /// m_wCommand = CMD_STOP;
    }

    return 1;
}

//--------------------------------------------------------------------------------
/// class : CObAI
/// @brief  : Ÿ������ �̵��� ��ų ó�� --> ���� ���·� �ȴ�.
///           04/4/28 Ÿ�ٿ� �����ߴٰ� ĳ������ ������������ ��ٸ��ٰ� ������ ����
///				start ��Ŷ�� ������ ĳ������ �����϶�.
///				Ÿ���� �������.. Cast_end()�� ȣ���Ѱ� �ƴ϶� ��� ��ų �ε����� ����..
//--------------------------------------------------------------------------------

int
CObjAI::ProcCMD_Skill2OBJECT() {

//---------------------------------------------------------------------------------
#if defined(_GBC)
    //����ȣ::�����϶� ���� �̵���Ű��,�ƹ�Ÿ ��ų�� ĳ�����Ѵ�.
    if (GetPetMode() >= 0)
        return ProcCMD_Skill2OBJECT_PET();
#endif
    //---------------------------------------------------------------------------------

    // Ÿ������ �̵�... & ��ų ����
    /// CObjCHAR *pTarget = (CObjCHAR*)this->Get_TARGET();
    CObjCHAR* pTarget = CSkillManager::GetSkillTarget(m_iServerTarget,
        (this->m_nToDoSkillIDX) ? this->m_nToDoSkillIDX : this->m_nActiveSkillIDX);
    if (pTarget) {
        //-----------------------------------------------------------------------------------------
        /// ���� ��ų ĳ������ �������� �ʾҴٸ�..
        //-----------------------------------------------------------------------------------------
        if (!m_bCastingSTART) {
            // Before moving to the skill target and before waiting for
            // GSV_SKILL_START: the server plays out its swing at this point too.
            if (HoldCastForOwedSwing(this)) {
                return 1;
            }

            int iAttackRange = this->Get_AttackRange();

            //-----------------------------------------------------------------------------------------
            /// ĳ�������� �ƴϸ� Ÿ������ �̵�...
            //-----------------------------------------------------------------------------------------
            if (this->Goto_TARGET(pTarget, iAttackRange) == false) {
                if (!(Get_STATE() & CS_BIT_MOV)) {
                    LogString(LOG_NORMAL, "ProcCMD_Skill2Object::Start_Move[ %x ]\n", Get_STATE());
                    this->Start_MOVE(this->adjusted_move_speed);
                }

                return 1;
            }

            //-----------------------------------------------------------------------------------------
            /// �����κ��� ��ų������Ŷ�� ���޾Ƶ� Ÿ������ �̵�..
            //-----------------------------------------------------------------------------------------
            if (!bCanStartSkill()) {
                // Remote-cast watchdog. A monster's cast waits here for the server's
                // GSV_SKILL_START; if that never comes (the server cancelled or
                // superseded the cast, or the start was consumed elsewhere) the
                // monster used to sit in CMD_SKILL2OBJ for good, and a queued lethal
                // projectile event on the avatar then died by the 6 s hard cap
                // (Terrasaurus King, 16:35:50: cast applied, no start, timeout death
                // with no digit). Log the first wait so the next log names the case,
                // and abandon after kRemoteCastStartWaitMs: go idle like the server
                // copy and release the projectile events that can no longer launch.
                static const DWORD kRemoteCastStartWaitMs = 3000;
                if (!static_cast<CObjCHAR*>(this)->IsLocalAvatarAttacker()) {
                    const DWORD now = g_GameDATA.GetGameTime();
                    if (m_dwRemoteCastWaitSince == 0) {
                        m_dwRemoteCastWaitSince = now;
                        LogString(LOG_DEBUG_,
                            "CombatTrace remote cast waiting for skill start: caster %d skill %d target %d\n",
                            this->Get_INDEX(),
                            m_nToDoSkillIDX,
                            pTarget->Get_INDEX());
                    } else if (now - m_dwRemoteCastWaitSince >= kRemoteCastStartWaitMs) {
                        LogString(LOG_DEBUG_,
                            "CombatTrace remote cast abandoned, no skill start after %u ms: caster %d skill %d target %d\n",
                            (unsigned int)(now - m_dwRemoteCastWaitSince),
                            this->Get_INDEX(),
                            m_nToDoSkillIDX,
                            pTarget->Get_INDEX());
                        m_dwRemoteCastWaitSince = 0;
                        pTarget->DiscardQueuedProjectileDamageFromAttacker(
                            static_cast<CObjCHAR*>(this), "remote cast abandoned");
                        // The cast will not play, but the server applied its status
                        // (Deader Rex's Hex, 2026-09-29): retire what it parked now,
                        // status included, instead of leaving it to the timeouts --
                        // a projectile payload there was dropped status and all.
                        static_cast<CObjCHAR*>(this)->ResolveAbandonedCastPayloads(
                            m_nToDoSkillIDX, "remote cast abandoned");
                        Casting_END();
                        m_wCommand = CMD_STOP;
                        return 1;
                    }
                }
                m_wState = CS_STOP;
                m_fCurMoveSpeed = 0;
                /// this->Set_MOTION( this->GetANI_Casting() );
                return 1;
            }
            m_dwRemoteCastWaitSince = 0;
        }

        //-----------------------------------------------------------------------------------------
        /// Ÿ�� �������� ���� ������.
        //-----------------------------------------------------------------------------------------
        Set_ModelDIR(pTarget->m_PosCUR);

        //-----------------------------------------------------------------------------------------
        /// Ÿ���� �׾���ȴ�...
        //-----------------------------------------------------------------------------------------
        if (pTarget->m_bDead) {
            SetEffectedSkillFlag(true);
        }

        /// ĳ���� �Ǵ� �������� ����...
        const int iSkillResult = this->Do_SKILL(this->Get_TargetIDX(), pTarget);
        if (iSkillResult == 0 && !static_cast<CObjCHAR*>(this)->IsLocalAvatarAttacker()) {
            // Nothing left to cast (Do_SKILL's default branch: the state machine is
            // idle). The server sets CMD_STOP here; the client left the command in
            // place, which parked a remote caster in CMD_SKILL2OBJ doing nothing.
            LogString(LOG_DEBUG_,
                "CombatTrace remote cast ended without action: caster %d skill %d\n",
                this->Get_INDEX(),
                m_nDoingSkillIDX);
            m_wCommand = CMD_STOP;
        }
        if (1 != iSkillResult) {
            // casting: 1, cancel: 0, active: 2
            /// Do_Skill ���ο��� ����
            // m_wCommand = CMD_STOP;
            ;
        }

    } else
    //-----------------------------------------------------------------------------------------
    /// Ÿ���� ����. �ٵ�.. �̹� ĳ������ ���۵Ǿ��µ� �߰��� �׾�����ų� �Ѵٸ�?
    //-----------------------------------------------------------------------------------------
    {
        /// �� �ƹ�Ÿ�ϰ��� Ÿ���� ������� ���������� ����.. STOP ������ ����
        this->Set_TargetIDX(0);
        m_wCommand = CMD_STOP;

        pTarget = g_pObjMGR->Get_ClientCharOBJ(m_iServerTarget, false);

        //-----------------------------------------------------------------------------------------
        /// Ÿ���� ���ٸ� ��ų�� ���� ������ ������ �����Ѵ�.
        //-----------------------------------------------------------------------------------------
        SetEffectedSkillFlag(true);
        m_nActiveSkillIDX = 0;
        Casting_END();

        //-----------------------------------------------------------------------------------------
        /// �׾��ִ� ��ü�� ����.
        //-----------------------------------------------------------------------------------------
        if (!pTarget) {
            int iDistance = CD3DUtil::distance((int)m_PosCUR.x,
                (int)m_PosCUR.y,
                (int)m_PosMoveSTART.x,
                (int)m_PosMoveSTART.y);
            if (iDistance < m_iMoveDistance) {
                //-----------------------------------------------------------------------------------------
                /// Ŭ���̾�Ʈ������ �������� ���� ���� ��ǥ���� �̵��� ���� ����...
                /// �̰� �̻��ϴ�..
                //-----------------------------------------------------------------------------------------
                if (this->Goto_POSITION()) {
                    m_wState = CS_STOP;

                    this->Set_MOTION(this->GetANI_Stop());
                    this->Set_ModelSPEED(0.0f);
                } else
                    return 1;
            }
        }

        SetCMD_STOP();
    }

    return 1;
}

short
CObjAI::Get_RecoverHP(short nRecoverMODE) {
    short nRecoverHP = 0;

    switch (Get_COMMAND()) {
        case CMD_SIT: {
            nRecoverHP =
                this->GetAdd_RecoverHP() + (this->Get_CON() + 30) / 8 * (nRecoverMODE + 3) / 10;
        } break;
        default: {
            nRecoverHP = (short)((this->GetAdd_RecoverHP() + (this->Get_CON() + 40) / 6.f) / 6.f);
        } break;
    }
    return nRecoverHP;
}

short
CObjAI::Get_RecoverMP(short nRecoverMODE) {
    short nRecoverMP = 0;
    switch (Get_COMMAND()) {
        case CMD_SIT:
            nRecoverMP =
                (this->GetAdd_RecoverMP() + (this->Get_CON() + 20) / 10 * nRecoverMODE / 7);
    }

    return nRecoverMP;
}
//----------------------------------------------------------------------------------------------------
/// @brief ������ �нú� ��ų������ �߰��Ǳ����� ���� MaxHP���ϴ� �޽�� : 2005/7/13 - nAvy
//----------------------------------------------------------------------------------------------------
int
CObjAI::GetOri_MaxHP() {
    _RPT0(_CRT_WARN, "GetOri_MaxHP() ȣ��ÿ� ���� ����� �ִ�");
    return 0;
}
//----------------------------------------------------------------------------------------------------
/// @brief ������ �нú� ��ų������ �߰��Ǳ����� ���� MaxMP���ϴ� �޽�� : 2005/7/13 - nAvy
//----------------------------------------------------------------------------------------------------
int
CObjAI::GetOri_MaxMP() {
    _RPT0(_CRT_WARN, "GetOri_MaxMP() ȣ��ÿ� ���� ����� �ִ�");
    return 0;
}

uint32
CObjAI::total_attack_power() {
    return this->stats.attack_power;
}

uint32_t
CObjAI::total_hit_rate() {
    return this->stats.hit_rate;
}