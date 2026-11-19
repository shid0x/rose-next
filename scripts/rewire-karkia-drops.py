#!/usr/bin/env python3
"""Give Karkia its own drop tables. Wiring only -- no loot is authored here.

THE PROBLEM

`CCal::Get_DropITEM` picks which `ITEM_DROP.STB` row to roll on like this:

    if NPC_DROP_ITEM(col 20) - (1 + RANDOM(100)) >= 0 :  row = NPC_DROP_TYPE(col 18)
    else                                              :  row = the ZONE NUMBER

so a drop-table id and a zone id share one namespace, and Karkia lost on both:

1. **Its own tables collide with Oro's.** Karkia imported Jrose's col 18 verbatim,
   which lands on 831/832/835/851-854 -- and 831, 832 and 851 are already used by
   our Desert Scavenger, Agitated Desert Scavenger and the **Fearsome Terrasaurus
   King**. Authoring Karkia loot into those rows would have handed it to our own
   top boss.
2. **Its zone fallback belongs to somebody else.** Karkia is zones 86-144, and
   `ITEM_DROP` rows 86, 87, 88, 131 and 133-136, 144 are live tables for thirteen
   of our own low-level monsters -- Melt Dut, Moss Ant, Moss Golem, the Pomics,
   the Rackies, Beetle, Assistant Chef Woopie. Rows 86 and 87 are even named "EVE
   Zone (Fishing System)" and "(Farming System)". With Karkia's drop chance at 20,
   **~80% of its drops came from those tables.** That is what players were picking
   up in the Cemetery.

Clearing those rows was not an option -- it would have deleted the Pomics' loot.
So the legacy tables move instead, and Karkia inherits the zone numbers that are
now genuinely its own.

WHAT THIS DOES

* Relocates each legacy table squatting on a Karkia zone number to a free row,
  copying it cell for cell and repointing every monster that used it.
* Renumbers Karkia's own tables into a free band, preserving **Jrose's grouping**
  (one shared table for the Cemetery roster, one for Spire Village, one per boss)
  rather than inventing a new one.
* Raises Karkia's drop chance from 20 to OUR_ENDGAME_CHANCE, which is the median
  our own level-200+ monsters use.

Rows are chosen from the free space above everything live: `ITEM_DROP.STB` has
3701 rows and only 1-486 carry anything.

AFTER THIS, KARKIA DROPS NOTHING until the tables are filled -- that is the point.
Correct-and-empty beats plausible-and-wrong, and the content pass is now a pure
content pass with no wiring left in it. See doc/project-drops.md.

Idempotent, verifiable and reversible through a sidecar next to the STB. `data/` is
gitignored, so this docstring is the only committed record of the change.

Usage:
    python scripts/rewire-karkia-drops.py --dry-run
    python scripts/rewire-karkia-drops.py
    python scripts/rewire-karkia-drops.py --verify
    python scripts/rewire-karkia-drops.py --restore
"""
import argparse
import collections
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
NPC_STB = os.path.join(STB_DIR, "LIST_NPC.STB")
DROP_STB = os.path.join(STB_DIR, "ITEM_DROP.STB")
SIDECAR = os.path.join(STB_DIR, "ITEM_DROP.karkia-wiring.json")

COL_DROP_TYPE, COL_DROP_MONEY, COL_DROP_CHANCE = 18, 19, 20

# Both bands sit above every drop-table id anything references (the highest is 854)
# as well as above all live content (rows 1-486). "Free" has to mean BOTH: rows
# 500-506 look empty in ITEM_DROP and are still pointed at by Shaman Ghost and six
# of its neighbours, so authoring Karkia loot there would have handed it to them.
# --verify caught exactly that on the first run of this script.
KARKIA_BAND = 900      # Karkia's own mob tables land here, in Jrose's grouping
LEGACY_BAND = 940      # displaced legacy tables land here
OUR_ENDGAME_CHANCE = 80    # median NPC_DROP_ITEM across our own level-200+ monsters


def gi(stb, row, col):
    v = stb.get(row, col).strip()
    try:
        return int(v)
    except ValueError:
        return 0


def load(name):
    spec = importlib.util.spec_from_file_location(
        name.replace("-", "_"), os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, [name + ".py"]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = argv
    return mod


def row_cells(drop, row):
    return [drop.get(row, c) for c in range(drop.cols)]


def row_is_live(drop, row):
    for c in range(1, drop.cols):
        if gi(drop, row, c) > 1000:
            return True
    return False


def build_plan(npc, drop, karkia_ids, karkia_zones):
    """(table_remap, legacy_moves, chance_rows) -- all decided, nothing written."""
    # A row is only free if it holds nothing AND nothing points at it.
    live = {r for r in range(drop.rows) if row_is_live(drop, r)}
    live |= {gi(npc, r, COL_DROP_TYPE) for r in range(1, npc.rows)
             if npc.get(r, 0).strip() and r not in karkia_ids} - {0}

    # 1. Karkia's own tables, in Jrose's grouping, ordered so the mapping is stable
    groups = collections.OrderedDict()
    for i in sorted(karkia_ids):
        t = gi(npc, i, COL_DROP_TYPE)
        if t:
            groups.setdefault(t, []).append(i)
    table_remap = {}
    nxt = KARKIA_BAND
    for t in sorted(groups):
        while nxt in live or nxt in table_remap.values():
            nxt += 1
        table_remap[t] = nxt
        nxt += 1

    # 2. legacy tables sitting on a Karkia zone number, and who uses them
    users = collections.defaultdict(list)
    for r in range(1, npc.rows):
        if not npc.get(r, 0).strip() or r in karkia_ids:
            continue
        t = gi(npc, r, COL_DROP_TYPE)
        if t in karkia_zones:
            users[t].append(r)
    legacy_moves = {}
    nxt = LEGACY_BAND
    for z in sorted(set(list(users)) | {z for z in karkia_zones if row_is_live(drop, z)}):
        while nxt in live or nxt in table_remap.values() or nxt in legacy_moves.values():
            nxt += 1
        legacy_moves[z] = nxt
        nxt += 1

    chance_rows = [i for i in sorted(karkia_ids)
                   if gi(npc, i, COL_DROP_CHANCE) not in (0, OUR_ENDGAME_CHANCE)]
    return table_remap, legacy_moves, dict(users), chance_rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    import rosedata as oro  # the shared data layer (was import-oro.py)
    ik = load("import-karkia")
    npc = oro.Stb(NPC_STB)
    drop = oro.Stb(DROP_STB)
    karkia_ids = set(ik.KARKIA_MONSTERS)
    karkia_zones = {z for z, _f, _l, _k in ik.ZONES}

    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for row, cols in saved["npc"].items():
            for col, v in cols.items():
                npc.set(int(row), int(col), v)
        for row, cells in saved["drop"].items():
            for c, v in enumerate(cells):
                drop.set(int(row), c, v)
        with open(NPC_STB, "wb") as fh:
            fh.write(npc.to_bytes())
        with open(DROP_STB, "wb") as fh:
            fh.write(drop.to_bytes())
        os.remove(SIDECAR)
        print(f"restored {len(saved['npc'])} monster rows and "
              f"{len(saved['drop'])} drop rows; sidecar removed")
        return

    table_remap, legacy_moves, users, chance_rows = build_plan(
        npc, drop, karkia_ids, karkia_zones)

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the rewire has not been applied")
        bad = []
        # no Karkia monster may share a drop table with anything else
        kt = {gi(npc, i, COL_DROP_TYPE) for i in karkia_ids} - {0}
        for r in range(1, npc.rows):
            if npc.get(r, 0).strip() and r not in karkia_ids \
                    and gi(npc, r, COL_DROP_TYPE) in kt:
                bad.append(("shared table", r, gi(npc, r, COL_DROP_TYPE)))
        # every relocated legacy table must still hold what it held -- seen through
        # drops-below-oro.py's manifest (2026-11-19): it gave the relocated Marsh tables
        # (940-942) a uniques bucket and records the cells it changed
        below = os.path.join(ROOT, "build", "drops-below-oro", "manifest.json")
        if os.path.isfile(below):
            with open(below, encoding="utf-8") as fh:
                for key, (was, now) in json.load(fh)["drop"].items():
                    r, c = (int(x) for x in key.split(","))
                    if drop.get(r, c).strip() == now.encode("latin-1"):
                        drop.set(r, c, was.encode("latin-1"))
        for old, new in saved["moved"].items():
            if row_cells(drop, int(new)) != [c.encode() if isinstance(c, str) else c
                                             for c in saved["drop"][old]]:
                bad.append(("moved row content", old, new))
        # and its users must point at the new row
        for old, rows in saved["users"].items():
            for r in rows:
                if gi(npc, r, COL_DROP_TYPE) != int(saved["moved"][old]):
                    bad.append(("user not repointed", r, old))
        # Karkia must no longer fall back onto a live *legacy* table. This used
        # to be "the row is live", which was right while the zone rows were meant
        # to stay empty and is wrong now: stage 6e (add-karkia-drops.py) mirrors
        # Karkia's own tables into these exact rows on purpose, so a live row is
        # the intended end state. What still counts as a leak is the *legacy
        # content* sitting there -- compare against the cells the sidecar
        # recorded before the move.
        leak = []
        for z in sorted(karkia_zones):
            was = saved["drop"].get(str(z))
            if was is None:
                continue
            was = [c.encode() if isinstance(c, str) else c for c in was]
            if row_is_live(drop, z) and row_cells(drop, z) == was:
                leak.append(z)
        print(f"{len(bad)} problem(s)" + (f": {bad[:8]}" if bad else ""))
        print(f"Karkia zone rows still holding legacy loot: {leak or 'none'}")
        sys.exit(1 if bad or leak else 0)

    if saved:
        print(f"already applied -- nothing to do.")
        print("re-run with --restore first if you want to change the parameters.")
        return

    record = {"npc": collections.defaultdict(dict), "drop": {},
              "moved": {}, "users": {}}

    print("1. legacy tables squatting on a Karkia zone number, relocated")
    for z, new in sorted(legacy_moves.items()):
        cells = row_cells(drop, z)
        record["drop"][str(z)] = [c.decode("latin-1") for c in cells]
        record["drop"][str(new)] = [drop.get(new, c).decode("latin-1")
                                    for c in range(drop.cols)]
        record["moved"][str(z)] = new
        record["users"][str(z)] = users.get(z, [])
        name = drop.get(z, 0).decode("latin-1", "replace")[:34]
        print(f"   row {z:<4} -> {new:<4} {name!r:38} {len(users.get(z, []))} monster(s)")
        if not args.dry_run:
            for c, v in enumerate(cells):
                drop.set(new, c, v)
            for c in range(drop.cols):
                drop.set(z, c, b"")
            for r in users.get(z, []):
                record["npc"][str(r)][str(COL_DROP_TYPE)] = \
                    npc.get(r, COL_DROP_TYPE).decode("latin-1")
                npc.set(r, COL_DROP_TYPE, str(new))

    print("\n2. Karkia's own tables, renumbered out of Oro's range")
    groups = collections.defaultdict(list)
    for i in sorted(karkia_ids):
        t = gi(npc, i, COL_DROP_TYPE)
        if t in table_remap:
            groups[t].append(i)
    for old, new in sorted(table_remap.items()):
        clash = [r for r in range(1, npc.rows)
                 if npc.get(r, 0).strip() and r not in karkia_ids
                 and gi(npc, r, COL_DROP_TYPE) == old]
        print(f"   table {old:<4} -> {new:<4} {len(groups[old]):>2} Karkia monster(s)"
              + (f"   (was shared with {len(clash)}: "
                 f"{[npc.get(c,0).decode('latin-1','replace')[:22] for c in clash]})"
                 if clash else ""))
        if not args.dry_run:
            for i in groups[old]:
                record["npc"][str(i)][str(COL_DROP_TYPE)] = \
                    npc.get(i, COL_DROP_TYPE).decode("latin-1")
                npc.set(i, COL_DROP_TYPE, str(new))

    print(f"\n3. drop chance {OUR_ENDGAME_CHANCE} (our level-200+ median) "
          f"on {len(chance_rows)} Karkia monsters")
    if not args.dry_run:
        for i in chance_rows:
            record["npc"][str(i)][str(COL_DROP_CHANCE)] = \
                npc.get(i, COL_DROP_CHANCE).decode("latin-1")
            npc.set(i, COL_DROP_CHANCE, str(OUR_ENDGAME_CHANCE))

    if args.dry_run:
        print("\ndry run: nothing written")
        return

    with open(NPC_STB, "wb") as fh:
        fh.write(npc.to_bytes())
    with open(DROP_STB, "wb") as fh:
        fh.write(drop.to_bytes())
    record["npc"] = dict(record["npc"])
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(record, fh, indent=1, sort_keys=True)

    chk_npc, chk_drop = oro.Stb(NPC_STB), oro.Stb(DROP_STB)
    bad = []
    for old, new in legacy_moves.items():
        if [c.decode("latin-1") for c in row_cells(chk_drop, new)] != record["drop"][str(old)]:
            bad.append(f"row {old} -> {new} content mismatch")
        for r in users.get(old, []):
            if gi(chk_npc, r, COL_DROP_TYPE) != new:
                bad.append(f"monster {r} not repointed to {new}")
    if bad:
        sys.exit("VERIFY FAILED:\n  " + "\n  ".join(bad))
    print(f"\nwrote it; sidecar {os.path.basename(SIDECAR)}")
    print("Karkia now drops NOTHING until its tables are filled -- that is intended.")
    print("Next: doc/project-drops.md has the content plan.")


if __name__ == "__main__":
    sys.exit(main())
