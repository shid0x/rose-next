#include "stdAFX.h"

#ifndef __SERVER
    #include "CObjUSER.h"
#endif

#include "NET_Prototype.h"
#include "calculation.h"

using namespace Rose;

#ifdef __SERVER
    #include "ZoneLIST.H"
#else

short
Get_WorldDROP_M() {
    return 100;
}
short
Get_WorldDROP() {
    return 300;
}
short
Get_WorldEXP() {
    return 300;
}
short
Get_WorldREWARD() {
    return 100;
}
#endif

DropRule
drop_rule_from_int(int i) {
    if (i == 3) {
        return DropRule::AlwaysAppraisal;
    } else if (i == 4) {
        return DropRule::AlwaysSlotted;
    } else {
        return DropRule::Default;
    }
}

//-------------------------------------------------------------------------------------------------
int64_t
CCal::Get_NeedRawEXP(int iLevel) {
    if (iLevel > GameStaticConfig::MAX_LEVEL) {
        iLevel = GameStaticConfig::MAX_LEVEL;
    }

    if (iLevel <= 15) {
        return static_cast<int64_t>(((iLevel + 3) * (iLevel + 5) * (iLevel + 10) * 0.7f));
    }

    if (iLevel <= 60) {
        return static_cast<int64_t>(((iLevel - 5) * (iLevel + 2) * (iLevel + 2) * 2.2f));
    }

    if (iLevel <= 113) {
        return static_cast<int64_t>(((iLevel - 11) * (iLevel) * (iLevel + 4) * 2.5f));
    }

    if (iLevel <= 150) {
        return static_cast<int64_t>(((iLevel - 31) * (iLevel - 20) * (iLevel + 4) * 3.8f));
    }

    if (iLevel <= 189) {
        return static_cast<int64_t>(((iLevel - 67) * (iLevel - 20) * (iLevel - 10) * 6.f));
    }

    return static_cast<int64_t>(
        (iLevel - 90) * (iLevel - 120) * (iLevel - 60) * (iLevel - 170) * (iLevel - 188));
}

//-------------------------------------------------------------------------------------------------
int
CCal::Get_RewardVALUE(BYTE btEquation, int S_REWARD, CUserDATA* pUSER, short nDupCNT) {
    int iR = 0;

    switch (btEquation) {
        case 0: // °æÇèÄ¡ ±âÁØ°ª ¿ì¼±
            iR = ((S_REWARD + 30) * (pUSER->GetCur_CHARM() + 10) * (::Get_WorldREWARD())
                     * (pUSER->GetCur_FAME() + 20) / (pUSER->GetCur_LEVEL() + 70) / 30000)
                + S_REWARD;
            break;
        case 1: // °æÇèÄ¡ ·¹º§ºñÀ²
            iR = S_REWARD * (pUSER->GetCur_LEVEL() + 3)
                * (pUSER->GetCur_LEVEL() + pUSER->GetCur_CHARM() / 2 + 40) * (::Get_WorldREWARD())
                / 10000;
            break;
        case 2: // µ· È½¼ö
            iR = S_REWARD * nDupCNT;
            break;
        case 3: // µ· ±âÁØ°ª
        case 5: // ¾ÆÀÌÅÛ ±âÁØ°ª
            iR = ((S_REWARD + 20) * (pUSER->GetCur_CHARM() + 10) * (::Get_WorldREWARD())
                     * (pUSER->GetCur_FAME() + 20) / (pUSER->GetCur_LEVEL() + 70) / 30000)
                + S_REWARD;
            break;
        case 4: // µ· ·¹º§ ºñÀ²
            iR = (S_REWARD + 2) * (pUSER->GetCur_LEVEL() + pUSER->GetCur_CHARM() + 40)
                * (pUSER->GetCur_FAME() + 40) * (::Get_WorldREWARD()) / 140000;
            break;
        case 6: // ¾ÆÀÌÅÛ ·¹º§ºñÀ²
            iR = ((S_REWARD + 20) * (pUSER->GetCur_LEVEL() + pUSER->GetCur_CHARM())
                     * (pUSER->GetCur_FAME() + 20) * (::Get_WorldREWARD()) / 3000000)
                + S_REWARD;
            break;
    }

    return iR;
}

//-------------------------------------------------------------------------------------------------
bool
CCal::Get_DropITEM(int level_difference,
    CObjMOB* pMobCHAR,
    tagITEM& item,
    int iZoneNO,
    int iDropRate,
    int iCharm) {

    int iDrop_VAR = 0;

    if (level_difference < 0) {
        level_difference = 0;
    } else if (level_difference >= 10) {
        return false;
    }

    iDrop_VAR = static_cast<int>(
        (::Get_WorldDROP() + NPC_DROP_ITEM(pMobCHAR->Get_CharNO()) - (1 + RANDOM(100))
            - ((level_difference + 16) * 3.5f) - 10 + iDropRate)
        * 0.38f);

    if (iDrop_VAR <= 0) {
        return false;
    }

    if (1 + RANDOM(100) <= NPC_DROP_MONEY(pMobCHAR->Get_CharNO())) {
        // µ·»ý¼º
        // DROP_MONEY_Q = { (MOP_LV + 20) * (MOP_LV + DROP_ VAR +40) * WORLD_DROP_M / 3200 }
        int iMoney = (pMobCHAR->Get_LEVEL() + 20) * (pMobCHAR->Get_LEVEL() + iDrop_VAR + 40)
            * ::Get_WorldDROP_M() / 3200;
        if (iMoney <= 0)
            return false;

        item.m_cType = ITEM_TYPE_MONEY;
        item.m_uiMoney = iMoney;

        return true;
    }

    int iDropTBL;
    if (NPC_DROP_ITEM(pMobCHAR->Get_CharNO()) - (1 + RANDOM(100)) >= 0) {
        iDropTBL = NPC_DROP_TYPE(pMobCHAR->Get_CharNO());
    } else {
        iDropTBL = iZoneNO;
    }

    int iDropTblIDX = (iDrop_VAR > 30) ? RANDOM(30) : RANDOM(iDrop_VAR);
    int iDropITEM = DROPITEM_ITEMNO(iDropTBL, iDropTblIDX);
    if (iDropITEM <= 1000) {
        if (iDropITEM >= 1 && iDropITEM <= 4) {
            // ´Ù½Ã °è»ê
            iDropTblIDX = 26 + (iDropITEM * 5) + RANDOM(5);
            if (iDropTblIDX >= g_TblDropITEM.col_count) {
                // Å×ÀÌºí ÄÃ·³ °¹¼ö ÃÊ°ú...
                return false;
            }
            iDropITEM = DROPITEM_ITEMNO(iDropTBL, iDropTblIDX);
            if (iDropITEM <= 1000) {
                // ¾ø´Ù !
                return false;
            }
        } else {
            // ¾ø´Ù !
            return false;
        }
    }

    item.init();
    item.m_cType = static_cast<BYTE>(iDropITEM / 1000);
    item.m_nItemNo = iDropITEM % 1000;

    int iTEMP = 0;
    if (item.is_stackable() && !item.is_consumable()) {
        item.m_uiQuantity =
            1 + ((pMobCHAR->Get_LEVEL() + 10) / 9 + (1 + RANDOM(20)) + iDropRate) / (iDrop_VAR + 4);

        if (item.m_uiQuantity > GameStaticConfig::MAX_DROP_MULTIPLIER) {
            item.m_uiQuantity = GameStaticConfig::MAX_DROP_MULTIPLIER;
        }
    } else if (item.is_consumable()) {
        item.m_uiQuantity = GameStaticConfig::MAX_CONSUMABLE_DROP_MULTIPLIER;
    } else {
        short nRareType = ITEM_RARE_TYPE(item.GetTYPE(), item.GetItemNO());

        const DropRule drop_rule = drop_rule_from_int(nRareType);
        switch (drop_rule) {
            case DropRule::AlwaysSlotted:
                item.m_bHasSocket = 1;
                break;
            case DropRule::AlwaysAppraisal:
                item.m_nGEM_OP = 100 + RANDOM(41);
                break;
            case DropRule::Default: {
                int r = 1 + RANDOM(100);
                if (item.GetTYPE() <= ITEM_TYPE_KNAPSACK) {
                    int iITEM_OP = static_cast<int>(
                        ((pMobCHAR->Get_LEVEL() * 0.4f
                             + (NPC_DROP_ITEM(pMobCHAR->Get_CharNO()) - 35) * 4 + 80 - r + iCharm)
                            * 24 / (r + 13))
                        - 100);
                    if (iITEM_OP > 0) {
                        if (pMobCHAR->Get_LEVEL() < 230)
                            item.m_nGEM_OP = iITEM_OP % (pMobCHAR->Get_LEVEL() + 70);
                        else
                            item.m_nGEM_OP = iITEM_OP % 301;

                        item.m_bIsAppraisal = item.m_nGEM_OP ? 0 : 1;
                    }
                }
                break;
            }
        }

        int iTEMP = (int)(ITEM_DURABITY(item.GetTYPE(), item.GetItemNO())
            * (pMobCHAR->Get_LEVEL() * 0.3f + NPC_DROP_ITEM(pMobCHAR->Get_CharNO()) * 2 + 320)
            * 0.5f / (RANDOM(100) + 201));
        if (iTEMP > 100) {
            iTEMP = 100;
        }
        item.m_cDurability = iTEMP;

        iTEMP = (int)((NPC_DROP_ITEM(pMobCHAR->Get_CharNO()) + 200) * 80 / (31 + RANDOM(100)));
        if (iTEMP > MAX_ITEM_LIFE) {
            iTEMP = MAX_ITEM_LIFE;
        }

        item.m_nLife = iTEMP;
        item.m_cGrade = 0;
    }
    return true;
}

//-------------------------------------------------------------------------------------------------
// iGiveDamage = °ø°ÝÀÚ°¡ÁØ µ¥¹ÌÁö
__int64
CCal::Get_EXP(CObjCHAR* pAtkCHAR, CObjCHAR* pDefCHAR, int iGiveDamage) {
    __int64 iGAB, iEXP;

    iGAB = pAtkCHAR->Get_LEVEL() - pDefCHAR->Get_LEVEL();
    if (iGAB <= 3) {
        iEXP = (__int64)((float)((pDefCHAR->Get_LEVEL() + 3) * pDefCHAR->Get_GiveEXP()
                             * (iGiveDamage + pDefCHAR->Get_MaxHP() / 15.f + 30))
            * ::Get_WorldEXP() / (pDefCHAR->Get_MaxHP()) / 370.f);
    } else {
        iEXP = (__int64)((float)((pDefCHAR->Get_LEVEL() + 3) * pDefCHAR->Get_GiveEXP()
                             * (iGiveDamage + pDefCHAR->Get_MaxHP() / 15.f + 30))
            * ::Get_WorldEXP() / pDefCHAR->Get_MaxHP() / (iGAB + 3) / 60.f);
    }

    if (iEXP < 1)
        iEXP = 1;

    return iEXP;
}

#ifdef __SERVER

//-------------------------------------------------------------------------------------------------
// °ø°Ý ¼º°øµµ
int
CCal::Get_SuccessRATE(CObjCHAR* pATK, CObjCHAR* pDEF) // , int &iCriticalSUC )
{
    int iRAND1, iRAND2;
    int iSuccess;

    // PVP uses the same hit-vs-dodge formula as PVM. The original PVP-only formula
    // divided by the attacker's own dodge with truncating integer division, which
    // made riders (low dodge while mounted) miss ~93% of normal attacks.
    iRAND1 = 1 + RANDOM(50); // 1+RANDOM(100) * 0.6f;
    iRAND2 = 1 + RANDOM(60); // 1+RANDOM(100) * 0.7f;
    iSuccess = (int)((pATK->Get_LEVEL() + 10) - pDEF->Get_LEVEL() * 1.1f + (iRAND1 /* *0.6f */));
    if (iSuccess <= 0)
        return 0;

    return (int)(iSuccess
        * (pATK->total_hit_rate() * 1.1f - pDEF->Get_AVOID() * 0.93f + iRAND2 /* *0.7f */ + 5
            + pATK->Get_LEVEL() * 0.2f)
        / 80.f);
}

//-------------------------------------------------------------------------------------------------
// Å©¸®Æ¼ÄÃ Success °ª °ø½Ä 2005-7-13 Ãß°¡
int
CCal::Get_CriSuccessRATE(CObjCHAR* pATK) {
    return static_cast<int>(
        ((1 + RANDOM(100)) * 3 + pATK->Get_LEVEL() + 30) * 16 / (pATK->Get_CRITICAL() + 70));
}

//-------------------------------------------------------------------------------------------------
// ÀÏ¹Ý ¹°¸® µ¥¹ÌÁö °è»ê½Ä...
int
CCal::Get_BasicDAMAGE(CObjCHAR* pATK, CObjCHAR* pDEF, WORD wHitCNT, int iSuc) {
    int iDamage, iCriSuc;

    iCriSuc = Get_CriSuccessRATE(pATK);

    // ¸Â´Â µ¿ÀÛ È®·ü.
    // Signed math: stats getters are uint32_t; unsigned wrap here turns negative
    // results into huge positives (min-damage cases became 9999 OHKOs).
    int iHitActRATE =
        (28 - iCriSuc) * ((int)pATK->total_attack_power() + 20) / (pDEF->Get_DEF() + 5);

    if (iCriSuc < 20) {
        // Critical damage !!!
        if (pATK->IsUSER() && pDEF->IsUSER()) {
            // PVP :: Å©¸®Æ¼ÄÃ ¹°¸® µ¥¹ÌÁö
            iDamage = (int)(pATK->total_attack_power() * (iSuc * 0.05f + 35)
                    * ((int)pATK->total_attack_power() - pDEF->Get_DEF() + 430)
                    / ((pDEF->Get_DEF() + pDEF->Get_AVOID() * 0.4f + 10) * 300)
                + 25);
        } else {
            // ¸ó½ºÅÍ :: Å©¸®Æ¼ÄÃ ¹°¸® µ¥¹ÌÁö
            iDamage = (int)(pATK->total_attack_power() * (iSuc * 0.05f + 29)
                * ((int)pATK->total_attack_power() - pDEF->Get_DEF() + 230)
                / ((pDEF->Get_DEF() + pDEF->Get_AVOID() * 0.3f + 5) * 100));
        }
        // Ãß°¡ µ¥¹ÌÁö ½ºÅ³ ...
        if (FLAG_ING_DUMMY_DAMAGE & pATK->GetIngDurationStateFLAG()) {
            iDamage += (int)(iDamage * SKILL_POWER(pATK->GetIngDurationStateSKILL(ING_DUMMY_DAMAGE))
                / 100.f);
            // iDamage |= DMG_BIT_DUMMY;
        }

        iDamage *= wHitCNT;

        if (iDamage < 10)
            iDamage = 10;
        else {
            if (pATK->IsUSER() && pDEF->IsUSER()) {
                int iMaxDmg = (int)(pDEF->Get_MaxHP() * 0.35f);
                if (iDamage > iMaxDmg)
                    iDamage = iMaxDmg;
            } else if (iDamage > GameStaticConfig::MAX_DAMAGE)
                iDamage = GameStaticConfig::MAX_DAMAGE;
        }

        iDamage |= DMG_BIT_CRITICAL;
    } else {
        // Normal damage
        if (pATK->IsUSER() && pDEF->IsUSER()) {
            iDamage = (int)(pATK->total_attack_power() * (iSuc * 0.05f + 25)
                    * ((int)pATK->total_attack_power() - pDEF->Get_DEF() + 400)
                    / ((pDEF->Get_DEF() + pDEF->Get_AVOID() * 0.4f + 5) * 420)
                + 20);
        } else {
            iDamage = (int)(pATK->total_attack_power() * (iSuc * 0.03f + 26)
                * ((int)pATK->total_attack_power() - pDEF->Get_DEF() + 250)
                / ((pDEF->Get_DEF() + pDEF->Get_AVOID() * 0.4f + 5) * 145));
        }
        // Ãß°¡ µ¥¹ÌÁö ½ºÅ³ ...
        if (FLAG_ING_DUMMY_DAMAGE & pATK->GetIngDurationStateFLAG()) {
            iDamage += (int)(iDamage * SKILL_POWER(pATK->GetIngDurationStateSKILL(ING_DUMMY_DAMAGE))
                / 100.f);
            // iDamage |= DMG_BIT_DUMMY;
        }

        iDamage *= wHitCNT;

        if (iDamage < 5)
            iDamage = 5;
        else {
            if (pATK->IsUSER() && pDEF->IsUSER()) {
                int iMaxDmg = (int)(pDEF->Get_MaxHP() * 0.25f);
                if (iDamage > iMaxDmg)
                    iDamage = iMaxDmg;
            } else if (iDamage > GameStaticConfig::MAX_DAMAGE)
                iDamage = GameStaticConfig::MAX_DAMAGE;
        }
    }

    return (iHitActRATE >= 10) ? (DMG_BIT_HITTED | iDamage) : iDamage;
}

//-------------------------------------------------------------------------------------------------
// ¸¶¹ý µ¥¹ÌÁö
int
CCal::Get_MagicDAMAGE(CObjCHAR* pATK, CObjCHAR* pDEF, WORD wHitCNT, int iSuc) {
    int iDamage, iCriSuc;

    iCriSuc = Get_CriSuccessRATE(pATK);

    // ¸Â´Â µ¿ÀÛ È®·ü.
    // Signed math: see iHitActRATE comment in Get_BasicDAMAGE.
    int iHitActRATE =
        (28 - iCriSuc) * ((int)pATK->total_attack_power() + 20) / (pDEF->Get_RES() + 5);

    if (iCriSuc < 20) {
        // Critical damage !!!
        if (pATK->IsUSER() && pDEF->IsUSER()) { // ¸¶¹ý Å©¸®Æ¼ÄÃ PVP
            iDamage = (int)(pATK->total_attack_power() * (iSuc * 0.08f + 33)
                    * ((int)pATK->total_attack_power() - pDEF->Get_DEF() + 340)
                    / ((pDEF->Get_RES() + pDEF->Get_AVOID() * 0.3f + 20) * 360)
                + 25);
        } else {
            iDamage = (int)(pATK->total_attack_power() * (iSuc * 0.05f + 33)
                * (pATK->total_attack_power() - pDEF->Get_DEF() * 0.8f + 310)
                / ((pDEF->Get_RES() + pDEF->Get_AVOID() * 0.3f + 5) * 200));
        }

        // Ãß°¡ µ¥¹ÌÁö ...
        if (FLAG_ING_DUMMY_DAMAGE & pATK->GetIngDurationStateFLAG()) {
            iDamage += (int)(iDamage * SKILL_POWER(pATK->GetIngDurationStateSKILL(ING_DUMMY_DAMAGE))
                / 100.f);
            // iDamage |= DMG_BIT_DUMMY;
        }

        iDamage *= wHitCNT;

        if (iDamage < 10)
            iDamage = 10;
        else {
            if (pATK->IsUSER() && pDEF->IsUSER()) {
                int iMaxDmg = (int)(pDEF->Get_MaxHP() * 0.35f);
                if (iDamage > iMaxDmg)
                    iDamage = iMaxDmg;
            } else if (iDamage > GameStaticConfig::MAX_DAMAGE)
                iDamage = GameStaticConfig::MAX_DAMAGE;
        }

        iDamage |= DMG_BIT_CRITICAL;
    } else {
        // Normal damage
        if (pATK->IsUSER() && pDEF->IsUSER()) { // ¸¶¹ý ÀÏ¹Ý PVP
            iDamage = (int)((float)pATK->total_attack_power() * (iSuc * 0.06f + 29)
                    * (pATK->total_attack_power() - pDEF->Get_DEF() * 0.8f + 350)
                    / ((pDEF->Get_RES() + pDEF->Get_AVOID() * 0.3f + 5) * 640)
                + 20);
        } else {
            iDamage = (int)(pATK->total_attack_power() * (iSuc * 0.03f + 30)
                * (pATK->total_attack_power() - pDEF->Get_DEF() * 0.8f + 280)
                / ((pDEF->Get_RES() + pDEF->Get_AVOID() * 0.3f + 5) * 280));
        }

        // Ãß°¡ µ¥¹ÌÁö ...
        if (FLAG_ING_DUMMY_DAMAGE & pATK->GetIngDurationStateFLAG()) {
            iDamage += (int)(iDamage * SKILL_POWER(pATK->GetIngDurationStateSKILL(ING_DUMMY_DAMAGE))
                / 100.f);
            // iDamage |= DMG_BIT_DUMMY;
        }

        iDamage *= wHitCNT;

        if (iDamage < 5)
            iDamage = 5;
        else {
            if (pATK->IsUSER() && pDEF->IsUSER()) {
                int iMaxDmg = (int)(pDEF->Get_MaxHP() * 0.25f);
                if (iDamage > iMaxDmg)
                    iDamage = iMaxDmg;
            } else if (iDamage > GameStaticConfig::MAX_DAMAGE)
                iDamage = GameStaticConfig::MAX_DAMAGE;
        }
    }

    return (iHitActRATE >= 10) ? (DMG_BIT_HITTED | iDamage) : iDamage;
}

//-------------------------------------------------------------------------------------------------
int
CCal::Get_DAMAGE(CObjCHAR* pATK, CObjCHAR* pDEF, WORD wHitCNT) {
    int iSuc = Get_SuccessRATE(pATK, pDEF);

    if (iSuc < 20) {
        if ((int)(1 + RANDOM(100) + (pATK->Get_LEVEL() - pDEF->Get_LEVEL()) * 0.6f) < 94)
            return 0;
    }

    if (pATK->IsMagicDAMAGE())
        return Get_MagicDAMAGE(pATK, pDEF, wHitCNT, iSuc);

    return Get_BasicDAMAGE(pATK, pDEF, wHitCNT, iSuc);
}

//-------------------------------------------------------------------------------------------------
int
CCal::Get_WeaponSkillDAMAGE(CObjCHAR* pATK, CObjCHAR* pDEF, short nSkillIDX, int iSuccess) {
    int iDamage;

    // [¹«±â ½ºÅ³Çü] µ¥¹ÌÁö °è»ê½Ä
    if (pATK->IsUSER() && pDEF->IsUSER()) {
        // DMG= (SKILL_POW/100) *(D_LV/A_LV) * A_ATT * ( SUC*0.03+26)*(1.8* A_ATT - D_DEF +150) /
        // (1.1* D_DEF + D_AVO *0.4+50)/145
        iDamage = (int)((SKILL_POWER(nSkillIDX) / 100.f)
            * ((float)pDEF->Get_LEVEL() / pATK->Get_LEVEL()) * (pATK->total_attack_power())
            * (iSuccess * 0.03f + 26) * (1.8f * pATK->total_attack_power() - pDEF->Get_DEF() + 150)
            / (1.1f * pDEF->Get_DEF() + pDEF->Get_AVOID() * 0.4f + 50) / 126.f);
    } else {
        // DMG = (SKILL_POW/100 ) * A_ATT * ( SUC*0.03+26)*(1.8* A_ATT - D_DEF +150) / (1.1* D_DEF +
        // D_AVO *0.4+50)/145
        iDamage = (int)((SKILL_POWER(nSkillIDX) / 100.f) * (pATK->total_attack_power())
            * (iSuccess * 0.03f + 26) * (1.8f * pATK->total_attack_power() - pDEF->Get_DEF() + 150)
            / (1.1f * pDEF->Get_DEF() + pDEF->Get_AVOID() * 0.4f + 50) / 126.f);
    }

    return iDamage;
}
int
CCal::Get_MagicSkillDAMAGE(CObjCHAR* pATK, CObjCHAR* pDEF, short nSkillIDX, int iSuccess) {
    int iDamage;

    // [¸¶¹ý ½ºÅ³Çü] µ¥¹ÌÁö °è»ê½Ä
    if (pATK->IsUSER() && pDEF->IsUSER()) {
        // DMG = (SKILL_POW/100) *(D_LV/A_LV) * A_ATT *( SUC *0.03+25)*(1.8 *A_ATT - D_DEF
        // +150)/(1.1* D_RES + D_AVO *0.4+50)/140
        iDamage = (int)((SKILL_POWER(nSkillIDX) / 100.f)
            * ((float)pDEF->Get_LEVEL() / pATK->Get_LEVEL()) * (pATK->total_attack_power())
            * (iSuccess * 0.03f + 25) * (1.8f * pATK->total_attack_power() - pDEF->Get_DEF() + 150)
            / (1.1f * pDEF->Get_RES() + pDEF->Get_AVOID() * 0.4f + 50) / 120.f);
    } else {
        // DMG = (SKILL_POW/100)  * A_ATT *( SUC *0.03+25)*(1.8 *A_ATT - D_DEF +150)/(1.1* D_RES +
        // D_AVO *0.4+50)/120
        iDamage = (int)((SKILL_POWER(nSkillIDX) / 100.f) * (pATK->total_attack_power())
            * (iSuccess * 0.03f + 25) * (1.8f * pATK->total_attack_power() - pDEF->Get_DEF() + 150)
            / (1.1f * pDEF->Get_RES() + pDEF->Get_AVOID() * 0.4f + 50) / 120.f);
    }

    return iDamage;
}

int
CCal::Get_SkillDAMAGE(CObjCHAR* pATK, CObjCHAR* pDEF, short nSkillIDX, WORD wHitCNT) {
    int iDamage;

    // int iSuccess  = Get_SuccessRATE (pATK, pDEF); // 2005-7-26 kchs ¼öÁ¤. Àß¸øµÇ¼­ ´Ù½Ã ¿¾³¯·Î
    // º¹¿ø
    switch (SKILL_DAMAGE_TYPE(nSkillIDX)) {
        case 1: // ¹«±â ½ºÅ³
        {
            int iSuccess;
            int iRAND1 = 1 + RANDOM(60);
            int iRAND2 = 1 + RANDOM(70);
            iSuccess = (int)(((pATK->Get_LEVEL() + 20) - pDEF->Get_LEVEL() + (iRAND1 /* *0.6f */))
                * (pATK->total_hit_rate() - pDEF->Get_AVOID() * 0.6f + iRAND2 /* *0.7f */ + 10)
                / 110.f);

            if (iSuccess < 20) {
                if (iSuccess < 10)
                    return 0;

                iDamage = (int)(((SKILL_POWER(nSkillIDX) * 0.4f) * (pATK->total_attack_power() + 50)
                                    * ((1 + RANDOM(30)) + pATK->Get_SENSE() * 1.2f + 340))
                        / (pDEF->Get_DEF() + pDEF->Get_RES() + 20)
                        / (250 + pDEF->Get_LEVEL() - pATK->Get_LEVEL())
                    + 20);
            } else {
                if (pATK->IsUSER() && pDEF->IsUSER()) {
                    iDamage = (int)(((SKILL_POWER(nSkillIDX) + pATK->total_attack_power() * 0.2f)
                                        * (pATK->total_attack_power() + 60)
                                        * ((1 + RANDOM(30)) + pATK->Get_SENSE() * 0.7 + 370))
                            * 0.01 * (320 - pDEF->Get_LEVEL() + pATK->Get_LEVEL())
                            / (pDEF->Get_DEF() + pDEF->Get_RES() * 0.8f + pDEF->Get_AVOID() * 0.4f
                                + 40)
                            / 1600
                        + 60);
                } else {
                    // [���� ��ų��] ������ ����
                    iDamage = (int)(((SKILL_POWER(nSkillIDX) + pATK->total_attack_power() * 0.2f)
                                        * (pATK->total_attack_power() + 60)
                                        * ((1 + RANDOM(30)) + pATK->Get_SENSE() * 0.7f + 370))
                            * 0.01 * (120 - pDEF->Get_LEVEL() + pATK->Get_LEVEL())
                            / (pDEF->Get_DEF() + pDEF->Get_RES() * 0.8f + pDEF->Get_AVOID() * 0.4f
                                + 20)
                            / 270
                        + 20);
                }
            }
            break;
        }
        case 2: // ¸¶¹ý ½ºÅ³
        {
            int iSuccess;
            int iRAND1 = 1 + RANDOM(50);
            int iRAND2 = 1 + RANDOM(70);
            iSuccess = (int)(((pATK->Get_LEVEL() + 30) - pDEF->Get_LEVEL() + (iRAND1))
                * (pATK->total_hit_rate() - pDEF->Get_AVOID() * 0.56f + iRAND2 + 10) / 110.f);

            if (iSuccess < 20) {
                if (iSuccess < 8)
                    return 0;
                iDamage = (int)((SKILL_POWER(nSkillIDX)
                                    * (pATK->total_attack_power() * 0.8f + pATK->Get_INT() + 80)
                                    * ((1 + RANDOM(30)) + pATK->Get_SENSE() * 1.3f + 280) * 0.2f)
                        / (pDEF->Get_DEF() * 0.3f + pDEF->Get_RES() + 30)
                        / (250 + pDEF->Get_LEVEL() - pATK->Get_LEVEL())
                    + 20);
            } else {
                if (pATK->IsUSER() && pDEF->IsUSER()) {
                    iDamage =
                        (int)(((SKILL_POWER(nSkillIDX) + 50)
                                  * (pATK->total_attack_power() * 0.8f + pATK->Get_INT() * 1.2f
                                      + 100)
                                  * ((1 + RANDOM(30)) + pATK->Get_SENSE() * 0.7f + 350) * 0.01f)
                                * (380 - pDEF->Get_LEVEL() + pATK->Get_LEVEL())
                                / (pDEF->Get_DEF() * 0.4f + pDEF->Get_RES()
                                    + pDEF->Get_AVOID() * 0.3f + 60)
                                / 2500.f
                            + 60);
                } else {
                    iDamage =
                        (int)((SKILL_POWER(nSkillIDX)
                                  * (pATK->total_attack_power() * 0.8f + pATK->Get_INT() * 1.2f
                                      + 100)
                                  * ((1 + RANDOM(30)) + pATK->Get_SENSE() * 0.7f + 350) * 0.01f)
                                * (150 - pDEF->Get_LEVEL() + pATK->Get_LEVEL())
                                / (pDEF->Get_DEF() * 0.3f + pDEF->Get_RES()
                                    + pDEF->Get_AVOID() * 0.3f + 60)
                                / 350.f
                            + 20);
                }
            }

            break;
        }
        case 3: // ¸Ç¼Õ ½ºÅ³
        {
            int iSuccess;
            int iRAND1 = 1 + RANDOM(80);
            int iRAND2 = 1 + RANDOM(50);
            iSuccess = (int)(((pATK->Get_LEVEL() + 10) - pDEF->Get_LEVEL() + (iRAND1))
                * (pATK->total_hit_rate() - pDEF->Get_AVOID() * 0.5f + iRAND2 + 50) / 90.f);
            if (iSuccess < 20) {
                if (iSuccess < 6)
                    return 0;
                iDamage =
                    (int)((SKILL_POWER(nSkillIDX) * (SKILL_POWER(nSkillIDX) + pATK->Get_INT() + 80)
                              * ((1 + RANDOM(30)) + pATK->Get_SENSE() * 2 + 290) * 0.2f)
                            / (pDEF->Get_DEF() * 0.2f + pDEF->Get_RES() + 30)
                            / (250 + pDEF->Get_LEVEL() - pATK->Get_LEVEL())
                        + 20);
            } else {
                if (pATK->IsUSER() && pDEF->IsUSER()) {
                    iDamage = (int)(((35 + SKILL_POWER(nSkillIDX))
                                        * (SKILL_POWER(nSkillIDX) + pATK->Get_INT() + 140)
                                        * ((1 + RANDOM(30)) + pATK->Get_SENSE() + 380) * 0.01f)
                            * (400 - pDEF->Get_LEVEL() + pATK->Get_LEVEL())
                            / (pDEF->Get_DEF() * 0.5f + pDEF->Get_RES() * 1.2f
                                + pDEF->Get_AVOID() * 0.4f + 20)
                            / 3400
                        + 40);
                } else {
                    iDamage = (int)(((35 + SKILL_POWER(nSkillIDX))
                                        * (SKILL_POWER(nSkillIDX) + pATK->Get_INT() + 140)
                                        * ((1 + RANDOM(30)) + pATK->Get_SENSE() + 380) * 0.01f)
                            * (150 - pDEF->Get_LEVEL() + pATK->Get_LEVEL())
                            / (pDEF->Get_DEF() * 0.35f + pDEF->Get_RES() * 1.2f
                                + pDEF->Get_AVOID() * 0.4f + 10)
                            / 730
                        + 20);
                }
            }
            break;
        }
        default: {
            // ÀÏ¹Ý °ø°Ý µ¥¹ÌÁö·Î °è»ê...
            int iSuccess;
            int iRAND1 = 1 + RANDOM(80);
            int iRAND2 = 1 + RANDOM(50);
            iSuccess = (int)(((pATK->Get_LEVEL() + 8) - pDEF->Get_LEVEL() + (iRAND1 /* *0.8 */))
                * (pATK->total_hit_rate() - pDEF->Get_AVOID() * 0.6f + iRAND2 /* *0.5 */ + 50)
                / 90);
            if (iSuccess < 20) {
                if (iSuccess < 10)
                    return 0;
                iDamage = (int)(((SKILL_POWER(nSkillIDX) + 40) * (pATK->total_attack_power() + 40)
                                    * ((1 + RANDOM(30)) + pATK->Get_CRITICAL() * 0.2f + 40))
                        * 0.4f
                        / (pDEF->Get_DEF() + pDEF->Get_RES() * 0.3f + pDEF->Get_AVOID() * 0.4f + 10)
                        / 80
                    + 5);
            } else {
                if (pATK->IsUSER() && pDEF->IsUSER()) {
                    iDamage = (int)(((SKILL_POWER(nSkillIDX) + pATK->Get_CRITICAL() * 0.15f + 40)
                                        * (pATK->total_attack_power())
                                        * ((1 + RANDOM(30)) + pATK->Get_CRITICAL() * 0.32f + 35))
                            * 0.01f * (350 - pDEF->Get_LEVEL() + pATK->Get_LEVEL())
                            / (pDEF->Get_DEF() + pDEF->Get_RES() * 0.3f + pDEF->Get_AVOID() * 0.4f
                                + 35)
                            / 400
                        + 20);
                } else {
                    iDamage = (int)(((SKILL_POWER(nSkillIDX) + pATK->Get_CRITICAL() * 0.15f + 40)
                                        * (pATK->total_attack_power())
                                        * ((1 + RANDOM(30)) + pATK->Get_CRITICAL() * 0.32f + 35))
                            * 0.01f * (120 - pDEF->Get_LEVEL() + pATK->Get_LEVEL())
                            / (pDEF->Get_DEF() + pDEF->Get_RES() * 0.3f + pDEF->Get_AVOID() * 0.4f
                                + 10)
                            / 100.f
                        + 20);
                }
            }
        }
    }

    // Ãß°¡ µ¥¹ÌÁö ...
    if (FLAG_ING_DUMMY_DAMAGE & pATK->GetIngDurationStateFLAG()) {
        iDamage +=
            (int)(iDamage * SKILL_POWER(pATK->GetIngDurationStateSKILL(ING_DUMMY_DAMAGE)) / 100.f);
        // iDamage |= DMG_BIT_DUMMY;
    }

    if (iDamage < 5)
        iDamage = 5;

    iDamage *= wHitCNT;
    if (pATK->IsUSER() && pDEF->IsUSER()) {
        int iMaxDmg = (int)(pDEF->Get_MaxHP() * 0.45f);
        if (iDamage > iMaxDmg)
            iDamage = iMaxDmg;
    }

    if (iDamage > GameStaticConfig::MAX_DAMAGE)
        iDamage = GameStaticConfig::MAX_DAMAGE;

    // È÷Æ® µ¿ÀÛ È®·ü...
    int iHitActRATE;

    iHitActRATE = iDamage * (1 + (RANDOM(100)) + 100) / (pDEF->Get_AVOID() + 40) / 14;

    return (iHitActRATE >= 10) ? (DMG_BIT_HITTED | iDamage) : iDamage;
}

#endif

//-------------------------------------------------------------------------------------------------
int
CCal::Get_SkillAdjustVALUE(CObjUSER* pCHAR, short nSkillIDX, short nCol, int iSpellerINT) {
    int iAbilityValue;

#ifndef __SERVER
    iAbilityValue = pCHAR->GetCur_AbilityValue(SKILL_INCREASE_ABILITY(nSkillIDX, nCol));
#else
    iAbilityValue = pCHAR->Get_AbilityValue(SKILL_INCREASE_ABILITY(nSkillIDX, nCol));
#endif

    return (short)(iAbilityValue * SKILL_CHANGE_ABILITY_RATE(nSkillIDX, nCol) / 100.f
        + SKILL_INCREASE_ABILITY_VALUE(nSkillIDX, nCol) * (iSpellerINT + 300) / 315.f);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
