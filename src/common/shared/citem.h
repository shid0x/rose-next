#pragma once
#include "datatype.h"

#include "rose/util/uuid.h"

#include "nlohmann/json_fwd.hpp"

#define MAX_ITEM_LIFE 1000
#define MAX_DUP_ITEM_QUANTITY 999

#pragma pack(push, 1)
struct tagPartITEM {
    unsigned int m_nItemNo : 11;
    unsigned int m_nGEM_OP : 9;
    unsigned int m_bHasSocket : 1;
    unsigned int m_cGrade : 4;
};

#ifndef __SERVER
    #define tagITEM tagBaseITEM
#endif

int getItemNo(int iFullItemNo);
int getItemType(int iFullItemNo);
int setItemFullNo(int iItemType, int iItemNo);

struct tagBaseITEM {
    union {
        struct {
            unsigned short m_cType : 5;
            unsigned short m_nItemNo : 11;

            unsigned int m_nGEM_OP : 9;
            unsigned int m_cDurability : 7;

            unsigned int m_nLife : 10;
            unsigned int m_bHasSocket : 1;
            unsigned int m_bIsAppraisal : 1;
            unsigned int m_cGrade : 4;
        };

        struct {
            unsigned short m_cType_1 : 5;
            unsigned short m_nItemNo_1 : 11;

            unsigned int m_uiQuantity : 32;
        };

        struct {
            unsigned short m_cType_2 : 5;
            unsigned short m_nReserved1 : 11;

            unsigned int m_uiMoney : 32;
        };

        struct {
            unsigned short m_wHeader : 16;
            unsigned int m_dwBody : 32;
        };

        struct {
            DWORD m_dwLSB;
            WORD m_wMSB;
        };

        struct {
            WORD m_wLSB;
            DWORD m_dwMSB;
        };

        DWORD m_dwITEM;
    };

    void init();
    void Init(int iItem, unsigned int uiQuantity = 1);
    void Init(unsigned int item_type, unsigned int item_no, unsigned int uiQuantity = 1);

    bool is_stackable() {
        return this->is_stackable(this->GetTYPE());
    }
    bool is_consumable() { return this->GetTYPE() == ITEM_TYPE_USE; };
    bool is_money() { return this->GetTYPE() == ITEM_TYPE_MONEY; }
    bool is_vehicle_part() {
        return (this->GetTYPE() == ITEM_TYPE_RIDE_PART);
    }

    static bool is_stackable(unsigned int t) {
        return (t == ITEM_TYPE_USE || t == ITEM_TYPE_ETC || t == ITEM_TYPE_ETC || t == ITEM_TYPE_GEM
            || t == ITEM_TYPE_NATURAL || t == ITEM_TYPE_QUEST || t == ITEM_TYPE_MONEY);
    }

    void Clear() { m_dwLSB = m_wMSB = 0; }

    unsigned short GetItemNO() { return m_nItemNo; }

    bool IsEmpty() { return (0 == m_wHeader); }

    unsigned short GetHEADER() { return m_wHeader; }

    unsigned short GetTYPE() { return m_cType; }

    unsigned short GetGrade() { return m_cGrade; }
    unsigned short GetOption() { return m_nGEM_OP; }
    unsigned short GetGemNO() { return m_nGEM_OP; }

    unsigned short GetLife() { return m_nLife; }
    unsigned short GetDurability() { return m_cDurability; }
    unsigned int GetMoney() { return m_uiMoney; }

    bool IsAppraisal() { return (0 != m_bIsAppraisal); }
    bool HasSocket() { return (0 != m_bHasSocket); }

    bool IsEnableDROP();
    bool IsEnableSELL();
    bool IsEnableKEEPING();

    static bool IsValidITEM(DWORD wType, DWORD wItemNO);

    static bool IsValidITEM(tagBaseITEM* pItem) {
        return tagBaseITEM::IsValidITEM(pItem->GetTYPE(), pItem->GetItemNO());
    }

    bool IsValidITEM() { return IsValidITEM(this->GetTYPE(), this->GetItemNO()); }

    static bool IsEnableDupCNT(unsigned short cType) {
        return (cType >= ITEM_TYPE_USE && cType < ITEM_TYPE_RIDE_PART);
    }
    bool IsEnableDupCNT() const { return IsEnableDupCNT(m_cType); }

    bool IsCreated() const { return false; }
    bool IsEquipITEM() { return (m_cType && m_cType < ITEM_TYPE_USE); }
    bool IsEtcITEM() { return (m_cType > ITEM_TYPE_USE && m_cType < ITEM_TYPE_QUEST); }

    bool IsTwoHands();

    t_eSHOT GetShotTYPE();
    t_EquipINDEX GetEquipPOS();

    unsigned int GetQuantity();

#ifndef __SERVER
    short Subtract(tagITEM& sITEM);
    void SubtractOnly(tagITEM& sITEM);

    bool IsEnableAppraisal();
    bool IsEnableExchange();
    bool IsEnableSeparate();
    bool IsEnableUpgrade();
    bool HasLife();
    bool HasDurability();
    unsigned short GetModelINDEX();
    char* GettingMESSAGE(int iInventoryListNo);
    char* GettingMESSAGE();
    char* GettingMESSAGE_Party(const char* partyName_);
    char* GettingQuestMESSAGE();
    char* SubtractQuestMESSAGE();

    static t_eSHOT GetNaturalBulletType(int iItemNo);
    t_eSHOT GetBulletType();

    int GetHitRate();
    int GetAvoidRate();
    bool IsEqual(int iType, int iItemNo);
    int GetUpgradeCost();
    int GetSeparateCost();
    int GetAppraisalCost();
    const char* GetName();
#endif
};

#ifdef __SERVER
struct tagITEM: public tagBaseITEM {
    Rose::Util::UUID uuid;
    union {
        DWORD m_dwSN[2];
        __int64 m_iSN;
    };
    bool m_bCreated;

    short Subtract(tagITEM& sITEM);
    void SubtractOnly(tagITEM& sITEM);

    bool SubQuantity() {
        if (GetQuantity() > 0) {
            --m_uiQuantity;
            return true;
        }
        return false;
    }
    bool SubQuantity(unsigned int uiCnt) {
        if (GetQuantity() > uiCnt) {
            m_uiQuantity -= uiCnt;
            return true;
        } else if (m_uiQuantity == uiCnt) {
            this->Clear();
            return true;
        }
        return false;
    }

    void init() {
        // RAM: Adding a constructor breaks a lot of code related to packets so we
        // settle with this init function for now.
        this->Clear();
        this->uuid = Rose::Util::UUID::generate();
        this->m_bCreated = false;
    }

    void Init(int iItem, unsigned int uiQuantity = 1) {
        tagBaseITEM::Init(iItem, uiQuantity);
        this->uuid = Rose::Util::UUID::generate();
        m_iSN = 0;
        m_bCreated = false;
    }

    void Init(unsigned int item_type, unsigned int item_no, unsigned int uiQuantity = 1) {
        tagBaseITEM::Init(item_type, item_no, uiQuantity);
        this->uuid = Rose::Util::UUID::generate();
        m_iSN = 0;
        m_bCreated = false;
    }

    void Clear() {
        tagBaseITEM::Clear();
        m_iSN = 0;
        m_bCreated = false;
    }

    bool IsCreated() const { return m_bCreated; }

    bool IsValidITEM() {
        if (this->uuid.is_nil()) {
            return false;
        }
        return tagBaseITEM::IsValidITEM(this->GetTYPE(), this->GetItemNO());
    }

    static bool IsValidITEM(DWORD item_type, DWORD item_id) {
        return tagBaseITEM::IsValidITEM(item_type, item_id);
    }

    static bool IsValidITEM(tagITEM* pItem) {
        return tagBaseITEM::IsValidITEM(pItem->GetTYPE(), pItem->GetItemNO());
    }

    static bool IsValidITEM(tagBaseITEM* pItem) {
        return tagBaseITEM::IsValidITEM(pItem->GetTYPE(), pItem->GetItemNO());
    }

    void operator=(tagBaseITEM& rBASE) {
        m_wLSB = rBASE.m_wLSB;
        m_dwMSB = rBASE.m_dwMSB;
        m_iSN = 0;
        m_bCreated = false;
    }
};
#endif
#pragma pack(pop)

void to_json(nlohmann::json& j, const tagBaseITEM& i);
void from_json(const nlohmann::json& j, tagBaseITEM& i);
