#!/usr/bin/env python3
r"""Make the Intrepid Staff and Intrepid Wand deal magic damage like every other caster weapon.

THE DEFECT
----------
`WEAPON_IS_MAGIC_DAMAGE` is **column 37** of `LIST_WEAPON.STB`
(`src/common/include/rose/io/stb.h`, `5 + 32`). It is the only thing that picks the
formula for a normal attack (`CCal::Get_DAMAGE`, `src/common/calculation.cpp`):

    if (pATK->IsMagicDAMAGE())
        return Get_MagicDAMAGE(pATK, pDEF, wHitCNT, iSuc);
    return Get_BasicDAMAGE(pATK, pDEF, wHitCNT, iSuc);

and `CObjAVT::IsMagicDAMAGE()` is just that cell for the right-hand weapon. Two of the
level-210 weapons Huzam sells in Muris had it at 0:

    1375  Intrepid Staff    (type 241)
    1376  Intrepid Wand     (type 242)

so a Muse swinging one rolled the *physical* formula: the same attack power, but
mitigated the way a sword is -- by the target's DEF, which the physical formula
carries twice -- instead of by its RES. Against the high-DEF, low-RES monsters a
caster is meant to be good at, the staff hit like a weak club. Skills were
unaffected: `Get_SkillDAMAGE` switches on the skill row's `SKILL_DAMAGE_TYPE` and
never asks the weapon.

SCOPE
-----
These are the only two. Of our 117 staff/wand rows (types 241/242), 115 have the flag
and exactly these two do not; no non-caster row has it.

WHY THIS IS A REPAIR AND NOT A BALANCE CHANGE
---------------------------------------------
The rows come from the QQ-iROSE lineage, which carries the same defect: its
`Intrepid Staff` / `Intrepid Wand` (rows 605/606) have the flag at 0, while its own
`P2P Intrepid Staff` / `P2P Intrepid Wand` (rows 388/389) -- the same weapons -- have
it at 1. 667, RoseZA, Evo 137, ruff and titanRose have no caster row without the flag
at all. Nothing else on the two rows is touched: ATK (360 / 320), the level and INT
gates and the bonuses written by `rebalance-muse-weapons.py` (cols 24-28) stay as
they are.

Only the server acts on the flag (combat damage is server-authoritative; the client
declares `IsMagicDAMAGE()` but nothing calls it), so a gameserver restart is what
makes the fix take. Re-bake anyway so the client's copy of the table stays in step.

Idempotent; --dry-run, --verify and --restore supported.
"""
import argparse
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OURS = "data"
STB_REL = os.path.join("3DDATA", "STB", "LIST_WEAPON.STB")
MAGIC_COL = 37
WEAPON_TYPE_COL = 4
BROKEN, WANT = "0", "1"

# row -> (expected name, expected weapon type), so the pass refuses to rewrite a
# shifted table
ROWS = {
    1375: ("Intrepid Staff", "241"),
    1376: ("Intrepid Wand", "242"),
}
CASTER_TYPES = ("241", "242")


def cell(data, r, c):
    return data[r][c].decode("latin-1").strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true",
                    help="put the flag back to 0 on the two rows")
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(OURS, "3DDATA")):
        sys.exit("run from the repo root (data/3DDATA not found)")
    spec = importlib.util.spec_from_file_location("ii", os.path.join(HERE, "import-item.py"))
    ii = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ii)

    path = os.path.join(OURS, STB_REL)
    _d, _o, rows, cols, data = ii.stb_read(path)

    if args.verify:
        bad = 0
        for r, (name, _t) in sorted(ROWS.items()):
            cur = cell(data, r, MAGIC_COL)
            ok = cur == WANT
            bad += 0 if ok else 1
            print("  %-5d %-16s magic=%-3s %s"
                  % (r, name, cur, "OK" if ok else "*** expected %s ***" % WANT))
        # the whole table, so a later import that repeats the defect is caught here
        for r in range(1, len(data)):
            if cell(data, r, WEAPON_TYPE_COL) in CASTER_TYPES and cell(data, r, 0) \
                    and cell(data, r, MAGIC_COL) != WANT and r not in ROWS:
                bad += 1
                print("  %-5d %-16s is a staff/wand without the magic flag"
                      % (r, cell(data, r, 0)))
        sys.exit(1 if bad else 0)

    src, dst = (WANT, BROKEN) if args.restore else (BROKEN, WANT)
    changed = 0
    for r, (name, wtype) in sorted(ROWS.items()):
        if not 0 <= r < len(data):
            sys.exit("row %d out of range" % r)
        got = cell(data, r, 0)
        if got != name:
            sys.exit("row %d is %r, expected %r -- the table has shifted, refusing to "
                     "rewrite the wrong row" % (r, got, name))
        if cell(data, r, WEAPON_TYPE_COL) != wtype:
            sys.exit("row %d (%s) is weapon type %s, expected %s"
                     % (r, name, cell(data, r, WEAPON_TYPE_COL), wtype))
        cur = cell(data, r, MAGIC_COL)
        if cur == dst:
            print("  %-5d %-16s already %s" % (r, name, dst))
            continue
        if cur not in (src, ""):
            sys.exit("row %d (%s) has magic flag %r, expected %s or %s -- something else "
                     "changed it; not guessing" % (r, name, cur, src, dst))
        print("  %-5d %-16s magic %s -> %s" % (r, name, cur or "(blank)", dst))
        ii.stb_set_cell(path, r, MAGIC_COL, dst, args.dry_run)
        changed += 1

    if not args.dry_run and changed:
        _d3, _o3, r2, c2, d2 = ii.stb_read(path)
        assert (r2, c2) == (rows, cols), "table shape changed"
        for r in ROWS:
            assert cell(d2, r, MAGIC_COL) == dst, "row %d did not take" % r
        diff = sum(1 for a, b in zip(data, d2) for x, y in zip(a, b) if x != y)
        assert diff == changed, "%d cells differ, expected %d" % (diff, changed)
        print("verified: %d cell(s) written, nothing else differs, shape unchanged (%dx%d)"
              % (changed, r2 - 1, c2 - 1))
    print("\n%s" % ("DRY RUN - nothing written." if args.dry_run
                    else "done.  Restart the gameserver (it caches STBs) and re-bake."))


if __name__ == "__main__":
    main()
