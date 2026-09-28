#include "stdafx.h"

#include ".\ciconskill.h"
#include "../SlotContainer/CSkillSlot.h"
#include "../../Object.h"
#include "../../GameCommon/Skill.h"
#include "../Dlgs/SubClass/CSlot.h"
#include "../interfacetype.h"
#include "../IO_ImageRes.h"
#include "../../GameProc/SkillCommandDelay.h"

namespace {

/// LIST_STATUS col 3: 1 = harmful to the one it lands on (stun, slow, Taunt), 2 = a buff's
/// own drawback (Berserk's Defense loss, Endure's slow), 0 = helpful.
bool
IsHarmfulStatus(int iStateNo) {
    return iStateNo && STATE_PRIFITS_LOSSES(iStateNo) == 1;
}

/// The tooltip's "Type". Types 9, 12 and 13 are "a lasting status on someone" whether it helps
/// or hurts (Support and Freezing are both type 9), so they are named by their status instead.
const char*
SkillCategory(int iSkillNo) {
    switch (SKILL_TYPE(iSkillNo)) {
        case SKILL_ACTION_TARGET_BOUND_DURATION: /// 9
        case SKILL_ACTION_SELF_STATE_DURATION: /// 12
        case SKILL_ACTION_TARGET_STATE_DURATION: /// 13
            if (IsHarmfulStatus(SKILL_STATE_STB1(iSkillNo))
                || IsHarmfulStatus(SKILL_STATE_STB2(iSkillNo)))
                return "Debuff";
            return "Buff";
        default:
            return CStringManager::GetSingleton().GetSkillType(SKILL_TYPE(iSkillNo));
    }
}

} // namespace

//----------------------------------------------------------------------------------------------------
/// Class CIconSkill
/// @brief	Skill 객체에 대한 View Class
//----------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Constructor
//----------------------------------------------------------------------------------------------------
CIconSkill::CIconSkill(void) {}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Constructor
//----------------------------------------------------------------------------------------------------
CIconSkill::CIconSkill(int iSkillSlot) {
    SetSkillSlotToIcon(iSkillSlot);
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Destructor
//----------------------------------------------------------------------------------------------------

CIconSkill::~CIconSkill(void) {}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Overrided from CIcon
/// @modify CSlot에서 위치를 가져오지 않고 CSlot::MoveWindow에서 바꿔주는 Position을 사용한다. nAvy
/// 2004/5/20
//----------------------------------------------------------------------------------------------------

void
CIconSkill::Draw() {

    /// get skill.
    CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
    CSkill* pSkill = pSkillSlot->GetSkill(m_iSkillSlot);

    if (pSkill) {
        pSkill->DrawIcon(m_ptPosition.x, m_ptPosition.y);
    }
}

//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Overrided from CIcon
//----------------------------------------------------------------------------------------------------

void
CIconSkill::ExecuteCommand() {

    /// get skill.
    CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
    CSkill* pSkill = pSkillSlot->GetSkill(m_iSkillSlot);

    if (pSkill) {
        if (pSkill->IsEnable())
            pSkill->Execute();
        else
            g_itMGR.AppendChatMsg(STR_CANT_CASTING_STATE, IT_MGR::CHAT_TYPE_SYSTEM);
    }
}
//----------------------------------------------------------------------------------------------------
/// @param
/// @brief Overrided from CIcon
//----------------------------------------------------------------------------------------------------
CIcon*
CIconSkill::Clone() {
    return new CIconSkill(m_iSkillSlot);
}
const char*
CIconSkill::GetName() {
    CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
    int iSkillIndex = pSkillSlot->GetSkillIndex(m_iSkillSlot);

    if (iSkillIndex >= 1 && iSkillIndex <= g_SkillList.Get_SkillCNT())
        return SKILL_NAME(iSkillIndex);

    return NULL;
}

void
CIconSkill::GetToolTip(CInfo& ToolTip, DWORD dwDialogType, DWORD dwType) {

    switch (dwDialogType) {
        case DLG_TYPE_QUICKBAR:
        case DLG_TYPE_QUICKBAR_EXT:
            dwType = INFO_STATUS_FEW;
            if (GetAsyncKeyState(VK_RBUTTON) < 0) {
                dwType = INFO_STATUS_SIMPLE;
            }
            break;
        default:
            if (GetAsyncKeyState(VK_RBUTTON) < 0) {
                dwType = INFO_STATUS_DETAIL;
            }
            break;
    }

    // SIZE sizeString = {0,0};

    int iWidth = 0;
    int iHeight = 0;

    int iSkillNo = GetSkillIndex();
    int iSkillLv = GetSkillLevel();

    if (dwType & INFO_STATUS_FEW) {
        AddSkillName(iSkillNo, ToolTip);
        AddSkillUseProperty(iSkillNo, ToolTip);
    } else {
        switch (SKILL_TYPE(iSkillNo)) {
            case SKILL_BASE_ACTION: /// 1
            {
                AddSkillName(iSkillNo, ToolTip, false);

                if ((dwType & INFO_STATUS_SIMPLE) || (dwType & INFO_STATUS_DETAIL))
                    AddSkillTypeTarget(iSkillNo, ToolTip);

                if (dwType & INFO_STATUS_DETAIL)
                    AddSkillDesc(iSkillNo, ToolTip);

                break;
            }
            case SKILL_CREATE_WINDOW: /// 2
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip, false);
                AddSkillUseProperty(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);

                if (dwType & INFO_STATUS_DETAIL) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
                break;
            }
            case SKILL_ACTION_IMMEDIATE: /// 3,				///< 근접 즉시 발동.( 즉시 모션 교체 )
            case SKILL_ACTION_ENFORCE_WEAPON: /// 4,		///< 무기상태 변경( 강화, 효과 연출(
                                              /// 정령탄? ) )
            case SKILL_ACTION_ENFORCE_BULLET: /// 5,		///< 강화총알 변경 발사. ( 아이스
                                              /// 애로우.. 실제 화살이 변하는.. )
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillPower(iSkillNo, ToolTip);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);

                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillPower(iNextLevelNo, ToolTip);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }

                break;
            }
            case SKILL_ACTION_FIRE_BULLET: /// 6,			///< 발사.( 파이어볼 )
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillPower(iSkillNo, ToolTip);
                AddSkillDistanceScope(iSkillNo, ToolTip, false);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);

                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillPower(iNextLevelNo, ToolTip);
                        AddSkillDistanceScope(iNextLevelNo, ToolTip, false);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
                break;
            }
            case SKILL_ACTION_AREA_TARGET: ///= 7,			///< 지역 공격마법( 당근 범위.. )
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillPower(iSkillNo, ToolTip);
                AddSkillDistanceScope(iSkillNo, ToolTip, true);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);
                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillPower(iNextLevelNo, ToolTip);
                        AddSkillDistanceScope(iNextLevelNo, ToolTip, true);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
                break;
            }
            case SKILL_ACTION_SELF_BOUND_DURATION: ///=8
            case SKILL_ACTION_SELF_STATE_DURATION: /// = 12,		///< 자신에게 발동 지속 마법.(
                                                   /// 캐스팅 유 ) 상태관련
            {
                AddSkillName(iSkillNo, ToolTip);

                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillScope(iSkillNo, ToolTip);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);

                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillScope(iNextLevelNo, ToolTip);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
                break;
            }
            case SKILL_ACTION_TARGET_BOUND_DURATION: /// = 9,	///< 타겟에게 발동 지속 마법.(
                                                     /// 캐스팅 유 ) 능력치
            case SKILL_ACTION_TARGET_STATE_DURATION: /// = 13,	///< 상대에게 발동 지속 마법.(
                                                     /// 캐스팅 유 ) 상태관련
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillDistanceScope(iSkillNo, ToolTip, true);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);
                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillDistanceScope(iNextLevelNo, ToolTip, true);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }

                break;
            }
            case SKILL_ACTION_SELF_BOUND: /// = 10,			///< 자신에게 발동 바로 업 마법.( 캐스팅
                                          /// 유 ) 능력치
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillScope(iSkillNo, ToolTip);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);
                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillScope(iNextLevelNo, ToolTip);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
                break;
            }
            case SKILL_ACTION_TARGET_BOUND: /// = 11,			///< 타겟에게 발동 바로 업 마법.(
                                            /// 캐스팅 유 ) 능력치
            {

                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillDistanceScope(iSkillNo, ToolTip, true);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);
                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillDistanceScope(iNextLevelNo, ToolTip, true);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
                break;
            }
            case SKILL_ACTION_SUMMON_PET: /// = 14,				///< 팻 소환 스킬
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip, false);
                AddSkillUseProperty(iSkillNo, ToolTip);
                ///필요 소환량
                if (int iNpcNo = SKILL_SUMMON_PET(iSkillNo))
                    ToolTip.AddString(CStr::Printf("%s: %d",
                        STR_REQUIRE_SUMMONQUANTITY,
                        NPC_NEED_SUMMON_CNT(iNpcNo)));

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);
                if (dwType & INFO_STATUS_DETAIL) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);                        AddSkillUseProperty(iNextLevelNo, ToolTip);

                        if (int iNpcNo = SKILL_SUMMON_PET(iNextLevelNo))
                            ToolTip.AddString(CStr::Printf("%s: %d",
                                STR_REQUIRE_SUMMONQUANTITY,
                                NPC_NEED_SUMMON_CNT(iNpcNo)));

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
                break;
            }
            case SKILL_ACTION_PASSIVE: /// = 15,
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip, false);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);
                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
                break;
            }

            case SKILL_EMOTION_ACTION: /// = 16
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip, false);
                AddSkillUseProperty(iSkillNo, ToolTip);
                break;
            }
            case SKILL_ACTION_SELF_DAMAGE: /// = 17
            {

                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillPower(iSkillNo, ToolTip);
                AddSkillScope(iSkillNo, ToolTip);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);
                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillPower(iNextLevelNo, ToolTip);
                        AddSkillScope(iNextLevelNo, ToolTip);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }

                break;
            }
            case SKILL_ACTION_SELF_AND_TARGET: /// 19번
            {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillPower(iSkillNo, ToolTip);
                AddSkillSuction(iSkillNo, ToolTip);
                AddSkillStatus(iSkillNo, ToolTip);

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);
                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillPower(iNextLevelNo, ToolTip);
                        AddSkillSuction(iNextLevelNo, ToolTip);
                        AddSkillStatus(iNextLevelNo, ToolTip);

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
                break;
            }
            case SKILL_ACTION_RESURRECTION: {
                AddSkillName(iSkillNo, ToolTip);
                AddSkillTypeTarget(iSkillNo, ToolTip);
                AddSkillUseProperty(iSkillNo, ToolTip);
                AddSkillDistanceScope(iSkillNo, ToolTip, true);
                ToolTip.AddString(
                    CStr::Printf("%s: %d%%", STR_RECOVERY_EXP, SKILL_POWER(iSkillNo)));
                // AddSkillStatus( iSkillNo, ToolTip );

                AddSkillRequireJob(iSkillNo, ToolTip);
                AddSkillRequireAbility(iSkillNo, ToolTip);
                AddSkillRequireSkill(iSkillNo, ToolTip);
                AddSkillRequireSkillPoint2Learn(iSkillNo, ToolTip);
                AddSkillRequireEquip(iSkillNo, ToolTip);
                if ((dwType & INFO_STATUS_DETAIL)) {
                    AddSkillDesc(iSkillNo, ToolTip);
                    int iNextLevelNo = 0;
                    if (iNextLevelNo = GetSkillNextLevelNo(iSkillNo)) {
                        AddSkillNextLevelTitle(iNextLevelNo, ToolTip);
                        AddSkillUseProperty(iNextLevelNo, ToolTip);
                        AddSkillDistanceScope(iNextLevelNo, ToolTip, true);
                        ToolTip.AddString(
                            CStr::Printf("%s: %d%%", STR_RECOVERY_EXP, SKILL_POWER(iNextLevelNo)));
                        // AddSkillStatus( iNextLevelNo, ToolTip );

                        AddSkillRequireJob(iNextLevelNo, ToolTip);
                        AddSkillRequireAbility(iNextLevelNo, ToolTip);
                        AddSkillRequireSkill(iNextLevelNo, ToolTip);
                        AddSkillRequireSkillPoint(iSkillNo, ToolTip);
                    }
                }
            } break;
            default: {
                AddSkillName(iSkillNo, ToolTip);
                ToolTip.AddString("New Type Skill");
                AddSkillDesc(iSkillNo, ToolTip);
            } break;
        }
    }
}

///*---------------------------------------------------------------------------------------------/
/// 스킬 툴팁 관련
void
CIconSkill::AddSkillTypeTarget(int iSkillNo, CInfo& ToolTip, bool bAddTarget) {
    char* pszBuf = NULL;
    if (bAddTarget)
        pszBuf = CStr::Printf("%s: %s    %s: %s",
            STR_ITEM_TYPE,
            SkillCategory(iSkillNo),
            STR_TARGET,
            CStringManager::GetSingleton().GetSkillTarget(SKILL_CLASS_FILTER(iSkillNo)));
    else
        pszBuf = CStr::Printf("%s: %s", STR_ITEM_TYPE, SkillCategory(iSkillNo));

    ToolTip.AddString(pszBuf);
}
void
CIconSkill::AddSkillSummon(int iSkillNo, CInfo& ToolTip) {
    char* pszBuf = CStr::Printf("[%s:%s(%d)]",
        STR_SUMMON_MOB,
        NPC_NAME(SKILL_SUMMON_PET(iSkillNo)),
        SKILL_LEVEL(iSkillNo));
    ToolTip.AddString(pszBuf, g_dwBlueToolTip);
}

void
CIconSkill::AddSkillDistanceScope(int iSkillNo, CInfo& ToolTip, bool bAddScope) {
    ///거리가 0일경우는 표시하지 않는다.
    char* pszBuf;
    int iDistance = SKILL_DISTANCE(iSkillNo) / 100;
    int iScope = SKILL_SCOPE(iSkillNo) / 100;

    if (bAddScope) {
        if (iDistance && iScope) {
            pszBuf = CStr::Printf("%s: %d m    %s: %d m",
                STR_SHOOT_RANGE,
                iDistance,
                STR_APPLY_RANGE,
                iScope);
            ToolTip.AddString(pszBuf);
        } else if (iDistance) {
            pszBuf = CStr::Printf("%s: %d m", STR_SHOOT_RANGE, iDistance);
            ToolTip.AddString(pszBuf);
        } else if (iScope) {
            pszBuf = CStr::Printf("%s: %d m", STR_APPLY_RANGE, iScope);
            ToolTip.AddString(pszBuf);
        }
    } else {
        if (iDistance) {
            pszBuf = CStr::Printf("%s: %d m", STR_SHOOT_RANGE, iDistance);
            ToolTip.AddString(pszBuf);
        }
    }
}
void
CIconSkill::AddSkillScope(int iSkillNo, CInfo& ToolTip) {
    if (SKILL_SCOPE(iSkillNo)) {
        char* pszBuf = CStr::Printf("%s: %d m", STR_APPLY_RANGE, SKILL_SCOPE(iSkillNo) / 100);
        ToolTip.AddString(pszBuf);
    }
}
// void CIconSkill::AddSkillIncreaseAbility( int iSkillNo, CInfo& ToolTip )
//{
//	char* pszBuf;
//	for( int i = 0; i < SKILL_INCREASE_ABILITY_CNT; ++i )
//	{
//		if( SKILL_INCREASE_ABILITY( iSkillNo, i ) )
//		{
//			int iAbility = SKILL_INCREASE_ABILITY( iSkillNo, i );
//			int iValue   = SKILL_INCREASE_ABILITY_VALUE( iSkillNo, i );
//			pszBuf = CStr::Printf("[%s:%s %d]
//",STR_EFFECT,CStringManager::GetSingleton().GetAbility( iAbility ), iValue );
//
//			ToolTip.AddString( pszBuf, g_dwBlueToolTip );
//		}
//	}
//}

void
CIconSkill::AddSkillName(int iSkillNo, CInfo& ToolTip, bool bAddLevel) {
    if (bAddLevel) {
        char* pszBuf =
            CStr::Printf("%s (%s %d)", SKILL_NAME(iSkillNo), STR_LEVEL, SKILL_LEVEL(iSkillNo));
        ToolTip.AddString(pszBuf, g_dwYELLOW, g_GameDATA.m_hFONT[FONT_NORMAL_BOLD]);
    } else {
        ToolTip.AddString(SKILL_NAME(iSkillNo), g_dwYELLOW, g_GameDATA.m_hFONT[FONT_NORMAL_BOLD]);
    }
}

void
CIconSkill::AddSkillDuration(int iSkillNo, CInfo& ToolTip, DWORD color) {
    char* pszBuf = CStr::Printf("%s:%d%s", STR_CONTINUE_TIME, SKILL_DURATION(iSkillNo), STR_SECOND);
    ToolTip.AddString(pszBuf, color);
}

int
CIconSkill::GetSkillNextLevelNo(int iSkillNo) {

    if (iSkillNo >= g_SkillList.Get_SkillCNT()) ///최대값
        return 0;

    if (SKILL_1LEV_INDEX(iSkillNo) == SKILL_1LEV_INDEX(iSkillNo + 1)) {
        if (SKILL_LEVEL(iSkillNo + 1) == SKILL_LEVEL(iSkillNo) + 1)
            return iSkillNo + 1;
    }
    return 0;
}

void
CIconSkill::AddSkillNextLevelTitle(int iSkillNo, CInfo& ToolTip) {
    ToolTip.AddString(" ");
    char* pszBuf = CStr::Printf("%s (%s %d)", STR_NEXT_LEVEL, STR_LEVEL, SKILL_LEVEL(iSkillNo));
    ToolTip.AddString(pszBuf, g_dwYELLOW, g_GameDATA.m_hFONT[FONT_NORMAL_BOLD]);
}

void
CIconSkill::AddSkillPower(int iSkillNo, CInfo& ToolTip) {
    /// "Power: 560 (Weapon Attack)" -- the damage formula CCal::Get_SkillDAMAGE uses
    const char* pszFormula = NULL;
    switch (SKILL_DAMAGE_TYPE(iSkillNo)) {
        case 0:
            pszFormula = STR_SKILLPOWER_EFFECT_0;
            break;
        case 1:
            pszFormula = STR_SKILLPOWER_EFFECT_1;
            break;
        case 2:
            pszFormula = STR_SKILLPOWER_EFFECT_2;
            break;
        case 3:
            pszFormula = STR_SKILLPOWER_EFFECT_3;
            break;
        default:
            break;
    }

    if (pszFormula)
        ToolTip.AddString(
            CStr::Printf("%s: %d (%s)", STR_SKILL_POWER, SKILL_POWER(iSkillNo), pszFormula));
    else
        ToolTip.AddString(CStr::Printf("%s: %d", STR_SKILL_POWER, SKILL_POWER(iSkillNo)));
}

void
CIconSkill::AddSkillDesc(int iSkillNo, CInfo& ToolTip) {
    const char* pszDesc = SKILL_DESC(iSkillNo);
    if (pszDesc == NULL || pszDesc[0] == '\0')
        return;

    ToolTip.AddString(" ");
    ///박스의 최종 너비에 맞춰 단어 단위로 줄바꿈된다
    ToolTip.AddWrappedString(pszDesc);
}
void
CIconSkill::AddSkillUseProperty(int iSkillNo, CInfo& ToolTip) {
    char* pszBuf;
    DWORD dwColor = 0;
    int iUseValue = 0;
    for (int i = 0; i < SKILL_USE_PROPERTY_CNT; ++i) {
        if (SKILL_USE_PROPERTY(iSkillNo, i)) {
            iUseValue = g_pAVATAR->Skill_ToUseAbilityVALUE(iSkillNo, i);

            pszBuf = CStr::Printf("[%s: %s %d]",
                STR_CONSUME_ABILITY,
                CStringManager::GetSingleton().GetAbility(SKILL_USE_PROPERTY(iSkillNo, i)),
                iUseValue);

            if (g_pAVATAR->GetCur_AbilityValue(SKILL_USE_PROPERTY(iSkillNo, i)) >= iUseValue)
                dwColor = g_dwGREEN;
            else
                dwColor = g_dwRED;
            ToolTip.AddString(pszBuf, dwColor);
        }
    }
}

void
CIconSkill::AddSkillRequireEquip(int iSkillNo, CInfo& ToolTip) {
    std::string strTemp("[");
    strTemp.append(STR_REQUIRE_EQUIP);
    strTemp.append(": ");

    int iCount = 0;
    DWORD dwColor = g_dwRED;
    const char* pszChar;
    tagITEM EquipItem;

    for (int i = 0; i < SKILL_NEED_WEAPON_CNT; ++i) {
        if (SKILL_NEED_WEAPON(iSkillNo, i)) ///아이템 타입이다
        {
            for (int iEquipIdx = 0; iEquipIdx < MAX_EQUIP_IDX; ++iEquipIdx) {
                EquipItem = g_pAVATAR->m_Inventory.m_ItemEQUIP[iEquipIdx];
                if (!EquipItem.IsEmpty()) {
                    if (ITEM_TYPE(EquipItem.GetTYPE(), EquipItem.GetItemNO())
                        == SKILL_NEED_WEAPON(iSkillNo, i)) {
                        dwColor = g_dwGREEN;
                        break;
                    }
                }
            }

            pszChar = CStringManager::GetSingleton().GetItemType(SKILL_NEED_WEAPON(iSkillNo, i));

            if (pszChar) {
                if (iCount)
                    strTemp.append(", ");
                strTemp.append(pszChar);
                ++iCount;
            }
        }
    }
    if (iCount) {
        strTemp.append("]");
        ToolTip.AddString(strTemp.c_str(), dwColor);
    }
}

void
CIconSkill::AddSkillRequireUnion(int iSkillNo, CInfo& ToolTip) {
    std::string strTemp("[조합:");

    DWORD dwColor = g_dwRED;
    int iCheckCount = 0;
    bool bCorrect = false;

    for (int i = 0; i < SKILL_AVAILBLE_UNION_CNT; i++) {
        if (0 != SKILL_AVAILBLE_UNION(iSkillNo, i)) {
            ++iCheckCount;
            strTemp.append(" ");
            strTemp.append(UNION_NAME(SKILL_AVAILBLE_UNION(iSkillNo, i)));

            if (SKILL_AVAILBLE_UNION(iSkillNo, i) == g_pAVATAR->GetCur_UNION()) {
                bCorrect = true;
                break;
            }
        }
    }

    strTemp.append("]");

    if (iCheckCount) {
        if (bCorrect)
            dwColor = g_dwGREEN;

        ToolTip.AddString(strTemp.c_str(), dwColor);
    }
}

void
CIconSkill::AddSkillRequireJob(int iSkillNo, CInfo& ToolTip) {
    int iClass = SKILL_AVAILBLE_CLASS_SET(iSkillNo);
    /// REMARK FOR TEST<- 2004.3.18.nAvy LIST_SKILL.STB가 아직 수정안되있으므로 해서 임시적으로 막는
    /// 코드
    if (iClass >= g_TblClass.row_count)
        return;
    ///->
    if (iClass) {
        const char* pszTemp = CStr::Printf("[%s: %s]", STR_REQUIRE_JOB, CLASS_NAME(iClass));
        DWORD color = g_dwRED;

        int iJob = g_pAVATAR->Get_JOB();

        if (iJob) {
            for (int i = 0; i < CLASS_INCLUDE_JOB_CNT; ++i) {
                if (iJob == CLASS_INCLUDE_JOB(iClass, i)) {
                    color = g_dwGREEN;
                    break;
                }
            }
        }
        ToolTip.AddString(pszTemp, color);
    }
}

void
CIconSkill::AddSkillRequireSkill(int iSkillNo, CInfo& ToolTip) {
    std::string strTemp("[");

    strTemp.append(STR_REQUIRE_SKILL);
    strTemp.append(": ");
    DWORD dwColor = g_dwGREEN;

    int iCount = 0;
    int iLearnSkillLv = 0;
    for (int i = 0; i < SKILL_NEED_SKILL_CNT; ++i) {
        if (SKILL_NEED_SKILL_INDEX(iSkillNo, i)) {
            if (iCount)
                strTemp.append(", ");
            strTemp.append(SKILL_NAME(
                SKILL_NEED_SKILL_INDEX(iSkillNo, i) + SKILL_NEDD_SKILL_LEVEL(iSkillNo, i) - 1));
            strTemp.append(CStr::Printf(" (%s %d)",
                CStringManager::GetSingleton().GetAbility(AT_LEVEL),
                SKILL_NEDD_SKILL_LEVEL(iSkillNo, i)));
            ++iCount;

            iLearnSkillLv = g_pAVATAR->Skill_FindLearnedLevel(SKILL_NEED_SKILL_INDEX(iSkillNo, i));
            if (iLearnSkillLv < SKILL_NEDD_SKILL_LEVEL(iSkillNo, i))
                dwColor = g_dwRED;
        }
    }
    if (iCount) {
        strTemp.append("]");
        ToolTip.AddString(strTemp.c_str(), dwColor);
    }
}

void
CIconSkill::AddSkillSuction(int iSkillNo, CInfo& ToolTip) {
    if (SKILL_INCREASE_ABILITY(iSkillNo, 0)) {
        int iAbility = SKILL_INCREASE_ABILITY(iSkillNo, 0);
        /// What a hit gives you: the server scales it by the attacker's INT (Skill_START_19)
        int iValue =
            SKILL_INCREASE_ABILITY_VALUE(iSkillNo, 0) * (g_pAVATAR->Get_INT() + 300) / 315;

        ToolTip.AddString(CStr::Printf("%s: %s %d",
                              STR_ABSORPTION,
                              CStringManager::GetSingleton().GetAbility(iAbility),
                              iValue),
            g_dwBlueToolTip);
    }
}

void
CIconSkill::AddSkillRequireAbility(int iSkillNo, CInfo& ToolTip) {
    std::string strTemp("[");
    strTemp.append(STR_REQUIRE_ABILITY);
    strTemp.append(": ");

    char* pszBuf;

    int iCount = 0;
    DWORD dwColor = g_dwRED;
    for (int i = 0; i < SKILL_NEED_ABILITY_TYPE_CNT; ++i) {
        if (SKILL_NEED_ABILITY_TYPE(iSkillNo, i)) {
            if (iCount)
                strTemp.append(", ");
            pszBuf = CStr::Printf("%s %d",
                CStringManager::GetSingleton().GetAbility(SKILL_NEED_ABILITY_TYPE(iSkillNo, i)),
                SKILL_NEED_ABILITY_VALUE(iSkillNo, i));
            strTemp.append(pszBuf);
            ++iCount;
            if (g_pAVATAR->GetCur_AbilityValue(SKILL_NEED_ABILITY_TYPE(iSkillNo, i))
                >= SKILL_NEED_ABILITY_VALUE(iSkillNo, i))
                dwColor = g_dwGREEN;
        }
    }
    if (iCount) {
        strTemp.append("]");
        ToolTip.AddString(strTemp.c_str(), dwColor);
    }
}
bool
CIconSkill::GetSkillIncreaseAbility(int iSkillNo,
    int iColumn,
    std::string& strOut,
    bool bAddTypeName) {
    if (iColumn >= SKILL_INCREASE_ABILITY_CNT)
        return false;

    if (SKILL_INCREASE_ABILITY(iSkillNo, iColumn) == 0)
        return false;

    strOut.erase(strOut.begin(), strOut.end());

    if (bAddTypeName) {
        strOut.append(
            CStringManager::GetSingleton().GetAbility(SKILL_INCREASE_ABILITY(iSkillNo, iColumn)));
        strOut.append(" ");
    }

    if (SKILL_INCREASE_ABILITY_VALUE(iSkillNo, iColumn)) {
        int iIncreaseAbilityValue = 0;
        switch (SKILL_TYPE(iSkillNo)) {
            case 8:
            case 9:
            case 10:
            case 11:
                iIncreaseAbilityValue = SKILL_INCREASE_ABILITY_VALUE(iSkillNo, iColumn)
                    * (g_pAVATAR->Get_INT() + 300) / 315;
                break;
            default:
                iIncreaseAbilityValue = SKILL_INCREASE_ABILITY_VALUE(iSkillNo, iColumn);
                break;
        }

        if (SKILL_INCREASE_ABILITY(iSkillNo, iColumn) == AT_PSV_SAVE_MP)
            strOut.append(CStr::Printf("%d%%", iIncreaseAbilityValue));
        else
            strOut.append(CStr::Printf("%d", iIncreaseAbilityValue));

        if (SKILL_CHANGE_ABILITY_RATE(iSkillNo, iColumn))
            strOut.append(" ");
    }

    if (SKILL_CHANGE_ABILITY_RATE(iSkillNo, iColumn))
        strOut.append(CStr::Printf("%d%%", SKILL_CHANGE_ABILITY_RATE(iSkillNo, iColumn)));

    return true;
}
void
CIconSkill::AddSkillStatus(int iSkillNo, CInfo& ToolTip) {
    /// One line per status slot, each paired with the ability slot of the same index:
    /// "Effect: Slow Run (53%)", "Effect: Fainted", and with no status the bare stat --
    /// "Bonus: Max HP 59%" on a passive, "Effect: HP 700" on a heal.
    std::string strHeader(
        SKILL_TYPE(iSkillNo) == SKILL_ACTION_PASSIVE ? STR_CHANGE_ABILITY : STR_STATE);
    strHeader.append(": ");

    std::string strTemp;
    std::string strOut;

    for (int i = 0; i < 2; ++i) { /// SKILL_STATE_STB has two slots, cols 11-12
        int iStateNo = SKILL_STATE_STB(iSkillNo, i);
        strTemp = strHeader;
        if (iStateNo) {
            strTemp.append(STATE_NAME(iStateNo));
            if (STATE_TYPE(iStateNo) == ING_DUMMY_DAMAGE)
                strTemp.append(CStr::Printf(" (%d%%)", SKILL_POWER(iSkillNo)));
            else if (GetSkillIncreaseAbility(iSkillNo, i, strOut, false))
                strTemp.append(" (" + strOut + ")");
            ToolTip.AddString(strTemp.c_str(), g_dwBlueToolTip);
        } else if (i == 0 && SKILL_TYPE(iSkillNo) == SKILL_ACTION_SELF_AND_TARGET) {
            /// A drain skill's slot 0 is what it drains, already printed by AddSkillSuction
            /// ("Drains: HP 500"); printing it here too read as a second, separate heal.
            continue;
        } else if (GetSkillIncreaseAbility(iSkillNo, i, strOut, true)) {
            strTemp.append(strOut);
            ToolTip.AddString(strTemp.c_str(), g_dwBlueToolTip);
        }
    }

    if (SKILL_STATE_STB(iSkillNo, 0) || SKILL_STATE_STB(iSkillNo, 1))
        AddSkillSuccessRateDuration(iSkillNo, ToolTip);
}

void
CIconSkill::AddSkillSuccessRateDuration(int iSkillNo, CInfo& ToolTip) {
    if (SKILL_DURATION(iSkillNo) > 0)
        ToolTip.AddString(
            CStr::Printf("%s: %d %s", STR_CONTINUE_TIME, SKILL_DURATION(iSkillNo), STR_SECOND));

    /// The server's roll (CObjCHAR::Skill_ApplyIngSTATUS). A column of 0 always lands. A harmful
    /// status (LIST_STATUS col 3 non-zero) lands when
    ///     ratio * (2 * caster level + INT + 20) / (target RES * 0.6 + AVOID + 5) > 1..100,
    /// so the column is a base that the caster's level and INT raise and the target resists; a
    /// helpful one when ratio >= target level - caster level + 1..100, i.e. about ratio %.
    /// This line used to print 80-100% of the column, which matched neither.
    int iRatio = SKILL_SUCCESS_RATIO(iSkillNo);
    if (iRatio <= 0)
        return;

    bool bHarmful = false;
    for (int i = 0; i < 2; ++i) {
        int iStateNo = SKILL_STATE_STB(iSkillNo, i);
        if (iStateNo && STATE_PRIFITS_LOSSES(iStateNo))
            bHarmful = true;
    }

    if (bHarmful)
        ToolTip.AddString(CStr::Printf("%s: %d base (vs. %s, %s)",
            STR_SUCCESS_RATE,
            iRatio,
            CStringManager::GetSingleton().GetAbility(AT_RES),
            CStringManager::GetSingleton().GetAbility(AT_AVOID)));
    else if (iRatio < 100)
        ToolTip.AddString(CStr::Printf("%s: about %d%%", STR_SUCCESS_RATE, iRatio));
}

void
CIconSkill::AddSkillRequireSkillPoint(int iSkillNo, CInfo& ToolTip) {
    int iNeedPoint = GetNeedPoint4LevelUp(iSkillNo);

    char* pszBuf = CStr::Printf("[%s: %d]", STR_REQUIRE_SKILLPOINT, iNeedPoint);

    if (g_pAVATAR->GetCur_SkillPOINT() >= iNeedPoint)
        ToolTip.AddString(pszBuf, g_dwGREEN);
    else
        ToolTip.AddString(pszBuf, g_dwRED);
}

int
CIconSkill::GetNeedPoint4LevelUp(int iSkillNo) {

    int iNextLevelSkillIDX = iSkillNo + 1;

    if (iNextLevelSkillIDX >= g_SkillList.Get_SkillCNT()) {
        // 더이상 레벨업 할수 없다.
        return 0;
    }

    // 같은 종류의 스킬이고 배우려는 레벨이 현재 레벨의 다음 레벨인가 ??
    if (SKILL_1LEV_INDEX(iSkillNo) != SKILL_1LEV_INDEX(iNextLevelSkillIDX)
        || SKILL_LEVEL(iSkillNo) + 1 != SKILL_LEVEL(iNextLevelSkillIDX)) {
        return 0;
    }

    // TODO:: 여기서 skill stb의 컬럼에 있는 값을 전송...
    return SKILL_NEED_LEVELUPPOINT(iNextLevelSkillIDX);
}

int
CIconSkill::GetSkillLevel() {
    CSkill* pSkill = GetSkill();
    if (pSkill)
        return pSkill->GetSkillLevel();

    return 0;
}

int
CIconSkill::GetSkillIndex() {
    CSkill* pSkill = GetSkill();
    if (pSkill)
        return pSkill->GetSkillIndex();

    return 0;
}

CSkill*
CIconSkill::GetSkill() {
    CSkillSlot* pSkillSlot = g_pAVATAR->GetSkillSlot();
    return pSkillSlot->GetSkill(m_iSkillSlot);
}

void
CIconSkill::AddSkillRequireSkillPoint2Learn(int iSkillNo, CInfo& ToolTip) {

    int iNeedPoint = SKILL_NEED_LEVELUPPOINT(iSkillNo);
    if (iNeedPoint && SKILL_TAB_TYPE(iSkillNo) != 3) ///필요포인트가 있고, 클랜스킬이 아닐경우에만(
                                                     ///클랜스킬일경우 다른 데이타가 들어간다 )
    {
        char* pszBuf = CStr::Printf("[%s: %d]", STR_REQUIRE_SKILLPOINT, iNeedPoint);

        if (g_pAVATAR->GetCur_SkillPOINT() >= iNeedPoint)
            ToolTip.AddString(pszBuf, g_dwGREEN);
        else
            ToolTip.AddString(pszBuf, g_dwRED);
    }
}

int
CIconSkill::GetIndex() {
    return GetSkillSlotFromIcon();
}

/// Mirrors CSkill::DrawIcon: the skill's own reload, and over it the global
/// casting delay CSkillCommandDelay draws on every skill icon.
bool
CIconSkill::GetSprite(int& iModuleID, int& iGraphicID) {
    CSkill* pSkill = GetSkill();
    if (pSkill == NULL)
        return false;
    iModuleID = IMAGE_RES_SKILL_ICON;
    iGraphicID = SKILL_ICON_NO(pSkill->GetSkillIndex());
    return true;
}

float
CIconSkill::GetCooldown(int* piRemainMs) {
    if (piRemainMs)
        *piRemainMs = 0;

    CSkill* pSkill = GetSkill();
    if (pSkill == NULL)
        return 0.0f;

    float fRate = 0.0f;
    const int iDelay = pSkill->GetSkillDelayTime();
    const int iTotal = SKILL_RELOAD_TIME(pSkill->GetSkillIndex()) * 200;
    if (iDelay > 0 && iTotal > 0) {
        fRate = (float)iDelay / (float)iTotal;
        if (piRemainMs)
            *piRemainMs = iDelay;
    }

    const int iGlobal = CSkillCommandDelay::GetSingleton().GetSkillCommandDelayProgressRatio();
    const float fGlobal = (float)(100 - iGlobal) / 100.0f;
    if (fGlobal > fRate)
        fRate = fGlobal;

    if (fRate < 0.0f)
        fRate = 0.0f;
    if (fRate > 1.0f)
        fRate = 1.0f;
    return fRate;
}
