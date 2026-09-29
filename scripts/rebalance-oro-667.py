#!/usr/bin/env python3
"""Put the 667-build Oro monsters on our curve: level, HP, ATK, HIT, AVOID.

scripts/import-oro-667.py brings the monsters in with the 667 build's stats
verbatim, deliberately. Measured against RoseZA's (which we had already
rebalanced once) they are another step up: levels run to 255 against our 240
cap, HP is ~3x on the bosses and ~2x on trash, DEF sits at 3,400-4,000 on trash
where our band is ~750 (every swing on the damage floor), and the boss "MDEF"
column reaches 58,000. None of it is playable as shipped.

WHAT THIS DOES (the same shape as rebalance-karkia.py, on purpose)
-------------------------------------------------------------------
1. **Levels**, remapped linearly inside each zone onto ZONE_BANDS, so 667's own
   internal progression survives. Oro is the *other* endgame path next to Karkia
   (215-238), so it spans 205-240 with Fossil Sanctuary at the top. Levels
   first: the DEF/RES pass caps against level.
2. **HP** and **ATK** onto our sub-200 trend for the new level, times a modest
   multiplier, times the monster's ratio to its zone median -- square-rooted and
   capped, so ranking survives without the absurdity (compress the spread, do
   not flatten it).
3. **HIT** and **AVOID** the same way. rebalance-oro-accuracy.py scaled RoseZA's
   values by a factor fitted to RoseZA; 667 is a different scale again, so the
   trend fit replaces the factor and that script is retired.
4. **Bosses** by hand (BOSSES): level, an HP multiple of the trend (10x is what
   rebalance-oro-bosses.py gave the old Oro bosses; lesser kings get less), ATK
   capped at 3,000 (our lv240 boss at 2,997 already kills a Champion in four
   swings). Written to LIST_NPC.oro667-bosses.json, which
   rebalance-endgame-curve.py reads to give them BOSS_MULTIPLIER DEF/RES -- and
   which it NEEDS, because this pass lowers their HP column under the 1000
   threshold it would otherwise recognise a boss by.
5. **The Shadow Ghost ladder** (SUMMONS, 2230-2239): AI-summoned only, never in
   a spawn lump, so the zone bands cannot reach them. Levels kept (it is a
   ladder), glass stats, and -- the one exception to the division of labour --
   DEF/RES set to the trend here, because six of the ten sit below level 200
   where rebalance-endgame-curve.py never looks. Their EXP is computed with
   rebalance-exp-rewards.py's own formula at half weight, since that pass only
   sees spawning monsters.

DEF and RES on everything else stay rebalance-endgame-curve.py's job, EXP stays
rebalance-exp-rewards.py's. ORDER, written out:

    python scripts/rebalance-oro-667.py
    python scripts/rebalance-endgame-curve.py --stat def
    python scripts/rebalance-endgame-curve.py --stat res
    python scripts/rebalance-exp-rewards.py
    then every --verify, including rebalance-karkia.py and
    rebalance-eldeon-outliers.py: both recompute their targets from trends
    fitted on the live table.

Idempotent, verifiable and reversible through a sidecar next to the STB.
`data/` is gitignored, so this docstring is the only committed record.

    python scripts/rebalance-oro-667.py --dry-run
    python scripts/rebalance-oro-667.py
    python scripts/rebalance-oro-667.py --verify
    python scripts/rebalance-oro-667.py --restore
"""
import argparse
import collections
import glob
import importlib.util
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

_te_spec = importlib.util.spec_from_file_location(
    "balance_trend_exclude", os.path.join(HERE, "balance-trend-exclude.py"))
trend_exclude = importlib.util.module_from_spec(_te_spec)
_te_spec.loader.exec_module(trend_exclude)   # rows no level trend may include
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
NPC_STB = os.path.join(STB_DIR, "LIST_NPC.STB")
SIDECAR = os.path.join(STB_DIR, "LIST_NPC.oro667.json")
BOSS_SIDECAR = os.path.join(STB_DIR, "LIST_NPC.oro667-bosses.json")
MAPS = os.path.join(ROOT, "data", "3DDATA", "MAPS", "ORO")

COL_LEVEL, COL_HP, COL_ATK, COL_HIT, COL_DEF, COL_RES, COL_AVOID, COL_EXP = 7, 8, 9, 10, 11, 12, 13, 17
WRITE_COLS = (COL_LEVEL, COL_HP, COL_ATK, COL_HIT, COL_AVOID)
SUMMON_COLS = WRITE_COLS + (COL_DEF, COL_RES, COL_EXP)
FIT_BELOW = 200
LEVEL_CAP = 240

# Only rows in this band are Oro's; the spawn lumps also name our own critters
# (lizards 6/7, chick/chicken) and event summons, which stay as they are.
ORO_ID_RANGE = range(2100, 2400)

# folder -> (low, high). ODD04/ODD05/ODRP01 share the Ikaness/Devourer/Scavenger
# roster and are banded as one pool through ZONE_ALIAS (rebalance-karkia.py's
# lesson: a shared roster split across bands re-plans every monster).
ZONE_BANDS = {"ODGR01": (205, 222), "WASTELAND": (222, 234),
              "TOWN": (220, 220), "ODFS01": (230, 240)}
ZONE_ALIAS = {"ODD04": "WASTELAND", "ODD05": "WASTELAND", "ODRP01": "WASTELAND"}

# row -> (level, HP multiple of the effective-HP trend, ATK)
BOSSES = {
    2207: (222,  6, 2500),    # Crowned Asper King          Golden Ring
    2254: (224,  6, 2500),    # Poisontail Scorpio King     Golden Ring
    2281: (226,  7, 2600),    # Golden Scarab King          Golden Ring
    2226: (230,  8, 2700),    # Grand Master Devourer       Wasteland
    2247: (231,  8, 2700),    # Desert Scavenger King       Wasteland
    2296: (234,  9, 2800),    # Ikaness Mecha               Wasteland / Ruins Path
    2221: (237,  9, 2800),    # Giant Fiery Snapper         Fossil Sanctuary
    2259: (239, 10, 2900),    # Prehistoric Mastyx King     Fossil Sanctuary
    2265: (240, 10, 2900),    # Fearsome Terrasaurus King   Fossil Sanctuary
    2276: (240, 10, 3000),    # Camouflaged Armastyx King   Fossil Sanctuary
}
SUMMONS = list(range(2230, 2240))       # Shadow Ghost ladder, levels kept
SUMMON_HP_MULT, SUMMON_ATK_MULT, SUMMON_EXP_WEIGHT = 0.5, 0.9, 0.5

TRASH_HP_MULT = 1.5
TRASH_ATK_MULT = 1.1
BOSS_HIT_MULT = 1.2
RATIO_CAP = 3.0
ACC_RATIO_CAP = 2.0


def gi(stb, row, col):
    v = stb.get(row, col).strip()
    try:
        return int(v)
    except ValueError:
        return 0


def load(name, fname):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fname))
    mod = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, [fname]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = argv
    return mod


def oro_rows(oro, stb):
    """{row: zone} for every Oro monster a 667 map spawns, plus the summons."""
    zone_of = {}
    for p in sorted(glob.glob(os.path.join(MAPS, "*", "*.[iI][fF][oO]"))):
        zone = os.path.basename(os.path.dirname(p)).upper()
        zone = ZONE_ALIAS.get(zone, zone)
        buf, bounds = oro.read_ifo(p)
        objs, _ = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        for o in objs or ():
            for i in oro.regen_mob_ids(o["extra"]):
                if i in ORO_ID_RANGE and stb.get(i, 0).strip():
                    zone_of.setdefault(i, zone)
    for i in SUMMONS:
        if stb.get(i, 0).strip():
            zone_of.setdefault(i, "SUMMON")
    return zone_of


def fit(stb, col, exclude, value=None):
    """Least squares on per-band medians below FIT_BELOW, Oro excluded."""
    band = collections.defaultdict(list)
    for r in range(1, stb.rows):
        if not stb.get(r, 0).strip() or r in exclude or trend_exclude.excluded(r):
            continue
        lv = gi(stb, r, COL_LEVEL)
        if not (60 <= lv < FIT_BELOW):
            continue
        v = value(stb, r) if value else gi(stb, r, col)
        if v > 0:
            band[lv // 10 * 10].append(v)
    xs = sorted(band)
    ys = [statistics.median(band[b]) for b in xs]
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    slope = (sum((x - mx) * (y - my) for x, y in zip(xs, ys))
             / sum((x - mx) ** 2 for x in xs))
    return lambda lv: max(1.0, slope * lv + (my - slope * mx))


def plan(stb, zone_of, exp_mod):
    """{row: {col: new value}}."""
    scope = set(zone_of)
    ehp = fit(stb, None, scope, value=lambda s, r: gi(s, r, COL_LEVEL) * gi(s, r, COL_HP))
    atk = fit(stb, COL_ATK, scope)
    hit = fit(stb, COL_HIT, scope)
    avd = fit(stb, COL_AVOID, scope)
    dfn = fit(stb, COL_DEF, scope)
    res = fit(stb, COL_RES, scope)

    by_zone = collections.defaultdict(list)
    for i, z in zone_of.items():
        if i not in BOSSES and i not in SUMMONS:
            by_zone[z].append(i)
    med = {}
    for z, ids in by_zone.items():
        med[z] = {
            "ehp": statistics.median(gi(stb, i, COL_LEVEL) * gi(stb, i, COL_HP) for i in ids) or 1,
            "atk": statistics.median(gi(stb, i, COL_ATK) for i in ids) or 1,
            "hit": statistics.median(gi(stb, i, COL_HIT) for i in ids) or 1,
            "avd": statistics.median(gi(stb, i, COL_AVOID) for i in ids) or 1,
        }

    def ratio(v, m, cap):
        return min(cap, max(0.5, (v / m) ** 0.5)) if m else 1.0

    out = {}
    for row, (lv, mult, a) in sorted(BOSSES.items()):
        if row not in zone_of:
            continue
        out[row] = {COL_LEVEL: lv, COL_HP: max(1, round(mult * ehp(lv) / lv)), COL_ATK: a,
                    COL_HIT: round(hit(lv) * BOSS_HIT_MULT), COL_AVOID: round(avd(lv))}
    for row in SUMMONS:
        if row not in zone_of:
            continue
        lv = gi(stb, row, COL_LEVEL)
        exp_col = (exp_mod.need_raw_exp(lv) / exp_mod.target_kills(lv)
                   / exp_mod.EXP_FORMULA_K / (lv + 3)) * SUMMON_EXP_WEIGHT
        out[row] = {COL_LEVEL: lv, COL_HP: max(1, round(SUMMON_HP_MULT * ehp(lv) / lv)),
                    COL_ATK: round(atk(lv) * SUMMON_ATK_MULT), COL_HIT: round(hit(lv)),
                    COL_AVOID: round(avd(lv)), COL_EXP: max(1, round(exp_col))}
        if lv < FIT_BELOW:
            # below 200 the DEF/RES pass never looks; at 200+ it owns the value
            # (its own fit differs by a few points, which --verify would report)
            out[row][COL_DEF] = round(dfn(lv))
            out[row][COL_RES] = round(res(lv))
    for z, ids in sorted(by_zone.items()):
        lo, hi = ZONE_BANDS.get(z, (None, None))
        if lo is None:
            raise SystemExit(f"zone {z} has no band (rows {sorted(ids)[:5]})")
        lvs = [gi(stb, i, COL_LEVEL) for i in ids]
        a, b = min(lvs), max(lvs)
        for i in sorted(ids):
            cur = gi(stb, i, COL_LEVEL)
            lv = lo if b == a else round(lo + (cur - a) * (hi - lo) / float(b - a))
            m = med[z]
            r_hp = ratio(cur * gi(stb, i, COL_HP), m["ehp"], RATIO_CAP)
            r_atk = ratio(gi(stb, i, COL_ATK), m["atk"], RATIO_CAP)
            r_hit = ratio(gi(stb, i, COL_HIT), m["hit"], ACC_RATIO_CAP)
            r_avd = ratio(gi(stb, i, COL_AVOID), m["avd"], ACC_RATIO_CAP)
            out[i] = {COL_LEVEL: lv,
                      COL_HP: max(1, round(ehp(lv) * TRASH_HP_MULT * r_hp / lv)),
                      COL_ATK: max(1, round(atk(lv) * TRASH_ATK_MULT * r_atk)),
                      COL_HIT: max(1, round(hit(lv) * r_hit)),
                      COL_AVOID: max(1, round(avd(lv) * r_avd))}
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load("import_oro", "import-oro.py")
    exp_mod = load("rebalance_exp_rewards", "rebalance-exp-rewards.py")
    stb = oro.Stb(NPC_STB)
    zone_of = oro_rows(oro, stb)
    if not zone_of:
        sys.exit("no Oro monsters found in the map lumps -- run import-oro-667.py first")

    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = {int(r): {int(c): v for c, v in cols.items()}
                     for r, cols in json.load(fh).items()}

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for row, cols in saved.items():
            for col, v in cols.items():
                stb.set(row, col, str(v))
        with open(NPC_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        if os.path.exists(BOSS_SIDECAR):
            os.remove(BOSS_SIDECAR)
        print(f"restored {len(saved)} rows; sidecars removed")
        print("re-run rebalance-endgame-curve.py --restore / re-apply, and rebalance-exp-rewards.py")
        return

    # plan from the ORIGINALS once applied, or the pass remaps its own output
    source = stb
    if saved:
        source = oro.Stb(NPC_STB)
        for row, cols in saved.items():
            for col, v in cols.items():
                source.set(row, col, str(v))
    rows = plan(source, zone_of, exp_mod)

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the rebalance has not been applied")
        bad = [r for r, cols in rows.items()
               if any(gi(stb, r, c) != v for c, v in cols.items())]
        over = [r for r in zone_of if gi(stb, r, COL_LEVEL) > LEVEL_CAP]
        print(f"{len(saved)} rows recorded; {len(bad)} do not match" + (f": {bad}" if bad else ""))
        if over:
            print(f"!! {len(over)} rows above the level-{LEVEL_CAP} cap: {over}")
        sys.exit(1 if bad or over else 0)

    if saved:
        print(f"already applied to {len(saved)} rows -- nothing to do (--restore first to re-plan)")
        return

    print(f"{'monster':<30}{'zone':<10}{'level':>10}{'effective HP':>22}{'ATK':>13}{'HIT':>12}{'AVOID':>11}")
    print("-" * 108)
    record = {}
    for row, cols in sorted(rows.items(), key=lambda kv: (zone_of[kv[0]], kv[1][COL_LEVEL])):
        old = {c: gi(stb, row, c) for c in cols}
        record[row] = old
        tag = "  BOSS" if row in BOSSES else ("  summon" if row in SUMMONS else "")
        print(f"  {stb.get(row, 0).decode('latin-1')[:27]:<28}{zone_of[row][:9]:<10}"
              f"{f'{old[COL_LEVEL]}->{cols[COL_LEVEL]}':>10}"
              f"{f'{old[COL_LEVEL] * old[COL_HP]:,}->{cols[COL_LEVEL] * cols[COL_HP]:,}':>22}"
              f"{f'{old[COL_ATK]}->{cols[COL_ATK]}':>13}"
              f"{f'{old[COL_HIT]}->{cols[COL_HIT]}':>12}"
              f"{f'{old[COL_AVOID]}->{cols[COL_AVOID]}':>11}{tag}")
        if not args.dry_run:
            for c, v in cols.items():
                stb.set(row, c, str(v))
    if args.dry_run:
        print(f"\ndry run: {len(rows)} rows would change")
        return

    with open(NPC_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({str(r): {str(c): v for c, v in cols.items()} for r, cols in record.items()},
                  fh, indent=1, sort_keys=True)
    with open(BOSS_SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({str(k): stb.get(k, 0).decode("latin-1") for k in sorted(BOSSES) if k in rows},
                  fh, indent=1, sort_keys=True)
    check = oro.Stb(NPC_STB)
    bad = [r for r, cols in rows.items() if any(gi(check, r, c) != v for c, v in cols.items())]
    if bad:
        sys.exit(f"VERIFY FAILED on {len(bad)} rows: {bad}")
    print(f"\nwrote {len(rows)} rows; sidecar {os.path.basename(SIDECAR)}, "
          f"boss sidecar {os.path.basename(BOSS_SIDECAR)} ({sum(1 for k in BOSSES if k in rows)} rows)")
    print("\nNEXT, in this order: rebalance-endgame-curve.py --stat def, --stat res, "
          "rebalance-exp-rewards.py, then every --verify")


if __name__ == "__main__":
    sys.exit(main())
