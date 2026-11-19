"""Drops below Oro: no empty tables, map rows, the uniques' home (roadmap 1.2, 2026-11-19).

`economy-audit.py drops` found the band under the 200+ endgame in three ways short of the
philosophy the rest of the game follows (doc/project-drops.md section 7):

  * **Spawned monsters with an empty table** pay nothing but money or the zone fallback.
    The user's rule: "monsters shouldn't have an empty table but something adequate to
    their level band". Found: the Lunar Keepers (Luna, lv122-131, no table at all), the
    Shady Jungle's Tiger Riders (lv168-170) and Turak (lv180, gold star), the Forest of
    Wandering's Destroyers, Killers, Elite Slaughterer and Cursed Ant Vagabond (lv187-200),
    and the whole Sikuku Prison (lv200-208 and Executor Kera, lv240).
  * **Zone rows 63-65 are monster tables**, the Sikuku family's, shared by twelve Forest of
    Wandering monsters. The double drop (roadmap 1.3) rolls a zone's row on every kill,
    so gear on those rows would double the gear rate across the three zones.
  * **The 668 uniques (LIST_WEAPON 904-995) drop nowhere.** The user: they belong to the
    lv180-210 band ("a weapon for ~level 180 that carries to ~210"), from bosses and also
    from ordinary monsters -- "we don't want to be always previsible. A little fun factor /
    exploration / farming reward".

What this pass writes, in order:

1. **Relocate** the tables on rows 63/64/65 to RELOCATE's free rows (their users follow),
   then write each of those zone rows as a **map table**: the materials and consumables of
   the zone's own monster tables, most frequent first, no gear (add-karkia-drops.map_table).
   The Prison (65) has no materials of its own; its row takes the Marsh of Ghosts'.
2. **The Sikuku Prison** (the user: "fairly simple, mostly a few monsters and Kera"):
   **Executor Kera gets a boss table** (add-karkia-drops.boss_table, ~90% gear): six
   uniques on top and the other nine in the two armour buckets (~45% a unique a kill),
   below them the vanilla lv200 weapons and armour (WPN_200 / ARM_200), which had no
   source anywhere; **every other prison monster carries the Prison's map table** ("for
   now"), the Skeleton included (it had no table).
3. **Every other empty table is a copy of its band**: the table of the nearest-level
   monster in the same zone that has one (the peer is printed), cell for cell. A gold star
   (Turak) then has three of its peer's gear items in the top slots: "pay better than the
   rest but not up to boss level". The Lunar Keepers had no table row; they get KEEPER_ROWS.
4. **Eldeon's empty buckets** (2026-11-19, the user's call after the reference check):
   Xita Refuge and the Shady Jungle redirect most rolls into buckets that retail never
   filled -- ruff, 139, Evo 137, Jrose and RoseZA leave the same groups empty, so nothing
   was lost and nothing can be restored. Each empty bucket a table in zones 61-65 reaches
   gets five pieces of gear from the same zone's live buckets, the levels nearest the
   zone's own gear-to-monster offset (from monsters within 10 levels when the zone pays no
   gear at all). Measured under the corrected redirect base (kRedirectBaseColumn 25).
5. **Uniques in the lv180-210 band** (zones 62-64, monsters of level 180-210): each table a
   monster there uses gets a uniques bucket in a free redirect group, reached from its last
   empty common slot (or its last duplicated material slot) -- one unique in the five cells
   for an ordinary monster (about 1 kill in 300-500), two for a bronze or silver star, all
   five for a gold star, king or skull (the Moss Golem). The uniques rotate across the
   tables, so which one drops where is not the same everywhere.

Left alone on purpose: ambient critters (butterflies, fish, the Mini Bee, level <= 13),
quest objects (Rune Stone, Seal Stone: icon 0), the Sunshine Coast's seasonal event
monsters (zone 37), Mount Eruca (58, unreachable) and Skaaj (89: "an empty planet for
now", the user).

`--dry-run` / `--verify` / `--restore`. The plan is computed from the cells this pass found
(the manifest's originals), so a re-run writes nothing; `--restore` puts back every cell
that still holds what it wrote. Manifest: build/drops-below-oro/manifest.json. Restart the
servers and re-bake (the Monster Inspector reads ITEM_DROP).
"""
import argparse
import collections
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)


def _load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [file]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = saved
    return mod


kd = _load("add_karkia_drops", "add-karkia-drops.py")
audit = _load("economy_audit", "economy-audit.py")
oro = audit.asn.oro

STB = os.path.join(REPO, "data", "3DDATA", "STB")
DROP_STB = os.path.join(STB, "ITEM_DROP.STB")
NPC_STB = os.path.join(STB, "LIST_NPC.STB")
MANIFEST = os.path.join(REPO, "build", "drops-below-oro", "manifest.json")

COL_LEVEL, COL_TABLE, COL_MARK = 7, 18, 27
SLOTS = kd.LAST_SLOT + 1                  # 0..49; slot s is column 1 + s
T_WEAPON, T_NATURAL, T_USE = 8, 12, 10
EQUIP = {1, 2, 3, 4, 5, 6, 8, 9, 14}

# 668's uniques (import-668-uniques.py), stat-gated, no level requirement
UNIQUES = [904, 911, 918, 922, 929, 935, 936, 943, 955, 962, 970, 979, 986, 994, 995]
# vanilla lv200 gear, unobtainable until now: weapons one per type, armour per class line
WPN_200 = [25, 54, 76, 123, 152, 184, 224, 253, 281, 324, 352, 423, 450]
ARM_200 = [(t, n) for n in (43, 73, 103, 133) for t in (3, 2, 4, 5)]

RELOCATE = {63: 921, 64: 922, 65: 923}    # Sikuku tables off the zone rows
KEEPER_ROWS = {384: 924, 385: 925, 386: 926}
PRISON, MARSH = 65, 64
KERA, SKELETON, PRISON_SHARED = 1719, 1720, 691
UNIQUE_ZONES, UNIQUE_BAND = {62, 63, 64}, (180, 210)
STARS, GOLD_UP = {8, 9}, {10, 11, 16}
BOSSY = {10, 11, 12, 13, 16}               # gold star, king, special, skull
EXEMPT_ZONES = {37, 58, 89}
DEAD_BUCKET_ZONES = {61, 62, 63, 64, 65}   # Eldeon (the user: "fill eldeon empty bucket")
CRITTER_MAX_LEVEL = 13


def num(b):
    s = b.strip()
    return int(s) if s.lstrip(b"-").isdigit() else 0


def load_manifest():
    if os.path.isfile(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as fh:
            return json.load(fh)
    return {"drop": {}, "npc": {}}


def original(stb, cells):
    for key, (old, new) in cells.items():
        r, c = (int(x) for x in key.split(","))
        if stb.get(r, c).strip() == new.encode("latin-1"):
            stb.set(r, c, old.encode("latin-1"))
    return stb


def row_cells(drop, row):
    """{slot: packed} of a row (non-zero only)."""
    return {s: num(drop.get(row, 1 + s)) for s in range(SLOTS) if num(drop.get(row, 1 + s))}


def entries(cells):
    """(type, no) of every item in a row, common section and buckets alike."""
    out = []
    for s in sorted(cells):
        d = audit.decode(cells[s])
        if d:
            out.append(d)
    return out


def map_cells(rows):
    """A map table from the materials and consumables of `rows` (cell dicts)."""
    mats, uses = collections.Counter(), collections.Counter()
    for cells in rows:
        for t, n in entries(cells):
            if t == T_NATURAL:
                mats[n] += 1
            elif t == T_USE:
                uses[n] += 1
    return kd.build_row(*kd.map_table([m for m, _ in mats.most_common()],
                                      [u for u, _ in uses.most_common()] or kd.USE_FIELD))


def add_uniques(cells, ids):
    """Put `ids` in a free redirect group of `cells` (in place); the slot that reaches it."""
    common = {s: cells.get(s, 0) for s in range(kd.COMMON_SLOTS)}
    reached = {v for v in common.values() if 1 <= v <= 4}

    def group_slots(g):
        return [kd.REDIRECT_BASE_COL + g * kd.REDIRECT_WIDTH + k for k in range(kd.REDIRECT_WIDTH)
                if kd.REDIRECT_BASE_COL + g * kd.REDIRECT_WIDTH + k <= kd.LAST_SLOT]

    order = [3, 2, 1, 4]
    free = [g for g in order if g not in reached]
    empty = [g for g in free if not any(cells.get(s) for s in group_slots(g))]
    if not free:
        # every group is in use (the Shady Jungle's retail tables): the uniques share a
        # bucket, taking a repeated cell where there is one, else the last cells -- still
        # a fifth of that bucket's slot per unique
        for g in sorted(reached, reverse=True):
            gs = group_slots(g)
            vals = collections.Counter(cells.get(s, 0) for s in gs)
            dup = [s for s in gs if vals[cells.get(s, 0)] > 1 or not cells.get(s)]
            take = (dup + [s for s in reversed(gs) if s not in dup])[:len(ids)]
            for s, u in zip(take, ids):
                cells[s] = kd.encode(T_WEAPON, u)
            return None, g
    g = (empty or free)[0]                 # an unreached group's cells are dead content
    slot = next((s for s in range(kd.COMMON_SLOTS - 1, -1, -1) if not common[s]), None)
    if slot is None:
        seen = collections.Counter(v for v in common.values() if v > 4)
        slot = next((s for s in range(kd.COMMON_SLOTS - 1, -1, -1)
                     if common[s] > 4 and seen[common[s]] > 1), None)
    if slot is None:
        raise SystemExit("no slot to reach the uniques from")
    for k, s in enumerate(group_slots(g)):
        if k < len(ids):
            cells[s] = kd.encode(T_WEAPON, ids[k])
        else:
            cells.pop(s, None)
    cells[slot] = g
    return slot, g


class Planner:
    def __init__(self, drop, npc):
        self.drop, self.npc = drop, npc
        self.spawns = audit.zone_spawns()
        self.zones_of = collections.defaultdict(set)
        for z, _n, c in self.spawns:
            for r in c:
                self.zones_of[r].add(z)
        self.rows = {}            # ITEM_DROP row -> (label, {slot: packed})
        self.npc_set = {}         # (npc row, col) -> value
        self.notes = []

    def name(self, r):
        return self.npc.get(r, 0).strip().decode("latin-1", "replace")

    def table(self, r):
        t = self.npc_set.get((r, COL_TABLE), num(self.npc.get(r, COL_TABLE)))
        return t

    def cells_of(self, t):
        return self.rows[t][1] if t in self.rows else row_cells(self.drop, t)

    def mobs(self, zone):
        return [r for z, _n, c in self.spawns if z == zone for r in c
                if self.npc.get(r, 0).strip() and num(self.npc.get(r, 9)) > 0]

    # 1. the Sikuku tables leave the zone rows; the zone rows become map tables
    def relocate(self):
        for z, new in RELOCATE.items():
            if row_cells(self.drop, new):
                raise SystemExit(f"ITEM_DROP row {new} is not free")
            self.rows[new] = (f"Sikuku table (was row {z})", row_cells(self.drop, z))
            users = [r for r in range(1, self.npc.rows)
                     if self.npc.get(r, 0).strip() and num(self.npc.get(r, COL_TABLE)) == z]
            for r in users:
                self.npc_set[(r, COL_TABLE)] = new
            self.notes.append(f"row {z} -> {new}, {len(users)} user(s)")

    def zone_maps(self):
        for z in (63, 64):
            tabs = {self.table(r) for r in self.mobs(z)} - {0}
            self.rows[z] = (f"zone {z} map table", map_cells([self.cells_of(t) for t in tabs]))
        marsh = {self.table(r) for r in self.mobs(MARSH)} - {0}
        self.prison_map = map_cells([self.cells_of(t) for t in marsh])
        self.rows[PRISON] = ("zone 65 map table (the Marsh's materials)", dict(self.prison_map))

    # 2. the Prison
    def prison(self):
        mats = [n for t, n in entries(self.prison_map) if t == T_NATURAL]
        lower = []
        for k in range(max(len(WPN_200), len(ARM_200))):
            if k < len(WPN_200):
                lower.append((T_WEAPON, WPN_200[k]))
            if k < len(ARM_200):
                lower.append(ARM_200[k])
        common, groups = kd.boss_table(
            kd.W(UNIQUES[:6]), [kd.W(UNIQUES[6:11]), kd.W(UNIQUES[11:15])], lower,
            list(dict.fromkeys(mats))[:4], kd.USE_BOSS[0],
            kd.J(kd.win(kd.JEWEL_ORO, 0, 5)), kd.G(kd.win(kd.GEM_5, 0, 4)))
        kt = self.table(KERA)
        self.rows[kt] = ("BOSS Executor Kera (Prison): uniques, vanilla lv200 gear",
                         kd.build_row(common, groups))
        for r in self.mobs(PRISON):
            if r == KERA:
                continue
            t = self.table(r)
            if t == 0:
                self.npc_set[(r, COL_TABLE)] = t = PRISON_SHARED
            self.rows[t] = ("Sikuku Prison monster (map table)", dict(self.prison_map))

    # 3. every other empty table: a copy of its band
    def empty_targets(self):
        out = []
        for r, zones in self.zones_of.items():
            if r in (KERA,) or PRISON in zones or zones & EXEMPT_ZONES:
                continue
            if not self.npc.get(r, 0).strip() or num(self.npc.get(r, 9)) <= 0:
                continue
            if num(self.npc.get(r, COL_LEVEL)) <= CRITTER_MAX_LEVEL or num(self.npc.get(r, COL_MARK)) == 0:
                continue
            t = self.table(r)
            if t == 0 or not self.cells_of(t):     # planned rows count (the relocations)
                out.append(r)
        return sorted(out)

    def peer(self, r, exclude):
        """Nearest level in the same zones, same class (boss-like or not); a boss-like
        monster with no boss-like neighbour takes the nearest one of any class."""
        try:
            return self._peer(r, exclude, True)
        except SystemExit:
            if num(self.npc.get(r, COL_MARK)) not in BOSSY:
                raise
            return self._peer(r, exclude, False)

    def _peer(self, r, exclude, same_class):
        lv = num(self.npc.get(r, COL_LEVEL))
        boss = num(self.npc.get(r, COL_MARK)) in BOSSY
        best = None
        for z in self.zones_of[r]:
            for p in self.mobs(z):
                t = self.table(p)
                if p in exclude or t == 0 or not self.cells_of(t):
                    continue
                if same_class and (num(self.npc.get(p, COL_MARK)) in BOSSY) != boss:
                    continue                       # a king's table is no peer for a trash mob
                key = (abs(num(self.npc.get(p, COL_LEVEL)) - lv), p)
                if best is None or key < best[0]:
                    best = (key, p, t)
        if best is None:
            raise SystemExit(f"{r} {self.name(r)}: no peer with a table in its zones")
        return best[1], best[2]

    def gear_near(self, r):
        """Gear for a gold star whose zone pays none (the Shady Jungle's retail tables point
        their buckets at empty columns): the gear of the nearest-level spawned monster
        anywhere that has some."""
        lv = num(self.npc.get(r, COL_LEVEL))
        for p in sorted(self.zones_of, key=lambda p: (abs(num(self.npc.get(p, COL_LEVEL)) - lv), p)):
            t = self.table(p)
            gear = [e for e in entries(self.cells_of(t)) if e[0] in EQUIP] if t else []
            if gear and num(self.npc.get(p, COL_MARK)) not in BOSSY:
                self.notes.append(f"  {r} {self.name(r)}: gear from table {t} ({self.name(p)[:24]})")
                return gear
        return []

    def fill_empty(self):
        targets = self.empty_targets()
        for r in targets:
            p, pt = self.peer(r, set(targets))
            t = self.table(r)
            if t == 0:
                t = KEEPER_ROWS.get(r)
                if t is None:
                    raise SystemExit(f"{r} {self.name(r)} has no table row and none is planned")
                if row_cells(self.drop, t):
                    raise SystemExit(f"ITEM_DROP row {t} is not free")
                self.npc_set[(r, COL_TABLE)] = t
            cells = dict(self.cells_of(pt))
            label = f"copy of {pt} ({self.name(p)[:20]}) for {self.name(r)[:24]}"
            if num(self.npc.get(r, COL_MARK)) == 10:     # gold star: gear on top
                gear = [e for e in entries(cells) if e[0] in EQUIP] or self.gear_near(r)
                for s, e in zip(range(3), gear):
                    cells[s] = kd.encode(*e)
                label = "ELITE " + label
            self.rows[t] = (label, cells)
            self.notes.append(f"{r} {self.name(r)} lv{num(self.npc.get(r, COL_LEVEL))} "
                              f"-> table {t}: {label}")

    # 4. Eldeon's empty buckets
    def group_cells(self, cells, g):
        return [kd.REDIRECT_BASE_COL + g * kd.REDIRECT_WIDTH + k for k in range(kd.REDIRECT_WIDTH)
                if kd.REDIRECT_BASE_COL + g * kd.REDIRECT_WIDTH + k <= kd.LAST_SLOT]

    def live_gear(self, t):
        """(type, no) of the gear in the redirect groups a table really reaches."""
        cells = self.cells_of(t)
        reached = {cells.get(s) for s in range(kd.COMMON_SLOTS) if 1 <= cells.get(s, 0) <= 4}
        out = []
        for g in reached:
            for s in self.group_cells(cells, g):
                d = audit.decode(cells.get(s, 0))
                if d and d[0] in EQUIP and self.items.exists(*d) and self.items.level(*d) > 0:
                    out.append(d)
        return out

    def fill_dead_buckets(self):
        self.items = audit.Items()
        users = collections.defaultdict(list)          # table -> [(level, zone)]
        for z in DEAD_BUCKET_ZONES:
            for r in self.mobs(z):
                t = self.table(r)
                if t:
                    users[t].append((num(self.npc.get(r, COL_LEVEL)), z))
        # each zone's live gear and where it sits against its monsters' levels
        pool, offsets = collections.defaultdict(set), collections.defaultdict(list)
        for z, *_ in self.spawns:
            for r in self.mobs(z):
                t = self.table(r)
                for e in (self.live_gear(t) if t else []):
                    pool[z].add(e)
                    offsets[z].append(self.items.level(*e) - num(self.npc.get(r, COL_LEVEL)))
        for k, t in enumerate(sorted(users)):
            cells = dict(self.cells_of(t))
            reached = sorted({cells.get(s) for s in range(kd.COMMON_SLOTS) if 1 <= cells.get(s, 0) <= 4})
            dead = [g for g in reached if not any(cells.get(s) for s in self.group_cells(cells, g))]
            if not dead:
                continue
            lv = min(l for l, _ in users[t])
            zones = {z for _, z in users[t]}
            cand = set().union(*(pool[z] for z in zones))
            offs = sorted(o for z in zones for o in offsets[z])
            if not cand:                               # the zone pays no gear at all
                near = [z for z, *_ in self.spawns if any(
                    abs(num(self.npc.get(r, COL_LEVEL)) - lv) <= 10 for r in self.mobs(z))]
                cand = set().union(*(pool[z] for z in near))
                offs = sorted(o for z in near for o in offsets[z])
            if not cand:
                self.notes.append(f"table {t}: dead groups {dead}, no gear anywhere near lv{lv}")
                continue
            want = lv + (offs[len(offs) // 2] if offs else -10)
            ranked = sorted(cand, key=lambda e: (abs(self.items.level(*e) - want), e))[:15]
            for j, g in enumerate(dead):
                pick = [ranked[(5 * (j + k) + i) % len(ranked)] for i in range(kd.REDIRECT_WIDTH)]
                for s, e in zip(self.group_cells(cells, g), pick):
                    cells[s] = kd.encode(*e)
            label = self.rows[t][0] if t in self.rows else self.drop.get(t, 0).decode("latin-1").strip()
            self.rows[t] = (label, cells)
            lvls = sorted({self.items.level(*e) for e in ranked})
            self.notes.append(f"table {t} (lv{lv}): {len(dead)} empty bucket(s) {dead} filled with "
                              f"gear lv{lvls[0]}-{lvls[-1]} (aim lv{want})")

    # 5. the uniques
    def uniques(self):
        want = {}
        for z in UNIQUE_ZONES:
            for r in self.mobs(z):
                lv = num(self.npc.get(r, COL_LEVEL))
                if not UNIQUE_BAND[0] <= lv <= UNIQUE_BAND[1] or r == KERA:
                    continue
                m = num(self.npc.get(r, COL_MARK))
                n = 5 if m in GOLD_UP else 2 if m in STARS else 1
                t = self.table(r)
                if t:
                    want[t] = max(want.get(t, 0), n)
        for k, t in enumerate(sorted(want)):
            label, cells = self.rows.get(t, (self.drop.get(t, 0).decode("latin-1").strip(),
                                             row_cells(self.drop, t)))
            cells = dict(cells)
            ids = [UNIQUES[(3 * k + j) % len(UNIQUES)] for j in range(want[t])]
            slot, g = add_uniques(cells, ids)
            self.rows[t] = (label, cells)
            self.notes.append(f"table {t}: {len(ids)} unique(s) in group {g}, from slot {slot}")

    def plan(self):
        self.relocate()
        self.zone_maps()
        self.prison()
        self.fill_empty()
        self.fill_dead_buckets()
        self.uniques()
        return self.rows, self.npc_set


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    man = load_manifest()
    drop, npc = oro.Stb(DROP_STB), oro.Stb(NPC_STB)
    if args.restore:
        if not man["drop"] and not man["npc"]:
            print("nothing to restore (no manifest)")
            return 0
        skipped = 0
        for stb, cells in ((drop, man["drop"]), (npc, man["npc"])):
            for key, (old, new) in cells.items():
                r, c = (int(x) for x in key.split(","))
                if stb.get(r, c).strip() != new.encode("latin-1"):
                    skipped += 1
                    continue
                stb.set(r, c, old.encode("latin-1"))
        open(DROP_STB, "wb").write(drop.to_bytes())
        open(NPC_STB, "wb").write(npc.to_bytes())
        os.remove(MANIFEST)
        print(f"restored ITEM_DROP / LIST_NPC ({skipped} cell(s) edited since were left alone)")
        return 0

    base_drop = original(oro.Stb(DROP_STB), man["drop"])
    base_npc = original(oro.Stb(NPC_STB), man["npc"])
    pl = Planner(base_drop, base_npc)
    rows, npc_set = pl.plan()

    want = {}                                   # (row, col) -> str value
    for t, (label, cells) in rows.items():
        want[(t, 0)] = label[:60]
        for s in range(SLOTS):
            want[(t, 1 + s)] = str(cells[s]) if cells.get(s) else ""
    want_npc = {k: str(v) for k, v in npc_set.items()}
    pend = {k: v for k, v in want.items() if drop.get(*k).strip().decode("latin-1") != v}
    pend_npc = {k: v for k, v in want_npc.items() if npc.get(*k).strip().decode("latin-1") != v}

    for n in pl.notes:
        print("  " + n)
    print(f"{len(rows)} ITEM_DROP rows, {len(npc_set)} LIST_NPC cells planned; "
          f"{len(pend) + len(pend_npc)} cell(s) differ")
    if args.verify:
        if pend or pend_npc:
            print("VERIFY FAILED")
            return 1
        print("ALL CHECKS PASSED")
        return 0
    if not pend and not pend_npc:
        print("nothing to do.")
        return 0
    if args.dry_run:
        print("dry run: nothing written")
        return 0
    for stb, pending, rec, src in ((drop, pend, man["drop"], base_drop),
                                   (npc, pend_npc, man["npc"], base_npc)):
        for (r, c), v in pending.items():
            key = f"{r},{c}"
            old = rec.get(key, [src.get(r, c).strip().decode("latin-1")])[0]
            rec[key] = [old, v]
            stb.set(r, c, v.encode("latin-1"))
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8") as fh:
        json.dump(man, fh, indent=1)
    open(DROP_STB, "wb").write(drop.to_bytes())
    open(NPC_STB, "wb").write(npc.to_bytes())
    print(f"written (undo: --restore, manifest {os.path.relpath(MANIFEST, REPO)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
