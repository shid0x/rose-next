"""Put the Muse line on the damage curve it was never on, and give the Mage back
what a class that no longer exists was holding.

    python scripts/rebalance-muse.py --profile arcane
    python scripts/rebalance-muse.py --restore

Same machinery and sidecar contract as the other class passes. The staff/wand
half of this lives in `rebalance-muse-weapons.py`.

Why this pass exists
--------------------
Three problems, and only the third is the usual one.

**1. Magic does not scale. That is structural, not tuning.**
`Get_SkillDAMAGE` (`src/common/calculation.cpp`) uses a different shape per
damage type:

    physical (type 1)   (POWER + ATK*0.2) * (ATK + 60)        quadratic in ATK
    magic    (type 2)   POWER * (ATK*0.8 + INT*1.2 + 100)     linear

A physical class's damage compounds with its own growth; a magic class's only
adds to it. And `GameStaticConfig::MAX_STAT` is **300**, which a Mage's INT
reaches around level 200 -- so its primary damage stat *stops* while a Scout's
ATK keeps climbing out of weapons. After level 200 the magic bracket moves 8%
across forty levels.

Replayed through `scripts/balance-sim.py` against the median field monster,
best skill each side:

    level      magic     physical    ratio
       80       61.5        137.1     0.45
      200       59.3        192.5     0.31
      240       47.9        175.9     0.27

Magic damage per second is **flat-to-falling across 160 levels**. The line was
never behind; it was never on the curve.

**2. Twelve families were amputated by a class that is out of the game.** Their
upper ranks are gated to the Cleric (col 35 re-declared mid-curve --
[[reference-skill-class-gates-per-rank]]), so with no Cleric a Mage is capped at
rank 2 on Blessing Mind, 4 on Blessing Armor, 5 on Healing/Cure/Support/the
summons, and 10 of 20 on Meditation.

**3. The usual cooldown regression.** Mana Bolt 4.2 -> 6.8 s, Ice Bolt
8.4 -> 11.2, Lightening 12.0 -> 17.6, Wind Storm 14.0 -> 17.6, and Blessing
Armor 50 -> 68 s against a 30 -> 45 s duration.

What changed
------------
**Damage, sized by solving for a target rather than by eye.** Cooldowns first
(rank 1 is never made worse, and every curve now improves with rank), then
`balance-sim.py` solved the power that reaches the physical single-hit band --
Eagle Shot 170, Power Attack 192, Poison Arrow 177 dmg/s -- at **level 200**:

    Ice Bolt / Freezing Bolt   250 -> 518   cd  8.4 -> 7.5     185 dmg/s
    Mana Bolt / Mana Spear     160 -> 240   cd  4.2 -> 3.8     172
    Lightening Shock           200 -> 598   cd 12.0 -> 10.0    160
    Wind Storm / Tornado       200 -> 501   cd 14.0 -> 12.0    112  (area)
    Curse                      110 -> 159   cd  4.0 ->  3.6    122

The area skill and Curse sit below the band on purpose -- Tornado hits a group,
and Curse is the cheapest thing the line owns at a 3.6 s cooldown.

Parity is set at **level 200, not 240**, deliberately. 200 -> 240 stays flat
because that is the INT cap, and no amount of SKILL_POWER fixes a stat that has
stopped -- propping the endgame up with power would overshoot every level below
it. If the flatline turns out to matter in play, the honest fix is the formula,
not this table.

**Three gates re-pointed from Cleric (64) to Mage (63)** -- one cell each, since
the gate is declared once at the transition rank:

    row 926  Healing     rank 6     a Mage was capped at rank 5
    row 936  Cure        rank 6     capped at rank 5
    row 831  Meditation  rank 11    capped at rank 10 of 20

**The buffs are deliberately NOT re-pointed.** Support, Power Support, Blessing
Body/Armor/Mind stay Cleric-gated even though nothing can reach them, because
rebuilding a class whose job is buffing everyone else is the thing this whole
rebalance exists to avoid. Same for the three summons (ButterFly, BoneFire,
Phantom Sword) -- those are the material a future summoner Cleric would be built
from, and handing them out now means taking them back later. Meditation is in
because it is a self *passive* (MP pool and regen), not a buff.

**Heals raised, and kept flat on purpose.** They decayed exactly like the old
flat buffs: Cure covers 106% of a bar at level 50 and 27% at 240.

    Cure     740 -> 1000 at rank 10       Healing  520 -> 700 (area)

They must stay in the flat column: `Get_SkillAdjustVALUE` resolves `AT_HP` to
*current* HP, so a percentage heal scales with what you have left and is
worthless exactly when it is needed. MP costs rise with them so mana stays the
limiter.

**Blessing Armor's cooldown** goes 50 -> 68 s down to 28 -> 24 s, against a
30 -> 45 s duration. It is still Cleric-gated at rank 5, so only ranks 1-4 are
reachable -- but those four were unmaintainable too, which is a live bug for
anyone playing a Muse today.

What is deliberately left alone
-------------------------------
**Fire Ring and Mild** -- debuffs, no damage, flat cooldowns, both fine.

**Meteorite Strike** (941). It is `SKILL_TYPE` 3 (attack-mode change) with
`SKILL_DAMAGE_TYPE` **0**, so it does not go through the magic branch at all and
cannot be sized against the others. Its cooldown already improves with rank
(10.0 -> 6.0 s). It needs its own investigation, not a number nudged blind.

**Summon Mastery** (861) is dead data and is left as such. It grants
`AT_PSV_SUMMON_MOB_CNT`, and `GetMax_SummonCNT()` (`cuserdata.h`) is computed
and reported to the login server (`gs_socketlsv.cpp`) but **never compared
against the running count** before summoning -- `Skill_START` case
`SKILL_TYPE_14` just calls `Add_SummonCNT` unconditionally. The passive raises a
cap nobody enforces. It belongs with the summons, so it waits for them.

**Curse's ranks 6-10** are written but unreachable, since it keeps its Cleric
gate. Ranks 1-5 improve, and the curve is correct for whenever that class
returns.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKILL_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.muse.json")

# Game column indices, from src/common/io_skill.h.
COL_NAME = 0
COL_FAMILY = 1      # SKILL_1LEV_INDEX, stable across a mid-curve rename
COL_LEVEL = 2       # SKILL_LEVEL, the rank within the family
COL_POWER = 9       # SKILL_POWER
COL_COST = 17       # SKILL_USE_VALUE(s, 0); the property is col 16, AT_MP here
COL_RELOAD = 20     # SKILL_RELOAD_TIME, x 0.2 s
COL_AB1_VALUE = 22  # SKILL_INCREASE_ABILITY_VALUE(s, 0) -- the flat heal
COL_CLASS = 35      # SKILL_AVAILBLE_CLASS_SET, re-declared per rank

WRITTEN = (COL_POWER, COL_RELOAD, COL_COST, COL_AB1_VALUE)

MUSE, MAGE, CLERIC = 42, 63, 64

# base row -> (label, family, first rank, rank count).
FAMILIES = {
    821:  ("Meditation", 821, 1, 20),
    901:  ("Mana Bolt / Mana Spear", 901, 1, 10),
    921:  ("Healing", 921, 1, 10),
    931:  ("Cure", 931, 1, 10),
    951:  ("Lightening / Lightening Shock", 951, 1, 10),
    981:  ("Ice Bolt / Freezing Bolt", 981, 1, 10),
    991:  ("Blessing Armor", 991, 1, 10),
    1081: ("Wind Storm / Tornado", 1081, 1, 10),
    1141: ("Curse", 1141, 1, 10),
}


def lin(a, b, n=10):
    """Linear rank ramp, rounded."""
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


def cd(a, b, n=10):
    """Cooldown ramp in SECONDS -> SKILL_RELOAD_TIME units (x0.2 s)."""
    return [round(v / 0.2) for v in (a + (b - a) * k / (n - 1) for k in range(n))]


# profile -> base row -> (power, reload, mp, ability1-value).
# None leaves that column vanilla.
PROFILES = {
    "arcane": {
        # --- damage: cooldowns stop regressing, power solved for level 200 ----
        981:  (lin(125, 518), cd(8.4, 7.5),  lin(30, 120), None),   # 185 dmg/s
        901:  (lin(60, 240),  cd(4.2, 3.8),  lin(20, 70),  None),   # 172
        951:  (lin(89, 598),  cd(12.0, 10.0), lin(30, 110), None),  # 160
        1081: (lin(151, 501), cd(14.0, 12.0), lin(50, 130), None),  # 112, area
        1141: (lin(43, 159),  cd(4.0, 3.6),  lin(15, 40),  None),   # 122

        # --- heals: flat values raised so they stop decaying with level -------
        931:  (None, None, lin(30, 100), lin(280, 1000)),  # Cure, single target
        921:  (None, None, lin(35, 110), lin(220, 700)),   # Healing, area

        # --- cooldown only: a buff whose cooldown exceeded its own duration ---
        991:  (None, cd(28.0, 24.0), None, None),
    },
}

# profile -> row -> new SKILL_AVAILBLE_CLASS_SET. One cell per family, at the
# rank where the gate is declared.
REGATE = {
    "arcane": {
        926: MAGE,   # Healing    rank 6
        936: MAGE,   # Cure       rank 6
        831: MAGE,   # Meditation rank 11
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


def gs(stb, r, c):
    return stb.get(r, c).strip().decode("utf-8", "replace")


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


def check_gates(stb, profile):
    """The rows we re-point must currently be declared CLERIC, and nothing else."""
    for r in sorted(REGATE[profile]):
        got = gi(stb, r, COL_CLASS)
        if got not in (CLERIC, REGATE[profile][r]):
            sys.exit(f"row {r} ({gs(stb, r, COL_NAME)}): class gate is {got}, expected "
                     f"{CLERIC} (Cleric) -- refusing to write")


def read_sidecar():
    if not os.path.exists(SIDECAR):
        return None, {}, {}
    with open(SIDECAR, encoding="utf-8") as fh:
        raw = json.load(fh)
    return (raw["profile"],
            {int(k): tuple(v) for k, v in raw["rows"].items()},
            {int(k): v for k, v in raw.get("gates", {}).items()})


def write_sidecar(profile, rows, gates):
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"profile": profile,
                   "rows": {str(k): list(v) for k, v in sorted(rows.items())},
                   "gates": {str(k): v for k, v in sorted(gates.items())}},
                  fh, indent=1)


def target_state(stb, profile, vanilla):
    want = {}
    for base, curves in PROFILES[profile].items():
        for n, r in enumerate(rows_for(stb, base)):
            base_vals = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            want[r] = tuple(curve[n] if curve else base_vals[i]
                            for i, curve in enumerate(curves))
    return want


def apply_values(stb, values, cols=WRITTEN):
    for r, vals in values.items():
        for c, v in zip(cols, vals):
            stb.set(r, c, str(v) if v else "")


def apply_gates(stb, gates):
    for r, v in gates.items():
        stb.set(r, COL_CLASS, str(v) if v else "")


CLS = {MUSE: "Muse", MAGE: "Mage", CLERIC: "Cleric", 0: "-"}


def show(stb, profile, want, vanilla, gates, gate_vanilla):
    for base in sorted(PROFILES[profile]):
        rows = rows_for(stb, base)
        label, _f, first, _n = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]})")
        print(f"  {'rank':>5}{'power':>16}{'cooldown s':>20}{'MP':>14}"
              f"{'heal':>16}{'pow/s':>9}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            now = want[r]
            def fmt(i, scale=1.0, suffix=""):
                a, b = was[i] * scale, now[i] * scale
                if not a and not b:
                    return "-"
                if a == b:
                    return f"{b:g}{suffix} ="
                return f"{a:g} -> {b:g}{suffix}"
            dps = f"{now[0] / (now[1] * 0.2):.1f}" if (now[0] and now[1]) else "-"
            print(f"  {first + n:>5}{fmt(0):>16}{fmt(1, 0.2):>20}{fmt(2):>14}"
                  f"{fmt(3):>16}{dps:>9}")

    print("\nClass gates re-pointed  (one cell per family, at the transition rank)")
    print(f"  {'row':>5}  {'skill':<24}{'rank':>6}{'from':>10}{'to':>10}")
    for r in sorted(gates):
        was = gate_vanilla.get(r, gi(stb, r, COL_CLASS))
        print(f"  {r:>5}  {gs(stb, r, COL_NAME)[:24]:<24}{gi(stb, r, COL_LEVEL):>6}"
              f"{CLS.get(was, was):>10}{CLS.get(gates[r], gates[r]):>10}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--profile", choices=sorted(PROFILES))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_stb_module()
    stb = oro.Stb(SKILL_STB)
    active, vanilla, gate_vanilla = read_sidecar()

    if args.restore:
        if not vanilla:
            sys.exit("no sidecar -- nothing to restore")
        apply_values(stb, vanilla)
        apply_gates(stb, gate_vanilla)
        with open(SKILL_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print(f"restored {len(vanilla)} rows and {len(gate_vanilla)} class gates to "
              f"vanilla (was profile {active!r}); sidecar removed")
        return

    if args.verify:
        if not active:
            sys.exit("no sidecar -- no profile is applied")
        want = target_state(stb, active, vanilla)
        bad = [(r, c, gi(stb, r, c), v)
               for r, vals in sorted(want.items())
               for c, v in zip(WRITTEN, vals) if gi(stb, r, c) != v]
        bad += [(r, COL_CLASS, gi(stb, r, COL_CLASS), v)
                for r, v in sorted(REGATE[active].items()) if gi(stb, r, COL_CLASS) != v]
        print(f"profile {active!r}: {len(want)} rows + {len(REGATE[active])} gates, "
              f"{len(bad)} columns do not match")
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
        apply_gates(stb, gate_vanilla)
    else:
        vanilla = {r: tuple(gi(stb, r, c) for c in WRITTEN)
                   for base in PROFILES[args.profile] for r in rows_for(stb, base)}
        gate_vanilla = {r: gi(stb, r, COL_CLASS) for r in REGATE[args.profile]}

    check_gates(stb, args.profile)
    want = target_state(stb, args.profile, vanilla)
    show(stb, args.profile, want, vanilla, REGATE[args.profile], gate_vanilla)
    apply_values(stb, want)
    apply_gates(stb, REGATE[args.profile])

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    with open(SKILL_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    write_sidecar(args.profile, vanilla, gate_vanilla)

    chk = oro.Stb(SKILL_STB)
    for r, vals in want.items():
        for c, v in zip(WRITTEN, vals):
            if gi(chk, r, c) != v:
                sys.exit(f"verify failed: row {r} col {c} = {gi(chk, r, c)}, expected {v}")
    for r, v in REGATE[args.profile].items():
        if gi(chk, r, COL_CLASS) != v:
            sys.exit(f"verify failed: row {r} gate = {gi(chk, r, COL_CLASS)}, expected {v}")
    print(f"\ndone -- profile {args.profile!r} applied to {len(want)} rows and "
          f"{len(REGATE[args.profile])} gates, and verified. "
          f"Sidecar: {os.path.basename(SIDECAR)}")
    print("Restart the game server (it caches STBs at startup) and rebake the VFS. "
          "No client rebuild is needed -- this is data only.")


if __name__ == "__main__":
    main()
