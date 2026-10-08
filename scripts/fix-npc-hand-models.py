"""Clear NPC hand-weapon cells that point past the model table they index.

A monster's weapons are LIST_NPC col 5 (right hand) and col 6 (left hand).
The client resolves them at LIST_NPC.CHR load (CCharMODELDATA::Load_MOBorNPC)
against two different model tables: the right hand indexes
3Ddata\\WEAPON\\LIST_WEAPON.ZSC, the left hand LIST_SUBWPN.ZSC. An index past
the table logs `ERROR:: Invalid model data index[ N / count ]` on every launch
and the hand is left empty. The server reads only the right hand
(CObjMOB::Get_R_WEAPON) -- the left hand is purely visual.

Gangster Pang Little Jack (1456) and Gangster Pang Jack (1457) carry 1080 in
the left hand: the *weapon* id of the Goblin Dungeon Ranger's mob weapon, typed
into the sub-weapon slot. LIST_SUBWPN.ZSC has 343 models here, and sub-weapon
1080 is blank even in the dumps whose table is long enough to hold it (139,
Evo 137, Jrose, Rose Brazil). The slip is retail: ruff, QQ, titan, tsuki, 139,
Evo, Jrose and Brazil all carry it; RoseZA and 667, the later authoring of
these two rows, cleared the cell. So do we. Both Pangs are unspawned (their
skills were imported 2026-09-15); the fix only silences the startup error.

    python scripts/fix-npc-hand-models.py --dry-run
    python scripts/fix-npc-hand-models.py
    python scripts/fix-npc-hand-models.py --verify
    python scripts/fix-npc-hand-models.py --restore

The scan covers both hands of every row. The reviewed outcome is EXPECTED;
anything else out of range is reported and refused unless --allow-new, so an
import that brings the same slip shows up here rather than being cleared
unseen. Undo is per cell (build/npc-hand-models/manifest.json) -- never a
whole-file copy, which would revert every later script's LIST_NPC edits.
`data/` is gitignored, so this file is the record. Client-only data: re-bake
the VFS; a server restart is not needed.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from mapgen import catalogue  # noqa: E402

STB = os.path.join(REPO, "data", "3DDATA", "STB", "LIST_NPC.STB")
ZSC = {
    5: os.path.join(REPO, "data", "3DDATA", "WEAPON", "LIST_WEAPON.ZSC"),
    6: os.path.join(REPO, "data", "3DDATA", "WEAPON", "LIST_SUBWPN.ZSC"),
}
HAND = {5: "right (LIST_WEAPON.ZSC)", 6: "left (LIST_SUBWPN.ZSC)"}
BACKUP_DIR = os.path.join(REPO, "build", "npc-hand-models")
MANIFEST = os.path.join(BACKUP_DIR, "manifest.json")

# (row, col): the value found when reviewed. Cleared to blank, as RoseZA/667 have it.
EXPECTED = {
    (1456, 6): b"1080",   # Gangster Pang Little Jack: weapon id in the sub-weapon slot
    (1457, 6): b"1080",   # Gangster Pang Jack: same
}


def load(name, fname):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fname))
    m = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [fname]
    try:
        spec.loader.exec_module(m)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return m


oro = load("import_oro", "import-oro.py")


def model_counts():
    return {col: len(catalogue.read_zsc(path)[1]) for col, path in ZSC.items()}


def scan(stb, counts):
    """{(row, col): value} for every hand cell that indexes past its model table."""
    bad = {}
    for row in range(stb.rows):
        for col, count in counts.items():
            v = stb.get(row, col).strip()
            if not v:
                continue
            try:
                n = int(v)
            except ValueError:
                bad[(row, col)] = v
                continue
            if n < 0 or n >= count:
                bad[(row, col)] = v
    return bad


def describe(stb, row, col, value):
    name = stb.get(row, 0).decode("latin-1").strip() or "(no name)"
    return "LIST_NPC %d %-28s %s hand = %s" % (row, name, HAND[col], value.decode("latin-1"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--allow-new", action="store_true",
                    help="also clear out-of-range cells that are not in EXPECTED")
    a = ap.parse_args()

    original = open(STB, "rb").read()
    stb = oro.Stb(STB)
    if stb.to_bytes() != original:
        print("ABORT: the STB writer does not round-trip LIST_NPC.STB byte-for-byte")
        return 1

    if a.restore:
        if not os.path.isfile(MANIFEST):
            print("nothing to restore (%s not found)" % MANIFEST)
            return 0
        man = json.load(open(MANIFEST))
        for key, cell in man["cells"].items():
            row, col = (int(x) for x in key.split(","))
            if stb.get(row, col).strip():
                print("   skip %d,%d: no longer blank (%r) -- edited since" % (row, col, stb.get(row, col)))
                continue
            stb.set(row, col, cell.encode("latin-1"))
            print("   restored " + describe(stb, row, col, cell.encode("latin-1")))
        open(STB, "wb").write(stb.to_bytes())
        os.remove(MANIFEST)
        print("restored LIST_NPC.STB")
        return 0

    counts = model_counts()
    print("models: %d weapons, %d sub-weapons" % (counts[5], counts[6]))
    bad = scan(stb, counts)
    known = {k: v for k, v in bad.items() if EXPECTED.get(k) == v}
    new = {k: v for k, v in bad.items() if k not in known}
    for (row, col), v in sorted(known.items()):
        print("   " + describe(stb, row, col, v))
    for (row, col), v in sorted(new.items()):
        print("   NEW " + describe(stb, row, col, v))

    if a.verify:
        if bad:
            print("VERIFY FAILED: %d hand cell(s) index past their model table" % len(bad))
            return 1
        print("ALL CHECKS PASSED -- every NPC hand indexes a model that exists.")
        return 0
    if new and not a.allow_new:
        print("ABORT: %d out-of-range cell(s) not in EXPECTED -- review, then add them "
              "or pass --allow-new" % len(new))
        return 1
    todo = dict(known)
    if a.allow_new:
        todo.update(new)
    if not todo:
        print("nothing to do -- every NPC hand indexes a model that exists.")
        return 0
    if a.dry_run:
        print("dry run: %d cell(s) would be cleared, nothing written" % len(todo))
        return 0

    os.makedirs(BACKUP_DIR, exist_ok=True)
    man = json.load(open(MANIFEST)) if os.path.isfile(MANIFEST) else {"cells": {}}
    for (row, col), v in todo.items():
        man["cells"].setdefault("%d,%d" % (row, col), v.decode("latin-1"))
        stb.set(row, col, b"")
    json.dump(man, open(MANIFEST, "w"), indent=1)
    open(STB, "wb").write(stb.to_bytes())
    print("cleared %d cell(s) in LIST_NPC.STB (undo: --restore, manifest in %s)"
          % (len(todo), BACKUP_DIR))

    left = scan(oro.Stb(STB), counts)
    if left:
        print("VERIFY FAILED after write: %d cell(s) still out of range" % len(left))
        return 1
    print("verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
