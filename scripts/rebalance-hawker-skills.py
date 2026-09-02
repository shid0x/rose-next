"""Bring the Hawker's attack skills up to the scale the other lines now use.

    python scripts/rebalance-hawker-skills.py --profile skirmish
    python scripts/rebalance-hawker-skills.py --restore

Fourth in the series after `rebalance-dealer-skills.py`,
`rebalance-soldier-skills.py` and the advanced-class passes; identical
machinery. The sidecar holds the *vanilla* values plus the active profile name,
so switching profiles is revert-then-apply and never accumulates.

Why this pass exists
--------------------
Not a design idea -- a debt we created. Before this, the Hawker's attack skills
topped out at 100-130 SKILL_POWER while the classes we had already retuned sat
at 260-700 (Soldier) and 460-900 (Dealer). Its only real hit was Flame Hawk at
400 on a 21.6 s cooldown. The most-played line in the game had quietly become
the weakest, entirely because of our own earlier passes, so this one is
corrective before it is creative. The Raider and Scout identity work is separate
and comes after.

Scope
-----
Twelve rows-of-ten. Nine are pure Hawker (SKILL_AVAILBLE_CLASS_SET 43); three
are shared curves whose upper half belongs to an advanced class, handled the
same way the Soldier pass handled Divine Force and Spin Attack -- ranks 1-5 stay
near vanilla and the exclusive half carries the payoff:

    Holding Arrow   ranks 1-5 Hawker, 6-10 SCOUT     (col 35 re-declares at r6)
    Screw Attack    ranks 1-5 Hawker, 6-10 RAIDER    (likewise)

Two families rename at rank 11 into an advanced class's row block, exactly like
Soldier's Double -> Triple Attack:

    Double Shot   (1521, r1-10, Hawker) -> Triple Shot   (1531, r11-20, SCOUT)
    Double Attack (1541, r1-10, Hawker) -> Triple Attack (1551, r11-20, RAIDER)

Those two tails are included here, but only as a smooth continuation so that
ranking past 10 is never a downgrade. They are *not* tuned for Raider/Scout
identity -- that is the next pass's job, and it will revisit these rows.

Untouched: the masteries (Bow, Combat, Battle, Rest), Hawker Spirit, Sprint,
Vanish, and Iron Skin. Vanish deserves a note -- it is a 7 m radius **Sleep** at
25 m range on a 5.4 s cooldown, the only AoE hard crowd-control in the game, and
it is already good. It is left exactly alone on purpose.

Multi-hit is animation-driven, so power is not damage
-----------------------------------------------------
The damage multiplier is the ZMO's attack-frame count, not SKILL_ANI_HIT_COUNT
(col 70), which is dead data -- see the root CLAUDE.md and
`reference_multihit_is_animation_driven`. Parsed from the motions these skills
actually use:

    Double Shot / Double Attack    motion 91  -> bow_2attack / kartar_2attack   x2
    Triple Shot / Triple Attack    motion 92  -> bow_3attack / kartar_3attack   x3
    everything else here                                                        x1

So the ladder below is written in *effective* damage (power x hits) and the
power column is back-derived from it. Setting the x2/x3 skills by raw power
would overshoot by 2-3x, which is exactly the trap the Dealer pass fell into.

The ladder, at rank 10 (the Hawker's own ceiling), effective damage:

    Aim Shot        300  x1  5.0 s   bow, cheap and always ready
    Power Attack    320  x1  4.8 s   the melee twin of Aim Shot
    Double Shot     468  x2  5.0 s   bow, the fast cycle
    Double Attack   336  x2  3.6 s   melee, the fastest cycle in the class
    Spiral Kick     420  x1 11.0 s   the only Hawker AoE, 7 m
    Spirit Heart    430  x1  8.0 s   26 m, + slows the target's attack speed
    Flame Hawk      780  x1 14.0 s   40 m, the signature hit

For comparison the Dealer now sits at 460 (Power Gun Shot) to 900 (Smash Gun) on
8-13 s cooldowns. The Hawker deliberately lands *under* that per hit and well
under it per cooldown: this line's damage is supposed to come from attack speed
and its skills from control, so the skills buy tempo rather than burst.

Double Attack and Double Shot carry **no status effect at any rank** -- checked
per row, not inferred from the family. An earlier draft of this file claimed
Double Attack stuns and held its damage down to pay for it; that was a misread of
the status column, taken from the last row of the family (which is Triple
Attack rank 20) rather than from the rank in question. The stun and the slow are
capstones on the *advanced* halves of those curves, at ranks 19-20 only:

    Triple Attack  r19/20  Faint     15% / 20%,  4 s /  6 s   (RAIDER)
    Triple Shot    r19/20  Slow Run  15% / 20%, 10 s / 15 s   (SCOUT)

The numbers below were left unchanged anyway, on their own merit: Double
Attack's axis is tempo, not size. At 3.6 s it is the fastest cycle in the class
and already the highest sustained output in the set (75 damage/s against 54-67
for the rest), which is the right shape for a melee skill with no utility
attached and a position risk the bow skills do not take.

The status effects that ARE real on this line, verified per rank:

    Vanish          Sleep,     80% -> 104%, 16 -> 34 s  (7 m radius, 25 m range)
    Holding Arrow   Slow Run,  70% ->  90%, 20 -> 26 s  (+ Slow Attack from r6)
    Screw Attack    Def down,  35% ->  55%, 20 s
    Spirit Heart    Slow Atk,  30% ->  62%, 15 -> 20 s
    Hawk Shot       Faint,     30% ->  50%,  6 -> 10 s  (Scout, and r6+ only)

Cooldowns that got worse as you ranked them
-------------------------------------------
Eight of the thirteen Hawker families charged SP to make a skill *slower*:

    Speed Shot     110.0 -> 126.2 s      Spirit Heart   10.0 -> 13.6 s
    Spiral Kick     14.0 ->  19.4 s      Flame Hawk     18.0 -> 21.6 s
    Holding Arrow    8.4 ->  11.6 s      Screw Attack   10.0 -> 11.8 s
    Aim Shot         6.0 ->   6.8 s      Power Attack    5.6 ->  6.0 s

This is a retail-wide quirk rather than something specific to this line --
vanilla Soldier was 40% inverted and vanilla Dealer 33%, measured from their
sidecars. Our earlier passes happened to remove it by writing flat or improving
curves; this one does the same for the Hawker. Every cooldown here now improves
with rank or holds flat.

Speed Shot is the worst case and gets a real fix. It is the attack-speed class's
own attack-speed buff, and at *every* rank its cooldown exceeded its duration by
~28 s: 110 s / 80 s at rank 1, 126.2 s / 100 s at rank 10. Ranking it widened the
absolute gap.

The fix has to clear the duration *at the same rank*, which is easy to get wrong
-- duration itself ramps 80 -> 100 s, so a cooldown curve chosen to look right at
rank 10 can still leave rank 1 twenty seconds short. It now runs 76 -> 62 s
against that 80 -> 100 s duration: maintainable at every rank, with the slack
widening as you invest. For scale the Champion's Berserk is 190 s of buff on a
6 s cooldown, so this is not generous by the standards of the lines already
touched. SKILL_DURATION is not written -- only the cooldown moves.

Left for the Raider and Scout passes
------------------------------------
Sharpen Arrow (Scout, 70 s duration / 92.6 s cooldown) and Stealth (Raider, 90 s
/ 104 s) have the same uptime hole as Speed Shot. They are class 66 and 65, out
of scope here, and are deliberately not touched.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKILL_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.hawker-skills.json")

# Game column indices, from src/common/io_skill.h.
COL_NAME = 0
COL_FAMILY = 1      # SKILL_1LEV_INDEX, stable across a mid-curve rename
COL_LEVEL = 2       # SKILL_LEVEL, the rank within the family
COL_POWER = 9       # SKILL_POWER
COL_COST = 17       # SKILL_USE_VALUE(s, 0). The property is col 16 and is AT_MP
                    # for every row here -- checked, and never written.
COL_RELOAD = 20     # SKILL_RELOAD_TIME, x 0.2 s (io_skill.cpp: x200 - 100 ms)

WRITTEN = (COL_POWER, COL_RELOAD, COL_COST)   # order matches the sidecar tuples
RANKS = 10

# base row -> (label, SKILL_1LEV_INDEX the rows must share, first rank number).
# Keyed on the family column rather than the name, because two of these rename
# partway up (Double Shot -> Triple Shot, Double Attack -> Triple Attack).
FAMILIES = {
    1481: ("Aim Shot", 1481, 1),
    1501: ("Power Attack", 1501, 1),
    1521: ("Double Shot", 1521, 1),
    1531: ("Triple Shot", 1521, 11),          # same family as Double Shot
    1541: ("Double Attack", 1541, 1),
    1551: ("Triple Attack", 1541, 11),        # same family as Double Attack
    1571: ("Spiral Kick", 1571, 1),
    1581: ("Holding Arrow", 1581, 1),
    1591: ("Screw Attack", 1591, 1),
    1601: ("Spirit Heart", 1601, 1),
    1641: ("Flame Hawk", 1641, 1),
    1681: ("Speed Shot", 1681, 1),
}

# How many times each family's animation actually lands, so the tables below can
# be read as damage. See the docstring.
HITS = {1521: 2, 1531: 3, 1541: 2, 1551: 3}


def lin(a, b, n=RANKS):
    """Linear rank ramp, rounded. Readable curves beat hand-tuned ones."""
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


def cd(a, b, n=RANKS):
    """Cooldown ramp in SECONDS -> SKILL_RELOAD_TIME units (x0.2 s)."""
    return [round(v / 0.2) for v in (a + (b - a) * k / (n - 1) for k in range(n))]


def per_hit(a, b, hits, n=RANKS):
    """Effective-damage ramp -> the SKILL_POWER that produces it."""
    return [round(v / hits) for v in (a + (b - a) * k / (n - 1) for k in range(n))]


# profile -> base row -> (power, reload, mp); None leaves that column vanilla.
PROFILES = {
    "skirmish": {
        # --- pure Hawker -------------------------------------------------
        1481: (lin(90, 300), cd(6.0, 5.0), lin(20, 40)),          # bow poke
        1501: (lin(95, 320), cd(5.6, 4.8), lin(20, 42)),          # melee poke
        1521: (per_hit(140, 468, 2), cd(6.0, 5.0), lin(24, 50)),  # x2, bow
        1541: (per_hit(120, 336, 2), cd(5.4, 3.6), lin(22, 52)),  # x2, melee
        1571: (lin(120, 420), cd(14.0, 11.0), lin(35, 85)),       # 7 m AoE
        1601: (lin(140, 430), cd(10.0, 8.0), lin(40, 80)),        # 26 m, + slow atk
        1641: (lin(280, 780), cd(18.0, 14.0), lin(55, 120)),      # 40 m signature

        # Speed Shot carries no power. Cooldown only, and it has to clear the
        # *duration at the same rank* (80 s at rank 1, rising to 100 s at rank
        # 10) or the hole is still there -- 100 -> 80 s looked fine at the top
        # and still left rank 1 twenty seconds short.
        1681: (None, cd(76.0, 62.0), lin(30, 50)),

        # --- split curves: ranks 1-5 Hawker, 6-10 the advanced class ------
        # Starts at vanilla's own 8.4 s, not 9.0 -- anything higher would make
        # rank 1 slower than it is today, which is the very thing this fixes.
        1581: (lin(70, 140, 5) + lin(190, 360, 5),                # -> SCOUT at r6
               cd(8.4, 7.0), lin(24, 55)),
        1591: (lin(130, 230, 5) + lin(300, 560, 5),               # -> RAIDER at r6
               cd(10.0, 8.0), lin(45, 95)),

        # --- rank 11-20 tails, continuation only (see the docstring) -----
        1531: (per_hit(562, 657, 3), cd(6.0, 5.0), lin(52, 86)),  # x3, SCOUT
        1551: (per_hit(505, 579, 3), cd(5.4, 4.4), lin(56, 92)),  # x3, RAIDER
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
    """The ten rank rows of one family, checked against what we expect to find.

    Guards against a data change having shifted the table: a silent write to the
    wrong ten rows would be very hard to notice afterwards.
    """
    label, family, first = FAMILIES[base]
    out = []
    for n in range(RANKS):
        r = base + n
        got = gi(stb, r, COL_FAMILY)
        if got != family:
            sys.exit(f"row {r} ({label}): expected family {family}, found {got} -- "
                     f"LIST_SKILL.STB has moved; refusing to write")
        if gi(stb, r, COL_LEVEL) != first + n:
            sys.exit(f"row {r} ({label}): expected rank {first + n}, found "
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
    """{row: (power, reload, mp)} the named profile should produce.

    Vanilla values fill in wherever a profile leaves a column alone, so apply and
    verify read the identical map and cannot drift apart.
    """
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
            stb.set(r, c, str(v))


def show(stb, profile, want, vanilla):
    for base in sorted(PROFILES[profile]):
        rows = rows_for(stb, base)
        label, _family, first = FAMILIES[base]
        hits = HITS.get(base, 1)
        tag = f"  [x{hits} per cast]" if hits > 1 else ""
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]}, ranks {first}-{first + 9}){tag}")
        print(f"  {'rank':>5}{'power':>16}{'damage':>14}{'cooldown s':>18}{'MP':>14}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            now = want[r]
            dmg = (f"{was[0] * hits} -> {now[0] * hits}"
                   if was[0] != now[0] else ("-" if not now[0] else f"{now[0] * hits} ="))
            cool = (f"{was[1] * 0.2:.1f} -> {now[1] * 0.2:.1f}"
                    if was[1] != now[1] else f"{was[1] * 0.2:.1f} =")
            pw = f"{was[0]} -> {now[0]}" if was[0] != now[0] else ("-" if not now[0] else f"{now[0]} =")
            mp = f"{was[2]} -> {now[2]}" if was[2] != now[2] else f"{now[2]} ="
            print(f"  {first + n:>5}{pw:>16}{dmg:>14}{cool:>18}{mp:>14}")


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
        print("use --restore to go back to vanilla.")
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
    print("Restart the game server (it caches STBs at startup) and the client "
          "(it reads LIST_SKILL.STB for cooldowns and tooltips). Rebake the VFS "
          "before deploying.")


if __name__ == "__main__":
    main()
