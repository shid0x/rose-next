/*
    $Header: /Client/CObjUSER.cpp 176   05-09-14 11:44a Raning $
*/
#include "stdAFX.h"

#include "CObjUSER.h"

#include "Game.h"
#include "Game_FUNC.h"
#include "Network/CNetwork.h"
#include "interface/dlgs/cpartydlg.h"
#include "gamedata/cbank.h"
#include "gamedata/cparty.h"
#include "system/cgame.h"
#include "gamecommon/item.h"
#include "rose/io/stb.h"
#include "CommandFilter.h"

//박지호
#include "./GameProc/UseItemDelay.h"

#include "rose/common/status_effect/goddess_effect.h"

using namespace Rose::Common;

CObjUSER* CObjUSER::m_pInstance = NULL;

//-------------------------------------------------------------------------------------------------
CObjUSER::CObjUSER() {
    m_Inventory.Clear();
    ::ZeroMemory(m_iAddValue, sizeof(int) * AT_MAX);

    this->m_iTeamInfo = 2;
    m_nCurrWeight = 0;
    m_iServerSavedMemoCount = 0;

    m_iSummonMobCapacity = 0;
    m_bWaitUpdateInventory = false;
    m_dwLastBattleTime = 0;

    m_AruaAddHp = 0;
    m_AruaAddMp = 0;
    m_AruaAddCritical = 0;
    m_AruaAddAttackPower = 0;
    m_AruaAddDefence = 0;
    m_PatGuageMax = 0;
}

CObjUSER::~CObjUSER() {}

CObjUSER*
CObjUSER::Instance() {
    if (!m_pInstance)
        m_pInstance = new CObjUSER;

    return m_pInstance;
}
void
CObjUSER::Destroy() {
    SAFE_DELETE(m_pInstance);
}

//-------------------------------------------------------------------------------------------------
void
CObjUSER::Init_DefaultValue(void) {
    ::ZeroMemory(&m_BasicINFO, sizeof(tagBasicINFO));
    ::ZeroMemory(&m_BasicAbility, sizeof(tagBasicAbility));
    ::ZeroMemory(&m_GrowAbility, sizeof(tagGrowAbility));
    ::ZeroMemory(&m_Skills, sizeof(tagSkillAbility));
    ::ZeroMemory(&m_Quests, sizeof(tagQuestData));
    ::ZeroMemory(&m_Battle, sizeof(tagBattleAbility));
    m_Inventory.Clear();

    Set_STR(100); // 근력
    Set_DEX(100); // 민첩
    Set_INT(10); // 지력
    Set_CON(10); // 집중
    Set_CHARM(10); // 매력
    Set_SENSE(10); // 감각

    tagITEM sITEM;
    ::ZeroMemory(&sITEM, sizeof(tagITEM));

    sITEM.m_cType = ITEM_TYPE_ARMOR; // 갑옷
    sITEM.m_nItemNo = 72;
    Set_EquipITEM(EQUIP_IDX_ARMOR, sITEM);

    sITEM.m_cType = ITEM_TYPE_KNAPSACK; // 등장비
    sITEM.m_nItemNo = 0;
    Set_EquipITEM(EQUIP_IDX_KNAPSACK, sITEM);

    sITEM.m_cType = ITEM_TYPE_GAUNTLET; // 장갑
    sITEM.m_nItemNo = 71;
    Set_EquipITEM(EQUIP_IDX_GAUNTLET, sITEM);

    sITEM.m_cType = ITEM_TYPE_BOOTS; // 신발
    sITEM.m_nItemNo = 44;
    Set_EquipITEM(EQUIP_IDX_BOOTS, sITEM);

    //*
    sITEM.m_cType = ITEM_TYPE_WEAPON; // 오른손
    sITEM.m_nItemNo = 36; // 231;//202;//26;
    Set_EquipITEM(EQUIP_IDX_WEAPON_R, sITEM);
    //	117 ;	// 언월도
    //	209 ;	// 장궁

    sITEM.m_cType = ITEM_TYPE_SUBWPN;
    sITEM.m_nItemNo = 0; // 왼손
    Set_EquipITEM(EQUIP_IDX_WEAPON_L, sITEM);
    //*/

    this->SetPartITEM(BODY_PART_FACE, 1); // 얼굴
    this->SetPartITEM(BODY_PART_HAIR, 0); // 기본 머리털
    this->SetPartITEM(BODY_PART_HELMET, 51);
    // TEST .......

    Set_MONEY(900000);

    short nA, nI, nJ;

    short nDefWpnITEM[] = {5, 32, 105, 161, 232, 303, 0};
    for (nI = 0; nDefWpnITEM[nI]; nI++) {
        sITEM.m_cType = ITEM_TYPE_WEAPON;
        sITEM.m_nItemNo = nDefWpnITEM[nI];
        Add_ITEM(MAX_EQUIP_IDX + INV_WEAPON * INVENTORY_PAGE_SIZE + nI, sITEM);
    }

    short nDefSubITEM[] = {2, 12, 13, 0};
    for (nJ = 0; nDefSubITEM[nJ]; nJ++) {
        sITEM.m_cType = ITEM_TYPE_SUBWPN;
        sITEM.m_nItemNo = nDefSubITEM[nJ];
        Add_ITEM(MAX_EQUIP_IDX + INV_WEAPON * INVENTORY_PAGE_SIZE + nI + nJ, sITEM);
    }

    short nDefArmorITEM[] = {7, 31, 35, 0};
    for (nA = 0; nDefArmorITEM[nA]; nA++) {
        sITEM.m_cType = ITEM_TYPE_ARMOR;
        sITEM.m_nItemNo = nDefArmorITEM[nA];
        Add_ITEM(MAX_EQUIP_IDX + INV_WEAPON * INVENTORY_PAGE_SIZE + nI + nJ + nA, sITEM);
    }

    // 모자
    sITEM.m_cType = BODY_PART_HELMET;
    sITEM.m_nItemNo = 55;
    Add_ITEM(MAX_EQUIP_IDX + INV_WEAPON * INVENTORY_PAGE_SIZE + nI + nJ + nA, sITEM);

    sITEM.m_cType = BODY_PART_HELMET;
    sITEM.m_nItemNo = 35;
    Add_ITEM(MAX_EQUIP_IDX + INV_WEAPON * INVENTORY_PAGE_SIZE + nI + nJ + nA + 1, sITEM);

    sITEM.m_cType = BODY_PART_HELMET;
    sITEM.m_nItemNo = 8;
    Add_ITEM(MAX_EQUIP_IDX + INV_WEAPON * INVENTORY_PAGE_SIZE + nI + nJ + nA + 2, sITEM);

    //부츠
    sITEM.m_cType = EQUIP_IDX_BOOTS;
    sITEM.m_nItemNo = 44;
    Add_ITEM(MAX_EQUIP_IDX + INV_WEAPON * INVENTORY_PAGE_SIZE + nI + nJ + nA + 3, sITEM);

    /*
    //장갑
    sITEM.m_cType     = ITEM_TYPE_GAUNTLET;
    sITEM.m_nItemNo   = 7;
    Add_ITEM( MAX_EQUIP_IDX+INV_WEAPON*INVENTORY_PAGE_SIZE + nI+nJ+nA+4, sITEM );

    sITEM.m_cType     = ITEM_TYPE_GAUNTLET;
    sITEM.m_nItemNo   = 6;
    Add_ITEM( MAX_EQUIP_IDX+INV_WEAPON*INVENTORY_PAGE_SIZE + nI+nJ+nA+5, sITEM );

    //가방
    sITEM.m_cType     = ITEM_TYPE_KNAPSACK;
    sITEM.m_nItemNo   = 2;
    Add_ITEM( MAX_EQUIP_IDX+INV_WEAPON*INVENTORY_PAGE_SIZE + nI+nJ+nA+6, sITEM );

    sITEM.m_cType     = ITEM_TYPE_KNAPSACK;
    sITEM.m_nItemNo   = 4;
    Add_ITEM( MAX_EQUIP_IDX+INV_WEAPON*INVENTORY_PAGE_SIZE + nI+nJ+nA+7, sITEM );

    //얼굴
    sITEM.m_cType     = ITEM_TYPE_FACE_ITEM;
    sITEM.m_nItemNo   = 5;
    Add_ITEM( MAX_EQUIP_IDX+INV_WEAPON*INVENTORY_PAGE_SIZE + nI+nJ+nA+8, sITEM );

    sITEM.m_cType     = ITEM_TYPE_FACE_ITEM;
    sITEM.m_nItemNo   = 8;
    Add_ITEM( MAX_EQUIP_IDX+INV_WEAPON*INVENTORY_PAGE_SIZE + nI+nJ+nA+9, sITEM );
    */

    struct tagDefITEM {
        short nItemNo;
        short nQuantity;
    };

    tagDefITEM sDefUseITEM[] = {{3, 2},
        {6, 2},
        {9, 2},
        {13, 2},
        {6, 2},
        {19, 2},

        {0, 0}};

    // 소모
    for (nI = 0; sDefUseITEM[nI].nItemNo; nI++) {
        sITEM.m_cType = ITEM_TYPE_USE;
        sITEM.m_nItemNo = sDefUseITEM[nI].nItemNo;
        sITEM.m_uiQuantity = sDefUseITEM[nI].nQuantity;
        Add_ITEM(MAX_EQUIP_IDX + INV_USE * INVENTORY_PAGE_SIZE + nI, sITEM);
    }

    // 기타
    tagDefITEM sDefEtcITEM[] = {/*
                                {	1, 100	},	{ 3, 100 },	{ 6, 50 },	{ 8, 50 },	{ 23, 20 },
                                {	31, 20  },	{ 54, 20 },
                                */
        {1, 2},
        {21, 2},
        {31, 2},
        {41, 2},
        {51, 2},
        {61, 2},
        {71, 2},
        {122, 2},

        {0, 0}};
    for (nI = 0; sDefEtcITEM[nI].nItemNo; nI++) {
        sITEM.m_cType = ITEM_TYPE_NATURAL;
        sITEM.m_nItemNo = sDefEtcITEM[nI].nItemNo;
        sITEM.m_uiQuantity = sDefEtcITEM[nI].nQuantity;
        Add_ITEM(MAX_EQUIP_IDX + INV_ETC * INVENTORY_PAGE_SIZE + nI, sITEM);
    }

    this->m_GrowAbility.m_nLevel = 1;
    this->LevelUP();

    /// 공통 액션
    AddNewSkill(1, 0, 1);
    AddNewSkill(2, 1, 1);
    AddNewSkill(3, 2, 1);

    /// 공통- 이모션
    AddNewSkill(11, 10, 1);
    AddNewSkill(12, 11, 1);
    AddNewSkill(13, 12, 1);

    /// 전투 - 근접
    AddNewSkill(21, MAX_LEARNED_SKILL_CNT / 4 + 0, 1);
    AddNewSkill(22, MAX_LEARNED_SKILL_CNT / 4 + 1, 1);

    /// 전투 - 원거리 발사
    AddNewSkill(31, MAX_LEARNED_SKILL_CNT / 4 + 2, 1);

    /// 전투 - 보조
    AddNewSkill(41, MAX_LEARNED_SKILL_CNT / 4 + 3, 1);
}

/// 새로운 스킬 추가.
bool
CObjUSER::AddNewSkill(int iSkillIDX, int iSkillSlotNO, int iSkillLevel, bool bSubPOINT) {
    if (iSkillSlotNO >= MAX_LEARNED_SKILL_CNT)
        return false;

    /// 스킬 배우면 적용
    if (this->Skill_LEARN(iSkillSlotNO, iSkillIDX, bSubPOINT)) {
        /// 패시브 스킬중 속도에 변화가 생길경우..
        this->Update_SPEED();
    }

    m_SkillSlot.SetSkillSlot(iSkillSlotNO, iSkillIDX, iSkillLevel);

    return true;
}
///  기존 스킬의 레벨업
bool
CObjUSER::Skill_LevelUp(int iSkillSlotNo, int iSkillIndex) {
    if (iSkillSlotNo >= MAX_LEARNED_SKILL_CNT)
        return false;

    m_SkillSlot.SkillLevelUp(iSkillSlotNo, iSkillIndex);
    return true;
}

void
CObjUSER::Update_SPEED() {
    if (this->GetPetMode() < 0) {
        m_fRunAniSPEED = Cal_RunAniSPEED(this->stats.move_speed);
    } else {
        m_fRunAniSPEED = (this->stats.move_speed + 500) / 1000.f;
    }
};

//-------------------------------------------------------------------------------------------------
void
CObjUSER::Add_EXP(short nExp) {
    m_GrowAbility.m_lEXP += nExp;

    int iNeedEXP = this->Get_NeedEXP(this->Get_LEVEL());
    bool bLevelUp = false;

    while (m_GrowAbility.m_lEXP >= iNeedEXP) {
        // 레벨 제한 ???
        this->Set_LEVEL(this->Get_LEVEL() + 1);

        g_itMGR.AppendChatMsg(CStr::Printf(">>> %s LEVEL UP to %d", Get_NAME(), Get_LEVEL()),
            IT_MGR::CHAT_TYPE_SYSTEM);

        m_GrowAbility.m_lEXP -= iNeedEXP;

        iNeedEXP = this->Get_NeedEXP(this->Get_LEVEL());
        bLevelUp = true;
    }

    /*if ( bLevelUp )
        ON_CharLevelUp( m_nIndex );*/
}

void
CObjUSER::RideCartToggle(bool bRide) {
    CObjAVT::RideCartToggle(bRide);

    this->UpdateAbility();
    Cal_PatAbility();

    /// Pat탑승 여부에 따라 스킬의 활성화/비활성화를 바꾸어 준다.
    ///
    m_SkillSlot.SetActiveSkillEnableByRideState(bRide);
}

//-------------------------------------------------------------------------------------------------
/// 클래스 내부에 설정된 아바타 정보를 이용해서, 업데이트 한다.
void
CObjUSER::Update(bool bUpdateBONE) {
    CObjAVT::Update(bUpdateBONE);

    // 7. 공격, 이동 속도 갱신.
    this->Update_SPEED();
}

//-------------------------------------------------------------------------------------------------
/// Add : 2004 / 2/ 12 : nAvy
bool
CObjUSER::Quest_IsPartyLEADER() {
    return CParty::GetInstance().IsPartyLeader();
}
int
CObjUSER::Quest_GetPartyLEVEL() {
    return CParty::GetInstance().GetLevel();
}

/*override*/ int
CObjUSER::Quest_GetZoneNO() {
    return g_pTerrain->GetZoneNO();
}

/*override*/ int
CObjUSER::Quest_DistanceFrom(int iX, int iY, int iZ) {
    int iDistance = CD3DUtil::distance((int)m_PosCUR.x, (int)m_PosCUR.y, iX, iY);
    return iDistance;
}

/*override*/ int
CObjUSER::Quest_PartyMemberCNT() {
    return CParty::GetInstance().GetMemberCount();
}

//-------------------------------------------------------------------------------------------------
/*override*/ int
CObjUSER::Get_BulletNO() {
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

    if (iShotType < MAX_SHOT_TYPE && CUserDATA::m_Inventory.m_ItemSHOT[iShotType].GetQuantity() > 0)
        iBulletNo = NATURAL_BULLET_NO(CUserDATA::m_Inventory.m_ItemSHOT[iShotType].GetItemNO());

    return iBulletNo;
}

void
CObjUSER::SubActiveBulletQuantity() {
    tagITEM sItem;
    sItem.m_cType = ITEM_TYPE_WEAPON;
    sItem.m_nItemNo = Get_R_WEAPON();

    int iBulletNo = 0;
    int iShotType = sItem.GetShotTYPE();

    if (iShotType >= MAX_SHOT_TYPE)
        return;

    CItem* pItem = m_ItemSlot.GetItem(INVENTORY_SHOT_ITEM0 + iShotType);
    if (pItem == NULL) {
        assert(pItem && "Not Found Bullet @CObjUSER::SubActiveBulletQuantity() ");
        return;
    }

    pItem->SubQuantity(1);
    m_ItemSlot.ChangeItemStatus(INVENTORY_SHOT_ITEM0 + iShotType);

    ///인벤토리에서 삭제
    /// gsv_SET_BULLET으로 셋팅된값이 0라면 ( 다 사용했다고 알려졌다면 );
    if (CObjAVT::m_ShotData[iShotType].m_nItemNo == 0) {
        ClearITEM(INVENTORY_SHOT_ITEM0 + iShotType);
        UpdateAbility();
        g_itMGR.AppendChatMsg(STR_WARN_BULLET_USEUP, IT_MGR::CHAT_TYPE_SYSTEM);
        D3DVECTOR PosSCR;
        g_pAVATAR->GetScreenPOS(PosSCR);
        g_pNet->Send_cli_STOP(PosSCR);
    }
    UpdateInventory();
}

BYTE
CObjUSER::GetCur_MOVE_MODE() {
    return this->m_btMoveMODE;
}

/// 현재 펫이 조합이 다되어 탈수 있는가?
bool
CObjUSER::CanRidePET() {
    if (m_Inventory.m_ItemRIDE[0].IsEmpty()) {
        return false;
    }

    int item_type = ITEM_TYPE(m_Inventory.m_ItemRIDE[0].GetTYPE(),
        m_Inventory.m_ItemRIDE[0].GetItemNO());

    if (item_type == TUNING_PART_BODY_MOUNT) {
        return true;
    }

    for (int i = 0; i < MAX_RIDING_PART - 2; i++) {
        if (m_Inventory.m_ItemRIDE[i].GetTYPE() == 0) {
            return false;
        }
    }

    return true;
}

// 2인승 카트
bool
CObjUSER::IsRideItem(int iIndex_) {
    if (iIndex_ >= 0 && iIndex_ < MAX_RIDING_PART && m_Inventory.m_ItemRIDE[iIndex_].GetTYPE() != 0
        && PAT_ITEM_ABILITY_TYPE(m_Inventory.m_ItemRIDE[iIndex_].GetItemNO())) {
        return true;
    }
    return false;
}

void
CObjUSER::Set_HP(int iHP) {
    assert(iHP != 0);
    SetCur_HP(iHP);
}
void
CObjUSER::Set_MP(int iMP) {
    SetCur_MP(iMP);
}
//--------------------------------------------------------------------------------
/// @brief  : CInventoryDlg의 VirtualInventory과 WeightRate를 갱신한다.
//--------------------------------------------------------------------------------
void
CObjUSER::UpdateInventory() {
    if (m_bWaitUpdateInventory)
        return;

    /// 가상 인벤토리 갱신
    // g_itMGR.UpdateInventory();

    /// 무게비율 갱신
    int iMaxWeight = Get_MaxWEIGHT(); ///최대 소지량

    // assert( iMaxWeight > 0 );
    if (iMaxWeight <= 0)
        return;

    int iCurrWeight = 0; ///현재 소지량
    int iItemWeight = 0; ///아이템 1개당 무게

    tagITEM* pItem = NULL;
    for (int iIndex = 0; iIndex < INVENTORY_TOTAL_SIZE; ++iIndex) {
        pItem = &(m_Inventory.m_ItemLIST[iIndex]);
        assert(pItem);
        if (!pItem->IsEmpty()) {
            iItemWeight = ITEM_WEIGHT(pItem->GetTYPE(), pItem->GetItemNO());

            if (pItem->IsEnableDupCNT())
                iCurrWeight += iItemWeight * pItem->GetQuantity();
            else
                iCurrWeight += iItemWeight;
        }
    }

    SetCurrWeight(iCurrWeight);

    BYTE btPrevWeightRate = GetWeightRate();
    BYTE btCurrWeightRate = 0;

    int iCurrWeightRate = iCurrWeight * 100 / iMaxWeight;

    if (iCurrWeightRate >= 250)
        btCurrWeightRate = 250;
    else
        btCurrWeightRate = (BYTE)iCurrWeightRate;

    SetWeightRate(btCurrWeightRate);

    ///일정비율( 80%, 100% )을 넘거나 내려왔을때 서버에게 알린다.
    int iPrevWeightRateState = 0;
    if (btPrevWeightRate < 100)
        iPrevWeightRateState = 0;
    else if (btPrevWeightRate < 110)
        iPrevWeightRateState = 1;
    else
        iPrevWeightRateState = 2;

    int iCurrWeightRateState = 0;
    if (btCurrWeightRate < 100)
        iCurrWeightRateState = 0;
    else if (btCurrWeightRate < 110)
        iCurrWeightRateState = 1;
    else
        iCurrWeightRateState = 2;

    if (iPrevWeightRateState != iCurrWeightRateState) {
        if (CGame::GetInstance().GetCurrStateID() == CGame::GS_MAIN)
            g_pNet->Send_cli_SET_WEIGHT_RATE(btCurrWeightRate);

        // Apply the walk/run toggle locally — Recv_gsv_SET_WEIGHT_RATE on the
        // self-avatar can't observe the previous rate (we already overwrote
        // it above with SetWeightRate), so the run-restore branch wouldn't
        // fire for the local player otherwise.
        if (g_pAVATAR && g_pAVATAR->GetPetMode() < 0) {
            if (btCurrWeightRate >= 100)
                g_pAVATAR->SetCMD_TOGGLE(MOVE_MODE_WALK + TOGGLE_TYPE_DRIVE);
            else if (btPrevWeightRate >= 100)
                g_pAVATAR->SetCMD_TOGGLE(MOVE_MODE_RUN + TOGGLE_TYPE_DRIVE);
        }
    }
}

short
CObjUSER::Add_ITEM(short nListRealNO, tagITEM& sITEM) {
    short nRet = CUserDATA::Add_ITEM(nListRealNO, sITEM);
    m_ItemSlot.AddItem(nListRealNO, sITEM);
    UpdateInventory();
    return nRet;
}

void
CObjUSER::Sub_ITEM(short nListRealNO, tagITEM& sITEM) {
    CUserDATA::Sub_ITEM(nListRealNO, sITEM);
    m_ItemSlot.SubItem(nListRealNO, sITEM);
    UpdateInventory();
}

void
CObjUSER::ClearITEM(WORD wListRealNO) {
    CUserDATA::ClearITEM(wListRealNO);
    m_ItemSlot.DelItem(wListRealNO);
    UpdateInventory();
}

short
CObjUSER::Add_CatchITEM(short nListRealNO, tagITEM& sITEM) {
    short nRet = CUserDATA::Add_CatchITEM(nListRealNO, sITEM);
    if (sITEM.GetTYPE() != ITEM_TYPE_MONEY) {
        m_ItemSlot.AddItem(nListRealNO, sITEM);
        UpdateInventory();
    }
    ///인벤토리에 아이템이 들어갈때
    g_pSoundLIST->IDX_PlaySound(SID_GETTING_ITEM);
    return nRet;
}

short
CObjUSER::Add_ITEM(tagITEM& sITEM) {
    short nRet = CUserDATA::Add_ITEM(sITEM);
    m_ItemSlot.AddItem(sITEM);
    UpdateInventory();
    return nRet;
}

void
CObjUSER::Set_ITEM(short nListRealNO, tagITEM& sITEM) {
    tagITEM* pItem = &(CUserDATA::m_Inventory.m_ItemLIST[nListRealNO]);

    ///기존슬롯에 아이템이 있고 ... 중복가능하며, 같은 아이템일경우는 Count만 늘린다.
    if (!pItem->IsEmpty() && !sITEM.IsEmpty() && sITEM.IsEnableDupCNT()
        && pItem->IsEqual(sITEM.GetTYPE(), sITEM.GetItemNO())) {
        CUserDATA::m_Inventory.m_ItemLIST[nListRealNO] = sITEM;
        GetItemSlot()->ChangeItemStatus(nListRealNO);
    } else {
        CUserDATA::Set_ITEM(nListRealNO, sITEM);
        if (!sITEM.IsEmpty()) {
            if ((nListRealNO > 0 && nListRealNO < INVENTORY_ITEM_INDEX_0)
                || (nListRealNO >= INVENTORY_SHOT_ITEM0
                    && nListRealNO < INVENTORY_SHOT_ITEM0 + MAX_SHOT_TYPE)
                || (nListRealNO >= INVENTORY_COSTUME_ITEM0
                    && nListRealNO < INVENTORY_COSTUME_ITEM0 + MAX_COSTUME_IDX)) ///장착시
                g_pSoundLIST->IDX_PlaySound(ITEM_EQUIP_SOUND(sITEM.GetTYPE(), sITEM.GetItemNO()));
        }
    }
    /// m_ItemSlot의 Add,Sub,DelItem은 CUserDATA::Set_ITEM에서 호출된다.
    // m_ItemSlot.SetItem( nListRealNO, sITEM );

    if (nListRealNO >= INVENTORY_RIDE_ITEM0
        && nListRealNO < INVENTORY_RIDE_ITEM0 + MAX_RIDING_PART) {
        if (!sITEM.IsEmpty()) {
            int iItemIDX = sITEM.GetItemNO();

            if (nListRealNO == INVENTORY_RIDE_ITEM0)
                g_pAVATAR->SetPetType(PAT_ITEM_PART_TYPE(iItemIDX));

            g_pAVATAR->SetPetParts(nListRealNO - INVENTORY_RIDE_ITEM0, iItemIDX);
        } else {
            g_pAVATAR->SetPetParts(nListRealNO - INVENTORY_RIDE_ITEM0, 0);
        }
    }
    UpdateInventory();
}

void
CObjUSER::UpdateAbility() {
    GoddessEffect* goddess_effect = this->m_EndurancePack.get_goddess_effect();
    if (goddess_effect) {
        goddess_effect->update(GetCur_LEVEL());
    }

    Cal_BattleAbility();
    Calc_AruaAddAbility();

    // 홍근 : UpdateInventory()와 Update_SPEED()의 위치 바꿈.
    UpdateInventory(); ///최대 소지량이 변경되었을수 있다.
    Update_SPEED();
}

void
CObjUSER::SetBankItem(int iIndex, tagITEM& Item) {
    CUserDATA::m_Bank.Set_ITEM(iIndex, Item);

    CBank::GetInstance().SetItem(iIndex, Item);
}

void
CObjUSER::InitBank() {
    CUserDATA::m_Bank.Init();
    CBank::GetInstance().ClearItems();
}

int
CObjUSER::Proc(void) {
    m_SkillSlot.UpdateSkillSlot();

    return CObjAVT::Proc();
}

//-------------------------------------------------------------------------------------------------
/// 무게 관련
short
CObjUSER::GetCur_WEIGHT() {
    return m_nCurrWeight;
}

void
CObjUSER::SetCurrWeight(short nWeight) {
    m_nCurrWeight = nWeight;
}

//-------------------------------------------------------------------------------------------------
/// 월드서버에서 유니크한
void
CObjUSER::SetUniqueTag(DWORD dwUniqueTag) {
    m_dwUniqueTag = dwUniqueTag;
}

DWORD
CObjUSER::GetUniqueTag() {
    return m_dwUniqueTag;
}

//-------------------------------------------------------------------------------------------------
/// 서버에 저장되어 있는 쪽지 관련 처리를 위한 Method
void
CObjUSER::SetServerSavedMemoCount(int iCount) {
    m_iServerSavedMemoCount = iCount;
}

int
CObjUSER::GetServerSavedMemoCount() {
    return m_iServerSavedMemoCount;
}

void
CObjUSER::DecreaseServerSavedMemoCount() {
    --m_iServerSavedMemoCount;
    assert(m_iServerSavedMemoCount >= 0);
    if (m_iServerSavedMemoCount < 0)
        m_iServerSavedMemoCount;
}

/// 소환가능 총 량..
int
CObjUSER::GetCur_SummonMaxCapacity() {
    return 50 + GetPassiveSkillValue(AT_PSV_SUMMON_MOB_CNT);
}

void
CObjUSER::AddSummonedMob(int iIndex, int iSkillIDX) {
    /// 이미 있는놈이라면..
    std::list<SummonMobInfo>::iterator begin = m_SummonedMobList.begin();

    for (; begin != m_SummonedMobList.end(); ++begin) {
        SummonMobInfo& monInfo = *begin;

        if (monInfo.iIndex == iIndex)
            return;
    }

    /// if( itor == m_SummonedMobList.end() )
    {
        SummonMobInfo summonMobInfo;
        summonMobInfo.iIndex = iIndex;
        summonMobInfo.iSkillIDX = iSkillIDX;

        CObjCHAR* pObj = g_pObjMGR->Get_ClientCharOBJ(iIndex, false);
        if (pObj) {
            m_iSummonMobCapacity += NPC_NEED_SUMMON_CNT(pObj->Get_CharNO());

            summonMobInfo.iCapacity = NPC_NEED_SUMMON_CNT(pObj->Get_CharNO());
            m_SummonedMobList.push_back(summonMobInfo);
        }
    }
}

void
CObjUSER::SubSummonedMob(int iIndex) {
    /// 이미 있는놈이라면..
    std::list<SummonMobInfo>::iterator begin = m_SummonedMobList.begin();

    for (; begin != m_SummonedMobList.end(); ++begin) {
        SummonMobInfo& monInfo = *begin;

        if (monInfo.iIndex == iIndex) {
            m_iSummonMobCapacity -= monInfo.iCapacity;

            m_SummonedMobList.erase(begin);
            break;
        }
    }
}

bool
CObjUSER::GetSummonedMobInfo(int iIndex, SummonMobInfo& mobInfo) {
    /// 이미 있는놈이라면..
    std::list<SummonMobInfo>::iterator begin = m_SummonedMobList.begin();

    for (; begin != m_SummonedMobList.end(); ++begin) {
        SummonMobInfo& monInfo = *begin;

        if (monInfo.iIndex == iIndex) {
            mobInfo = monInfo;
            return true;
        }
    }

    return false;
}

short
CObjUSER::GetCur_STAMINA() {
    return CUserDATA::m_GrowAbility.m_nSTAMINA;
}

void
CObjUSER::SetCur_STAMINA(short nStamina) {
    CUserDATA::m_GrowAbility.m_nSTAMINA = nStamina;
}
//*----------------------------------------------------------------------
/// @brief overriden CObjAVT::GetStamina() -- CObjAVT::Proc에서 호출시 CObjUSER의 경우 호출되어
/// 진다.
//*----------------------------------------------------------------------
short
CObjUSER::GetStamina() {
    return GetCur_STAMINA();
}

void
CObjUSER::SetWaitUpdateInventory(bool bWait) {
    m_bWaitUpdateInventory = bWait;
}

void
CObjUSER::Set_JOB(int iValue) {
    SetCur_JOB(iValue);
    if (GetClanID())
        g_pNet->Send_cli_CLAN_MEMBER_JOBnLEV(Get_LEVEL(), iValue);

    // 20050823 홍근 치트로 직업을 변경했을 경우에도 MAXHP 변화 표시 해주기 위해.
    UpdateAbility();
}

bool
CObjUSER::IsInventoryFull(std::list<tagITEM>& appendItems,
    std::list<pair<int, tagITEM>>& removeItems) {
    CInventory TempInventory;
    memcpy(&TempInventory, &m_Inventory, sizeof(CInventory));

    std::list<tagITEM>::iterator iterAppend;
    std::list<pair<int, tagITEM>>::iterator iterRemove;
    short nInvenIndex = 0;
    short nWeight = 0;

    for (iterRemove = removeItems.begin(); iterRemove != removeItems.end(); ++iterRemove)
        TempInventory.SubtractITEM(iterRemove->first, iterRemove->second, nWeight);

    for (iterAppend = appendItems.begin(); iterAppend != appendItems.end(); ++iterAppend) {
        if (TempInventory.AppendITEM(*iterAppend, nWeight) == -1)
            return true;
    }
    return false;
}

bool
CObjUSER::IsInventoryFull(std::list<tagITEM>& appendItems) {
    CInventory TempInventory;
    memcpy(&TempInventory, &m_Inventory, sizeof(CInventory));

    std::list<tagITEM>::iterator iterAppend;
    short nInvenIndex = 0;
    short nWeight = 0;

    for (iterAppend = appendItems.begin(); iterAppend != appendItems.end(); ++iterAppend) {
        if (TempInventory.AppendITEM(*iterAppend, nWeight) == -1)
            return true;
    }
    return false;
}

void
CObjUSER::SetBattleTime(DWORD dwTime) {
    m_dwLastBattleTime = dwTime;
    // LogString( LOG_NORMAL,"Set Battle Time\n" );
}

DWORD
CObjUSER::GetLastBattleTime() {
    return m_dwLastBattleTime;
}

void
CObjUSER::ChangeModelInfo(short nType, int iValue) {
    /// AT_SEX, AT_HAIR, AT_FACE
    switch (nType) {
        case AT_SEX: {
            this->m_bIsFemale = iValue;
            this->m_btRace = iValue;
            this->m_CharMODEL.Set_SEX(m_bIsFemale);
            this->Update(true);
        } break;
        case AT_HAIR:
            this->SetPartITEM(BODY_PART_HAIR, iValue);
            m_BasicINFO.m_cHairIDX = iValue;
            this->Update();
            break;
        case AT_FACE:
            this->SetPartITEM(BODY_PART_FACE, iValue);
            m_BasicINFO.m_cFaceIDX = iValue;
            this->Update();
            break;

        default:
            break;
    }
}

short
CObjUSER::GetPsv_ATKSPEED(float fCurSpeed, short nRightWeaponItemNo) {
    return this->GetPassiveSkillAttackSpeed(fCurSpeed, nRightWeaponItemNo)
        + CUserDATA::m_iAddValue[AT_ATK_SPD];
}

/// 스킬에 의해 보정된 값이 있을경우 더해져서 리턴할 함수
/// arua상태에 의해서 변경된 값도 더해준다.					2005/7/12 : nAvy
int
CObjUSER::GetCur_MaxHP() {
    return this->m_Battle.m_nMaxHP + m_EndurancePack.GetStateValue(ING_INC_MAX_HP) + m_AruaAddHp;
};

/// 스킬에 의해 보정된 값이 있을경우 더해져서 리턴할 함수
/// arua상태에 의해서 변경된 값도 더해준다.					2005/7/12 : nAvy
int
CObjUSER::GetCur_MaxMP() {
    return this->m_Battle.m_nMaxMP + m_EndurancePack.GetStateValue(ING_INC_MAX_MP) + m_AruaAddMp;
};
//------------------------------------------------------------------------------------
/// @brief aura 상태에 의해서 변경된 값 적용					2005/7/13 : nAvy
//------------------------------------------------------------------------------------
int
CObjUSER::GetCur_CRITICAL() {
    int crit = GetDef_CRITICAL() + m_EndurancePack.GetStateValue(ING_INC_CRITICAL)
        - m_EndurancePack.GetStateValue(ING_DEC_CRITICAL) + m_AruaAddCritical;

    // Goddess effect doesn't stack with other buffs
    auto goddess_effect = m_EndurancePack.get_goddess_effect();
    if (goddess_effect) {
        crit += max(0, goddess_effect->crit - m_EndurancePack.GetStateValue(ING_INC_CRITICAL));
    }

    return (crit > 10) ? crit : 10;
}

int
CObjUSER::GetCur_DEF() {
    int iR = GetDef_DEF() + m_EndurancePack.GetStateValue(ING_INC_DPOWER)
        - m_EndurancePack.GetStateValue(ING_DEC_DPOWER) + m_AruaAddDefence;
    return (iR > 10) ? iR : 10;
}

//------------------------------------------------------------------------------------
/// @brief virtual method from CObjAI					2005/7/13 : nAvy
//------------------------------------------------------------------------------------
int
CObjUSER::GetOri_MaxHP() {
    return m_Battle.m_nMaxHP;
}
//------------------------------------------------------------------------------------
/// @brief virtual method from CObjAI					2005/7/13 : nAvy
//------------------------------------------------------------------------------------
int
CObjUSER::GetOri_MaxMP() {
    return m_Battle.m_nMaxMP;
}
//------------------------------------------------------------------------------------
/// @brief 아루아 버프 걸린 상태에 대한 추가적인 능력치 계산 2005/7/13 : navy
//------------------------------------------------------------------------------------
void
CObjUSER::Calc_AruaAddAbility() {
    if (m_IsAroa) {
        m_AruaAddHp = (short)(GetOri_MaxHP() * 0.2f);
        m_AruaAddMp = (short)(GetOri_MaxMP() * 0.2f);
        m_AruaAddCritical = (short)(GetDef_CRITICAL() * 0.2f);
        m_AruaAddAttackPower = (short)(this->stats.attack_power * 0.2f);
        m_AruaAddDefence = (short)(GetDef_DEF() * 0.2f);
    } else {
        m_AruaAddHp = 0;
        m_AruaAddMp = 0;
        m_AruaAddCritical = 0;
        m_AruaAddAttackPower = 0;
        m_AruaAddDefence = 0;
    }
}

//------------------------------------------------------------------------------------
bool
CObjUSER::SetCMD_Skill2OBJ(WORD wSrvDIST,
    const D3DVECTOR& PosTO,
    int iServerTarget,
    short nSkillIDX) {
    CObjSkill2ObjCommand* pObjCommand =
        (CObjSkill2ObjCommand*)g_CommandFilter.GetCommandObject(OBJECT_COMMAND_Skill2OBJ);
    pObjCommand->SetCMD_Skill2OBJ(wSrvDIST, PosTO, iServerTarget, nSkillIDX);
    g_CommandFilter.SetPrevCommand(pObjCommand);

    // SubPatGuagePerPatSkillExcute( nSkillIDX  );

    return CObjAI::SetCMD_Skill2OBJ(wSrvDIST, PosTO, iServerTarget, nSkillIDX);
}

void
CObjUSER::SetCMD_Skill2SELF(short nSkillIDX) {
    CObjSkill2SelfCommand* pObjCommand =
        (CObjSkill2SelfCommand*)g_CommandFilter.GetCommandObject(OBJECT_COMMAND_Skill2SELF);
    pObjCommand->SetCMD_Skill2SELF(nSkillIDX);
    g_CommandFilter.SetPrevCommand(pObjCommand);

    // SubPatGuagePerPatSkillExcute( nSkillIDX  );

    CObjAI::SetCMD_Skill2SELF(nSkillIDX);
}

void
CObjUSER::SetCMD_Skill2POS(const D3DVECTOR& PosGOTO, short nSkillIDX) {
    CObjSkill2PosCommand* pObjCommand =
        (CObjSkill2PosCommand*)g_CommandFilter.GetCommandObject(OBJECT_COMMAND_Skill2POS);
    pObjCommand->SetCMD_Skill2POS(PosGOTO, nSkillIDX);
    g_CommandFilter.SetPrevCommand(pObjCommand);

    // SubPatGuagePerPatSkillExcute( nSkillIDX  );

    CObjAI::SetCMD_Skill2POS(PosGOTO, nSkillIDX);
}

//------------------------------------------------------------------------------------------------------
/// @brief 카트의 추가적인 능력치를 구해주는 함수
///			1. 카트 게이지
///		- 서버에서 값을 내려주기 때문에 이 부분을 뺀다 2005/7/27 - nAvy
//------------------------------------------------------------------------------------------------------
void
CObjUSER::Cal_PatAbility() {
#if defined(_GBC)
    m_PatGuageMax = 0;
    for (int i = 0; i < MAX_RIDING_PART; ++i) {
        if (!m_Inventory.m_ItemRIDE[i].IsEmpty())
            m_PatGuageMax += PAT_ITEM_CART_GAUAGE(m_Inventory.m_ItemRIDE[i].GetItemNO());
    }

    if (GetCur_PatHP() > m_PatGuageMax) {
        _RPTF0(_CRT_ASSERT, "m_GrowAbility.m_nPatHP > m_PatGuageMax\n");
        SetCur_PatHP(m_PatGuageMax);
    }
#endif
}

int
CObjUSER::GetPatGuageMax() {
    return m_PatGuageMax;
}

int
CObjUSER::GetPatGuageCurrent() {
    return GetCur_PatHP();
}

void
CObjUSER::SetPatGuageMax(int guage) {
    m_PatGuageMax = guage;
}

void
CObjUSER::SetPatGuageCurrent(int guage) {
    SetCur_PatHP(guage);
}

BOOL
CObjUSER::SetCoolTimeUseItem(char* uName, short iItem) {
    return FALSE;
}

BOOL
CObjUSER::SetCoolTimeSkill(char* uName, int iSkill) {
    return FALSE;
}

void
CObjUSER::Set_Block_CartRide(bool ride) {
    GetSkillSlot()->SetActiveSkillEnableByRideState_Aid(ride);
}

int
CObjUSER::Get_AbilityValue(WORD wType) {
    switch (wType) {
        // case AT_BIRTHSTONE :	break;
        case AT_SEX:
            return (GetCur_RACE() & 0x01);
        case AT_RACE:
            return (GetCur_RACE() / 0x02);

        case AT_CLASS:
            return GetCur_JOB();
        case AT_UNION:
            return GetCur_UNION();
        case AT_RANK:
            return GetCur_RANK();
        case AT_FAME:
            return GetCur_FAME();

        case AT_FACE:
            return m_BasicINFO.m_cFaceIDX;
        case AT_HAIR:
            return m_BasicINFO.m_cHairIDX;

        case AT_STR:
            return GetCur_STR();
        case AT_DEX:
            return GetCur_DEX();
        case AT_INT:
            return GetCur_INT();
        case AT_CON:
            return GetCur_CON();
        case AT_CHARM:
            return GetCur_CHARM();
        case AT_SENSE:
            return GetCur_SENSE();
        case AT_HP:
            return GetCur_HP();
        case AT_MP:
            return GetCur_MP();
        case AT_ATK:
            return this->stats.attack_power;
        case AT_DEF:
            return GetCur_DEF();
        case AT_HIT:
            return this->stats.hit_rate;
        case AT_RES:
            return GetCur_RES();
        case AT_AVOID:
            return GetCur_AVOID();
        case AT_SPEED:
            return this->stats.move_speed;
        case AT_ATK_SPD:
            return GetCur_ATK_SPD();
        case AT_WEIGHT:
            return GetCur_WEIGHT();
        case AT_CRITICAL:
            return GetCur_CRITICAL();

            // case AT_RECOVER_HP :
            // case AT_RECOVER_MP :	break;

        case AT_EXP:
            return GetCur_EXP();
        case AT_LEVEL:
            return GetCur_LEVEL();
        case AT_BONUSPOINT:
            return GetCur_BonusPOINT();
        case AT_SKILLPOINT:
            return GetCur_SkillPOINT();
            // case AT_CHAOS	:
            // case AT_PK_LEV	:	break;

        case AT_HEAD_SIZE:
            return GetCur_HeadSIZE();
        case AT_BODY_SIZE:
            return GetCur_BodySIZE();

        case AT_MONEY:
            if (GetCur_MONEY() > MAX_INT)
                return MAX_INT;
            return (int)GetCur_MONEY();

        case AT_MAX_HP:
            return GetCur_MaxHP();
        case AT_MAX_MP:
            return GetCur_MaxMP();
        case AT_STAMINA:
            return m_GrowAbility.m_nSTAMINA;
        case AT_PATHP:
            return GetCur_PatHP();
        default: {
            if (wType >= AT_UNION_POINT1 && wType <= AT_UNION_POINT10)
                return GetCur_UnionPOINT(wType);
            break;
        }
    }

    return 0;
}