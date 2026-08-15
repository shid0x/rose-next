"""Fix the two Eldeon monsters that sit outside the rules the other passes enforce.

Eldeon is in good shape. Sweeping every monster that spawns in EJT01/EJ01/EJ02/
EJ03/EZ01 and measuring it against the game's own fitted curves puts the median
at DEF 1.0x / ATK 1.0x, and the whole planet inside 0.8-1.4x -- ordinary band
noise, nothing like Oro's 4.9x. The level-200+ Sikuku tier was already corrected
by rebalance-endgame-curve.py. Two monsters escaped, each through a scope gap in
an existing pass rather than through a rule of their own:

  * **Executor Kera (row 1719) is level 250** -- the only named monster in the
    entire game above our character level cap of 240, and rebalance-oro-bosses.py
    only caps levels inside Oro. Level is a hard gate, not a slope:
    Get_SuccessRATE discards an attack outright when
    `(player_lv + 10) - monster_lv * 1.1 + rand(1..50)` is non-positive, so
    against a level-250 monster a level-216 player (the level EZ01 is built for,
    its other monsters being 200-208) clears the gate 2% of the time and then
    fails the `iSuc < 20` roll -- a simulated 0% hit rate. Not "hard": unkillable.
    Even at our cap it takes ~920 swings.

    Capping at 240 rather than something zone-appropriate is deliberate, and the
    reason is drops: CCal::Get_DropITEM returns false once
    `player_lv - monster_lv >= 10`, so pulling Kera down to its zone's level
    would leave a capped player with nothing to show for the fight. 240 keeps it
    reachable from level ~230 up and keeps drops alive at cap.

  * **Sikuku Elite Slaughterer (row 1571) has DEF 1117 at level 195** -- 1.7x the
    fitted trend, and rebalance-endgame-curve.py starts at level 200. Damage is
    proportional to `(ATK - DEF + 250)`, so against a level-appropriate character
    it lands 40 per hit where its neighbours take 212, and with 20,280 HP that is
    ~720 swings versus ~49 for an ordinary level-195 monster.

    It is *not* corrected to the plain 1.0x non-boss cap. EJ02's named variants
    (Lion Sikuku Assassin Captain/Leader, Tiger Sikuku Captain, Sikuku Killer)
    consistently sit at 1.3-1.4x DEF with 1.1x ATK -- a deliberate elite tier, not
    a defect -- and flattening the Slaughterer to 1.0x would leave the zone's
    toughest elite *softer* than the level-178 captains. ELITE_MULTIPLIER puts it
    at the top of its own tier instead: ~6.6x an ordinary monster of its level
    rather than ~15x.

Everything else Eldeon is left alone on purpose:
  * Moss Golem (lv210, 56,700 HP, 7.0x the HP trend) and Turak (lv180, 32,580,
    4.4x) are big-HP field monsters with on-curve DEF/RES -- you hit them for full
    damage, they just take a while. Same piñata design as Luna's Gems.
  * Rows 996/997/998 (Moss Golem, Nepenthes, Turak duplicates) carry absurd HP
    columns -- 13,000,000, i.e. 1.95 *billion* max HP. They spawn in no REGEN lump
    in any map, so they are unused rows and are left untouched rather than
    "repaired" into something that might then get used.

Idempotent, verifiable and reversible through a sidecar next to the STB.

Usage:
    python scripts/rebalance-eldeon-outliers.py --dry-run
    python scripts/rebalance-eldeon-outliers.py
    python scripts/rebalance-eldeon-outliers.py --verify
    python scripts/rebalance-eldeon-outliers.py --restore
"""
import argparse
import importlib.util
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
NPC_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.eldeon-outliers.json")

COL_NAME, COL_LEVEL, COL_HP, COL_DEF = 0, 7, 8, 11
LEVEL_CAP = 240             # our character level cap
ELITE_MULTIPLIER = 1.4      # what EJ02's other named variants already sit at
FIT_BELOW = 200

# Row ids are pinned *and* name-checked. These are two hand-picked monsters, not a
# rule -- a rule would have caught them in the passes they escaped -- so the script
# refuses to run rather than rewrite whatever moved into the row.
KERA = (1719, "Executor Kera")
SLAUGHTERER = (1571, "Sikuku Elite Slaughterer")


def load_oro():
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


def name(stb, r):
    return stb.get(r, COL_NAME).decode("latin-1", "replace").strip()


def fit_def_trend(stb):
    """Least-squares DEF vs level over ten-level bands below FIT_BELOW.

    Same fit as rebalance-endgame-curve.py so the two agree on what "the trend"
    means: bands rather than raw rows, and nothing at or above FIT_BELOW, where
    the curve is known to break.
    """
    pts = []
    for lo in range(60, FIT_BELOW, 10):
        vals = [gi(stb, i, COL_DEF) for i in range(1, stb.rows)
                if stb.get(i, COL_NAME).strip()
                and lo <= gi(stb, i, COL_LEVEL) < lo + 10 and gi(stb, i, COL_DEF) > 0]
        if len(vals) >= 4:
            pts.append((lo + 5, statistics.median(vals)))
    if len(pts) < 6:
        sys.exit("not enough data below level %d to fit a trend" % FIT_BELOW)
    n = len(pts)
    sx = sum(x for x, _ in pts)
    sy = sum(y for _, y in pts)
    sxx = sum(x * x for x, _ in pts)
    sxy = sum(x * y for x, y in pts)
    slope = (n * sxy - sx * sy) / (n * sxx - sx * sx)
    return slope, (sy - sx * slope) / n


def check_rows(stb):
    for row, want in (KERA, SLAUGHTERER):
        if row >= stb.rows or name(stb, row) != want:
            sys.exit(f"row {row} is {name(stb, row)!r}, expected {want!r} -- "
                     f"LIST_NPC.STB has shifted; re-pin the rows before running")


def plan(stb, slope, intercept):
    """[(row, col, old, new, why)] -- only ever lowering."""
    out = []
    row, _ = KERA
    lv = gi(stb, row, COL_LEVEL)
    if lv > LEVEL_CAP:
        out.append((row, COL_LEVEL, lv, LEVEL_CAP, f"level cap {LEVEL_CAP}"))
    row, _ = SLAUGHTERER
    lv, cur = gi(stb, row, COL_LEVEL), gi(stb, row, COL_DEF)
    cap = round(max(1, slope * lv + intercept) * ELITE_MULTIPLIER)
    if cur > cap:
        out.append((row, COL_DEF, cur, cap, f"{ELITE_MULTIPLIER}x the level-{lv} DEF trend"))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_oro()
    stb = oro.Stb(NPC_STB)
    check_rows(stb)

    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = {tuple(int(x) for x in k.split(":")): v
                     for k, v in json.load(fh).items()}

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for (row, col), old in saved.items():
            stb.set(row, col, str(old))
        with open(NPC_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print(f"restored {len(saved)} values; sidecar removed")
        return

    slope, intercept = fit_def_trend(stb)

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the rebalance has not been applied")
        # Recompute the intended value from the *original* recorded in the sidecar,
        # so verify re-derives rather than trusting whatever is in the table.
        restored = oro.Stb(NPC_STB)
        for (row, col), old in saved.items():
            restored.set(row, col, str(old))
        want = {(r, c): new for r, c, _old, new, _why
                in plan(restored, *fit_def_trend(restored))}
        bad = [k for k in saved if gi(stb, *k) != want.get(k)]
        print(f"{len(saved)} values recorded; {len(bad)} do not match"
              + (f": {bad}" if bad else ""))
        sys.exit(1 if bad else 0)

    if saved:
        print(f"already applied to {len(saved)} values -- nothing to do.")
        print("re-run with --restore first if you want to change the parameters.")
        return

    print(f"DEF trend fitted below level {FIT_BELOW}: "
          f"{slope:.2f} x level {intercept:+.0f}\n")
    todo = plan(stb, slope, intercept)
    if not todo:
        print("both monsters already within their budgets -- nothing to do.")
        return

    print(f"  {'monster':<30}{'stat':>7}{'change':>18}   why")
    record = {}
    for row, col, old, new, why in todo:
        stat = {COL_LEVEL: "level", COL_DEF: "DEF"}[col]
        print(f"  {name(stb, row)[:29]:<30}{stat:>7}{f'{old} -> {new}':>18}   {why}")
        record[f"{row}:{col}"] = old
        stb.set(row, col, str(new))

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    with open(NPC_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(record, fh, indent=1)

    chk = oro.Stb(NPC_STB)
    for row, col, _old, new, _why in todo:
        assert gi(chk, row, col) == new, (row, col, gi(chk, row, col), new)
    print(f"\ndone -- {len(record)} values rewritten and verified.")
    print("Restart the game server (STBs are cached at startup).")


if __name__ == "__main__":
    main()
