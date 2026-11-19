"""Re-level the 200+ band: Oro up to ~230, Karkia from 230 (roadmap milestone 1.1, 2026-11-19).

The user's target for the endgame (the roadmap's "200+ economy"): **Oro serves ~205-230**
(it is the bigger continent) and **Karkia 230-240**; Oro's bosses may run over the band.
What the spawn data said on 2026-11-19 (`economy-audit.py bands --min-level 200`):

    Golden Ring        205-222 plain, kings 222-226        -> unchanged
    Wasteland (+Ruins) 222-234, kings/skulls 230-234       -> 217-226
    Fossil Sanctuary   230-240, kings/skull 237-240        -> 224-230
    Desolate Cemetery  215-228, Deadly Drake 238           -> 230-235, Drake 239
    Burned Forest      220-225 (Cemetery rows)             -> 232-234
    Spire Village      228-240                             -> 234-240
    Memories / Garden  230-240 (Karkia flashbacks)         -> 235-240 (the Spire's map)
    Catacombs          235-240                             -> unchanged (already the target)

So two zones were on the wrong side of 230: Karkia's Cemetery sat inside Oro's band and
Oro's Fossil Sanctuary inside Karkia's. Each zone group gets one linear map (MAPS below),
which keeps the order of its monsters and their spread, only narrower.

--- what moves with a level, and how

**Stats follow the level trend, so every monster keeps its role** (the user's condition:
"keep the monster icon identity philosophy"). The role layer (rebalance-monster-roles.py)
states each icon as a multiple of the level trend; scaling a monster's every stat by
trend(new level) / trend(old level) keeps every one of those multiples, and its quirks.
The trend is a log-linear fit per stat over the spawned plain monsters (icons 1-7) of
levels FIT_LO-FIT_HI, read *without* the role layer and without this pass. Max HP is level
x the HP column, so the column moves by the trend ratio times old/new level. The slopes
are **frozen** in SLOPE as fitted on 2026-11-19 (141 monsters): a plan computed from a live
fit moved as soon as balance-trend-exclude.py changed (it did, the same day). The fit still
runs and its drift from SLOPE is printed, for information only.

**EXP keeps each monster's kills per level.** The EXP pass (rebalance-exp-rewards.py) pays
`0.2883 x (level + 3) x EXP` per kill against `need(level)`; the column is rescaled by
need(new)/(new+3) over need(old)/(old+3), so a monster is exactly as many kills of a level
at its new level as it was at its old one. That pass's own --verify will now report these
rows: it is one of the passes the user considers outdated.

**The level itself moves the rest, with no data change**: who can hit it (Get_SuccessRATE's
gate), who gets its drops (nobody 10+ levels above it), its money drop, its name colour.
The drop audiences are milestone 1.2's business: `--dry-run` prints the drop tables each
group uses and where their audience moves. Known consequences on 2026-11-19: the Fossil
Sanctuary's lv240 armour buckets now drop off lv224-230 monsters, and the Cemetery's Key of
Remorse tables (900-902) now reach level-240 players too.

**Summons and morphs come along.** A monster an AI file of a moved monster summons or turns
into (Summon, SummonMaster*, Change_CHAR, Create_PET) moves with its group, unless a map
spawns it somewhere this pass does not touch. A row that two groups would move differently
stops the pass.

--- order (the role layer is a layer on top of every balance pass)

    python scripts/rebalance-monster-roles.py --restore
    python scripts/relevel-200-band.py [--dry-run]
    python scripts/rebalance-monster-roles.py
    python scripts/rebalance-monster-roles.py --verify
    python scripts/relevel-200-band.py --verify

It refuses to write, verify or restore while the role layer is applied (a --dry-run reads
through it) (the same rule as
the older passes). Idempotent: the plan is computed from the values recorded in its
manifest (build/relevel-200-band/manifest.json, [original, written] per cell), so a re-run
writes nothing. `--restore` puts back the cells that still hold what this pass wrote.

The server and the client both read LIST_NPC: restart the servers and re-bake. `data/` is
gitignored, so this file is the record.
"""
import argparse
import importlib.util
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)


def _load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


trend_exclude = _load("balance_trend_exclude", "balance-trend-exclude.py")
audit = _load("economy_audit", "economy-audit.py")
exp_pass = _load("rebalance_exp_rewards", "rebalance-exp-rewards.py")
asn = audit.asn
oro = asn.oro

NPC_STB = os.path.join(REPO, "data", "3DDATA", "STB", "LIST_NPC.STB")
MANIFEST = os.path.join(REPO, "build", "relevel-200-band", "manifest.json")

COL_LEVEL, COL_EXP, COL_DROP, COL_MARK = 7, 17, 18, 27
STATS = dict(hp=8, atk=9, hit=10, df=11, res=12, avoid=13, aspd=14)
PLAIN = set(range(1, 8))
FIT_LO, FIT_HI = 150, 240
# ln(stat) per level, fitted 2026-11-19 over 141 spawned plain monsters, levels 150-240
# (about +1.10%/level for max HP, +0.82% ATK, +0.47% HIT, +0.64% DEF, +0.56% RES,
# +0.66% AVOID, +0.01% attack speed).
SLOPE = dict(hp=0.010949703864494825, atk=0.008187783571404024, hit=0.004712421356435033,
             df=0.006350130875782599, res=0.0055657800388862295, avoid=0.006608847927763603,
             aspd=0.00014826180464894788)
MAX_LEVEL = 240

# group: (zone ids, (old_lo, old_hi), (new_lo, new_hi)); new = linear map, rounded,
# clamped to MAX_LEVEL. Monsters outside old_lo..old_hi (bosses) follow the same line.
MAPS = {
    "Wasteland": ({78, 79, 81}, (222, 234), (217, 226)),
    "Fossil Sanctuary": ({85}, (230, 240), (224, 230)),
    "Cemetery": ({87, 131}, (215, 228), (230, 235)),
    "Spire and flashbacks": ({88, 133, 144}, (228, 240), (234, 240)),
}
# Zones whose spawns stay where they are; a row they spawn is not moved by summon closure.
SUMMON_ACTS = {"Change_CHAR", "Create_PET", "Summon", "SummonMaster", "SummonMasterDist"}


def num(b):
    s = b.strip()
    return int(s) if s.lstrip(b"-").isdigit() else 0


def new_level(group, old):
    _z, (a, b), (c, d) = MAPS[group]
    return min(MAX_LEVEL, int(math.floor(c + (old - a) * (d - c) / (b - a) + 0.5)))


def load_manifest():
    if os.path.isfile(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as fh:
            return json.load(fh)
    return {"cells": {}}


def original(stb, man):
    """The table as it was before this pass (in place): every cell still holding what
    the pass wrote goes back to its recorded original."""
    for key, (old, new) in man["cells"].items():
        r, c = (int(x) for x in key.split(","))
        if stb.get(r, c).strip() == new.encode("latin-1"):
            stb.set(r, c, old.encode("latin-1"))
    return stb


def scope(base):
    """{row: group}, plus the zones and the problems found."""
    spawns = audit.zone_spawns()
    group_of_zone = {z: g for g, (zs, _o, _n) in MAPS.items() for z in zs}
    rows, elsewhere, problems = {}, set(), []
    for z, _name, count in spawns:
        g = group_of_zone.get(z)
        for r in count:
            if g is None:
                elsewhere.add(r)
            elif rows.setdefault(r, g) != g:
                problems.append(f"row {r} spawns in two groups ({rows[r]}, {g})")
    # A row a group spawns that also spawns outside every group (a Muris town-edge
    # scavenger) moves with its field, which is what a player meets it in.
    #
    # Summons: walk the closure from every spawned row, moved or not, and record who
    # reaches each target. A target moves only if exactly one group reaches it and no
    # unmoved monster does; the generic ones (Oro's Shadow Ghosts, the event pigs, the
    # Ghost Seeds) serve several bands and stay where they are.
    a = asn.Audit(asn.Files(asn.DATA))
    reach = {}
    todo = [(r, g) for r, g in rows.items()] + [(r, None) for r in elsewhere if r not in rows]
    seen = set()
    while todo:
        r, g = todo.pop()
        if (r, g) in seen:
            continue
        seen.add((r, g))
        path, _why = a.ai_file(r)
        if not path:
            continue
        for label, mob in asn.ai_monsters(path) or []:
            if label not in SUMMON_ACTS or mob <= 0 or not base.get(mob, 0).strip():
                continue
            if mob in rows or mob in elsewhere:
                continue                     # spawned on a map: placed by its own zone
            reach.setdefault(mob, set()).add(g)
            todo.append((mob, g))
    kept = {}
    for mob, gs in reach.items():
        if len(gs) == 1 and None not in gs:
            rows[mob] = next(iter(gs))
        else:
            kept[mob] = gs
    return rows, problems, kept


def fit_trend(base):
    """{stat: slope of ln(stat) per level} over the spawned plain monsters."""
    spawned = set()
    for _z, _n, count in audit.zone_spawns():
        spawned.update(count)
    pts = {k: [] for k in STATS}
    for r in spawned:
        lv = num(base.get(r, COL_LEVEL))
        if not (FIT_LO <= lv <= FIT_HI) or num(base.get(r, COL_MARK)) not in PLAIN:
            continue
        if trend_exclude.excluded(r) or num(base.get(r, STATS["atk"])) <= 0:
            continue
        for k, c in STATS.items():
            v = num(base.get(r, c))
            if k == "aspd" and v <= 0:
                v = 100
            if k == "hp":
                v *= lv
            if v > 0:
                pts[k].append((lv, math.log(v)))
    slope = {}
    for k, p in pts.items():
        n = len(p)
        mx = sum(x for x, _ in p) / n
        my = sum(y for _, y in p) / n
        slope[k] = (sum((x - mx) * (y - my) for x, y in p)
                    / sum((x - mx) ** 2 for x, _ in p))
    return slope, len(pts["atk"])


def exp_factor(old, new):
    need = exp_pass.need_raw_exp
    return (need(new) / (new + 3)) / (need(old) / (old + 3))


def plan(base, rows, slope):
    out, moves = {}, []
    for r, g in sorted(rows.items()):
        old = num(base.get(r, COL_LEVEL))
        if old <= 0:
            continue
        new = new_level(g, old)
        if new == old:
            continue
        moves.append((g, r, old, new))
        out[(r, COL_LEVEL)] = new
        for k, c in STATS.items():
            v = num(base.get(r, c))
            if v <= 0:
                continue
            f = math.exp(slope[k] * (new - old))
            if k == "hp":
                f *= old / new
            out[(r, c)] = max(1, int(math.floor(v * f + 0.5)))
        e = num(base.get(r, COL_EXP))
        if e > 1:       # 1 is the house "worth nothing" sentinel
            out[(r, COL_EXP)] = max(2, int(math.floor(e * exp_factor(old, new) + 0.5)))
    return out, moves


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    if trend_exclude.roles_applied() and not args.dry_run:
        print("ABORT: the monster role layer is applied. Run "
              "`rebalance-monster-roles.py --restore` first, then this, then the roles again.")
        return 1
    raw = open(NPC_STB, "rb").read()
    stb = oro.Stb(NPC_STB)
    if stb.to_bytes() != raw:
        print("ABORT: the STB writer does not round-trip LIST_NPC.STB byte-for-byte")
        return 1
    man = load_manifest()

    if args.restore:
        if not man["cells"]:
            print("nothing to restore (no manifest)")
            return 0
        skipped = 0
        for key, (old, new) in man["cells"].items():
            r, c = (int(x) for x in key.split(","))
            if stb.get(r, c).strip() != new.encode("latin-1"):
                print(f"   skip {key}: {stb.get(r, c).strip()!r} was edited since")
                skipped += 1
                continue
            stb.set(r, c, old.encode("latin-1"))
        open(NPC_STB, "wb").write(stb.to_bytes())
        os.remove(MANIFEST)
        print(f"restored LIST_NPC.STB ({skipped} cells edited since were left alone)")
        return 0

    if trend_exclude.roles_applied():          # a dry run may look through the layer
        stb = trend_exclude.without_roles(stb)
    # the layer comes off first: its cells hold role values, not what this pass wrote
    base = original(trend_exclude.without_roles(oro.Stb(NPC_STB)), man)
    rows, problems, kept = scope(base)
    if problems:
        for p in problems:
            print("ABORT:", p)
        return 1
    live, n = fit_trend(base)
    todo, moves = plan(base, rows, SLOPE)
    pending = {k: v for k, v in todo.items() if num(stb.get(*k)) != v}

    print(f"trend (frozen SLOPE, per level): "
          + ", ".join(f"{k} {100 * (math.exp(s) - 1):+.2f}%" for k, s in SLOPE.items()))
    print(f"   today's fit over {n} plain monsters, levels {FIT_LO}-{FIT_HI}, differs by: "
          + ", ".join(f"{k} {100 * (live[k] - SLOPE[k]):+.3f}" for k in SLOPE) + " pts")
    for g in MAPS:
        ms = [m for m in moves if m[0] == g]
        if not ms:
            continue
        print(f"\n== {g}  ({len(ms)} monsters)")
        for _g, r, old, new in sorted(ms, key=lambda m: (m[2], m[1])):
            name = base.get(r, 0).strip().decode("latin-1", "replace")
            mark = num(base.get(r, COL_MARK))
            hp_old = old * num(base.get(r, STATS["hp"]))
            hp_new = new * todo.get((r, STATS["hp"]), 0)
            atk = todo.get((r, STATS["atk"]), num(base.get(r, STATS["atk"])))
            exp = todo.get((r, COL_EXP), num(base.get(r, COL_EXP)))
            print(f"   {r:5}  lv {old:3} -> {new:3}  icon {mark:2}  maxHP {hp_old:7} -> {hp_new:7}"
                  f"  ATK {num(base.get(r, STATS['atk'])):5} -> {atk:5}"
                  f"  EXP {num(base.get(r, COL_EXP)):6} -> {exp:6}  drop {num(base.get(r, COL_DROP)):4}"
                  f"  {name[:28]}")
    shown = {m: gs for m, gs in kept.items() if gs - {None}}
    if shown:
        print("\nsummons left alone (shared, or also reached from unmoved monsters):")
        for m, gs in sorted(shown.items()):
            who = ", ".join(sorted(g or "unmoved monsters" for g in gs))
            name = base.get(m, 0).strip().decode("latin-1", "replace")
            print(f"   {m:5}  lv {num(base.get(m, COL_LEVEL)):3}  {name[:28]:28} reached from {who}")
    print(f"\n{len(todo)} cells over {len(moves)} monsters; {len(pending)} differ from the table")

    if args.verify:
        if pending:
            print(f"VERIFY FAILED: {len(pending)} cell(s) differ from the plan")
            return 1
        print("ALL CHECKS PASSED -- the 200+ band is re-levelled as planned.")
        return 0
    if not pending:
        print("nothing to do.")
        return 0
    if args.dry_run:
        print("dry run: nothing written")
        return 0
    for (r, c), v in todo.items():
        key = f"{r},{c}"
        old = man["cells"].get(key, [base.get(r, c).strip().decode("latin-1")])[0]
        man["cells"][key] = [old, str(v)]
        stb.set(r, c, str(v).encode("latin-1"))
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8") as fh:
        json.dump(man, fh, indent=1)
    open(NPC_STB, "wb").write(stb.to_bytes())
    print(f"wrote {len(pending)} cell(s) (undo: --restore, manifest {MANIFEST})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
