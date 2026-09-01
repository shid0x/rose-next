"""Give the Artisan something to do in a fight: speed and critical hits.

    python scripts/rebalance-artisan.py --profile gunsmith
    python scripts/rebalance-artisan.py --restore

Same machinery as the other class passes: the sidecar holds the *vanilla* values
plus the active profile name, so applying is idempotent and --restore always
returns the original bytes.

The problem, stated exactly
---------------------------
**The Artisan has zero exclusive attack skills.** Not weak ones -- none. Every
offensive skill in the Dealer tree is class 44 (shared with the whole line) or 67
(Bourgeois-only). It is the only class in the game whose job change adds nothing
whatsoever to a fight; an Artisan's combat kit is a base Dealer's, permanently.

Sixteen families *are* Artisan-exclusive, and every one of them is crafting:
Craft Mastery, Weapon Research, Armor Research, nine craft skills, plus Cart
Craft, Castle Gear, Gem Cutting and Refine Item.

Worse, two of those exclusives are conveniences rather than capabilities. Refine
Item and Item Divide both exist as NPC services **anyone** can walk up to, and
the only difference in `Proc_CRAFT_UPGRADE_REQ` is a single boolean: the skill
pays MP, the NPC pays zuly. Same success, same result. So "I can refine" is not
an Artisan thing; "I can refine without going to town" is.

What this pass does about it
----------------------------
The three Artisan-exclusive *passives* all have an **empty second ability slot**
(`SKILL_INCREASE_ABILITY(s,1)` at cols 24/25/26, verified zero across every
rank). So a combat bonus can be *added* to the ranks the Artisan exclusively
owns without touching what is already there:

    Craft Mastery    r6+   keeps MaxMP%,  adds  +6..20%  gun attack speed
    Weapon Research  r11+  keeps CON,     adds  +5..20%  critical

Both are back-loaded past their own class gates (`SKILL_AVAILBLE_CLASS_SET` is
declared per rank -- see the note below), so the shared lower ranks are untouched
and a Bourgeois gains nothing at all.

**Armor Research is deliberately left alone.** It grants DEF%, and the brief for
this class is "very weak defence, relies on speed and crits". Buffing its armour
would work against the identity, so its Artisan-exclusive ranks stay vanilla.

Why these two abilities specifically
------------------------------------
* `AT_PSV_ATK_SPD_GUN` (50) is read by `CUserDATA::GetPassiveSkillAttackSpeed`,
  which maps weapon classes 232/233/253 -- gun, launcher, dual gun -- onto it. It
  is a **rate**, applied as `fCurSpeed * rate / 100`, so it scales with the
  weapon rather than going stale. Attack speed is also a straight multiplier on
  damage output, which is why it is the one bonus in this game that does not
  decay with level.
* `AT_PSV_CRITICAL` (100) is read by `CUserDATA::Cal_CRITICAL` as
  `value + critical * rate / 100`. Using the rate rather than the flat means it
  grows with the SENSE the class already wants.

**Critical is a low-ceiling stat here, and the curve is small on purpose.**
`Get_CriSuccessRATE` crits when `((1+rand(100))*3 + lv + 30) * 16 / (crit + 70)`
comes out under 20, and critical is driven by SENSE -- which this whole line
stacks. A Dealer-line character therefore already crits often before any passive
at all:

    level      60    100    140    180    220
    base      39%    48%    57%    65%    77%
    +20%      47%    59%    72%    85%    99%
    +40%      56%    73%    90%   100%   100%

At +40% it reaches a permanent, varianceless 100% by level 180, which is why the
curve stops at +20%. Even that is nearly capped at 220. The honest reading is
that this passive is worth a great deal across the two hundred levels of getting
there and very little once you arrive. If it feels redundant at cap, the drop-in
replacement is `AT_PSV_HIT` (99): accuracy does not saturate, and it is what
gates fighting monsters above your own level.

Both indices are inside `int m_iAddValue[AT_MAX]` (AT_MAX = 109), which is what
`GetPassiveSkillValue` indexes -- worth checking before inventing a passive,
since an out-of-range ability code is silent.

Note the Artisan stacks its gun speed **on top of** Combat Mastery (+2..15%,
shared with the whole Dealer line), so a fully invested Artisan reaches about
+35% attack speed from passives before Union Weapon's buff is counted.

What this does not fix
----------------------
This is a floor, not a ceiling. The Artisan still has no attack skill of its
own, and no amount of passive tuning invents one -- it is the clearest case in
the game for an imported skill, precisely because there is nothing here to
rebalance. Treat this as making the class playable rather than finished.

On making crafting worth it
---------------------------
Recorded here because it is the obvious next question. An item's bonus is up to
**two flat stat bonuses** from `LIST_JEMITEM`, reached either by *appraising* a
random rolled option or by *socketing* a cut gem:

    if (item.GetGemNO() && (item.IsAppraisal() || item.HasSocket()))
        for (i = 0; i < 2; i++)
            m_iAddValue[GEMITEM_ADD_DATA_TYPE(gem, i)] += GEMITEM_ADD_DATA_VALUE(gem, i);

378 gem rows, 356 with bonuses, across 18 stats. The strongest available are
ATK+55, MaxHP+70, DEF+40, the base stats at +35, **ATK_SPD+25** and CRIT+20.

Those are flat, so they decay like everything else -- ATK+55 is 6.5% of a
level-200 character. **Attack speed is the exception**, because it multiplies
rather than adds: a +25 speed gem is worth 10-17% at endgame and stays worth it.
That is a neat fit with this class, since the Artisan is both the one who cuts
gems and the one who benefits most from wearing speed.

Raising the top of the gem table would make crafting matter -- but it would make
it matter for whoever *wears* the gem, not whoever cut it, which turns the
Artisan into a service rather than a character. That is a separate decision and
deliberately not taken here.

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
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.artisan.json")

# LIST_SKILL columns (src/common/io_skill.h). Ability slot 1 is 21/22/23 and is
# never touched here; slot 2 is 24/25/26 and is empty on all three passives.
SK_NAME, SK_FAMILY, SK_LEVEL = 0, 1, 2
SK_ABIL2, SK_ABIL2_VAL, SK_ABIL2_RATE = 24, 25, 26

# t_AbilityINDEX, src/common/shared/datatype.h
AT_PSV_ATK_SPD_GUN = 50     # -> GetPassiveSkillAttackSpeed, weapon 232/233/253
AT_PSV_CRITICAL = 100       # -> Cal_CRITICAL

WRITTEN = (SK_ABIL2, SK_ABIL2_VAL, SK_ABIL2_RATE)

# base row -> (label, SKILL_1LEV_INDEX, first rank, rank count, gate rank)
FAMILIES = {
    2081: ("Craft Mastery", 2081, 1, 10, 6),
    2111: ("Weapon Research", 2111, 1, 20, 11),
}


def lin(a, b, n):
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


def gated(ability, rates, shared, total):
    """Ability slot 2 written only past the class gate.

    `shared` ranks get a zeroed slot -- the same nothing they have in vanilla --
    and the remaining ranks carry the ability. Returns the three column curves.
    """
    exclusive = total - shared
    assert len(rates) == exclusive, (len(rates), exclusive)
    return ([0] * shared + [ability] * exclusive,      # col 24, ability type
            [0] * total,                                # col 25, flat (unused)
            [0] * shared + rates)                       # col 26, rate


PROFILES = {
    "gunsmith": {
        # Artisan-exclusive from rank 6: gun attack speed, stacking on top of
        # Combat Mastery's shared +2..15%.
        2081: gated(AT_PSV_ATK_SPD_GUN, lin(6, 20, 5), shared=5, total=10),
        # Artisan-exclusive from rank 11: critical, as a rate so it grows with
        # the SENSE this class already wants.
        2111: gated(AT_PSV_CRITICAL, lin(5, 20, 10), shared=10, total=20),
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
    label, family, first, count, _gate = FAMILIES[base]
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
    NAMES = {0: "-", AT_PSV_ATK_SPD_GUN: "gun atk speed", AT_PSV_CRITICAL: "critical"}
    for base in sorted(PROFILES[profile]):
        rows = rows_for(stb, base)
        label, _fam, first, _cnt, gate = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]}, Artisan-exclusive from rank {gate})")
        print(f"  {'rank':>5}{'2nd ability':>18}{'rate %':>16}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            wt, _wv, wr = was
            nt, _nv, nr = want[r]
            ab = (NAMES.get(nt, nt) if wt == nt
                  else f"{NAMES.get(wt, wt)} -> {NAMES.get(nt, nt)}")
            rate = f"{wr} -> {nr}" if wr != nr else ("-" if nr == 0 else f"{nr} =")
            print(f"  {first + n:>5}{ab:>18}{rate:>16}")


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
