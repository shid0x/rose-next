#!/usr/bin/env python3
"""Put Karkia's monsters on our curve: levels, HP and ATK, plus the boss sidecar.

Karkia imported with Jrose's stats verbatim (roadmap stage 3, deliberately), and
they are authored for a much stronger player than ours. Measured against our own
level 210-229 band:

    DEF     750 ->  2500   3.3x      <- not this script's job, see below
    effHP 13420 -> 34808   2.6x
    ATK    1340 ->  2950   2.2x
    EXP    1955 -> 283173  145x      <- rebalance-exp-rewards.py
    HIT / RES / AVOID              1.2-1.3x, near enough to leave alone

DEF is the one that produced "4,200 swings per trash mob": damage is proportional
to `(ATK - DEF + 250)` and a level-215 character has ATK around 1,200-1,500, so a
DEF of 2,500 puts every hit on the damage floor. **That is already fixed by
`rebalance-endgame-curve.py`**, which caps every level-200+ monster at the trend
fitted from levels 60-199 -- it simply has not seen Karkia, because it last ran in
August and Karkia arrived in September. This script deliberately does not touch
DEF or RES; duplicating that logic would give us two things to keep in sync.

WHAT THIS ONE DOES
------------------
1. **Levels**, onto the bands in roadmap section 5, remapped linearly inside each
   zone so Jrose's own internal progression survives (Cemetery 211-221 -> 215-228,
   Spire Village 225-231 -> 228-238). Levels first, because the DEF/RES trend is
   indexed by level -- correcting DEF against the old levels and then moving the
   levels would leave the caps subtly wrong.
2. **HP**, the honest knob: it lengthens a fight without distorting how the fight
   reads. Trash goes to TRASH_HP_MULT x our own trend for its new level.
3. **ATK**, capped, because 2.2x our band is what kills a player in three swings.
4. **A boss sidecar**, which is not optional. `rebalance-endgame-curve.py` decides
   what is a boss with `row in sidecar OR NPC_HP >= 1000`, and step 2 lowers boss
   HP through that threshold -- exactly the trap its own comments warn about for
   `rebalance-oro-bosses.py`. Without the sidecar the bosses silently drop to the
   ordinary 1.0x DEF cap on the next run.

Ranking is preserved rather than flattened. Jrose's spread inside a zone is enormous
-- the Neg Golem is 41x the Cemetery median -- so each monster's ratio to its zone
median is square-rooted and then capped (RATIO_CAP), which keeps the Neg Golem the
tankiest thing in the Cemetery without it being sixteen times our own top boss.
Flattening everything to one number would have thrown away the only authoring signal
in the table.

ORDER (the DEF/RES pass caches its own trend and refuses to re-run over itself)
------------------------------------------------------------------------------
    python scripts/rebalance-endgame-curve.py --stat def --restore
    python scripts/rebalance-endgame-curve.py --stat res --restore
    python scripts/rebalance-karkia.py
    python scripts/rebalance-endgame-curve.py --stat def
    python scripts/rebalance-endgame-curve.py --stat res
    python scripts/rebalance-exp-rewards.py --restore && python scripts/rebalance-exp-rewards.py

Restoring first is what makes the re-apply see Karkia at its *new* levels. This is
the order dependency the root CLAUDE.md warns about, written out.

Idempotent, verifiable and reversible through a sidecar next to the STB. `data/` is
gitignored, so this docstring is the only committed record of the change.

Usage:
    python scripts/rebalance-karkia.py --dry-run
    python scripts/rebalance-karkia.py
    python scripts/rebalance-karkia.py --verify
    python scripts/rebalance-karkia.py --restore
"""
import argparse
import collections
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
SIDECAR = os.path.join(STB_DIR, "LIST_NPC.karkia.json")
BOSS_SIDECAR = os.path.join(STB_DIR, "LIST_NPC.karkia-bosses.json")

COL_LEVEL, COL_HP, COL_ATK = 7, 8, 9
FIT_BELOW = 200                    # our curve is trustworthy below the endgame break

# Karkia's own zones, from the REGEN lumps: Cemetery 2701-2730, Spire Village
# 2690-2699. Target bands come from roadmap section 5 -- Karkia is an alternative
# to late Oro, so it overlaps 208-240 rather than sitting above it.
ZONE_BANDS = {"KCEMETERY": (215, 228), "KSPIREVIL": (228, 238),
              # The flashback pair, stage 7b. Reached from the Church by a
              # player who already crossed Karkia, so they sit at the top of
              # the planet rather than beside the Cemetery, and the Garden
              # (reached only through Memories) sits above Memories.
              "KMEMORIES": (230, 238), "KFLOWERGARDEN": (235, 240)}

# Zones whose roster is deliberately a *reuse* of another zone's, and which must
# therefore be banded as one pool.
#
# The Burned Forest is present-day Karkia hanging off the Cemetery and shares its
# monsters. Without this alias `zone_of` (a setdefault over an alphabetical glob)
# attributes Woodnoid, Dark Tower, Evil Fairy and both Gargoyles to
# KBURNEDFOREST, which sorts first -- that zone has no band, so they drop out of
# the Cemetery's median and every remaining Cemetery monster re-plans to a
# different number. It is silent: the levels stay legal and only --verify
# notices, which it did, on 14 rows.
ZONE_ALIAS = {"KBURNEDFOREST": "KCEMETERY"}

# row -> (level, effective HP, ATK). Effective HP is level x NPC_HP.
# HP budget from roadmap section 5: a geared party does ~34-46 DPS against a boss
# in this DEF band, so the raid encounter totals ~450k across three bodies.
# ATK stays at 2,600-3,000: our own level-240 boss sits at 2,997 and already kills
# a baseline Champion in 4.2 swings, and past ~3,200 the healer's window is shorter
# than the server round trip.
BOSSES = {
    2729: (238, 180_000, 2800),    # Deadly Drake, the Cemetery zone boss
    2699: (240, 120_000, 2900),    # Deadly Drake Alpha, the raid opener
    2685: (240, 165_000, 2800),    # Hebarn Officer Pazugenti
    2686: (240, 165_000, 2800),    # Hebarn Officer Scylla Mira
    2687: (238,  60_000, 2600),    # Corroded Golem -- summoned by TRASH, so an
    2688: (235,  45_000, 2600),    # elite add, deliberately well under zone-boss
}

# Summon-only monsters that are not bosses. They never appear in a spawn lump, so
# the zone-band remap cannot reach them and they are named here instead.
#   2704 D-Seed is a stationary spawner (ATK 10, it never attacks) at 215,000
#        authored HP; clearing it should be an objective, not an endurance test.
#   2689 / 2731 Ghost Seed have ATK 1 and are pure nuisance spawn.
#   2705 D-Pollinosis comes six at a time off the D-Seed.
SUMMONS = {
    2704: (215,  40_000,   10),
    2705: (217,   6_000, 1500),
    2689: (225,   4_000,    1),
    2731: (215,   4_000,    1),
}

TRASH_HP_MULT = 1.5    # a real step up from our own field, not 2.6x of one
TRASH_ATK_MULT = 1.1   # 2.2x is a three-swing kill; this is roughly eight
RATIO_CAP = 3.0        # how far above its zone median any one monster may sit


def gi(stb, row, col):
    v = stb.get(row, col).strip()
    try:
        return int(v)
    except ValueError:
        return 0


def load_oro():
    spec = importlib.util.spec_from_file_location(
        "import_oro", os.path.join(HERE, "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, ["import-oro.py"]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = argv
    return mod


def karkia_rows(oro):
    """Karkia's monster ids and which zone spawns each, read from the map lumps.

    Scoped from the REGEN lumps rather than from a row range, for the reason in
    reference_monster_elite_signal: the STB is not the authority on what a zone
    actually contains.
    """
    import glob
    spec = importlib.util.spec_from_file_location(
        "import_karkia", os.path.join(HERE, "import-karkia.py"))
    ik = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, ["import-karkia.py"]
    try:
        spec.loader.exec_module(ik)
    finally:
        sys.argv = argv
    zone_of = {}
    maps = os.path.join(ROOT, "data", "3DDATA", "MAPS", "KARKIA")
    for p in glob.glob(os.path.join(maps, "**", "*.IFO"), recursive=True):
        zone = os.path.basename(os.path.dirname(p)).upper()
        try:
            buf, bounds = oro.read_ifo(p)
            objs, _ = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        except Exception:
            continue
        zone = ZONE_ALIAS.get(zone, zone)
        for o in objs or ():
            for i in oro.regen_mob_ids(o["extra"]):
                if i in ik.KARKIA_MONSTERS:
                    zone_of.setdefault(i, zone)
    return ik.KARKIA_MONSTERS, zone_of


def fit(stb, col, karkia, value=None):
    """Least squares on per-band medians below FIT_BELOW, Karkia excluded.

    `value` lets the caller fit effective HP (level x col) rather than a raw column.
    Karkia is excluded so the curve it is being measured against cannot include it.
    """
    band = collections.defaultdict(list)
    for r in range(1, stb.rows):
        # 2100-2399 is Oro's band: its Shadow Ghost ladder (183-219) sits inside
        # the fit window with values rebalance-oro-667.py authored, not retail's.
        # 3001-3003 are the Lucky Pig / Leprechaun event summons the same import
        # brought in for its AI closure: level 100 with HP 50 / ATK 10, i.e.
        # placeholder stats that would drag the 100-band median down.
        if (not stb.get(r, 0).strip() or r in karkia or 2100 <= r < 2400
                or 3001 <= r <= 3003 or trend_exclude.excluded(r)):
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
    return slope, my - slope * mx


def plan(stb, karkia_names, zone_of):
    """[(row, new_level, new_hp_col, new_atk)] plus the boss set."""
    karkia = set(karkia_names)
    hp_s, hp_i = fit(stb, None, karkia,
                     value=lambda s, r: gi(s, r, COL_LEVEL) * gi(s, r, COL_HP))
    atk_s, atk_i = fit(stb, COL_ATK, karkia)
    ehp_trend = lambda lv: max(1.0, hp_s * lv + hp_i)
    atk_trend = lambda lv: max(1.0, atk_s * lv + atk_i)

    # zone medians of the AUTHORED values, so each monster keeps its rank
    by_zone = collections.defaultdict(list)
    for i, z in zone_of.items():
        if i not in BOSSES and i not in SUMMONS:
            by_zone[z].append(i)
    med_ehp, med_atk = {}, {}
    for z, ids in by_zone.items():
        med_ehp[z] = statistics.median(gi(stb, i, COL_LEVEL) * gi(stb, i, COL_HP)
                                       for i in ids) or 1
        med_atk[z] = statistics.median(gi(stb, i, COL_ATK) for i in ids) or 1

    out = []
    for row, (lv, ehp, atk) in sorted(BOSSES.items()):
        out.append((row, lv, max(1, round(ehp / lv)), atk))
    for row, (lv, ehp, atk) in sorted(SUMMONS.items()):
        out.append((row, lv, max(1, round(ehp / lv)), atk))

    for z, ids in sorted(by_zone.items()):
        lo, hi = ZONE_BANDS.get(z, (None, None))
        if lo is None:
            continue
        lvs = [gi(stb, i, COL_LEVEL) for i in ids]
        a, b = min(lvs), max(lvs)
        for i in sorted(ids):
            cur = gi(stb, i, COL_LEVEL)
            lv = lo if b == a else round(lo + (cur - a) * (hi - lo) / float(b - a))
            r_hp = min(RATIO_CAP,
                       ((gi(stb, i, COL_LEVEL) * gi(stb, i, COL_HP)) / med_ehp[z]) ** 0.5)
            r_atk = min(RATIO_CAP, (gi(stb, i, COL_ATK) / med_atk[z]) ** 0.5)
            ehp = ehp_trend(lv) * TRASH_HP_MULT * r_hp
            atk = atk_trend(lv) * TRASH_ATK_MULT * r_atk
            out.append((i, lv, max(1, round(ehp / lv)), max(1, round(atk))))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_oro()
    stb = oro.Stb(NPC_STB)
    names, zone_of = karkia_rows(oro)

    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = {int(k): v for k, v in json.load(fh).items()}

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for row, old in saved.items():
            for col, v in zip((COL_LEVEL, COL_HP, COL_ATK), old):
                stb.set(row, col, str(v))
        with open(NPC_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        if os.path.exists(BOSS_SIDECAR):
            os.remove(BOSS_SIDECAR)
        print(f"restored level/HP/ATK on {len(saved)} rows; sidecars removed")
        print("re-run rebalance-endgame-curve.py --restore then re-apply, "
              "since the levels it capped against have moved")
        return

    # plan() reads levels and stats to derive its targets, so once the pass has been
    # applied it must be re-planned against the ORIGINALS or it remaps the already
    # remapped values and reports every trash row as a mismatch. The sidecar holds
    # them, so rebuild a pre-state view and plan from that.
    source = stb
    if saved:
        source = oro.Stb(NPC_STB)
        for row, old in saved.items():
            for col, v in zip((COL_LEVEL, COL_HP, COL_ATK), old):
                source.set(row, col, str(v))
    rows = plan(source, names, zone_of)

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the rebalance has not been applied")
        bad = [r for r, lv, hp, atk in rows
               if (gi(stb, r, COL_LEVEL), gi(stb, r, COL_HP), gi(stb, r, COL_ATK))
               != (lv, hp, atk)]
        over = [r for r in names if gi(stb, r, COL_LEVEL) > 240]
        print(f"{len(saved)} rows recorded; {len(bad)} do not match"
              + (f": {bad}" if bad else ""))
        if over:
            print(f"!! {len(over)} rows above the level-240 cap: {over}")
        sys.exit(1 if bad or over else 0)

    if saved:
        print(f"already applied to {len(saved)} rows -- nothing to do.")
        print("re-run with --restore first if you want to change the parameters.")
        return

    print(f"{'monster':<28}{'level':>12}{'effective HP':>22}{'ATK':>16}")
    print("-" * 78)
    record = {}
    for row, lv, hp, atk in sorted(rows, key=lambda x: -(x[1] * x[2])):
        old = (gi(stb, row, COL_LEVEL), gi(stb, row, COL_HP), gi(stb, row, COL_ATK))
        record[row] = list(old)
        tag = "  BOSS" if row in BOSSES else ("  summon" if row in SUMMONS else "")
        print(f"  {names.get(row, '?')[:25]:<26}"
              f"{f'{old[0]} -> {lv}':>12}"
              f"{f'{old[0] * old[1]:,} -> {lv * hp:,}':>22}"
              f"{f'{old[2]} -> {atk}':>16}{tag}")
        if not args.dry_run:
            stb.set(row, COL_LEVEL, str(lv))
            stb.set(row, COL_HP, str(hp))
            stb.set(row, COL_ATK, str(atk))

    if args.dry_run:
        print(f"\ndry run: {len(rows)} rows would change")
        return

    with open(NPC_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({str(k): v for k, v in record.items()}, fh, indent=1, sort_keys=True)
    with open(BOSS_SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({str(k): names.get(k, "") for k in sorted(BOSSES)}, fh,
                  indent=1, sort_keys=True)

    check = oro.Stb(NPC_STB)
    bad = [r for r, lv, hp, atk in rows
           if (gi(check, r, COL_LEVEL), gi(check, r, COL_HP), gi(check, r, COL_ATK))
           != (lv, hp, atk)]
    if bad:
        sys.exit(f"VERIFY FAILED on {len(bad)} rows: {bad}")
    print(f"\nwrote {len(rows)} rows; sidecar {os.path.basename(SIDECAR)}")
    print(f"boss sidecar {os.path.basename(BOSS_SIDECAR)} lists {len(BOSSES)} rows -- "
          f"rebalance-endgame-curve.py needs it, because this pass lowers boss HP "
          f"below the NPC_HP >= 1000 threshold it would otherwise recognise them by")
    print("\nNEXT, in this order:")
    print("   rebalance-endgame-curve.py --stat def --restore   (then without --restore)")
    print("   rebalance-endgame-curve.py --stat res --restore   (then without --restore)")
    print("   rebalance-exp-rewards.py  --restore               (then without --restore)")


if __name__ == "__main__":
    sys.exit(main())
