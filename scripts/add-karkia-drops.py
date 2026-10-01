"""Stage 5b: Karkia's loot tables.

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
is the cap-level farm and has twice the weapon pool (10 lv235 weapons against 5
elsewhere), so weapons run 1 in 16 there. Deliberate -- the hardest monsters in
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
REDIRECT_BASE_COL = 26      # Rose::Drop::kRedirectBaseColumn
REDIRECT_WIDTH = 5
COMMON_SLOTS = 30           # the main roll is RANDOM(30)
LAST_SLOT = 49              # slot 50 reads col 51 on a 51-column table -> dead

T_BACK, T_WEAPON, T_SUBWPN, T_USE, T_NATURAL = 6, 8, 9, 10, 12

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
# **The three Hearts stay.** They are the only vanilla material that reads as a
# graveyard drop, and the catacomb entry design in
# doc/karkia-catacombs-implementation-brief.md uses them as its toll -- a player
# clears the cemetery above to pay for the crypt below. Pulling them out here
# would silently break that before it was built.
MAT_CEMETERY = [742, 743, 744, 751, 151, 152, 760, 153]
MAT_SPIRE = [745, 746, 763, 760, 747, 748, 752, 758]

# Two boss pools rather than one, because several descriptions name the creature
# they come from and it costs nothing to keep that true:
#   761 "Seven of these will break the seal"        Nagia's chain, from the
#   759 "Nagia forges the key ... from ten of them" god's own jailers
#   762 "Prised from the ash-grey wyrm"             so: the Drakes, not anything else
MAT_BOSS = [759, 761]
MAT_DRAKE = [762, 758]

# The flashback is Karkia *alive*: bees, plants and beasts in a world the goddess
# still tends. It draws the clean half of the catalogue -- colour cores, star
# scales, starlight -- where the present day pays in plague crystals and
# blackened cogs, so the two eras still read differently in your bag.
MAT_MEMORIES = [753, 754, 755, 756, 757, 745, 763, 741]
MAT_GARDEN = [755, 756, 757, 741, 749, 750, 764, 758]

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
OFF_BOSS = OFF_240[:2]                                    # boss common slots: NEO + Evolved Ushumgal

# Jrose weapons by tier (type 8) -- all above id 999, so all wide-encoded
WPN_220 = [1426, 1427, 1428, 1429, 1430, 1431, 1432, 1433, 1434, 1435, 1436]
WPN_235 = [1437, 1438, 1439, 1440, 1441, 1442, 1443, 1444, 1445, 1446]
WPN_240 = [1383, 1386, 1389, 1392, 1395, 1398, 1401, 1404, 1407, 1410, 1413,
           1416, 1419]

# ---------------------------------------------------------------- the tables
# common: slots 0.. in order. A ("redirect", group) entry costs one common slot
#         and makes that group reachable.
# groups: group number -> the five items in its window.
def field(mats, uses, shields, weapons, extra=None):
    common = [(T_NATURAL, m) for m in mats[:8]] + [(T_USE, u) for u in uses]
    common = common[:10 if extra is None else 9]
    groups = {1: [(T_SUBWPN, s) for s in shields],
              2: [(T_WEAPON, w) for w in weapons]}
    common += [("redirect", 1), ("redirect", 2)]
    if extra:
        groups[3] = [(T_WEAPON, w) for w in extra]
        common.insert(-2, ("redirect", 3))
    return common, groups


def elite(weapons, shields, mats):
    """Between trash and a boss: the queen behind the Melitta swarm.

    She only ever comes off the *tactics* list, so she is rare by construction
    and can afford to pay well -- four weapon slots (about 20% a kill) rather
    than a boss's eight, then materials and a shield bucket.
    """
    common = [(T_WEAPON, w) for w in weapons[:4]]
    common += [(T_NATURAL, m) for m in mats[:5]]
    common += [(T_USE, USE_BOSS[0])]
    common += [("redirect", 1), ("redirect", 2)]
    groups = {1: [(T_SUBWPN, x) for x in shields],
              2: [(T_WEAPON, w) for w in weapons[:5]]}
    return common, groups


def boss(mythicals, shields, mats=None):
    """Eight mythical slots -> 36% per kill, then materials and a shield.

    `mats` takes two slots rather than the one it used to, so the seal chain
    (759 + 761) can come off Hebarn's officers while the Drakes pay their own
    stone. Twelve of a row's slots are used either way; there is room.
    """
    common = [(T_WEAPON, w) for w in mythicals[:8]]
    common += [(T_SUBWPN, s) for s in shields[:2]]
    common += [(T_NATURAL, m) for m in (mats or MAT_BOSS)[:2]]
    common += [(T_USE, USE_BOSS[0])]
    return common, {}


TABLES = {
    900: ("Cemetery trash (17 mobs, lv215-228)",
          field(MAT_CEMETERY, USE_FIELD, OFF_CEMETERY, WPN_220[:5])),
    901: ("Revived Quarantine Officer (lv223)",
          field(MAT_CEMETERY, USE_FIELD, OFF_OFFICER, WPN_220[5:10])),
    902: ("D-Seed (lv215)",
          field(MAT_CEMETERY, USE_FIELD, OFF_DSEED, [WPN_220[10]] + WPN_220[:4])),
    906: ("Alpha roster (10 mobs, lv228-240)",
          field(MAT_SPIRE, USE_FIELD, OFF_SPIRE, WPN_235[:5], extra=WPN_235[5:])),
    # --- the flashback zones, stage 7b --------------------------------------
    909: ("Memories wildlife (lv230-232)",
          field(MAT_MEMORIES, USE_FIELD, OFF_MEMORIES, WPN_235[:5])),
    910: ("ELITE Basilissa Melitta (lv238)",
          elite(WPN_235[5:], OFF_QUEEN, MAT_GARDEN)),
    911: ("Garden wildlife (lv235-240)",
          field(MAT_GARDEN, USE_FIELD, OFF_GARDEN, WPN_235[5:])),
    903: ("BOSS Hebarn Officer Pazugenti (lv240)",
          boss([1389, 1398, 1404, 1407, 1419, 1395, 1392, 1386], OFF_BOSS)),
    904: ("BOSS Hebarn Officer Scylla Mira (lv240)",
          boss([1383, 1386, 1392, 1401, 1410, 1413, 1416, 1389], OFF_BOSS)),
    905: ("BOSS Corroded Golem + Revived Veteran (lv238/235)",
          boss([1395, 1404, 1416, 1413, 1401, 1410, 1383, 1398], OFF_BOSS)),
    907: ("BOSS Deadly Drake (lv238, Cemetery)",
          boss([1389, 1392, 1395, 1398, 1401, 1404, 1407, 1410], OFF_BOSS,
               MAT_DRAKE)),
    908: ("BOSS Deadly Drake Alpha (lv240, Spire Village)",
          boss([1383, 1386, 1413, 1416, 1419, 1389, 1398, 1404], OFF_BOSS,
               MAT_DRAKE)),
}

# Zone-number rows mirror the dominant table for that zone, so the 20% fallback
# is not a dead roll. Without these every rate above loses a fifth.
ZONE_MIRROR = {87: 900, 88: 906,
               # The flashback pair mirror their own zones. The Burned Forest
               # is present-day Karkia sharing the Cemetery's roster, so its
               # fallback is the Cemetery's table rather than one of its own.
               133: 909, 144: 911, 131: 900}

# npc row -> new drop table. Both Drakes were sharing a trash table, and the
# seven flashback monsters arrived pointing at Jrose's table 831, which is empty
# on our side.
NPC_TABLE_FIX = {2729: 907, 2699: 908,
                 2527: 909, 2528: 909, 2529: 909,   # Memories wildlife
                 2530: 910,                          # the queen
                 2539: 911, 2547: 911, 2549: 911}    # Garden wildlife

# The flashback monsters also import with NPC_DROP_ITEM of 10 or 30 against the
# 80 the rest of Karkia uses. At 10 the drop roll is usually <= 0 outright, and
# the table-vs-zone split inverts: 90% of what does roll would come off the zone
# fallback rather than the monster's own table.
DROP_ITEM_FIX = {2527: 80, 2528: 80, 2529: 80, 2530: 80,
                 2539: 80, 2547: 80, 2549: 80}

BOSS_ROWS = {2685, 2686, 2687, 2688, 2699, 2729}
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
    reachable = {e[1] for e in common if e[0] == "redirect"}
    for group in groups:
        if group not in reachable:
            raise SystemExit(f"group {group} has no redirect pointing at it")
    return cells


def plan():
    rows = {}
    for row, (label, (common, groups)) in TABLES.items():
        rows[row] = (label, build_row(common, groups))
    for zone, src in ZONE_MIRROR.items():
        rows[zone] = (f"zone {zone} fallback, mirrors {src}", dict(rows[src][1]))
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

    oro = load("import-oro")

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
        print(f"{len(TABLES)} tables + {len(ZONE_MIRROR)} zone mirrors; "
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
        json.dump({"tables": sorted(TABLES), "zones": ZONE_MIRROR,
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
