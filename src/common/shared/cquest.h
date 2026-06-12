#pragma once

#include "CBITArray.h"
#include "CItem.h"

#include "nlohmann/json_fwd.hpp"

#include <bitset>

#define QUEST_SWITCH_PER_QUEST 32 // 퀘스트당 스위치
#define QUEST_VAR_PER_QUEST 10 // 퀘스트당 변수
#define QUEST_ITEM_PER_QUEST 6 // 퀘스트당 인벤토리 수

typedef short t_qstvar;

class CQUEST {
public:
    // Total 2 + 2 + 2*10 + 1*4 = 28 bytes !!!
#pragma pack(push, 1)
    WORD m_wID;
    DWORD m_dwExpirationTIME; // 만기 시간...	0이면 무제한
    t_qstvar m_pVAR[QUEST_VAR_PER_QUEST]; // 10개 변수
    std::bitset<QUEST_SWITCH_PER_QUEST> switches;
    tagBaseITEM m_ITEMs[QUEST_ITEM_PER_QUEST];
#pragma pack(pop)

public:
    void Init();
    void SetID(WORD wID, bool bResetTimer);
    WORD GetID(void) const { return m_wID; }

    void Set_VAR(int iVarNO, int iValue);
    int Get_VAR(int iVarNO);

    void set_switch(int idx, int val);
    int get_switch(int idx);

    DWORD GetExpirationTIME() const { return m_dwExpirationTIME; }
    DWORD GetRemainTIME();

    tagBaseITEM* GetSameITEM(WORD wItemHEADER);
    tagBaseITEM* GetSlotITEM(int iSlotNo);
    bool SubITEM(tagITEM& sITEM);
    bool AddITEM(tagITEM& sITEM);

    void CheckExpiredTIME();
};

// Worldserver does not implement CQuest (does not include CQuest.cpp)
// so including these functions causes issues
#ifndef __WORLDSERVER
void to_json(nlohmann::json& j, const CQUEST& q);
void from_json(const nlohmann::json& j, CQUEST& q);
#endif