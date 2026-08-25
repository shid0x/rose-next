/**
 * \ingroup SHO_GS
 * \file	CObjCHAR.h
 * \brief	avatar/mob/npc등 케릭터의 기본 클래스로 케릭터 처리
 */
#ifndef __CObjCHAR_H
#define __CObjCHAR_H
#include "CGameOBJ.h"
#include "CCharDATA.h"
#include "rose/io/stb.h"
#include "IO_AI.h"

#include "CInventory.h"
#include "CUserDATA.h"
#include "CObjAI.h"
#include "status_effects.h"

#include "rose/common/pvp_state_types.h"
#include "rose/network/packets/combat_generated.h"

using namespace Rose;
using namespace Rose::Common;

// #define	__CS_TARGET_LIST

enum enumRIDE_ANI {
    PAT_ANI_STOP1 = 0,
    PAT_ANI_MOVE,
    PAT_ANI_ATTACK,
    PAT_ANI_DIE,
    PAT_ANI_CASTING,
    PAT_ANI_SKILL,
};

#define TEAMNO_MOB 100
#define TEAMNO_NPC 1
#define TEAMNO_USER 2

class CRegenPOINT;
class CObjAVT;
class classUSER;
class CParty;
class CGuild;

/**
 * \ingroup SHO_GS_LIB
 * \class	CObjCHAR
 * \author	wookSang.Jo
 * \brief	케릭터 데이터 관리및 처리하는 클래스
 *			인공지능 처리 클래스인 CObjAI를 상속받음
 */
class CObjCHAR: public CObjAI {
public:
    PvpState pvp_state = PvpState::NoPvp;

    uint16_t total_move_speed() override;
    uint16_t total_attack_speed() override;

    uint32_t total_attack_power() override;
    uint32_t total_hit_rate() override;

    bool is_pvp_enabled();

private:
    void Adjust_HEIGHT() { /* nop */
    }

    bool SendPacketToUSER(CObjCHAR* pChar, classPACKET* pCPacket) { return true; }

    bool Send_gsv_STOP();
    bool Send_gsv_MOUSECMD(int iTargetObject);
    bool Send_gsv_ATTACK(int iTargetObject);
    bool Send_gsv_DAMAGE2Sector(int iAttackObject, int wDamage, CObjITEM* pDropITEM);
    bool Send_gsv_DAMAGE2Target(CObjCHAR* pAtkOBJ, int wDamage);
    bool Send_combat_damage_event(CObjCHAR* pAtkOBJ,
        uniDAMAGE sDamage,
        Rose::Network::Packets::DamagePresentationKind presentationKind,
        short nSkillIDX = 0,
        int iSourceObjIDX = 0);
    bool Send_combat_swing(CObjCHAR* pTarget, uniDAMAGE sDamage);

protected:
    // Protected so CObjSUMMON::PlayerOrderMoveTo can broadcast a tolerant
    // (non-IsMovablePOS-gated) player-ordered move.
    bool Send_gsv_MOVE();

    bool Send_gsv_TOGGLE(BYTE btTYPE, bool bUpdateSpeed);
    bool Send_gsv_SELF_SKILL(short nSkillIDX, short nNpcSkillMotion = -1);
    bool Send_gsv_TARGET_SKILL(short nSkillIDX, short nNpcSkillMotion = -1);
    bool Send_gsv_POSITION_SKILL(short nSkillIDX);

    void Init_ADD_CHAR(classPACKET* pCPacket);
    void Add_ADJ_STATUS(classPACKET* pCPacket);

    void Send_gsv_RESULT_OF_SKILL(short nSkillIDX);

    bool Send_gsv_EFFECT_OF_SKILL(int iSpellOBJ, short nSkillIDX, BYTE btResult, short nSpellerINT);
    bool Send_gsv_DAMAGE_OF_SKILL(int iSpellOBJ,
        short nSkillIDX,
        int wDamage,
        BYTE btResult,
        short nSpellerINT,
        CObjITEM* pDropITEM = NULL);
    bool Send_gsv_SET_MOTION(WORD wValue, short nMotionNO);
    bool Send_gsv_CLEAR_STATUS(DWORD dwChangedFLAG);

    float m_fCurAniSPEED;
    uint32_t m_uCombatDefenderSeq;

    BYTE Apply_DAMAGE(CObjCHAR* pTarget,
        uniDAMAGE& sDamage,
        CObjITEM** ppOutITEM = NULL,
        bool bDropItem = false);

public:
    CAI_OBJ* AI_FindFirstOBJ(tPOINTF& PosCENTER, int iDistance);
    CAI_OBJ* AI_FindFirstOBJ(int iDistance);
    CAI_OBJ* AI_FindNextOBJ();

public:
    // nSkillIDX is display metadata for the client damage meter (skill
    // attribution in the DamageEvent packet); it does not affect damage or
    // presentation behavior. 0 = normal attack / unknown.
    void Give_DAMAGE(CObjCHAR* pTarget, uniDAMAGE sDamage, bool bDropItem = true,
        Rose::Network::Packets::DamagePresentationKind kind
        = Rose::Network::Packets::DamagePresentationKind::Immediate,
        short nSkillIDX = 0);
    // nSkillIDX / iSourceObjIDX are meter display metadata: the skill that
    // applied the status and the caster's object index (StatusEffects stores
    // both per slot). 0 = unknown; damage/presentation behavior is unaffected.
    void Give_STATUS_DAMAGE(short nDamage, short nSkillIDX = 0, int iSourceObjIDX = 0);

    // Mirror of the client's CObjCHAR::IsProjectilePresentedSkillDamage. Decides
    // whether a skill's damage event should be presented at projectile-impact
    // time (true) or immediately on packet arrival (false). Server uses this to
    // pick the DamagePresentationKind for SKILL_TYPE_06 magic / ranged skill
    // hits so the client's bullet-impact gating actually fires.
    static bool IsProjectilePresentedSkill(short nSkillIDX);

    bool Send_gsv_CHAT(char* szMsg);
    bool Send_gsv_SHOUT(char* szMsg);
    void Send_gsv_ANNOUNCE_CHAT(char* szMsg);
    bool Send_gsv_ALLIED_CHAT(char* szMsg);
    bool Send_gsv_ALLIED_SHOUT(char* szMsg);

    CObjCHAR();
    virtual ~CObjCHAR();

    union {
        short m_nCharIdx; // NPC, MOB 번호
        short m_nCharRACE; // 아바타: 인간 남자0, 인간 여자 1, ...
    };
    float m_fScale;

    int m_iHP;
    int m_iMP;
    int m_iTeamNO;
    short m_nPosZ;
    StatusEffects m_IngSTATUS;

    // >>>> virtual function ----------------------------------------------------------------------
    virtual void CloseNETWORK() { /* nop */
    }
    virtual bool SendPacketToPARTY(classPACKET* pCPacket) { return true; }
    virtual bool SendPacketIfPARTY(classPACKET* pCPacket) { return true; }
    virtual bool SendPacketToPartyExceptME(classPACKET* pCPacket) { return true; }
    virtual bool SendPacketToPartyExecpNearUSER(classPACKET* pCPacket) { return true; }
    virtual bool SendPacketToTARGET(CObjCHAR* pAtkCHAR, classPACKET* pCPacket) = 0;
    virtual bool Send_gsv_SET_HPnMP(BYTE btApply) { return true; }

    virtual bool Run_EVENT() { return false; }
    virtual bool Check_EVENT(CGameOBJ* pSourOBJ, short nEventIDX = -1) { return false; }

    virtual unsigned long Get_AICheckTIME(int iIDX) { return 0; }
    virtual void Set_AICheckTIME(int iIDX, unsigned long lCheckTIME) { /* nop */
    }

    virtual bool IsFemale() = 0;
    virtual short IsMagicDAMAGE() = 0;

    virtual int GetCallerUsrIDX() { return 0; }
    virtual int GetCallerObjIDX() { return 0; }
    virtual t_HASHKEY GetCallerHASH() { return 0; }
    virtual short GetSummonedSkillIDX() { return 0; }

    virtual tagMOTION* Get_MOTION(short nActionIdx) = 0;

    virtual int Get_HP() { return m_iHP; }
    virtual int Get_MP() { return m_iMP; }

    virtual void Set_HP(int iHP) { m_iHP = iHP; }
    virtual void Set_MP(int iMP) { m_iMP = iMP; }

    virtual int Add_HP(int iAdd) {
        m_iHP += iAdd;
        if (m_iHP > Get_MaxHP())
            m_iHP = Get_MaxHP();
        return m_iHP;
    }
    virtual int Add_MP(int iAdd) {
        m_iMP += iAdd;
        if (m_iMP > Get_MaxMP())
            m_iMP = Get_MaxMP();
        return m_iMP;
    }
    virtual int Sub_HP(int iSub) {
        m_iHP -= iSub;
        if (m_iHP < 0)
            m_iHP = 0;
        return m_iHP;
    }
    virtual int Sub_MP(int iSub) {
        m_iMP -= iSub;
        if (m_iMP < 0)
            m_iMP = 0;
        return m_iMP;
    }

    virtual void Add_STAMINA(short) { /* nop */
    }
    virtual void Add_MONEY(short) { /* nop */
    }

    virtual short Get_WEIGHT() = 0;
    //	virtual short	Get_ItemQUALITY (short nEquipIDX)=0;
    virtual int Get_LEVEL() = 0;
    virtual int Get_GiveEXP() = 0;
    virtual void Add_EXP(int iExp, bool bApplyStamina = true) { ; /* nop */ }
    virtual int Get_AbilityValue(WORD wType) = 0;
    /// Value of a stat *before* duration buffs are folded in. Only percentage
    /// skill effects want this -- taking a percentage of the already-buffed stat
    /// makes the buff compound with itself on every recast. Defaults to the
    /// current value for object types that carry no buffs worth distinguishing.
    virtual int Get_BaseAbilityValue(WORD wType) { return Get_AbilityValue(wType); }

    virtual int Get_EXP() { return 0; }
    virtual int Get_JOB() { return 0; }
    virtual DWORD Get_RIGHT() { return 0; }
    virtual __int64 Get_MONEY() { return 0; }

    virtual int Get_INT() = 0;
    virtual int Get_CHARM() = 0;
    virtual int Get_SENSE() = 0;

    virtual BYTE Get_MoveMODE() = 0;
    virtual BYTE Get_RideMODE() = 0;

    virtual int GetOri_MaxHP() = 0;
    virtual int GetOri_MaxMP() = 0;

    virtual int GetOri_DEF() = 0;
    virtual int GetOri_RES() = 0;
    virtual int GetOri_AVOID() = 0;
    virtual int GetOri_CRITICAL() = 0;

    virtual int Save_Damage(int iAttackerIDX, int iDamage) = 0;
    virtual void Dec_WeaponLife() { /* nop */
    }
    virtual void Dec_EngineLife() { /* nop */
    }

    virtual classUSER* Give_EXP() { return NULL; }

    virtual t_HASHKEY Get_CharHASH() { return 0; }

    virtual void Clear_SummonCNT() { /* nop */
    }
    virtual void Add_SummonCNT(short nV) { /* nop */
    }
    virtual void Sub_SummonCNT(short nV) { /* nop */
    }
    virtual short Get_SummonCNT() { return 0; }
    virtual short Max_SummonCNT() { return 0; }

    virtual void Add_ToTargetLIST(CObjAVT* pAVTChar) { /* nop */
    }
    virtual void Sub_FromTargetLIST(CObjAVT* pAVTChar) { /* nop */
    }

    virtual CParty* GetPARTY() { return NULL; }
    virtual CGuild* GetGUILD() { return NULL; }

    virtual bool Chk_ShotITEM(BYTE btShotCnt) { return true; }
    virtual bool Dec_ShotITEM(BYTE btShotCnt) { return true; }

    virtual void Stamp_AttackTIME() { /* nop */
    }

    virtual bool Dead(CObjCHAR* pKiller);
    virtual int Proc();

    virtual void UpdateCartGuest() { /* nop */
    }
    // <<<< virtual function ----------------------------------------------------------------------

    /// 스킬에서 능력치를 바로 적용...
    bool Skill_IsPassFilter(CObjCHAR* pTarget, short nSkillIDX);
    // BYTE		Skill_IncAbilityValue( short nSkillIDX );
    void Skill_ChangeIngSTATUS(CObjCHAR* pTarget);
    BYTE Skill_ApplyIngSTATUS(short nSkillIDX, CObjCHAR* pSpeller);
    bool Skill_DamageToAROUND(tPOINTF& PosCENTER);

    tPOINTF Get_GotoPOSITION() { return m_PosGOTO; }
    tPOINTF Get_CurPOSITION() { return m_PosCUR; }
    tPOINTF Get_BornPOSITION() { return m_PosBORN; }

    short Get_nAttackSPEED() { return this->total_attack_speed(); }

    int Get_DEF() {
        int iR = GetOri_DEF() + m_IngSTATUS.Adj_DPOWER();
        return (iR > 10) ? iR : 10;
    } // 방어력
    int Get_RES() {
        int iR = GetOri_RES() + m_IngSTATUS.Adj_RES();
        return (iR > 10) ? iR : 10;
    }

    int Get_AVOID() {
        int iR = GetOri_AVOID() + m_IngSTATUS.Adj_AVOID();
        return (iR > 10) ? iR : 10;
    } // 회피력
    int Get_CRITICAL() {
        int iR = GetOri_CRITICAL() + m_IngSTATUS.Adj_CRITICAL();
        return (iR > 10) ? iR : 10;
    }

    // int		Get_MaxHP()				{	return ( GetOri_MaxHP() + m_IngSTATUS.Inc_MAX_HP() );
    // }
    int Get_MaxHP() { return (GetOri_MaxHP() + m_IngSTATUS.Inc_MAX_HP()); }
    int Get_MaxMP() { return (GetOri_MaxMP() + m_IngSTATUS.Inc_MAX_MP()); }

    int GetCurrentFrame() { return m_iCurMotionFRAME; }
    DWORD GetCurAbsSEC();

    void Move(float fSpeed, tPOINTF& Position);
    void Command_MOVE(float fPosX, float fPosY, float fSpeed);
    void ActionEVENT(int iActionIDX);
    int RealProc(DWORD dwPassTIME);
    void Proc_IngSTATUS(DWORD dwPassTime);

public: // virtual function inherit from CAI_OBJ
    t_ObjTAG Get_CharObjTAG() { return this->Get_TYPE(); }
    int Get_EconomyVAR(short nVarIDX);
    int Get_WorldVAR(short nVarIDX);

    void Set_EconomyVAR(short nVarIDX, int iValue);
    void Set_WorldVAR(short nVarIDX, int iValue);

    void Add_DAMAGE(int wDamage) {
        if (this->Get_HP() >= GameStaticConfig::MAX_DAMAGE) {
            this->Set_HP(1);
            wDamage = GameStaticConfig::MAX_DAMAGE;
        }
        uniDAMAGE sDamage;
        sDamage.m_wDamage = wDamage;
        this->Give_DAMAGE(this, sDamage);
    }

    DWORD Get_MagicSTATUS() { return m_IngSTATUS.GetFLAGs(); }
    int Get_TeamNO() { return m_iTeamNO; }
    int Get_TEAM() { return Get_TeamNO(); }

    BYTE Is_DAY(void);

    int Get_ZoneTIME() { return GetCur_ZoneTIME(); }
    int Get_WorldTIME() { return GetCur_WorldTIME(); }

    bool Create_PET(int iCharIDX,
        float fPosX,
        float fPosY,
        int iRange,
        BYTE btSetOwner,
        bool bImmediate);
    CAI_OBJ* Find_LocalNPC(int iNpcNO);

    float Get_SCALE() { return m_fScale; }

    int Get_CharNO() { return m_nCharIdx; }
    int Get_RANDOM(int iMod) { return (rand() % iMod); }

    float Get_CurXPOS() { return m_PosCUR.x; }
    float Get_CurYPOS() { return m_PosCUR.y; }
    float Get_BornXPOS() { return m_PosBORN.x; }
    float Get_BornYPOS() { return m_PosBORN.y; }
    void Special_ATTACK() { /* nop */
        ;
    }

    float Get_MoveDISTANCE() { return distance(m_PosBORN, m_PosCUR); }
    int Get_DISTANCE(tPOINTF& Pos) { return (int)distance(m_PosCUR, Pos); }
    float Get_DISTANCE(CAI_OBJ* pObj) { return distance(m_PosCUR, ((CObjCHAR*)pObj)->m_PosCUR); }

    bool SetCMD_STOP();
    bool SetCMD_MOVE2D(float fPosX, float fPosY, BYTE btRunMODE);
    bool SetCMD_ATTACK(int iTargetObject);
    void SetCMD_RUNnATTACK(int target_id) override;
    bool UsesProjectileAttackPresentation();

public: // virtual function inherit from CObjAI
    bool SetCMD_Skill2SELF(short nSkillIDX);
    bool SetCMD_Skill2OBJ(int iTargetObjIDX, short nSkillIDX);
    bool SetCMD_Skill2POS(tPOINTF& PosGOTO, short nSkillIDX);
    bool Is_TauntSTATUS(int iTargetObject) {
        return this->m_IngSTATUS.IsTauntSTATUS(iTargetObject, this->GetZONE());
    }

    DWORD GetIngDurationStateFLAG() { return m_IngSTATUS.GetFLAGs(); }
    short GetIngDurationStateSKILL(eING_TYPE eTYPE) { return m_IngSTATUS.GetSkillIDX(eTYPE); }

    bool SetCMD_MOVE(tPOINTF& PosCUR, tPOINTF& PosTO, int iTargetObjIDX);

    bool IsInRANGE(CObjCHAR* pT, int iRange) {
        return distance((int)m_PosCUR.x, (int)m_PosCUR.y, (int)pT->m_PosCUR.x, (int)pT->m_PosCUR.y)
            <= iRange;
    }
    bool Set_MOTION(short nActionIdx,
        float fMoveSpeed = 0,
        float fAniSpeed = 1.0f,
        bool bAttackMotion = false);

    void Move_COMPLETED();

    bool Send_gsv_SKILL_CANCEL(BYTE btWhy);

private:
    void Skill_START_03_04_05(CObjCHAR* pTarget);
    void Skill_START_10_11(CObjCHAR* pTarget);
    void Skill_START_19(CObjCHAR* pTarget);

public:
    bool Skill_START(CObjCHAR* pTarget);
    bool Attack_START(CObjCHAR* pTarget);
    bool Casting_START(CObjCHAR* pTarget);
    void Casting_END() { m_bCastingSTART = false; }
};

//-------------------------------------------------------------------------------------------------
#endif
