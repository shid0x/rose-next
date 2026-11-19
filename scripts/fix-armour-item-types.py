"""Hawker caps and gloves typed as the Soldier's (2026-11-19).

An armour piece's item type (LIST_CAP / LIST_ARMS col 4) is what the craft window groups
recipes by (`CManufacture::LoadClassesFromSTB`: one category per type, "Helmet", "Hat" ...)
and what the tooltip names; nothing in the code branches on it. Retail gives each class
line its own: caps 121 Helmet (Soldier), 122 Magic Hat (Muse), 123 Hat (Hawker, Dealer);
gloves 141 Gauntlet (Soldier), 142 Magic Glove (Muse), 143 Glove (Hawker, Dealer).

The user saw Combat Armor Craft list the Hawker's OG cap under "Helmet" beside the
Soldier's (roadmap 1.4, artisan-crafts-200.py). Every Hawker cap and glove of the 220-240
sets carried the Soldier's type -- the endgame set imports cloned their columns from one
template piece -- and two vanilla lv200 caps had drifted from retail (ruff types Steel
Resolve Hat and Mana Steam Head 123, as every other Hawker and Dealer cap). Bodies and boots
were all on the convention (checked 2026-11-19 against every piece of level 200+).

Rows are checked by name and by the value they are expected to hold; cell-level manifest in
build/armour-item-types/, `--dry-run` / `--verify` / `--restore`. Client-side display data
(and the server's tables): re-bake, restart the servers.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from rosefmt.stb import Stb  # noqa: E402

STB = os.path.join(REPO, "data", "3DDATA", "STB")
MANIFEST = os.path.join(REPO, "build", "armour-item-types", "manifest.json")
COL_TYPE = 4
HELMET, HAT, GAUNTLET, GLOVE = 121, 123, 141, 143

# (table, row, name prefix in col 0, expected type, correct type)
FIXES = [
    ("LIST_CAP", 103, "Steel Resolve Hat", HELMET, HAT),       # Hawker, lv200 (retail: Hat)
    ("LIST_CAP", 133, "Mana Steam Head", HELMET, HAT),         # Dealer, lv200 (retail: Hat)
    ("LIST_CAP", 979, "Legendary Crystal Hawk", HELMET, HAT),  # Hawker, lv220
    ("LIST_CAP", 991, "Jackal Bandit Mask", HELMET, HAT),      # Hawker, lv225 Egyptian
    ("LIST_CAP", 268, "Thief's Hat", HELMET, HAT),             # Hawker, lv230 OG
    ("LIST_CAP", 264, "Spinosaur Helmet", HELMET, HAT),        # Hawker, lv235
    ("LIST_CAP", 272, "Sleipnir Hat", HELMET, HAT),            # Hawker, lv240 Norse
    ("LIST_ARMS", 882, "Legendary Crystal Hawk", GAUNTLET, GLOVE),
    ("LIST_ARMS", 894, "Jackal Bandit Bracers", GAUNTLET, GLOVE),
    ("LIST_ARMS", 268, "Thief's Gloves", GAUNTLET, GLOVE),
    ("LIST_ARMS", 264, "Spinosaur Gloves", GAUNTLET, GLOVE),
    ("LIST_ARMS", 272, "Sleipnir Gloves", GAUNTLET, GLOVE),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    man = json.load(open(MANIFEST, encoding="utf-8")) if os.path.isfile(MANIFEST) else {"cells": {}}
    stbs = {f: Stb(os.path.join(STB, f + ".STB")) for f in {f for f, *_ in FIXES}}

    if args.restore:
        skipped = 0
        for key, (old, new) in man["cells"].items():
            f, r = key.split(",")
            s = stbs[f]
            if s.i(int(r), COL_TYPE) != int(new):
                skipped += 1
                continue
            s.set(int(r), COL_TYPE, old.encode("latin-1"))
        for f, s in stbs.items():
            open(os.path.join(STB, f + ".STB"), "wb").write(s.to_bytes())
        if os.path.isfile(MANIFEST):
            os.remove(MANIFEST)
        print(f"restored ({skipped} cell(s) edited since were left alone)")
        return 0

    pending = []
    for f, r, name, expect, want in FIXES:
        s = stbs[f]
        got_name = s.get(r, 0).decode("latin-1").strip()
        if not got_name.startswith(name[:16]):
            print(f"ABORT: {f} {r} is {got_name!r}, expected {name!r}")
            return 1
        cur = s.i(r, COL_TYPE)
        if cur == want:
            continue
        if cur != expect:
            print(f"ABORT: {f} {r} {name} has type {cur}, expected {expect} (reviewed) or {want}")
            return 1
        pending.append((f, r, want))
        print(f"   {f:9} {r:4} {name:24} type {cur} -> {want}")
    print(f"{len(FIXES)} pieces; {len(pending)} to change")
    if args.verify:
        print("VERIFY FAILED" if pending else "ALL CHECKS PASSED")
        return 1 if pending else 0
    if not pending or args.dry_run:
        print("nothing to do." if not pending else "dry run: nothing written")
        return 0
    for f, r, want in pending:
        key = f"{f},{r}"
        man["cells"].setdefault(key, [str(stbs[f].i(r, COL_TYPE)), ""])[1] = str(want)
        stbs[f].set(r, COL_TYPE, str(want).encode("latin-1"))
    for f in {f for f, *_ in pending}:
        open(os.path.join(STB, f + ".STB"), "wb").write(stbs[f].to_bytes())
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    json.dump(man, open(MANIFEST, "w", encoding="utf-8"), indent=1)
    print(f"written (undo: --restore, manifest {os.path.relpath(MANIFEST, REPO)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
