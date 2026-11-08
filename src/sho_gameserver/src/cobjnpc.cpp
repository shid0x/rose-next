
#include "stdAFX.h"

#include "CObjNPC.h"
#include "GS_ThreadZONE.h"
#include "IO_Quest.h"
#include "ZoneLIST.h"

#include "rose/common/store_item_code.h"

#include <climits>

namespace {

// Level x HP in 64 bits, clamped to int. See CObjMOB::Init.
int
ClampedMobMaxHP(int iCharIDX) {
    const long long llMaxHP = (long long)NPC_LEVEL(iCharIDX) * (long long)NPC_HP(iCharIDX);
    if (llMaxHP > INT_MAX)
        return INT_MAX;
    if (llMaxHP < 1)
        return 1;
    return (int)llMaxHP;
}

// The saved-damage table (per-attacker damage share for EXP and drops) is
// sized HP/8+4 into a short: 40 entries for an ordinary monster, ~1250 for the
// 9999-HP quest bosses. Cap well inside the short.
short
ClampedSavedDamageCNT(int iCharIDX) {
    const int kMaxSavedDamage = 4096;
    int iCnt = NPC_HP(iCharIDX) / 8 + 4;
    if (iCnt > kMaxSavedDamage)
        iCnt = kMaxSavedDamage;
    if (iCnt < 4)
        iCnt = 4;
    return (short)iCnt;
}

} // namespace

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
bool
CObjMOB::Init(CZoneTHREAD* pZONE,
    short nCharIdx,
    float fXPos,
    float fYPos,
    int iTeamNO,
    CRegenPOINT* pRegenPOINT,
    short nQuestIDX) {
    m_pCharMODEL = g_pCharDATA->GetMODEL(nCharIdx);
    if (NULL == m_pCharMODEL)
        return false;
    if (!m_pCharMODEL->IsValid())
        return false;

    CObjTARGET::Set_TargetIDX(0);
    m_IngSTATUS.Reset(true);

    m_iCallerMobObjIDX = 0;
    m_iTeamNO = iTeamNO;

    m_nCharIdx = nCharIdx;
    m_nQuestIDX = nQuestIDX;
    m_pRegenPOINT = pRegenPOINT;

    m_PosBORN.x = fXPos;
    m_PosBORN.y = fYPos;
    m_PosCUR = m_PosBORN;
    m_nPosZ = 0;

    // Age is measured from here rather than from the constructor: these objects
    // are pooled, so a recycled slot would inherit its predecessor's spawn time
    // and could expire the instant it appeared. See AICOND_30.
    this->Set_SpawnTIME(::timeGetTime());

    m_fRunAniSPEED = 1.0f;

    m_nCritical = (short)(NPC_LEVEL(m_nCharIdx) * 0.6f);

    m_fScale = NPC_SCALE(m_nCharIdx) / 100.f;
    // LIST_NPC carries a few junk rows with 7.9-13 million in the HP column
    // (996 Moss Golem, 997 Nepenthes, 998 Turak; the largest real value is
    // 10,701). Level x HP overflowed int to a negative max HP, and HP/8+4 --
    // the saved-damage table size, a short -- wrapped negative, so `new[]`
    // took a size_t of ~4 billion and `/mon 996` killed the process
    // (2026-09-15). A junk row must degrade like a missing one: clamp both.
    m_iOriMaxHP = ClampedMobMaxHP(m_nCharIdx);
    m_iHP = m_iOriMaxHP;

    m_nSavedDamageCNT = ClampedSavedDamageCNT(m_nCharIdx);
    m_SavedDAMAGED = new tagSavedDAMAGE[m_nSavedDamageCNT];
    ::ZeroMemory(m_SavedDAMAGED, sizeof(tagSavedDAMAGE) * m_nSavedDamageCNT);

    ::ZeroMemory(m_iAiVAR, sizeof(int) * MAX_MOB_VAR_CNT);

    this->stats.move_speed = NPC_RUN_SPEED(m_nCharIdx);
    this->stats.attack_speed = NPC_ATK_SPEED(m_nCharIdx);

    this->stats.attack_power = NPC_ATK(m_nCharIdx);
    this->stats.hit_rate = NPC_HIT(m_nCharIdx);
    
    this->pvp_state = pvp_state_from(NPC_PVP_STATE(m_nCharIdx));

    if (NULL == this->GetZONE()) {
        CObjAI::SetCMD_STOP();
        return pZONE->Add_OBJECT(this); // 몹 생성
    }

    // 정지 명령후 전송...
    SetCMD_STOP();

    return true;
}

//-------------------------------------------------------------------------------------------------
int
CObjMOB::Get_AttackRange(short nSkillIDX) {
    /// 스킬에 공격 거리가 입력되어 있다면 스킬거리 아니면 무기 거리..
    if (nSkillIDX && SKILL_DISTANCE(nSkillIDX)) {
        return SKILL_DISTANCE(nSkillIDX);
    }

    // 몹은 입력된 데이타로...
    return (int)(Def_AttackRange() + (this->Get_SCALE() * 120));
}

//-------------------------------------------------------------------------------------------------
bool
CObjMOB::Make_gsv_ADD_OBJECT(classPACKET* pCPacket) {
    if (this->Get_HP() <= 0)
        return false;

    this->Init_ADD_CHAR(pCPacket);

    pCPacket->m_HEADER.m_wType = GSV_MOB_CHAR;
    pCPacket->m_HEADER.m_nSize = sizeof(gsv_MOB_CHAR);

    pCPacket->m_gsv_MOB_CHAR.m_nCharIdx = this->m_nCharIdx;
    pCPacket->m_gsv_MOB_CHAR.m_nQuestIDX = this->m_nQuestIDX;
    pCPacket->m_gsv_MOB_CHAR.m_nPosZ = (short)this->Nav_SendZ();

    this->Add_ADJ_STATUS(pCPacket);

    return true;
}

//-------------------------------------------------------------------------------------------------
CAI_OBJ*
CObjMOB::Get_CALLER() {
    if (this->GetCallerObjIDX()) {
        CObjCHAR* pOwner = g_pObjMGR->Get_CharOBJ(this->GetCallerObjIDX(), true);
        if (pOwner && pOwner->Get_CharHASH() == this->GetCallerHASH())
            return pOwner;
    }
    return NULL;
}

bool
CObjMOB::SetCMD_Skill2SELF(short nSkillIDX, short nMotion) {
    // called from CAI_OBJ..
    if (this->m_IngSTATUS.IsIgnoreSTATUS() || this->m_IngSTATUS.IsSET(FLAG_ING_DUMB))
        return true;

    if (CObjAI::SetCMD_Skill2SELF(nSkillIDX)) {
        m_cSkillMotionIDX = (char)nMotion;
        return this->Send_gsv_SELF_SKILL(nSkillIDX, nMotion);
    }
    return false;
}
bool
CObjMOB::SetCMD_Skill2OBJ(int iTargetObjIDX, short nSkillIDX, short nMotion) {
    // called from CAI_OBJ..
    if (this->m_IngSTATUS.IsIgnoreSTATUS() || this->m_IngSTATUS.IsSET(FLAG_ING_DUMB))
        return true;

    if (CObjAI::SetCMD_Skill2OBJ(iTargetObjIDX, nSkillIDX)) {
        m_cSkillMotionIDX = (char)nMotion;
        return this->Send_gsv_TARGET_SKILL(nSkillIDX, nMotion);
    }
    return false;
}

//-------------------------------------------------------------------------------------------------
bool
CObjMOB::Make_gsv_SUB_OBJECT(classPACKET* pCPacket) {
    if (this->Get_HP() <= 0) {
        // 맞아 죽을경우는 클라이언트에서 알아서 뺄수 있으니까...
        return false;
    }

    return CGameOBJ::Make_gsv_SUB_OBJECT(pCPacket);
}

//-------------------------------------------------------------------------------------------------
bool
CObjMOB::SendPacketToTARGET(CObjCHAR* pAtkCHAR, classPACKET* pCPacket) {
    classDLLNODE<CObjAVT*>* pTargetNODE;

    this->LockTargetLIST();
    {
        pTargetNODE = this->m_TargetLIST.GetHeadNode();
        while (pTargetNODE) {
            pTargetNODE->DATA->SendPacket(pCPacket);
            pTargetNODE = this->m_TargetLIST.GetNextNode(pTargetNODE);
        }
    }
    this->UnlockTargetLIST();

    return true;
}

void
CObjMOB::Add_ToTargetLIST(CObjAVT* pAVTChar) {
    this->LockTargetLIST();
    this->m_TargetLIST.AppendNode(pAVTChar->m_pTargetNODE);
    this->UnlockTargetLIST();
}
void
CObjMOB::Sub_FromTargetLIST(CObjAVT* pAVTChar) {
    this->LockTargetLIST();
    this->m_TargetLIST.DeleteNode(pAVTChar->m_pTargetNODE);
    this->UnlockTargetLIST();
}

// The list holds the targeters' own nodes, so a monster that leaves the zone alive
// (GM npc DEL, the Cerberus sleeper swap, the escort's end) left them linked to a
// freed list, and one that is re-indexed (GM npc CALL, regen CALL) kept them under
// its new index. The player's next attack on it then appended a node already in
// the list: a self-loop that hung the zone thread in this loop when it died.
void
CObjMOB::DetachTargeters() {
    classDLLNODE<CObjAVT*>* pTargetNODE;

    this->LockTargetLIST();
    {
        pTargetNODE = this->m_TargetLIST.GetHeadNode();
        while (pTargetNODE) {
            assert(this->Get_INDEX() == pTargetNODE->DATA->Get_TargetIDX());

            this->m_TargetLIST.DeleteNode(pTargetNODE);
            pTargetNODE->DATA->CObjTARGET::Set_TargetIDX(0);
            pTargetNODE = this->m_TargetLIST.GetHeadNode();
        }
    }
    this->UnlockTargetLIST();
}

bool
CObjMOB::Dead(CObjCHAR* pKiller) {
    if (CObjCHAR::Dead(NULL)) {
        if (this->GetCallerUsrIDX()) {
            // 소환수가 있으면 갯수 감소
            CObjCHAR* pOwner = g_pObjMGR->Get_CharOBJ(this->GetCallerUsrIDX(), true);
            if (pOwner && pOwner->IsUSER()) {
                if (this->GetCallerHASH() == pOwner->Get_CharHASH()) {
                    short nNeedValue = NPC_NEED_SUMMON_CNT(this->Get_CharNO());
                    pOwner->Sub_SummonCNT(nNeedValue);
                    // pOwner->Send_gsv_SET_SUMMON_COUNT (); damage로 날라감...
                }
            }
        }

        this->DetachTargeters();

        return true;
    }

    return false;
}

//-------------------------------------------------------------------------------------------------
#define EXPIRED_DAMAGED_TIME (10 * 1000) // 10 sec
#define IGNORE_DAMAGED_TIME (5 * 60 * 1000) // 5 min
int
CObjMOB::Save_Damage(int iAttackerIDX, int iDamage) {
    short nElderSlot = -1;
    DWORD dwElderTime = 0x0ffffffff;
    DWORD dwCurTIME = (this->GetZONE())->GetCurrentTIME();

    for (short nI = 0; nI < this->m_nSavedDamageCNT; nI++) {
        if (this->m_SavedDAMAGED[nI].m_iObjectIDX == iAttackerIDX) {
            this->m_SavedDAMAGED[nI].m_dwUpdateTIME = dwCurTIME;
            this->m_SavedDAMAGED[nI].m_iDamage += iDamage;
            // Narrowing is safe here: in the damage role the field is bounded by mob HP.
            return static_cast<int>(this->m_SavedDAMAGED[nI].m_iDamage);
        }

        if (dwCurTIME - this->m_SavedDAMAGED[nI].m_dwUpdateTIME >= EXPIRED_DAMAGED_TIME) {
            if (this->m_SavedDAMAGED[nI].m_dwUpdateTIME < dwElderTime) {
                // 가장 오래된 공격자를 삭제..
                dwElderTime = this->m_SavedDAMAGED[nI].m_dwUpdateTIME;
                nElderSlot = nI;
            }
        }
    }

    if (nElderSlot >= 0) {
        this->m_SavedDAMAGED[nElderSlot].m_dwInsertTIME = dwCurTIME;
        this->m_SavedDAMAGED[nElderSlot].m_dwUpdateTIME = dwCurTIME;
        this->m_SavedDAMAGED[nElderSlot].m_iObjectIDX = iAttackerIDX;
        this->m_SavedDAMAGED[nElderSlot].m_iDamage = iDamage;
        return iDamage;
    }

    return 0;
}

int
CObjMOB::Get_AbilityValue(WORD wType) {
    switch (wType) {
            //		case AT_STR		:		return Get_STR ();
            //		case AT_DEX		:		return Get_DEX ();
        case AT_INT:
            return Get_INT();
            //		case AT_CON		:		return Get_CON ();
        case AT_CHARM:
            return Get_CHARM();
        case AT_SENSE:
            return Get_SENSE();
        case AT_HP:
            return Get_HP();
        case AT_MP:
            return Get_MP();
        case AT_ATK:
            return this->total_attack_power();
        case AT_DEF:
            return Get_DEF();
        case AT_HIT:
            return this->total_hit_rate();
        case AT_RES:
            return Get_RES();
        case AT_AVOID:
            return Get_AVOID();
        case AT_SPEED:
            return this->total_move_speed();
        case AT_ATK_SPD:
            return this->total_attack_speed();
        case AT_WEIGHT:
            return Get_WEIGHT();
        case AT_CRITICAL:
            return Get_CRITICAL();

        case AT_LEVEL:
            return Get_LEVEL();
            //		case AT_BONUSPOINT :	return Get_BonusPOINT ();
            //		case AT_SKILLPOINT :	return Get_SkillPOINT ();

        case AT_MAX_HP:
            return Get_MaxHP();
        case AT_MAX_MP:
            return Get_MaxMP();
    }

    return 0;
}

//-------------------------------------------------------------------------------------------------
classUSER*
CObjMOB::Give_EXP() {
    DWORD dwElderTIME = 0x0ffffffff;
    CObjCHAR* pHitterCHAR;
    classUSER *pHitterUSER, *pOwner = NULL;
    // int iDistance;
    bool bPartyUser = false;

    DWORD dwCurTIME = (this->GetZONE())->GetCurrentTIME();
    short nI;
    __int64 iEXP;
    for (nI = 0; nI < this->m_nSavedDamageCNT; nI++) {
        if (this->m_SavedDAMAGED[nI].m_dwInsertTIME) {
            if (dwCurTIME - this->m_SavedDAMAGED[nI].m_dwUpdateTIME > IGNORE_DAMAGED_TIME) {
                // 오래된 누적 데이미지는 경험치 주지 않는다.
                this->m_SavedDAMAGED[nI].m_iObjectIDX = 0;
                continue;
            }

            pHitterCHAR = g_pObjMGR->Get_CharOBJ(this->m_SavedDAMAGED[nI].m_iObjectIDX, true);
            if (NULL == pHitterCHAR /* || !pHitterCHAR->IsUSER() */) { // 05.02.17 부분 주석처리..
                this->m_SavedDAMAGED[nI].m_iObjectIDX = 0;
                continue;
            }
            pHitterUSER = (classUSER*)pHitterCHAR->Get_CALLER();

            // if ( !pHitterCHAR->IsUSER() ) {
            //	// 사용자에 의해 소환된 NPC냐 ????
            //	pHitterUSER = g_pObjMGR->Get_UserOBJ( pHitterCHAR->GetOwnerOBJ() );
            //	this->m_SavedDAMAGED[ nI ].m_iObjectIDX = pHitterCHAR->GetOwnerOBJ();
            //} else
            //	pHitterUSER = (classUSER*)pHitterCHAR;

            if (pHitterUSER && pHitterUSER->GetZONE() == this->GetZONE()) {
                this->m_SavedDAMAGED[nI].m_iObjectIDX =
                    pHitterUSER->Get_INDEX(); // 2004. 9. 30 추가...
                if (dwCurTIME - this->m_SavedDAMAGED[nI].m_dwUpdateTIME < EXPIRED_DAMAGED_TIME
                    && this->m_SavedDAMAGED[nI].m_dwInsertTIME < dwElderTIME) {
                    // EXPIRED_DAMAGED_TIME이 지나지 않은 유저중 먼저 때린넘으로..
                    dwElderTIME = this->m_SavedDAMAGED[nI].m_dwInsertTIME;
                    pOwner = pHitterUSER;
                }

                iEXP = CCal::Get_EXP(pHitterUSER, this, this->m_SavedDAMAGED[nI].m_iDamage);
                // if ( !pHitterCHAR->IsUSER() ) {
                //	// 소환 NPC로 부터 얻은 경치는 50% ...
                //	nEXP /= 2;
                //}

                if (pHitterUSER->GetPARTY()) {
                    this->m_SavedDAMAGED[nI].m_iDamage = iEXP;
                    bPartyUser = true;
                    continue;
                }
                if (iEXP > 0) {
                    // 파티 없으면 바로 경험치 올림.
                    pHitterUSER->Add_EXP(iEXP, true, this->Get_INDEX());
                }
            }

            this->m_SavedDAMAGED[nI].m_iObjectIDX = 0;
        }
    }

    // 파티 경험치 분배...
    if (bPartyUser) {
        classUSER* pPartyUSER;
        short nJ;

        for (nI = 0; nI < this->m_nSavedDamageCNT; nI++) {
            if (this->m_SavedDAMAGED[nI].m_iObjectIDX && this->m_SavedDAMAGED[nI].m_dwInsertTIME) {
                pHitterUSER = g_pObjMGR->Get_UserOBJ(this->m_SavedDAMAGED[nI].m_iObjectIDX);
                if (!pHitterUSER
                    || !pHitterUSER->GetPARTY()) // 0x00061abb 오류... 소환몹일경우 뻑나나???
                    continue;

                for (nJ = nI + 1; nJ < this->m_nSavedDamageCNT; nJ++) {
                    if (this->m_SavedDAMAGED[nJ].m_iObjectIDX
                        && this->m_SavedDAMAGED[nJ].m_dwInsertTIME) {
                        pPartyUSER = g_pObjMGR->Get_UserOBJ(this->m_SavedDAMAGED[nJ].m_iObjectIDX);
                        if (pPartyUSER && pHitterUSER->GetPARTY() == pPartyUSER->GetPARTY()) {
                            // 같은 파티원의 경험치를 몰아서 얻는다.
                            this->m_SavedDAMAGED[nI].m_iDamage +=
                                this->m_SavedDAMAGED[nJ].m_iDamage;
                            this->m_SavedDAMAGED[nJ].m_iObjectIDX = 0;
                        }
                    }
                }

                // 누적된 경험치를 파티를 통해 전송.
                if (!pHitterUSER->m_pPartyBUFF->AddEXP(this,
                        this->m_SavedDAMAGED[nI].m_iDamage,
                        ZONE_PARTY_EXP_A(this->GetZONE()->Get_ZoneNO()),
                        ZONE_PARTY_EXP_B(this->GetZONE()->Get_ZoneNO()))) {
                    // 파티 경험치 적용안됐다.
                    pHitterUSER->Add_EXP(this->m_SavedDAMAGED[nI].m_iDamage,
                        true,
                        this->Get_INDEX());
                }
            }
        }
    }

    return pOwner;
}

//-------------------------------------------------------------------------------------------------
void
CObjMOB::Run_AWAY(int iDistance) {
    float fX, fY;

    fX = (float)(RANDOM(iDistance * 2) - iDistance);
    fY = (float)(RANDOM(iDistance * 2) - iDistance);
    if (m_pRegenPOINT) {
        // 리젠 포인트를 중심으로 iDistance만큼 이동...
        fX += m_pRegenPOINT->m_fXPos;
        fY += m_pRegenPOINT->m_fYPos;
    } else {
        // 소환된 몹이다..
        fX += m_PosBORN.x;
        fY += m_PosBORN.y;
    }

    // With a walk grid a few more points are tried; none in a straight line, the monster
    // walks a path home (Phase 5), or straight there: the leash must always fire, or the
    // monster chases forever.
    if (Nav::Grid* pGrid = this->GetZONE()->NavGrid()) {
        bool bMoved = this->SetCMD_MOVE2D(fX, fY, true);
        for (int iTry = 0; iTry < 5 && !bMoved; ++iTry) {
            fX = (m_pRegenPOINT ? m_pRegenPOINT->m_fXPos : m_PosBORN.x) + (RANDOM(iDistance * 2) - iDistance);
            fY = (m_pRegenPOINT ? m_pRegenPOINT->m_fYPos : m_PosBORN.y) + (RANDOM(iDistance * 2) - iDistance);
            bMoved = this->SetCMD_MOVE2D(fX, fY, true);
        }
        if (!bMoved) {
            const float fHomeX = m_pRegenPOINT ? m_pRegenPOINT->m_fXPos : m_PosBORN.x;
            const float fHomeY = m_pRegenPOINT ? m_pRegenPOINT->m_fYPos : m_PosBORN.y;
            bMoved = this->Nav_MoveOrStraight(fHomeX, fHomeY, true, pGrid->AnchorZ(fHomeX, fHomeY));
        }
        if (bMoved) {
            this->Set_COMMAND(CMD_RUNAWAY);
        }
        return;
    }
    if (this->SetCMD_MOVE2D(fX, fY, true))
        this->Set_COMMAND(CMD_RUNAWAY);
    else
        this->GetZONE()->m_NavMeter.CountLeashFail(); // the leash point is not walkable
}

//-------------------------------------------------------------------------------------------------
void
CObjMOB::Drop_ITEM(short nDropITEM, BYTE btToOwner) {
    tagITEM sITEM;
    sITEM.Init(nDropITEM);

    // 아이템 수치가 맞게 입력되어 있는가 ??
    if (sITEM.GetTYPE() && sITEM.GetItemNO()) {
        CObjITEM* pObjITEM = new CObjITEM;
        if (pObjITEM) {
            tPOINTF DropPOS = this->m_PosCUR;
            DropPOS.x += (RANDOM(1001) - 500); // 랜덤 5미터..
            DropPOS.y += (RANDOM(1001) - 500);

            if (btToOwner) {
                DWORD dwElderTIME = 0x0ffffffff;
                CObjCHAR* pHitterCHAR;
                classUSER *pHitterUSER, *pMobOWNER = NULL;
                DWORD dwCurTIME = (this->GetZONE())->GetCurrentTIME();
                for (short nI = 0; nI < this->m_nSavedDamageCNT; nI++) {
                    if (this->m_SavedDAMAGED[nI].m_dwInsertTIME) {
                        if (dwCurTIME - this->m_SavedDAMAGED[nI].m_dwUpdateTIME
                            > IGNORE_DAMAGED_TIME) {
                            // 오래된 누적 데이미지는 경험치 주지 않는다.
                            // this->m_SavedDAMAGED[ nI ].m_iObjectIDX = 0;
                            continue;
                        }
                        pHitterCHAR =
                            g_pObjMGR->Get_CharOBJ(this->m_SavedDAMAGED[nI].m_iObjectIDX, true);
                        if (NULL == pHitterCHAR /* || !pHitterCHAR->IsUSER() */) {
                            // this->m_SavedDAMAGED[ nI ].m_iObjectIDX = 0;
                            continue;
                        }
                        pHitterUSER = (classUSER*)pHitterCHAR->Get_CALLER();
                        if (pHitterUSER && pHitterUSER->GetZONE() == this->GetZONE()) {
                            // this->m_SavedDAMAGED[ nI ].m_iObjectIDX = pHitterUSER->Get_INDEX();
                            // // 2004. 9. 30 추가...
                            if (dwCurTIME - this->m_SavedDAMAGED[nI].m_dwUpdateTIME
                                    < EXPIRED_DAMAGED_TIME
                                && this->m_SavedDAMAGED[nI].m_dwInsertTIME < dwElderTIME) {
                                // EXPIRED_DAMAGED_TIME이 지나지 않은 유저중 먼저 때린넘으로..
                                dwElderTIME = this->m_SavedDAMAGED[nI].m_dwInsertTIME;
                                pMobOWNER = pHitterUSER;
                            }
                        }
                    }
                }
                if (pMobOWNER)
                    pObjITEM->InitItemOBJ(NULL,
                        DropPOS,
                        this->m_PosSECTOR,
                        sITEM,
                        pMobOWNER,
                        true,
                        pMobOWNER->GetPARTY()); // 죽을때 떨굼
                else
                    pObjITEM->InitItemOBJ(NULL, DropPOS, this->m_PosSECTOR, sITEM);
            } else
                pObjITEM->InitItemOBJ(NULL, DropPOS, this->m_PosSECTOR, sITEM);
            pObjITEM->m_fFloorZ = this->Nav_FloorAt(DropPOS.x, DropPOS.y); // the dropper's floor (Phase 6)

            this->GetZONE()->Add_DIRECT(pObjITEM); // AI에서 떨굼 아이템
            /*
                if ( sITEM.m_cType != ITEM_TYPE_MONEY )
                    LogString( LOG_NORMAL, "Zone: %d :: 아이템 %s 드롭 Type: %d, NO: %d \n",
               pObjITEM->GetZONE()->Get_ZoneNO(), ITEM_NAME( sITEM.m_cType, sITEM.m_nItemNo ),
               sITEM.m_cType, sITEM.m_nItemNo ); else LogString( LOG_NORMAL, "Zone: %d :: 돈 드롭 %d
               \n", pObjITEM->GetZONE()->Get_ZoneNO(), sITEM.m_nItemNo);
            */
        }
    }
}

bool
CObjMOB::Change_CHAR(int iCharIDX) {
    /// Validate before anything is broadcast. This is reached straight from AI
    /// action 9 with a WORD out of the .aip and nothing in between, so a monster
    /// id that is missing from LIST_NPC -- Karkia's kak_ghost_cemetery.aip asks
    /// for 2734-2738, none of which were ever imported -- used to be sent to every
    /// client in the sector and applied to this object as well. The client then
    /// tore its model down, failed to build the new one, and kept a modelless
    /// character alive until zone teardown corrupted the heap on it.
    ///
    /// The same guard CZoneTHREAD::RegenCharacter uses: get_cstr returns nullptr
    /// for a blank cell, so a NULL name is exactly "this row does not exist".
    if (iCharIDX < 1 || (size_t)iCharIDX >= g_TblNPC.row_count || !NPC_NAME(iCharIDX)) {
        LOG_WARN("Change_CHAR to unknown npc {} refused (obj {}, zone {})",
            iCharIDX,
            (int)this->Get_INDEX(),
            this->GetZONE() ? this->GetZONE()->Get_ZoneNO() : -1);
        return false;
    }

    /// The new form starts at its own full HP, and the clients have to be told:
    /// the packet carries it, because the client cannot derive it (it would have
    /// to know this is a fresh body and not the old one with a new model) and the
    /// only other HP it hears about is the next hit's hp_after. So the state is
    /// switched first and the packet built from it.
    // 이거만 해도 될려나 ???
    // 모델 데이타 !!!! 안해서 뻑~~~
    this->m_nCharIdx = iCharIDX;
    this->m_pCharMODEL = g_pCharDATA->GetMODEL(iCharIDX);
    this->m_fScale = NPC_SCALE(iCharIDX) / 100.f;
    this->m_iOriMaxHP = ClampedMobMaxHP(iCharIDX);
    this->m_iHP = m_iOriMaxHP;

    /// Level, DEF, RES and AVOID read the table through m_nCharIdx, but these are
    /// cached by Init, so the new form fought with the old one's. Every Ghost Seed
    /// is ATK 1 / HIT 1: the Candle and Yigore Ghosts they turn into landed 6-8%
    /// of their swings at any dodge, and for 5 damage.
    this->m_nCritical = (short)(NPC_LEVEL(iCharIDX) * 0.6f);
    this->stats.attack_speed = NPC_ATK_SPEED(iCharIDX);
    this->stats.attack_power = NPC_ATK(iCharIDX);
    this->stats.hit_rate = NPC_HIT(iCharIDX);
    this->update_speed();

    classPACKET* pCPacket = Packet_AllocNLock();
    if (!pCPacket)
        return false;

    pCPacket->m_HEADER.m_wType = GSV_CHANGE_NPC;
    pCPacket->m_HEADER.m_nSize = sizeof(gsv_CHANGE_NPC);
    pCPacket->m_gsv_CHANGE_NPC.m_wObjectIDX = this->Get_INDEX();
    pCPacket->m_gsv_CHANGE_NPC.m_nNPCNo = iCharIDX;
    pCPacket->m_gsv_CHANGE_NPC.m_iHP = this->m_iHP;

    this->GetZONE()->SendPacketToSectors(this, pCPacket);

    Packet_ReleaseNUnlock(pCPacket);

    return true;
}
void
CObjMOB::Set_EMOTION(short nEmotionIDX) {
    if (!this->Send_gsv_SET_MOTION((WORD)BIT_MOTION_STOP_CMD, nEmotionIDX))
        return;

    this->Set_MOTION(nEmotionIDX, m_fCurMoveSpeed);
}

void
CObjMOB::update_speed() {
    if (this->m_bRunMODE == MOVE_MODE_WALK) {
        this->stats.move_speed = NPC_WALK_SPEED(this->m_nCharIdx);
    } else {
        this->stats.move_speed = NPC_RUN_SPEED(this->m_nCharIdx);
    }
}

int
CObjMOB::Proc() {
    if (this->Get_HP() <= 0 && CMD_DIE != this->Get_COMMAND()) {
        // HP가 DEAD_HP인넘이 CMD가 STOP이되어 리제포인트에서 빠지지 않아
        // 리젠이 되지 않는경우가 있다... 어떤 경우냐 ???
        LogString(0xffff,
            ">>Warnning:: DeadMOB( %s ), Zone%d:%s, HP: %d, CMD: %x\n",
            this->Get_NAME(),
            this->GetZONE()->Get_ZoneNO(),
            this->GetZONE()->Get_NAME(),
            this->Get_HP(),
            this->Get_COMMAND());

        this->Set_COMMAND(CMD_DIE);
    }

    return CObjCHAR::Proc();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void
CObjSUMMON::SetCallerOBJ(int iOwnerOBJ,
    t_HASHKEY HashOwnerOBJ,
    int iOwnerLEVEL,
    short nSkillIDX,
    CObjCHAR* pOwnerAVT) {
    m_iCallerUserObjIDX = iOwnerOBJ;
    m_HashCALLER = HashOwnerOBJ;

    m_iLevel = iOwnerLEVEL;
    m_nSummonedSkillIDX = nSkillIDX;

    short nSkillLEV = SKILL_LEVEL(nSkillIDX);

    m_iOriMaxHP = (int)(NPC_HP(m_nCharIdx) * (nSkillLEV + 16) * (iOwnerLEVEL + 85) / 2600.f);
    m_iOriDEF = (int)(CObjMOB::GetOri_DEF() * (nSkillLEV + 30) * (iOwnerLEVEL + 80) / 4400.f);
    m_iOriRES = (int)(CObjMOB::GetOri_RES() * (nSkillLEV + 24) * (iOwnerLEVEL + 90) / 3600.f);
    m_iOriAVOID = (int)(CObjMOB::GetOri_AVOID() * (nSkillLEV + 22) * (iOwnerLEVEL + 90) / 3400.f);
    this->Set_HP(m_iOriMaxHP);

    this->stats.attack_power = CObjMOB::total_attack_power() * (nSkillLEV + 22) * (iOwnerLEVEL + 100) / 4000.f;
    this->stats.hit_rate = CObjMOB::total_hit_rate() * (nSkillLEV + 30) * (iOwnerLEVEL + 50) / 3200.f;
}

// Stationary summons (Muse Bonfire, future totems/turrets, etc.) are flagged
// in the NPC table by having both walking and running speed set to 0. They
// should not be pulled toward the owner by the follow logic and must not have
// their move_speed silently upgraded to the owner's speed.
static inline bool
IsStationarySummonNpc(short nCharIdx) {
    return NPC_WALK_SPEED(nCharIdx) <= 0 && NPC_RUN_SPEED(nCharIdx) <= 0;
}

bool
CObjSUMMON::IsStationary() {
    return IsStationarySummonNpc(this->m_nCharIdx);
}

bool
CObjSUMMON::PlayerOrderMoveTo(float fXPos, float fYPos) {
    // Same as CObjCHAR::SetCMD_MOVE2D but WITHOUT its walk check, so
    // the summon honors player CTRL+click moves onto cells the server marks
    // non-walkable. The player clicked valid-looking terrain and the avatar's own
    // click-to-move has no such gate, so the summon should behave the same.
    // CObjAI::SetCMD_MOVE2D sets the run/destination state; Send_gsv_MOVE
    // broadcasts it to nearby clients so the move is actually shown.
    // Round what is in the way when the straight line is blocked (Phase 5), to the walkable
    // floor nearest the click within 6 m (the packet has no height: the floor there nearest
    // the summon's). The order is never refused: no path, it goes straight as before.
    if (this->Nav_Pathing() && !this->Nav_CanWalkTo(fXPos, fYPos)
        && this->Nav_PathTo(fXPos, fYPos, Nav::NO_Z, 1 /* run */, 6)) {
        return true;
    }
    if (CObjAI::SetCMD_MOVE2D(fXPos, fYPos, 1 /* run */)) {
        return this->Send_gsv_MOVE();
    }
    return false;
}

void
CObjSUMMON::update_speed() {
    // Stationary summons keep the base 0 speed from the NPC table.
    if (IsStationarySummonNpc(this->m_nCharIdx)) {
        CObjMOB::update_speed();
        return;
    }

    // Summons scale a bit with owner speed so they can still make progress
    // catching up, but are intentionally slower than the owner — so they
    // lag visibly and feel like companions rather than glued shadows.
    CObjCHAR* pOwner = (CObjCHAR*)this->Get_CALLER();
    if (pOwner && this->m_bRunMODE != MOVE_MODE_WALK) {
        int iOwnerSpeed = (int)(pOwner->total_move_speed() * 0.85f);
        int iBaseSpeed = NPC_RUN_SPEED(this->m_nCharIdx);
        this->stats.move_speed = (uint16_t)(iOwnerSpeed > iBaseSpeed ? iOwnerSpeed : iBaseSpeed);
        return;
    }
    CObjMOB::update_speed();
}

int
CObjSUMMON::Proc() {
    // Active continuous follow: re-issue the move command every FOLLOW_TICK_MS
    // whether the summon is currently idle OR already moving. Previously the
    // summon committed to a destination and ran toward where the owner WAS,
    // only re-checking once it arrived — producing laggy, unreactive following.
    const ULONG FOLLOW_TICK_MS     = 300;   // how often to re-plan
    const float FOLLOW_DISTANCE    = 350.0f;// desired resting distance
    const float FOLLOW_TRIGGER     = 550.0f;// start moving when farther than this

    // Auto-despawn for stationary summons (e.g. Bonfire). Mobile summons follow
    // the owner so they can't get "left behind"; stationary ones stay put and
    // the player walks away. Once the player is well outside the aura they
    // should be unsummoned so the summon counter / summon UI clear. Sectors
    // are 4000 units across; despawn well inside the outer broadcast ring so
    // the death packet still flows through the normal sector path while the
    // bonfire is visible to the owner.
    const float STATIONARY_DESPAWN_DISTANCE = 3000.0f;

    CAI_OBJ* pOwner = this->Get_CALLER();

    // Owner-validity / owner-death despawn applies to ALL summons (mobile and
    // stationary). Summons share the fate of their summoner: when the owner
    // dies, the phantom swords / companions must die with them rather than keep
    // fighting while the player is a ghost or has respawned. The distance-based
    // despawn below remains stationary-only (mobile summons follow the owner and
    // so can't be "left behind").
    if (this->Get_HP() > 0) {
        CObjCHAR* pOwnerChar = (CObjCHAR*)pOwner;
        bool bDespawn = false;
        if (!pOwnerChar
            || pOwnerChar->Get_CharHASH() != this->GetCallerHASH()
            || pOwnerChar->GetZONE() != this->GetZONE()
            || pOwnerChar->Get_HP() <= DEAD_HP) {
            // Owner is gone, replaced, in a different zone, or dead. Detect death
            // via HP (== DEAD_HP), not FLAG_ING_FAINTING — that flag is shared by
            // stun and death, so it would wrongly despawn summons on a mere stun.
            bDespawn = true;
        } else if (IsStationarySummonNpc(this->m_nCharIdx)) {
            float dx = this->Get_CurXPOS() - pOwnerChar->Get_CurXPOS();
            float dy = this->Get_CurYPOS() - pOwnerChar->Get_CurYPOS();
            if (dx * dx + dy * dy
                > STATIONARY_DESPAWN_DISTANCE * STATIONARY_DESPAWN_DISTANCE) {
                bDespawn = true;
            }
        }

        if (bDespawn && this->Get_HP() > 0) {
            // Send the legacy gsv_DAMAGE death packet directly — a despawn is
            // not a combat kill, so no Give_DAMAGE runs and no FlatBuffer
            // CombatSwing/DamageEvent is emitted (combat kills decrement the
            // client's m_SummonedMobList via their lethal flag and are
            // mirrored to out-of-range owners; despawns get neither without
            // this synthetic packet). attacker=0 is
            // load-bearing: it routes the client through Recv_gsv_DAMAGE's
            // missing-attacker branch which calls PresentImmediateCombatDamage
            // right away. A real attacker (e.g. self) would queue the death
            // for a hit-frame consumer that never arrives for a synthetic
            // despawn, leaving the bonfire visually alive after the counter
            // already cleared. SubSummonedMob still fires at the top of
            // Recv_gsv_DAMAGE because DMG_BIT_DEAD is set.
            classPACKET* pPacket = Packet_AllocNLock();
            if (pPacket) {
                pPacket->m_HEADER.m_wType = GSV_DAMAGE;
                pPacket->m_HEADER.m_nSize = sizeof(gsv_DAMAGE);
                pPacket->m_gsv_DAMAGE.m_wAtkObjIDX = 0;
                pPacket->m_gsv_DAMAGE.m_wDefObjIDX = this->Get_INDEX();
                pPacket->m_gsv_DAMAGE.m_Damage.m_wDamage = DMG_BIT_DEAD;

                this->GetZONE()->SendPacketToSectors(this, pPacket);

                // Always mirror to the owner so the summon UI updates
                // even if they have already walked out of the bonfire's
                // broadcast neighborhood.
                if (pOwnerChar && pOwnerChar->IsUSER()) {
                    pOwnerChar->SendPacket(pPacket);
                }
                Packet_ReleaseNUnlock(pPacket);
            }

            this->Dead(this);
            return CObjCHAR::Proc();
        }
    }

    // Stationary summons (NPC walk+run speed == 0) skip the follow loop entirely.
    // Their AI script still runs through CObjCHAR::Proc() below, so pulsing
    // heal/aura behavior (e.g. Bonfire via SUR_FIRE1.AIP) is unaffected.
    if (pOwner && !this->Get_TARGET() && !IsStationarySummonNpc(this->m_nCharIdx)
        && !this->IsManualOrderActive()) {
        WORD wCmd = this->Get_COMMAND();
        if (wCmd == CMD_STOP || wCmd == CMD_MOVE) {
            ULONG ulNow = ::timeGetTime();
            if (ulNow - this->Get_AICheckTIME(1) >= FOLLOW_TICK_MS) {
                this->Set_AICheckTIME(1, ulNow);

                float dx = this->Get_CurXPOS() - ((CObjCHAR*)pOwner)->Get_CurXPOS();
                float dy = this->Get_CurYPOS() - ((CObjCHAR*)pOwner)->Get_CurYPOS();
                float fDist = sqrtf(dx * dx + dy * dy);

                if (fDist > FOLLOW_TRIGGER) {
                    float fPosX, fPosY;
                    if (fDist < 1.0f) {
                        fPosX = ((CObjCHAR*)pOwner)->Get_CurXPOS();
                        fPosY = ((CObjCHAR*)pOwner)->Get_CurYPOS();
                    } else {
                        fPosX = ((CObjCHAR*)pOwner)->Get_CurXPOS() + (dx / fDist) * FOLLOW_DISTANCE;
                        fPosY = ((CObjCHAR*)pOwner)->Get_CurYPOS() + (dy / fDist) * FOLLOW_DISTANCE;
                    }
                    // Fresh destination every tick → summon tracks owner continuously; a
                    // blocked line takes a path to the owner's floor, else straight (a
                    // summon must never be stranded)
                    this->Nav_MoveOrStraight(fPosX, fPosY, 1 /* run */, ((CObjCHAR*)pOwner)->Nav_WalkZ());
                }
            }
        }
    }

    return CObjCHAR::Proc();
}

CObjNPC::CObjNPC() {
    // TODO:: Get npc data from db ...
    m_bShow = true;
}
CObjNPC::~CObjNPC() {
    // TODO:: Set npc data to db

    // Drop ourselves from the quest-system NPC lookup; see
    // CZoneLIST::Add_LocalNPC. Zone-file NPCs live for the whole process, but a
    // GM `/npc add` spawn can be destroyed, and COND_013 would otherwise hand
    // out a dangling pointer as the NPC variable record.
    if (g_pZoneLIST)
        g_pZoneLIST->Del_LocalNPC(this);
}

//-------------------------------------------------------------------------------------------------
short
CObjNPC::VGetCur_ZoneNO() {
    if (!this->GetZONE())
        return 0;
    return this->GetZONE()->Get_ZoneNO();
}

//-------------------------------------------------------------------------------------------------
bool
CObjNPC::Send_gsv_SET_EVENT_STATUS() {
    classPACKET* pCPacket = Packet_AllocNLock();
    if (!pCPacket)
        return false;

    pCPacket->m_HEADER.m_wType = GSV_SET_EVENT_STATUS;
    pCPacket->m_HEADER.m_nSize = sizeof(gsv_SET_EVENT_STATUS);
    pCPacket->m_gsv_SET_EVENT_STATUS.m_wObjectIDX = this->Get_INDEX();
    pCPacket->m_gsv_SET_EVENT_STATUS.m_nEventSTATUS = this->m_nEventSTATUS;

    this->GetZONE()->SendPacketToSectors(this, pCPacket);

    Packet_ReleaseNUnlock(pCPacket);

    return true;
}
//-------------------------------------------------------------------------------------------------
bool
CObjNPC::Make_gsv_ADD_OBJECT(classPACKET* pCPacket) {
    if (this->Get_HP() <= 0)
        return false;

    this->Init_ADD_CHAR(pCPacket);

    pCPacket->m_HEADER.m_wType = GSV_NPC_CHAR;
    pCPacket->m_HEADER.m_nSize = sizeof(gsv_NPC_CHAR);

    if (this->m_bShow)
        pCPacket->m_gsv_NPC_CHAR.m_nCharIdx = this->m_nCharIdx;
    else
        pCPacket->m_gsv_NPC_CHAR.m_nCharIdx = -this->m_nCharIdx;

    pCPacket->m_gsv_NPC_CHAR.m_nQuestIDX = this->m_nQuestIDX;
    pCPacket->m_gsv_NPC_CHAR.m_nPosZ = (short)this->Nav_SendZ();
    pCPacket->m_gsv_NPC_CHAR.m_fModelDIR = this->m_fModelDIR;

    pCPacket->m_gsv_NPC_CHAR.m_nEventSTATUS = this->m_nEventSTATUS;

    this->Add_ADJ_STATUS(pCPacket);

    return true;
}

//-------------------------------------------------------------------------------------------------
bool
CObjNPC::Get_SellITEM(short nSellTAB, short nSellCOL, tagITEM& OutITEM) {
    short nListSellROW;

    nListSellROW = NPC_SELL_TAB(this->Get_CharNO(), nSellTAB);
    if (0 == nListSellROW || nListSellROW >= g_TblStore.row_count)
        return false;

    if (nSellCOL < 0 || nSellCOL + 2 >= g_TblStore.col_count)
        return false;

    // int, not short: the wide form of a slot (Rose::Store) runs past 32767, and a
    // short here would silently truncate it into a different item.
    const int iSellITEM = STORE_ITEM(nListSellROW, nSellCOL);
    int iType = 0, iNo = 0;
    if (!Rose::Store::decode_store_item(iSellITEM, iType, iNo))
        return false;

    OutITEM.Init(static_cast<unsigned int>(iType), static_cast<unsigned int>(iNo));

    return true;
}

//-------------------------------------------------------------------------------------------------
void
CObjNPC::VSet_SHOW(BYTE btShowMode) {
    switch (btShowMode) {
        case 0: // 숨기기
            if (!this->m_bShow)
                return;
            this->m_bShow = false;
            break;
        case 1: // 보이기
            if (this->m_bShow)
                return;
            this->m_bShow = true;
            break;
        case 2: // 토글
            if (this->m_bShow) {
                this->m_bShow = false;
            } else {
                this->m_bShow = true;
            }
            break;
    }

    classPACKET* pCPacket = Packet_AllocNLock();
    if (pCPacket) {
        pCPacket->m_HEADER.m_wType = GSV_SET_NPC_SHOW;
        pCPacket->m_HEADER.m_nSize = sizeof(gsv_SET_NPC_SHOW);

        pCPacket->m_gsv_SET_NPC_SHOW.m_wObjectIDX = this->Get_INDEX();
        pCPacket->m_gsv_SET_NPC_SHOW.m_bShow = this->m_bShow;

        this->GetZONE()->SendPacketToSectors(this, pCPacket);

        Packet_ReleaseNUnlock(pCPacket);
    }
}

//-------------------------------------------------------------------------------------------------
int
CObjNPC::Proc() {
    if (CObjVAR::ProcVAR((this->GetZONE())->GetPassTIME())) {
        // 다음 이벤트 처리...
        // m_HashCurrentTrigger = m_HashNextTrigger;
        g_QuestList.CheckQUEST(NULL, m_HashNextTrigger, true, 0, this);
        m_HashNextTrigger = 0;
    }

    return CObjCHAR::Proc();
}

//-------------------------------------------------------------------------------------------------
