"""Stage 5b: Karkia's loot tables.

--- 2026-11-19: reshaped (roadmap 1.2) -- this section supersedes the rates below

The tables were measured against the whole game (`economy-audit.py drops`,
doc/project-drops.md section 7) and brought onto the 1-200 philosophy, the user's
decisions: a field kill paid gear 6-13% of the time against 1-4% everywhere below 200,
materials 26-30% against 58-64% at 170-199, and no jewellery or gems at all. Now, per
kill at level parity, measured for the double drop (roadmap 1.3: the monster's own table
always, then the zone row):

    field      materials ~40-49%, consumables 6%, gear 4.2%, jewellery 1.5% (Diamond,
               q56-74 earrings), gems 0.8% ([6]/[7]), money 15%, nothing ~23%
    bosses     ~90% gear ("i don't wanna frustrate the players"): six top slots (the
               mythicals' 36%), two armour buckets (Norse / OG), fifteen slots of the tier
               below; then materials, a bottle, jewellery, a gem
    zone rows  real map tables: ~55-65% materials, 12% consumables, no gear

The shapes (`field_table` / `boss_table` / `map_table`, slot odds in the comment above
them) are shared with add-oro-drops.py. The pools follow relevel-200-band.py: the
Cemetery band (lv230-235) mixes the lv230 and lv235 lines, the Spire band, the
flashbacks and the crypts' corridors pay lv235 (the crypts paid the lv230 line from
2026-11-13 to 11-19), Asmodeus (124, lv235) has a source at last. The Neg Golem, the
Cemetery's king, has a boss table of its own (918; it shared the trash table). The rate
tables in the sections below are the 2026-09/11 history.

Karkia's tables were wired at stage 5 and left empty. This fills them, and it is
the first authored endgame loot tier the game has had -- Oro's 38 tables are
still empty and should get the same treatment next (doc/project-drops.md #5).

--- the three things that decide every number here

**1. The level gap picks the audience, not us.** `Get_DropITEM` returns false at
10+ levels above the monster, so a level-240 player gets *nothing* from anything
below 231. Karkia therefore has two separate loot audiences and the tiers are
placed to match:

    900/901/902 + zone 87   Cemetery, mobs lv215-228   -> players 215-238
    906         + zone 88   Spire Village, lv228-240   -> the only cap-level farm
    912         + zones 90, 91   the catacombs, lv235-240 (2026-11-10)
    913-917                 the catacombs' five bosses, lv240, one class line each
    903/904/905/907/908     bosses, lv235-240

That is also why the lv240 mythical weapons are boss-only: they have to come off
lv235+ monsters or nobody at cap could ever earn one.

**2. Every kill already rolls.** With `NPC_DROP_ITEM` (col 20) at 80, `drop_var`
is never <= 0 at level parity, so there is no "does it drop" step -- **how many
of slots 0..29 are filled *is* the drop rate.** Measured over 1.5M simulated
kills at parity: slots 0-4 are ~6.0% each, decaying to 1.17% at slot 29; filling
12 slots yields 61.7%, filling all 30 yields 100%. 12 is the house figure, and
matches our live 200-210 tables.

**3. Two multipliers apply before any cell is read**, and they are easy to
forget:

    col 19 (money)  -- fires *instead of* an item, so it eats the item rate
    col 20 (80)     -- 80% own table, 20% the row numbered after the ZONE

The zone fallback is why rows 87 and 88 are written too: they mirror 900 and 906
so the 20% is not a dead roll. Without that mirroring every rate below drops by
a fifth (shields would be 1 in 37, not 1 in 30).

--- the resulting rates, at level parity

    materials / use items      46% of kills
    a shield  (redirect @ 10)  1 in 30
    a weapon  (redirect @ 11)  1 in 32
    money     (col 19 = 15)    15% of kills
    nothing                    33%

    a mythical weapon from a boss   36% per kill  (slots 0-7, col 19 = 0 so
                                                   money never eats the roll)

Table 906 is the exception and carries **three** redirects rather than two: it
is the cap-level farm and has twice the weapon pool (10 of the 13 lv235 weapons
against 5 elsewhere), so weapons run 1 in 16 there. Deliberate -- the hardest monsters in
the zone should pay best.

--- what fills them

Materials are metals, leather and "hearts", never wood: Karkia is a dead world
of ruins and undead and its tables should not read like a forest. Bosses get the
top of the range.

(2026-10-01: the "shield" bucket is now an off-hand bucket -- repackage-subweapons.py
turned most of the imported shields into Support Tools and Charms and left one
real shield per tier; see the OFF_* lists. Re-applied with `--rewrite`.)

The signature drops are the two imported Jrose sets, which have **no source
anywhere in the game** until now:

  * 17 shields at lv210-240 (`9:308-342`). We ship exactly two endgame shields
    of our own, so this is the worst-served slot in the game by a wide margin.
  * 34 weapons across three tiers (`8:1381-1453`), reachable at all only since
    the drop encoding was widened -- ids above 999 could not be addressed by a
    drop cell before. See rose/common/drop_item_code.h.

**The catacombs, interim (2026-11-13, the user: "every boss loots the most powerful
stuff in the game, each one a different type of weapon / armors. Trash monsters:
lvl 130 stuff"; to be refined later).** Each of the five bosses has a table of its
own, 913-917, and pays one class line of the lv240 gear: its weapon types' mythicals
in the eight common slots (~36% a kill), the red weapon of the same types behind one
redirect (the red set has no other source) and that line's Norse set behind two
(Ferdinando, the fifth boss for four armour lines, pays the lv240 off-hands there).
See CRYPT_BOSSES. The crypt's monsters (912) pay the tier below: the plain lv230
weapon line, all thirteen, and an OG armour window. **"lvl 130" is read as 230**:
`Get_DropITEM` pays nobody 10 levels above a lv235 monster, so level-130 gear would
reach only players who are 95 levels past it. Confirmed by the user the same day.

**Armour (2026-10-20, `armour-endgame-sets.py`):** the OG cinematic set (lv230)
rides the three lv228-240 field tables as extra redirects paid for by one material
each (still 12 slots; 2.5% / 5.8% / 5.8% an OG piece per kill), and the Norse set
(lv240) the five boss tables, two redirects on top (~5.5% a piece per boss kill,
mythicals unchanged at ~37%). Both are drop-only by the user's call.

The Jrose *back* items are deliberately not here: they cap at level 150 (DEF
5-29), so dropping one off a level-238 monster would be wrong. They want a pass
of their own aimed at the 100-150 band.

--- NPC table changes

Both Drakes are bosses that were sharing a *trash* table -- Deadly Drake (2729)
pointed at 900 and Deadly Drake Alpha (2699) at 906, so they dropped exactly
what the mobs around them did. They get rows 907 and 908.

Idempotent. `--dry-run` / `--verify` / `--restore`. Backups go to build/, never
beside the data: src/pipeline/src/pack.rs walks the data tree filtering only
*hidden* entries -- no extension filter -- so a .bak in data/3DDATA/STB gets
baked into the .vfs.

After running: restart the servers (they cache STBs at startup) and re-bake the
client VFS (the Monster Inspector reads these tables client-side).
"""
import argparse
import collections
import importlib.util
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB = os.path.join(ROOT, "data", "3DDATA", "STB")
DROP_STB = os.path.join(STB, "ITEM_DROP.STB")
NPC_STB = os.path.join(STB, "LIST_NPC.STB")
BACKUP = os.path.join(ROOT, "build", "karkia-drops-backup")
SIDECAR = os.path.join(ROOT, "build", "karkia-drops.json")

WIDE_BASE = 100000          # Rose::Store::kWideBase
LEGACY_MAX_NO = 999
MAX_ITEM_NO = 2047          # tagBaseITEM: m_nItemNo is 11 bits
MAX_SENTINEL = 1000         # Rose::Drop::kMaxSentinel
REDIRECT_BASE_COL = 25      # Rose::Drop::kRedirectBaseColumn (26 until 2026-11-19: one column late)
REDIRECT_WIDTH = 5
COMMON_SLOTS = 30           # the main roll is RANDOM(30)
LAST_SLOT = 49              # column 50, the table's last (group 4's fifth cell since the base fix)

T_BACK, T_WEAPON, T_SUBWPN, T_USE, T_NATURAL = 6, 8, 9, 10, 12
T_CAP, T_BODY, T_ARMS, T_FOOT = 2, 3, 4, 5

# LIST_NPC columns
COL_DROP_TYPE, COL_DROP_MONEY, COL_DROP_ITEM = 18, 19, 20

MONEY_FIELD = 15            # % of kills that pay zuly instead of an item
MONEY_BOSS = 0              # bosses pay items; money would eat the mythical roll

# ---------------------------------------------------------------- item pools
# Materials, cheapest first -- low slots are the most frequent.
#
# These were vanilla metals, leather and hearts until 2026-09-12. A level-240
# monster paying out Chromium (595z, a Junon-tier ore) read as filler, and the 25
# Karkia materials imported in stage 8 had no source at all: twelve were buyable
# from Astraea and the other thirteen were reachable by nothing in the game. The
# shop script called those thirteen "reserved for drops or a craft"; this is the
# drops half of that promise.
#
# **Two Hearts stay** (Green, Blue): the only vanilla material that reads as a
# graveyard drop. Black Hearts were the catacombs' placeholder toll; since
# 2026-11-13 the keepers ask a **Key of Remorse** (150, catacomb_toll.py: Jrose's,
# imported for this), which takes the Black Hearts' slot here -- a player clears
# the cemetery above to pay for the crypt below -- and Magic Stone Crystal's in the
# Spire (the Cemetery and the crypt still pay that one).
KEY_OF_REMORSE = 150
MAT_CEMETERY = [742, 743, 744, 751, KEY_OF_REMORSE, 152, 760, 153]
MAT_SPIRE = [745, 746, 763, KEY_OF_REMORSE, 747, 748, 752, 758]
# 2026-11-19: Karkia paid 25 distinct materials, all of them "metal" (material-classes-200.py
# gave them real classes). Every Karkia pool now ends with the top vanilla essence, powder
# and rare metal -- Enthiric, Hermes (q68, dropped nowhere before), the Lisents up to (Ra)
# q74 and (U) q78 (nowhere before either), Philosopher Stone, Mercury -- appended, so the
# slots the Key of Remorse and the signature materials sit in do not move.
KARKIA_TOP = {"cemetery": [69, 87, 19], "spire": [70, 89, 20], "crypt": [70, 90, 89],
              "memories": [69, 88, 19], "garden": [70, 88, 20]}
MAT_CEMETERY = MAT_CEMETERY + KARKIA_TOP["cemetery"]
MAT_SPIRE = MAT_SPIRE + KARKIA_TOP["spire"]

# Two boss pools rather than one, because several descriptions name the creature
# they come from and it costs nothing to keep that true:
#   761 "Seven of these will break the seal"        Nagia's chain, from the
#   759 "Nagia forges the key ... from ten of them" god's own jailers
#   762 "Prised from the ash-grey wyrm"             so: the Drakes, not anything else
MAT_BOSS = [759, 761]
# The catacombs (2026-11-10): Green and Blue Hearts first -- the toll (the Key of
# Remorse) drops outside, so the crypt does not pay its own way in --
# then the Spire's upper materials.
MAT_CRYPT = [152, 153, 763, 760, 747, 748, 752, 758] + KARKIA_TOP["crypt"]
MAT_DRAKE = [762, 758]

# The flashback is Karkia *alive*: bees, plants and beasts in a world the goddess
# still tends. It draws the clean half of the catalogue -- colour cores, star
# scales, starlight -- where the present day pays in plague crystals and
# blackened cogs, so the two eras still read differently in your bag.
MAT_MEMORIES = [753, 754, 755, 756, 757, 745, 763, 741] + KARKIA_TOP["memories"]
MAT_GARDEN = [755, 756, 757, 741, 749, 750, 764, 758] + KARKIA_TOP["garden"]

# 740 Graphistone is deliberately absent. Its description says it is "found only
# in the Tower of Despair", and the Tower has no population yet -- dropping it in
# the Cemetery would make the item lie about itself. It stays unobtainable until
# there is something in zone 136 to take it from.

USE_FIELD = [13, 32]                                      # Vital / Spiritual (XL)
USE_BOSS = [35, 36]                                       # Health / Mana Bottle (XL)

# Off-hand bucket (type 9). Until 2026-10-01 these were seven "shield" lists of
# the Jrose imports; repackage-subweapons.py then made most of those rows
# Support Tools (262, casters) and Charms (263, DEX or STR trinkets), leaving
# one real shield per tier: Garm 309 (210), Golden Angel 306 (220), Ancient
# Davion 307 (230), Righteous Shield NEO 330 (240). A bucket is a five-slot
# redirect window, so each tier's window is shield + tool + charms, with the
# shield listed twice where there is room so its own rate stays near the old
# 1-in-30. Read as [shield, tool, light charm, melee charm, ...].
OFF_210 = [309, 308, 310]                                 # Garm, Freyja, Fafnir's Scale
OFF_215 = [321, 320, 322]                                 # Steam Clock silver/gold/black
OFF_220 = [306, 311, 312, 313]                            # Golden Angel, Cathedral, Ashura, Reconquista
OFF_225 = [331]                                           # Lord Knight Crest
OFF_230 = [307, 314, 315, 316]                            # Davion, Ushumgal, Zlatorog, Lindwurm Talon
OFF_235 = [328]                                           # Lord of Riot
OFF_240 = [330, 317, 318, 319]                            # NEO, Evolved Ushumgal/Zlatorog/Lindwurm

OFF_CEMETERY = OFF_210 + [OFF_215[0], OFF_210[0]]         # 900 / zone 87
OFF_OFFICER = [OFF_215[2]] + [306, 311, 313] + OFF_225    # 901
OFF_DSEED = OFF_210 + [312, OFF_215[1]]                   # 902
OFF_SPIRE = OFF_230 + [OFF_240[0]]                        # 906 / zone 88
OFF_MEMORIES = OFF_230 + [OFF_230[0]]                     # 909
OFF_QUEEN = OFF_240 + [OFF_240[0]]                        # 910
OFF_GARDEN = OFF_240 + OFF_235                            # 911
OFF_CRYPT = OFF_235 + OFF_230[1:] + [OFF_240[0]]             # 912 / zones 90, 91
OFF_BOSS = OFF_240[:2]                                    # boss common slots: NEO + Evolved Ushumgal

# Jrose weapons by tier (type 8) -- all above id 999, so all wide-encoded
WPN_220 = [1426, 1427, 1428, 1429, 1430, 1431, 1432, 1433, 1434, 1435, 1436]
# lv235 since 2026-10-25: 668's dinosaur set at the retail ids, one per type in type
# order (replace-235-weapons.py; the Jrose Jotunn line 1437-1446 is erased). Thirteen
# do not split into the old 5 + 5, so each table takes a five-wide window (wrapping);
# 906 still carries two, and every weapon drops from at least one Karkia table.
WPN_235 = [908, 915, 926, 939, 932, 947, 959, 966, 974, 982, 990, 999, 950]
WPN_240 = [1383, 1386, 1389, 1392, 1395, 1398, 1401, 1404, 1407, 1410, 1413,
           1416, 1419]
# The plain lv230 line (the Arcidian look since weapon-ladder-pass.py), one per type:
# what the crypt's monsters pay, a tier under their bosses.
WPN_230 = [1368, 1369, 1370, 1371, 1372, 1373, 1374, 1424, 1375, 1376, 1377,
           1378, 1379]

# The black blood-stained Jrose set (lv230, red names): the catacomb bosses' loot, its
# only source (the user, 2026-11-09: "red level weapon, it can be tailored later").
WPN_RED = list(range(1456, 1469))

# The catacombs' five bosses, one class line of the top tier each (2026-11-13).
# table -> (label, lv240 mythicals, the red weapons of the same types, the gear group).
# The gear group is the line's Norse set (LIST_BODY/CAP/ARMS/FOOT row) or, for
# Ferdinando, the lv240 off-hands; the Necromancer's has room for the casters' tool.
# Ferdinando drops the red katana he fights with (1462). LIST_NPC's drop cells for
# 2557-2560 are also in catacomb_pool.py (DROP_TABLE), which rebuilds those rows.
NORSE_SOLDIER, NORSE_MUSE, NORSE_HAWKER, NORSE_DEALER = 270, 271, 272, 273


def norse(row):
    return [(t, row) for t in (T_BODY, T_CAP, T_ARMS, T_FOOT)]


CRYPT_BOSSES = {
    913: ("Crypt Necromancer: staff, wand, Eir (Muse)",
          [1407, 1410], [1463, 1459], norse(NORSE_MUSE) + [(T_SUBWPN, 317)]),
    914: ("Ferdinando: 1H sword, 1H blunt, off-hands",
          [1383, 1386], [1462, 1467], [(T_SUBWPN, s) for s in (330, 318, 319)]),
    915: ("Argentia: gun, launcher, crossbow, Forseti (Dealer)",
          [1401, 1404, 1419], [1464, 1456, 1468], norse(NORSE_DEALER)),
    916: ("Gladiomurus: 2H sword, spear, 2H axe, Fenrir (Soldier)",
          [1389, 1392, 1395], [1461, 1457, 1465], norse(NORSE_SOLDIER)),
    917: ("Ikaness Greater Assassin: katar, dual, bow, Sleipnir (Hawker)",
          [1413, 1416, 1398], [1458, 1466, 1460], norse(NORSE_HAWKER)),
}

# Jrose armour sets (armour-endgame-sets.py, 2026-10-20), drop-only by the user's
# call: the OG cinematic outfits (lv230, rows 266-269) off the lv228-240 field tables,
# the Norse set (lv240, rows 270-273) off bosses only "for now". Ordered body, cap,
# gloves, boots with the four classes inside each, so any 4-5 wide window mixes classes.
def armour_pool(first):
    return [(t, first + c) for t in (T_BODY, T_CAP, T_ARMS, T_FOOT) for c in range(4)]


ARM_OG = armour_pool(266)
ARM_NORSE = armour_pool(270)


def windows(pool, start, sizes):
    """Consecutive windows of `pool` (wrapping), one per size, from `start` on."""
    out, i = [], start
    for n in sizes:
        out.append([pool[(i + k) % len(pool)] for k in range(n)])
        i += n
    return out

# ---------------------------------------------------------------- the tables
# Reshaped 2026-11-19 (roadmap 1.2; the user's decisions: field gear down to the 1-200
# rate, materials up, the pools re-tiered to the re-levelled bands, top jewellery and
# gems added, bosses ~90% gear, map rows rebuilt as real map tables). The numbers below
# are for the double drop (roadmap 1.3): a kill rolls its own table (no zone fallback)
# and then its zone's row; `economy-audit.py drops --roll monster|map` measures each.
#
# A roll at level parity with col 20 = 80 reads slot k (0-based) with these odds:
#   slots 0-4 6.0% each, 5 5.6, 6 5.1, 7 4.7, 8 4.4, 9 4.1, 10 3.9, 11 3.6, 12 3.4,
#   13 3.2, 14 3.0, 15 2.8, 16 2.7, 17 2.5, 18 2.4, 19 2.25, 20 2.1, 21 2.0, 22 1.9,
#   23 1.7, 24 1.65, 25 1.5, 26 1.4, 27 1.3, 28 1.2, 29 1.2   (all 30 = 100%)
# so a shape is a choice of slots. An empty slot pays nothing; a redirect pays its group
# (five cells, one in five each; group g is slots 25 + 5g .. 29 + 5g = columns 26 + 5g .. 30 + 5g
# since the 2026-11-19 base fix -- with the old base 26, group 4's fifth cell was column 51).

T_JEWEL, T_GEM = 7, 11

# Field: 11 material slots (58% of rolls), two consumables (7%), a weapon bucket and an
# off-hand/armour bucket (Karkia slots 16 + 19 = 4.9%, Oro 20 + 26 = 3.5%), a jewellery
# bucket (23: 1.7%) and a gem bucket (28: 1.2%). With 15% money first, per kill:
# materials ~49%, consumables ~6%, gear ~4.2% (Karkia) / ~3.0% (Oro), jewellery ~1.5%,
# gems ~0.8%, nothing ~21% -- the 1-200 monster roll's shape (doc/project-drops.md s7).
SLOT_MATS = list(range(11))
SLOT_USES = [11, 12]
SLOT_GEAR = {"karkia": (16, 19), "oro": (20, 26)}
SLOT_JEWEL, SLOT_GEM = 23, 28
# Boss (~90% gear, the user's call: "i don't wanna frustrate the players"): six top-tier
# slots (35.7%, the mythical rate it always had), two armour buckets (9.8%), fifteen
# slots of the tier below (44.3%), then materials, a bottle, jewellery and a gem.
BOSS_TOP = list(range(6))
BOSS_ARMOUR = [6, 7]
BOSS_GEAR = list(range(8, 23))
BOSS_MATS = [23, 24, 25, 26]
BOSS_USE, BOSS_JEWEL, BOSS_GEM = 27, 28, 29
# Elite (a gold star, "pay better than the rest but not up to boss level", the user):
# three gear slots (18.1%) and two gear buckets (12.0%), eleven materials, two
# consumables, jewellery and gems. With 15% money: gear ~26% a kill, materials ~33%.
ELITE_GEAR = [0, 1, 2]
ELITE_BUCKETS = [3, 4]
ELITE_MATS = list(range(5, 16))
ELITE_USES = [16, 17]
ELITE_JEWEL, ELITE_GEM = 18, 19
# Map (the zone row): 13 material slots (65%) and four consumables (12%), never gear --
# what every retail zone row 1-62 is.
MAP_MATS = list(range(13))
MAP_USES = [13, 14, 15, 16]

# Weapon tiers, one per type in the same type order (211 212 221 222 223 231 232 233 241
# 242 251 252 271) so a tier's index i is the same weapon type in every list.
WPN_210 = [27, 58, 128, 187, 154, 229, 259, 288, 326, 358, 426, 452, 80]     # tsuki's 220 set
WPN_215 = [1381, 1384, 1387, 1390, 1393, 1396, 1399, 1402, 1405, 1408, 1411, 1414, 1417]
WPN_220_ALL = [1426, 1427, 1428, 1429, 1430, 1431, 1432, 1433, 1434, 1435, 424, 125, 1436,
               126, 425]                    # + tsuki's second dual / katar (no source until now)
WPN_225 = [28, 59, 129, 188, 155, 254, 260, 289, 327, 359, 451, 453, 81]     # Cursed Vermillion
ASMODEUS = 124                              # tsuki's 2H at 235, no source until now
WPN_235_POOL = WPN_235 + [ASMODEUS]

# Jewellery is graded by quality, not level, and nothing of quality 50+ dropped anywhere.
# Oro: Platinum (q50) and the named q50-54 pieces; Karkia: Diamond (q60) and the q56-74
# earrings. Rings, earrings and necklaces interleave so any window mixes them.
JEWEL_ORO = [281, 307, 333, 286, 312, 338, 291, 317, 343, 296, 322, 348, 301, 327, 353,
             359, 364, 369, 28, 178, 108, 29, 179, 109, 30, 180, 110]
JEWEL_KARKIA = [282, 308, 334, 287, 313, 339, 292, 318, 344, 297, 323, 349, 302, 328, 354,
                360, 365, 370, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190]
# Gems by tier: [5] q70, [6] q80, [7] q90 (Garnet .. Pink Opal). Oro [5]-[6], Karkia [6]-[7].
GEM_5 = [305, 315, 325, 335, 345, 355, 365, 375]
GEM_6 = [306, 316, 326, 336, 346, 356, 366, 376]
GEM_7 = [307, 317, 327, 337, 347, 357, 367, 377]
GEM_ORO = GEM_5 + GEM_6
GEM_KARKIA = GEM_6 + GEM_7

# --- what the Artisan crafts (artisan-crafts-200.py, roadmap 1.4, 2026-11-19) ----------
# The user: tiers 225 and 230, "some are dropped, some are craftable, some are both", the
# same for armour; Jewel Item Craft gets something to make; a few craft-only backs. The
# split lives here because every drop pool is built here: craft-only items leave the drop
# tables (the *_DROP pools), and artisan-crafts-200.py writes the recipes from the same sets.
# Weapon picks are indices into the type-ordered tier lists (WPN_225 / WPN_230), chosen so
# every type has a craftable version in one tier or the other.
#   0 1H sword  1 blunt  2 2H sword  3 spear  4 2H axe  5 bow  6 gun  7 launcher
#   8 staff  9 wand  10 katar  11 dual swords  12 crossbow
CRAFT_225 = {"craft": [2, 5, 8, 10], "both": [0, 6, 11, 12]}     # drop-only: 1 3 4 7 9
CRAFT_230 = {"craft": [1, 3, 7, 9], "both": [4, 5, 8, 10]}       # drop-only: 0 2 6 11 12
CRAFT_ONLY_WEAPONS = ({WPN_225[i] for i in CRAFT_225["craft"]}
                      | {WPN_230[i] for i in CRAFT_230["craft"]})
WPN_225_DROP = [w for w in WPN_225 if w not in CRAFT_ONLY_WEAPONS]
WPN_230_DROP = [w for w in WPN_230 if w not in CRAFT_ONLY_WEAPONS]
# Armour: one piece type of each set is craft-only, one is both, the rest drop only.
# Egyptian (lv225, add-oro-drops.EGYPT): gauntlets craft-only, boots both.
# OG (lv230, ARM_OG): boots craft-only, caps both.
CRAFT_ARMOUR = {"egypt": {"craft": T_ARMS, "both": T_FOOT},
                "og": {"craft": T_FOOT, "both": T_CAP}}
ARM_OG_DROP = [(t, n) for t, n in ARM_OG if t != CRAFT_ARMOUR["og"]["craft"]]
# Jewellery: Platinum (q50) both; Diamond (q60) rings and earrings both, necklaces craft-only.
PLATINUM = [281, 286, 291, 296, 301, 359, 307, 312, 317, 322, 327, 364,
            333, 338, 343, 348, 353, 369]
DIAMOND = [282, 287, 292, 297, 302, 360, 308, 313, 318, 323, 328, 365,
           334, 339, 344, 349, 354, 370]
JEWEL_CRAFT_ONLY = {334, 339, 344, 349, 354, 370}                # the Diamond necklaces
JEWEL_KARKIA_DROP = [j for j in JEWEL_KARKIA if j not in JEWEL_CRAFT_ONLY]
# The Artisan's exclusives: four Jrose backs that dropped nowhere (LIST_BACK).
CRAFT_BACKS = {1079: "Antares Ward", 1080: "Noble Sacred Cross",
               1081: "Michael's Half-Wing", 1082: "Chieftain's Heirloom Blade"}


def cycle(pool, n):
    return [pool[i % len(pool)] for i in range(n)]


def W(ids):
    return [(T_WEAPON, n) for n in ids]


def S(ids):
    return [(T_SUBWPN, n) for n in ids]


def J(ids):
    return [(T_JEWEL, n) for n in ids]


def G(ids):
    return [(T_GEM, n) for n in ids]


def win(pool, start, n):
    return windows(pool, start, [n])[0]


def twin_235(mythical):
    """The lv235 weapon of a lv240 mythical's type."""
    return WPN_235[WPN_240.index(mythical)]


def _bucket(items, n=REDIRECT_WIDTH):
    """A redirect re-rolls rand(5) over its window and a blank cell is a dead roll:
    short lists repeat until the cells are full."""
    return cycle(list(items), n)


def field_table(mats, uses, weapons, offarm, jewels, gems, gear="karkia"):
    """mats/uses: item numbers, cheapest first (repeats favour the first); weapons and
    offarm: (type, no) lists for the two gear buckets; jewels/gems: windows."""
    common = [None] * COMMON_SLOTS
    for s, m in zip(SLOT_MATS, cycle(mats, len(SLOT_MATS))):
        common[s] = (T_NATURAL, m)
    for s, u in zip(SLOT_USES, cycle(uses, len(SLOT_USES))):
        common[s] = (T_USE, u)
    w, o = SLOT_GEAR[gear]
    groups = {}
    for slot, g, items, n in ((w, 1, weapons, 5), (o, 2, offarm, 5), (SLOT_JEWEL, 3, jewels, 5),
                              (SLOT_GEM, 4, gems, 5)):
        if items:                                  # an empty bucket would be a dead roll
            common[slot], groups[g] = ("redirect", g), _bucket(items, n)
    return common, groups


def elite_table(gear, weapons, offarm, mats, uses, jewels, gems):
    """gear: (type, no) for the three direct slots; weapons/offarm: the two buckets."""
    common = [None] * COMMON_SLOTS
    groups = {}
    for s, e in zip(ELITE_GEAR, cycle(gear, len(ELITE_GEAR))):
        common[s] = e
    for slot, g, items, n in ((ELITE_BUCKETS[0], 1, weapons, 5), (ELITE_BUCKETS[1], 2, offarm, 5),
                              (ELITE_JEWEL, 3, jewels, 5), (ELITE_GEM, 4, gems, 5)):
        if items:
            common[slot], groups[g] = ("redirect", g), _bucket(items, n)
    for s, m in zip(ELITE_MATS, cycle(mats, len(ELITE_MATS))):
        common[s] = (T_NATURAL, m)
    for s, u in zip(ELITE_USES, cycle(uses, len(ELITE_USES))):
        common[s] = (T_USE, u)
    return common, groups


def boss_table(top, armour, gear, mats, use, jewels, gems):
    """top: (type, no) for the six top slots; armour: up to two bucket windows (empty:
    the armour slots take gear too); gear: the tier below, fifteen or more."""
    common = [None] * COMMON_SLOTS
    groups = {}
    for s, e in zip(BOSS_TOP, cycle(top, len(BOSS_TOP))):
        common[s] = e
    gear = list(gear)
    for k, s in enumerate(BOSS_ARMOUR):
        if armour:
            g = k % len(armour) + 1
            groups[g] = _bucket(armour[g - 1])
            common[s] = ("redirect", g)
        else:
            common[s] = gear.pop()
    for s, e in zip(BOSS_GEAR, cycle(gear, len(BOSS_GEAR))):
        common[s] = e
    for s, m in zip(BOSS_MATS, cycle(mats, len(BOSS_MATS))):
        common[s] = (T_NATURAL, m)
    common[BOSS_USE] = (T_USE, use)
    common[BOSS_JEWEL], common[BOSS_GEM] = ("redirect", 3), ("redirect", 4)
    groups[3], groups[4] = _bucket(jewels), _bucket(gems)
    return common, groups


def map_table(mats, uses):
    common = [None] * COMMON_SLOTS
    for s, m in zip(MAP_MATS, cycle(mats, len(MAP_MATS))):
        common[s] = (T_NATURAL, m)
    for s, u in zip(MAP_USES, cycle(uses, len(MAP_USES))):
        common[s] = (T_USE, u)
    return common, {}


def map_from(shapes, uses=None):
    """A zone row's map table from the materials and consumables of the zone's own
    tables, most frequent first (the importers' zone rows: Shibuya, Ulverick)."""
    mats, cons = collections.Counter(), collections.Counter()
    for common, groups in shapes:
        for e in list(common) + [x for g in groups.values() for x in g]:
            if e and e[0] == T_NATURAL:
                mats[e[1]] += 1
            elif e and e[0] == T_USE:
                cons[e[1]] += 1
    return map_table([m for m, _ in mats.most_common()],
                     [u for u, _ in cons.most_common()] or uses or USE_FIELD)


def no_key(pool):
    """The Key of Remorse stays a monster drop (about 1 kill in 20): out of map rows."""
    return [m for m in pool if m != KEY_OF_REMORSE]


def karkia_field(mats, w230, w235, offhands, og, jewel_at, gem_at):
    """Cemetery-band tables mix the lv230 and lv235 weapon lines; Spire-band ones are lv235."""
    weapons = W(win(WPN_230_DROP, w230, 3) + win(WPN_235_POOL, w235, 2)) if w230 is not None \
        else W(win(WPN_235_POOL, w235, 5))
    return field_table(mats, USE_FIELD, weapons, S(offhands) + win(ARM_OG_DROP, og, 3),
                       J(win(JEWEL_KARKIA_DROP, jewel_at, 5)), G(win(GEM_KARKIA, gem_at, 4)))


def karkia_boss(mythicals, norse_at, mats=None, at=0):
    """A Karkia boss: six of its eight mythicals on top, the Norse set in the two armour
    buckets, and below them the lv235 twin of every mythical it carries, the lv240
    off-hands, Lord of Riot and its last two mythicals."""
    lower = W([twin_235(m) for m in mythicals]) + S(OFF_240) + S(OFF_235) + W(mythicals[6:8])
    return boss_table(W(mythicals[:6]), windows(ARM_NORSE, norse_at, [5, 5]), lower,
                      mats or MAT_BOSS, USE_BOSS[0], J(win(JEWEL_KARKIA_DROP, at, 5)),
                      G(win(GEM_KARKIA, at, 4)))


def crypt_boss(mythicals, reds, gear, at):
    """A catacomb boss: its mythicals on top, its red weapons and its gear group (the
    line's Norse set, or Ferdinando's lv240 off-hands) in the two armour buckets, and
    below them its types' lv235 weapons, its gear and its red weapons again."""
    twins = [twin_235(m) for m in mythicals]
    lower = W(cycle(twins, 6)) + cycle(list(gear), 5) + W(cycle(reds, 4))
    return boss_table(W(mythicals), [W(reds), list(gear)], lower, [758, 752], USE_BOSS[0],
                      J(win(JEWEL_KARKIA_DROP, at, 5)), G(win(GEM_KARKIA, at, 4)))


TABLES = {
    # --- Cemetery band (lv230-235 since relevel-200-band.py): lv230 + lv235 weapons ----
    900: ("Cemetery trash (17 mobs, lv230-235)",
          karkia_field(MAT_CEMETERY, 0, 0, [OFF_230[0], OFF_235[0]], 0, 0, 0)),
    901: ("Revived Quarantine Officer (lv233)",
          karkia_field(MAT_CEMETERY, 3, 2, [OFF_230[1], OFF_235[0]], 3, 5, 4)),
    902: ("D-Seed (lv230)",
          karkia_field(MAT_CEMETERY, 6, 4, [OFF_230[2], OFF_230[0]], 6, 10, 8)),
    # --- Spire band (lv234-240): lv235 weapons, the OG set (lv230) ---------------------
    906: ("Alpha roster (10 mobs, lv234-239)",
          karkia_field(MAT_SPIRE, None, 0, [OFF_240[0], OFF_235[0]], 9, 15, 12)),
    909: ("Memories wildlife (lv235-236)",
          karkia_field(MAT_MEMORIES, None, 5, [OFF_235[0], OFF_240[1]], 12, 20, 0)),
    911: ("Garden wildlife (lv238-240)",
          karkia_field(MAT_GARDEN, None, 10, [OFF_240[0], OFF_240[2]], 2, 25, 4)),
    # The catacombs' six monsters: lv235 (was the lv230 line, the 2026-11-13 interim)
    912: ("Catacombs (6 mobs, lv235-240)",
          karkia_field(MAT_CRYPT, None, 8, [OFF_240[0], OFF_235[0]], 5, 2, 8)),
    # --- bosses: ~90% gear ----------------------------------------------------------
    903: ("BOSS Hebarn Officer Pazugenti (lv240)",
          karkia_boss([1389, 1398, 1404, 1407, 1419, 1395, 1392, 1386], 0, at=0)),
    904: ("BOSS Hebarn Officer Scylla Mira (lv240)",
          karkia_boss([1383, 1386, 1392, 1401, 1410, 1413, 1416, 1389], 3, at=5)),
    905: ("BOSS Corroded Golem + Revived Veteran (lv239/238)",
          karkia_boss([1395, 1404, 1416, 1413, 1401, 1410, 1383, 1398], 6, at=10)),
    907: ("BOSS Deadly Drake (lv239, Cemetery)",
          karkia_boss([1389, 1392, 1395, 1398, 1401, 1404, 1407, 1410], 9, MAT_DRAKE, at=15)),
    908: ("BOSS Deadly Drake Alpha (lv240, Spire Village)",
          karkia_boss([1383, 1386, 1413, 1416, 1419, 1389, 1398, 1404], 12, MAT_DRAKE, at=20)),
    # The queen behind the Melitta swarm (king icon, lv239): lv235 weapons on top, the OG
    # set in her buckets, the lv240 off-hands and lv230 weapons below.
    910: ("BOSS Basilissa Melitta (lv239)",
          boss_table(W(win(WPN_235_POOL, 2, 6)), windows(ARM_OG_DROP, 4, [5, 5]),
                     S(OFF_240) + S(OFF_235) + W(win(WPN_230_DROP, 0, 10)),
                     MAT_GARDEN, USE_BOSS[0], J(win(JEWEL_KARKIA_DROP, 25, 5)),
                     G(win(GEM_KARKIA, 12, 4)))),
    # The Cemetery's king (Neg Golem, lv235) paid the trash table 900 until 2026-11-19:
    # a king gets a boss table. lv235 weapons on top, the OG set in its buckets.
    918: ("BOSS Neg Golem (lv235, Cemetery king)",
          boss_table(W(win(WPN_235_POOL, 8, 6)), windows(ARM_OG_DROP, 8, [5, 5]),
                     S(OFF_235 + OFF_230) + W(win(WPN_230_DROP, 4, 10)),
                     MAT_CEMETERY[:4], USE_BOSS[0], J(win(JEWEL_KARKIA_DROP, 10, 5)),
                     G(win(GEM_KARKIA, 8, 4)))),
    # The Cemetery's two gold stars (sub-bosses, two-player content) shared the trash
    # table 900 until 2026-11-19: elite tables, the Cemetery band's gear a quarter of
    # the time.
    919: ("ELITE Dark Tower (lv233, gold star)",
          elite_table(W(win(WPN_235_POOL, 4, 2)) + win(ARM_OG_DROP, 1, 1),
                      W(win(WPN_230_DROP, 1, 3) + win(WPN_235_POOL, 6, 2)),
                      S([OFF_230[0], OFF_235[0]]) + win(ARM_OG_DROP, 2, 3),
                      MAT_CEMETERY, USE_FIELD, J(win(JEWEL_KARKIA_DROP, 3, 5)), G(win(GEM_KARKIA, 2, 4)))),
    920: ("ELITE Element Battler (lv233, gold star)",
          elite_table(W(win(WPN_235_POOL, 9, 2)) + win(ARM_OG_DROP, 7, 1),
                      W(win(WPN_230_DROP, 7, 3) + win(WPN_235_POOL, 11, 2)),
                      S([OFF_230[1], OFF_235[0]]) + win(ARM_OG_DROP, 10, 3),
                      MAT_CEMETERY, USE_FIELD, J(win(JEWEL_KARKIA_DROP, 13, 5)), G(win(GEM_KARKIA, 6, 4)))),
    # The catacombs' five bosses (CRYPT_BOSSES)
    **{row: ("BOSS " + label, crypt_boss(myth, reds, gear, 3 * k))
       for k, (row, (label, myth, reds, gear)) in enumerate(sorted(CRYPT_BOSSES.items()))},
}

# The zone rows: real map tables since 2026-11-19 (they mirrored a monster table, gear
# included, so the 20% fallback was not a dead roll). The Burned Forest is present-day
# Karkia beside the Cemetery and shares its row's shape.
ZONE_TABLES = {
    87: ("Cemetery map", map_table(no_key(MAT_CEMETERY), USE_FIELD)),
    131: ("Burned Forest map", map_table(no_key(MAT_CEMETERY), USE_FIELD)),
    88: ("Spire Village map", map_table(no_key(MAT_SPIRE), USE_FIELD)),
    90: ("Catacombs of Reminiscence map", map_table(MAT_CRYPT, USE_FIELD)),
    91: ("Catacombs of Sorrow map", map_table(MAT_CRYPT, USE_FIELD)),
    133: ("Memories of Karkia map", map_table(MAT_MEMORIES, USE_FIELD)),
    144: ("Garden of Karkia map", map_table(MAT_GARDEN, USE_FIELD)),
}

# npc row -> new drop table. Both Drakes were sharing a trash table, and the
# seven flashback monsters arrived pointing at Jrose's table 831, which is empty
# on our side.
NPC_TABLE_FIX = {2729: 907, 2699: 908, 2722: 918, 2720: 919, 2721: 920,
                 2557: 914, 2558: 915, 2559: 916, 2560: 917,   # the crypt's boss pool
                 2527: 909, 2528: 909, 2529: 909,   # Memories wildlife
                 2530: 910,                          # the queen
                 2539: 911, 2547: 911, 2549: 911}    # Garden wildlife

# The flashback monsters also import with NPC_DROP_ITEM of 10 or 30 against the
# 80 the rest of Karkia uses. At 10 the drop roll is usually <= 0 outright, and
# the table-vs-zone split inverts: 90% of what does roll would come off the zone
# fallback rather than the monster's own table.
DROP_ITEM_FIX = {2527: 80, 2528: 80, 2529: 80, 2530: 80,
                 2539: 80, 2547: 80, 2549: 80}

BOSS_ROWS = {2685, 2686, 2687, 2688, 2699, 2729, 2530, 2722}   # 2530 the queen: a boss table since 2026-11-19
KARKIA_NPC_RANGE = list(range(2685, 2732)) + [2527, 2528, 2529, 2530,
                                              2539, 2547, 2549]


def load(name):
    spec = importlib.util.spec_from_file_location(
        name.replace("-", "_"), os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [name]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = saved
    return mod


def encode(item_type, item_no):
    """Rose::Drop::encode_drop_item."""
    if not 0 < item_no <= MAX_ITEM_NO:
        raise SystemExit(f"item no {item_no} outside 1..{MAX_ITEM_NO}")
    packed = (item_type * 1000 + item_no if item_no <= LEGACY_MAX_NO
              else item_type * WIDE_BASE + item_no)
    if packed <= MAX_SENTINEL:
        raise SystemExit(f"{item_type}:{item_no} packs to {packed}, a sentinel")
    return packed


def build_row(common, groups):
    """slot -> packed value, for one ITEM_DROP row."""
    cells = {}
    for slot, entry in enumerate(common):
        if slot >= COMMON_SLOTS:
            raise SystemExit(f"common section overflows slot {COMMON_SLOTS - 1}")
        if entry is None:
            continue
        if entry[0] == "redirect":
            cells[slot] = entry[1]
        else:
            cells[slot] = encode(*entry)
    for group, items in groups.items():
        base = REDIRECT_BASE_COL + group * REDIRECT_WIDTH
        if len(items) > REDIRECT_WIDTH:
            raise SystemExit(f"group {group}: {len(items)} items, max {REDIRECT_WIDTH}")
        for i, entry in enumerate(items):
            slot = base + i
            if slot > LAST_SLOT:
                raise SystemExit(f"group {group} entry {i} lands on dead slot {slot}")
            cells[slot] = encode(*entry)
    reachable = {e[1] for e in common if e and e[0] == "redirect"}
    for group in groups:
        if group not in reachable:
            raise SystemExit(f"group {group} has no redirect pointing at it")
    return cells


def plan():
    rows = {}
    for row, (label, (common, groups)) in TABLES.items():
        rows[row] = (label, build_row(common, groups))
    for zone, (label, (common, groups)) in ZONE_TABLES.items():
        rows[zone] = (f"zone {zone}: {label}", build_row(common, groups))
    return rows


def npc_cells_before(npc):
    """{row: {col: value}} for every LIST_NPC cell this script may write."""
    rows = set(NPC_TABLE_FIX) | set(DROP_ITEM_FIX) | set(KARKIA_NPC_RANGE)
    out = {}
    for nid in sorted(rows):
        if nid >= npc.rows:
            continue
        out[str(nid)] = {str(c): npc.get(nid, c).decode("latin-1")
                         for c in (COL_DROP_TYPE, COL_DROP_MONEY, COL_DROP_ITEM)}
    return out


def apply(oro, dry, rewrite=False):
    drop = oro.Stb(DROP_STB)
    npc = oro.Stb(NPC_STB)
    npc_before = npc_cells_before(npc)
    report = []
    rows = plan()

    for row in sorted(rows):
        label, cells = rows[row]
        if row >= drop.rows:
            raise SystemExit(f"ITEM_DROP has no row {row}")
        occupied = [s for s in range(LAST_SLOT + 1) if drop.get(row, 1 + s).strip()]
        if occupied and not rewrite and not all(
                int(drop.get(row, 1 + s) or 0) == cells.get(s, 0) for s in occupied):
            raise SystemExit(
                f"ITEM_DROP row {row} already has content in slots {occupied[:6]} "
                "-- refusing to overwrite a table we did not author "
                "(--rewrite if the TABLES above changed and these rows are ours)")
        for s in range(LAST_SLOT + 1):
            v = cells.get(s, 0)
            drop.set(row, 1 + s, str(v).encode("latin-1") if v else b"")
        common = sum(1 for s in cells if s < COMMON_SLOTS)
        rare = sum(1 for s in cells if s >= COMMON_SLOTS)
        report.append(f"  row {row:<4} {label:<46} {common:>2} common, {rare:>2} rare")

    for nid, tbl in NPC_TABLE_FIX.items():
        was = npc.get(nid, COL_DROP_TYPE).decode("latin-1")
        npc.set(nid, COL_DROP_TYPE, str(tbl).encode("latin-1"))
        report.append(f"  npc {nid} drop table {was} -> {tbl}")

    fixed = 0
    for nid, want in DROP_ITEM_FIX.items():
        if int(npc.get(nid, COL_DROP_ITEM) or 0) != want:
            npc.set(nid, COL_DROP_ITEM, str(want).encode("latin-1"))
            fixed += 1
    if fixed:
        report.append(f"  drop-item rate raised to 80 on {fixed} flashback rows")

    money = 0
    for nid in KARKIA_NPC_RANGE:
        if nid >= npc.rows or not npc.get(nid, 0).strip():
            continue
        if not npc.get(nid, COL_DROP_TYPE).strip() or \
                int(npc.get(nid, COL_DROP_TYPE) or 0) == 0:
            continue                       # Ghost Seeds never drop; leave them
        want = MONEY_BOSS if nid in BOSS_ROWS else MONEY_FIELD
        if int(npc.get(nid, COL_DROP_MONEY) or 0) != want:
            npc.set(nid, COL_DROP_MONEY, str(want).encode("latin-1"))
            money += 1
    report.append(f"  money: {money} rows set "
                  f"(field {MONEY_FIELD}%, bosses {MONEY_BOSS}%)")

    if dry:
        return report, None, npc_before
    return report, (drop.to_bytes(), npc.to_bytes()), npc_before


def verify(oro):
    drop = oro.Stb(DROP_STB)
    npc = oro.Stb(NPC_STB)
    bad = []
    for row, (label, cells) in plan().items():
        for s in range(LAST_SLOT + 1):
            got = int(drop.get(row, 1 + s) or 0)
            if got != cells.get(s, 0):
                bad.append(f"row {row} slot {s}: {got} != {cells.get(s, 0)}")
                break
    for nid, tbl in NPC_TABLE_FIX.items():
        if int(npc.get(nid, COL_DROP_TYPE) or 0) != tbl:
            bad.append(f"npc {nid}: drop table is "
                       f"{npc.get(nid, COL_DROP_TYPE).decode('latin-1')}")
    for nid, want in DROP_ITEM_FIX.items():
        if int(npc.get(nid, COL_DROP_ITEM) or 0) != want:
            bad.append(f"npc {nid}: drop-item rate is "
                       f"{npc.get(nid, COL_DROP_ITEM).decode('latin-1')}, want {want}")
    for nid in KARKIA_NPC_RANGE:
        if nid >= npc.rows or not npc.get(nid, 0).strip():
            continue
        if int(npc.get(nid, COL_DROP_TYPE) or 0) == 0:
            continue
        want = MONEY_BOSS if nid in BOSS_ROWS else MONEY_FIELD
        if int(npc.get(nid, COL_DROP_MONEY) or 0) != want:
            bad.append(f"npc {nid}: money is "
                       f"{npc.get(nid, COL_DROP_MONEY).decode('latin-1')}, want {want}")
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--rewrite", action="store_true",
                    help="overwrite rows this script authored earlier (after a TABLES change); "
                         "the original whole-file backup is kept")
    args = ap.parse_args()

    import rosedata as oro  # the shared data layer (was import-oro.py)

    if args.restore:
        if not os.path.isdir(BACKUP):
            sys.exit("no backup -- nothing to restore")
        src = os.path.join(BACKUP, os.path.basename(DROP_STB))
        if os.path.isfile(src):
            shutil.copyfile(src, DROP_STB)
        cells = 0
        if os.path.exists(SIDECAR):
            with open(SIDECAR, encoding="utf-8") as fh:
                saved = json.load(fh).get("npc_cells") or {}
            npc = oro.Stb(NPC_STB)
            for nid, cols in saved.items():
                for col, val in cols.items():
                    npc.set(int(nid), int(col), val.encode("latin-1"))
                    cells += 1
            with open(NPC_STB, "wb") as fh:
                fh.write(npc.to_bytes())
            os.remove(SIDECAR)
        print(f"restored ITEM_DROP.STB and {cells} LIST_NPC cell(s) "
              "(cells, not the whole table -- four scripts write that file)")
        return 0

    if args.verify:
        bad = verify(oro)
        print(f"{len(TABLES)} tables + {len(ZONE_TABLES)} zone map tables; "
              f"{len(bad)} problem(s)" + ("\n  " + "\n  ".join(bad[:10]) if bad else ""))
        return 1 if bad else 0

    report, blobs, npc_before = apply(oro, args.dry_run, args.rewrite)
    print("\n".join(report))
    if args.dry_run:
        print("\ndry run: nothing written")
        return 0

    os.makedirs(BACKUP, exist_ok=True)
    # ITEM_DROP.STB is ours alone, so a whole-file backup is safe. LIST_NPC.STB
    # is NOT: import-karkia, rebalance-karkia and add-karkia-shops all write it,
    # so a file copy taken here goes stale the moment any of them runs and
    # restoring it silently reverts their work. This cost the seven flashback
    # monsters once -- a --restore wiped rows written by a later import, and only
    # rebalance-karkia --verify noticed. LIST_NPC is therefore restored cell by
    # cell, from values recorded in the sidecar.
    bak = os.path.join(BACKUP, os.path.basename(DROP_STB))
    if not os.path.exists(bak):
        shutil.copyfile(DROP_STB, bak)
    for path, blob in zip((DROP_STB, NPC_STB), blobs):
        with open(path, "wb") as fh:
            fh.write(blob)
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"tables": sorted(TABLES), "zones": sorted(ZONE_TABLES),
                   "npc_table_fix": NPC_TABLE_FIX,
                   "npc_cells": npc_before}, fh, indent=1)

    bad = verify(oro)
    if bad:
        print("VERIFY FAILED after writing:")
        for b in bad[:10]:
            print("  " + b)
        return 1
    print("\nwritten and verified; restart the servers and re-bake the VFS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
