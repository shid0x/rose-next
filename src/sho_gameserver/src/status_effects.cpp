
#include "stdAFX.h"

#include "status_effects.h"

#include "rose/common/status_effect/status_effect_data.h"
#include "rose/io/stb.h"

using namespace Rose::Common;

bool
StatusEffects::IsTauntSTATUS(int iAttackObjIDX, CZoneTHREAD* pCurZone) {
    if (this->IsSET(c_dwIngFLAG[ING_TAUNT])) {
        CObjCHAR* pDestCHAR = g_pObjMGR->Get_CharOBJ(this->m_iTargetOBJ[ING_TAUNT], true);
        if (pDestCHAR) {
            // 타운트를 건넘이면 타운트 상태가 아닌것으로...
            if (this->m_iTargetOBJ[ING_TAUNT] == iAttackObjIDX)
                return false;

            if (pCurZone == pDestCHAR->GetZONE())
                return true;
        }

        // 타겟이 없거나 존을 이동했다...타운트 풀리도록..
        m_nTICKs[ING_TAUNT] = 0;
        return false;
    }

    return false;
}

//-------------------------------------------------------------------------------------------------
bool
StatusEffects::IsEnableApplayITEM(short nIngStbIDX) {
    short nTypeIDX = STATE_TYPE(nIngStbIDX);

    // 이미 적용되어 있는가 ?
    if (this->IsSET(c_dwIngFLAG[nTypeIDX])) {
        if (0 == STATE_CAN_DUPLICATED(nIngStbIDX)) {
            // 갱신 적용 불가면 무조건 취소...
            return false;
        }

        // 적용 되어 있는것 보다 우선 순위가 있는가 ?
        if (m_nIngSTBIdx[nTypeIDX] > nIngStbIDX) {
            return false;
        }
    }

    return true;
}
bool
StatusEffects::IsEnableApplay(short nIngStbIDX, short nAdjValue) {
    short nTypeIDX = STATE_TYPE(nIngStbIDX);

    // 이미 적용되어 있는가 ?
    if (this->IsSET(c_dwIngFLAG[nTypeIDX])) {
        if (0 == STATE_CAN_DUPLICATED(nIngStbIDX)) {
            // 갱신 적용 불가면 무조건 취소...
            return false;
        }

        if (m_nAdjVALUE[nTypeIDX] > nAdjValue) {
            return false;
        }
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
DWORD
StatusEffects::UpdateIngSTATUS(CObjCHAR* pCharOBJ,
    short nIngStbIDX,
    short nTick,
    short nAdjVALUE,
    short nSkillIDX,
    int iTargetObjIDX) {
    // 반드시 적용 가능여부 판단한후 호출되어야 한다 !!!
    short nTypeIDX = STATE_TYPE(nIngStbIDX);

    // if ( nTypeIDX <= ING_INC_MP ) {
    //	// 스킬로는 물약으로 변경되는 상태를 변경할수 없도록...
    //	return 0;
    //}

    if (nTypeIDX > ING_CHECK_END) {
        switch (nTypeIDX) {
            case ING_CLEAR_INVISIBLE: // 투명상태 해지..
            {
                if (this->IsSET(c_dwIngFLAG[ING_DISGUISE])) {
                    m_nTICKs[ING_DISGUISE] =
                        0; // 바로 삭제 하지 말고 다음 처리해 주위에 전송될수 있도록..
                }
                if (this->IsSET(c_dwIngFLAG[ING_TRANSPARENT])) {
                    m_nTICKs[ING_TRANSPARENT] =
                        0; // 바로 삭제 하지 말고 다음 처리해 주위에 전송될수 있도록..
                }
                return 0;
            }
            case ING_CLEAR_GOOD: // 유리
            {
                for (short nI = ING_POISONED; nI <= ING_CHECK_END; nI++) {
                    if (this->IsSET(c_dwIngFLAG[nI])
                        && STATE_PRIFITS_LOSSES(m_nIngSTBIdx[nI]) == 0) {
                        m_nTICKs[nI] =
                            0; // 바로 삭제 하지 말고 다음 처리해 주위에 전송될수 있도록..
                    }
                }
                return 0;
            }
            case ING_CLEAR_BAD: // 불리
            {
                for (short nI = ING_POISONED; nI <= ING_CHECK_END; nI++) {
                    if (this->IsSET(c_dwIngFLAG[nI])
                        && STATE_PRIFITS_LOSSES(m_nIngSTBIdx[nI]) == 1) {
                        m_nTICKs[nI] =
                            0; // 바로 삭제 하지 말고 다음 처리해 주위에 전송될수 있도록..
                    }
                }
                if (this->IsSET(FLAG_ING_TAUNT)) {
                    m_nTICKs[ING_TAUNT] = 0;
                }
                return 0;
            }
            case ING_CLEAR_ALL: {
                // 전체 해지시 물약도 해지되기 때문에...
                // this->ClearALL( FLAG_ING_CLEAR );		// 상점 상태는 유지.
                for (short nI = ING_POISONED; nI <= ING_CHECK_END; nI++) {
                    if (this->IsSET(c_dwIngFLAG[nI])
                        && STATE_PRIFITS_LOSSES(m_nIngSTBIdx[nI]) < 2) {
                        this->ClearSTATUS((eING_TYPE)nI);
                    }
                }
                return 0;
            }
            case ING_DEC_LIFE_TIME:
                this->SetFLAG(c_dwIngFLAG[nTypeIDX]);
                this->m_nIngSTBIdx[nTypeIDX] = nIngStbIDX;
                return 0;
        }

        // 타운트등...
    }

    this->SetFLAG(c_dwIngFLAG[nTypeIDX]);

    m_nAdjVALUE[nTypeIDX] = nAdjVALUE;
    m_nTICKs[nTypeIDX] = nTick;
    m_nIngSTBIdx[nTypeIDX] = nIngStbIDX;
    m_nIngSKILL[nTypeIDX] = nSkillIDX;
    m_iTargetOBJ[nTypeIDX] = iTargetObjIDX;

    /*
        삭제~~~
        if ( c_dwIngFLAG[ nTypeIDX ] & ( FLAG_ING_MAX_HP | FLAG_ING_MAX_MP ) ) {
            // 최대 HP,MP가 올라가는 경우 현재 수치까지 같이 올리자...	2004. 4. 12
            // 먼저 MAX_HP를 올려야 아래서 현재 피가 오른다.
            switch( nTypeIDX ) {
                case ING_INC_MAX_HP :	pCharOBJ->Add_HP( nAdjVALUE );	break;
                case ING_INC_MAX_MP :	pCharOBJ->Add_MP( nAdjVALUE );	break;
            }
        }
    */

    return (c_dwIngFLAG[nTypeIDX] & (FLAG_ING_FAINTING | FLAG_ING_SLEEP));
}

void
StatusEffects::UpdateIngPOTION(short nIngStbIDX, short nTotValue, short nAdjPerSEC) {
    // 반드시 적용 가능여부 판단한후 호출되어야 한다 !!!
    short nTypeIDX = STATE_TYPE(nIngStbIDX);

    if (nTypeIDX > FLAG_ING_MP)
        return;

    this->SetFLAG(c_dwIngFLAG[nTypeIDX]);

    m_nIngSTBIdx[nTypeIDX] = nIngStbIDX;

    m_POTION[nTypeIDX].m_dwTotTIME = (1000 * nTotValue / nAdjPerSEC); // 전체 필요시간

    assert(m_POTION[nTypeIDX].m_dwTotTIME);

    m_POTION[nTypeIDX].m_dwAccTIME = 0; // 누적시간.
    m_POTION[nTypeIDX].m_nTotVALUE = nTotValue;
    m_POTION[nTypeIDX].m_nAccVALUE = 0;
}

short
StatusEffects::Proc_IngPOTION(PotionEffect* pPOTION, DWORD dwPassTIME) {
    pPOTION->m_dwAccTIME += dwPassTIME;

    assert(pPOTION->m_dwTotTIME);

    int iCurAccValue =
        floor_int(pPOTION->m_dwAccTIME * pPOTION->m_nTotVALUE / pPOTION->m_dwTotTIME);

    short nDiff;
    if (iCurAccValue >= pPOTION->m_nTotVALUE) {
        // 적용끝...
        nDiff = pPOTION->m_nTotVALUE - pPOTION->m_nAccVALUE;
    } else {
        nDiff = iCurAccValue - pPOTION->m_nAccVALUE;
    }
    pPOTION->m_nAccVALUE += nDiff;

    return nDiff;
}

//-------------------------------------------------------------------------------------------------
#define ONE_SECOND 1000
DWORD
StatusEffects::Proc(CObjCHAR* pCharOBJ, DWORD dwPassTIME) {
    DWORD dwChangedFLAG = 0;

    // 물약 처리... 매 프레임별...
    if (this->IsSET(FLAG_ING_HP | FLAG_ING_MP)) {
        short nAdj;
        PotionEffect* pPOTION;
        BYTE btIngEND = 0;
        if (this->IsSET(FLAG_ING_HP)) {
            pPOTION = &m_POTION[ING_INC_HP];
            nAdj = this->Proc_IngPOTION(pPOTION, dwPassTIME);

            // LogString( 0xfff, "HP: %d,  %d/%d,   %d  PassTIME:%d \n", nAdj, pPOTION->m_nAccVALUE,
            // pPOTION->m_nTotVALUE, pCharOBJ->Get_HP(), dwPassTIME );

            if (pCharOBJ->Add_HP(nAdj) >= pCharOBJ->Get_MaxHP()
                || pPOTION->m_nAccVALUE >= pPOTION->m_nTotVALUE) {
                // 처리 완료 or 만땅.
                this->ClearStatusFLAG(FLAG_ING_HP);
                dwChangedFLAG |= FLAG_ING_HP;
                btIngEND = 0x01;
            }
        }

        if (this->IsSET(FLAG_ING_MP)) {
            pPOTION = &m_POTION[ING_INC_MP];
            nAdj = this->Proc_IngPOTION(pPOTION, dwPassTIME);

            // LogString( 0xfff, "MP: %d,  %d/%d,   %d  PassTIME:%d \n", nAdj, pPOTION->m_nAccVALUE,
            // pPOTION->m_nTotVALUE, pCharOBJ->Get_MP(), dwPassTIME );

            if (pCharOBJ->Add_MP(nAdj) >= pCharOBJ->Get_MaxMP()
                || pPOTION->m_nAccVALUE >= pPOTION->m_nTotVALUE) {
                // 처리 완료 or 만땅.
                this->ClearStatusFLAG(FLAG_ING_MP);
                dwChangedFLAG |= FLAG_ING_MP;
                btIngEND |= 0x02;
            }
        }

        if (btIngEND) {
            pCharOBJ->Send_gsv_SET_HPnMP(btIngEND);
        }
    }

    // 1초당 처리 할것들...
    m_dwAccTIME += dwPassTIME;
    if (m_dwAccTIME < ONE_SECOND)
        return dwChangedFLAG;
    m_dwAccTIME -= ONE_SECOND;

    if (this->IsSET(FLAG_ING_PROC_PER_TICK)) {
        // 중독...
        if (this->IsSET(FLAG_ING_POISONED)) {
            if (m_nTICKs[ING_POISONED]-- > 0) {
                // dec hp
                if (pCharOBJ->Get_HP() > 0) {
                    short nValue = STATE_APPLY_ABILITY_VALUE(m_nIngSTBIdx[ING_POISONED], 0);

                    pCharOBJ->Give_STATUS_DAMAGE(nValue);
                }
            } else {
                this->ClearStatusFLAG(FLAG_ING_POISONED);
                dwChangedFLAG |= FLAG_ING_POISONED;
            }
        }

        // Summon lifetime HP-drain removed: produced a per-second damage number
        // and hit/knockback animation on owned summons. FLAG_ING_DEC_LIFE_TIME
        // is still set on summons because it is reused as a "this is a summon"
        // marker by Add_ADJ_STATUS and the client.
    }

    // 비트 플레그 집합
    if (this->IsSET(FLAG_ING_CHECK_PER_TICK)) {
        // 최대 HP
        if (this->IsSET(FLAG_ING_MAX_HP)) {
            if (m_nTICKs[ING_INC_MAX_HP]-- <= 0) {
                this->ClearSTATUS(ING_INC_MAX_HP);
                pCharOBJ->Add_HP(0);
                dwChangedFLAG |= FLAG_ING_MAX_HP;
            }
        }

        // 최대 MP
        if (this->IsSET(FLAG_ING_MAX_MP)) {
            if (m_nTICKs[ING_INC_MAX_MP]-- <= 0) {
                this->ClearSTATUS(ING_INC_MAX_MP);
                pCharOBJ->Add_MP(0);
                dwChangedFLAG |= FLAG_ING_MAX_MP;
            }
        }

        // 유지 시간 체크..
        for (short nI = ING_CHECK_START; nI <= ING_CHECK_END; nI++) {
            if (!this->IsSET(c_dwIngFLAG[nI]))
                continue;

            if (m_nTICKs[nI]-- <= 0) {
                // 시간이 완료 되었으면...
                this->ClearSTATUS((eING_TYPE)nI);
                dwChangedFLAG |= c_dwIngFLAG[nI];
            }
        }

        // 타운트..
        if (this->IsSET(FLAG_ING_TAUNT)) {
            if (m_nTICKs[ING_TAUNT]-- <= 0) {
                this->m_iTargetOBJ[ING_TAUNT] = 0;
                this->ClearSTATUS(ING_TAUNT);
                dwChangedFLAG |= FLAG_ING_TAUNT;
            }
        }

        // 부활
        if (this->IsSET(FLAG_ING_REVIVE)) {
            if (m_nTICKs[ING_REVIVE]-- <= 0) {
                this->ClearSTATUS(ING_REVIVE);
                dwChangedFLAG |= FLAG_ING_REVIVE;
            }
        }
    }

    // TODO: RAM: This doesn't need to be updated per tick. Update conditions:
    //	- Update on level up
    //	- Update on create
    if (this->is_enabled(StatusEffectType::Goddess)) {
        this->goddess_effect.update(pCharOBJ->Get_LEVEL());
    }

    return dwChangedFLAG;
}

short
StatusEffects::Adj_RUN_SPEED() {
    short val = m_nAdjVALUE[ING_INC_MOV_SPD] - m_nAdjVALUE[ING_DEC_MOV_SPD] + m_nAruaRunSPD;

    // Goddess buff doesn't stack with other buffs
    if (is_enabled(StatusEffectType::Goddess)) {
        val += max(0, goddess_effect.move_speed - m_nAdjVALUE[ING_INC_MOV_SPD]);
    }

    return val;
}

short
StatusEffects::Adj_ATK_SPEED() {
    short val = m_nAdjVALUE[ING_INC_ATK_SPD] - m_nAdjVALUE[ING_DEC_ATK_SPD];

    // Goddess buff doesn't stack with other buffs
    if (is_enabled(StatusEffectType::Goddess)) {
        val += max(0, goddess_effect.attack_speed - m_nAdjVALUE[ING_INC_ATK_SPD]);
    }

    return val;
}

short
StatusEffects::Adj_APOWER() {
    short val = m_nAdjVALUE[ING_INC_APOWER] - m_nAdjVALUE[ING_DEC_APOWER] + m_nAruaATK;

    // Goddess buff doesn't stack with other buffs
    if (is_enabled(StatusEffectType::Goddess)) {
        val += max(0, goddess_effect.attack_power - m_nAdjVALUE[ING_INC_APOWER]);
    }

    return val;
}

short
StatusEffects::Adj_DPOWER() {
    return m_nAdjVALUE[ING_INC_DPOWER] - m_nAdjVALUE[ING_DEC_DPOWER] + m_nAruaRES;
}

short
StatusEffects::Adj_RES() {
    return m_nAdjVALUE[ING_INC_RES] - m_nAdjVALUE[ING_DEC_RES];
}

short
StatusEffects::Adj_HIT() {
    short val = m_nAdjVALUE[ING_INC_HIT] - m_nAdjVALUE[ING_DEC_HIT];

    // Goddess buff doesn't stack with other buffs
    if (is_enabled(StatusEffectType::Goddess)) {
        val += max(0, goddess_effect.hit - m_nAdjVALUE[ING_INC_HIT]);
    }

    return val;
}

short
StatusEffects::Adj_CRITICAL() {
    short val = m_nAdjVALUE[ING_INC_CRITICAL] - m_nAdjVALUE[ING_DEC_CRITICAL] + m_nAruaCRITICAL;

    // Goddess buff doesn't stack with other buffs
    if (is_enabled(StatusEffectType::Goddess)) {
        val += max(0, goddess_effect.crit - m_nAdjVALUE[ING_INC_CRITICAL]);
    }

    return val;
}

short
StatusEffects::Adj_AVOID() {
    return m_nAdjVALUE[ING_INC_AVOID] - m_nAdjVALUE[ING_DEC_AVOID];
}

void
StatusEffects::enable_status(StatusEffectType type) {
    StatusEffectFlag flag = set2sef(type);
    is_sub_type(type) ? set_sub_flag(flag) : set_flag(flag);
}

void
StatusEffects::disable_status(StatusEffectType type) {
    StatusEffectFlag flag = set2sef(type);
    is_sub_type(type) ? unset_sub_flag(flag) : unset_flag(flag);
}

bool
StatusEffects::is_enabled(StatusEffectType type) {
    StatusEffectFlag flag = set2sef(type);
    if (is_sub_type(type)) {
        return is_sub_flag_set(flag);
    }

    return is_flag_set(flag);
}

void
StatusEffects::set_flag(StatusEffectFlag flag) {
    m_dwIngStatusFLAG |= static_cast<uint32_t>(flag);
}

void
StatusEffects::unset_flag(StatusEffectFlag flag) {
    m_dwIngStatusFLAG &= ~static_cast<uint32_t>(flag);
}

bool
StatusEffects::is_flag_set(StatusEffectFlag flag) {
    return (m_dwIngStatusFLAG & static_cast<uint32_t>(flag));
}

void
StatusEffects::set_sub_flag(StatusEffectFlag flag) {
    m_dwSubStatusFLAG |= static_cast<uint32_t>(flag);
}

void
StatusEffects::unset_sub_flag(StatusEffectFlag flag) {
    m_dwSubStatusFLAG &= ~static_cast<uint32_t>(flag);
}

bool
StatusEffects::is_sub_flag_set(StatusEffectFlag flag) {
    return (m_dwSubStatusFLAG & static_cast<uint32_t>(flag));
}

bool
StatusEffects::is_sub_flag(StatusEffectFlag flag) {
    switch (flag) {
        case StatusEffectFlag::Hide:
        case StatusEffectFlag::StoreMode:
        case StatusEffectFlag::IntroChat:
        case StatusEffectFlag::Goddess:
        case StatusEffectFlag::AruaFairy:
        case StatusEffectFlag::Invincible:
            return true;
        default:
            return false;
    }
    return false;
}

bool
StatusEffects::is_sub_type(Rose::Common::StatusEffectType type) {
    switch (type) {
        case StatusEffectType::Hide:
        case StatusEffectType::StoreMode:
        case StatusEffectType::IntroChat:
        case StatusEffectType::Goddess:
        case StatusEffectType::AruaFairy:
        case StatusEffectType::Invincible:
            return true;
        default:
            return false;
    }
    return false;
}
