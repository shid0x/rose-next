#include "stdafx.h"

#include "StatDescriptions.h"
#include "CInfo.h"

/// Plain-English explanation of each base ability, shown as a tooltip when the
/// player hovers the stat name/value on the ABILITY tab. Each entry is a bold
/// title, a one-line summary, then the concrete things the stat feeds into.
/// Rows are ordered top-to-bottom exactly as DrawAbilityInfo() draws them.
/// The weapon lists mirror the server's CObjAVT::Cal_ATTACK switch (and the
/// CUserDATA::Cal_* formulas for the rest) -- keep them in sync if those change.
const StatDescription g_BaseStatDescriptions[6] = {
    {"Strength (STR)",
        "Boosts melee weapons and toughness.",
        {"Attack power: swords, axes, spears, katars, dual swords, launchers",
            "Maximum HP",
            "Defense",
            "Carry weight"}},
    {"Dexterity (DEX)",
        "Improves bows, katars, dual swords and evasion.",
        {"Attack power: bows, crossbows, katars, dual swords",
            "Some gun attack power",
            "Dodge rate (avoid enemy hits)",
            "Movement speed"}},
    {"Intelligence (INT)",
        "Powers magic and your MP pool.",
        {"Maximum MP",
            "Attack power: wands, staves",
            "Magic skill damage, heal & buff strength",
            "Magic resistance"}},
    {"Concentration (CON)",
        "Helps you land hits and recover.",
        {"Accuracy (hit rate)",
            "Attack power: guns, launchers",
            "HP & MP recovery speed",
            "Crafting success, and a little critical"}},
    {"Charm (CHA)",
        "Improves quest rewards and loot.",
        {"Bigger quest EXP, zuly & item rewards",
            "Dropped gear more often has bonus stats",
            NULL,
            NULL}},
    {"Sensibility (SEN)",
        "Sharpens criticals and skill damage.",
        {"Critical hit rate",
            "Skill damage",
            "Attack power: wands, guns, launchers (bows a little)",
            "Crafted gear more often has bonus stats"}},
};

/// Right column: the derived combat stats the base abilities feed into.
/// Ordered top-to-bottom exactly as DrawAbilityInfo() draws them.
const StatDescription g_DerivedStatDescriptions[8] = {
    {"Attack Power",
        "How hard your attacks hit.",
        {"More damage on every normal attack",
            "Grows with your weapon and its main stat (STR/DEX/INT...)",
            NULL,
            NULL}},
    {"Defense",
        "Reduces physical damage you take.",
        {"Lowers damage from melee/physical hits",
            "Comes from armor, plus STR and level",
            NULL,
            NULL}},
    {"Magic Resistance",
        "Reduces magic damage you take.",
        {"Lowers damage from spells", "Comes from gear, plus INT and level", NULL, NULL}},
    {"Accuracy",
        "How reliably you land hits.",
        {"Higher chance to hit (vs the enemy's dodge)",
            "Raised mainly by CON and your weapon",
            NULL,
            NULL}},
    {"Critical",
        "Chance to score critical hits.",
        {"Critical hits deal extra damage", "Raised mainly by SEN (and some CON)", NULL, NULL}},
    {"Dodge",
        "How well you avoid enemy attacks.",
        {"Higher chance to evade incoming hits",
            "Raised mainly by DEX, armor and level",
            NULL,
            NULL}},
    {"Attack Speed",
        "How fast you attack.",
        {"More swings over time means more damage",
            "Set by your weapon (and some buffs)",
            NULL,
            NULL}},
    {"Move Speed",
        "How fast you move.",
        {"Faster running and travel", "Raised by boots and DEX (and some buffs)", NULL, NULL}},
};

void
BuildStatToolTip(const StatDescription& Desc, CInfo& Info) {
    Info.AddString(Desc.pszTitle,
        D3DCOLOR_ARGB(255, 255, 221, 102),
        g_GameDATA.m_hFONT[FONT_NORMAL_BOLD]);
    Info.AddString(Desc.pszSummary, g_dwWHITE);
    Info.AddString(""); ///separator gap
    for (int e = 0; e < 4 && Desc.pszEffects[e]; ++e)
        Info.AddString(CStr::Printf("- %s", Desc.pszEffects[e]), g_dwBlueToolTip);
}
