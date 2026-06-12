#include "stdAFX.h"

#include "CQuest.h"

#ifndef __SERVER
    #include "..\GameProc\CDayNNightProc.h"
    #include "../Game.h"
    #include "../interface/it_mgr.h"
    #include "io_quest.h"
#else
    #include "IO_Quest.h"
    extern DWORD Get_WorldPassTIME();
#endif

#include "nlohmann/json.hpp"

using json = nlohmann::json;

#define MAX_ASSOCIATION 20 // 최대 조합 갯수

// 메인 퀘스트 진행 수치
// 친분 수치[ MAX_ASSOCIATION ] .	처음 시작 시 : 0
// 퀘스트 진행 수치[ MAX_ASSOCIATION ]
/*
// 진행중인 퀘스트
{
    퀘스트 수행 변수 10개
    퀘스트 스위치 8개
    퀘스트 타이머
    퀘스트 제공자
}
*/

//-------------------------------------------------------------------------------------------------
void
CQUEST::Init() {
    m_wID = 0;
    this->switches.reset();
    ::ZeroMemory(m_pVAR, sizeof(m_pVAR));
    ::ZeroMemory(m_ITEMs, sizeof(m_ITEMs));
}

void
CQUEST::CheckExpiredTIME() {
    // if ( this->m_wID )
    {
        if (QUEST_TIME_LIMIT(this->m_wID)) {
            DWORD dwNewExpired = ::Get_WorldPassTIME() + QUEST_TIME_LIMIT(this->m_wID);
            if (this->m_dwExpirationTIME > dwNewExpired) {
                // 서버 통합되면서 케릭이전될때... 서버타임이 틀려지니까...
                this->m_dwExpirationTIME = dwNewExpired;
            }
        }
    }
}

void
CQUEST::SetID(WORD wID, bool bResetTimer) {
    m_wID = wID;

    if (bResetTimer) {
        if (QUEST_TIME_LIMIT(wID)) {
            m_dwExpirationTIME = ::Get_WorldPassTIME() + QUEST_TIME_LIMIT(wID);
        } else
            m_dwExpirationTIME = 0; // 무제한..
    } else if (QUEST_TIME_LIMIT(wID)) {
        // 리셋이 필요없는 경우인데...바뀌는것이 시간 제한이 필요하면...
        m_dwExpirationTIME = ::Get_WorldPassTIME() + QUEST_TIME_LIMIT(wID);
    }
}

//-------------------------------------------------------------------------------------------------
DWORD
CQUEST::GetRemainTIME() {
    DWORD dwCurTime = ::Get_WorldPassTIME();

    if (dwCurTime >= m_dwExpirationTIME) {
        // 남은 시간 없다.
        return 0;
    }

    return (m_dwExpirationTIME - dwCurTime);
}

//-------------------------------------------------------------------------------------------------
void
CQUEST::Set_VAR(int iVarNO, int iValue) {
    if (iVarNO < 0 || iVarNO >= QUEST_VAR_PER_QUEST)
        return;

    m_pVAR[iVarNO] = iValue;
}
int
CQUEST::Get_VAR(int iVarNO) {
    if (iVarNO < 0 || iVarNO >= QUEST_VAR_PER_QUEST)
        return 0;
    return m_pVAR[iVarNO];
}

//-------------------------------------------------------------------------------------------------
void
CQUEST::set_switch(int iSwitchNO, int iValue) {
    if (iSwitchNO < 0 || iSwitchNO >= QUEST_SWITCH_PER_QUEST)
        return;

    this->switches.set(iSwitchNO, iValue);
}

int
CQUEST::get_switch(int iSwitchNO) {
    if (iSwitchNO < 0 || iSwitchNO >= QUEST_SWITCH_PER_QUEST)
        return -1;

    return this->switches[iSwitchNO];
}

//-------------------------------------------------------------------------------------------------
tagBaseITEM*
CQUEST::GetSlotITEM(int iSlotNo) {
    if (iSlotNo < 0 || iSlotNo >= QUEST_ITEM_PER_QUEST)
        return NULL;

    return &(m_ITEMs[iSlotNo]);
}
tagBaseITEM*
CQUEST::GetSameITEM(WORD wItemHEADER) {
    for (short nI = 0; nI < QUEST_ITEM_PER_QUEST; nI++) {
        if (this->m_ITEMs[nI].GetHEADER() == wItemHEADER)
            return &this->m_ITEMs[nI];
    }
    return NULL;
}
bool
CQUEST::SubITEM(tagITEM& sITEM) {
    for (short nI = 0; nI < QUEST_ITEM_PER_QUEST; nI++) {
        if (this->m_ITEMs[nI].GetHEADER() == sITEM.GetHEADER()) {
            tagITEM sOriITEM;
            sOriITEM = this->m_ITEMs[nI];
            sOriITEM.SubtractOnly(sITEM);
            this->m_ITEMs[nI] = sOriITEM;

#ifndef __SERVER
            g_itMGR.AppendChatMsg(sITEM.SubtractQuestMESSAGE(), IT_MGR::CHAT_TYPE_QUEST);
#endif
            return true;
        }
    }
    return false;
}

//-------------------------------------------------------------------------------------------------
bool
CQUEST::AddITEM(tagITEM& sITEM) {
    short nI;

    if (sITEM.IsEnableDupCNT()) {
        // 중복 가능, 같은 아이템이 있는가 ??
        for (nI = 0; nI < QUEST_ITEM_PER_QUEST; nI++) {
            if (this->m_ITEMs[nI].GetHEADER() == sITEM.GetHEADER()) {
                this->m_ITEMs[nI].m_uiQuantity += sITEM.GetQuantity();
                return true;
            }
        }
    }

    // 빈곳에 추가..
    for (nI = 0; nI < QUEST_ITEM_PER_QUEST; nI++) {
        if (0 == this->m_ITEMs[nI].GetHEADER()) {
            this->m_ITEMs[nI] = sITEM;
            return true;
        }
    }

    return false;
}

void to_json(json& j, const CQUEST& q) {
    json vars = json::array();
    for (size_t i = 0; i < QUEST_VAR_PER_QUEST; ++i) {
        vars.push_back(q.m_pVAR[i]);
    }

    json switches = json::array();
    for (size_t i = 0; i < q.switches.size(); ++i) {
        const int val = q.switches[i]; // Implicit conversion from bool to int
        switches.push_back(val);
    }

    json items = json::array();
    for (size_t i = 0; i < QUEST_ITEM_PER_QUEST; ++i) {
        items.push_back(q.m_ITEMs[i]);
    }

    j["id"] = q.m_wID;
    j["expiration"] = q.m_dwExpirationTIME;
    j["vars"] = vars;
    j["switches"] = switches;
    j["items"] = items;
}

void
from_json(const json&j, CQUEST& q) {
    if (!j.is_object()) {
        return;
    }

    if (j.contains("id") && j["id"].is_number_integer()) {
        q.m_wID = j["id"];
    }

    if (j.contains("expiration") && j["expiration"].is_number_integer()) {
        q.m_dwExpirationTIME = j["expiration"];
    }

    if (j.contains("vars") && j["vars"].is_array()) {
        json vars = j["vars"];
        for (size_t i = 0; i < min(vars.size(), QUEST_VAR_PER_QUEST); ++i) {
            q.m_pVAR[i] = vars[i];
        }
    }

    if (j.contains("switches") && j["switches"].is_array()) {
        json switches = j["switches"];
        for (size_t i = 0; i < min(switches.size(), QUEST_SWITCH_PER_QUEST); ++i) {
            const int val = switches[i]; // Implicit conversion from json to int
            q.switches.set(i, val);
        }
    }

    if (j.contains("items") && j["items"].is_array()) {
        json items = j["items"];
        for (size_t i = 0; i < min(items.size(), QUEST_ITEM_PER_QUEST); ++i) {
            q.m_ITEMs[i] = items[i];
        }
    }
}
