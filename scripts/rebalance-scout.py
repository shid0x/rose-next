"""Make the Scout advancement an upgrade instead of a downgrade.

    python scripts/rebalance-scout.py --profile marksman
    python scripts/rebalance-scout.py --restore

Same machinery and sidecar contract as the other class passes. The dodge half of
the Scout identity lives in `rebalance-scout-weapons.py`.

Why this pass exists
--------------------
The Scout's five exclusive skills are the weakest set in the game, and advancing
into the class is currently a *downgrade* at everything except Triple Shot.
Measured at rank 10, effective damage per second (SKILL_POWER x the animation's
attack-frame count, over the cooldown):

    Scout-only          Poison Arrow      14.1     <- worst in the game
                        Eagle Shot        19.5
    inherited           Triple Shot      131.4
                        Power Attack      66.7
                        Aim Shot          60.0
                        Flame Hawk        55.7
                        Spirit Heart      53.8
                        Holding Arrow     51.4
                        Spiral Kick       38.2

Worse, **three of the five get slower as you rank them** -- the retail-wide quirk
the earlier passes stripped out everywhere else. Ranking Eagle Shot costs 67%
cooldown (9.2 -> 15.4 s) to buy 3x power; ranking Poison Arrow costs 30%
cooldown to buy 83% power on a base of *sixty*. Sharpen Arrow's cooldown grows
from 80 s to 92.6 s against a duration of 50 -> 70 s, so it is unmaintainable at
every rank and ranking it never closes the gap.

What changed
------------
    Eagle Shot     220 -> 750 power, cd 11.0 -> 10.0 s      75.0 dmg/s
    Poison Arrow   110 -> 360 power, cd  6.0 ->  5.5 s      65.5 dmg/s
    Sharpen Arrow  cooldown 44.0 -> 38.0 s (was 80.0 -> 92.6)
    Calling Hawk   cooldown  6.0 ->  5.0 s (was  6.0 ->  9.6)
    Hawk (NPC 890) AVOID 0 -> 200

Every cooldown now improves with rank, as everywhere else in the data.

The two damage numbers sit deliberately *below* the Raider's exclusives (Power
Burst 100, Prime Hit 105 dmg/s): the Scout fights at forty metres and the brief
is "normal damages, high dodge". They sit clearly above the base Hawker band
(38-70) so the advancement is worth taking.

Eagle Shot is the signature and is shaped as an opener -- 750 in one hit, the
biggest single number the class owns, from range 4000 (tied longest in the
game), with the Faint it already gained at rank 6. Poison Arrow is the
repeatable one: two thirds the damage per second, a third of the cooldown, and
it keeps its poison ramp. Rank 1 Eagle Shot takes a 9.2 -> 11.0 s cooldown, the
only place anything gets slower; its rank-1 damage more than doubles in
exchange, and the point of the skill is the big slow opener.

Sharpen Arrow's +ATK% (9 -> 21%) is untouched -- the skill was never weak, it
was just impossible to keep up. Cooldown now runs 6 s under its duration at
rank 1 and 32 s under at rank 10, so ranking it buys uptime.

What is deliberately left alone
-------------------------------
**Detecting** (1671-1673, three ranks). It works, it is cheap, and it is the
only skill in the game that strips invisibility -- pointed squarely at the
Raider's Stealth, its own sibling class. Three ranks is right for a niche
counter; scaling it would only make the counter cheaper, not more interesting.

**The Hawk's stats, other than the hole.** It needed far less than it first
appeared (see the trap below). Scaled to rank 10 / owner level 240 it lands at
767 ATK against Firegon's 748, on 5158 HP against Firegon's 7153 -- the hardest
hitter of the mid-tier summons on about 72% of the HP, with by far the best
dodge. A fragile evasive striker is exactly the right pet for this class, so
that shape is kept.

**Calling Hawk's MP.** 155 -> 200 is already the cheapest real summon in the
game (Call Firegon 250 -> 350, Call Elemental 400 -> 600).

Traps
-----
**LIST_NPC columns are not where you would guess, and reading the wrong ones
tells a clean, wrong story.** Cols 5 and 6 are R_WEAPON and L_WEAPON -- the
weapon *model* ids -- and they are 0 for most summons. Read as ATK/DEF they say
the Hawk is a 0-attack, 0-defence pet, which is a coherent enough conclusion to
act on and completely false. The real map (src/common/include/rose/io/stb.h) is
level 7, HP 8, ATK 9, HIT 10, DEF 11, RES 12, AVOID 13, ATK_SPEED 14.

**The server scales six summon stats; the client mirrors only three.**
`CObjSUMMON::SetCallerOBJ` (cobjnpc.cpp) scales HP, DEF, RES, AVOID, ATK and
HIT. `recvpacket.cpp` recomputes only ATK/DEF/RES for the summon info panel, so
reading the client copy to learn what scales silently omits AVOID -- which is
the stat this pass fixes. AVOID scales as

    NPC_AVOID * (skillLevel + 22) * (ownerLevel + 90) / 3400

so at rank 10 with a level-240 owner the multiplier is 3.11. NPC 890's AVOID of
0 therefore does not mean "a bit less dodge at max rank"; it means **ranking
Calling Hawk to 10 deletes the pet's dodge entirely**, dropping it from ~612 to
0 and taking away the one stat keeping a low-HP summon alive. Every other
summon's top row is fine -- 890 is the only zero in the table.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKILL_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.STB")
NPC_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.scout.json")

# LIST_SKILL game columns, from src/common/io_skill.h.
COL_NAME = 0
COL_FAMILY = 1      # SKILL_1LEV_INDEX, stable across a mid-curve rename
COL_LEVEL = 2       # SKILL_LEVEL, the rank within the family
COL_POWER = 9       # SKILL_POWER
COL_COST = 17       # SKILL_USE_VALUE(s, 0); the property is col 16, AT_MP here
COL_RELOAD = 20     # SKILL_RELOAD_TIME, x 0.2 s

WRITTEN = (COL_POWER, COL_RELOAD, COL_COST)

# LIST_NPC game columns, from src/common/include/rose/io/stb.h. See the trap in
# the docstring: 5 and 6 are weapon MODEL ids, not stats.
NPC_COL_NAME = 0
NPC_COL_LEVEL = 7
NPC_COL_ATK = 9
NPC_COL_AVOID = 13

# base row -> (label, family, first rank, rank count).
FAMILIES = {
    1621: ("Poison Arrow", 1621, 1, 10),
    1691: ("Sharpen Arrow", 1691, 1, 10),
    1701: ("Hawk Shot / Eagle Shot", 1701, 1, 10),
    1711: ("Calling Hawk", 1711, 1, 10),
}

# The Hawk summons, one per Calling Hawk rank.
HAWK_ROWS = list(range(881, 891))
HAWK_NAME = "Hawk"


def lin(a, b, n=10):
    """Linear rank ramp, rounded."""
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


def cd(a, b, n=10):
    """Cooldown ramp in SECONDS -> SKILL_RELOAD_TIME units (x0.2 s)."""
    return [round(v / 0.2) for v in (a + (b - a) * k / (n - 1) for k in range(n))]


# profile -> base row -> (power, reload, mp). None leaves that column vanilla.
PROFILES = {
    "marksman": {
        # The signature: the longest-range opener in the game, and the biggest
        # single number the class owns. 75.0 dmg/s at rank 10.
        1701: (lin(220, 750), cd(11.0, 10.0), lin(40, 110)),

        # The repeatable one. 65.5 dmg/s, and it keeps its poison ramp
        # (duration 12 -> 21 s, land chance 70 -> 90%) untouched.
        1621: (lin(110, 360), cd(6.0, 5.5), lin(30, 60)),

        # Cooldown only. +ATK% and duration are fine; it was simply never
        # possible to hold the buff up.
        1691: (None, cd(44.0, 38.0), None),

        # Cooldown only, to stop it regressing 6.0 -> 9.6 s with rank.
        1711: (None, cd(6.0, 5.0), None),
    },
}

# profile -> the AVOID the Hawk summons should carry. Only NPC 890 differs from
# vanilla (it is 0); the rest are written so --verify covers the whole ramp.
NPC_PROFILES = {
    "marksman": {
        NPC_COL_AVOID: [174, 176, 179, 182, 185, 188, 191, 194, 197, 200],
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


def check_hawks(npc):
    """Confirm 881-890 really are the Hawk summons before writing to them."""
    for r in HAWK_ROWS:
        name = gs(npc, r, NPC_COL_NAME)
        if name != HAWK_NAME:
            sys.exit(f"NPC row {r}: expected {HAWK_NAME!r}, found {name!r} -- "
                     f"LIST_NPC.STB has moved; refusing to write")
        # A summon with no attack power would mean the column map is wrong
        # again; the whole point of the check is to catch that early.
        if gi(npc, r, NPC_COL_ATK) <= 0:
            sys.exit(f"NPC row {r} ({name}): ATK column {NPC_COL_ATK} reads "
                     f"{gi(npc, r, NPC_COL_ATK)} -- column map looks wrong, "
                     f"refusing to write")
    return HAWK_ROWS


def read_sidecar():
    if not os.path.exists(SIDECAR):
        return None, {}, {}
    with open(SIDECAR, encoding="utf-8") as fh:
        raw = json.load(fh)
    return (raw["profile"],
            {int(k): tuple(v) for k, v in raw["rows"].items()},
            {int(k): tuple(v) for k, v in raw.get("npc_rows", {}).items()})


def write_sidecar(profile, rows, npc_rows):
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"profile": profile,
                   "rows": {str(k): list(v) for k, v in sorted(rows.items())},
                   "npc_rows": {str(k): list(v) for k, v in sorted(npc_rows.items())}},
                  fh, indent=1)


def target_state(stb, profile, vanilla):
    want = {}
    for base, curves in PROFILES[profile].items():
        for n, r in enumerate(rows_for(stb, base)):
            base_vals = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            want[r] = tuple(curve[n] if curve else base_vals[i]
                            for i, curve in enumerate(curves))
    return want


def npc_target_state(npc, profile):
    cols = sorted(NPC_PROFILES[profile])
    want = {}
    for n, r in enumerate(check_hawks(npc)):
        want[r] = tuple(NPC_PROFILES[profile][c][n] for c in cols)
    return cols, want


def apply_values(stb, values, cols=WRITTEN):
    for r, vals in values.items():
        for c, v in zip(cols, vals):
            stb.set(r, c, str(v) if v else "")


def show(stb, npc, profile, want, vanilla, npc_cols, npc_want, npc_vanilla):
    for base in sorted(PROFILES[profile]):
        rows = rows_for(stb, base)
        label, _f, first, _n = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]})")
        print(f"  {'rank':>5}{'power':>18}{'cooldown s':>20}{'MP':>15}{'dmg/s':>10}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            now = want[r]
            pw = f"{was[0]} -> {now[0]}" if was[0] != now[0] else ("-" if not now[0] else f"{now[0]} =")
            cool = (f"{was[1] * 0.2:.1f} -> {now[1] * 0.2:.1f}"
                    if was[1] != now[1] else f"{was[1] * 0.2:.1f} =")
            mp = f"{was[2]} -> {now[2]}" if was[2] != now[2] else f"{now[2]} ="
            dps = f"{now[0] / (now[1] * 0.2):.1f}" if now[0] and now[1] else "-"
            print(f"  {first + n:>5}{pw:>18}{cool:>20}{mp:>15}{dps:>10}")

    print(f"\nHawk summons  (LIST_NPC rows {HAWK_ROWS[0]}-{HAWK_ROWS[-1]})")
    print(f"  {'npc':>5}{'lv':>5}{'avoid':>18}")
    for n, r in enumerate(HAWK_ROWS):
        was = npc_vanilla.get(r) or tuple(gi(npc, r, c) for c in npc_cols)
        now = npc_want[r]
        av = f"{was[0]} -> {now[0]}" if was[0] != now[0] else f"{now[0]} ="
        print(f"  {r:>5}{gi(npc, r, NPC_COL_LEVEL):>5}{av:>18}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--profile", choices=sorted(PROFILES))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_stb_module()
    stb = oro.Stb(SKILL_STB)
    npc = oro.Stb(NPC_STB)
    active, vanilla, npc_vanilla = read_sidecar()

    if args.restore:
        if not vanilla:
            sys.exit("no sidecar -- nothing to restore")
        apply_values(stb, vanilla)
        with open(SKILL_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        if npc_vanilla:
            apply_values(npc, npc_vanilla, sorted(NPC_PROFILES[active]))
            with open(NPC_STB, "wb") as fh:
                fh.write(npc.to_bytes())
        os.remove(SIDECAR)
        print(f"restored {len(vanilla)} skill rows and {len(npc_vanilla)} NPC rows "
              f"to vanilla (was profile {active!r}); sidecar removed")
        return

    if args.verify:
        if not active:
            sys.exit("no sidecar -- no profile is applied")
        want = target_state(stb, active, vanilla)
        npc_cols, npc_want = npc_target_state(npc, active)
        bad = [(SKILL_STB, r, c, gi(stb, r, c), v)
               for r, vals in sorted(want.items())
               for c, v in zip(WRITTEN, vals) if gi(stb, r, c) != v]
        bad += [(NPC_STB, r, c, gi(npc, r, c), v)
                for r, vals in sorted(npc_want.items())
                for c, v in zip(npc_cols, vals) if gi(npc, r, c) != v]
        print(f"profile {active!r}: {len(want)} skill rows + {len(npc_want)} NPC rows, "
              f"{len(bad)} columns do not match")
        for path, r, c, got, exp in bad:
            print(f"    {os.path.basename(path)} row {r} col {c}: {got} != {exp}")
        sys.exit(1 if bad else 0)

    if not args.profile:
        print(f"active profile: {active!r}" if active else "no profile applied (vanilla)")
        print(f"available: {', '.join(sorted(PROFILES))}")
        print("pass --profile <name> to apply one, or --restore to go back to vanilla")
        return

    if active == args.profile:
        print(f"profile {args.profile!r} is already applied -- nothing to do.")
        return

    npc_cols = sorted(NPC_PROFILES[args.profile])
    if active:
        print(f"switching profile {active!r} -> {args.profile!r}\n")
        apply_values(stb, vanilla)
        apply_values(npc, npc_vanilla, sorted(NPC_PROFILES[active]))
    else:
        vanilla = {r: tuple(gi(stb, r, c) for c in WRITTEN)
                   for base in PROFILES[args.profile] for r in rows_for(stb, base)}
        npc_vanilla = {r: tuple(gi(npc, r, c) for c in npc_cols)
                       for r in check_hawks(npc)}

    want = target_state(stb, args.profile, vanilla)
    _cols, npc_want = npc_target_state(npc, args.profile)
    show(stb, npc, args.profile, want, vanilla, npc_cols, npc_want, npc_vanilla)
    apply_values(stb, want)
    apply_values(npc, npc_want, npc_cols)

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    with open(SKILL_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(NPC_STB, "wb") as fh:
        fh.write(npc.to_bytes())
    write_sidecar(args.profile, vanilla, npc_vanilla)

    chk = oro.Stb(SKILL_STB)
    for r, vals in want.items():
        for c, v in zip(WRITTEN, vals):
            if gi(chk, r, c) != v:
                sys.exit(f"verify failed: LIST_SKILL row {r} col {c} = "
                         f"{gi(chk, r, c)}, expected {v}")
    chk_npc = oro.Stb(NPC_STB)
    for r, vals in npc_want.items():
        for c, v in zip(npc_cols, vals):
            if gi(chk_npc, r, c) != v:
                sys.exit(f"verify failed: LIST_NPC row {r} col {c} = "
                         f"{gi(chk_npc, r, c)}, expected {v}")
    print(f"\ndone -- profile {args.profile!r} applied to {len(want)} skill rows and "
          f"{len(npc_want)} NPC rows, and verified. Sidecar: {os.path.basename(SIDECAR)}")
    print("Restart the game server (it caches STBs at startup) and rebake the VFS. "
          "No client rebuild is needed -- this is data only.")


if __name__ == "__main__":
    main()
