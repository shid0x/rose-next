"""Retune the Soldier line's attack skills so no one of them is the obvious pick.

    python scripts/rebalance-soldier-skills.py --profile brawl
    python scripts/rebalance-soldier-skills.py --restore

Companion to `rebalance-dealer-skills.py`, same machinery: the sidecar holds the
*vanilla* values plus the active profile name, so switching is revert-then-apply
and never accumulates.

Scope
-----
The nine attack skills whose SKILL_AVAILBLE_CLASS_SET is 41 ("Soldier Job" =
111/121/122/131/132 in LIST_CLASS.STB), cross-checked against the nodes actually
present in `data/3DDATA/CONTROL/xml/skilltree_soldier.xml`. That is the shared
spine the whole Soldier line keeps using, so it covers base Soldier, Knight and
Champion. Knight-only (Slow Shot, Range Bow Shot, Impact Wave, Lightning
Crasher, Holy Blood) and Champion-only (Tendon Slash, Champion Hit) skills are
class 61/62 and are deliberately left alone. Non-attack skills are untouched.

Design goal: no dominant option
-------------------------------
The Dealer pass built a clean escalation (filler -> nuke -> slam) where the
ranking is obvious on purpose. This one is the opposite: every skill is meant to
win on *one* axis and lose on the others, so the best choice depends on the
situation and the build rather than on a number. The axes are already in the
data, which is what makes it work:

    Heavy Attack    cheapest, shortest cooldown       -- wins when MP-bound
    Double Attack   2 hits on the CRITICAL-scaling    -- wins when cooldown-bound,
                    formula (SKILL_DAMAGE_TYPE 0)        but drinks MP
    Leap Attack     one huge number                   -- wins a single burst
    Divine Force    ranged 20 m, needs no weapon,     -- wins on safety, and sets
                    applies -RES for 15-23 s             up everything else
    Spin Attack     7 m AoE                           -- wins from 2-3 targets up
    Blood Attack    heals you on hit                  -- wins on downtime
    Taunt Shot      crossbow, cheap and fast          -- the ranged Soldier build
    Heavy Bow Shot  crossbow, big hit                 -- ditto

Measured at level 52 rank 4 (Champion, ATK 186, vs the median level-52 field
monster), spamming one skill alongside auto-attack:

                    cd-bound dps   MP-bound dps   dmg/MP   biggest hit
    Heavy Attack             123             96     27.7           361
    Double Attack            142             72      8.6           398
    Leap Attack              134             79     12.9           900
    Divine Force             111             74      9.8           519
    Spin Attack               91             69      6.6       478 x N
    Blood Attack             106             72      8.6           516

Three different skills win three different questions -- Double Attack if the
fight is short and your bar is full, Heavy Attack if you are grinding and MP is
the limit, Leap Attack if you need one big number now -- and the MP-bound column
is deliberately tight (69-96) so none of them runs away with it. That MP
pressure is load-bearing: with nine skills and roughly 90 MP/min of standing
regen, you cannot press everything, and *that* is what makes the choice real.

**Divine Force -> anything is a genuine combo.** Its debuff is LIST_STATUS row
23 "Magic Resistance Decreased" (`ING_DEC_RES`), `StatusEffects::Adj_RES()`
subtracts it, and monster `Get_RES()` in `cobjchar.h` consults it -- so it works
server-side on monsters. Measured at -35% RES it is worth **+17.7%** to the
magic-formula skills (Divine Force itself, Spin Attack) and **+10.7%** to the
weapon-formula ones. Opening with it is a real, discoverable choice. The debuff
columns are deliberately NOT touched here; only damage, cooldown and MP move.

Who actually owns each rank
---------------------------
**`SKILL_AVAILBLE_CLASS_SET` (col 35) is declared per rank, not per family.**
Rank 1 names the base class, the rank where the tree branches re-declares an
advanced one, and ranks in between carry 0 meaning "inherit". Reading a family's
first row therefore tells you nothing about who owns its upper half. Nine
families in this tree split mid-curve:

    Champion-exclusive from       Knight-exclusive from
      Spin Attack/Twist Attack r6   Armor Mastery              r11
      Blood Attack             r6   Shield Barrier             r6
      Quick Step               r6   Endure                     r6
      Berserk                 r11   Divine Force/Lightening    r6
                                    CrossBow Mastery           r6

The split is coherent -- Knight takes armour, shield, endure and the debuff;
Champion takes the area attack, the heal, movement and Berserk.

Three of those live in this file, and their curves are now **back-loaded**: the
shared ranks 1-5 stay near vanilla so a plain Soldier gains little, and the
payoff lands past the gate on the branch that owns it. Spin Attack becomes the
Champion's area attack, Blood Attack (damage *and* heal) becomes the Champion's
sustain, and Divine Lightening becomes the Knight's group debuff. The other six
are handled in `rebalance-knight.py`, `rebalance-champion.py` and
`rebalance-crossbow-knight.py`.

Leaving room for the advanced classes
-------------------------------------
This profile was written before any Knight or Champion work and set the *base*
skills so high that they buried the capstones. Class 41 is shared with Knight and
Champion, so a level-100 Champion was unlocking Champion Hit (vanilla, 330 power)
having already spent a hundred levels with Leap Attack (950 power, plus a stun,
plus an HP cost). Advancing made you worse at hitting things.

Three ceilings were pulled down so the advanced skills have somewhere to sit:

    Leap Attack     260->950  =>  180->560   (still the biggest *base* hit)
    Triple Attack   340->620  =>  300->520
    Heavy Bow Shot  200->620  =>  180->520

Everything else is unchanged -- the axis design is the good part and none of the
others were competing with a capstone. The resulting order at level 140, biggest
single hit first: Champion Hit 2146, Range Bow Shot 1501, Tendon Slash 1340,
Leap Attack 1114. Base sits under advanced, which is the whole point.

Rank 11-20 continuity
---------------------
Double Attack continues into **Triple Attack** (rows 331-340) past rank 10, and
that extension is included -- otherwise ranking up would be a *downgrade*. Note
this is already true in vanilla (Double Attack rank 10 is power 90 x2 hits;
Triple Attack rank 11 is power 45 x3), and buffing ranks 1-10 alone would widen
the cliff rather than create it. Triple Attack genuinely has 3 hits, so its
power is set lower per hit; the curve is sized so rank 11 is ~1.37x rank 10 in
damage and ~1.1x in DPS. `SKILL_ANI_HIT_COUNT` itself is never touched -- see
the note in `rebalance-dealer-skills.py` for why that column is not a knob.

Other things worth knowing
--------------------------
* **Blood Attack's heal has to stay flat.** It is `SKILL_INCREASE_ABILITY_VALUE`
  on AT_HP, and `Get_SkillAdjustVALUE` resolves AT_HP against *current* HP, so a
  percentage heal would scale with what you have left -- useless exactly when
  you need it (this is the rule recorded in the project CLAUDE.md). A flat heal
  decays with level instead: vanilla rank 10 is 370, which is 41% of a level-52
  Soldier's HP but only 11% at level 200. The curve here runs 350 -> 1000, which
  overheals at low level (capped, so merely wasted) and still lands ~31% at
  level 200.
* **Spin Attack's AoE is real** -- it is SKILL_TYPE 17, which reaches
  `CObjCHAR::Skill_DamageToAROUND`, unlike the type-3 skills where SKILL_SCOPE
  only spreads the status effect.
* Taunt Shot and Heavy Bow Shot require weapon type 271
  (`WEAPON_ITEM_USE_ARROW2`, a one-handed arrow-consuming weapon -- a crossbow),
  so they are a separate build and are tuned as their own pair, not against the
  melee six.
* `scripts/balance-sim.py`'s gear model picks a req-level-0 "Knight Killer" for
  every level under 40, so its sub-40 output is junk; low ranks here were not
  validated against a real early character.

Idempotent: --dry-run, --verify and --restore are available. data/ is
gitignored, so this file is the only record of the change.
"""
import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKILL_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.soldier-skills.json")

# Game column indices, from src/common/io_skill.h.
COL_NAME = 0
COL_FAMILY = 1      # SKILL_1LEV_INDEX, stable across a mid-curve rename
COL_LEVEL = 2       # SKILL_LEVEL, the rank within the family
COL_POWER = 9       # SKILL_POWER
COL_ABILITY = 22    # SKILL_INCREASE_ABILITY_VALUE(s, 0). What it *means* depends on
                    # col 21: AT_HP for Blood Attack (the heal), but AT_AVOID on
                    # Triple Attack ranks 19-20. Only Blood Attack is written here.
COL_COST = 17       # SKILL_USE_VALUE(s, 0). NOT always mana: the property lives in
                    # col 16, and it is AT_HP for Leap Attack -- so that skill is
                    # priced in blood, deliberately, and always has been. This pass
                    # only moves the value; the property is never touched.
COL_RELOAD = 20     # SKILL_RELOAD_TIME, x 0.2s (io_skill.cpp: x200 - 100 ms)

WRITTEN = (COL_POWER, COL_RELOAD, COL_COST, COL_ABILITY)   # order matches sidecar tuples
RANKS = 10

# base row -> (label, SKILL_1LEV_INDEX the rows must share, first rank number).
#
# Three of these families change *name* partway up -- Double Attack becomes
# Triple Attack at rank 11 (and gains a third hit), Divine Force becomes Divine
# Lightening at rank 6, Spin Attack becomes Twist Attack at rank 6 (both gaining
# SKILL_SCOPE) -- so the rows cannot be identified by name. They are identified
# by the family column instead, which is stable across the rename.
FAMILIES = {
    301: ("Heavy Attack", 301, 1),
    321: ("Double Attack", 321, 1),
    331: ("Triple Attack", 321, 11),          # same family as Double Attack
    341: ("Leap Attack", 341, 1),
    391: ("Divine Force / Divine Lightening", 391, 1),
    401: ("Spin Attack / Twist Attack", 401, 1),
    471: ("Taunt Shot", 471, 1),
    481: ("Heavy Bow Shot", 481, 1),
    651: ("Blood Attack", 651, 1),
}


def lin(a, b, n=RANKS):
    """Linear rank ramp, rounded. Readable curves beat hand-tuned ones."""
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


# Multi-hit: power is NOT damage
# ------------------------------
# These families land more than once per cast -- the multiplier is the
# animation's attack-frame count, not SKILL_ANI_HIT_COUNT (col 70), which is
# dead data. Power therefore has to be set from *effective* damage.
#
# The ceiling comes from vanilla rather than from taste. Vanilla made multi-hit
# the high-damage mana-burner deliberately, and the two source lines disagreed
# about how far: the Soldier's multi-hit sat at 3.2-4.0x its own single-hit
# median, the Dealer's at 1.7-2.4x. Restoring each line's own ratio would put a
# dominant button straight back into the Soldier tree, so the Dealer's -- the
# moderate precedent -- is the house standard for every line:
#
#     x2 per cast  ->  1.68x the line's single-hit median damage/second
#     x3 per cast  ->  2.36x
#
# Rank-11 power curves are deliberately shallow. The upgrade from the x2 skill
# to the x3 skill is the extra hit and the cooldown, not raw power; letting
# power climb as well puts the ceiling back. Each x3 curve starts at whatever
# lands on the x2 family's rank-10 damage/second, so the rename is never a
# downgrade.

# profile -> base row -> (power, reload, mp, heal); None leaves that column vanilla.
PROFILES = {
    "brawl": {
        301: (lin(80, 260), [25] * 10, lin(8, 24), None),           # cheap, fast
        # x2 per cast -> 95 dmg/s (1.68 x the 56 dmg/s single-hit median)
        321: (lin(60, 170), [27, 26, 25, 24, 23, 22, 21, 20, 19, 18],
              lin(30, 78), None),                                   # crit gamble
        # x3 per cast -> 133 dmg/s; starts at 140 so rank 11 matches rank 10
        331: (lin(140, 160), lin(22, 18), lin(82, 130), None),      # 3 hits, r11-20
        341: (lin(180, 560), [60] * 10, lin(50, 110), None),        # the nuke
        # Split families: ranks 1-5 stay near vanilla, the exclusive upper half
        # carries the payoff. See the ownership table in the docstring.
        391: (lin(160, 260, 5) + lin(400, 700, 5),                  # -> KNIGHT at r6
              [50] * 10, lin(40, 80), None),
        401: (lin(130, 220, 5) + lin(380, 700, 5),                  # -> CHAMPION at r6
              [75] * 10, lin(55, 110), None),
        471: (lin(90, 330), [25] * 10, lin(14, 38), None),          # crossbow, fast
        481: (lin(200, 620), [45] * 10, lin(26, 70), None),         # crossbow, big
        651: (lin(150, 260, 5) + lin(380, 620, 5),                  # -> CHAMPION at r6
              [55] * 10, lin(45, 90),
              lin(300, 500, 5) + lin(750, 1400, 5)),                # the heal, likewise
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

    Keyed on SKILL_1LEV_INDEX and the rank number rather than the displayed
    name, because several of these families rename partway up their curve.
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
    """{row: (power, reload, mp, heal)} the named profile should produce.

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


def show(stb, want, vanilla):
    for base in sorted(FAMILIES):
        if base not in PROFILES["brawl"]:
            continue
        rows = rows_for(stb, base)
        label, _family, first = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]}, ranks {first}-{first + 9})")
        ab = "heal" if base == 651 else "abilityVal"
        cost = "HP cost" if base == 341 else "MP cost"
        print(f"  {'rank':>5}{'power':>16}{'cooldown s':>18}{cost:>14}{ab:>16}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            cells = []
            for i, (w, v) in enumerate(zip(was, want[r])):
                if i == 1:
                    w, v = w * 0.2, v * 0.2
                    cells.append(f"{w:.1f} -> {v:.1f}" if w != v else f"{w:.1f} =")
                elif i == 3 and w == v == 0:
                    cells.append("-")
                else:
                    cells.append(f"{w} -> {v}" if w != v else f"{w} =")
            print(f"  {first + n:>5}{cells[0]:>16}{cells[1]:>18}{cells[2]:>14}{cells[3]:>16}")


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
    show(stb, want, vanilla)
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
