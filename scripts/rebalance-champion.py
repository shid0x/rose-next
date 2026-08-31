"""Make the Champion a heavy hitter who pays for damage in blood.

    python scripts/rebalance-champion.py --profile berserker
    python scripts/rebalance-champion.py --restore

Same machinery as the other class passes: the sidecar holds the *vanilla* values
plus the active profile name, so applying is idempotent and --restore always
returns the original bytes.

Read `rebalance-soldier-skills.py` first -- this pass only makes sense on top of
the ceiling reduction described there. The base Soldier skills are shared with
the Champion, and they had been tuned so high that Champion Hit was weaker than a
skill every Soldier already owned.

The design brief
----------------
"Champion: can use 3 weapons (axe, 2h swords, spear), strong attacker, decent
defence, weak against magic. Strong skills, longer CDs." Plus the thing the data
handed us: **the Champion already pays for power with health**, and that is the
most interesting thing about it.

Almost none of this needed inventing:

* **Champion Hit already enforces the three weapons.** Its SKILL_NEED_WEAPON
  list is exactly 2H sword / 2H spear / 2H axe -- no one-handers. The weapon
  identity was always in the data.
* **Two-Hand Mastery is already the strongest weapon mastery in the game**, at
  +17%..36% over ten ranks, against Bow Mastery's 30% over twenty and the
  crossbow's 26%. Untouched here; it did not need help.
* **The blood price runs through the whole line already.** Leap Attack costs
  AT_HP (not mana -- see the cost-column note below), and the Knight's Holy Blood
  costs 150 HP. Champion Hit joining them makes a theme out of what was an
  oddity.

What this pass changes
----------------------
The Champion has exactly two exclusive attack skills, so both matter.

    Champion Hit    power 160->330  =>  400->1200   cd 12.0->15.6s => 14.0->11.0s
                    cost  70->110 MP  =>  120->320 HP  +  40->80 MP
    Tendon Slash    power 100->200  =>  250->700    cd  7.0->8.8s  =>  8.0->6.0s

Champion Hit becomes the biggest single hit in the Soldier line and is paid for
in blood: 320 HP at rank 10 is about 14% of a level-140 Champion's health, every
eleven seconds. Tendon Slash is the other half -- SKILL_TYPE 6 with a 20 m range
and no two-hander requirement, so it is the Champion's only way to touch
something it cannot reach.

Both also had the cooldown-lengthens-with-rank anti-pattern that runs through
this whole table, and both now shorten with investment instead.

The blood economy
-----------------
The interesting consequence is a loop that nobody designed and the data supports:

    Blood Attack   (base, class 41)   heals 350->1000 on hit
    Leap Attack    (base, class 41)   costs  50->110 HP, stuns
    Champion Hit   (Champion only)    costs 120->320 HP, biggest hit in the line

So a Champion spends health to hit hard and wins it back by hitting more. Blood
Attack stops being a footnote and becomes the thing that funds the rotation.
It is safe by construction: `Skill_GetAbilityValue(AT_HP)` returns
`GetCur_HP() - 1`, so the affordability check can never let a skill kill you.

Things worth knowing
--------------------
* **The cost columns are (property, value) pairs.** `SKILL_USE_PROPERTY(s,0)` is
  col 16 with its value in col 17; slot 1 is cols 18/19. The property is a
  `t_AbilityINDEX`, so col 17 means nothing on its own -- it is mana for most
  skills, health for Leap Attack, and money for the Bourgeois mercenaries. This
  pass writes both slots explicitly, which is also how Champion Hit's price is
  changed from mana to blood.
* **"Weak against magic" is deliberately not implemented.** It would mean a
  self-debuff passive, and it would barely register: only ~12% of monsters deal
  magic damage at all (`NPC_IS_MAGIC_DAMAGE`), falling to 5-7% at endgame. It is
  really a PvP trait, and the honest place for it is gear and stat choices rather
  than an invented penalty skill.
* **Tendon Slash works with one-handers too**, which dilutes the two-hander
  identity slightly. Left alone on purpose: it is the Champion's only ranged
  option, and Champion Hit is where the two-hander commitment is already paid
  off. Restricting it is a one-column change (SKILL_NEED_WEAPON, cols 30-34) if
  that turns out to be the wrong call.
* The Champion's other capstone-shaped thing is Two-Hand Mastery, which is
  already best-in-game and untouched.

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
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.champion.json")

# LIST_SKILL columns (src/common/io_skill.h)
SK_NAME, SK_FAMILY, SK_LEVEL = 0, 1, 2
SK_POWER = 9
# Cost is up to two (property, value) pairs; the property is a t_AbilityINDEX, so
# a value column means nothing without it. Both slots are written explicitly.
SK_PROP0, SK_VAL0 = 16, 17
SK_PROP1, SK_VAL1 = 18, 19
SK_RELOAD = 20

AT_HP, AT_MP = 16, 17           # t_AbilityINDEX, src/common/shared/datatype.h

WRITTEN = (SK_POWER, SK_PROP0, SK_VAL0, SK_PROP1, SK_VAL1, SK_RELOAD)
RANKS = 10

# base row -> (label, SKILL_1LEV_INDEX, first rank)
FAMILIES = {
    661: ("Tendon Slash", 661, 1),
    671: ("Champion Hit", 671, 1),
}


def lin(a, b, n=RANKS):
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


# profile -> base row -> (power, prop0, val0, prop1, val1, reload); None keeps vanilla.
PROFILES = {
    "berserker": {
        # The reach. Type 6, 20 m, usable with any weapon -- the Champion's only
        # answer to something it cannot walk to.
        661: (lin(250, 700), None, None, None, None, lin(40, 30)),
        # The capstone, and the blood price: biggest single hit in the Soldier
        # line, bought with health rather than mana.
        671: (lin(400, 1200), [AT_HP] * 10, lin(120, 320),
              [AT_MP] * 10, lin(40, 80), lin(70, 55)),
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
    AT = {0: "", AT_HP: " HP", AT_MP: " MP"}
    for base in sorted(PROFILES[profile]):
        rows = rows_for(stb, base)
        label, _fam, first = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]})")
        print(f"  {'rank':>5}{'power':>16}{'cost slot 0':>20}"
              f"{'cost slot 1':>16}{'cooldown s':>16}")
        for n, r in enumerate(rows):
            wp, wp0, wv0, wp1, wv1, wrl = vanilla.get(r) or tuple(
                gi(stb, r, c) for c in WRITTEN)
            np_, np0, nv0, np1, nv1, nrl = want[r]

            def slot(op, ov, xp, xv):
                if not op and not xp:
                    return "-"
                a, b = f"{ov}{AT.get(op, op)}", f"{xv}{AT.get(xp, xp)}"
                return a + " =" if a == b else f"{a} -> {b}"

            pw = f"{wp} -> {np_}" if wp != np_ else f"{wp} ="
            cd = (f"{wrl * 0.2:.1f} -> {nrl * 0.2:.1f}" if wrl != nrl
                  else f"{wrl * 0.2:.1f} =")
            print(f"  {first + n:>5}{pw:>16}{slot(wp0, wv0, np0, nv0):>20}"
                  f"{slot(wp1, wv1, np1, nv1):>16}{cd:>16}")


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
