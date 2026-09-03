"""Give katars crit and dual wields raw attack, as weapon bonus stats.

    python scripts/rebalance-raider-weapons.py --profile edge
    python scripts/rebalance-raider-weapons.py --restore

Companion to `rebalance-raider.py`, which splits the *skills* by weapon. This
one makes the two weapon types feel different in the hand.

Why this and not a passive skill
--------------------------------
The first attempt at this added two ability indices and weapon-aware reads in
`Cal_CRITICAL` / `GetPassiveSkillAttackPower`, because retail's weapon switches
fold katar (251) and dual (252) into one shared index and genuinely cannot tell
them apart. That worked, and it was the wrong tool. It was reverted.

`LIST_WEAPON.STB` already carries two bonus-stat slots per weapon, the engine
already reads them, and **the shipped data already uses exactly these two stats
on exactly these two weapon types** -- the Zamadar katar grants +35 CRITICAL and
the Firangi-Firangi dual grants +30 ATK. So the mechanism needed no code at all.

It is also better on three counts:

* **Visible.** A bonus stat shows in the item tooltip. A passive is invisible --
  the player's crit would change with no explanation anywhere in the UI.
* **It scales with level for free.** A flat crit bonus is worth far more at
  level 100 than at 230, because `Get_CriSuccessRATE` subtracts the attacker's
  level. Putting it on the weapon makes the bonus track gear tier, which tracks
  level, with no mechanism to build.
* **No paired deploy.** No enum, no class-layout change, no rebuild. A server
  restart and a re-bake.

The one thing the passive could do that this cannot is scale with *skill rank*
instead of gear tier. Gear tier is the better axis anyway.

What it does
------------
    KATAR (type 251)  +CRITICAL,  +8 at level 10  ->  +75 at level 230
    DUAL  (type 252)  +ATK,       +6 at level 30  ->  +90 at level 230

Scaled linearly against each weapon's own level requirement (the AT_LEVEL entry
in its required-stat columns), so a starter weapon grants a token amount and the
endgame ones carry the real bonus.

Why those numbers, and what they actually buy
---------------------------------------------
Crit chance in this game is much harder to reach than it looks. Crit fires when
`((1+rand(100))*3 + LEVEL + 30) * 16 / (CRITICAL + 70) < 20`, so the attacker's
own level works against them, and `CRITICAL = SENSE + (CON+20)*0.2` -- while
dodge comes from DEX, and the endgame katar itself demands 335 DEX.

Measured at level 230, katar bonus against a player's base CRITICAL:

    katar +      pure DEX build (base 76)      some SENSE (base 150)
      0                 0%                            4%
     30                 0%                           17%
     60                 0%                           29%
     75                ~5%                          ~35%
    120                24%                           54%

**So the weapon does not hand anyone the crit build -- it makes investing in
SENSE worth doing.** That is the intended design: the katar gets you to the
threshold, your stat spread decides whether you cross it. Before this, SENSE was
a dead stat for the whole line.

+75 is a little over twice the largest bonus in the shipped data (+35). That is
deliberate and it is the main number to argue with after testing.

The other half of the katar bonus is quieter and worth knowing: CRITICAL is also
a **linear damage term** in the type-0 skill branch, appearing twice --
`(SKILL_POWER + CRITICAL*0.15 + 40)` and `(rand + CRITICAL*0.32 + 35)`. Double
Attack and Triple Attack are type 0, so +75 is worth roughly **+33% damage on
those two regardless of whether anything crits**. Power Burst, Prime Hit and
Screw Attack are type 1 (weapon) and never read CRITICAL, so they are unaffected.

The dual's +90 ATK is the counterweight, and it works differently: it is smaller
in percentage terms (~7% of a level-230 attack power) but applies to
*everything*, including its own 1150-power Prime Hit. Katar therefore leans
towards sustained multi-hit output, dual towards raw and burst -- which is the
shape the class brief asked for.

This is a first pass sized to be tested, not a proven balance.

Slots and safety
----------------
Each weapon has two bonus-stat slots (`Bonus Stat 1/2`, cols 24 and 27). The
pass takes slot 1 where it is free, falls back to slot 2, and where both are
taken **overwrites slot 2**, reporting what it displaced.

Skipping full weapons was the first attempt and it quietly gutted the design:
the two highest-level weapons of each type are exactly the ones with both slots
full, so the endgame weapons this hinges on would have been the only ones left
out and the curve would have stopped at level 200. Slot 2 is the safe one to
take -- across these weapons slot 1 holds the load-bearing bonuses (ATK +30,
HIT +40) and slot 2 a small stat trickle (DEX +10/+15, STR +15). The
"Assigned condition" columns are left blank, as on every shipped bonus.

Weapons with no level requirement are skipped: there is nothing to scale
against, and they are the starter items.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WEAPON_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_WEAPON.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_WEAPON.raider-edge.json")

# LIST_WEAPON.STB game columns.
COL_NAME = 0
COL_TYPE = 4            # 251 katar, 252 dual wield
COL_REQ_STAT0 = 19      # Required Stat 1 / Amount 1
COL_REQ_AMT0 = 20
COL_REQ_STAT1 = 21      # Required Stat 2 / Amount 2
COL_REQ_AMT1 = 22
COL_BONUS0 = 24         # Bonus Stat 1 / Amount 1   (col 23 is its condition)
COL_BONUS0_AMT = 25
COL_BONUS1 = 27         # Bonus Stat 2 / Amount 2   (col 26 is its condition)
COL_BONUS1_AMT = 28

WRITTEN = (COL_BONUS0, COL_BONUS0_AMT, COL_BONUS1, COL_BONUS1_AMT)

KATAR, DUAL = 251, 252

# t_AbilityINDEX, src/common/shared/datatype.h.
AT_LEVEL = 31
AT_ATK = 18
AT_CRITICAL = 26

ABILITY_NAME = {10: "STR", 11: "DEX", 18: "ATK", 20: "HIT",
                22: "AVOID", 24: "ATKSPD", 26: "CRITICAL"}

# profile -> weapon type -> (bonus stat, amount at the lowest level, amount at
# the highest, label). Amounts are interpolated against each weapon's own level
# requirement, between the lowest and highest found for that type.
PROFILES = {
    "edge": {
        KATAR: (AT_CRITICAL, 8, 75, "crit"),
        DUAL: (AT_ATK, 6, 90, "attack"),
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

    Prefer a free slot. When both are taken, **overwrite slot 2** rather than
    skipping the weapon.

    Skipping was the first attempt and it quietly gutted the design: the two
    highest-level weapons of each type -- Akela Katar and Juxtapose Katar, Dual
    Viper Blades and Arcidian Dual Hand -- are precisely the ones with both
    slots full, so the endgame weapons the whole split hinges on would have been
    the only ones to get nothing, and the curve would have stopped at level 200.

    Slot 2 is the one to take. Across these weapons slot 1 carries the load-
    bearing bonuses (ATK +30, HIT +40) while slot 2 carries a small stat trickle
    (DEX +10, DEX +15, STR +15). Displacing 10-15 points of a base stat is a far
    smaller loss than displacing +40 HIT, which feeds the accuracy gate and is
    deliberately kept flat everywhere else in the data.

    Returns (rows, displaced) where displaced lists what each override costs, so
    it is visible in --dry-run rather than silent.
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
            # Slot 1 already grants the very stat we are adding -- raise it
            # rather than writing a second entry of the same stat into slot 2,
            # which would show as two identical lines in the item tooltip.
            # One weapon hits this: Dual Viper Blades, whose slot 1 is ATK +30.
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
            vals = list(tuple(gi(stb, r, c) for c in WRITTEN))
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
        name = "KATAR" if wtype == KATAR else "DUAL WIELD"
        print(f"\n{name}  (type {wtype})  -- +{label}, {lo_amt} to {hi_amt} by weapon level")
        print(f"  {'lv':>4}  {'weapon':<26}{'slot':>6}{'bonus':>10}")
        for r, lv, stat_col, amt_col in sorted(rows, key=lambda x: x[1]):
            slot = 1 if stat_col == COL_BONUS0 else 2
            amt = want[r][WRITTEN.index(amt_col)]
            print(f"  {lv:>4}  {stb.get(r, COL_NAME).decode('utf-8', 'replace').strip()[:26]:<26}"
                  f"{slot:>6}{('+%d' % amt):>10}")
        for r, lv, was_stat, was_amt in displaced:
            print(f"  note: lv{lv} {stb.get(r, COL_NAME).decode('utf-8', 'replace').strip()[:26]}"
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
