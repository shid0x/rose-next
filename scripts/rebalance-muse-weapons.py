"""Make the staff the thing that beats the Mage's stat cap.

    python scripts/rebalance-muse-weapons.py --profile focus
    python scripts/rebalance-muse-weapons.py --restore

Same machinery and sidecar contract as `rebalance-raider-weapons.py` and
`rebalance-scout-weapons.py`, which this is a sibling of. The skill side of the
Muse pass lives in `rebalance-muse.py`.

Why this pass exists
--------------------
A Mage's damage stops growing at level 200, and no amount of SKILL_POWER fixes
that, because the thing that stopped is a *stat*.

    GameStaticConfig::MAX_STAT = 300

`gs_user.cpp` (`Recv_cli_USE_BPOINT_REQ`) refuses to raise a basic ability past
it, and a Mage's INT reaches 300 around level 200. From there its primary damage
stat is frozen while a Scout's ATK keeps climbing out of weapon tiers.

**But the cap is enforced only on spent points.** `GetCur_INT()`
(`src/common/shared/cuserdata.h`) is

    GetDef_INT() + m_iAddValue[AT_INT] + m_PassiveAbilityFromRate[...]

and only `m_BasicAbility.m_nBasicA[]` is checked against MAX_STAT. **INT granted
by gear bypasses it entirely.** So the one lever that keeps a Mage scaling past
level 200 is INT on the weapon.

Why INT rather than ATK
-----------------------
Both feed magic damage, but not equally. The staff's own attack power
(`CObjAVT::Cal_ATTACK`, weapon class 24, type 241) is

    AP = (STR*0.4 + INT*0.4 + LEVEL*0.2) + weaponAP * (INT*0.05 + 29) / 30

so INT appears twice -- additively, and as a *multiplier on the weapon itself*.
That AP then enters the magic formula at 0.8, and INT enters again directly at
1.2. Differentiating the magic bracket at the best staff (weaponAP 401):

    +1 INT  ->  0.8 * (0.4 + 401 * 0.05 / 30) + 1.2  =  2.05
    +1 ATK  ->  0.8                                  =  0.80

**INT is 2.6x the value of ATK per point, and unlike ATK it is not something a
Mage can already max.** A wand (type 242) takes the other branch, which scales
its weapon AP by SENSE rather than INT, so INT is worth 1.68/point there --
still its best single stat, hence the smaller curve.

Sizing, and what this does and does not fix
-------------------------------------------
+4 INT at level 10 rising to +60 at 230 on staves (+3 to +50 on wands),
interpolated on each weapon's own level requirement. At level 200 that is about
+52 INT, worth roughly **+15% magic damage** -- which is why the skill pass had
to carry the rest. This does not fix the 200-240 flatline either; it raises the
whole line and softens the ceiling. Actually removing the ceiling means changing
either MAX_STAT or the magic formula, both C++, and that is deliberately not
attempted here.

Slot handling
-------------
Staves are the opposite of bows: **slot 1 is occupied on 47 of 47** by
`AT_PSV_SAVE_MP`, and slot 2 is free on 45. So this pass takes **slot 2** almost
everywhere and leaves the MP discount alone -- it is the Muse's flavour, it is
not competing for the slot, and displacing it would be a straight nerf to the
class this pass exists to help.

The two endgame weapons of each type already carry INT in slot 2 (+15 at level
210, +10 at 230), so they are **raised in place** rather than displaced. That is
worth noting on its own: INT on a magic weapon is not an invention here, it is
an existing endgame pattern that simply never existed below level 210 and was
tiny where it did. Nothing is displaced by this pass at all.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WEAPON_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_WEAPON.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_WEAPON.muse-focus.json")

# LIST_WEAPON.STB game columns.
COL_NAME = 0
COL_TYPE = 4            # 241 staff, 242 wand
COL_REQ_STAT0 = 19      # Required Stat 1 / Amount 1
COL_REQ_AMT0 = 20
COL_REQ_STAT1 = 21      # Required Stat 2 / Amount 2
COL_REQ_AMT1 = 22
COL_BONUS0 = 24         # Bonus Stat 1 / Amount 1   (col 23 is its condition)
COL_BONUS0_AMT = 25
COL_BONUS1 = 27         # Bonus Stat 2 / Amount 2   (col 26 is its condition)
COL_BONUS1_AMT = 28

WRITTEN = (COL_BONUS0, COL_BONUS0_AMT, COL_BONUS1, COL_BONUS1_AMT)

STAFF, WAND = 241, 242

# t_AbilityINDEX, src/common/shared/datatype.h.
AT_LEVEL = 31
AT_INT = 12

ABILITY_NAME = {10: "STR", 11: "DEX", 12: "INT", 13: "CON", 15: "SENSE",
                18: "ATK", 19: "DEF", 20: "HIT", 21: "RES", 22: "AVOID",
                24: "ATKSPD", 26: "CRITICAL", 29: "SAVEMP", 39: "MAXMP"}

# profile -> weapon type -> (bonus stat, amount at the lowest level, amount at
# the highest, label). Amounts are interpolated against each weapon's own level
# requirement, between the lowest and highest found for that type.
PROFILES = {
    "focus": {
        STAFF: (AT_INT, 4, 60, "INT"),
        WAND: (AT_INT, 3, 50, "INT"),
    },
}


def load_stb_module():
    """import-oro.py carries the only writable STB implementation we have."""
    spec = importlib.util.spec_from_file_location(
        "import_oro", os.path.join(HERE, "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["import-oro.py", "--help"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def gi(stb, r, c):
    v = stb.get(r, c).strip()
    return int(v) if v.lstrip(b"-").isdigit() else 0


def level_of(stb, r):
    """The weapon's level requirement, from whichever required-stat slot holds it."""
    for stat_col, amt_col in ((COL_REQ_STAT0, COL_REQ_AMT0), (COL_REQ_STAT1, COL_REQ_AMT1)):
        if gi(stb, r, stat_col) == AT_LEVEL:
            return gi(stb, r, amt_col)
    return 0


def candidates(stb, wtype, vanilla=None, stat=None):
    """Rows of one weapon type with a level requirement, and which slot to use.

    Order matters. Raising the stat *in place* wherever a slot already grants it
    comes first, so a weapon never ends up with two tooltip lines of the same
    stat; then a free slot; then, only if both are taken by something else,
    slot 2 is overwritten and the loss is reported.

    For staves and wands nothing is ever displaced: slot 1 is SAVE_MP almost
    everywhere and slot 2 is either free or already INT.
    """
    vanilla = vanilla or {}
    rows, displaced = [], []
    for r in range(stb.rows):
        if gi(stb, r, COL_TYPE) != wtype:
            continue
        lv = level_of(stb, r)
        if not lv:
            continue
        # Judge the slots from the VANILLA values, never from what is on disk.
        # Reading live data makes this non-idempotent: once a slot has been
        # written, a re-read sees it occupied and picks differently, so --verify
        # computes a different target than --profile just applied.
        b0, _a0, b1, a1 = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
        if stat and b0 == stat:
            rows.append((r, lv, COL_BONUS0, COL_BONUS0_AMT))
        elif stat and b1 == stat:
            rows.append((r, lv, COL_BONUS1, COL_BONUS1_AMT))
        elif not b0:
            rows.append((r, lv, COL_BONUS0, COL_BONUS0_AMT))
        elif not b1:
            rows.append((r, lv, COL_BONUS1, COL_BONUS1_AMT))
        else:
            rows.append((r, lv, COL_BONUS1, COL_BONUS1_AMT))
            displaced.append((r, lv, b1, a1))
    return rows, displaced


def target_state(stb, profile, vanilla=None):
    """{row: (bonus0, amt0, bonus1, amt1)} the named profile should produce."""
    want = {}
    for wtype, (stat, lo_amt, hi_amt, _label) in PROFILES[profile].items():
        rows, _displaced = candidates(stb, wtype, vanilla, stat)
        if not rows:
            continue
        lv_lo = min(lv for _r, lv, _c, _a in rows)
        lv_hi = max(lv for _r, lv, _c, _a in rows)
        span = (lv_hi - lv_lo) or 1
        for r, lv, stat_col, amt_col in rows:
            amount = round(lo_amt + (hi_amt - lo_amt) * (lv - lv_lo) / span)
            vals = list(vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN))
            vals[WRITTEN.index(stat_col)] = stat
            vals[WRITTEN.index(amt_col)] = amount
            want[r] = tuple(vals)
    return want


def apply_values(stb, values):
    for r, vals in values.items():
        for c, v in zip(WRITTEN, vals):
            stb.set(r, c, str(v) if v else "")


def read_sidecar():
    if not os.path.exists(SIDECAR):
        return None, {}
    with open(SIDECAR, encoding="utf-8") as fh:
        raw = json.load(fh)
    return raw["profile"], {int(k): tuple(v) for k, v in raw["rows"].items()}


def write_sidecar(profile, rows):
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"profile": profile,
                   "rows": {str(k): list(v) for k, v in sorted(rows.items())}},
                  fh, indent=1)


def show(stb, profile, want, vanilla=None):
    for wtype, (stat, lo_amt, hi_amt, label) in PROFILES[profile].items():
        rows, displaced = candidates(stb, wtype, vanilla, stat)
        name = "STAFF" if wtype == STAFF else "WAND"
        print(f"\n{name}  (type {wtype})  -- +{label}, {lo_amt} to {hi_amt} by weapon level")
        print(f"  {'lv':>4}  {'weapon':<28}{'slot':>6}{'bonus':>10}   kept in slot 1")
        for r, lv, stat_col, amt_col in sorted(rows, key=lambda x: x[1]):
            slot = 1 if stat_col == COL_BONUS0 else 2
            amt = want[r][WRITTEN.index(amt_col)]
            keep = want[r][0]
            kept = f"{ABILITY_NAME.get(keep, keep)}+{want[r][1]}" if keep and slot == 2 else "-"
            print(f"  {lv:>4}  {stb.get(r, COL_NAME).decode('utf-8', 'replace').strip()[:28]:<28}"
                  f"{slot:>6}{('+%d' % amt):>10}   {kept}")
        for r, lv, was_stat, was_amt in displaced:
            print(f"  note: lv{lv} {stb.get(r, COL_NAME).decode('utf-8', 'replace').strip()[:28]}"
                  f" had both slots full -- slot 2 displaced "
                  f"{ABILITY_NAME.get(was_stat, was_stat)} +{was_amt}")
        if not displaced:
            print("  (nothing displaced)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--profile", choices=sorted(PROFILES))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_stb_module()
    stb = oro.Stb(WEAPON_STB)
    active, vanilla = read_sidecar()

    if args.restore:
        if not vanilla:
            sys.exit("no sidecar -- nothing to restore")
        apply_values(stb, vanilla)
        with open(WEAPON_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print(f"restored {len(vanilla)} weapons to vanilla (was profile "
              f"{active!r}); sidecar removed")
        return

    if args.verify:
        if not active:
            sys.exit("no sidecar -- no profile is applied")
        want = target_state(stb, active, vanilla)
        bad = [(r, c, gi(stb, r, c), v)
               for r, vals in sorted(want.items())
               for c, v in zip(WRITTEN, vals) if gi(stb, r, c) != v]
        print(f"profile {active!r}: {len(want)} weapons, {len(bad)} columns do not match")
        for r, c, got, exp in bad:
            print(f"    row {r} col {c}: {got} != {exp}")
        sys.exit(1 if bad else 0)

    if not args.profile:
        print(f"active profile: {active!r}" if active else "no profile applied (vanilla)")
        print(f"available: {', '.join(sorted(PROFILES))}")
        print("pass --profile <name> to apply one, or --restore to go back to vanilla")
        return

    if active == args.profile:
        print(f"profile {args.profile!r} is already applied -- nothing to do.")
        return

    if active:
        print(f"switching profile {active!r} -> {args.profile!r}\n")
        apply_values(stb, vanilla)

    if not active:
        # Snapshot every row the profile could touch before anything is written.
        vanilla = {}
        for wtype in PROFILES[args.profile]:
            stat = PROFILES[args.profile][wtype][0]
            for r, _lv, _c, _a in candidates(stb, wtype, None, stat)[0]:
                vanilla[r] = tuple(gi(stb, r, c) for c in WRITTEN)
    want = target_state(stb, args.profile, vanilla)
    show(stb, args.profile, want, vanilla)
    apply_values(stb, want)

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    with open(WEAPON_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    write_sidecar(args.profile, vanilla)

    chk = oro.Stb(WEAPON_STB)
    for r, vals in want.items():
        for c, v in zip(WRITTEN, vals):
            if gi(chk, r, c) != v:
                sys.exit(f"verify failed: row {r} col {c} = {gi(chk, r, c)}, expected {v}")
    print(f"\ndone -- profile {args.profile!r} applied to {len(want)} weapons and verified. "
          f"Sidecar: {os.path.basename(SIDECAR)}")
    print("Restart the game server (it caches STBs at startup) and rebake the VFS. "
          "No client rebuild is needed -- this is data only.")


if __name__ == "__main__":
    main()
