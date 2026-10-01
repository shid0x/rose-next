#!/usr/bin/env python3
"""Re-gate the imported shields (LIST_SUBWPN 308-342) so STR tracks their tier.

THE DEFECT
----------
`import-item.py --art-only` clones every stat column from a template row and then
overrides only what is passed on the command line. The shield import passed
--def/--res/--req-level but **not** the STR requirement, so STR came from the
template while the level came from the flag, and the two decoupled.

Our ladder scales STR with tier -- Buckler 52, Kite 100, Blood 164, Righteous
180, Vivor 264, Golden Angel and Ancient Davion 375 -- so a row templated on
Golden Angel inherited STR 375 no matter what level it was given. Six items ended
up at level 165-190 demanding STR 375, which is an endgame gate: base stats cap
at 300 (`GameStaticConfig::MAX_STAT`), so 375 needs +75 from gear. A level-170
Steam Feather was unequippable at its own level.

Requirement pairs live at game cols 19/20 and 21/22 as (ability id, value), and
`AT_STR` is **10** (datatype.h). The check is `GetCur_AbilityValue(AT_STR) <
required` in CUserDATA, i.e. current STR including gear bonuses.

THE ROLE SPLIT IS JROSE'S, AND WORTH KEEPING
--------------------------------------------
Their tier set is gated three ways in job column 16 -- 61, 64, 66 -- and their
STR requirements follow it: knight shields want 421-457, the "mirrors" only
274-290, bucklers in between. Those mirrors are caster off-hands meant for a wand,
classified as shields because that is the slot they occupy. Their geometry says
the same thing: mirrors measure 0.58-0.63 m against 0.80-0.90 m for the knight
shields.

We do not gate shields by job at all (our job columns are blank), so STR is the
only lever, and it reproduces the intent well enough: a caster can meet a low STR
gate, a strength build meets any of them.

Values below keep heavy shields at our existing 375 endgame bar, put light ones
about 15% under it, and casters about 33% under -- roughly Jrose's own ratio --
then scale the whole thing down for anything below level 200. Rows 335-342 were
already proportionate and are left alone.

Idempotent; --dry-run and --verify supported.
"""
import argparse
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OURS = "data"
STB_REL = os.path.join("3DDATA", "STB", "LIST_SUBWPN.STB")
AT_STR = 10

# id -> (expected name fragment, role, new STR requirement)
GATES = {
    308: ("Freyja",            "caster", 250),
    309: ("Garm",              "light",  310),
    310: ("Fafnir",            "heavy",  375),
    311: ("Cathedral",         "caster", 258),
    312: ("Ashura",            "light",  318),
    313: ("Reconquista",       "heavy",  375),
    314: ("Ushumgal",          "caster", 265),
    315: ("Zlatorog",          "light",  325),
    316: ("Lindwurm",          "heavy",  375),
    317: ("Evolved Ushumgal",  "caster", 272),
    318: ("Evolved Zlatorog",  "light",  332),
    319: ("Evolved Lindwurm",  "heavy",  375),
    320: ("Steam Clock (Gold",   "heavy",  360),
    321: ("Steam Clock (Silver", "heavy",  360),
    322: ("Steam Clock (Black",  "heavy",  360),
    323: ("Steam Feather (Gold",   "caster", 210),
    324: ("Steam Feather (Silver", "caster", 210),
    325: ("Steam Feather (Black",  "caster", 210),
    326: ("Chronos Aspis",     "heavy",  330),
    327: ("Lero Escudo",       "caster", 170),
    328: ("Lord of Riot",      "heavy",  375),
    329: ("Red Feather Compass", "caster", 160),
    330: ("Righteous Shield NEO", "heavy", 375),   # lv240 since repackage-subweapons.py
    331: ("Lord Knight", "heavy", 375),
    332: ("Black Heaven Dragon", "light", 300),
    333: ("Black Heaven Lion",  "light",  270),
    334: ("Gryphon Lumen",     "caster", 190),
}


def req_slots(row):
    """(str_slot, level) reading the two (ability, value) requirement pairs."""
    str_slot, level = None, None
    for c in (19, 21):
        t = row[c].decode("latin-1").strip()
        if t == str(AT_STR):
            str_slot = c
        elif t == "31":
            level = int(row[c + 1].decode("latin-1") or 0)
    return str_slot, level


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
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
        for r, (frag, role, want) in sorted(GATES.items()):
            if data[r][4].strip() != b"261":
                print("  %-4d %-26s re-packaged (not a shield since repackage-subweapons.py)"
                      % (r, data[r][0].decode("latin-1")[:26]))
                continue
            slot, lv = req_slots(data[r])
            cur = int(data[r][slot + 1].decode("latin-1") or 0) if slot is not None else None
            ok = cur == want
            bad += 0 if ok else 1
            print("  %-4d %-26s %-7s lv=%-4s STR=%-4s %s"
                  % (r, data[r][0].decode("latin-1")[:26], role, lv, cur,
                     "OK" if ok else "*** expected %d ***" % want))
        sys.exit(1 if bad else 0)

    changed = 0
    for r, (frag, role, want) in sorted(GATES.items()):
        if not 0 <= r < len(data):
            sys.exit("row %d out of range" % r)
        name = data[r][0].decode("latin-1")
        if frag.lower() not in name.lower():
            sys.exit("row %d is %r, expected a name containing %r -- the table has shifted, "
                     "refusing to re-gate the wrong row" % (r, name, frag))
        if data[r][4].strip() != b"261":
            print("  %-4d %-26s re-packaged, skipped" % (r, name[:26]))
            continue
        slot, lv = req_slots(data[r])
        if slot is None:
            sys.exit("row %d (%s) has no STR requirement slot to rewrite" % (r, name))
        cur = int(data[r][slot + 1].decode("latin-1") or 0)
        if cur == want:
            print("  %-4d %-26s already STR %d" % (r, name[:26], want))
            continue
        print("  %-4d %-26s %-7s lv=%-4s STR %4d -> %d" % (r, name[:26], role, lv, cur, want))
        ii.stb_set_cell(path, r, slot + 1, str(want), args.dry_run)
        changed += 1

    if not args.dry_run and changed:
        _d, _o, r2, c2, d2 = ii.stb_read(path)
        assert (r2, c2) == (rows, cols), "table shape changed"
        for r, (_f, _role, want) in GATES.items():
            slot, _lv = req_slots(d2[r])
            assert int(d2[r][slot + 1].decode("latin-1")) == want, "row %d did not take" % r
        print("verified: %d row(s) re-gated, shape unchanged (%dx%d)" % (changed, r2 - 1, c2 - 1))
    print("\n%s" % ("DRY RUN - nothing written." if args.dry_run
                    else "done.  Restart the gameserver (it caches STBs) and re-bake."))


if __name__ == "__main__":
    main()
