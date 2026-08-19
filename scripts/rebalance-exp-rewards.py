"""Solve every monster's EXP reward back from a target levelling pace.

The problem this fixes is that nothing in the code or the data ties the two
halves of levelling together. `CCal::Get_NeedRawEXP` grows cubically with level;
the LIST_NPC EXP column does not grow at all -- its median is 22 at level 10, 122
at level 140, and it *falls back* to 118 across the Sikuku tier. The pace of the
game is the ratio of those two numbers, and measured against every monster that
actually spawns on a map it collapses by a factor of two thousand:

    level  20:      96 kills for one level   (1.05  % per kill)
    level  50:     466                       (0.215 %)
    level 143:   1,537  Pincer               (0.065 %)
    level 200:  35,843                       (0.003 %)
    level 240:  66,489                       (0.0015%)

604,853 yellow-name kills from 1 to 240, ~500 hours of pure grinding, and 99.6%
of it above level 180.

WHAT THIS PASS DOES

Rather than nudge the EXP column, it inverts the reward formula. For a solo kill
`CCal::Get_EXP` reduces to

    exp per kill = (1 + 1/15) x WORLD_VAR_EXP / 370 x (mob_level + 3) x EXP_column
                 = 0.2883 x (mob_level + 3) x EXP_column        (at the default rate 100)

so given a target number of kills per level the column falls straight out:

    EXP_column = need(lv) / target_kills(lv) / 0.2883 / (lv + 3)

The pace ladder is anchored on level 50, which is the one point in the game that
currently feels right at 466 kills, and then compounds 10% per fifty levels:

    lv   1- 50   untouched -- no row at or below level 50 is written at all
    lv  51-100   466 -> 513      (+10%)
    lv 101-150   513 -> 564      (+10%)
    lv 151-200   564 -> 620      (+10%)
    lv 200-240   682, flat       (+10%, then held to the cap)

Anchors are interpolated, not stepped, so level 75 sits between 466 and 513. The
one discontinuity is deliberate: the last tier starts *at* 200 rather than ramping
into it, so crossing into the endgame is a single visible 10% step.

Note this is +10% per tier in KILLS, not in time. Monster max HP goes from ~1,240
at level 50 to ~13,220 at level 220, so the same kill count is far more work: a
level at the cap costs 18.5x the total HP of a level at 50, and roughly 4x the
time even assuming player damage grows linearly with level. That is intended --
rising time per level is what makes an endgame feel like one -- but it is the
reason the kill counts here look almost flat when the experience is not.

ELITES

Strong monsters must be worth more, and the obvious ways to spot them are both
broken in our data:

  * **NPC_HP column >= 1000**, the heuristic the other balance passes use, now
    matches ZERO spawning monsters. rebalance-oro-bosses.py capped the Oro kings
    down to 364-373, and everything older (Astarot King 609, Behemoth King 462,
    the Guardians 375-426) was always below the bar.
  * **The EXP column itself**, ranked against its tier, reads ordinary Oro field
    monsters as 26-49x elites -- the level-200 band straddles the Eldeon/Oro
    import seam, so the median is Eldeon-shaped while the Oro columns are ~5.5x
    larger.

Max HP has neither problem. It is on one consistent scale across that seam and it
is what actually makes a kill expensive:

    toughness = max_HP / median max_HP of the tier        (max_HP = level x NPC_HP)
    multiplier = clamp(toughness, 1, ELITE_CAP) ^ ELITE_EXPONENT

The exponent is the knob that decides whether elites are worth *hunting* rather
than merely worth more per kill: at 1.0 a monster with 14x the HP pays 14x the EXP
and is exactly break-even on EXP per hour. At 1.5 it pays 52x, i.e. ~3.7x the
hourly rate of ordinary grinding. ELITE_CAP is a ceiling on toughness, not on the
multiplier, and at 25 it currently binds on nothing -- the toughest spawning
monster in the game is Aqua Guardian at 19.3x. It is there to stop a future import
of something with 100x HP from paying 1000x.

CALIBRATION

The median ordinary monster sits slightly above its own tier median in HP, so it
would earn a multiplier a little over 1.0 and the whole tier would land faster than
the anchor. `calib(lv)` is the median multiplier among ordinary monsters at that
level, divided back out so the median monster lands exactly on the ladder.

SCOPE

Scope comes from the map REGEN lumps, never from the STB alone. LIST_NPC also
holds quest actors, summons and unspawned duplicates whose stats are nothing like
a field monster's: the quest Moss Golem, Nepenthes and Turak rows carry an HP
column of 13 million (1.95 BILLION max HP), the PhantomSword summon ladder sits at
83k-116k max HP around level 55, and the dragons at 783k. Ranked against their
tiers those read as thousand-fold elites and would be paid as such. None of them
regenerate on any map, so the spawn scope excludes every one -- which is also why
the two spawn thresholds below are different numbers.

Rows are then skipped when the monster is at or below FREEZE_TO, when it has no
name, or when its EXP column is exactly 1 -- that value is a deliberate "worth
nothing" sentinel (Skeleton, the Hornet set, the event mobs) and the game's own
`if (iEXP < 1) iEXP = 1` clamp shows 1 is the floor, not a real reward.

This pass pairs with the `CCal::Get_NeedRawEXP` change that continues the cubic
past level 189 instead of handing over to the quintic. Applied without it the
level 190-240 columns come out ~200x too large, because they would be solving
against an 11.79-billion requirement at the cap.

ORDER DEPENDENCY: this reads levels and max HP, so re-apply it after anything that
changes either -- rebalance-oro-bosses.py in particular caps both.

Idempotent, verifiable and reversible through a sidecar next to the STB.

Usage:
    python scripts/rebalance-exp-rewards.py --dry-run
    python scripts/rebalance-exp-rewards.py
    python scripts/rebalance-exp-rewards.py --verify
    python scripts/rebalance-exp-rewards.py --restore
"""
import argparse
import collections
import glob
import importlib.util
import json
import os
import statistics
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
NPC_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.exp-rewards.json")
MAPS = os.path.join(ROOT, "data", "3DDATA", "MAPS")

COL_LEVEL, COL_HP, COL_EXP = 7, 8, 17
MAX_LEVEL = 240                 # GameStaticConfig::MAX_LEVEL

# (1 + 1/15) * WORLD_VAR_EXP / 370, with WORLD_VAR_EXP at its CWorldVAR default of
# 100. The 1/15 and the +30 in Get_EXP are the mob's own max-HP terms; for a solo
# kill the damage share is the full HP bar, so they collapse to this constant.
EXP_FORMULA_K = (1.0 + 1.0 / 15.0) * 100.0 / 370.0

FREEZE_TO = 50                  # levels 1-50 keep today's pace exactly
ANCHOR_KILLS = 466.0            # measured pace at level 50 today
TIER_STEP = 0.10
TIER_SIZE = 50
LAST_TIER_FROM = 200            # flat from here to the cap

ELITE_CAP = 25.0
ELITE_EXPONENT = 1.5

ORDINARY_HP = 2.0               # "ordinary field monster" = max HP within 2x the tier median
BAND = 5                        # tier reference window, +/- levels
MIN_BAND = 7                    # widen the window until it holds at least this many monsters
# Two different thresholds on purpose. The tier reference wants a representative
# sample, so a monster with one or two placements must not move a median. But the
# rewrite scope wants "does this exist in the world at all", and a boss legitimately
# has one to three spawn points -- Astarot King has 2, Crowned Asper King has 1.
# One threshold for both jobs either lets junk skew the tiers or leaves every boss
# in the game on its old EXP.
MIN_SPAWNS_REFERENCE = 4
MIN_SPAWNS_SCOPE = 1

SENTINEL_EXP = 1                # "worth nothing", never lifted


def load_oro():
    """import-oro.py carries the STB and IFO codecs the other passes reuse."""
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


def need_raw_exp(lv):
    """CCal::Get_NeedRawEXP, with the cubic continued past 189."""
    lv = min(lv, MAX_LEVEL)
    if lv <= 15:
        return int((lv + 3) * (lv + 5) * (lv + 10) * 0.7)
    if lv <= 60:
        return int((lv - 5) * (lv + 2) * (lv + 2) * 2.2)
    if lv <= 113:
        return int((lv - 11) * lv * (lv + 4) * 2.5)
    if lv <= 150:
        return int((lv - 31) * (lv - 20) * (lv + 4) * 3.8)
    return (lv - 67) * (lv - 20) * (lv - 10) * 6


def ladder():
    """(level, kills) anchors: level 50 as measured, then +10% per fifty levels."""
    pts = []
    for t in range(0, 4):
        pts.append((FREEZE_TO + t * TIER_SIZE, ANCHOR_KILLS * (1.0 + TIER_STEP) ** t))
    return pts, ANCHOR_KILLS * (1.0 + TIER_STEP) ** 4


ANCHORS, LAST_TIER_KILLS = ladder()


def target_kills(lv):
    if lv >= LAST_TIER_FROM:
        return LAST_TIER_KILLS
    for (a, ka), (b, kb) in zip(ANCHORS, ANCHORS[1:]):
        if a < lv <= b:
            return ka + (kb - ka) * (lv - a) / (b - a)
    return ANCHORS[-1][1]


def spawning_monsters(oro):
    """Row -> total placed count, from every map's REGEN lump.

    Scope comes from the maps rather than the STB because LIST_NPC is full of
    quest actors, event props and unspawned duplicates whose stats would poison
    every tier median they land in.
    """
    def regen_mobs(extra):
        o = 0

        def bstr():
            nonlocal o
            n = extra[o]
            o += 1
            s = extra[o:o + n]
            o += n
            return s

        bstr()
        out = []
        for _ in range(2):
            cnt, = struct.unpack_from("<i", extra, o)
            o += 4
            for _ in range(cnt):
                bstr()
                mob, num = struct.unpack_from("<ii", extra, o)
                o += 8
                out.append((mob, num))
        return out

    counts = collections.Counter()
    for path in glob.glob(os.path.join(MAPS, "**", "*.IFO"), recursive=True):
        try:
            buf, bounds = oro.read_ifo(path)
            objs, _ = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        except Exception:
            continue
        for ob in objs or []:
            try:
                for mob, num in regen_mobs(ob["extra"]):
                    if mob > 0:
                        counts[mob] += max(num, 0)
            except Exception:
                pass
    return counts


def tier_reference(stb, spawns):
    """median max HP per level, and the calibration factor for the median monster."""
    by_level = collections.defaultdict(list)
    for row, total in spawns.items():
        if (row >= stb.rows or total < MIN_SPAWNS_REFERENCE
                or not stb.get(row, 0).strip()):
            continue
        lv, hpc, exp = gi(stb, row, COL_LEVEL), gi(stb, row, COL_HP), gi(stb, row, COL_EXP)
        if lv <= 0 or hpc <= 0 or exp <= SENTINEL_EXP:
            continue
        by_level[lv].append(lv * hpc)

    def band(lv):
        out = []
        for d in range(0, 40):
            for s in ((lv,) if d == 0 else (lv - d, lv + d)):
                out += by_level.get(s, [])
            if len(out) >= MIN_BAND and d >= BAND:
                break
        return out

    median_hp = {}
    for lv in range(1, MAX_LEVEL + 1):
        hps = sorted(band(lv))
        if hps:
            # bottom three quarters: the elites in the band must not drag up the
            # very reference they are measured against
            median_hp[lv] = statistics.median(hps[:max(3, int(len(hps) * 0.75))])

    def multiplier(lv, max_hp):
        return min(ELITE_CAP, max(1.0, max_hp / median_hp[lv])) ** ELITE_EXPONENT

    calib = {}
    for lv in median_hp:
        ordinary = [h for h in band(lv) if h <= ORDINARY_HP * median_hp[lv]]
        calib[lv] = (statistics.median([multiplier(lv, h) for h in ordinary])
                     if ordinary else 1.0)
    return median_hp, calib, multiplier


def planned(stb, spawns, median_hp, calib, multiplier):
    """Row -> new EXP column, for every row this pass is willing to touch.

    Scope is the map REGEN lumps, not the STB. LIST_NPC also holds quest actors,
    summons and unspawned duplicates whose stats are nothing like a field monster's
    -- the quest Moss Golem, Nepenthes and Turak rows carry an HP column of 13
    million (1.95 BILLION max HP), the PhantomSword summon ladder sits at 83k-116k
    max HP around level 55, and the dragons at 783k. All of them read as
    thousand-fold elites against their tier and would be paid accordingly. None of
    them regenerate on any map, so the spawn scope excludes every one.
    """
    out = {}
    for row in range(1, stb.rows):
        if not stb.get(row, 0).strip() or spawns.get(row, 0) < MIN_SPAWNS_SCOPE:
            continue
        lv, hpc, exp = gi(stb, row, COL_LEVEL), gi(stb, row, COL_HP), gi(stb, row, COL_EXP)
        if lv <= FREEZE_TO or hpc <= 0 or exp <= SENTINEL_EXP or lv not in median_hp:
            continue
        standard = (need_raw_exp(lv) / target_kills(lv)
                    / EXP_FORMULA_K / (lv + 3) / calib[lv])
        out[row] = max(1, int(round(standard * multiplier(lv, lv * hpc))))
    return out


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
        for row, old in saved.items():
            stb.set(row, COL_EXP, str(old))
        with open(NPC_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print(f"restored the EXP column on {len(saved)} rows; sidecar removed")
        return

    spawns = spawning_monsters(oro)
    if len(spawns) < 100:
        sys.exit(f"only {len(spawns)} spawning monsters found in {MAPS} -- "
                 "the map scan failed, refusing to fit tiers on that")
    median_hp, calib, multiplier = tier_reference(stb, spawns)
    plan = planned(stb, spawns, median_hp, calib, multiplier)

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the rebalance has not been applied")
        # The plan is re-derived from the *current* table, whose EXP column this
        # pass already rewrote. That is safe precisely because nothing here reads
        # the EXP column as an input: tiers come from level and max HP, and the
        # column is only checked for the <= 1 sentinel, which is never written.
        bad = [row for row in saved if gi(stb, row, COL_EXP) != plan.get(row)]
        missing = sorted(set(plan) - set(saved))
        print(f"{len(saved)} rows recorded; {len(bad)} do not match"
              + (f": {bad[:12]}" if bad else ""))
        if missing:
            print(f"{len(missing)} rows are now in scope but were not written: {missing[:12]}")
        sys.exit(1 if (bad or missing) else 0)

    if saved:
        print(f"already applied to {len(saved)} rows -- nothing to do.")
        print("re-run with --restore first if you want to change the parameters.")
        return

    print(f"ladder anchored at level {FREEZE_TO} = {ANCHOR_KILLS:.0f} kills, "
          f"+{TIER_STEP * 100:.0f}% per {TIER_SIZE} levels")
    for lv, k in ANCHORS:
        print(f"    lv {lv:>3}: {k:>6.0f} kills")
    print(f"    lv {LAST_TIER_FROM}+: {LAST_TIER_KILLS:>6.0f} kills, flat to {MAX_LEVEL}")
    print(f"elites: toughness ^ {ELITE_EXPONENT} capped at {ELITE_CAP:.0f}x\n")

    record, rows = {}, []
    for row, new in sorted(plan.items()):
        cur = gi(stb, row, COL_EXP)
        record[row] = cur
        stb.set(row, COL_EXP, str(new))
        lv, hpc = gi(stb, row, COL_LEVEL), gi(stb, row, COL_HP)
        rows.append((row, lv, cur, new, (lv * hpc) / median_hp[lv]))

    up = sum(1 for _, _, c, n, _ in rows if n > c)
    down = sum(1 for _, _, c, n, _ in rows if n < c)
    spawning = [r for r, n in spawns.items()
                if n >= MIN_SPAWNS_SCOPE and r < stb.rows and stb.get(r, 0).strip()
                and gi(stb, r, COL_LEVEL) > 0]
    frozen = sum(1 for r in spawning if gi(stb, r, COL_LEVEL) <= FREEZE_TO)
    print(f"{len(rows)} rows rewritten, of {len(spawning)} monsters that spawn on a map: "
          f"{up} raised, {down} lowered, {len(rows) - up - down} unchanged")
    print(f"{frozen} of those sit at or below level {FREEZE_TO} and are untouched; "
          f"the rest carry the <= 1 sentinel")
    print(f"{stb.rows - len(spawning)} rows never regenerate on any map "
          f"(quest actors, summons, duplicates) and are out of scope\n")

    def show(title, sel):
        print(f"  {title}")
        print(f"    {'monster':<32}{'lv':>5}{'tough':>8}{'EXP column':>22}")
        for row, lv, cur, new, tough in sel:
            name = stb.get(row, 0).decode("latin-1", "replace")[:31]
            print(f"    {name:<32}{lv:>5}{tough:>7.1f}x{f'{cur:,} -> {new:,}':>22}")

    show("largest reductions", sorted(rows, key=lambda r: r[3] / max(r[2], 1))[:8])
    print()
    show("toughest monsters in the game", sorted(rows, key=lambda r: -r[4])[:8])

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    with open(NPC_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({str(k): v for k, v in record.items()}, fh, indent=1)

    chk = oro.Stb(NPC_STB)
    for row, _, _, new, _ in rows:
        assert gi(chk, row, COL_EXP) == new, (row, gi(chk, row, COL_EXP), new)
    for row in range(1, chk.rows):
        if 0 < gi(chk, row, COL_LEVEL) <= FREEZE_TO:
            assert gi(chk, row, COL_EXP) == gi(stb, row, COL_EXP)
    print(f"\ndone -- {len(record)} rows rewritten and verified.")
    print("Rebuild the client (Get_NeedRawEXP is shared code), re-bake the VFS, "
          "and restart the game server (STBs are cached at startup).")


if __name__ == "__main__":
    main()
