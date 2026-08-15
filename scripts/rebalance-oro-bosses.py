"""Bring the Oro named monsters' HP and level into range.

The DEF/RES pass (rebalance-endgame-curve.py) made these seven killable rather
than immune, but two things still put them out of reach:

  * **HP.** Max HP is `level x NPC_HP`, so a column of 1165 at level 245 is
    285,425. Fitted against the game's own curve -- median max HP per ten-level
    band below level 200 -- that is 32x what the trend predicts, where our own
    toughest non-boss (Executor Kera, 72,500) is 8x. At ~580 per skill hit it
    was roughly 500 landed hits, before accounting for misses.

  * **Level.** Two sit above our level cap of 240, and level drives two separate
    gates. Get_SuccessRATE discards an attack outright when
    `(player_lv + 10) - monster_lv * 1.1 + rand(1..50)` is non-positive, so a
    capped player opens the gate 62% of the time against a level-245 monster
    versus 72% against a 240. And CCal::Get_DropITEM returns false once
    `player_lv - monster_lv >= 10`, so level also decides whether a kill drops
    anything at all.

Both are capped, never raised, using the same trend-from-our-own-data approach as
the DEF/RES pass:

    max HP  <=  BOSS_HP_MULTIPLIER x (22 x level + 3422)
    level   <=  LEVEL_CAP

Bosses are identified from the data, not a name list, but the NPC_HP column alone
is not enough to find them. Ordinary monsters top out at 290 and these seven sit
at 1133-1272 -- so do [Mechanic's Dog] Chopper at 5000 and a set of quest/event
NPCs (Tiger, [Ghost] Harry, [Ranger] Paul ...) at 9999, i.e. ~2,000,000 max HP,
and capping those to a boss budget would quietly rewrite quest encounters. Scope
therefore comes from the map REGEN lumps: monsters spawning in Oro zones and
nowhere else, then filtered by HP column.

Note the drop rule cuts the other way and is NOT addressed here: any monster more
than 9 levels below the killer drops nothing, so at level 240 a player already
gets no drops from anything below level 231 -- which is most of the game,
including the whole Sikuku tier. That is retail anti-farming behaviour rather
than a defect, but it is worth knowing before authoring drops.

Idempotent, verifiable and reversible through a sidecar next to the STB.

Usage:
    python scripts/rebalance-oro-bosses.py --dry-run
    python scripts/rebalance-oro-bosses.py
    python scripts/rebalance-oro-bosses.py --verify
    python scripts/rebalance-oro-bosses.py --restore
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
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.oro-bosses.json")

COL_LEVEL, COL_HP = 7, 8
BOSS_HP_COLUMN = 1000       # clean gap: ordinary monsters top out at 290
BOSS_HP_MULTIPLIER = 10     # our toughest non-boss sits at 8x the trend
LEVEL_CAP = 240             # our character level cap
FIT_BELOW = 200


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


def fit_hp_trend(stb, bosses):
    """Least-squares max-HP vs level over ten-level bands below FIT_BELOW.

    Bosses are excluded from the fit as well as being its target -- leaving them
    in would drag the line towards the very values it is meant to correct.
    """
    pts = []
    for lo in range(60, FIT_BELOW, 10):
        vals = [gi(stb, i, COL_LEVEL) * gi(stb, i, COL_HP) for i in range(1, stb.rows)
                if stb.get(i, 0).strip() and lo <= gi(stb, i, COL_LEVEL) < lo + 10
                and gi(stb, i, COL_HP) > 0 and i not in bosses]
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
    return slope, (sy - sx * slope) / n, len(pts)


ORO_MAP_DIRS = {"TOWN", "OROIP", "ODP01", "ODC01", "ODD01", "ODD02", "ODD03",
                "ODD04", "ODD05", "ODOS01", "ODRP01", "ODE01"}


def regen_mobs(extra):
    """Mob ids named by one REGEN point: a name, then two (name, mob, count) lists."""
    import struct
    o = 0

    def bstr():
        nonlocal o
        n = extra[o]
        o += 1
        s = extra[o:o + n]
        o += n
        return s

    bstr()
    ids = []
    for _ in range(2):
        cnt, = struct.unpack_from("<i", extra, o)
        o += 4
        for _ in range(cnt):
            bstr()
            mob, _num = struct.unpack_from("<ii", extra, o)
            o += 8
            ids.append(mob)
    return ids


def find_bosses(oro, stb):
    """Oro-only monsters with a boss-sized HP column.

    The HP column alone is not enough. Ordinary monsters top out at 290 and the
    Oro named monsters sit at 1133-1272, but so do several things that must not
    be touched: [Mechanic's Dog] Chopper at 5000, and a set of quest/event NPCs
    (Tiger, [Ghost] Harry, [Ranger] Paul ...) at 9999, i.e. ~2,000,000 max HP.
    Capping those to a boss budget would quietly rewrite quest encounters.

    Scope therefore comes from the map REGEN lumps -- monsters that spawn in Oro
    zones and nowhere else -- exactly as rebalance-oro-accuracy.py does.
    """
    import collections
    import glob
    where = collections.defaultdict(set)
    for path in glob.glob(os.path.join(ROOT, "data", "3DDATA", "MAPS", "**", "*.IFO"),
                          recursive=True):
        zone = os.path.basename(os.path.dirname(path)).upper()
        try:
            buf, bounds = oro.read_ifo(path)
            objs, _ = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        except Exception:
            continue
        for o in objs or []:
            try:
                for mob in regen_mobs(o["extra"]):
                    if mob > 0:
                        where[mob].add(zone)
            except Exception:
                pass
    return {i for i, zones in where.items()
            if zones and zones <= ORO_MAP_DIRS
            and i < stb.rows and stb.get(i, 0).strip()
            and gi(stb, i, COL_HP) >= BOSS_HP_COLUMN
            and gi(stb, i, COL_LEVEL) >= 200}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_oro()
    stb = oro.Stb(NPC_STB)
    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = {int(k): v for k, v in json.load(fh).items()}

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for row, (lv, hp) in saved.items():
            stb.set(row, COL_LEVEL, str(lv))
            stb.set(row, COL_HP, str(hp))
        with open(NPC_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print(f"restored level/HP on {len(saved)} bosses; sidecar removed")
        return

    bosses = find_bosses(oro, stb)
    # A boss whose level this run already lowered would otherwise be measured at
    # its new level; the sidecar holds the original, so prefer that.
    slope, intercept, nbands = fit_hp_trend(stb, bosses)
    trend = lambda lv: max(1, slope * lv + intercept)

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the rebalance has not been applied")
        bad = []
        for row, (lv0, hp0) in saved.items():
            want_lv = min(lv0, LEVEL_CAP)
            want_hp = min(hp0, max(1, round(trend(want_lv) * BOSS_HP_MULTIPLIER / want_lv)))
            if gi(stb, row, COL_LEVEL) != want_lv or gi(stb, row, COL_HP) != want_hp:
                bad.append(row)
        print(f"{len(saved)} bosses recorded; {len(bad)} do not match"
              + (f": {bad}" if bad else ""))
        sys.exit(1 if bad else 0)

    if saved:
        print(f"already applied to {len(saved)} bosses -- nothing to do.")
        print("re-run with --restore first if you want to change the parameters.")
        return

    print(f"max-HP trend fitted on {nbands} bands below level {FIT_BELOW}: "
          f"{slope:.0f} x level {intercept:+.0f}")
    print(f"boss budget = {BOSS_HP_MULTIPLIER}x that; level capped at {LEVEL_CAP}\n")
    print(f"  {'boss':<32}{'level':>12}{'max HP':>22}{'x trend':>9}")
    record = {}
    for i in sorted(bosses, key=lambda x: -gi(stb, x, COL_LEVEL)):
        lv0, hp0 = gi(stb, i, COL_LEVEL), gi(stb, i, COL_HP)
        lv = min(lv0, LEVEL_CAP)
        hp = min(hp0, max(1, round(trend(lv) * BOSS_HP_MULTIPLIER / lv)))
        if lv == lv0 and hp == hp0:
            continue
        record[i] = (lv0, hp0)
        stb.set(i, COL_LEVEL, str(lv))
        stb.set(i, COL_HP, str(hp))
        print(f"  {stb.get(i, 0).decode('latin-1', 'replace')[:31]:<32}"
              f"{f'{lv0} -> {lv}':>12}{f'{lv0*hp0:,} -> {lv*hp:,}':>22}"
              f"{lv * hp / trend(lv):>9.1f}")

    if not record:
        print("  (every boss already within budget)")
        return
    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    with open(NPC_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({str(k): list(v) for k, v in record.items()}, fh, indent=1)

    chk = oro.Stb(NPC_STB)
    for i, (lv0, hp0) in record.items():
        lv = min(lv0, LEVEL_CAP)
        assert gi(chk, i, COL_LEVEL) == lv, (i, gi(chk, i, COL_LEVEL), lv)
        assert gi(chk, i, COL_HP) == min(hp0, max(1, round(trend(lv) * BOSS_HP_MULTIPLIER / lv)))
    print(f"\ndone -- {len(record)} bosses rewritten and verified.")
    print("Restart the game server (STBs are cached at startup).")


if __name__ == "__main__":
    main()
