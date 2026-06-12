
#include "stdAFX.h"

#include "CObjAVT.h"
#include "GS_ThreadZONE.h"

CObjAVT::CObjAVT() {
    m_pTargetNODE = new classDLLNODE<CObjAVT*>;
    m_pTargetNODE->DATA = this;
}
CObjAVT::~CObjAVT() {
    SAFE_DELETE(m_pTargetNODE);
}

void
CObjAVT::update_speed() {
    float fSpeed = this->Cal_RunSPEED();
    this->stats.move_speed = floor_int(fSpeed);

    short nWeaponSpeed;
    if (this->GetCur_MOVE_MODE() <= MOVE_MODE_RUN) {
        this->m_fRunAniSPEED = Cal_RunAniSPEED(this->stats.move_speed);

        short nWeaponItemNo = GetPartITEM(BODY_PART_WEAPON_R);
        nWeaponSpeed = WEAPON_ATTACK_SPEED(nWeaponItemNo) + 5;

        fSpeed = 1500.f / nWeaponSpeed + m_iAddValue[AT_ATK_SPD];
        this->m_nPassiveAttackSpeed = this->GetPassiveSkillAttackSpeed(fSpeed, nWeaponItemNo);
        this->stats.attack_speed = (short)(fSpeed + this->m_nPassiveAttackSpeed);
    } else {
        m_fRunAniSPEED = (this->stats.move_speed + 500) / 1000.f;

        nWeaponSpeed = PAT_ITEM_ATK_SPD(m_RideITEM[RIDE_PART_ARMS].m_nItemNo) + 5;
        fSpeed = 1500.f / nWeaponSpeed;
        this->stats.attack_speed = floor_int(fSpeed);
        this->stats.attack_speed += this->m_iAddValue[AT_ATK_SPD];
    }

    Cal_AruaAtkSPD();
}

//-------------------------------------------------------------------------------------------------
int
CObjAVT::Get_AttackRange(short nSkillIDX) {
    /// ½ºÅ³¿¡ °ø°Ý °Å¸®°¡ ÀÔ·ÂµÇ¾î ÀÖ´Ù¸é ½ºÅ³°Å¸® ¾Æ´Ï¸é ¹«±â °Å¸®..
    if (nSkillIDX && SKILL_DISTANCE(nSkillIDX)) {
        return SKILL_DISTANCE(nSkillIDX);
    }

    if (this->GetCur_MOVE_MODE() <= MOVE_MODE_RUN) {
        /// ¾îÅÃ ·¹ÀÎÁö¸¦ ±¸ÇÑ´Ù. ¿À¸¥ÂÊ ¹«±â°¡ ÀÖÀ¸¸é ±× ¹«±âÀÇ ·¹ÀÎÁö ¸®ÅÏ.
        if (Get_R_WEAPON()) {
            return (int)(WEAPON_ATTACK_RANGE(this->Get_R_WEAPON()) + (Get_SCALE() * 120));
        }
    } else {
        // if ( this->m_nRideItemIDX[ RIDE_PART_ARMS ] ) {
        if (this->m_RideITEM[RIDE_PART_ARMS].m_nItemNo) {
            // ÀåÂøµÈ ¹«±â°¡ ÀÖ´Ù.
            // return (int) ( PAT_ITEM_ATK_RANGE( this->m_nRideItemIDX[RIDE_PART_ARMS] ) + (
            // Get_SCALE() * 120 ) );
            return (int)(PAT_ITEM_ATK_RANGE(this->m_RideITEM[RIDE_PART_ARMS].m_nItemNo)
                + (Get_SCALE() * 120));
        }
    }

    // Default attack range...
    return (int)(Def_AttackRange() + (this->Get_SCALE() * 120));
}

bool
CObjAVT::Make_gsv_ADD_OBJECT(classPACKET* pCPacket) {
    this->Init_ADD_CHAR(pCPacket);

    pCPacket->m_HEADER.m_wType = GSV_AVT_CHAR;
    pCPacket->m_HEADER.m_nSize = sizeof(gsv_AVT_CHAR);

    pCPacket->m_gsv_AVT_CHAR.m_btCharRACE = (BYTE)this->m_nCharRACE;
    pCPacket->m_gsv_AVT_CHAR.m_nRunSpeed = this->total_move_speed();
    pCPacket->m_gsv_AVT_CHAR.m_nPsvAtkSpeed = this->total_attack_speed();
    pCPacket->m_gsv_AVT_CHAR.m_btWeightRate = this->m_btWeightRate;
    pCPacket->m_gsv_AVT_CHAR.m_dwSubFLAG = this->m_IngSTATUS.m_dwSubStatusFLAG;

    ::CopyMemory(pCPacket->m_gsv_AVT_CHAR.m_PartITEM,
        this->m_PartITEM,
        sizeof(tagPartITEM) * MAX_BODY_PART);

    ::CopyMemory(pCPacket->m_gsv_AVT_CHAR.costume,
        this->costume,
        sizeof(tagPartITEM) * MAX_BODY_PART);

    pCPacket->m_gsv_AVT_CHAR.m_sShotItem[SHOT_TYPE_ARROW].m_wShotITEM =
        this->m_Inventory.m_ItemSHOT[SHOT_TYPE_ARROW].GetHEADER();
    pCPacket->m_gsv_AVT_CHAR.m_sShotItem[SHOT_TYPE_BULLET].m_wShotITEM =
        this->m_Inventory.m_ItemSHOT[SHOT_TYPE_BULLET].GetHEADER();
    pCPacket->m_gsv_AVT_CHAR.m_sShotItem[SHOT_TYPE_THROW].m_wShotITEM =
        this->m_Inventory.m_ItemSHOT[SHOT_TYPE_THROW].GetHEADER();

    ::CopyMemory(pCPacket->m_gsv_AVT_CHAR.m_RidingITEM,
        this->m_RideITEM,
        sizeof(tagPartITEM) * MAX_RIDING_PART);

    pCPacket->m_gsv_AVT_CHAR.m_nJOB = this->GetCur_JOB();
    pCPacket->m_gsv_AVT_CHAR.m_btLEVEL = this->GetCur_LEVEL();

    pCPacket->m_gsv_AVT_CHAR.pvp_state = this->pvp_state;

    /*
    if ( this->m_btRideMODE )
        pCPacket->m_tag_ADD_CHAR.m_btMoveMODE = this->m_btRideMODE;
    */
    pCPacket->AppendString(this->Get_RNAME());

    this->Add_ADJ_STATUS(pCPacket);

    pCPacket->AppendString(this->Get_NAME());

    // °³ÀÎ »óÁ¡ »óÅÂ¸é »óÁ¡ Å¸ÀÌÆ²...
    if (this->m_IngSTATUS.IsSubSET(FLAG_SUB_STORE_MODE)) {
#ifdef __INC_PLATINUM
        short nStoreSkin = this->m_GrowAbility.GetStoreSKIN(this->GetCurAbsSEC());
        pCPacket->AppendData(&nStoreSkin, sizeof(short));
#endif
        pCPacket->AppendString(this->Get_StoreTITLE());
    } else if (this->m_IngSTATUS.IsSubSET(FLAG_SUB_INTRO_CHAT)) {
        pCPacket->AppendString(this->Get_StoreTITLE());
    }

    if (this->GetClanID()) {
        pCPacket->AppendData(&this->m_CLAN, sizeof(tag_CLAN_ID));
        pCPacket->AppendString(this->GetClanNAME());
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
tagMOTION*
CObjAVT::Get_MOTION(short nActionIdx) {
#ifdef _DEBUG
    if (nActionIdx >= g_TblAniTYPE.row_count) {
        assert(0 && "nActionIdx >= g_TblAniTYPE.row_count");
        return NULL;
    }
#endif

    short nFileIDX, nType;
    tagMOTION* pMOTION;

    if (!this->m_btRideMODE) {
        // ¿À¸¥¼Õ ¹«±â Á¾·ù¿¡µû¶ó...
        // nFileIDX = FILE_MOTION( WEAPON_MOTION_TYPE(this->m_nRWeaponIDX), nActionIdx );
        nFileIDX = FILE_MOTION(WEAPON_MOTION_TYPE(this->m_sRWeaponIDX.m_nItemNo), nActionIdx);
        nType = this->IsFemale();
    } else {
        nFileIDX = FILE_MOTION(this->m_nPatAniCOL, nActionIdx);
        nType = 0;
    }

    if (0 == nFileIDX) {
        // ¸ð¼ÇÀÌ ¾øÀ¸¸é ¸Ç¼Õ ¸ð¼ÇÀ¸·Î ´ëÃ¼ ??? :: 0¹ø ÆÄÀÏ ¾ø´Âµ¥ ??..
        // nFileIDX = FILE_MOTION( 0, nActionIdx );
        return NULL;
    }

    // TODO:: °°Àº µ¿ÀÛ¿¡ ´ëÇØ¼­´Â ³²/¿© ¸ðµÎ ÇÁ·¹ÀÓÀÌ °°´Ù.
    // Á¾Á·ÀÌ Æ²¸®¸é ???
    pMOTION = g_MotionFILE.IDX_GetMOTION(nFileIDX, nType);
    if (pMOTION) {
        pMOTION->m_nActionIdx = nActionIdx;
    }
    return pMOTION;
}

//-------------------------------------------------------------------------------------------------
static short s_nEquipIdxToPartNO[] = {
    -1, // 0
    BODY_PART_FACE_ITEM, // EQUIP_IDX_FACE_ITEM = 1,
    BODY_PART_HELMET, // EQUIP_IDX_HELMET,
    BODY_PART_ARMOR, // EQUIP_IDX_ARMOR,
    BODY_PART_KNAPSACK, // EQUIP_IDX_KNAPSACK,
    BODY_PART_GAUNTLET, // EQUIP_IDX_GAUNTLET,
    BODY_PART_BOOTS, // EQUIP_IDX_BOOTS,	// 5
    BODY_PART_WEAPON_R, // EQUIP_IDX_WEAPON_R,
    BODY_PART_WEAPON_L, // EQUIP_IDX_WEAPON_L,
    -1, // EQUIP_IDX_NECKLACE,
    -1, // EQUIP_IDX_RING,
};
void
CObjAVT::SetPartITEM(short nEquipInvIDX) {
    COMPILE_TIME_ASSERT(EQUIP_IDX_FACE_ITEM == 1);
    COMPILE_TIME_ASSERT(EQUIP_IDX_HELMET == 2);
    COMPILE_TIME_ASSERT(EQUIP_IDX_ARMOR == 3);
    COMPILE_TIME_ASSERT(EQUIP_IDX_KNAPSACK == 4);
    COMPILE_TIME_ASSERT(EQUIP_IDX_GAUNTLET == 5);
    COMPILE_TIME_ASSERT(EQUIP_IDX_BOOTS == 6);
    COMPILE_TIME_ASSERT(EQUIP_IDX_WEAPON_R == 7);
    COMPILE_TIME_ASSERT(EQUIP_IDX_WEAPON_L == 8);
    COMPILE_TIME_ASSERT(EQUIP_IDX_NECKLACE == 9);
    COMPILE_TIME_ASSERT(EQUIP_IDX_RING == 10);

    short nPartNO = s_nEquipIdxToPartNO[nEquipInvIDX];
    if (nPartNO > 0) {
        this->SetPartITEM(nPartNO, m_Inventory.m_ItemLIST[nEquipInvIDX]);
    }

    if (this->GetCur_MOVE_MODE() <= MOVE_MODE_RUN)
        this->UpdateAbility(); // ÆÄÆ® ¾ÆÀÌÅÛ ¼³Á¤
}
void
CObjAVT::SetRideITEM(short nRideInvIDX) {
    //	m_nRideItemIDX[ nRideInvIDX ] = this->m_Inventory.m_ItemRIDE[ nRideInvIDX ].GetItemNO();

    m_RideITEM[nRideInvIDX].m_nItemNo = this->m_Inventory.m_ItemRIDE[nRideInvIDX].GetItemNO();
    m_RideITEM[nRideInvIDX].m_nGEM_OP = this->m_Inventory.m_ItemRIDE[nRideInvIDX].GetGemNO();
    m_RideITEM[nRideInvIDX].m_cGrade = this->m_Inventory.m_ItemRIDE[nRideInvIDX].GetGrade();
    m_RideITEM[nRideInvIDX].m_bHasSocket = this->m_Inventory.m_ItemRIDE[nRideInvIDX].m_bHasSocket;

    if (this->GetCur_MOVE_MODE() == MOVE_MODE_DRIVE)
        this->UpdateAbility(); // Å¾½Â ¾ÆÀÌÅÛ ¼³Á¤
}

void
CObjAVT::set_costume_item(short equip_idx) {
    short part_index = s_nEquipIdxToPartNO[equip_idx - INVENTORY_COSTUME_ITEM0];
    if (part_index < 0) {
        return;
    }

    this->costume[part_index].m_nItemNo = this->m_Inventory.m_ItemLIST[equip_idx].GetItemNO();
    this->costume[part_index].m_nGEM_OP = this->m_Inventory.m_ItemLIST[equip_idx].GetGemNO();
    this->costume[part_index].m_cGrade = this->m_Inventory.m_ItemLIST[equip_idx].GetGrade();
    this->costume[part_index].m_bHasSocket = this->m_Inventory.m_ItemLIST[equip_idx].m_bHasSocket;
}

//-------------------------------------------------------------------------------------------------
void
CObjAVT::Set_TargetIDX(int iTargetIndex, bool bCheckHP) {
    if (Get_TargetIDX() == iTargetIndex)
        return;

    CObjCHAR* pTarget;
    if (Get_TargetIDX()) {
        pTarget = (CObjCHAR*)this->Get_TargetOBJ(
            true); // HP Ã¼Å©µÈ CAI_OBJ¸¦ »ó¼Ó¹ÞÀº CObjCHAR°¡ ³Ñ¾î ¿Â´Ù.
        if (pTarget) {
            pTarget->Sub_FromTargetLIST(this);
        }
    }

    this->CObjTARGET::Set_TargetIDX(iTargetIndex);

    if (Get_TargetIDX()) {
        pTarget = (CObjCHAR*)this->Get_TargetOBJ(
            bCheckHP); // HP Ã¼Å©µÈ CAI_OBJ¸¦ »ó¼Ó¹ÞÀº CObjCHAR°¡ ³Ñ¾î ¿Â´Ù.
        if (pTarget) {
            pTarget->Add_ToTargetLIST(this);
            this->Send_gsv_HP_REPLY(Get_TargetIDX(), pTarget->Get_HP());
        } else {
            // Ä³¸¯ÅÍ°¡ ¾Æ´Ï°Å³ª Á×¾ú´Ù...
            CObjTARGET::Set_TargetIDX(0);
        }
    }
}

//-------------------------------------------------------------------------------------------------
bool
CObjAVT::SetCMD_TOGGLE(BYTE btTYPE, bool bForce) {
#ifdef __BLOCK_WHEN_SKILL
    // 2005.08.03 : icarus :: Á¶°Ç °Ë»ö¾øÀÌ °­Á¦ Åä±Û ½ÃÅ³¼ö ÀÖµµ·Ï ¼öÁ¤...
    if (!bForce && Get_ActiveSKILL()) {
        return true;
    }
#endif

    switch (btTYPE) {
        case TOGGLE_TYPE_RUN: {
            if (this->m_btRideMODE) {
                return true;
            }
            if (this->m_btWeightRate >= WEIGHT_RATE_WALK) {
                return true;
            }

            this->m_bRunMODE = !this->m_bRunMODE;
            if (!m_bRunMODE) {
                this->stats.move_speed = Rose::GameStaticConfig::DEFAULT_WALK_SPEED;;
            } else {
                this->update_speed();
            }

            if (Get_STATE() == CS_MOVE) {
                this->m_fCurMoveSpeed = this->total_move_speed();
                this->Reset_MoveVEC();
            }
            btTYPE = TOGGLE_TYPE_DRIVE + this->Get_MoveMODE();
            break;
        }

        case TOGGLE_TYPE_SIT: {
            if (this->m_btRideMODE) {
                return true;
            }

            switch (this->Get_COMMAND()) {
                case CMD_STOP:
                    CObjAI::SetCMD_SIT();
                    break;

                case CMD_SIT:
                    CObjAI::SetCMD_STAND();
                    this->Send_gsv_SET_HPnMP(0x03);
                    break;

                default:
                    // ¾ÉÀ»¼ö ¾ø´Â °æ¿ì...
                    return true;
            }
            break;
        }

        case TOGGLE_TYPE_DRIVE: {
            if (this->m_btRideMODE) {
                if (this->m_iLinkedCartObjIDX) {
                    classUSER* pUSER = g_pObjMGR->Get_UserOBJ(this->m_iLinkedCartObjIDX);
                    if (RIDE_MODE_GUEST == pUSER->GetCur_RIDE_MODE()) {
                        // ÅÂ¿ü´ø ¼Õ´Ôµµ ³»¸®°Ô...
                        pUSER->m_btRideMODE = 0;
                    }
                    pUSER->m_iLinkedCartObjIDX = 0;
                }

                this->m_btRideMODE = 0;
                this->m_iLinkedCartObjIDX = 0;
                this->m_IngSTATUS.ClearAllGOOD();
            } else {
                if (this->m_Inventory.m_ItemRIDE[RIDE_PART_BODY].IsEmpty()) {
                    return true;
                }

                const int item_type = PAT_ITEM_TYPE(this->m_Inventory.m_ItemRIDE[RIDE_PART_BODY].GetItemNO());
                if (item_type != TUNING_PART_BODY_MOUNT) {
                    for (short nI = 0; nI < MAX_RIDING_PART - 2; nI++) {
                        if (ITEM_TYPE_RIDE_PART != this->m_Inventory.m_ItemRIDE[nI].GetTYPE()) {
                            return true;
                        }
                    }
                }

                if (ZONE_RIDING_REFUSE_FLAG(this->GetZONE()->Get_ZoneNO())) {
                    if ((item_type % 3) & ZONE_RIDING_REFUSE_FLAG(this->GetZONE()->Get_ZoneNO())) {
                        return true;
                    }
                }

#ifdef __KCHS_BATTLECART__
                if (this->GetCur_PatHP() <= 0) // Ä«Æ®Ã¼·ÂÀÌ 0ÀÌ¸é Ä«Æ®¸¦ ¼ÒÈ¯ÇÒ ¼ö ¾ø´Ù
                    return true;
#endif

                this->Dec_EngineLife(); // ½ÂÂ÷½Ã ¿¬·á °¨¼Ò
                this->m_btRideMODE = RIDE_MODE_DRIVE;

                // Å¾½Â½Ã ¸ðµç À¯¸® »óÅÂ ÇØÁö...
                this->m_IngSTATUS.ClearAllGOOD();
            }
            this->m_dwRecoverTIME = 0;

            this->UpdateAbility();
            btTYPE = TOGGLE_TYPE_DRIVE + this->Get_MoveMODE();
            return this->Send_gsv_TOGGLE(btTYPE, true);
        }
    }

    // except TOGGLE_TYPE_DRIVE
    this->Send_gsv_TOGGLE(btTYPE, true);

    return true;
}

//-------------------------------------------------------------------------------------------------
void
CObjAVT::SetCur_SEX(char cValue) {
    this->m_nCharRACE &= 0x0fffe; // ÇÏÀ§ºñÆ® ³¯¸®°í~
    this->m_nCharRACE |= (cValue & 0x01); // ÇÏÀ§ºñÆ® ¼³Á¤ÇÏ°í~
}

int
CObjAVT::GetCur_FUEL() {
    tagITEM* pEngine = &m_Inventory.m_ItemRIDE[RIDE_PART_ENGINE];
    if (ITEM_TYPE_RIDE_PART != pEngine->GetTYPE())
        return 0;
    return pEngine->m_nLife;
}
void
CObjAVT::SubCur_FUEL(short nValue) {
    tagITEM* pEngine = &m_Inventory.m_ItemRIDE[RIDE_PART_ENGINE];
    if (ITEM_TYPE_RIDE_PART != pEngine->GetTYPE())
        return;

    if (pEngine->m_nLife > nValue)
        pEngine->m_nLife -= nValue;
    else {
        // ¼ö¸í 0µÆ´Ù...
        pEngine->m_nLife = 0;
        this->Send_gsv_SET_ITEM_LIFE(INVENTORY_RIDE_ITEM0 + RIDE_PART_ENGINE, 0);
        // this->SetCMD_STOP ();
    }
}

int
CObjAVT::Get_AbilityValue(WORD wType) {
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
            return GetCur_FACE();
        case AT_HAIR:
            return GetCur_HAIR();

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
            return this->total_attack_power();
        case AT_DEF:
            return GetCur_DEF();
        case AT_HIT:
            return this->total_hit_rate();
        case AT_RES:
            return GetCur_RES();
        case AT_AVOID:
            return GetCur_AVOID();
        case AT_SPEED:
            return this->total_move_speed();
        case AT_ATK_SPD:
            return this->total_attack_speed();
        case AT_WEIGHT:
            return GetCur_WEIGHT();
        case AT_CRITICAL:
            return Get_CRITICAL();

        case AT_SAVE_MP:
            return GetCur_SaveMP();
        case AT_DROP_RATE:
            return GetCur_DropRATE();

        case AT_FAME_G:
            return GetCur_FameG();
        case AT_FAME_B:
            return GetCur_FameB();

        case AT_STAMINA:
            return GetCur_STAMINA();

        case AT_GUILD_NO:
            return GetClanID();
        case AT_GUILD_POS:
            return GetClanPOS();
        case AT_GUILD_SCORE:
            return GetClanSCORE();

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

        case AT_PK_FLAG:
            return GetCur_PK_FLAG(); // TODO JV: Replace with this->pvp_state? This would allow us to make gear that is only usable when pvp is enabled
        case AT_TEAM_NO:
            return Get_TeamNO();

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

        case AT_CURRENT_PLANET:
            if (this->GetZONE())
                return ZONE_PLANET_NO(this->GetZONE()->Get_ZoneNO());
            return -1;

        default:
            if (wType >= AT_UNION_POINT1 && wType <= AT_UNION_POINT10)
                return GetCur_UnionPOINT(wType - AT_UNION_POINT1 + 1);
    }

    return 0;
}

//-------------------------------------------------------------------------------------------------
void
CObjAVT::Stamp_AttackTIME() {
    this->m_dwAttackTIME = this->GetZONE()->GetTimeGetTIME();
}

int
CObjAVT::Save_Damage(int iAttackerIDX, int iDamage) {
    this->Stamp_AttackTIME();

    // ¹æ¾î±¸ ¼ö¸í °¨¼Ò
    int iRand = 1 + RANDOM(400);
    if (iRand >= 101)
        return 0;

    tagITEM* pItem;
    short nInvIDX;

    if (this->Get_MoveMODE() == MOVE_MODE_DRIVE) {
        if (iRand >= 51)
            nInvIDX = INVENTORY_RIDE_ITEM0 + RIDE_PART_BODY; // ¹Ùµð
        else if (iRand >= 21)
            nInvIDX = INVENTORY_RIDE_ITEM0 + RIDE_PART_LEG; // ·¹±×
        else
        // if ( iRand >= 16 )
        {
            nInvIDX = INVENTORY_RIDE_ITEM0 + RIDE_PART_ARMS; // ¿þÆù
        }
        // else				nInvIDX =  INVENTORY_RIDE_ITEM0 + RIDE_PART_ENGINE;	// ¿£Áø
    } else {
        pItem = &m_Inventory.m_ItemLIST[EQUIP_IDX_WEAPON_L];
        if (pItem->GetLife()
            && WEAPON_ITEM_SHIELD == ITEM_TYPE(pItem->GetTYPE(), pItem->GetItemNO())) {
            // ¹æÆÐ ÀÖÀ½
            if (iRand > 60)
                nInvIDX = EQUIP_IDX_WEAPON_L; // ¹æÆÐ, ¿Þ¼Õ¹«±â
            else if (iRand > 30)
                nInvIDX = EQUIP_IDX_ARMOR; // °©¿Ê
            else if (iRand > 22)
                nInvIDX = EQUIP_IDX_BOOTS; // ½Å¹ß
            else if (iRand > 16)
                nInvIDX = EQUIP_IDX_HELMET; // ¸Ó¸®
            else if (iRand > 9)
                nInvIDX = EQUIP_IDX_KNAPSACK; // µî
            else if (iRand > 5)
                nInvIDX = EQUIP_IDX_GAUNTLET; // Àå°©
            else
                nInvIDX = EQUIP_IDX_FACE_ITEM; // ¾ó±¼(¾Ç¼¼»ç¸®)
        } else {
            // ¹æÆÐ ¾øÀ½
            if (iRand > 64)
                nInvIDX = EQUIP_IDX_ARMOR; // °©¿Ê
            else if (iRand > 46)
                nInvIDX = EQUIP_IDX_BOOTS; // ½Å¹ß
            else if (iRand > 31)
                nInvIDX = EQUIP_IDX_GAUNTLET; // Àå°©
            else if (iRand > 15)
                nInvIDX = EQUIP_IDX_KNAPSACK; // µî
            else if (iRand > 7)
                nInvIDX = EQUIP_IDX_HELMET; // ¸Ó¸®
            else
                nInvIDX = EQUIP_IDX_FACE_ITEM; // ¾ó±¼(¾Ç¼¼»ç¸®)
        }
    }

    pItem = &m_Inventory.m_ItemLIST[nInvIDX];
    if (0 == pItem->GetHEADER() || pItem->GetLife() <= 0)
        return 0;

    iRand = 1 + RANDOM(120) - (pItem->GetDurability() + 10 - (int)(iDamage * 0.1f));
    if (iRand >= 0) {
        // ³»±¸µµ 1 °¨¼Ò, 10´ÜÀ§ º¯È­°¡ ÀÖÀ¸¸é Å¬¶óÀÌ¾ðÆ®¿¡ Àü¼Û
        if (pItem->GetLife() > 1) {
            pItem->m_nLife--;
            if (pItem->GetLife() % 10 == 0) {
                // º¯°æµÈ ¼ö¸í Àü¼Û...
                this->Send_gsv_SET_ITEM_LIFE(nInvIDX, pItem->GetLife());
            }
        } else {
            pItem->m_nLife = 0;
            this->Send_gsv_SET_ITEM_LIFE(nInvIDX, 0);

            short nCurSpeed = this->stats.move_speed;
            this->UpdateAbility(); // ¹æ¾î±¸ ¼ö¸í = 0
            if (nCurSpeed != this->stats.move_speed) {
                this->Send_gsv_SPEED_CHANGED(false);
            }
        }
    }

    return 0;
}

//-------------------------------------------------------------------------------------------------
void
CObjAVT::Dec_EngineLife() {
    tagITEM* pEngine = &m_Inventory.m_ItemRIDE[RIDE_PART_ENGINE];
    if (ITEM_TYPE_RIDE_PART != pEngine->GetTYPE())
        return;

    int iUseValue = PAT_ITEM_USE_FUEL_RATE(pEngine->GetItemNO());
    if (pEngine->m_nLife > iUseValue) {
        int iDiv10 = pEngine->m_nLife / 10;
        pEngine->m_nLife -= iUseValue;
        if (iDiv10 != (pEngine->m_nLife / 10))
            this->Send_gsv_SET_ITEM_LIFE(INVENTORY_RIDE_ITEM0 + RIDE_PART_ENGINE,
                pEngine->GetLife());
    } else {
        // ¼ö¸í 0µÆ´Ù...
        pEngine->m_nLife = 0;
        this->Send_gsv_SET_ITEM_LIFE(INVENTORY_RIDE_ITEM0 + RIDE_PART_ENGINE, 0);
        this->SetCMD_STOP();
    }
}

//-------------------------------------------------------------------------------------------------
void
CObjAVT::Dec_WeaponLife() {
    tagITEM* pWeapon;
    short nInvIDX;

    if (this->GetCur_MOVE_MODE() == MOVE_MODE_DRIVE) {
        this->Dec_EngineLife(); // °ø°Ý½Ã ¸¶´Ù ¿£Áø ¿¬·á °¨¼Ò...

        // PAT
        nInvIDX = INVENTORY_RIDE_ITEM0 + RIDE_PART_ARMS;
    } else {
        nInvIDX = EQUIP_IDX_WEAPON_R;
    }

    pWeapon = &m_Inventory.m_ItemLIST[nInvIDX];
    if (0 == pWeapon->GetHEADER() || pWeapon->GetLife() <= 0)
        return;

    if (RANDOM(710) + 1 - (pWeapon->GetDurability() + 600) >= 0) {
        // ¼ö¸í 1 °¨¼Ò
        if (pWeapon->GetLife() > 1) {
            pWeapon->m_nLife--;
            if (pWeapon->GetLife() % 10 == 0) {
                // º¯°æµÈ ¼ö¸í Àü¼Û...
                this->Send_gsv_SET_ITEM_LIFE(nInvIDX, pWeapon->GetLife());
            }
        } else {
            pWeapon->m_nLife = 0;
            this->Send_gsv_SET_ITEM_LIFE(nInvIDX, 0);

            short nCurSpeed = this->stats.move_speed;
            this->UpdateAbility(); // ¹«±â ¼ö¸í = 0
            if (nCurSpeed != this->stats.move_speed)
                this->Send_gsv_SPEED_CHANGED(false);
        }
    }
}

//-------------------------------------------------------------------------------------------------
bool
CObjAVT::Send_gsv_GODDNESS_MODE(BYTE btOnOff) {
    classPACKET* pCPacket = Packet_AllocNLock();
    if (!pCPacket)
        return false;

    pCPacket->m_HEADER.m_wType = GSV_GODDNESS_MODE;
    pCPacket->m_HEADER.m_nSize = sizeof(gsv_GODDNESS_MODE);
    pCPacket->m_gsv_GODDNESS_MODE.m_btOnOff = btOnOff;
    pCPacket->m_gsv_GODDNESS_MODE.m_wObjectIDX = this->Get_INDEX();

    this->GetZONE()->SendPacketToSectors(this, pCPacket);

    Packet_ReleaseNUnlock(pCPacket);

    return true;
}

int
CObjAVT::Cal_ATTACK() {
    int iAP = 0;

    tagITEM* pRightWPN = this->Get_EquipItemPTR(EQUIP_IDX_WEAPON_R);
    int iWeaponAP;

    if (pRightWPN->GetHEADER() && pRightWPN->GetLife() > 0)
        iWeaponAP = WEAPON_ATTACK_POWER(pRightWPN->m_nItemNo);
    else
        iWeaponAP = 0;

    if (this->GetCur_MOVE_MODE() <= MOVE_MODE_RUN) {
        t_eSHOT ShotTYPE = pRightWPN->GetShotTYPE();
        if (ShotTYPE < MAX_SHOT_TYPE) {
            tagITEM* pShotITEM = &this->m_Inventory.m_ItemSHOT[ShotTYPE];

            short nItemQ, nItemW;
            if (pShotITEM->IsEtcITEM()) {
                nItemQ = ITEM_QUALITY(pShotITEM->GetTYPE(), pShotITEM->GetItemNO());
                nItemW = ITEM_WEIGHT(pShotITEM->GetTYPE(), pShotITEM->GetItemNO());
            } else {
                nItemQ = nItemW = 0;
            }

            switch (ShotTYPE) {
                case SHOT_TYPE_ARROW:
                    iAP = (int)((GetCur_DEX() * 0.62f + GetCur_STR() * 0.2f + GetCur_LEVEL() * 0.2f
                                    + nItemQ)
                        + (((iWeaponAP + ITEMGRADE_ATK(pRightWPN->GetGrade())) + nItemQ * 0.5f + 8)
                            * (GetCur_DEX() * 0.04f + GetCur_SENSE() * 0.03f + 29) / 30.f));
                    break;
                case SHOT_TYPE_BULLET:
                    iAP = (int)((GetCur_DEX() * 0.4f + GetCur_CON() * 0.5f + GetCur_LEVEL() * 0.2f
                                    + nItemQ)
                        + (((iWeaponAP + ITEMGRADE_ATK(pRightWPN->GetGrade())) + nItemQ * 0.6f + 8)
                            * (GetCur_CON() * 0.03f + GetCur_SENSE() * 0.05f + 29) / 30));
                    break;

                case SHOT_TYPE_THROW:
                    iAP = (int)((GetCur_STR() * 0.52f + GetCur_CON() * 0.5f + GetCur_LEVEL() * 0.2f
                                    + nItemQ)
                        + (((iWeaponAP + ITEMGRADE_ATK(pRightWPN->GetGrade())) + nItemQ + 12)
                            * (GetCur_CON() * 0.04f + GetCur_SENSE() * 0.05f + 29) / 30));
            }
        } else {
            switch (WEAPON_TYPE(pRightWPN->m_nItemNo) / 10) {
                case 21: // ÇÑ¼Õ
                case 22: // ¾ç¼Õ		// ±ÙÁ¢ ¹«±â
                    iAP = (int)((GetCur_STR() * 0.75f + GetCur_LEVEL() * 0.2f)
                        + ((iWeaponAP + ITEMGRADE_ATK(pRightWPN->GetGrade()))
                            * (GetCur_STR() * 0.05f + 29) / 30.f));
                    break;
                case 24: // ¸¶¹ý ¹«±â
                    if (241 == WEAPON_TYPE(pRightWPN->m_nItemNo)) {
                        // ¸¶¹ý ÁöÆÎÀÌ.
                        iAP = (int)((GetCur_STR() * 0.4f + GetCur_INT() * 0.4f
                                        + GetCur_LEVEL() * 0.2f)
                            + ((iWeaponAP + ITEMGRADE_ATK(pRightWPN->GetGrade()))
                                * (GetCur_INT() * 0.05f + 29) / 30.f));
                    } else {
                        iAP = (int)((GetCur_INT() * 0.6f + GetCur_LEVEL() * 0.2f)
                            + ((iWeaponAP + ITEMGRADE_ATK(pRightWPN->GetGrade()))
                                * (GetCur_SENSE() * 0.1f + 26) / 27.f));
                    }
                    break;
                case 25: // Ä«Å¸¸£
                    if (252 == WEAPON_TYPE(pRightWPN->m_nItemNo)) {
                        // ÀÌµµ·ù
                        iAP = (int)((GetCur_STR() * 0.63f + GetCur_DEX() * 0.45f
                                        + GetCur_LEVEL() * 0.2f)
                            + ((iWeaponAP + ITEMGRADE_ATK(pRightWPN->GetGrade()))
                                * (GetCur_DEX() * 0.05f + 25) / 26.f));
                    } else {
                        iAP = (int)((GetCur_STR() * 0.42f + GetCur_DEX() * 0.55f
                                        + GetCur_LEVEL() * 0.2f)
                            + ((iWeaponAP + ITEMGRADE_ATK(pRightWPN->GetGrade()))
                                * (GetCur_DEX() * 0.05f + 20) / 21.f));
                    }
                    break;
                case 0: // ½ºÅ³¸Ç¼Õ °ø°Ý·Â : ¹«µµ°¡ Á÷¾÷¿¡ Æ¯ÇÑµÈ ½ºÅ³·Î, ÆÐ½Ãºê ¼º°ÝÀÇ ¸Ç¼Õ
                        // °ø°Ý·ÂÀ» Á¦°øÇÑ´Ù.
                    iAP = (int)(GetCur_STR() * 0.5f + GetCur_DEX() * 0.3f + GetCur_LEVEL() * 0.2f);
                    break;
            }
        }

        // this->m_nPassiveAttackSpeed = this->GetPassiveSkillAttackSpeed( pRightWPN->m_nItemNo );

        iAP += this->m_iAddValue[AT_ATK];
        this->stats.attack_power =
            iAP + this->GetPassiveSkillAttackPower(iAP, pRightWPN->m_nItemNo);
    } else {
        t_eSHOT ShotTYPE = pRightWPN->GetShotTYPE();
        /// ¼ö¸íÀÌ ´ÙÇÑ ¹«±â´Â ¹«±âÀÇ ±âº» °ø°Ý·ÂÀ» 0 À¸·Î..
        if (pRightWPN->GetHEADER() && pRightWPN->GetLife() > 0)
            iWeaponAP = WEAPON_ATTACK_POWER(pRightWPN->m_nItemNo);
        else
            iWeaponAP = 0;
        int iWeaponTERM = iWeaponAP + ITEMGRADE_ATK(pRightWPN->GetGrade())
            + PAT_ITEM_ATK_POW(this->m_Inventory.m_ItemRIDE[RIDE_PART_ARMS].GetItemNO());

        iAP = (GetCur_LEVEL() * 3) + GetCur_CON()
            + PAT_ITEM_ATK_POW(this->m_Inventory.m_ItemRIDE[RIDE_PART_ARMS].GetItemNO());
        this->stats.attack_power = iAP + this->m_iAddValue[AT_ATK];
    }

    this->Cal_AruaATTACK();

    return this->stats.attack_power;
}

int
CObjAVT::Cal_HIT() {
    int iHitRate;
    tagITEM* pRightWPN;

    if (this->GetCur_MOVE_MODE() <= MOVE_MODE_RUN) {
        pRightWPN = this->Get_EquipItemPTR(EQUIP_IDX_WEAPON_R);
        if (pRightWPN->GetItemNO() && pRightWPN->GetLife() > 0) {
            iHitRate = (int)((GetCur_CON() + 10) * 0.8f)
                + (int)((ITEM_QUALITY(ITEM_TYPE_WEAPON, pRightWPN->GetItemNO())) * 0.6f
                    + ITEMGRADE_HIT(pRightWPN->GetGrade()) + pRightWPN->GetDurability() * 0.8f);
        } else {
            iHitRate = (int)((GetCur_CON() + 10) * 0.5f + 15);
        }
    } else {
        pRightWPN = &this->m_Inventory.m_ItemRIDE[RIDE_PART_ARMS];
        if (pRightWPN->GetLife() > 0)
            iHitRate = (int)((GetCur_CON() + 10) * 0.8f) + (GetCur_LEVEL() * 0.5f)
                + (int)(ITEM_QUALITY(ITEM_TYPE_RIDE_PART, pRightWPN->GetItemNO()) * 1.2f);
        else
            iHitRate = 0;
    }

    this->stats.hit_rate = iHitRate + this->m_iAddValue[AT_HIT];

    iHitRate = this->GetPassiveSkillValue(AT_PSV_HIT)
        + (short)(this->stats.hit_rate * this->GetPassiveSkillRate(AT_PSV_HIT) / 100.f);
    this->stats.hit_rate += iHitRate;

    this->Cal_AruaHIT();
    return this->stats.hit_rate;
}

//-------------------------------------------------------------------------------------------------
//#define	ARUA_RATE_ATK	0.1f
//#define	ARUA_RATE_HIT	0.1f
//#define	ARUA_RATE_AVD	0.1f

#define ARUA_RATE_ATK_SPD 0.2f
#define ARUA_RATE_RUN_SPD 0.2f
#define ARUA_RATE_CRI 0.2f
#define ARUA_RATE_MAX_HP 0.2f
#define ARUA_RATE_MAX_MP 0.2f
#define ARUA_RATE_RCV_HP 0.5f
#define ARUA_RATE_RCV_MP 0.5f
#define ARUA_RATE_ATK 0.2f
#define ARUA_RATE_RES 0.2f

void
CObjAVT::Cal_AruaAtkSPD(void) {
    // if ( this->m_IngSTATUS.IsSubSET( FLAG_SUB_ARUA_FAIRY ) ) {
    //	this->m_IngSTATUS.m_nAruaAtkSPD = (short)( this->total_attack_speed() * ARUA_RATE_ATK_SPD );
    //} else
    //	this->m_IngSTATUS.m_nAruaAtkSPD = 0;
}
void
CObjAVT::Cal_AruaRunSPD(void) {
    if (this->m_IngSTATUS.IsSubSET(FLAG_SUB_ARUA_FAIRY)) {
        this->m_IngSTATUS.m_nAruaRunSPD =
            (short)(this->stats.move_speed * ARUA_RATE_RUN_SPD); // 0.2f
    } else
        this->m_IngSTATUS.m_nAruaRunSPD = 0;
}

void
CObjAVT::Cal_AruaMaxHP(void) {
    if (this->m_IngSTATUS.IsSubSET(FLAG_SUB_ARUA_FAIRY)) {
        this->m_IngSTATUS.m_nAruaMaxHP = (short)(this->GetOri_MaxHP() * ARUA_RATE_MAX_HP); // 0.2f
    } else
        this->m_IngSTATUS.m_nAruaMaxHP = 0;
}
void
CObjAVT::Cal_AruaMaxMP(void) {
    if (this->m_IngSTATUS.IsSubSET(FLAG_SUB_ARUA_FAIRY)) {
        this->m_IngSTATUS.m_nAruaMaxMP = (short)(this->GetOri_MaxMP() * ARUA_RATE_MAX_MP); // 0.2f
    } else
        this->m_IngSTATUS.m_nAruaMaxMP = 0;
}

void
CObjAVT::Cal_AruaATTACK(void) {
    if (this->m_IngSTATUS.IsSubSET(FLAG_SUB_ARUA_FAIRY)) {
        this->m_IngSTATUS.m_nAruaATK = (short)(this->stats.attack_power * ARUA_RATE_ATK);
    } else
        this->m_IngSTATUS.m_nAruaATK = 0;
}
void
CObjAVT::Cal_AruaHIT(void) {}

void
CObjAVT::Cal_AruaAVOID(void) {}

void
CObjAVT::Cal_AruaCRITICAL(void) {
    if (this->m_IngSTATUS.IsSubSET(FLAG_SUB_ARUA_FAIRY)) {
        this->m_IngSTATUS.m_nAruaCRITICAL = (short)(GetOri_CRITICAL() * ARUA_RATE_CRI);
    } else
        this->m_IngSTATUS.m_nAruaCRITICAL = 0;
}

void
CObjAVT::Cal_AruaRES(void) {
    if (this->m_IngSTATUS.IsSubSET(FLAG_SUB_ARUA_FAIRY)) {
        this->m_IngSTATUS.m_nAruaRES = (short)(this->GetOri_DEF() * ARUA_RATE_RES);
    } else
        this->m_IngSTATUS.m_nAruaRES = 0;
}

bool
CObjAVT::Add_Goddess(void) {
    this->m_IngSTATUS.SetSubFLAG(FLAG_SUB_ARUA_FAIRY);

    Cal_AruaMaxHP();
    Cal_AruaMaxMP();

    Cal_AruaRunSPD();
    Cal_AruaAtkSPD();

    Cal_AruaATTACK();
    Cal_AruaHIT();
    Cal_AruaAVOID();

    Cal_AruaCRITICAL();
    Cal_AruaRES();

    return this->Send_gsv_GODDNESS_MODE(1);
}
bool
CObjAVT::Del_Goddess(void) {
    DWORD dwIsSet = this->m_IngSTATUS.IsSubSET(FLAG_SUB_ARUA_FAIRY);

    this->m_IngSTATUS.DelArua();
    if (this->GetCur_HP() > this->Get_MaxHP())
        this->SetCur_HP(Get_MaxHP());
    if (this->GetCur_MP() > this->Get_MaxMP())
        this->SetCur_MP(Get_MaxMP());

    return dwIsSet ? this->Send_gsv_GODDNESS_MODE(0) : true;
}

//-------------------------------------------------------------------------------------------------
#define CHECK_GODDNESS_CALL_TIME (60 * 60 * 1000)

#define CHECK_PAT_COOL_TIME (1000)
#define PAT_DECREASE_COOLTIME_PER_SEC (1 * 1000) // 1ÃÊ½Ä °¨¼Ò½ÃÄÑ¼­..

void
CObjAVT::Check_PerFRAME(DWORD dwPassTIME) {
    m_dwGoddnessTIME += dwPassTIME;
    m_dwRecoverTIME += dwPassTIME;

    switch (this->Get_MoveMODE()) {
        case MOVE_MODE_RIDEON:
            break;
        case MOVE_MODE_DRIVE:
            if (m_dwRecoverTIME >= GameStaticConfig::FUEL_DECREASE_TIME) {
                m_dwRecoverTIME -= GameStaticConfig::FUEL_DECREASE_TIME;
                this->Dec_EngineLife();
            }
            break;
        default: // HP / MP È¸º¹
        {
            const DWORD dwCheckTime = GameStaticConfig::RECOVERY_RATE_TIME;
            if (m_dwRecoverTIME >= dwCheckTime) {
                m_dwRecoverTIME -= dwCheckTime;

                int iAdd = 0;
                // Poison (and any HP-draining DOT) suppresses natural HP
                // recovery. Otherwise regen runs on its own RECOVERY_RATE_TIME
                // cadence and gets synced to the client via send_update_hpmp,
                // while poison lowers HP every second through the combat
                // DamageEvent path. The two independent channels fight over the
                // visible bar and produce a small sawtooth "snap" during poison.
                // MP recovery is unaffected and keeps running.
                const bool bPoisoned = this->m_IngSTATUS.IsSET(FLAG_ING_POISONED);
                if (CMD_SIT == Get_COMMAND()) {
                    if (!bPoisoned) {
                        iAdd = this->Get_RecoverHP(GameStaticConfig::RECOVERY_RATE_SITTING);
                        this->Add_HP(iAdd);
                    }

                    iAdd = this->Get_RecoverMP(GameStaticConfig::RECOVERY_RATE_SITTING);
                    this->Add_MP(iAdd);
                } else {
                    if (!bPoisoned) {
                        iAdd =
                            (int)((this->GetAdd_RecoverHP() + (this->Get_CON() + 40) / 6.f) / 6.f);
                        this->Add_HP(iAdd);
                    }

                    // Standing MP regen mirrors the standing HP formula above
                    // and folds in GetAdd_RecoverMP so gear / passive MP
                    // recovery bonuses actually apply outside of sitting.
                    // Previously this branch only regenerated HP, leaving
                    // standing MP regen at a flat 0 even for characters with
                    // recovery stats.
                    iAdd =
                        (int)((this->GetAdd_RecoverMP() + (this->Get_CON() + 40) / 6.f) / 6.f);
                    this->Add_MP(iAdd);
                }
            }
        }
    }
}

//-------------------------------------------------------------------------------------------------
void
CObjAVT::Resurrection(short nSkillIDX) {
    this->m_IngSTATUS.Reset(false);
    this->m_dwRecoverTIME = 0;

    this->Del_ActiveSKILL();
    this->Clear_SummonCNT(); // ºÎÈ°½Ã...
    this->CObjAI::SetCMD_STOP();

    // Á×À»¶§ °¨¼ÒµÈ °æÇèÄ¡ º¹±¸
    this->Cancel_PenalEXP(SKILL_POWER(nSkillIDX));
    // ¸®Á®·º¼Ç ½ºÅ³¿¡ µû¶ó °æÄ¡ º¹±¸·®ÀÌ Æ²·ÁÁö´Ï ÇöÀç °æÄ¡ Àü¼Û :: 2005. 03. 25
    this->Send_gsv_SETEXP(0);

    this->Set_HP(3 * this->GetCur_MaxHP() / 10);

    // ÆÄÆ¼ÀÏ °æ¿ì ÆÄÆ¼¿ø¿¡°Ô ÇÇ Àü¼Û...
    if (this->GetPARTY()) {
        this->GetPARTY()->Change_ObjectIDX((classUSER*)this);
    }
}

//-------------------------------------------------------------------------------------------------
bool
CObjAVT::Is_ALLIED(CAI_OBJ* pDestOBJ) {
    // Player-vs-player alliance uses the pvp_state system (same signal as the
    // melee path in CObjAI::can_attack), not the legacy ZONE_FLAG_PK_ALLOWED
    // global flag, which is no longer set from zone data.
    int destType = pDestOBJ->Get_ObjTYPE();
    if (destType == OBJ_AVATAR || destType == OBJ_USER) {
        CObjCHAR* pDestCHAR = static_cast<CObjCHAR*>(pDestOBJ);

        if (pDestCHAR == this)
            return true;

        // If either side is not flagged for PVP (e.g. a save zone), treat all
        // players as allies so hostile skills/attacks cannot land.
        if (!this->is_pvp_enabled() || !pDestCHAR->is_pvp_enabled())
            return true;

        // PVP is active between both players. Respect the zone's safe-exception:
        // party members in "all except party" zones, guild/clan members in
        // "all except clan" zones remain allied. (Mirrors client IsEnemy.)
        switch (this->pvp_state) {
            case PvpState::AllExceptParty:
                if (this->GetPARTY() && this->GetPARTY() == pDestCHAR->GetPARTY())
                    return true;
                break;
            case PvpState::AllExceptClan:
                if (this->GetGUILD() && this->GetGUILD() == pDestCHAR->GetGUILD())
                    return true;
                break;
            default:
                break;
        }

        // Otherwise they are enemies.
        return false;
    }

    return CAI_OBJ::Is_ALLIED(pDestOBJ);
}

void
CObjAVT::SetCur_UNION(char cValue) {
    if (cValue >= 0 && cValue < MAX_UNION_COUNT) {
        this->m_BasicINFO.m_cUnion = cValue;
    }
}

//-------------------------------------------------------------------------------------------------
void
CObjAVT::SetCur_UnionPOINT(BYTE btUnionIDX, short nValue) {
    if (btUnionIDX >= 1) {
        this->m_GrowAbility.m_nUnionPOINT[btUnionIDX - 1] = nValue;
    }
}

//-------------------------------------------------------------------------------------------------
void
CObjAVT::UpdateCartGuest() {
    if (Is_CartDriver()) {
        classUSER* pGuest = g_pObjMGR->Get_UserOBJ(this->m_iLinkedCartObjIDX);
        if (!pGuest || this->GetZONE() != pGuest->GetZONE()) {
            this->m_iLinkedCartObjIDX = 0;
            this->m_iLinkedCartUsrIDX = 0;
            return;
        }

        // ¼Õ´Ô À§Ä¡ º¸Á¤~
        pGuest->m_PosCUR = this->m_PosCUR;
        this->GetZONE()->UpdateSECTOR(pGuest);
    }
}

//-------------------------------------------------------------------------------------------------
#ifdef __KCHS_BATTLECART__
bool
CObjAVT::CanDoPatSkill(short nSkillIDX) {
    if (!this->Get_RideMODE())
        return false;

    if (SKILL_AVAILBLE_STATUS(nSkillIDX) < 2) // 2º¸´Ù Å©¸é Ä«Æ® ¾Æ´Ï¸é °ø¿ë ½ºÅ³
        return false;

    if (m_RideITEM[RIDE_PART_ARMS].m_nItemNo <= 0) // ARMS ÆÄÆ®¿¡ ¹«±â°¡ ÀåÂøµÇ¾î ÀÖ¾î¾ß ÇÑ´Ù.
        return false;

    return true;
}

//-------------------------------------------------------------------------------------------------
// btOnOff : 0 : Ä«Æ®¼ÒÈ¯ ºÒ°¡´É(ÄðÅ¸ÀÓ°ª ³¯¶ó°¨) , 1 : Ä«Æ® ¼ÒÈ¯ °¡´É
bool
CObjAVT::Send_gsv_PATSTATE_CHAGE(BYTE btOnOff, DWORD dwCoolTIME) {
    classPACKET* pCPacket = Packet_AllocNLock();
    if (!pCPacket)
        return false;

    pCPacket->m_HEADER.m_wType = GSV_PATSTATE_CHANGE;
    pCPacket->m_HEADER.m_nSize = sizeof(gsv_PATSTATE_CHANGE);
    pCPacket->m_gsv_PATSTATE_CHANGE.m_btOnOff = btOnOff;
    pCPacket->m_gsv_PATSTATE_CHANGE.m_dwCoolTIME = btOnOff ? 0 : 30 * 60 * 1000 /*PAT_COOLTIME*/;
    pCPacket->m_gsv_PATSTATE_CHANGE.m_nMaxPatHP = btOnOff ? GetCur_PatMaxHP() : 0;
    pCPacket->m_gsv_PATSTATE_CHANGE.m_wObjectIDX = this->Get_INDEX();

    this->GetZONE()->SendPacketToSectors(this, pCPacket);

    Packet_ReleaseNUnlock(pCPacket);

    if (!btOnOff) {
        this->m_btRideMODE = 0; // ¸ÕÀú ÇÏÂ÷¸ðµå·Î..
        UpdateAbility();
        this->Send_gsv_TOGGLE(TOGGLE_TYPE_DRIVE + this->Get_MoveMODE(),
            true); // Å¬¶ó¿¡¼­´Â ÀÌ°Ô ÇÊ¿äÇÏ´Ù.. ÀÌ°Å ¾øÀ¸¸é Å¬¶ó¿¡¼­ ¼Óµµ ¸ø ¸ÂÃá´Ù.

        this->SetCMD_STOP(); // 2005.08.03 : icarus :: Á¤Áö¸í·ÉÀ¸·Î
    }

    return true;
}
#endif

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
