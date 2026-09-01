"""Retune the Dealer's offensive skills. Two profiles, switchable.

    python scripts/rebalance-dealer-skills.py --profile parity   # balance-first
    python scripts/rebalance-dealer-skills.py --profile burst    # feel-first
    python scripts/rebalance-dealer-skills.py --restore          # back to vanilla

Switching profiles is safe and direct -- the sidecar always holds the *vanilla*
values, so `--profile burst` while `parity` is applied restores and re-applies in
one step. Nothing is cumulative.

The report that started this
----------------------------
A level 50-55 Dealer with Twin Shot 4, Power Gun Shot 3 and Gun Smash 2 finds
auto-attacking out-values all three. Replaying the real formulas against the
STBs (`scripts/balance-sim.py` ports them), that is true, for three reasons --
and SKILL_POWER is not one of them. The Dealer's power curves already sit on the
same band as every other physical base class:

    Heavy Attack   (Soldier)  30->100   cd 6.0->6.6s   MP 10->30
    Aim Shot       (Hawker)   30->100   cd 6.0->6.8s   MP 10->30
    Power Attack   (Hawker)   35->110   cd 5.6->6.0s   MP 10->30
    Power Gun Shot (Dealer)   35->100   cd 7.0->7.8s   MP 10->30   <-- cd outlier
    Double Attack  (Sol+Hwk)  40-> 90   cd 5.4->3.6s   MP 20->38
    Twin Shot      (Dealer)   40-> 90   cd 6.0->5.2s   MP 20->38

1. **Power Gun Shot's cooldown is a genuine outlier** -- the slowest tier-1
   attack skill in the game, 17-30% longer than the Soldier and Hawker skills
   carrying the same power and the same MP cost, with nothing compensating.

2. **The Dealer has no fast two-hit skill.** Twin Shot is byte-identical to the
   Hawker's Double Shot, but Soldier *and* Hawker also get Double Attack
   (5.4->3.6s on the same 40->90 power). The Dealer only got the slow variant.

3. **The Dealer's own kit devalues its skills, uniquely.** Combat Mastery
   (AT_PSV_ATK_SPD_GUN, +15% at rank 20) and Union Weapon (AT_ATK_SPD, +53% at
   rank 10) pump auto-attack and do nothing for a fixed cooldown. At level 52
   Power Gun Shot's DPS uplift over pure auto-attacking falls from +21.8% to
   +10.3% once Union Weapon is up. Investing in the class identity halves what
   its attack skills are worth.

Underneath all three: MP, not cooldown, is the binding constraint at that level.
Standing regen is `(GetAdd_RecoverMP() + (CON+40)/6)/6` per 2s (`cobjavt.cpp`)
-- about 90 MP/min for a level-52 Dealer against a 280 MP pool, while casting on
cooldown costs several times that. Auto-attack is free, so auto-attacking really
was the correct play. **Both profiles therefore raise damage per MP.** Cutting
cooldowns alone would have made the felt problem worse.

Profile: parity (balance-first)
-------------------------------
Brings the three low-tier skills level with their peers and stops there. It does
NOT cover the rest of the offensive kit -- those skills have no clean peer to be
at parity *with*, so inventing targets for them would be a different exercise.
Use `burst` for the whole kit.

    Power Gun Shot  power 35->100 => 45->135   cd 7.0-7.8s => 5.6-6.0s
    Twin Shot       power unchanged            cd 6.0-5.2s => 5.4-3.6s
    Smash Gun       power 60->140 => 75->175   cd 9.0s     => 8.4s
    MP              untouched

Twin Shot's cooldown curve is copied verbatim from Double Attack and Power Gun
Shot's from Power Attack, so both are pure parity. At level 52 rank 4 the DPS
uplift over auto-attack goes +24.6% -> +42.3%, +41.7% -> +50.3% and
+29.1% -> +39.0%, against peer references of +34.5%, +50.1% and +27.3%.

Profile: burst (feel-first)
---------------------------
Deliberately *not* balanced, and covering all seven offensive skills. The first
three were built as a clean escalation -- filler, nuke, commitment -- where the
ranking is obvious on purpose. The other four were added afterwards on the
Soldier pass's principle instead: each wins one axis and loses the others, so
which is "best" depends on the situation rather than on a number.

    Twin Shot       filler   power  50->220   cd 5.4->4.4s    MP 12->26
    Power Gun Shot  nuke     power  80->460   cd 10.0->8.4s   MP 10->30
    Smash Gun       slam     power 150->900   cd 16.0->13.0s  MP 25->95
    Sniping Shot    safe hit power 200->850   cd 14.0->11.0s  MP 60->130
    Aim Point       reach    power 150->560   cd 10.0->8.0s   MP 30->65
    Poison Fang     softener power 100->330   cd 12.0->9.0s   MP 30->60
    Zuly Pink       armour   power 180->620   cd 7.0->5.2s    MP 55->150

Smash Gun keeps its 300 (3 m) range on purpose -- that is the risk, and it
carries the reward to match. Its MP curve was later raised from 25->50 to
25->95: at rank 10 it was both the biggest hit *and* the most MP-efficient
skill in the kit, winning four axes of six, because the sim cannot price the
3 m range. The change is negligible at low rank (28 -> 33 MP at rank 2) and
leaves its damage untouched, so how it feels in the hand is unchanged.

Measured at level 80 rank 4 (median field monster, DEF 226, HP 3936):

    skill            burst  exp.tot    cd   mp  cd dps  MPbound  dmg/MP  4 mobs
    Twin Shot          315      315   5.0   17     141      115    18.5     315
    Power Gun Shot     408      408   9.4   17     121      121    24.0     408
    Smash Gun          697      697  15.0   48     125      107    14.5     697
    Sniping Shot       723      723  13.0   83     134       96     8.7     723
    Aim Point          528      528   9.4   42     134      103    12.6     528
    Poison Fang        363      541  11.0   40     127      105    13.5    1077
    Zuly Pink          619      619   6.4   87     175       92     7.1     619

Five skills win six different axes, and the ordering is stable from level 80 to
200: biggest number is Sniping Shot early and Smash Gun later, cooldown-bound
DPS is Zuly Pink, MP-bound DPS and damage-per-MP are Power Gun Shot, and
multi-target is Poison Fang by a wide margin.

The identities, and why they are not interchangeable:

* **Smash Gun vs Sniping Shot** is the sharpest question in the kit: comparable
  damage, but Smash Gun is cheaper and needs you at 3 m, while Sniping Shot is
  safe and costs roughly twice the MP. "Is three metres worth 80 MP?" has no
  fixed answer, which is the point.
* **Zuly Pink is the anti-armour option.** It is SKILL_DAMAGE_TYPE 2, and the
  magic branch divides by `DEF*0.3 + RES + AVOID*0.3 + 60` where the weapon
  branch divides by `DEF + RES*0.8 + AVOID*0.4 + 20` -- so DEF hurts it about a
  third as much. Sampled against real level-80 field monsters it beats Sniping
  Shot on Firegon (DEF 226 / RES 104) and Grand Master Doonga (DEF 379) and
  loses on everything with ordinary armour. Its MP cost is what stops it simply
  being the best.
* **Poison Fang is the crowd softener.** Its damage is single-target but its
  SKILL_SCOPE (800->1250, i.e. 8-12.5 m) spreads the *poison* to a whole pack,
  and against four monsters that is roughly double any other skill in the kit.
* **Aim Point does not win a column, and that is honest.** Its edge is 37-42 m
  of range -- by far the longest here, against Zuly Pink's 25-30 m and weapon
  range for everything else -- which the simulation cannot score. On the numbers
  it is simply the efficient one among the long-range options.

Note damage-per-MP *rises* with the tier among the original three, so the big
buttons are also the efficient ones -- and roughly doubles versus the parity
profile, which makes the burst profile the more MP-sustainable of the two
despite the bigger numbers.

Poison Fang's DoT is NOT tunable here
-------------------------------------
Poison damage is `STATE_APPLY_ABILITY_VALUE(status_row, 0)` in
`status_effects.cpp` -- a flat per-second value read from LIST_STATUS.STB, with
no reference to the caster, the skill or anyone's level. Poison Fang already
escalates through three rows across its ranks (8 "Poisoned 2" = 20/s for ranks
1-5, 9 "Poisoned 3" = 30/s for 6-9, 10 for rank 10), so the progression is
already there. Those rows are **shared** -- row 9 is also the Raider's Poison
Knife, and rows 7/8 are used by monster skills -- so raising the tick would
change poison across the whole game, including what monsters do to players.
Left alone deliberately. It does mean the DoT is flat and decays with level,
which is why the direct damage carries the skill at high level.

Cooldowns stay well inside one fight. Time-to-kill on auto-attacks alone is ~24s
at level 52 and only grows with level, so even Smash Gun's 15s lands 1.6 times
per monster; nothing here is a dead button. Cooldowns shorten with rank rather
than lengthening (retail had Power Gun Shot getting *slower* as you invested in
it), so ranking up always feels like a gain.

Ranks 11-20: Twin Shot continues into Triple Shot
-------------------------------------------------
Rows 2231-2240 are the same family (SKILL_1LEV_INDEX 2221) at ranks 11-20, so
they are included -- otherwise ranking Twin Shot past 10 would be a *downgrade*.

**That downgrade exists in vanilla and is almost certainly a data bug.** The
Soldier/Hawker equivalent, Triple Attack, goes from `power 90 x2 hits` at rank 10
to `power 45 x3 hits` at rank 11 -- the power halves because a third hit is
added, so the total is roughly preserved. Triple Shot has the *identical* halved
power curve (45..100 against Twin Shot's 40..90) but `SKILL_ANI_HIT_COUNT` is
still **2**, making rank 11 strictly worse than rank 10 for a longer cooldown.
The Hawker's Double Shot line has the same defect. The obvious reading is that
Triple Shot was authored as a three-hit skill and its hit count was never
updated.

**The hit count is now corrected to 3**, which was held back until the animation
could be checked rather than assumed. It checks out: `SKILL_ANI_ACTION_TYPE` 92
resolves through `TYPE_MOTION` column 12 (the gun column) to
`3DDATA/MOTION/AVATAR/gun_3attack_m1.zmo`, which is present on disk -- a
purpose-built three-shot gun animation that **Triple Shot already pointed at**
while carrying a hit count of 2. Triple Attack uses the same action type with a
hit count of 3 and works. So the motion services three hits and the third was
simply never switched on.

Power is rescaled to suit: at the old 2-hit curve a third hit would have made
rank 11 a 50% jump over rank 10. Burst runs 175->335 and parity 65->110, both at
3 hits, so rank 11 is a ~1.3x step up from Twin Shot rank 10 rather than a cliff
in either direction.

Caveat worth keeping: the filename and Triple Attack's behaviour are strong
evidence, not proof -- the ZMO's hit-frame events were not parsed. If a third
damage number fails to appear in game, the client is orphaning the event and the
count should go back to 2.

Things worth knowing before changing these numbers
--------------------------------------------------
* **SKILL_ANI_HIT_COUNT (col 70) is only safe to change when the motion backs
  it.** It multiplies damage directly (`wHitCNT` in `Get_SkillDAMAGE`), but the
  client consumes exactly one queued DamageEvent per hit frame of the motion, so
  a count the animation cannot service orphans an event -- the failure mode the
  whole combat-presentation machinery in the client CLAUDE.md exists to handle.
  Triple Shot qualifies because its animation is literally `gun_3attack_m1.zmo`.
  Do not raise it on a skill whose motion has not been checked the same way:
  resolve `SKILL_ANI_ACTION_TYPE` through `TYPE_MOTION` at the skill's **own
  weapon column** and read the `FILE_MOTION` path.
* **AoE is not available on these skills.** `SKILL_SCOPE` (col 8) only spreads
  the *status effect* for SKILL_TYPE 3 (`Skill_ChangeIngSTATUS`); AoE *damage*
  goes through `Skill_DamageToAROUND`, which only types 7 and 17 reach. Giving
  Smash Gun a splash would mean changing its skill type, which changes the
  client casting flow too -- a feature, not a data tweak.
* **Cooldowns here are independent.** `SKILL_RELOAD_TYPE` (col 27) is 0 for all
  three, and the group timer `m_dwLastSkillGroupSpeelTIME` is written in three
  places in `gs_user.cpp` and never read, so group cooldowns are inert. The real
  gate is per hotbar slot (`m_dwLastSkillSpellTIME[slot]`).
* **PVP is separately capped** at 45% of the defender's max HP inside
  `Get_SkillDAMAGE`, so no power value here can make PVP a one-shot.
* `scripts/balance-sim.py`'s gear model picks a req-level-0 "Knight Killer"
  (weapon ATK 179) for every level under 40, so its sub-40 numbers are junk and
  the low-rank values above were never validated against a real early character.
  The rank-1 floors are set conservatively for that reason.
* `balance-sim.py` has **no port of the `default:` branch** of `Get_SkillDAMAGE`
  (SKILL_DAMAGE_TYPE 0), which is what Twin Shot, Double Attack and Double Shot
  all use. It was ported ad hoc for this work; port it again if you need it.

Idempotent: records the vanilla values in a sidecar, so re-running the active
profile is a no-op and --restore always returns to vanilla. --dry-run and
--verify are available. data/ is gitignored, so this file is the only record.
"""
import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKILL_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.dealer-skills.json")

# Game column indices, from src/common/io_skill.h.
COL_NAME = 0        # internal name, English, not the displayed one
COL_FAMILY = 1      # SKILL_1LEV_INDEX, stable across a mid-curve rename
COL_LEVEL = 2       # SKILL_LEVEL, the rank within the family
COL_POWER = 9       # SKILL_POWER
COL_MP = 17         # SKILL_USE_VALUE(s, 0)
COL_RELOAD = 20     # SKILL_RELOAD_TIME, x 0.2s (io_skill.cpp: x200 - 100 ms)
COL_HITS = 70       # SKILL_ANI_HIT_COUNT -- see the Triple Shot note below

WRITTEN = (COL_POWER, COL_RELOAD, COL_MP, COL_HITS)   # order matches the sidecar tuples
RANKS = 10
# base row -> (expected name, SKILL_1LEV_INDEX the rows share, first rank number).
# Twin Shot's family continues under a different *name* at rank 11, so the family
# column is the reliable identifier.
FAMILIES = {
    2201: ("Power Gun Shot", 2201, 1),
    2211: ("Sniping Shot", 2211, 1),
    2221: ("Twin Shot", 2221, 1),
    2231: ("Triple Shot", 2221, 11),      # same family as Twin Shot
    2261: ("Aim Point", 2261, 1),
    2271: ("Poison Fang", 2271, 1),
    2281: ("Smash Gun", 2281, 1),
    2311: ("Zuly Pink", 2311, 1),
}


def lin(a, b, n=RANKS):
    """Linear rank-1..rank-n ramp, rounded. Readable curves beat hand-tuned ones."""
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


# profile -> base row -> (power, reload, mp); None leaves that column vanilla.
PROFILES = {
    "parity": {
        2201: (lin(45, 135), [28, 28, 28, 28, 29, 29, 29, 29, 30, 30], None, None),
        2221: (None, [27, 26, 25, 24, 23, 22, 21, 20, 19, 18], None, None),
        # ranks 11-20. Hit count corrected 2 -> 3, power rescaled to suit.
        2231: (lin(65, 110), lin(20, 18), None, [3] * 10),
        2281: ([75, 86, 97, 108, 119, 130, 141, 152, 163, 175], [42] * 10, None, None),
    },
    "burst": {
        # -- the original three, plus Twin Shot's rank 11-20 continuation
        2201: (lin(80, 460), lin(50, 42), lin(10, 30), None),      # nuke: reliable mid
        2221: (lin(50, 220), lin(27, 22), lin(12, 26), None),      # filler: fast + cheap
        # ranks 11-20. Hit count corrected 2 -> 3, power rescaled to suit: at the
        # old 260->480 a third hit would have been a 50% jump on rank 11.
        2231: (lin(175, 335), lin(21, 18), lin(28, 48), [3] * 10),
        2281: (lin(150, 900), lin(80, 65), lin(25, 95), None),     # slam: 3 m risk, biggest
        # -- the rest of the Dealer's offensive kit
        2211: (lin(200, 850), lin(70, 55), lin(60, 130), None),    # safe big hit, pricey
        2261: (lin(150, 560), lin(50, 40), lin(30, 65), None),     # 37-42 m, efficient
        2271: (lin(100, 330), lin(60, 45), lin(30, 60), None),     # AoE poison softener
        2311: (lin(180, 620), lin(35, 26), lin(55, 150), None),    # anti-armour, fast
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

    Keyed on SKILL_1LEV_INDEX and the rank number rather than the displayed name,
    because Twin Shot's family renames to Triple Shot at rank 11. Guards against a
    data change having shifted the table under us: a silent write to the wrong ten
    rows would be very hard to notice afterwards.
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


def read_sidecar(stb):
    """(profile, {row: (power, reload, mp)}) of vanilla values, or (None, {}).

    Understands the v1 sidecar (a flat row -> [power, reload] map, always the
    parity profile, which never touched MP -- so MP on disk is still vanilla).
    """
    if not os.path.exists(SIDECAR):
        return None, {}
    with open(SIDECAR, encoding="utf-8") as fh:
        raw = json.load(fh)
    if "profile" in raw:
        return raw["profile"], {int(k): tuple(v) for k, v in raw["rows"].items()}
    rows = {int(k): (v[0], v[1], gi(stb, int(k), COL_MP)) for k, v in raw.items()}
    return "parity", rows


def write_sidecar(profile, rows):
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"profile": profile,
                   "rows": {str(k): list(v) for k, v in sorted(rows.items())}},
                  fh, indent=1)


def target_state(stb, profile):
    """{row: (power, reload, mp)} the named profile should produce.

    Vanilla values fill in wherever a profile leaves a column alone, so apply
    and verify read the identical map and cannot drift apart.
    """
    _, vanilla = read_sidecar(stb)
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


def save(stb):
    with open(SKILL_STB, "wb") as fh:
        fh.write(stb.to_bytes())


def show(stb, want, vanilla, profile):
    """Print every rank as vanilla -> target, grouped by family."""
    for base in sorted(PROFILES[profile]):
        rows = rows_for(stb, base)
        label, _family, first = FAMILIES[base]
        print(f"\n{label}  (rows {rows[0]}-{rows[-1]}, ranks {first}-{first + 9})")
        print(f"  {'rank':>5}{'power':>16}{'cooldown s':>20}{'MP':>14}{'hits':>10}")
        for n, r in enumerate(rows):
            was = vanilla.get(r) or tuple(gi(stb, r, c) for c in WRITTEN)
            new = want[r]
            cells = []
            for i, (w, v) in enumerate(zip(was, new)):
                if i == 1:      # reload column, shown in seconds
                    w, v = w * 0.2, v * 0.2
                    cells.append(f"{w:.1f} -> {v:.1f}" if w != v else f"{w:.1f} =")
                else:
                    cells.append(f"{w} -> {v}" if w != v else f"{w} =")
            print(f"  {first + n:>5}{cells[0]:>16}{cells[1]:>20}{cells[2]:>14}{cells[3]:>10}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--profile", choices=sorted(PROFILES),
                    help="which tuning to apply; switching is safe and direct")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_stb_module()
    stb = oro.Stb(SKILL_STB)
    active, vanilla = read_sidecar(stb)

    if args.restore:
        if not vanilla:
            sys.exit("no sidecar -- nothing to restore")
        apply_values(stb, vanilla)
        save(stb)
        os.remove(SIDECAR)
        print(f"restored {len(vanilla)} rows to vanilla (was profile "
              f"{active!r}); sidecar removed")
        return

    if args.verify:
        if not active:
            sys.exit("no sidecar -- no profile is applied")
        profile = args.profile or active
        if args.profile and args.profile != active:
            sys.exit(f"profile {active!r} is applied, cannot verify {args.profile!r}")
        want = target_state(stb, profile)
        bad = [(r, c, gi(stb, r, c), v)
               for r, vals in sorted(want.items())
               for c, v in zip(WRITTEN, vals) if gi(stb, r, c) != v]
        print(f"profile {profile!r}: {len(want)} rows, {len(bad)} columns do not match")
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
        print("use --restore to go back to vanilla, or --profile <other> to switch.")
        return

    # The sidecar always holds vanilla, so switching is just: revert, then apply.
    if active:
        print(f"switching profile {active!r} -> {args.profile!r}\n")
        apply_values(stb, vanilla)
    else:
        vanilla = {r: tuple(gi(stb, r, c) for c in WRITTEN)
                   for base in PROFILES[args.profile] for r in rows_for(stb, base)}

    want = target_state(stb, args.profile)
    show(stb, want, vanilla, args.profile)
    apply_values(stb, want)

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    save(stb)
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
