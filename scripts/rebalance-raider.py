"""Give the Raider a kit worth pressing, and make katar and dual actually differ.

    python scripts/rebalance-raider.py --profile assassin
    python scripts/rebalance-raider.py --restore

Same machinery and sidecar contract as the other passes. The stat side of the
katar/dual split lives in `rebalance-raider-weapons.py`.

Why this pass exists
--------------------
Two problems, one corrective and one design.

**Corrective.** The Raider's own exclusive skills were the weakest part of its
own toolkit: 7-27 damage/second against the 132 it inherits from Triple Attack
and the 70 from Screw Attack. Its "signature" Prime Hit did 25. Splitting skills
by weapon accomplishes nothing while neither skill is worth casting, so the
numbers come first.

**Design.** The katar/dual choice is real in the *weapons* and invisible in the
*skills*:

    Katar (type 251)  median atk 132, max 378, delay 8 (7 at the top end)
    Dual  (type 252)  median atk 178, max 467, delay 10

Fast-and-light versus slow-and-heavy, already true in LIST_WEAPON.STB. But every
melee Raider skill requires `251,252` -- both -- so the skills are blind to which
you hold and the choice collapses to "pick the bigger number". Both types run to
level 230 with four endgame options each, so neither is a dead end.

What changed
------------
**Two skills become weapon-exclusive**, giving each build a signature the other
cannot press:

    Power Burst  -> KATAR only   fast and repeatable: 8.0 -> 6.0 s
    Prime Hit    -> DUAL only    the heavy hit:      1150 power at 11.0 s

Only these two, and both are Raider-exclusive, so nothing a base Hawker already
uses is taken away. The gate is soft: `CUserDATA::CheckNeedWeapon` refuses the
cast with a "need equipment" message, costs no SP and leaves the skill learned,
so a player may carry both weapons and swap. That is deliberate -- a build should
be a lean, not a cage.

**The two thrown knives are promoted from afterthought to identity.** They are
the most distinctive thing the class owns: 30 m debuff throws that work with any
weapon, which makes the Raider an opener rather than a melee body. Poison Knife
27 -> 80 damage/second, Magickal Knife 17 -> 60. Magickal Knife stays the lower
of the two on purpose: -30% resistance helps the *party's* casters more than the
Raider's own physical hits, so it is priced as a setup tool.

**Red Cloud** is the only Raider AoE and was doing 7 damage/second. At 50 it is
worth pressing on a group while still being mainly a -40% dodge strip.

Cooldowns and uptime
--------------------
Four of the seven exclusives charged SP to make the skill *slower* -- the
retail-wide quirk described in `reference_skill_cooldown_uptime_traps`:

    Power Burst   8.0 -> 11.6 s      Red Cloud  16.0 -> 19.6 s
    Prime Hit    14.0 -> 17.6 s      Stealth    80.0 -> 104.0 s

All four now improve with rank.

**Stealth could never be held up.** Its cooldown exceeded its own duration at
every rank (80 s / 50 s at rank 1, 104 s / 90 s at rank 5), the same hole Speed
Shot had. It now runs 48 -> 40 s against a 50 -> 90 s duration. Note the duration
ramps as well, so a curve chosen to clear rank 5 can still leave rank 1 short --
that mistake was made once already on Speed Shot and is checked for here.

Where the numbers come from
---------------------------
The other advanced classes sit at 67-117 damage/second (Knight 67-93, Champion
109-117, Artisan 71), so the Raider's exclusives are aimed at that band rather
than at the Hawker base's 38-75. The result is a ladder with no dominant option:

    Triple Attack  132/s   inherited, x3, and by far the most MP/second
    Prime Hit      105/s   DUAL only
    Power Burst    100/s   KATAR only
    Poison Knife    80/s   30 m, poison
    Screw Attack    70/s   inherited, -DEF
    Magickal Knife  60/s   30 m, -RES
    Red Cloud       50/s   14 m AoE, -dodge

Triple Attack still leads on raw output, which is correct -- it costs the most
mana per second and carries no utility at all. Everything else buys something.

Deliberately untouched
----------------------
Mana Blood (the HP -> MP sustain engine; 280 HP -> 380 MP is already a fair
rate and its cooldown already improves) and Combat Mastery ranks 11-20.

Where the stat difference lives
-------------------------------
Not here. This pass splits the *skills*; the katar/dual **stat** difference is
`rebalance-raider-weapons.py`, which puts +CRITICAL on katars and +ATK on dual
wields as weapon bonus stats.

That was originally attempted as a pair of weapon-gated passive skills, which
needed C++: retail's weapon switches fold katar (251) and dual (252) into one
shared index and cannot tell them apart. That work was written, then deleted --
`LIST_WEAPON.STB` already has bonus-stat slots the engine reads, and the shipped
data already uses these exact stats on these exact weapon types. The item route
is also better: the bonus is visible in the tooltip, and it scales with gear tier
and therefore with level, which matters because crit chance carries a level
penalty.

Worth knowing when reading the numbers above: dodge and crit are driven by
*competing* stats -- `AVOID = (DEX + 10) * 0.8 + LEVEL * 0.5` but
`CRITICAL = SENSE + (CON + 20) * 0.2` -- and the endgame katar demands 335 DEX.
A pure-DEX Raider crits almost never at level 230 even with the weapon bonus.
That is deliberate: the katar gets you to the threshold and your stat spread
decides whether you cross it.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKILL_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.raider.json")

# Game column indices, from src/common/io_skill.h.
COL_NAME = 0
COL_FAMILY = 1      # SKILL_1LEV_INDEX, stable across a mid-curve rename
COL_LEVEL = 2       # SKILL_LEVEL, the rank within the family
COL_POWER = 9       # SKILL_POWER
COL_COST = 17       # SKILL_USE_VALUE(s, 0); the property is col 16, AT_MP on
                    # every row here except Mana Blood, which is out of scope.
COL_RELOAD = 20     # SKILL_RELOAD_TIME, x 0.2 s
COL_WEAPON0 = 30    # SKILL_NEED_WEAPON(s, 0) -- 251 katar, 252 dual
COL_WEAPON1 = 31    # SKILL_NEED_WEAPON(s, 1); 0 terminates the list

WRITTEN = (COL_POWER, COL_RELOAD, COL_COST, COL_WEAPON0, COL_WEAPON1)

KATAR, DUAL = 251, 252

# base row -> (label, family, first rank, rank count).
FAMILIES = {
    1511: ("Power Burst", 1511, 1, 10),
    1611: ("Prime Hit", 1611, 1, 10),
    1841: ("Stealth", 1841, 1, 5),
    1851: ("Poison Knife", 1851, 1, 10),
    1861: ("Red Cloud", 1861, 1, 10),
    1871: ("Magickal Knife", 1871, 1, 10),
}


def lin(a, b, n=10):
    """Linear rank ramp, rounded."""
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


def cd(a, b, n=10):
    """Cooldown ramp in SECONDS -> SKILL_RELOAD_TIME units (x0.2 s)."""
    return [round(v / 0.2) for v in (a + (b - a) * k / (n - 1) for k in range(n))]


# profile -> base row -> (power, reload, mp, weapon0, weapon1).
# None leaves that column vanilla.
PROFILES = {
    "assassin": {
        # --- weapon-exclusive signatures --------------------------------
        # KATAR: fast and repeatable. 100 dmg/s at rank 10.
        1511: (lin(180, 600), cd(8.0, 6.0), lin(45, 95), [KATAR] * 10, [0] * 10),
        # DUAL: the heavy hit. 105 dmg/s, and the biggest single number the
        # line owns -- in range of the Champion's 1200.
        1611: (lin(380, 1150), cd(14.0, 11.0), lin(70, 150), [DUAL] * 10, [0] * 10),

        # --- the thrown knives, promoted --------------------------------
        1851: (lin(180, 560), cd(9.0, 7.0), lin(45, 95), None, None),   # 80 dmg/s
        1871: (lin(140, 420), cd(9.0, 7.0), lin(30, 65), None, None),   # 60 dmg/s

        # --- the only AoE -----------------------------------------------
        1861: (lin(200, 700), cd(16.0, 14.0), lin(70, 140), None, None),  # 50 dmg/s

        # --- Stealth: cooldown only, so it can actually be held up -------
        # 5 ranks. Duration runs 50 -> 90 s, so every rank now has slack.
        1841: (None, cd(48.0, 40.0, 5), None, None, None),
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


WNAME = {0: "-", KATAR: "katar", DUAL: "dual"}


def show(stb, profile, want, vanilla):
    for base in sorted(PROFILES[profile]):
        rows = rows_for(stb, base)
        label, _f, first, _n = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]})")
        print(f"  {'rank':>5}{'power':>16}{'cooldown s':>18}{'MP':>13}{'weapon':>24}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            now = want[r]
            pw = f"{was[0]} -> {now[0]}" if was[0] != now[0] else ("-" if not now[0] else f"{now[0]} =")
            cool = (f"{was[1] * 0.2:.1f} -> {now[1] * 0.2:.1f}"
                    if was[1] != now[1] else f"{was[1] * 0.2:.1f} =")
            mp = f"{was[2]} -> {now[2]}" if was[2] != now[2] else f"{now[2]} ="
            wold = "+".join(WNAME.get(w, str(w)) for w in (was[3], was[4]) if w)
            wnew = "+".join(WNAME.get(w, str(w)) for w in (now[3], now[4]) if w)
            wp = f"{wold} -> {wnew}" if wold != wnew else f"{wnew or 'any'} ="
            print(f"  {first + n:>5}{pw:>16}{cool:>18}{mp:>13}{wp:>24}")


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
    print("Restart the game server and the client, and rebake the VFS before deploying.")


if __name__ == "__main__":
    main()
