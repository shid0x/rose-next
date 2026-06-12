#include "stdAFX.h"

#include "rose/io/stb.h"
#include "CInventory.h"

#ifndef __SERVER
    #include "..\\Object.h"
#endif

t_InvTYPE CInventory::m_InvTYPE[ITEM_TYPE_MONEY] = {
    MAX_INV_TYPE, //	Not used...

    INV_WEAPON, //	ITEM_TYPE_FACE = 1,				// 1	LIST_FACEITEM.stb	¾ó±¼ Àå½Ä
    INV_WEAPON, //	ITEM_TYPE_HELMET,				// 2	LIST_CAP.stb
    INV_WEAPON, //	ITEM_TYPE_ARMOR,				// 3	LIST_BODY.stb
    INV_WEAPON, //	ITEM_TYPE_GAUNTLET,				// 4	LIST_ARMS.stb
    INV_WEAPON, //	ITEM_TYPE_BOOTS,				// 5	LIST_FOOT.stb
    INV_WEAPON, //	ITEM_TYPE_KNAPSACK,				// 6	LIST_BACK.stb
    INV_WEAPON, //	ITEM_TYPE_JEWEL,				// 7	LIST_JEWEL.stb		Àå½Å±¸ : ¸ñ°ÉÀÌ ¹ÝÁö

    INV_WEAPON, //	ITEM_TYPE_WEAPON,				// 8	LIST_WEAPON.stb		¹«±â
    INV_WEAPON, //	ITEM_TYPE_SUBWPN,				// 9	LIST_SUBWPN.stb

    INV_USE, //	ITEM_TYPE_USE,					// 10	LIST_USEITEM.stb	¼Ò¸ð

    INV_ETC, //	ITEM_TYPE_ETC = ITEM_TYPE_GEM,	// 11						±âÅ¸ : º¸¼®
    INV_ETC, //	ITEM_TYPE_NATURAL,				// 12	LIST_NATURAL.stb
    INV_ETC, //	ITEM_TYPE_QUEST,				// 13	LIST_QUESTITEM.stb
    INV_RIDING, //	ITEM_TYPE_SPECIAL,				// 14	xxx
};

//-------------------------------------------------------------------------------------------------
void
CInventory::Clear() {
    m_i64Money = 0;
    ::ZeroMemory(m_ItemLIST, sizeof(m_ItemLIST));
#ifndef __SERVER
    ::ZeroMemory(m_btIndexLIST, sizeof(BYTE) * INVENTORY_TOTAL_SIZE);
#endif
}

#ifndef __SERVER
//-------------------------------------------------------------------------------------------------
//
/// ItemLIST ¸¦ ±â¹ÝÀ¸·Î IndexLIST ¸¦ ¸¸µç´Ù.
void
CInventory::MakeItemIndexList() {
    int i, j, iIndexListCount = 0;

    memset(m_btIndexLIST, 0, sizeof(m_btIndexLIST));

    for (i = 0; i < INVENTORY_TOTAL_SIZE; i++)
        m_btIndexLIST[i] = i;

    return;

    /// °¢ ¾ÆÀÌÅÛ Å¸ÀÔ¿¡ ´ëÇØ
    for (i = 0; i < MAX_INV_TYPE; i++) {
        iIndexListCount = 0;

        for (j = 0; j < INVENTORY_PAGE_SIZE; j++) {

            /// ¹º°¡ ÀÌÀÌÅÛÀÌ ÀÖ´Ù¸é
            if (m_ItemPAGE[i][j].m_cType != 0) {
                m_btIndexPAGE[i][iIndexListCount] = (MAX_EQUIP_IDX + i * INVENTORY_PAGE_SIZE) + j;
                iIndexListCount++;
            }
        }
    }
}

/// ½ÇÁ¦ ¾ÆÀÌÅÛ ÀÎµ¦½º·Î ÂüÁ¶Å×ÀÌºí ÀÎµ¦½º¸¦ ±¸ÇÑ´Ù.
short
CInventory::GetLookupIndexFromRealIndex(short nRealIndex) {
    int i, j;

    return nRealIndex;

    /// °¢ ¾ÆÀÌÅÛ Å¸ÀÔ¿¡ ´ëÇØ
    for (i = 0; i < MAX_INV_TYPE; i++) {
        for (j = 0; j < INVENTORY_PAGE_SIZE; j++) {
            if (m_btIndexPAGE[i][j] == nRealIndex) {
                return (MAX_EQUIP_IDX + i * INVENTORY_PAGE_SIZE) + j;
            }
        }
    }
    return -1;
}

//-------------------------------------------------------------------------------------------------
/// Lookup Table ¸¦ ÂüÁ¶ÇØ¼­ ½ÇÁ¦ ¾ÆÀÌÅÛÀ» ¾ò´Â´Ù.
bool
CInventory::IDX_GetITEM(short nLookUpIndexNO, tagITEM& OutITEM) {
    _ASSERT(nLookUpIndexNO >= 0 && nLookUpIndexNO < INVENTORY_TOTAL_SIZE);

    if (m_btIndexLIST[nLookUpIndexNO] > 0) {
        OutITEM = m_ItemLIST[m_btIndexLIST[nLookUpIndexNO]];

        if (OutITEM.GetTYPE())
            return true;
    }
    return false;
}

/// Lookup Table ¸¦ ÂüÁ¶ÇØ¼­ ½ÇÁ¦ ÀÎµ¦½º¸¦ ¾ò±âÀ§ÇÑ IDX_GetITEM ÇÔ¼ö¸¦ È£ÃâÇÑ´Ù.
bool
CInventory::IDX_GetITEM(short nInvTYPE, short nPageIndexNO, tagITEM& OutITEM) {
    _ASSERT(nInvTYPE >= 0 && nInvTYPE < MAX_EQUIP_IDX);
    _ASSERT(nPageIndexNO >= 0 && nPageIndexNO < INVENTORY_PAGE_SIZE);

    short nLookUpIndexNO = (MAX_EQUIP_IDX + nInvTYPE * INVENTORY_PAGE_SIZE) + nPageIndexNO;

    return IDX_GetITEM(nLookUpIndexNO, OutITEM);
}

//
//-------------------------------------------------------------------------------------------------
#endif // __SERVER

short
CInventory::GetBodyPartToItemType(short nEquipSlot) {

    short nBodyPartIDX = MAX_BODY_PART;
    switch (nEquipSlot) {
        case BODY_PART_GOGGLE:
            nBodyPartIDX = ITEM_TYPE_FACE_ITEM;
            break;
        case BODY_PART_HELMET:
            nBodyPartIDX = ITEM_TYPE_HELMET;
            break;
        case BODY_PART_ARMOR:
            nBodyPartIDX = ITEM_TYPE_ARMOR;
            break;
        case BODY_PART_GAUNTLET:
            nBodyPartIDX = ITEM_TYPE_GAUNTLET;
            break;
        case BODY_PART_BOOTS:
            nBodyPartIDX = ITEM_TYPE_BOOTS;
            break;
        case BODY_PART_WEAPON_R:
            nBodyPartIDX = ITEM_TYPE_WEAPON;
            break;
        case BODY_PART_WEAPON_L:
            nBodyPartIDX = ITEM_TYPE_SUBWPN;
            break;

        case BODY_PART_KNAPSACK:
            nBodyPartIDX = ITEM_TYPE_KNAPSACK;
            break;
    }

    return nBodyPartIDX;
}

short
CInventory::GetBodyPartByEquipSlot(short nEquipSlot) {
    short nBodyPartIDX = MAX_BODY_PART;
    switch (nEquipSlot) {
        case EQUIP_IDX_FACE_ITEM:
            nBodyPartIDX = BODY_PART_FACE_ITEM;
            break;
        case EQUIP_IDX_HELMET:
            nBodyPartIDX = BODY_PART_HELMET;
            break;
        case EQUIP_IDX_ARMOR:
            nBodyPartIDX = BODY_PART_ARMOR;
            break;
        case EQUIP_IDX_KNAPSACK:
            nBodyPartIDX = BODY_PART_KNAPSACK;
            break;
        case EQUIP_IDX_GAUNTLET:
            nBodyPartIDX = BODY_PART_GAUNTLET;
            break;
        case EQUIP_IDX_BOOTS:
            nBodyPartIDX = BODY_PART_BOOTS;
            break;
        case EQUIP_IDX_WEAPON_R:
            nBodyPartIDX = BODY_PART_WEAPON_R;
            break;
        case EQUIP_IDX_WEAPON_L:
            nBodyPartIDX = BODY_PART_WEAPON_L;
            break;
    }

    /// Àåºñ ¾ÆÀÌÅÛÀÌ ¾Æ´Ï´Ù.
    return nBodyPartIDX;
}

short
CInventory::GetBodyPartByEquipCostumeSlot(short nEquipSlot) {
    short nBodyPartIDX = MAX_BODY_PART;
    switch (nEquipSlot) {
        case COSTUME_IDX_FACE_ITEM:
            nBodyPartIDX = BODY_PART_FACE_ITEM;
            break;
        case COSTUME_IDX_HELMET:
            nBodyPartIDX = BODY_PART_HELMET;
            break;
        case COSTUME_IDX_ARMOR:
            nBodyPartIDX = BODY_PART_ARMOR;
            break;
        case COSTUME_IDX_KNAPSACK:
            nBodyPartIDX = BODY_PART_KNAPSACK;
            break;
        case COSTUME_IDX_GAUNTLET:
            nBodyPartIDX = BODY_PART_GAUNTLET;
            break;
        case COSTUME_IDX_BOOTS:
            nBodyPartIDX = BODY_PART_BOOTS;
            break;
    }

    return nBodyPartIDX;
}

tagITEM
CInventory::LST_GetITEM(short nListNO) {
    _ASSERT(nListNO >= 0 && nListNO < INVENTORY_TOTAL_SIZE);

    return m_ItemLIST[nListNO];
}

tagITEM
CInventory::LST_GetITEM(t_InvTYPE InvTYPE, short nPageListNO) {
    _ASSERT(InvTYPE >= 0 && InvTYPE < MAX_EQUIP_IDX);
    _ASSERT(nPageListNO >= 0 && nPageListNO < INVENTORY_PAGE_SIZE);

    short nListNO = (MAX_EQUIP_IDX + InvTYPE * INVENTORY_PAGE_SIZE) + nPageListNO;

    return LST_GetITEM(nListNO);
}

bool
CInventory::IDX_SetITEM(short nIndexNO, short nListNO, tagITEM& sITEM) {
#ifndef __SERVER
    m_btIndexLIST[nIndexNO] = (BYTE)nListNO;
#endif

    m_ItemLIST[nListNO] = sITEM;
    return true;
}

bool
CInventory::IDX_SetITEM(t_InvTYPE InvTYPE, short nPageListNO, short nTotListNO, tagITEM& sITEM) {
    _ASSERT(InvTYPE >= 0 && InvTYPE < MAX_EQUIP_IDX);
    _ASSERT(nPageListNO >= 0 && nPageListNO < INVENTORY_PAGE_SIZE);
    _ASSERT(nTotListNO >= 0 && nTotListNO < INVENTORY_TOTAL_SIZE);

    short nIndexNO = (MAX_EQUIP_IDX + InvTYPE * INVENTORY_PAGE_SIZE) + nPageListNO;

    return IDX_SetITEM(nIndexNO, nTotListNO, sITEM);
}

bool
CInventory::IDX_SetITEM(t_InvTYPE IdxInvTYPE,
    short nIdxPageListNO,
    t_InvTYPE LstInvTYPE,
    short nLstPageListNO,
    tagITEM& sITEM) {
    _ASSERT(IdxInvTYPE >= 0 && IdxInvTYPE < MAX_EQUIP_IDX);
    _ASSERT(nIdxPageListNO >= 0 && nIdxPageListNO < INVENTORY_PAGE_SIZE);

    _ASSERT(LstInvTYPE >= 0 && LstInvTYPE < MAX_EQUIP_IDX);
    _ASSERT(nLstPageListNO >= 0 && nLstPageListNO < INVENTORY_TOTAL_SIZE);

    short nIndexNO = (MAX_EQUIP_IDX + IdxInvTYPE * INVENTORY_PAGE_SIZE) + nIdxPageListNO;
    short nListNO = (MAX_EQUIP_IDX + LstInvTYPE * INVENTORY_PAGE_SIZE) + nLstPageListNO;

    return IDX_SetITEM(nIndexNO, nListNO, sITEM);
}

short
CInventory::GetWEIGHT(short nListNO) {
    tagITEM* pITEM = &this->m_ItemLIST[nListNO];

    if (0 == pITEM->GetTYPE()) {
        return 0;
    }

    if (pITEM->IsEnableDupCNT()) {
        return ITEM_WEIGHT(pITEM->m_cType, pITEM->m_nItemNo) * pITEM->GetQuantity();
    }

    return ITEM_WEIGHT(pITEM->m_cType, pITEM->m_nItemNo);
}

void
CInventory::SetInventory(short nListNO, int iItem, int iQuantity) {
    if (0 == iItem)
        return;

    tagITEM sITEM;
    sITEM.Init(iItem, iQuantity);
    m_ItemLIST[nListNO] = sITEM;
}

short
CInventory::AppendITEM(tagITEM& sITEM, short& nCurWeight) {
    _ASSERT(sITEM.GetTYPE());

    if (sITEM.IsEmpty()) {
        return -1;
    }

    if (ITEM_TYPE_MONEY == sITEM.m_cType) {
        // µ·ÀÌ´Ù..
        m_i64Money += sITEM.m_uiMoney;
        return 0;
    }

    t_InvTYPE InvTYPE = m_InvTYPE[sITEM.m_cType];
    if (sITEM.IsEnableDupCNT()) {
        for (short nI = 0; nI < INVENTORY_PAGE_SIZE; nI++) {
            // °°Àº ±âÅ¸ ¾ÆÀÌÅÛÀÌ¶óµµ ¾ÆÀÌÅÛ Å¸ÀÔÀÌ Æ²¸°°Íµé·Î ±¸¼ºµÊ...
            if (this->m_ItemPAGE[InvTYPE][nI].GetHEADER() == sITEM.GetHEADER()) {
                if (this->m_ItemPAGE[InvTYPE][nI].GetQuantity() + sITEM.GetQuantity()
                    <= MAX_DUP_ITEM_QUANTITY) {
                    // ´õÇßÀ» °æ¿ì ÃÖ´ë °¹¼ö°¡ ³Ñ¾î °¡¸é »õ ½º·Ô¿¡´Ù ÇÒ´ç.
                    nCurWeight +=
                        (ITEM_WEIGHT(sITEM.m_cType, sITEM.m_nItemNo) * sITEM.GetQuantity());

                    this->m_ItemPAGE[InvTYPE][nI].m_uiQuantity += sITEM.GetQuantity();
                    sITEM.m_uiQuantity = this->m_ItemPAGE[InvTYPE][nI].GetQuantity();

                    _ASSERT(INVENTORY_TOTAL_SIZE
                        > nI + MAX_EQUIP_IDX + (InvTYPE * INVENTORY_PAGE_SIZE));

                    return nI + MAX_EQUIP_IDX + (InvTYPE * INVENTORY_PAGE_SIZE);
                }
            }
        }
    }

    // Áßº¹µÉ¼ö ¾ø´Â Àåºñ¾ÆÀÌÅÛ, PAT¾ÆÀÌÅÛ ÀÌ³ª º¸À¯ÇÑ ¾ÆÀÌÅÛÁß¿¡ °°Àº ¾ÆÀÌÅÛÀÌ ¾ø´Â °æ¿ì...ºó½½·Ô
    // ÇÒ´ç...
    short nInvIDX = GetEmptyInventory(InvTYPE);
    if (nInvIDX >= 0) {
        AppendITEM(nInvIDX, sITEM, nCurWeight);
        return nInvIDX;
    }

    return -1;
}

//-------------------------------------------------------------------------------------------------
#ifndef __SERVER // ¼­¹ö¿¡¼± »ç¿ë ¾ÈÇÔ.
/// Client fucntion
short
CInventory::Add_CatchITEM(short nListRealNO, tagITEM& sITEM, short& nCurWeight) {
    if (ITEM_TYPE_MONEY == sITEM.m_cType) {
        // µ·¾ÆÀÌÅÛÀÏ °æ¿ì ÇöÀç µ·°ú ´õÇÑ´Ù..
        m_i64Money += sITEM.m_uiMoney;
        return 0;
    }

    if (nListRealNO >= INVENTORY_TOTAL_SIZE) {
        return -1;
    }

    if (m_ItemLIST[nListRealNO].m_dwITEM) {
        nCurWeight -= this->GetWEIGHT(nListRealNO);
    }

    if (m_ItemLIST[nListRealNO].GetTYPE() == sITEM.GetTYPE()) {
        // ´õÇÔ...
        m_ItemLIST[nListRealNO].m_uiQuantity += sITEM.m_uiQuantity;
    } else {
        // ±³Ã¼...
        m_ItemLIST[nListRealNO] = sITEM;
    }
    nCurWeight += this->GetWEIGHT(nListRealNO);

    /// Ãß°¡µÈ ¾ÆÀÌÅÛ¿¡ ´ëÇÑ Loopup table °»½Å
    MakeItemIndexList();
    g_pAVATAR->m_HotICONS.UpdateHotICON();

    return nListRealNO;
}
#endif

short
CInventory::AppendITEM(short nListRealNO, tagITEM& sITEM, short& nCurWeight) {
    _ASSERT(sITEM.GetTYPE());

    if (sITEM.IsEmpty()) {
        return -1;
    }

    if (ITEM_TYPE_MONEY == sITEM.m_cType) {
        // µ·¾ÆÀÌÅÛÀÏ °æ¿ì ÇöÀç µ·°ú ´õÇÑ´Ù..
        m_i64Money += sITEM.m_uiMoney;
        return 0;
    }

    if (nListRealNO >= INVENTORY_TOTAL_SIZE) {
        return -1;
    }

    // µ·À» Á¦¿ÜÇÑ ¾ÆÀÌÅÛÀº ¼­¹ö¿¡¼­ ¹ÞÀº ¾ÆÀÌÅÛÀ¸·Î ±³Ã¼...
    if (m_ItemLIST[nListRealNO].m_dwITEM) {
        nCurWeight -= this->GetWEIGHT(nListRealNO);
    }

    m_ItemLIST[nListRealNO] = sITEM;
    nCurWeight += this->GetWEIGHT(nListRealNO);

#ifndef __SERVER // ¼­¹ö¿¡¼± »ç¿ë ¾ÈÇÔ.
    /// Ãß°¡µÈ ¾ÆÀÌÅÛ¿¡ ´ëÇÑ Loopup table °»½Å
    MakeItemIndexList();
    g_pAVATAR->m_HotICONS.UpdateHotICON();
#endif

    return nListRealNO;
}

void
CInventory::DeleteITEM(WORD wListRealNO) {
    m_ItemLIST[wListRealNO].Clear();

#ifndef __SERVER // ¼­¹ö¿¡¼± »ç¿ë ¾ÈÇÔ.
    short nLookUpIndex = GetLookupIndexFromRealIndex(wListRealNO);
    if (nLookUpIndex < 0) {
        //_ASSERT( 0 && "nLookUpIndex < 0" );
        return;
    }

    m_btIndexLIST[nLookUpIndex] = 0;

#endif
}

void
CInventory::SubtractITEM(short nListNO, tagITEM& sITEM, short& nCurWEIGHT) {
    if (ITEM_TYPE_MONEY == sITEM.m_cType) {
        m_i64Money -= sITEM.m_uiMoney;
        return;
    }

    if (m_ItemLIST[nListNO].GetTYPE()) {
        nCurWEIGHT -= m_ItemLIST[nListNO].Subtract(sITEM);
    }
}

short
CInventory::FindITEM(tagITEM& sITEM) {
    t_InvTYPE InvTYPE = m_InvTYPE[sITEM.m_cType];

    for (short nI = 0; nI < INVENTORY_PAGE_SIZE; nI++) {
        if (this->m_ItemPAGE[InvTYPE][nI].GetHEADER() == sITEM.GetHEADER()) {
            return nI + MAX_EQUIP_IDX + (InvTYPE * INVENTORY_PAGE_SIZE);
        }
    }
    return -1;
}

short
CInventory::GetEmptyInventory(short nInvPAGE) {
    _ASSERT(nInvPAGE >= INV_WEAPON && nInvPAGE < MAX_INV_TYPE);

    for (short nI = 0; nI < INVENTORY_PAGE_SIZE; nI++) {
        if (m_ItemLIST[MAX_EQUIP_IDX + (nInvPAGE * INVENTORY_PAGE_SIZE) + nI].m_cType == 0) {
            _ASSERT(INVENTORY_TOTAL_SIZE > nI + MAX_EQUIP_IDX + (nInvPAGE * INVENTORY_PAGE_SIZE));

            return nI + MAX_EQUIP_IDX + (nInvPAGE * INVENTORY_PAGE_SIZE);
        }
    }

    return -1;
}

short
CInventory::GetEmptyInvenSlotCount(t_InvTYPE InvType) {
    short nCount = 0;

    _ASSERT(InvType >= INV_WEAPON && InvType < MAX_INV_TYPE);

    for (short nl = 0; nl < INVENTORY_PAGE_SIZE; ++nl) {
        if (m_ItemPAGE[InvType][nl].GetTYPE() == 0)
            ++nCount;
    }
    return nCount;
}