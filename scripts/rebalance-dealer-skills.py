"""Make the Dealer's attack skills worth casting instead of auto-attacking.

The report
----------
A level 50-55 Dealer with Twin Shot 4, Power Gun Shot 3 and Gun Smash 2 finds
that auto-attacking out-values every one of them. Measured against the real
formulas (`scripts/balance-sim.py` ports them), that is true, and there are
three separate reasons.

**SKILL_POWER is not one of them.** The Dealer's power curves sit right on the
band every other physical base class occupies:

    Heavy Attack   (Soldier)  30->100   cd 6.0->6.6s   MP 10->30
    Aim Shot       (Hawker)   30->100   cd 6.0->6.8s   MP 10->30
    Power Attack   (Hawker)   35->110   cd 5.6->6.0s   MP 10->30
    Power Gun Shot (Dealer)   35->100   cd 7.0->7.8s   MP 10->30   <-- cd outlier
    Double Attack  (Sol+Hwk)  40-> 90   cd 5.4->3.6s   MP 20->38
    Twin Shot      (Dealer)   40-> 90   cd 6.0->5.2s   MP 20->38

1. **Power Gun Shot's cooldown is a genuine outlier.** It is the slowest tier-1
   single-target attack skill in the game, 17-30% longer than the Soldier and
   Hawker skills that carry the same power and the same MP cost. Nothing
   compensates for it.

2. **The Dealer has no fast two-hit skill.** Twin Shot is byte-identical to the
   Hawker's Double Shot -- but Soldier *and* Hawker also get Double Attack
   (cd 5.4->3.6s on the same 40->90 power). The Dealer only got the slow
   variant, so at rank 10 it fires every 5.2s where everyone else fires every
   3.6s for identical damage.

3. **The Dealer's own kit devalues its skills, and no other class has this
   problem.** Combat Mastery (AT_PSV_ATK_SPD_GUN, +15% at rank 20) and Union
   Weapon (AT_ATK_SPD, +53% at rank 10) pump auto-attack and do exactly nothing
   for skills on a fixed cooldown. Measured at level 52 vs a median field
   monster, the DPS uplift of casting on cooldown over pure auto-attacking:

                        no buffs    Union Weapon 10
       Power Gun Shot     +21.8%          +10.3%
       Twin Shot          +40.7%          +23.3%
       Smash Gun          +23.7%          +14.0%

   Investing in the class identity halves what its attack skills are worth.

Not fixed here: MP is the real binding constraint at that level. Standing regen
is `(GetAdd_RecoverMP() + (CON+40)/6)/6` per 2s (cobjavt.cpp) -- about 90 MP/min
for a level-52 Dealer against a 280 MP pool, while casting all three on cooldown
costs ~530 MP/min. You go dry in ~38 seconds. That is why *shortening cooldowns
alone would make the felt problem worse*, and why this pass raises SKILL_POWER
(which improves damage per MP) rather than only cutting cooldowns. Fixing MP
sustainability is a separate, cross-class decision.

What this changes
-----------------
    Power Gun Shot  power 35->100  =>  45->135   cooldown 7.0->7.8s => 5.6->6.0s
    Twin Shot       power unchanged            cooldown 6.0->5.2s => 5.4->3.6s
    Smash Gun       power 60->140  =>  75->175  cooldown 9.0s flat => 8.4s flat

Twin Shot's new cooldown curve is copied verbatim from Double Attack, so that
change is pure parity. Power Gun Shot's is copied from Power Attack. MP costs
are deliberately untouched, so every point of added power is a straight gain in
damage per MP.

Measured result (level 52, rank 4, median lv52 field monster; DPS uplift over
pure auto-attack, with the same peer skills computed against the same attacker):

                        before   after     peer reference
       Power Gun Shot   +24.6%   +42.3%    Power Attack  +34.5%
       Twin Shot        +41.7%   +50.3%    Double Attack +50.1%
       Smash Gun        +29.1%   +39.0%    Leap Attack   +27.3%

Power Gun Shot and Smash Gun land slightly above their nearest peer on purpose
-- that is the reason-3 correction. Twin Shot lands exactly on Double Attack,
which is all it was ever supposed to be. The ordering holds from level 52 to
240; the Dealer's own higher-tier skills stay clearly ahead (Sniping Shot 330,
Aim Point 240), so nothing is made redundant.

The percentages explode at level 240 for every skill in the table, Dealer and
peer alike, because auto-attack collapses against the normal-attack level gate
there. That is the endgame problem recorded in doc/balance-analysis.md, not
something this pass introduces.

Idempotent: records the original values in a sidecar so a second run is a no-op
and --restore can put them back. --dry-run and --verify are available.

Usage:
    python scripts/rebalance-dealer-skills.py --dry-run
    python scripts/rebalance-dealer-skills.py
    python scripts/rebalance-dealer-skills.py --verify
    python scripts/rebalance-dealer-skills.py --restore
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
COL_NAME = 0          # internal name, English, not the displayed one
COL_LEVEL = 2         # SKILL_LEVEL, the rank within the family
COL_POWER = 9         # SKILL_POWER
COL_MP = 17           # SKILL_USE_VALUE(s, 0), reported only
COL_RELOAD = 20       # SKILL_RELOAD_TIME, x 0.2s (io_skill.cpp: x200 - 100 ms)

# base row -> (expected name, new power per rank, new reload per rank).
# `None` leaves that column untouched. Ranks are rows base .. base+9.
PLAN = {
    2201: ("Power Gun Shot",
           [45, 55, 65, 75, 85, 95, 105, 115, 125, 135],
           [28, 28, 28, 28, 29, 29, 29, 29, 30, 30]),      # = Power Attack (1501)
    2221: ("Twin Shot",
           None,
           [27, 26, 25, 24, 23, 22, 21, 20, 19, 18]),      # = Double Attack (321/1541)
    2281: ("Smash Gun",
           [75, 86, 97, 108, 119, 130, 141, 152, 163, 175],
           [42] * 10),
}
RANKS = 10


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


def row_name(stb, r):
    return stb.get(r, COL_NAME).decode("latin-1", "replace").strip()


def rows_for(stb, base, expect):
    """The ten rank rows of one family, checked against what we expect to find.

    Guards against a data change having shifted the table under us: a silent
    write to the wrong ten rows would be very hard to notice afterwards.
    """
    out = []
    for n in range(RANKS):
        r = base + n
        got = row_name(stb, r)
        if got != expect:
            sys.exit(f"row {r}: expected {expect!r}, found {got!r} -- "
                     f"LIST_SKILL.STB has moved; refusing to write")
        if gi(stb, r, COL_LEVEL) != n + 1:
            sys.exit(f"row {r}: expected rank {n + 1}, found "
                     f"{gi(stb, r, COL_LEVEL)} -- refusing to write")
        out.append(r)
    return out


def expected_state(stb):
    """Target value per (row, column), derived from PLAN.

    Both apply and verify read this, so the two cannot drift apart.
    """
    want = {}
    for base, (expect, powers, reloads) in PLAN.items():
        for n, r in enumerate(rows_for(stb, base, expect)):
            want[r] = (powers[n] if powers else None,
                       reloads[n] if reloads else None)
    return want


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_stb_module()
    stb = oro.Stb(SKILL_STB)

    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = {int(k): v for k, v in json.load(fh).items()}

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for row, (power, reload_) in saved.items():
            stb.set(row, COL_POWER, str(power))
            stb.set(row, COL_RELOAD, str(reload_))
        with open(SKILL_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print(f"restored power/reload on {len(saved)} rows; sidecar removed")
        return

    want = expected_state(stb)

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the rebalance has not been applied")
        bad = []
        for r, (p, rl) in sorted(want.items()):
            if p is not None and gi(stb, r, COL_POWER) != p:
                bad.append((r, "power", gi(stb, r, COL_POWER), p))
            if rl is not None and gi(stb, r, COL_RELOAD) != rl:
                bad.append((r, "reload", gi(stb, r, COL_RELOAD), rl))
        print(f"{len(saved)} rows recorded; {len(bad)} columns do not match")
        for r, what, got, exp in bad:
            print(f"    row {r} {what}: {got} != {exp}")
        sys.exit(1 if bad else 0)

    if saved:
        print(f"already applied to {len(saved)} rows (sidecar present) -- nothing to do.")
        print("re-run with --restore first if you want to change the values.")
        return

    record = {}
    for base, (expect, powers, reloads) in PLAN.items():
        rows = rows_for(stb, base, expect)
        print(f"\n{expect}  (rows {rows[0]}-{rows[-1]})")
        print(f"  {'rank':>5}{'power':>16}{'cooldown s':>20}{'MP':>6}")
        for n, r in enumerate(rows):
            p0, r0 = gi(stb, r, COL_POWER), gi(stb, r, COL_RELOAD)
            record[r] = (p0, r0)
            p1 = powers[n] if powers else p0
            r1 = reloads[n] if reloads else r0
            pcol = f"{p0} -> {p1}" if p1 != p0 else f"{p0} (kept)"
            rcol = (f"{r0 * 0.2:.1f} -> {r1 * 0.2:.1f}" if r1 != r0
                    else f"{r0 * 0.2:.1f} (kept)")
            print(f"  {n + 1:>5}{pcol:>16}{rcol:>20}{gi(stb, r, COL_MP):>6}")
            stb.set(r, COL_POWER, str(p1))
            stb.set(r, COL_RELOAD, str(r1))

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    with open(SKILL_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({str(k): v for k, v in record.items()}, fh, indent=1)

    chk = oro.Stb(SKILL_STB)
    for r, (p, rl) in want.items():
        if p is not None and gi(chk, r, COL_POWER) != p:
            sys.exit(f"verify failed: row {r} power {gi(chk, r, COL_POWER)} != {p}")
        if rl is not None and gi(chk, r, COL_RELOAD) != rl:
            sys.exit(f"verify failed: row {r} reload {gi(chk, r, COL_RELOAD)} != {rl}")
    print(f"\ndone -- {len(record)} rows rewritten and verified. "
          f"Sidecar: {os.path.basename(SIDECAR)}")
    print("Restart the game server (it caches STBs at startup) and the client "
          "(it reads LIST_SKILL.STB for cooldowns and tooltips). Rebake the VFS "
          "before deploying.")


if __name__ == "__main__":
    main()
