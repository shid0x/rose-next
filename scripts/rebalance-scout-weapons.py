"""Make the bow itself the reason a Scout dodges.

    python scripts/rebalance-scout-weapons.py --profile quiver
    python scripts/rebalance-scout-weapons.py --restore

Same machinery and sidecar contract as `rebalance-raider-weapons.py`, which this
is deliberately a sibling of. The skill side of the Scout pass lives in
`rebalance-scout.py`.

Why this pass exists
--------------------
The Scout is supposed to be the high-dodge class. It is not, and it never has
been: **AVOID appears in exactly zero skills across the entire Hawker/Scout
tree** -- not a passive, not a buff, nowhere. Dodge comes only from DEX, via

    AVOID = (DEX + 10) * 0.8 + LEVEL * 0.5

which the Raider gets in identical measure from the identical stat. Two classes
advancing from the same base, both DEX-scaling, cannot express "one of these
dodges more" through a stat they share.

So the differentiator goes on the weapon, exactly as the katar/dual split did.
It costs no skill points, scales with weapon level, and makes the identity a
property of *holding a bow* rather than a button you remember to press.

Bows only -- crossbows are the Knight's
---------------------------------------
`rebalance-crossbow-knight.py` builds an entire kiting Knight around the
crossbow (271): Taunt Shot, Slow Shot, Range Bow Shot, CrossBow Mastery. Handing
crossbows the Scout's dodge bonus would buff that build for free and blur the
one weapon distinction the archer classes have.

Restricting to bows (231) also creates a real choice rather than a strictly
better option. Several Scout skills accept either weapon -- Holding Arrow,
Poison Arrow, Speed Shot, Sharpen Arrow are all `231,271` -- while Bow Mastery,
Triple Shot and Eagle Shot are bow-only. A Scout holding a crossbow therefore
already gives up its three best skills; now it gives up the dodge too, and gets
the Knight's crossbow toolkit in exchange. That is a build, not a mistake.

How much dodge
--------------
+8 at level 10 rising to +80 at level 230, interpolated on each bow's own level
requirement. Sized against the formula it feeds, not picked to look tidy:

    Get_SuccessRATE -> iSuccess * (atkHIT * 1.1 - defAVOID * 0.93 + rand(1..60)
                                   + 5 + atkLv * 0.2) / 80

AVOID is subtracted at 0.93 per point straight off the attacker's hit term, and
a low enough result falls through to the `iSuc < 20` branch in `Get_DAMAGE`,
which discards the hit outright. A DEX-built level-240 Scout sits near 450
AVOID, so +80 is roughly a fifth again -- large enough to feel, far short of a
wall, and it arrives gradually because it tracks weapon level.

Slot handling
-------------
Each weapon has two bonus-stat slots (cols 24 and 27). Take slot 1 when it is
free, else slot 2, and where both are taken **overwrite slot 2**, reporting what
was displaced. Bows are unusually clean here -- 38 of 40 have both slots empty:

    Fluctuator Bow (lv210)  HIT +50 / DEX +15   -- slot 2 displaced
    Akela Bow      (lv230)  ATK +10 / free      -- slot 2 taken

Only the Fluctuator loses anything, and DEX +15 is the smaller half of its own
pair. Note DEX also feeds AVOID, so even that displacement is partly refunded.

Skipping the full ones instead was the mistake the raider pass made first: the
two weapons with occupied slots are the *endgame* ones, so the curve would have
stopped at level 200 and the whole point with it.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WEAPON_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_WEAPON.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_WEAPON.scout-quiver.json")

# LIST_WEAPON.STB game columns.
COL_NAME = 0
COL_TYPE = 4            # 231 bow (271 crossbow is deliberately not touched)
COL_REQ_STAT0 = 19      # Required Stat 1 / Amount 1
COL_REQ_AMT0 = 20
COL_REQ_STAT1 = 21      # Required Stat 2 / Amount 2
COL_REQ_AMT1 = 22
COL_BONUS0 = 24         # Bonus Stat 1 / Amount 1   (col 23 is its condition)
COL_BONUS0_AMT = 25
COL_BONUS1 = 27         # Bonus Stat 2 / Amount 2   (col 26 is its condition)
COL_BONUS1_AMT = 28

WRITTEN = (COL_BONUS0, COL_BONUS0_AMT, COL_BONUS1, COL_BONUS1_AMT)

BOW = 231

# t_AbilityINDEX, src/common/shared/datatype.h.
AT_LEVEL = 31
AT_AVOID = 22

ABILITY_NAME = {10: "STR", 11: "DEX", 12: "INT", 13: "CON", 15: "SENSE",
                18: "ATK", 19: "DEF", 20: "HIT", 21: "RES", 22: "AVOID",
                24: "ATKSPD", 26: "CRITICAL"}

# profile -> weapon type -> (bonus stat, amount at the lowest level, amount at
# the highest, label). Amounts are interpolated against each weapon's own level
# requirement, between the lowest and highest found for that type.
PROFILES = {
    "quiver": {
        BOW: (AT_AVOID, 8, 80, "dodge"),
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

    Returns (rows, displaced) so an overwrite is visible in --dry-run rather
    than silent.
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
        # Reading live data makes this non-idempotent: once slot 1 has been
        # written, a re-read sees it occupied and picks slot 2 instead, so
        # --verify computes a different target than --profile just applied.
        b0, _a0, b1, a1 = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
        if stat and b0 == stat:
            # Slot 1 already grants the very stat we are adding -- raise it in
            # place rather than writing a second entry of the same stat into
            # slot 2, which would show as two identical tooltip lines. No bow
            # currently hits this; kept so the two weapon passes stay identical.
            rows.append((r, lv, COL_BONUS0, COL_BONUS0_AMT))
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
        print(f"\nBOW  (type {wtype})  -- +{label}, {lo_amt} to {hi_amt} by weapon level")
        print(f"  {'lv':>4}  {'weapon':<28}{'slot':>6}{'bonus':>10}")
        for r, lv, stat_col, amt_col in sorted(rows, key=lambda x: x[1]):
            slot = 1 if stat_col == COL_BONUS0 else 2
            amt = want[r][WRITTEN.index(amt_col)]
            print(f"  {lv:>4}  {stb.get(r, COL_NAME).decode('utf-8', 'replace').strip()[:28]:<28}"
                  f"{slot:>6}{('+%d' % amt):>10}")
        for r, lv, was_stat, was_amt in displaced:
            print(f"  note: lv{lv} {stb.get(r, COL_NAME).decode('utf-8', 'replace').strip()[:28]}"
                  f" had both slots full -- slot 2 displaced "
                  f"{ABILITY_NAME.get(was_stat, was_stat)} +{was_amt}")


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
        print(f"restored {len(vanilla)} bows to vanilla (was profile "
              f"{active!r}); sidecar removed")
        return

    if args.verify:
        if not active:
            sys.exit("no sidecar -- no profile is applied")
        want = target_state(stb, active, vanilla)
        bad = [(r, c, gi(stb, r, c), v)
               for r, vals in sorted(want.items())
               for c, v in zip(WRITTEN, vals) if gi(stb, r, c) != v]
        print(f"profile {active!r}: {len(want)} bows, {len(bad)} columns do not match")
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
    print(f"\ndone -- profile {args.profile!r} applied to {len(want)} bows and verified. "
          f"Sidecar: {os.path.basename(SIDECAR)}")
    print("Restart the game server (it caches STBs at startup) and rebake the VFS. "
          "No client rebuild is needed -- this is data only.")


if __name__ == "__main__":
    main()
