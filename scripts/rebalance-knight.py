"""Make the armoured Knight the one that survives what kills everyone else.

    python scripts/rebalance-knight.py --profile bulwark
    python scripts/rebalance-knight.py --restore

Same machinery as the other class passes: the sidecar holds the *vanilla* values
plus the active profile name, so applying is idempotent and --restore always
returns the original bytes.

Scope is the three melee Knight-only attack skills. The crossbow pair (Slow Shot,
Range Bow Shot) belongs to the other Knight build and lives in
`rebalance-crossbow-knight.py`.

Why the brief changed
---------------------
This started as "Knight = tank, holds aggro off the party". Two things killed
that plan:

* **There is no threat system.** Monster targeting is AIP-script driven, and the
  only way to move a target is to force it outright (`ING_TAUNT` ->
  `SetCMD_RUNnATTACK`). Nothing accumulates, so nothing can be *held* -- only
  yanked, on a cooldown.
* A class built around protecting other people only has a job when other people
  are there. On a server where most play is solo grinding, that is a class that
  is worse at everything until a party shows up.

So the armoured Knight is aimed at **self-sufficiency** instead: the one who can
fight things well above their own level and walk away. That works alone and in a
group, and taunting becomes a bonus on top rather than the whole identity.

The mechanism, which is real and not flavour
--------------------------------------------
Fighting above your level is gated by accuracy, and **skills have a far gentler
gate than auto-attacks**. `Get_SuccessRATE` discards a normal swing on
`(lv + 10) - monsterLv * 1.05`, which is *proportional* and therefore gets worse
the higher you both are. `Get_SkillDAMAGE`'s weapon branch uses a flat
`lv + 20 - monsterLv`. A character at a level deficit stops landing auto-attacks
long before their skills stop working.

That is the whole build: a Knight punching above its weight is fighting with
skills, and the thing that makes that survivable is denying the target its turns.
Which is what Lightning Crasher does.

What this pass changes
----------------------
    Impact Wave        power 120->240  =>  250->720   cd 14.0->19.4s => 13.0->10.0s
    Lightning Crasher  power 120->270  =>  200->650   cd  8.0->9.8s  =>  9.0->7.0s
                       stun 50->70% / 4->5s  =>  60->100% / 4->6s
    Holy Blood         power 140->240  =>  280->800   cd 14.0->23.0s => 15.0->12.0s

All three had the cooldown-lengthens-with-rank anti-pattern and now shorten with
investment. Measured at level 140 they land at 1121 / 1028 / 1255, which sits
above the base Leap Attack (908) and below Champion Hit (1763) -- correct on both
counts. The Champion is the bigger single hit; the Knight trades that for area
damage and control.

Be careful with the stun
------------------------
Harmful-status success is **not** a flat percentage. From
`CObjCHAR::Skill_ApplyIngSTATUS`:

    chance% = SKILL_SUCCESS_RATIO * (casterLv*2 + casterINT + 20)
                                  / (targetRES*0.6 + 5 + targetAVOID)

Two consequences. It **scales with the caster's INT**, which a Knight does not
build -- so a control-focused Knight has a genuine reason to want some. And it
**decays with level**, because the denominator grows faster than the numerator:
vanilla Lightning Crasher lands 67% at level 60 and 44% at 220.

The ratio is also hypersensitive. Going from 70 to 150 takes the stun from ~50%
to a guaranteed 100% at every level, which would be a permanent lock. The curve
here stops at 100, giving 73% at level 140 and 63% at 200 -- reliable enough to
build around, never reliable enough to trivialise. **Do not raise it further
without re-measuring; the useful range is narrow.**

The same formula governs every debuff in the game, so Slow Shot, Poison Fang and
Divine Force all decay the same way. Worth knowing before tuning any of them.

Where the survivability comes from
----------------------------------
This is the half that was missing, and it was missing because
`SKILL_AVAILBLE_CLASS_SET` (col 35) is declared **per rank**, not per family. An
earlier version of this file claimed the Soldier tree's defensive kit was all
shared and that the Knight could therefore never own its durability. That was
wrong -- reading only each family's first row hides the fact that three of them
hand their upper half to the Knight alone:

    Armor Mastery   DEF%   ranks 1-10 shared,  11-20 KNIGHT
    Shield Barrier  RES%   ranks 1-5  shared,   6-10 KNIGHT
    Endure          DEF%   ranks 1-5  shared,   6-10 KNIGHT

All three are now **back-loaded**: the shared ranks are written back at their
exact vanilla numbers so a Soldier or a Champion gains nothing at all, and the
entire payoff lands past the gate. A Knight reaches +42% DEF from Armor Mastery
where a Champion caps at +12%, +56% RES from Shield Barrier against +21%, and
+66% DEF from Endure against +28%.

So the Knight is durable in a way no other class in the line can copy, which is
what "survives what kills everyone else" needed in order to be true rather than
flattering. Endure's second ability slot already shrinks its movement bonus as
the defence climbs (200 -> 130), so the dig-in trade is built in and is left
alone.

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
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.knight.json")

# LIST_SKILL columns (src/common/io_skill.h)
SK_NAME, SK_FAMILY, SK_LEVEL = 0, 1, 2
SK_POWER = 9
SK_SUCCESS = 13     # SKILL_SUCCESS_RATIO -- see the stun note above, NOT a percentage
SK_DURATION = 14    # SKILL_DURATION, seconds
SK_RELOAD = 20      # SKILL_RELOAD_TIME, x 0.2s
SK_RATE = 23        # SKILL_CHANGE_ABILITY_RATE(s,0) -- the passive/buff percentage

WRITTEN = (SK_POWER, SK_SUCCESS, SK_DURATION, SK_RELOAD, SK_RATE)

# base row -> (label, SKILL_1LEV_INDEX, first rank, rank count)
FAMILIES = {
    # attack skills, Knight-exclusive from rank 1
    511: ("Impact Wave", 511, 1, 10),
    521: ("Lightning Crasher", 521, 1, 10),
    531: ("Holy Blood", 531, 1, 10),
    # defensive families the Knight owns only past a mid-curve gate
    251: ("Armor Mastery", 251, 1, 20),
    381: ("Shield Barrier", 381, 1, 10),
    421: ("Endure", 421, 1, 10),
}


def lin(a, b, n=10):
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


# profile -> base row -> (power, success, duration, reload, rate); None keeps vanilla.
#
# The three defensive families are **back-loaded**: their shared low ranks are
# written back at their exact vanilla values so a plain Soldier or a Champion
# gains nothing, and the whole payoff lands past the gate where only a Knight can
# reach it. That is where this build's durability finally becomes its own.
PROFILES = {
    "bulwark": {
        # The crowd tool, and the only weapon-formula AoE in the Soldier line.
        511: (lin(250, 720), None, None, lin(70, 50), None),
        # The engine: deny the target its turns and a level deficit stops mattering.
        521: (lin(200, 650), lin(60, 100), lin(4, 6), lin(45, 35), None),
        # The widest AoE a Knight owns, still bought with health.
        531: (lin(280, 800), None, None, lin(75, 60), None),

        # DEF%, Knight-exclusive from rank 11 (20-rank family).
        251: (None, None, None, None,
              [2, 3, 4, 5, 6, 7, 8, 9, 10, 12] + lin(20, 42, 10)),
        # RES%, Knight-exclusive from rank 6.
        381: (None, None, None, None, [9, 12, 15, 18, 21] + lin(30, 56, 5)),
        # DEF%, Knight-exclusive from rank 6. Its SPEED column already shrinks as
        # the defence grows (200 -> 130), so the dig-in trade is built in; left
        # alone deliberately.
        421: (None, None, None, None, [16, 19, 22, 25, 28] + lin(38, 66, 5)),
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
    label, family, first, count = FAMILIES[base]
    out = []
    for n in range(count):
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
        label, _fam, first, _n = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]})")
        print(f"  {'rank':>5}{'power':>16}{'stun ratio':>16}"
              f"{'stun secs':>14}{'cooldown s':>16}{'rate %':>14}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            cells = []
            for i, (w, v) in enumerate(zip(was, want[r])):
                if i == 3:
                    w, v = w * 0.2, v * 0.2
                    cells.append(f"{w:.1f} -> {v:.1f}" if w != v else f"{w:.1f} =")
                elif w == v == 0:
                    cells.append("-")
                else:
                    cells.append(f"{w} -> {v}" if w != v else f"{w} =")
            print(f"  {first + n:>5}{cells[0]:>16}{cells[1]:>16}"
                  f"{cells[2]:>14}{cells[3]:>16}{cells[4]:>14}")


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
