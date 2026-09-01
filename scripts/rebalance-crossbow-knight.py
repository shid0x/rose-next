"""Make the crossbow Knight a real build: a kiter who wins by control and range.

    python scripts/rebalance-crossbow-knight.py --profile marksman
    python scripts/rebalance-crossbow-knight.py --restore

Same machinery as the other class passes: the sidecar holds the *vanilla* values
plus the active profile name, so applying is idempotent and --restore always
returns the original bytes.

Why this build is worth existing
--------------------------------
A complete crossbow kit is already in the Soldier tree and essentially nobody
uses it. Everything below already existed; none of it needed inventing:

    CrossBow Mastery  (271)  passive, AT_PSV_ATK_POW_AUTO_BOW, +4%..+26%
                             (Knight-exclusive from rank 6 -- col 35 splits)
    Taunt Shot        (471)  applies ING_TAUNT -- forces a target onto you
    Heavy Bow Shot    (481)  the big close-range punish
    Slow Shot         (491)  Knight-only. Applies ING_DEC_MOV_SPD at 40-53%
    Range Bow Shot    (501)  Knight-only. Range 3200-3500, i.e. 32-35 m

Slow them, shoot them from thirty-five metres, taunt whatever needs holding,
and punish anything that closes. That is a Knight who survives through
*distance and control* rather than armour -- a genuinely different answer to
self-sufficiency than the melee Knight's, which is the point of it being its
own build rather than a weapon swap.

Both control effects were verified to work on monsters, not just players:

* `ING_TAUNT` -> `CObjCHAR::Skill_ChangeIngSTATUS` -> `SetCMD_RUNnATTACK`, which
  is the only forced-target mechanism in the game (there is no threat table).
* `ING_DEC_MOV_SPD` (LIST_STATUS row 15, "Slow Run") -> `Adj_RUN_SPEED()` ->
  `CObjCHAR::total_move_speed()`, and `CObjCHAR` is the base of monsters too.

What this pass changes
----------------------
Taunt Shot and Heavy Bow Shot are class 41 and were already retuned by
`rebalance-soldier-skills.py --profile brawl`, so they are left alone here. That
leaves the two Knight-only skills, which are still vanilla and both carry the
same anti-pattern as everything else in this table -- a cooldown that gets
*longer* as you rank up:

    Range Bow Shot  power 100->250  =>  250->800   cd 12.0->15.6s => 11.0->8.0s
    Slow Shot       power  50->150  =>  120->380   cd  8.4->11.6s =>  7.0->5.0s

Range Bow Shot becomes the signature: a 35 m opener that lands before anything
can close on you. Slow Shot's cooldown drops below its own slow duration
(16-20 s), which is what makes kiting actually work -- you can keep a target
slowed indefinitely rather than watching it wear off.

And the mastery
---------------
`CrossBow Mastery` goes from +4..26% to +6..40%, **back-loaded past its rank-6
gate** -- col 35 re-declares the family as Knight-only there, so a Soldier or
Champion holding a crossbow caps at 16% while a Knight reaches 40%. At the top
that is **parity, not a buff**.
A sword Knight stacks two masteries -- Weapon Mastery (+12%) and One-Hand
Mastery (+25%) -- for 37% combined. A crossbow user gets exactly one, because
weapon class 271 appears in the attack-power mapping and *not* in the
attack-speed one (`CUserDATA::GetPassiveSkillAttackSpeed` covers bow, gun and
katar only, and returns 0 for anything else). So a crossbow build can scale
damage and can never scale attack speed, and 40% on one mastery lands just above
what a sword gets from two -- which is the compensation for having only one.

Things worth knowing
--------------------
* **Attack power branches on SHOT TYPE, not weapon type.** `CObjAVT::Cal_ATTACK`
  switches on `pRightWPN->GetShotTYPE()` first, and a crossbow consumes
  SHOT_TYPE_ARROW -- so it uses the *bow* formula,
  `DEX*0.62 + STR*0.2 + level*0.2 + ...`, not the CON-based gun one.
  `scripts/balance-sim.py` maps weapon type 271 to the gun branch and is wrong
  about this; it was corrected by hand for the measurements above. The practical
  consequence is the interesting part: a crossbow Knight has to build **DEX**,
  where a sword Knight builds STR and CON. It is a different stat line, not just
  a different weapon, and that is most of what makes it its own build.
* **A crossbow is one-handed** (`WEAPON_ITEM_USE_ARROW2`), so this build keeps
  its shield. It trades STR and CON for DEX, not defence for offence.
* **The slow's magnitude is deliberately untouched.** 40-53% is already a real
  slow and the mechanism is verified; how a *percentage* status resolves against
  an NPC's base value is a separate thing to confirm before moving the number.
  Only damage and cooldown move here.
* Taunt Shot is the one hard-taunt in the game. It matters more in a party than
  solo, and it is the reason this build can peel for someone else even though it
  fights at range.
* There are 33 crossbows in `LIST_WEAPON.STB` against 53 one-handed swords, so
  the gear exists, if more thinly. Worth a look if the build feels starved at
  some particular level band.

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
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.crossbow-knight.json")

# LIST_SKILL columns (src/common/io_skill.h)
SK_NAME, SK_FAMILY, SK_LEVEL = 0, 1, 2
SK_POWER = 9        # SKILL_POWER
SK_MP = 17          # SKILL_USE_VALUE(s,0); slot 0 is AT_MP for all of these
SK_RELOAD = 20      # SKILL_RELOAD_TIME, x 0.2s
SK_RATE = 23        # SKILL_CHANGE_ABILITY_RATE(s,0) -- the mastery percentage

WRITTEN = (SK_POWER, SK_RELOAD, SK_MP, SK_RATE)
RANKS = 10

# base row -> (label, SKILL_1LEV_INDEX, first rank)
FAMILIES = {
    271: ("CrossBow Mastery", 271, 1),
    491: ("Slow Shot", 491, 1),
    501: ("Range Bow Shot", 501, 1),
}


def lin(a, b, n=RANKS):
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


# profile -> base row -> (power, reload, mp, rate); None keeps vanilla.
PROFILES = {
    "marksman": {
        # Back-loaded past its rank-6 gate: a plain Soldier or a Champion with a
        # crossbow caps at 16%, while a Knight -- who exclusively owns ranks 6-10
        # -- reaches 40%, parity with the two masteries a sword Knight stacks
        # (Weapon Mastery 12% + One-Hand Mastery 25%). That makes the crossbow a
        # Knight weapon by progression rather than by convention.
        271: (None, None, None, [6, 8, 11, 13, 16] + lin(24, 40, 5)),
        # Control: cooldown drops under the slow's own 16-20s duration, so a
        # kiter can actually keep something slowed instead of losing it.
        491: (lin(120, 380), lin(35, 25), lin(20, 60), None),
        # The signature: a 35 m opener that lands before anything reaches you.
        501: (lin(250, 800), lin(55, 40), lin(50, 100), None),
    },
}


def load_stb_module():
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
    """Rank rows of one family, keyed on the family column and rank number."""
    label, family, first = FAMILIES[base]
    out = []
    for n in range(RANKS):
        r = base + n
        got = gi(stb, r, SK_FAMILY)
        if got != family:
            sys.exit(f"row {r} ({label}): expected family {family}, found {got} -- "
                     f"LIST_SKILL.STB has moved; refusing to write")
        if gi(stb, r, SK_LEVEL) != first + n:
            sys.exit(f"row {r} ({label}): expected rank {first + n}, found "
                     f"{gi(stb, r, SK_LEVEL)} -- refusing to write")
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
            stb.set(r, c, str(v))


def show(stb, want, vanilla, profile):
    for base in sorted(PROFILES[profile]):
        rows = rows_for(stb, base)
        label, _fam, first = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]})")
        print(f"  {'rank':>5}{'power':>16}{'cooldown s':>18}{'MP':>14}{'mastery %':>14}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            cells = []
            for i, (w, v) in enumerate(zip(was, want[r])):
                if i == 1:
                    w, v = w * 0.2, v * 0.2
                    cells.append(f"{w:.1f} -> {v:.1f}" if w != v else f"{w:.1f} =")
                elif w == v == 0:
                    cells.append("-")
                else:
                    cells.append(f"{w} -> {v}" if w != v else f"{w} =")
            print(f"  {first + n:>5}{cells[0]:>16}{cells[1]:>18}"
                  f"{cells[2]:>14}{cells[3]:>14}")


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
        bad = [(r, c, gi(stb, r, c), v) for r, vals in sorted(want.items())
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
    show(stb, want, vanilla, args.profile)
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
    print("Restart the game server (it caches STBs at startup) and the client. "
          "Rebake the VFS before deploying.")


if __name__ == "__main__":
    main()
