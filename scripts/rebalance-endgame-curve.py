"""Put level-200+ monster DEF/RES back on the curve the rest of the game follows.

Normal attacks stop working at endgame, in Oro *and* in our own content. The
damage formula is

    damage  proportional to  (ATK - DEF + 250)

so once a monster's DEF approaches the player's attack power the numerator
collapses and every hit lands on the damage floor. A level 216 Raider in full
level-220 gear has ATK 1201 (server `/stats`), which puts the practical wall at
about DEF 1350. Measured against that character:

    Sikuku Predator  (ours, lv207, DEF 1500)    5 damage per normal hit
    Drytail Scorpio  (Oro,  lv208, DEF 1279)   26 damage  (observed in game: 31)

The interesting part is that this is not an Oro import bug. Median monster DEF
across our own data is almost perfectly linear from level 60 to 199 -- fourteen
consecutive ten-level bands, ~610 monsters -- and then breaks:

    fitted trend below 200 :  DEF = 3.71 x level - 60
    level 195, ours        :  689   (trend 664)   on the curve
    level 205, ours        : 1156   (trend 701)   1.6x over
    level 220, Oro         : 1562   (trend 757)   2.1x over
    level 235, Oro         : 3985   (trend 813)   4.9x over

So the Sikuku tier already left the curve before Oro existed; Oro then left it
much further. This restores the trend rather than inventing a number: every
level-200+ monster is capped at what the game's own first 200 levels predict.

Rules:
  * The stat is only ever LOWERED. Anything already at or below the trend is left
    alone, which is why the level-200 quest NPCs sitting at DEF 300 (Tiger, the
    Ghost, the Town Girl and friends) are untouched without needing a list.
  * Bosses get BOSS_MULTIPLIER x the trend so they stay clearly the tankiest
    thing around. "Boss" is read from the data, not a name list: the NPC_HP
    column has a clean gap -- ordinary monsters sit at 1-290, the seven Oro
    named monsters at 1133-1272.
  * The trend is refitted at runtime from monsters BELOW level 200, so it
    re-derives if our data changes and cannot feed on its own output.

This does not touch boss HP (level x NPC_HP, so ~285k for the Terrasaurus King)
or boss level (up to 245, above our cap of 240). Both still want attention.

Idempotent, verifiable and reversible through a sidecar next to the STB.

RES has the same break for the same reason, and it is a separate --stat because
the two reach different players: DEF is the physical denominator (and appears in
both numerators), while RES is the magic denominator only. Correcting DEF alone
helps casters' numerator and leaves their divisor untouched.

Usage:
    python scripts/rebalance-endgame-curve.py --stat def --dry-run
    python scripts/rebalance-endgame-curve.py --stat def
    python scripts/rebalance-endgame-curve.py --stat res
    python scripts/rebalance-endgame-curve.py --stat res --verify
    python scripts/rebalance-endgame-curve.py --stat res --restore
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

COL_LEVEL, COL_HP = 7, 8
FROM_LEVEL = 200            # where the curve breaks
BOSS_HP_COLUMN = 1000       # clean gap: ordinary monsters top out at 290
BOSS_MULTIPLIER = 1.6

# Which stat to correct. Both break at the same level and for the same reason,
# but they are separate knobs because they reach different players: DEF is the
# physical denominator (and both numerators), RES is the magic denominator only,
# so a DEF-only pass quietly leaves casters where they were.
STATS = {
    "def": (11, os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.endgame-def.json")),
    "res": (12, os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.endgame-res.json")),
}


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


def fit_trend(stb, col):
    """Least-squares stat-vs-level over ten-level bands BELOW FROM_LEVEL.

    Bands rather than raw rows so a level with hundreds of monsters does not
    outvote one with a dozen, and below FROM_LEVEL only so the broken tail can
    never influence the line that is meant to correct it.
    """
    pts = []
    for lo in range(60, FROM_LEVEL, 10):
        vals = [gi(stb, i, col) for i in range(1, stb.rows)
                if stb.get(i, 0).strip() and lo <= gi(stb, i, COL_LEVEL) < lo + 10
                and gi(stb, i, col) > 0]
        if len(vals) >= 4:
            pts.append((lo + 5, statistics.median(vals), len(vals)))
    if len(pts) < 6:
        sys.exit("not enough data below level %d to fit a trend" % FROM_LEVEL)
    n = len(pts)
    sx = sum(x for x, _, _ in pts)
    sy = sum(y for _, y, _ in pts)
    sxx = sum(x * x for x, _, _ in pts)
    sxy = sum(x * y for x, y, _ in pts)
    slope = (n * sxy - sx * sy) / (n * sxx - sx * sx)
    intercept = (sy - sx * slope) / n
    return slope, intercept, pts


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stat", default="def", choices=sorted(STATS),
                    help="which column to correct (default: def)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    col, sidecar = STATS[args.stat]
    oro = load_oro()
    stb = oro.Stb(NPC_STB)
    saved = {}
    if os.path.exists(sidecar):
        with open(sidecar, encoding="utf-8") as fh:
            saved = {int(k): v for k, v in json.load(fh).items()}

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for row, old in saved.items():
            stb.set(row, col, str(old))
        with open(NPC_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(sidecar)
        print(f"restored {args.stat.upper()} on {len(saved)} rows; sidecar removed")
        return

    slope, intercept, pts = fit_trend(stb, col)
    trend = lambda lv: max(1, round(slope * lv + intercept))

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the rebalance has not been applied")
        bad = []
        for row in saved:
            lv = gi(stb, row, COL_LEVEL)
            mult = BOSS_MULTIPLIER if gi(stb, row, COL_HP) >= BOSS_HP_COLUMN else 1.0
            if gi(stb, row, col) != min(saved[row], round(trend(lv) * mult)):
                bad.append(row)
        print(f"{len(saved)} rows recorded; {len(bad)} do not match"
              + (f": {bad}" if bad else ""))
        sys.exit(1 if bad else 0)

    if saved:
        print(f"already applied to {len(saved)} rows -- nothing to do.")
        print("re-run with --restore first if you want to change the parameters.")
        return

    print(f"fitted on {len(pts)} bands below level {FROM_LEVEL}: "
          f"{args.stat.upper()} = {slope:.2f} x level {intercept:+.0f}\n")
    rows = [i for i in range(1, stb.rows)
            if stb.get(i, 0).strip() and gi(stb, i, COL_LEVEL) >= FROM_LEVEL
            and gi(stb, i, col) > 0]
    changed, record, untouched = [], {}, []
    for i in rows:
        lv, cur = gi(stb, i, COL_LEVEL), gi(stb, i, col)
        boss = gi(stb, i, COL_HP) >= BOSS_HP_COLUMN
        cap = round(trend(lv) * (BOSS_MULTIPLIER if boss else 1.0))
        if cur <= cap:
            untouched.append((i, cur, cap))
            continue
        record[i] = cur
        stb.set(i, col, str(cap))
        changed.append((i, lv, cur, cap, boss))

    print(f"{len(changed)} of {len(rows)} level-{FROM_LEVEL}+ monsters are above the trend\n")
    print(f"  {'monster':<32}{'lv':>5}{args.stat.upper():>16}{'':>4}")
    for i, lv, cur, cap, boss in sorted(changed, key=lambda x: -x[2]):
        print(f"  {stb.get(i, 0).decode('latin-1', 'replace')[:31]:<32}{lv:>5}"
              f"{f'{cur} -> {cap}':>16}{'  BOSS' if boss else '':>6}")
    print(f"\n  {len(untouched)} already at or below the trend, left alone"
          f" (e.g. {', '.join(stb.get(i,0).decode('latin-1','replace')[:18] for i,_,_ in untouched[:3])})")

    if args.dry_run:
        print("\ndry run -- nothing written")
        return
    with open(NPC_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(sidecar, "w", encoding="utf-8") as fh:
        json.dump({str(k): v for k, v in record.items()}, fh, indent=1)

    chk = oro.Stb(NPC_STB)
    for i, lv, cur, cap, boss in changed:
        assert gi(chk, i, col) == cap, (i, gi(chk, i, col), cap)
    print(f"\ndone -- {len(record)} rows rewritten and verified.")
    print("Restart the game server (STBs are cached at startup).")


if __name__ == "__main__":
    main()
