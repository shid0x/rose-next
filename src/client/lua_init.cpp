/*
    $Header: /Client/LUA_Init.cpp 6     04-04-07 10:27a Jeddli $
*/
#include "stdAFX.h"

#include "Event\Quest_FUNC.h"
#include "Game_FUNC.h"
//-------------------------------------------------------------------------------------------------

#include "game_func_def.inc"
#include "event\quest_func_def.inc"

// Global
void
GF_Init(lua_State* L) {
#include "game_func_reg.inc"
}

// Quest
void
QF_Init(lua_State* L) {
#include "game_func_reg.inc"
#include "event\quest_func_reg.inc"

    ZL_SETVAR(SV_SEX);
    ZL_SETVAR(SV_BIRTH);
    ZL_SETVAR(SV_CLASS);
    ZL_SETVAR(SV_UNION);
    ZL_SETVAR(SV_RANK);
    ZL_SETVAR(SV_FAME);
    ZL_SETVAR(SV_STR);
    ZL_SETVAR(SV_DEX);
    ZL_SETVAR(SV_INT);
    ZL_SETVAR(SV_CON);
    ZL_SETVAR(SV_CHA);
    ZL_SETVAR(SV_SEN);
    ZL_SETVAR(SV_EXP);
    ZL_SETVAR(SV_LEVEL);
    ZL_SETVAR(SV_POINT);

    /*
        ZL_SETVAR( BODY_PART_FACE		);
        ZL_SETVAR( BODY_PART_HAIR		);
        ZL_SETVAR( BODY_PART_HELMET		);
        ZL_SETVAR( BODY_PART_ARMOR		);
        ZL_SETVAR( BODY_PART_GAUNTLET	);
        ZL_SETVAR( BODY_PART_BOOTS		);
        ZL_SETVAR( BODY_PART_GOGGLE		);
        ZL_SETVAR( BODY_PART_KNAPSACK	);
        ZL_SETVAR( BODY_PART_WEAPON_R	);
        ZL_SETVAR( BODY_PART_WEAPON_L	);
    */

    ZL_SETVAR(ITEM_TYPE_FACE_ITEM);
    ZL_SETVAR(ITEM_TYPE_HELMET);
    ZL_SETVAR(ITEM_TYPE_ARMOR);
    ZL_SETVAR(ITEM_TYPE_GAUNTLET);
    ZL_SETVAR(ITEM_TYPE_BOOTS);
    ZL_SETVAR(ITEM_TYPE_KNAPSACK);
    ZL_SETVAR(ITEM_TYPE_JEWEL);
    ZL_SETVAR(ITEM_TYPE_WEAPON);
    ZL_SETVAR(ITEM_TYPE_SUBWPN);
    ZL_SETVAR(ITEM_TYPE_USE);
    ZL_SETVAR(ITEM_TYPE_ETC);
    ZL_SETVAR(ITEM_TYPE_GEM);
    ZL_SETVAR(ITEM_TYPE_NATURAL);
    /*
    ITEM_TYPE_QUEST
    ITEM_TYPE_SPECIAL
    ITEM_TYPE_MONEY = 0x1f
    */
}

//-------------------------------------------------------------------------------------------------
// Probe state for the NPC overhead quest icon (CEvent::GetQuestSignal). The icon
// evaluator walks the NPC's dialog tree headlessly, which means calling the
// dialog's check AND click functions -- so everything that acts on the world has
// to be inert here. QF_Init registers the real bindings; every name that is not a
// pure getter is then replaced by a no-op returning 0, and the two quest entry
// points record the trigger name they were handed.
static std::vector<std::string>* s_pProbeTriggerNames = NULL;

static const char* s_szProbeSafeFuncs[] = {"QF_findQuest",
    "QF_getEpisodeVAR",
    "QF_getEventOwner",
    "QF_getJobVAR",
    "QF_getNpcQuestZeroVal",
    "QF_getPlanetVAR",
    "QF_getQuestCount",
    "QF_getQuestID",
    "QF_getQuestItemQuantity",
    "QF_getQuestSwitch",
    "QF_getQuestVar",
    "QF_getSkillLevel",
    "QF_getUnionVAR",
    "QF_getUserSwitch",
    "QF_hasAruaFate",
    "QF_hasFate",
    "QF_hasHebarnFate",
    "GF_checkNumOfInvItem",
    "GF_checkTownItem",
    "GF_checkUserMoney",
    "GF_getDate",
    "GF_getGameVersion",
    "GF_getIDXOfInvItem",
    "GF_getItemRate",
    "GF_getName",
    "GF_getReviveZoneName",
    "GF_getTownRate",
    "GF_getTownVar",
    "GF_getVariable",
    "GF_getWorldRate",
    "GF_getZone",
    NULL};

static bool
IsProbeSafeFunc(const char* szName) {
    for (int i = 0; s_szProbeSafeFuncs[i]; i++)
        if (strcmp(s_szProbeSafeFuncs[i], szName) == 0)
            return true;
    return false;
}

static void
ProbeRecordTrigger(lua_State* L) {
    if (s_pProbeTriggerNames && lua_gettop(L) >= 1 && lua_isstring(L, 1))
        s_pProbeTriggerNames->push_back(lua_tostring(L, 1));
}

static int
lf_ProbeNop(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}

// Never fires the trigger; reports success so the click function carries on.
static int
lf_ProbeDoQuestTrigger(lua_State* L) {
    ProbeRecordTrigger(L);
    lua_pushnumber(L, 1);
    return 1;
}

// The real condition walk (no rewards, same call the dialog makes), plus the name.
static int
lf_ProbeCheckQuestCondition(lua_State* L) {
    ProbeRecordTrigger(L);
    int iResult = 0;
    if (lua_gettop(L) >= 1 && lua_isstring(L, 1))
        iResult = QF_checkQuestCondition(lua_tostring(L, 1));
    lua_pushnumber(L, iResult);
    return 1;
}

void
QF_InitProbe(lua_State* L) {
    QF_Init(L);

#undef ZL_REGISTER
#define ZL_REGISTER(func_name)          \
    if (!IsProbeSafeFunc(#func_name))   \
        lua_register(L, #func_name, lf_ProbeNop);
#include "game_func_reg.inc"
#include "event\quest_func_reg.inc"
#undef ZL_REGISTER
#define ZL_REGISTER(func_name) lua_register(L, #func_name, lf_##func_name);

    lua_register(L, "QF_doQuestTrigger", lf_ProbeDoQuestTrigger);
    lua_register(L, "QF_checkQuestCondition", lf_ProbeCheckQuestCondition);

    // The walk runs quiet (CEventLIST::GetNpcQuestSignal); this names the fault.
    lua_CaptureErrorText(L);
}

void
QF_SetProbeTriggerSink(std::vector<std::string>* pNames) {
    s_pProbeTriggerNames = pNames;
}

//-------------------------------------------------------------------------------------------------
