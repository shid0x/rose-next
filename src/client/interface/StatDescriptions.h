#ifndef _STAT_DESCRIPTIONS_H_
#define _STAT_DESCRIPTIONS_H_

/**
 * Plain-English explanation of each character stat: a title, a one-line
 * summary, then the concrete things the stat feeds into.
 *
 * Shared by the classic character window ( CCharacterDLG ) and the UI2 one
 * ( RoseRmlCharacterWindow ) so the two can never disagree. The weapon lists
 * mirror the server's CObjAVT::Cal_ATTACK switch ( and the CUserDATA::Cal_*
 * formulas for the rest ) -- keep them in sync if those change.
 */

class CInfo;

struct StatDescription {
    const char* pszTitle;
    const char* pszSummary;
    const char* pszEffects[4]; ///< NULL-terminated bullet list
};

/// The six base abilities, in BA_* order: STR, DEX, INT, CON, CHA, SEN.
extern const StatDescription g_BaseStatDescriptions[6];

/// The derived combat stats, in this order: attack power, defense, magic
/// resistance, accuracy, critical, dodge, attack speed, move speed.
extern const StatDescription g_DerivedStatDescriptions[8];

/// Fill a tooltip for one stat: bold title, plain summary, a blank gap, then
/// the bullet list of concrete effects. Position and registration are left to
/// the caller.
void BuildStatToolTip(const StatDescription& Desc, CInfo& Info);

#endif /// _STAT_DESCRIPTIONS_H_
