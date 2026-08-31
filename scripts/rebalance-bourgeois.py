"""Make the Bourgeois identity real: hired mercenaries and better loot.

    python scripts/rebalance-bourgeois.py --profile tycoon
    python scripts/rebalance-bourgeois.py --restore

Same machinery as `rebalance-dealer-skills.py` / `rebalance-soldier-skills.py`:
the sidecar holds the *vanilla* values plus the active profile name, so applying
is idempotent and --restore always returns the original bytes. Unlike those two
this pass writes **two** tables, LIST_SKILL.STB and LIST_NPC.STB.

The design brief
----------------
"Bourgeois: gets better loot (stockpile), can hire mercenaries to fight for him,
carries heavy launcher, slow attack but strong, weak defense. The rich man
class." This pass covers the first two pillars, which already exist in the
engine and only needed tuning. The launcher/armour half is item data and is a
separate exercise.

Pillar 1: mercenaries
---------------------
Three summon skills already exist -- Employ Warrior (2351), Employ Hunter (2361,
Bourgeois-only) and Terror Knight (2371, Bourgeois-only) -- and the summon
control + info panel work is already in the client. What was wrong is that they
were not worth their price and they fade with level.

`CObjSUMMON::SetCallerOBJ` scales a summon at creation from the NPC row by
skill rank and owner level:

    maxHP = NPC_HP  * (rank + 16) * (ownerLv + 85)  / 2600
    ATK   = NPC_ATK * (rank + 22) * (ownerLv + 100) / 4000

Measured against the median field monster of the owner's level, that gives:

    owner lv  rank   merc HP vs mob   merc ATK vs mob
          55     1        1.10-1.26x        0.70-0.91x
         100    10        1.04-1.26x        0.79-1.12x
         160    10        0.76-0.92x        0.58-0.81x
         220    10        0.46-0.56x        0.40-0.57x

So a mercenary starts as a rough equal and ends as half a monster. This pass
raises the NPC rows (HP x1.3, ATK x1.5) and cuts the MP prices, which fixes the
low and mid game. **It does not fix the endgame decay, and cannot**: the
owner-level term above is linear while monster HP grows far faster with level,
so no multiplier on the NPC row holds the ratio. Fixing that means changing the
scaling formula itself -- which lives on the server in `SetCallerOBJ` *and* is
mirrored in the client (`recvpacket.cpp`, for the summon info panel), so both
sides have to move together. Deliberately out of scope here.

MP was the other problem: Employ Warrior cost 150 MP at rank 1 when a level-55
Dealer has about 290 MP total, so fielding a squad was impossible. Prices now
run 90->170 / 110->210 / 150->280, which buys roughly three Warriors at level 55
and leaves you dry -- MP is the governor, deliberately (see the note below).

Pillar 2: better loot
---------------------
`CCal::Get_DropITEM` takes two player-supplied numbers, and both are already
wired to Dealer passives that nobody notices because the values are tiny:

* `iDropRate` <- `GetCur_DropRATE()` <- `AT_PSV_DROP_RATE`, granted by
  **Gathering** (2091). It adds directly to the drop roll, so it controls how
  often anything drops at all.
* `iCharm` <- `Get_CHARM()` <- `AT_CHARM`, granted by **Economy Research**
  (2001). It feeds `m_nGEM_OP` -- the **bonus stat rolled onto the dropped
  item**. This is literally "the rich man finds better gear" and it was sitting
  there unused.

Simulated on the real formula against a level-52 monster, 200k drops each:

    dropRate  charm    drop %   mean gemOP   with a bonus stat
           0      0     61.0%          0.9                5.1%
          18     40     79.0%          5.5               12.0%     <- vanilla max
          18    120     79.1%         14.1               27.9%
          60    300    100.0%         30.6               61.9%

Vanilla maxes at +18 drop rate and +40 charm. This pass takes them to +50 and
+150, which roughly triples the rate of bonus-stat drops and is actually
noticeable in play.

Things worth knowing
--------------------
* **The summon cap is not enforced, so Employ Troops is dead data.**
  `GetMax_SummonCNT()` is `50 + GetPassiveSkillValue(AT_PSV_SUMMON_MOB_CNT)`,
  each mercenary costs `NPC_NEED_SUMMON_CNT` (40-85 for these three), and the
  costs are beautifully judged -- at base you can afford one Warrior but not
  two, and no Terror Knight at all; maxed Employ Troops reaches 170, enough for
  three Warriors or two Terror Knights. But nothing ever compares the two:
  `SKILL_TYPE_14` in `CObjCHAR::Skill_START` summons unconditionally and only
  calls `Add_SummonCNT` afterwards. `Max_SummonCNT()` is computed and put in a
  packet and never read. So MP is currently the only limit on squad size, which
  is why the MP prices here are not cut further than they are.
  Enforcing it is a small server change, and the trap is that `Max_SummonCNT()`
  is virtual on `CObjCHAR` returning **0** for non-users -- gate it on
  `IsUSER()` or monsters lose the ability to summon entirely.
* **Mercenary NPC rows 851-880 are summon-only.** Checked against every map
  REGEN lump in `data/`: they are spawned by no map, so raising their stats
  cannot leak into field monsters.
* **Economy Research and Gathering are class 44 ("Dealer Job"), not 67.** They
  are shared with the Artisan branch, so this buffs loot for the whole Dealer
  line, not the Bourgeois alone. Making them Bourgeois-exclusive means changing
  `SKILL_AVAILBLE_CLASS_SET` (col 35) from 44 to 67 -- but 67 is 421/431 only,
  so a base Dealer could no longer learn them at all, which moves them behind
  the job change. That is a progression decision, not a tuning one, so it is
  left alone here.
* `m_nGEM_OP` is assigned as `iITEM_OP % (mobLevel + 70)`, so the mapping from
  charm to bonus stat is **not monotonic** -- a higher roll can wrap to a lower
  option. The simulated averages above already include that effect; do not
  expect a clean linear return on charm.
* Summons no longer expire on a timer (the lifetime HP-drain was removed, see
  `status_effects.cpp`), so they persist until killed. That matters if the cap
  is ever enforced: the counter decrements in `CObjMOB::Dead` and is cleared on
  owner death and zone warp.

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
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
SKILL_STB = os.path.join(STB_DIR, "LIST_SKILL.STB")
NPC_STB = os.path.join(STB_DIR, "LIST_NPC.STB")
SIDECAR = os.path.join(STB_DIR, "LIST_SKILL.bourgeois.json")

# LIST_SKILL columns (src/common/io_skill.h)
SK_NAME, SK_FAMILY, SK_LEVEL = 0, 1, 2
SK_MP = 17          # SKILL_USE_VALUE(s, 0)
SK_RELOAD = 20      # SKILL_RELOAD_TIME, x 0.2s
SK_ABILITY_VAL = 22  # SKILL_INCREASE_ABILITY_VALUE(s, 0); col 21 says what it means

# LIST_NPC columns (src/common/include/rose/io/stb.h)
NPC_HP, NPC_ATK = 8, 9

SKILL_COLS = (SK_MP, SK_RELOAD, SK_ABILITY_VAL)
NPC_COLS = (NPC_HP, NPC_ATK)


def lin(a, b, n=10):
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


# --- skill families: base row -> (label, family col value, first rank, ranks)
SKILL_FAMILIES = {
    2001: ("Economy Research (CHARM)", 2001, 1, 20),
    2091: ("Gathering (drop rate)", 2091, 1, 10),
    2351: ("Employ Warrior", 2351, 1, 10),
    2361: ("Employ Hunter", 2361, 1, 10),
    2371: ("Terror Knight", 2371, 1, 10),
}

# profile -> base row -> (mp, reload, ability_value); None keeps vanilla.
SKILL_PROFILES = {
    "tycoon": {
        # loot passives: the two numbers Get_DropITEM actually reads
        2001: (None, None, lin(6, 150, 20)),    # AT_CHARM  -> bonus stat on drops
        2091: (None, None, lin(12, 50)),        # AT_PSV_DROP_RATE -> drop chance
        # mercenaries: affordable enough to field a squad, MP still the governor
        2351: (lin(90, 170), [25] * 10, None),   # Employ Warrior, 5.0s
        2361: (lin(110, 210), [30] * 10, None),  # Employ Hunter, 6.0s
        2371: (lin(150, 280), [40] * 10, None),  # Terror Knight, 8.0s
    },
}

# profile -> (first npc row, last npc row, hp multiplier, atk multiplier)
NPC_PROFILES = {
    "tycoon": (851, 880, 1.30, 1.50),
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


def skill_rows(stb, base):
    """Rank rows of one family, identified by the family column, not the name."""
    label, family, first, count = SKILL_FAMILIES[base]
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
        return None, {}, {}
    with open(SIDECAR, encoding="utf-8") as fh:
        raw = json.load(fh)
    return (raw["profile"],
            {int(k): tuple(v) for k, v in raw["skills"].items()},
            {int(k): tuple(v) for k, v in raw["npcs"].items()})


def write_sidecar(profile, skills, npcs):
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"profile": profile,
                   "skills": {str(k): list(v) for k, v in sorted(skills.items())},
                   "npcs": {str(k): list(v) for k, v in sorted(npcs.items())}},
                  fh, indent=1)


def targets(sk, npc, profile, van_sk, van_npc):
    """({skill row: vals}, {npc row: vals}) the profile should produce."""
    want_sk = {}
    for base, curves in SKILL_PROFILES[profile].items():
        for n, r in enumerate(skill_rows(sk, base)):
            base_vals = van_sk.get(r) or tuple(gi(sk, r, c) for c in SKILL_COLS)
            want_sk[r] = tuple(curve[n] if curve else base_vals[i]
                               for i, curve in enumerate(curves))
    first, last, mhp, matk = NPC_PROFILES[profile]
    want_npc = {}
    for r in range(first, last + 1):
        hp, atk = van_npc.get(r) or (gi(npc, r, NPC_HP), gi(npc, r, NPC_ATK))
        want_npc[r] = (round(hp * mhp), round(atk * matk))
    return want_sk, want_npc


def apply_vals(stb, values, cols):
    for r, vals in values.items():
        for c, v in zip(cols, vals):
            stb.set(r, c, str(v))


def save(sk, npc):
    with open(SKILL_STB, "wb") as fh:
        fh.write(sk.to_bytes())
    with open(NPC_STB, "wb") as fh:
        fh.write(npc.to_bytes())


def show(sk, npc, want_sk, want_npc, van_sk, van_npc, profile):
    for base in sorted(SKILL_PROFILES[profile]):
        rows = skill_rows(sk, base)
        label, _fam, first, _n = SKILL_FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]}, ranks {first}-{first + len(rows) - 1})")
        print(f"  {'rank':>5}{'MP':>16}{'cooldown s':>18}{'ability val':>16}")
        for n, r in enumerate(rows):
            was = van_sk.get(r) or tuple(gi(sk, r, c) for c in SKILL_COLS)
            cells = []
            for i, (w, v) in enumerate(zip(was, want_sk[r])):
                if i == 1:
                    w, v = w * 0.2, v * 0.2
                    cells.append(f"{w:.1f} -> {v:.1f}" if w != v else f"{w:.1f} =")
                elif w == v == 0:
                    cells.append("-")
                else:
                    cells.append(f"{w} -> {v}" if w != v else f"{w} =")
            print(f"  {first + n:>5}{cells[0]:>16}{cells[1]:>18}{cells[2]:>16}")

    firstn, lastn, mhp, matk = NPC_PROFILES[profile]
    print(f"\nMercenary NPC stats (LIST_NPC rows {firstn}-{lastn}): "
          f"HP x{mhp}, ATK x{matk}")
    print(f"  {'row':>5}{'name':22}{'HP':>18}{'ATK':>16}")
    for r in range(firstn, lastn + 1):
        was = van_npc.get(r) or (gi(npc, r, NPC_HP), gi(npc, r, NPC_ATK))
        new = want_npc[r]
        if r % 3 and r not in (firstn, lastn):   # keep the listing readable
            continue
        nm = npc.get(r, 0).decode("latin-1", "replace")
        print(f"  {r:>5}{nm[:22]:22}{f'{was[0]} -> {new[0]}':>18}"
              f"{f'{was[1]} -> {new[1]}':>16}")
    print("  (every row in the range is written; listing thinned for readability)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--profile", choices=sorted(SKILL_PROFILES))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_stb_module()
    sk = oro.Stb(SKILL_STB)
    npc = oro.Stb(NPC_STB)
    active, van_sk, van_npc = read_sidecar()

    if args.restore:
        if not van_sk and not van_npc:
            sys.exit("no sidecar -- nothing to restore")
        apply_vals(sk, van_sk, SKILL_COLS)
        apply_vals(npc, van_npc, NPC_COLS)
        save(sk, npc)
        os.remove(SIDECAR)
        print(f"restored {len(van_sk)} skill rows and {len(van_npc)} NPC rows to "
              f"vanilla (was profile {active!r}); sidecar removed")
        return

    if args.verify:
        if not active:
            sys.exit("no sidecar -- no profile is applied")
        want_sk, want_npc = targets(sk, npc, active, van_sk, van_npc)
        bad = [(r, c, gi(sk, r, c), v) for r, vals in sorted(want_sk.items())
               for c, v in zip(SKILL_COLS, vals) if gi(sk, r, c) != v]
        bad += [(r, c, gi(npc, r, c), v) for r, vals in sorted(want_npc.items())
                for c, v in zip(NPC_COLS, vals) if gi(npc, r, c) != v]
        print(f"profile {active!r}: {len(want_sk)} skill rows + {len(want_npc)} NPC "
              f"rows, {len(bad)} columns do not match")
        for r, c, got, exp in bad:
            print(f"    row {r} col {c}: {got} != {exp}")
        sys.exit(1 if bad else 0)

    if not args.profile:
        print(f"active profile: {active!r}" if active else "no profile applied (vanilla)")
        print(f"available: {', '.join(sorted(SKILL_PROFILES))}")
        print("pass --profile <name> to apply one, or --restore to go back to vanilla")
        return

    if active == args.profile:
        print(f"profile {args.profile!r} is already applied -- nothing to do.")
        print("use --restore to go back to vanilla.")
        return

    if active:
        print(f"switching profile {active!r} -> {args.profile!r}\n")
        apply_vals(sk, van_sk, SKILL_COLS)
        apply_vals(npc, van_npc, NPC_COLS)
    else:
        van_sk = {r: tuple(gi(sk, r, c) for c in SKILL_COLS)
                  for base in SKILL_PROFILES[args.profile] for r in skill_rows(sk, base)}
        firstn, lastn, _, _ = NPC_PROFILES[args.profile]
        van_npc = {r: (gi(npc, r, NPC_HP), gi(npc, r, NPC_ATK))
                   for r in range(firstn, lastn + 1)}

    want_sk, want_npc = targets(sk, npc, args.profile, van_sk, van_npc)
    show(sk, npc, want_sk, want_npc, van_sk, van_npc, args.profile)
    apply_vals(sk, want_sk, SKILL_COLS)
    apply_vals(npc, want_npc, NPC_COLS)

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    save(sk, npc)
    write_sidecar(args.profile, van_sk, van_npc)

    chk_sk, chk_npc = oro.Stb(SKILL_STB), oro.Stb(NPC_STB)
    for r, vals in want_sk.items():
        for c, v in zip(SKILL_COLS, vals):
            if gi(chk_sk, r, c) != v:
                sys.exit(f"verify failed: skill row {r} col {c} = {gi(chk_sk, r, c)}, want {v}")
    for r, vals in want_npc.items():
        for c, v in zip(NPC_COLS, vals):
            if gi(chk_npc, r, c) != v:
                sys.exit(f"verify failed: npc row {r} col {c} = {gi(chk_npc, r, c)}, want {v}")
    print(f"\ndone -- profile {args.profile!r} applied to {len(want_sk)} skill rows and "
          f"{len(want_npc)} NPC rows, verified. Sidecar: {os.path.basename(SIDECAR)}")
    print("Restart the game server (it caches STBs at startup) and the client. "
          "Rebake the VFS before deploying.")


if __name__ == "__main__":
    main()
