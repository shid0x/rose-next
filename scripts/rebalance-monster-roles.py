"""Monster identity by icon: role profiles on spawned monsters (2026-10-15).

The icon beside a monster's name (LIST_NPC col 27) says what it is, but the old balance
passes had flattened the roles: Guard, Fighter and Warrior sat within 10% of Basic on
every stat, the stars were no ladder (bronze 1.4x HP, silver 1.6x, gold 5.2x), and Basic
was a grab-bag (ATK 0.22-1.24x, DEF 0.72-3x the level trend). The user's design, checked
in the combat simulator (class sweep, fights against each icon relative to a Basic of the
same level):

    icon        identity                         result vs Basic (kill time / damage taken)
    Guard       a wall: long to kill, hits soft  1.55 / 0.71
    Fighter     balanced, slightly aggressive    1.00 / 1.15
    Warrior     heavy hitter, softer             1.00 / 1.62
    Ranger      ranged, fragile, evasive         1.00 / 1.17
    Hunter      ranged, bigger slower shots      0.68 / 0.88
    Magician    magic damage, soft, resistant    1.00 / 0.96
    Leader      bronze star: a tougher leader    1.70 / 0.92
    Captain     silver star: a mini-fight        7.6  / 1.89
    Sub-boss    gold star: two-player content    solo wins ~25%, a duo ~100%

Monster damage grows much faster than ATK (damage follows ATK minus the player's
defence), so the ATK gaps are narrower than they look: a first draft at Warrior ATK 1.3
made Warriors hit 2.1x and Guards at 0.8 nearly harmless.

How: TARGET is each icon's stat as a multiple of the level trend (the median of
plain-role spawned monsters within 10 levels). Per icon and stat one factor moves the
icon's median onto its target, so every monster keeps its own quirks. Plain roles average
about 1 on each stat. Basic's extremes are then tidied into [TIDY_LO, TIDY_HI] x the
trend (critters matter in AoE fights). Kings and skulls are untouched (group content),
as are rows the trend excludes (balance-trend-exclude.py: hand-tuned imports such as the
Cerberus Lair).

Clan-field bosses (CLAN_BOSSES: the Junon Clan Field Guardians, Astarot, Astarot King)
are clan content, "a five-player job": HP x CLAN_HP, ATK x CLAN_ATK on their own values.
Simulated with HP and attacks split over the group: solo ~20%, a duo ~40%, five ~90%.
The Luna Clan Field's regular monsters (CLAN_DUO: Tirwins, Elec Ghosts) are two-player
content on top of their role: HP x DUO_HP, ATK x DUO_ATK (solo ~35%, a duo ~85%).

Kings and skulls (BOSS_HP): early kings below level 80 and kings are for three players,
skulls for five (scaled back from five / ten on 2026-10-16: the alpha pool is 5-6 players) (four field elites that wore a skull
are silver stars now, fix-monster-marks.py). HP only: each boss's multiplier (x2.35 ..
x41.66, frozen from the sim) puts its fight at ~45 s / 90 s / 3 min for its group (sim, at
the first group sizes: 42-67 s, 72-117 s, 149-202 s). Not the Desert of the Dead's four (PvPvE zone, later).
The ten bosses that never cast a skill (NOSKILL_BOSSES) get ATK x NOSKILL_ATK instead;
skills weaker than the boss's swing got a boss-only copy (boost-boss-skills.py).

This is a layer, applied last. The older monster passes (rebalance-karkia.py,
-oro-667.py, -endgame-curve.py, -oro-bosses.py, -eldeon-outliers.py, -exp-rewards.py) read
LIST_NPC without it (balance-trend-exclude.without_roles) and refuse to write while it is
applied, so they keep their own view. EXP stays priced on the pre-role HP (to be decided).
Note those passes already failed their own --verify before this one existed; the user
considers them outdated.

    python scripts/rebalance-monster-roles.py --dry-run
    python scripts/rebalance-monster-roles.py
    python scripts/rebalance-monster-roles.py --verify
    python scripts/rebalance-monster-roles.py --restore

Cell-level manifest in build/monster-roles/ ([pre-role, written] per cell); restore puts
back the cells that still hold what this pass wrote. The server reads LIST_NPC: restart it,
and re-bake the VFS. `data/` is gitignored, so this file is the record.
"""

import argparse
import importlib.util
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
from combatsim import GameData, sweep  # noqa: E402

_te_spec = importlib.util.spec_from_file_location(
    "balance_trend_exclude", os.path.join(HERE, "balance-trend-exclude.py"))
trend_exclude = importlib.util.module_from_spec(_te_spec)
_te_spec.loader.exec_module(trend_exclude)

NPC_STB = os.path.join(REPO, "data", "3DDATA", "STB", "LIST_NPC.STB")
MANIFEST = trend_exclude.ROLE_MANIFEST
COL_LEVEL, COL_MARK = 7, 27
COLS = dict(hp=8, atk=9, df=11, res=12, hit=10, avoid=13, aspd=14)
ORDER = ("hp", "atk", "df", "res", "hit", "avoid", "aspd")
PLAIN = set(range(1, 8))
TIDY_LO, TIDY_HI = 0.67, 1.5
TARGET = {   # icon: hp, atk, def, res, hit, avoid, attack speed (x the level trend)
    1: (1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0),       # Basic
    2: (1.2, 0.9, 1.2, 1.1, 0.95, 0.85, 0.9),     # Guard
    3: (1.0, 1.05, 1.0, 0.95, 1.05, 1.05, 1.05),  # Fighter
    4: (0.9, 1.15, 0.9, 0.85, 1.0, 0.9, 1.0),     # Warrior
    5: (0.85, 0.95, 0.85, 1.0, 1.05, 1.2, 1.0),   # Ranger (bow)
    6: (0.9, 1.1, 0.9, 0.95, 1.1, 1.0, 0.85),     # Hunter (crossbow)
    7: (0.85, 1.05, 0.8, 1.4, 1.0, 1.0, 0.9),     # Magician (staff)
    8: (1.5, 1.1, 1.1, 1.1, 1.05, 1.0, 1.0),      # Leader (bronze star)
    9: (2.2, 1.2, 1.15, 1.15, 1.1, 1.0, 1.05),    # Captain (silver star)
    10: (4.0, 1.35, 1.2, 1.2, 1.15, 1.0, 1.05),   # Sub-boss (gold star): two players
}
CLAN_BOSSES = {215: "Aqua Guardian", 216: "Grunter Guardian", 217: "Krawfy Guardian",
               218: "Golem Guardian", 219: "Goblin Guardian", 395: "Astarot",
               396: "Astarot King"}
CLAN_HP, CLAN_ATK = 2.5, 1.25
# The Luna Clan Field's regular monsters are two-player content (user), on top of their
# role: sim solo wins ~35%, a duo ~85%. Tirwin 397 also spawns in Forgotten Temple B1, so
# it keeps its plain role profile.
CLAN_DUO = {387: "Elec Ghost", 388: "Elec Ghost", 477: "Tirwin", 478: "Tirwin",
            487: "Tirwin", 488: "Tirwin", 497: "Tirwin", 498: "Tirwin"}
DUO_HP, DUO_ATK = 4.5, 1.6
# Kings and skulls (user, 2026-10-15; scaled back 2026-10-16 for an alpha pool of 5-6
# players): early kings (below level 80) and kings are for three players, skulls for
# five. The frozen multipliers were measured for 5 / 10 and scaled by 3/5 and 5/10 (a
# fight's length goes as HP / group). HP only -- normal attacks
# stay (their AI skills carry the threat; a skill pass may follow). Each boss's multiplier
# puts its fight at BOSS_FIGHT_S for its group: today's fight = HP / (group x the players'
# DPS against it, five classes at its level, combat sim). Frozen here so a re-run never
# depends on the simulator. Not here: the Desert of the Dead's four (PvPvE, later) and the
# clan bosses above.
BOSS_FIGHT_S = {"early king": (3, 45), "king": (3, 90), "skull": (5, 180)}
# Bosses that never cast a skill in the sim get a small ATK lift instead (user: "those may
# actually benefit from a little attack boost"); a skill weaker than the swing got a
# boss-only stronger copy (boost-boss-skills.py).
NOSKILL_ATK = 1.15
NOSKILL_BOSSES = {203: "Jelly King", 205: "Woopie King", 2254: "Poisontail Scorpio King",
                  2281: "Golden Scarab King", 2247: "Desert Scavenger King",
                  2530: "Basilissa Melitta", 2259: "Prehistoric Mastyx King",
                  2276: "Camouflaged Armastyx King", 2729: "Deadly Drake",
                  2699: "Deadly Drake Alpha"}
BOSS_HP = {   # row: (name, HP multiplier) -- frozen from the combat sim (see above)
    203: ('Jelly King', 41.66),   # early lv 13, 3 players 1 s today
    205: ('Woopie King', 13.45),   # early lv 25, 3 players 3 s today
    207: ('Aqua King', 4.89),   # early lv 40, 3 players 9 s today
    209: ('Grunter King', 4.19),   # early lv 60, 3 players 11 s today
    212: ('Krawfy King', 3.35),   # early lv 75, 3 players 13 s today
    1543: ('Mountain Bebeg', 7.63),   # king lv 80, measured 7 s for 5 players
    376: ('Dreadnaught King', 2.69),   # king lv 133, measured 20 s for 5 players
    382: ('Behemoth King', 2.35),   # king lv 142, measured 23 s for 5 players
    534: ('King Ulverick', 4.13),   # king lv 151, measured 13 s for 5 players
    2207: ('Crowned Asper King', 10.84),   # king lv 222, measured 5 s for 5 players
    2254: ('Poisontail Scorpio King', 10.54),   # king lv 224, measured 5 s for 5 players
    2281: ('Golden Scarab King', 9.02),   # king lv 226, measured 6 s for 5 players
    2722: ('Neg Golem', 14.39),   # king lv 228, measured 4 s for 5 players
    2247: ('Desert Scavenger King', 7.99),   # king lv 231, measured 7 s for 5 players
    2530: ('Basilissa Melitta', 27.14),   # king lv 238, measured 2 s for 5 players
    2259: ('Prehistoric Mastyx King', 6.55),   # king lv 239, measured 8 s for 5 players
    2276: ('Camouflaged Armastyx King', 6.98),   # king lv 240, measured 8 s for 5 players
    2265: ('Fearsome Terrasaurus King', 6.61),   # king lv 240, measured 8 s for 5 players
    4068: ('Deader Rex', 11.29),   # skull lv 100, measured 8 s for 10 players
    532: ("Ulverick's Leader", 18.25),   # skull lv 135, measured 5 s for 10 players
    533: ("Ulverick's General", 15.99),   # skull lv 145, measured 6 s for 10 players
    1591: ('Moss Golem', 20.37),   # skull lv 210, measured 4 s for 10 players
    2226: ('Grand Master Devourer', 25.39),   # skull lv 230, measured 4 s for 10 players
    2296: ('Ikaness Mecha', 22.57),   # skull lv 234, measured 4 s for 10 players
    2221: ('Giant Fiery Snapper', 24.2),   # skull lv 237, measured 4 s for 10 players
    2729: ('Deadly Drake', 8.63),   # skull lv 238, measured 10 s for 10 players
    1719: ('Executor Kera', 26.5),   # skull, measured at lv 240 (3 s for 10 players); lv 225 since 2026-11-20
    2699: ('Deadly Drake Alpha', 13.79),   # skull lv 240, measured 7 s for 10 players
}


def num(b):
    s = b.strip()
    return int(s) if s.lstrip(b"-").isdigit() else 0


def plan(stb, spawned):
    """{(row, col): new value} from the pre-role table `stb`."""
    def val(r, k):
        v = num(stb.get(r, COLS[k]))
        return 100 if k == "aspd" and v <= 0 else v

    mobs = [r for r in spawned if num(stb.get(r, 9)) > 0 and num(stb.get(r, 8)) > 0]
    level = {r: max(1, num(stb.get(r, COL_LEVEL))) for r in mobs}
    mark = {r: num(stb.get(r, COL_MARK)) for r in mobs}
    base = {r: {k: val(r, k) for k in COLS} for r in mobs}

    def x(r, k, v=None):
        v = base[r][k] if v is None else v
        return level[r] * v if k == "hp" else v

    trend = {}
    for r in mobs:
        peers = [p for p in mobs if mark[p] in PLAIN and abs(level[p] - level[r]) <= 10]
        if len(peers) >= 5:
            trend[r] = {k: statistics.median(x(p, k) for p in peers) for k in COLS}
    scope = [r for r in mobs if mark[r] in TARGET and r in trend
             and not trend_exclude.excluded(r) and r not in CLAN_BOSSES]

    def ratio(r, k, v=None):
        t = trend[r][k]
        return x(r, k, v) / t if t else None

    factor = {}
    for m in TARGET:
        rows = [r for r in scope if mark[r] == m]
        factor[m] = {}
        for i, k in enumerate(ORDER):
            rs = [ratio(r, k) for r in rows if ratio(r, k)]
            cur = statistics.median(rs) if rs else 1.0
            factor[m][k] = TARGET[m][i] / cur if cur else 1.0
    out = {}
    for r in scope:
        m = mark[r]
        for k in ORDER:
            v = base[r][k] * factor[m][k]
            if m == 1:
                rr = ratio(r, k, v)
                if rr and rr < TIDY_LO:
                    v *= TIDY_LO / rr
                elif rr and rr > TIDY_HI:
                    v *= TIDY_HI / rr
            v = max(1, round(v))
            if v != base[r][k] or (k == "aspd" and num(stb.get(r, COLS[k])) <= 0 and v != 100):
                out[(r, COLS[k])] = v
    for r in CLAN_BOSSES:
        out[(r, COLS["hp"])] = max(1, round(num(stb.get(r, COLS["hp"])) * CLAN_HP))
        out[(r, COLS["atk"])] = max(1, round(num(stb.get(r, COLS["atk"])) * CLAN_ATK))
    for r, (_name, mult) in BOSS_HP.items():
        out[(r, COLS["hp"])] = max(1, round(num(stb.get(r, COLS["hp"])) * mult))
    for r in NOSKILL_BOSSES:
        out[(r, COLS["atk"])] = max(1, round(num(stb.get(r, COLS["atk"])) * NOSKILL_ATK))
    for r in CLAN_DUO:                  # on top of the role profile
        for k, mult in (("hp", DUO_HP), ("atk", DUO_ATK)):
            key = (r, COLS[k])
            out[key] = max(1, round(out.get(key, num(stb.get(*key))) * mult))
    return out, factor, mark, level


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    raw = open(NPC_STB, "rb").read()
    stb = oro.Stb(NPC_STB)
    if stb.to_bytes() != raw:
        print("ABORT: the STB writer does not round-trip LIST_NPC.STB byte-for-byte")
        return 1

    if args.restore:
        if not trend_exclude.roles_applied():
            print("nothing to restore (no manifest)")
            return 0
        skipped = 0
        for key, (old, new) in json.load(open(MANIFEST))["cells"].items():
            r, c = (int(v) for v in key.split(","))
            if stb.get(r, c).strip() != new.encode("latin-1"):
                print(f"   skip {key}: {stb.get(r, c).strip()!r} was edited since")
                skipped += 1
                continue
            stb.set(r, c, old.encode("latin-1"))
        open(NPC_STB, "wb").write(stb.to_bytes())
        os.remove(MANIFEST)
        print(f"restored LIST_NPC.STB ({skipped} cells edited since were left alone)")
        return 0

    for r, name in (list(CLAN_BOSSES.items()) + list(CLAN_DUO.items())
                    + [(r, n) for r, (n, _m) in BOSS_HP.items()] + list(NOSKILL_BOSSES.items())):
        got = stb.get(r, 0).strip().decode("latin-1")
        if not got.startswith(name):
            print(f"ABORT: LIST_NPC {r} is {got!r}, expected {name!r}")
            return 1

    gd = GameData()
    spawned = sweep.spawned_monsters(gd)
    base = trend_exclude.without_roles(oro.Stb(NPC_STB))
    todo, factor, mark, level = plan(base, spawned)
    cur = {k: num(stb.get(*k)) for k in todo}
    pending = {k: v for k, v in todo.items() if cur[k] != v}

    print(f"{len(todo)} cells in the plan over "
          f"{len({r for r, _ in todo})} monsters; {len(pending)} differ from the table")
    for m in TARGET:
        print(f"   {sweep.MARKS.get(m, m)!s:<24}" + " ".join(
            f"{k} x{factor[m][k]:.2f}" for k in ORDER))
    print(f"   clan bosses: HP x{CLAN_HP}, ATK x{CLAN_ATK} ({', '.join(CLAN_BOSSES.values())})")
    print(f"   kings and skulls: HP x{min(m for _n, m in BOSS_HP.values())}"
          f"..x{max(m for _n, m in BOSS_HP.values())} ({len(BOSS_HP)} bosses)")
    print(f"   Luna clan-field regulars (two players): HP x{DUO_HP}, ATK x{DUO_ATK} on their role, "
          f"rows {sorted(CLAN_DUO)}")

    if args.verify:
        if pending or (todo and not trend_exclude.roles_applied()):
            print(f"VERIFY FAILED: {len(pending)} cell(s) differ from the plan")
            return 1
        print("ALL CHECKS PASSED -- the role layer is as planned.")
        return 0
    if not pending:
        print("nothing to do.")
        return 0
    if args.dry_run:
        print("dry run: nothing written")
        return 0
    man = json.load(open(MANIFEST)) if trend_exclude.roles_applied() else {"cells": {}}
    for (r, c), v in todo.items():
        key = f"{r},{c}"
        old = man["cells"].get(key, [base.get(r, c).strip().decode("latin-1")])[0]
        man["cells"][key] = [old, str(v)]
        stb.set(r, c, str(v).encode("latin-1"))
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    json.dump(man, open(MANIFEST, "w"), indent=1)
    open(NPC_STB, "wb").write(stb.to_bytes())
    print(f"wrote {len(pending)} cell(s) (undo: --restore, manifest {MANIFEST})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
