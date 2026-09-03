"""Give the Mage the one spell that ends things, and bring its other two onto the curve.

    python scripts/rebalance-mage.py --profile archmage
    python scripts/rebalance-mage.py --restore

Same machinery and sidecar contract as the other class passes. The Muse base
half is `rebalance-muse.py`; the staff/wand half is `rebalance-muse-weapons.py`.

Why this pass exists
--------------------
The Muse pass fixed the families a *Muse* owns. Three damage skills belong to
the Mage alone and were untouched by it, so they sat at the numbers everything
else has already been lifted off:

    Freezing Bolt (fixed)   68 power/second      <- the spam button
    Magma Burn              25
    Ice Bang                15
    Luna Stone              11                   <- the worst rate in the class

**The Mage had no big spell.** Its biggest hit was Lightening Shock at 1600
damage, which is its *filler*. Every other line owns something that ends a
fight -- Prime Hit, Champion Hit, Flame Hawk, Eagle Shot -- and the Mage's
equivalent was a button nobody pressed.

Luna Stone is that spell, and it already had every attribute of one
-------------------------------------------------------------------
Nothing here is invented. Before this pass Luna Stone had:

    29 s cooldown       the longest in the class by 2x (next is 14.4)
    215 MP              the most expensive spell the Mage owns
    1300 area           a 13 m ground-targeted blast at 20 m range
    no status, no debuff -- pure damage, which is exactly what a nuke should be
    casting animation 117, the long cast, not the bolts' 101
    a capstone tree slot: the terminal node of Wind Storm -> Tornado (rank 6+),
    the deepest single investment in the Muse tree

It cost the most, waited the longest, and did the least. The only thing missing
was the damage.

There are two capstones and they split cleanly, so both keep a job:

    Luna Stone (1091)   pure damage, no rider   -> the nuke
    Ice Bang   (1101)   area + 50% slow         -> the control blast

What changed
------------
Cooldowns first (rank 1 is never made worse, and all three regressed with rank),
then power solved against the level-200 reference the Muse pass established --
Freezing Bolt at 1389 damage per cast and 183 damage/second:

    Luna Stone       330 -> 1150   cd 20.0 -> 18.0    3060 per cast, 170 dmg/s
    Magma Burn       360 ->  503   cd 10.0 ->  9.0    1350 per cast, 150 dmg/s
    Ice Bang         240 ->  492   cd 14.0 -> 12.0    1320 per cast, 110 dmg/s

**Luna Stone's per-cast number is the point**: 3060 is 2.2x a Freezing Bolt hit
and 1.8x a Scout's Eagle Shot -- comfortably the biggest thing a Mage has ever
put on screen.

**Its damage per second is deliberately *below* the bolt's** (170 against 183).
That is what stops it becoming the only button: you cannot win by pressing the
nuke alone, so it is the opener you lead with and the answer to a group, while
the bolt is what you actually hold down. It also hits everything in 13 m, so on
three or more targets it is far ahead on raw output -- the single-target
discount is what pays for that.

MP goes 215 -> 300 at rank 10. The nuke should hurt to cast; mana is the Mage's
real limiter and this is where it should bite hardest.

Magma Burn and Ice Bang keep their riders and are sized under the bolt to match:
Magma Burn strips 20% defence, Ice Bang slows 50% across an area, and neither
should out-damage the skill that does nothing but damage.

What is deliberately left alone
-------------------------------
**Freezing** (1001) and **Silence** (1071) -- pure utility, flat cooldowns,
nothing wrong with them.

**Meteorite Strike** (941), for the same reason as in the Muse pass: it is
`SKILL_TYPE` 3 with `SKILL_DAMAGE_TYPE` **0**, so it never reaches the magic
branch and cannot be sized against these. It needs its own investigation.

**Nothing about the Mage's frailty.** The brief is "extremely frail, good magic
resistance" and the sim puts a Mage at 0.91x a Scout's HP, which is not frail at
all. That is an armour/HP/RES question and it is not a skill table.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKILL_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.mage.json")

# Game column indices, from src/common/io_skill.h.
COL_NAME = 0
COL_FAMILY = 1      # SKILL_1LEV_INDEX, stable across a mid-curve rename
COL_LEVEL = 2       # SKILL_LEVEL, the rank within the family
COL_POWER = 9       # SKILL_POWER
COL_COST = 17       # SKILL_USE_VALUE(s, 0); the property is col 16, AT_MP here
COL_RELOAD = 20     # SKILL_RELOAD_TIME, x 0.2 s

WRITTEN = (COL_POWER, COL_RELOAD, COL_COST)

# base row -> (label, family, first rank, rank count).
FAMILIES = {
    1061: ("Fire Burn / Magma Burn", 1061, 1, 10),
    1091: ("Luna Stone", 1091, 1, 10),
    1101: ("Ice Bang", 1101, 1, 10),
}


def lin(a, b, n=10):
    """Linear rank ramp, rounded."""
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


def cd(a, b, n=10):
    """Cooldown ramp in SECONDS -> SKILL_RELOAD_TIME units (x0.2 s)."""
    return [round(v / 0.2) for v in (a + (b - a) * k / (n - 1) for k in range(n))]


# profile -> base row -> (power, reload, mp). None leaves that column vanilla.
PROFILES = {
    "archmage": {
        # The nuke. 3060 per cast at level 200 -- 2.2x a Freezing Bolt hit --
        # but 170 dmg/s against the bolt's 183, so it cannot become the only
        # button. Expensive on purpose.
        1091: (lin(558, 1150), cd(20.0, 18.0), lin(150, 300)),

        # Bolt carrying a 20% defence strip. Sized under the bolt that carries
        # nothing.
        1061: (lin(252, 503), cd(10.0, 9.0), lin(90, 180)),

        # Area plus a 50% slow. The control blast, so the lowest of the three.
        1101: (lin(308, 492), cd(14.0, 12.0), lin(120, 190)),
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


def rows_for(stb, base):
    """The rank rows of one family, checked against what we expect to find."""
    label, family, first, n = FAMILIES[base]
    out = []
    for i in range(n):
        r = base + i
        got = gi(stb, r, COL_FAMILY)
        if got != family:
            sys.exit(f"row {r} ({label}): expected family {family}, found {got} -- "
                     f"LIST_SKILL.STB has moved; refusing to write")
        if gi(stb, r, COL_LEVEL) != first + i:
            sys.exit(f"row {r} ({label}): expected rank {first + i}, found "
                     f"{gi(stb, r, COL_LEVEL)} -- refusing to write")
        out.append(r)
    return out


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


def target_state(stb, profile, vanilla):
    want = {}
    for base, curves in PROFILES[profile].items():
        for n, r in enumerate(rows_for(stb, base)):
            base_vals = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            want[r] = tuple(curve[n] if curve else base_vals[i]
                            for i, curve in enumerate(curves))
    return want


def apply_values(stb, values):
    for r, vals in values.items():
        for c, v in zip(WRITTEN, vals):
            stb.set(r, c, str(v) if v else "")


def show(stb, profile, want, vanilla):
    for base in sorted(PROFILES[profile]):
        rows = rows_for(stb, base)
        label, _f, first, _n = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]})")
        # pow/s is SKILL_POWER over cooldown -- an internal consistency check
        # only. Magic damage is NOT power/cd, so this is not comparable with a
        # physical skill. The solved damage figures are in the docstring;
        # re-derive them with scripts/balance-sim.py.
        print(f"  {'rank':>5}{'power':>18}{'cooldown s':>20}{'MP':>16}{'pow/s':>9}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            now = want[r]

            def fmt(i, scale=1.0):
                a, b = was[i] * scale, now[i] * scale
                if not a and not b:
                    return "-"
                if a == b:
                    return f"{b:g} ="
                return f"{a:g} -> {b:g}"
            rate = f"{now[0] / (now[1] * 0.2):.1f}" if (now[0] and now[1]) else "-"
            print(f"  {first + n:>5}{fmt(0):>18}{fmt(1, 0.2):>20}{fmt(2):>16}{rate:>9}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--profile", choices=sorted(PROFILES))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_stb_module()
    stb = oro.Stb(SKILL_STB)
    active, vanilla = read_sidecar()

    if args.restore:
        if not vanilla:
            sys.exit("no sidecar -- nothing to restore")
        apply_values(stb, vanilla)
        with open(SKILL_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print(f"restored {len(vanilla)} rows to vanilla (was profile "
              f"{active!r}); sidecar removed")
        return

    if args.verify:
        if not active:
            sys.exit("no sidecar -- no profile is applied")
        want = target_state(stb, active, vanilla)
        bad = [(r, c, gi(stb, r, c), v)
               for r, vals in sorted(want.items())
               for c, v in zip(WRITTEN, vals) if gi(stb, r, c) != v]
        print(f"profile {active!r}: {len(want)} rows, {len(bad)} columns do not match")
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
    else:
        vanilla = {r: tuple(gi(stb, r, c) for c in WRITTEN)
                   for base in PROFILES[args.profile] for r in rows_for(stb, base)}

    want = target_state(stb, args.profile, vanilla)
    show(stb, args.profile, want, vanilla)
    apply_values(stb, want)

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    with open(SKILL_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    write_sidecar(args.profile, vanilla)

    chk = oro.Stb(SKILL_STB)
    for r, vals in want.items():
        for c, v in zip(WRITTEN, vals):
            if gi(chk, r, c) != v:
                sys.exit(f"verify failed: row {r} col {c} = {gi(chk, r, c)}, expected {v}")
    print(f"\ndone -- profile {args.profile!r} applied to {len(want)} rows and verified. "
          f"Sidecar: {os.path.basename(SIDECAR)}")
    print("Restart the game server (it caches STBs at startup) and rebake the VFS. "
          "No client rebuild is needed -- this is data only.")


if __name__ == "__main__":
    main()
