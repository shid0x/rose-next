"""The economy audit: the read-only numbers behind the 200+ economy work (2026-11-19).

Roadmap milestone 1 ("The 200+ economy": progression, drops, the double drop, recipes)
needs the same few views of the data before and after every change. This script prints
them and writes nothing. It is committed on purpose: the 2026-10-23 level audit's scratch
scripts were not, and had to be rewritten for this milestone.

    python scripts/economy-audit.py bands [--min-level 185] [--rows]

`bands`: every zone that spawns a monster at or above --min-level, with its level range,
the 10th/50th/90th percentile of its plain monsters (icons 1-7) weighted by REGEN count,
and the special monsters (stars, kings, skulls) one per line. --rows lists every spawned
row instead, grouped by the set of zones it spawns in -- a row shared by two zones moves
both when its level changes, which is the thing to check before any re-level.

    python scripts/economy-audit.py drops [--by band|zone] [--min-level N] [--max-level N]
                                         [--gap G] [--world-drop W]
    python scripts/economy-audit.py zone-rows [--min-level N]

`drops`: what a kill pays, as exact odds (not a simulation) of CCal::Get_DropITEM
(calculation.cpp) for every spawned monster at level gap --gap (player minus monster,
default 0 = parity) and world drop rate --world-drop (default 100), weighted by REGEN count
and summed by 10-level band (default) or by zone. Field monsters and bosses (gold star,
king, skull, special icons) are separate lines. The categories are the roadmap's:

    money       zuly (col 19 fires instead of an item)
    material    a LIST_NATURAL item some LIST_PRODUCT row uses, by id or by material
                class (col 1): crafting, refining and break-up all read that table
    junk        any other LIST_NATURAL item: sold, nothing else
    quest       LIST_QUESTITEM, or an item a QSD condition asks for
    consume     LIST_USEITEM (potions, food, scrolls, boxes, books)
    jewel       LIST_JEWEL (rings, necklaces, earrings)
    gem         LIST_JEMITEM
    equip       a wearable piece (armour, back, weapon, off-hand, cart part, mask) with a
                level requirement, or with none at all (starter gear)
    rare        a unique: a wearable piece gated on a stat and no level (the violet-name
                uniques, LIST_WEAPON 901-999 among them)
    dead        a cell naming a blank item row (the server zeroes those at load)
    nothing     no drop, an empty cell, or a redirect into an empty column

--roll monster / --roll map measure the double drop's two rolls (roadmap 1.3) one at a
time: the monster's own table always (no zone fallback), with its money roll; and the zone
row always, without money. Today's server makes one roll that is "monster" 80% of the time
and "map" 20% (col 20 = 80).

"eq lv" is the median level requirement of the level-gated equipment paid minus the
monster's level. Every monster species counts once per zone it spawns in (--weight regen
weights by REGEN count instead, which over-counts maps authored as carpets of points: the
Sikuku Prison alone would weigh as much as the rest of the 200+ band). In retail tables
nearly all gear sits behind the redirect buckets (cells 1-4 re-roll into columns 27-51),
so "in a bucket" says nothing about rarity; the bucket share is not reported.

`zone-rows`: the zone fallback row of every spawning zone (a drop-table id and a zone id
share one namespace, doc/project-drops.md section 2): blank, a mirror of a monster table,
also the own table of a monster spawned in another zone (a squatter; unspawned rows naming
it do not count), or a table of its own -- and what it pays.
The double drop (roadmap 1.3) rolls this row on every kill, so a squatter there would pay
its monster's loot all over the zone.

Scope comes from the map REGEN lumps (audit-spawned-npcs.py), never from LIST_NPC alone:
the table also holds quest actors, summons and unspawned duplicates.
"""
import argparse
import glob
import collections
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

_spec = importlib.util.spec_from_file_location(
    "audit_spawned_npcs", os.path.join(HERE, "audit-spawned-npcs.py"))
asn = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(asn)

from rosefmt import ifo, qsd  # noqa: E402

COL_LEVEL, COL_MARK = 7, 27
PLAIN = set(range(1, 8))
MARKS = {8: "bronze star", 9: "silver star", 10: "gold star", 11: "king", 12: "special",
         16: "skull"}


def npc_table():
    return asn.oro.Stb(asn.oro.rel_path(asn.DATA, asn.NPC_STB))


def zone_spawns():
    """[(zone id, zone name, {npc row: REGEN count})] for every zone that spawns anything."""
    zones = asn.zone_rows(asn.oro.Stb(asn.oro.rel_path(asn.DATA, asn.ZONE_STB)), set())
    out = []
    for zid, zname, folder in zones:
        path = asn.oro.rel_path(asn.DATA, folder)
        if not os.path.isdir(path):
            continue
        count = collections.Counter()
        for f in sorted(os.listdir(path)):
            if not f.lower().endswith(".ifo"):
                continue
            try:
                m = ifo.parse(open(os.path.join(path, f), "rb").read())
            except Exception:  # an unreadable chunk is audit-spawned-npcs.py's business
                continue
            for g in m.lump(ifo.REGEN) or []:
                for mob in list(g.basic) + list(g.tactic):
                    if mob.mob > 0 and mob.count > 0:
                        count[mob.mob] += mob.count
        if count:
            out.append((zid, zname, count))
    return out


def name(npc, r):
    return npc.get(r, 0).strip().decode("latin-1", "replace")


def cmd_bands(args):
    npc = npc_table()
    zones = [(z, n, c) for z, n, c in zone_spawns()
             if max(npc.i(r, COL_LEVEL) for r in c) >= args.min_level]
    if args.rows:
        where = collections.defaultdict(list)
        for z, n, c in zones:
            for r in c:
                where[r].append(f"{z} {n}")
        groups = collections.defaultdict(list)
        for r, zs in where.items():
            if npc.i(r, COL_LEVEL) >= args.min_level - 10:
                groups[" | ".join(sorted(set(zs)))].append(r)
        for key in sorted(groups, key=lambda k: min(npc.i(r, COL_LEVEL) for r in groups[k])):
            print(f"== {key}")
            for r in sorted(groups[key], key=lambda r: (npc.i(r, COL_LEVEL), r)):
                m = npc.i(r, COL_MARK)
                print(f"   {r:5}  lv {npc.i(r, COL_LEVEL):3}  icon {m:2} "
                      f"{MARKS.get(m, ''):12} {name(npc, r)}")
        return 0
    def plain_levels(c):
        return sorted(npc.i(r, COL_LEVEL) for r, k in c.items()
                      if npc.i(r, COL_MARK) in PLAIN for _ in range(k))

    def median(c):
        p = plain_levels(c)
        return p[len(p) // 2] if p else 0

    for z, n, c in sorted(zones, key=lambda t: (median(t[2]), t[0])):
        plain = plain_levels(c)
        lv = [npc.i(r, COL_LEVEL) for r in c]

        def pct(q):
            return plain[int(q * (len(plain) - 1))] if plain else 0
        print(f"zone {z:3} {n[:28]:28} lv {min(lv):3}-{max(lv):3}   plain p10/p50/p90 "
              f"{pct(.1)}/{pct(.5)}/{pct(.9)}")
        for r in sorted((r for r in c if npc.i(r, COL_MARK) not in PLAIN),
                        key=lambda r: npc.i(r, COL_LEVEL)):
            m = npc.i(r, COL_MARK)
            print(f"        lv {npc.i(r, COL_LEVEL):3}  {MARKS.get(m, f'icon {m}'):12} {name(npc, r)}")
    return 0


# ----------------------------------------------------------------- drops
TYPE_FILES = {1: "LIST_FACEITEM", 2: "LIST_CAP", 3: "LIST_BODY", 4: "LIST_ARMS",
              5: "LIST_FOOT", 6: "LIST_BACK", 7: "LIST_JEWEL", 8: "LIST_WEAPON",
              9: "LIST_SUBWPN", 10: "LIST_USEITEM", 11: "LIST_JEMITEM", 12: "LIST_NATURAL",
              13: "LIST_QUESTITEM", 14: "LIST_PAT"}
T_JEWEL, T_USE, T_GEM, T_NATURAL, T_QUEST = 7, 10, 11, 12, 13
EQUIP_TYPES = {1, 2, 3, 4, 5, 6, 8, 9, 14}
AT_LEVEL = 31
COL_DROP_TABLE, COL_DROP_MONEY, COL_DROP_ITEM = 18, 19, 20
BOSS_MARKS = {10, 11, 12, 13, 16}
CATS = ("nothing", "money", "material", "junk", "quest", "consume", "jewel", "gem",
        "equip", "rare", "dead")
WIDE_BASE, MAX_SENTINEL = 100000, 1000
REDIRECT_BASE, REDIRECT_WIDTH = 25, 5   # Rose::Drop::kRedirectBaseColumn since 2026-11-19 (was 26)


def stb(name):
    return asn.oro.Stb(os.path.join(asn.DATA, "3DDATA", "STB", name + ".STB"))


def decode(v):
    """(type, no) of a drop or product cell, or None (Rose::Drop::decode_drop_item)."""
    if v <= MAX_SENTINEL:
        return None
    return (v // WIDE_BASE, v % WIDE_BASE) if v >= WIDE_BASE else (v // 1000, v % 1000)


class Items:
    """What each item is: its table row, level requirement and natural class."""

    def __init__(self):
        self.t = {t: stb(f) for t, f in TYPE_FILES.items()
                  if os.path.isfile(os.path.join(asn.DATA, "3DDATA", "STB", f + ".STB"))}
        prod = stb("LIST_PRODUCT")
        self.mat_ids, self.mat_classes = set(), set()
        for r in range(1, prod.rows):
            if prod.i(r, 1):
                self.mat_classes.add(prod.i(r, 1))
            for k in range(4):
                d = decode(prod.i(r, 2 + 2 * k))
                if d:
                    self.mat_ids.add(d)
        self.quest_ids = set()
        for p in glob.glob(os.path.join(asn.DATA, "3DDATA", "QUESTDATA", "**", "*.QSD"),
                           recursive=True):
            try:
                refs = qsd.item_refs(p)
            except Exception:  # a QSD the reader cannot walk names nothing we count
                continue
            for _off, sn, kind in refs:
                if kind == "condition":
                    d = decode(sn)
                    if d:
                        self.quest_ids.add(d)

    def exists(self, t, n):
        tb = self.t.get(t)
        return bool(tb) and 0 < n < tb.rows and tb.occupied(n)

    def level(self, t, n):
        tb = self.t[t]
        for c in (19, 21):
            if tb.i(n, c) == AT_LEVEL:
                return tb.i(n, c + 1)
        return 0

    def unique(self, t, n):
        """No level requirement but a stat one (STR..SEN, ability types 10-15)."""
        tb = self.t[t]
        return self.level(t, n) == 0 and any(10 <= tb.i(n, c) <= 15 for c in (19, 21))

    def category(self, t, n, rare=False):
        if not self.exists(t, n):
            return "dead"
        if t == T_QUEST or (t, n) in self.quest_ids:
            return "quest"
        if t == T_NATURAL:
            used = (t, n) in self.mat_ids or self.t[t].i(n, 4) in self.mat_classes
            return "material" if used else "junk"
        if t == T_USE:
            return "consume"
        if t == T_JEWEL:
            return "jewel"
        if t == T_GEM:
            return "gem"
        if t in EQUIP_TYPES:
            return "rare" if self.unique(t, n) else "equip"
        return "dead"


def table_cells(drop, row):
    return [drop.i(row, c) for c in range(drop.cols)] if 0 < row < drop.rows else []


def kill_odds(items, cells_own, cells_zone, c19, c20, mob_level, gap=0, world=100,
              roll="today"):
    """({category: probability} for one kill, [(probability, equipment level offset)]).

    roll "today": Get_DropITEM as it stands (col 20 also splits own table / zone row).
    roll "monster" / "map": the two rolls of the double drop (roadmap 1.3) -- the monster's
    own table always, with its money roll; the zone row always, no money."""
    out = collections.Counter()
    eq = []
    p_money = min(max(c19, 0), 100) / 100
    p_own = min(max(c20, 0), 100) / 100
    if roll == "monster":
        p_own = 1.0
    elif roll == "map":
        p_own, p_money = 0.0, 0.0

    def cell(cells, col):
        return cells[col] if 0 <= col < len(cells) else 0

    def pay(v, w, rare):
        d = decode(v)
        if not d:
            out["nothing"] += w
            return
        c = items.category(d[0], d[1], rare)
        out[c] += w
        if c == "equip" and items.level(*d) > 0:
            eq.append((w, items.level(*d) - mob_level))

    for r in range(1, 101):
        dv = int((world + c20 - r - (gap + 16) * 3.5 - 10) * 0.38)
        if dv <= 0:
            out["nothing"] += 0.01
            continue
        out["money"] += 0.01 * p_money
        rest = 0.01 * (1 - p_money)
        n = 30 if dv > 30 else dv
        for cells, pt in ((cells_own, p_own), (cells_zone, 1 - p_own)):
            if pt <= 0:
                continue
            w = rest * pt / n
            for idx in range(n):
                v = cell(cells, 1 + idx)
                if 1 <= v <= 4:
                    for k in range(REDIRECT_WIDTH):
                        pay(cell(cells, 1 + REDIRECT_BASE + v * REDIRECT_WIDTH + k),
                            w / REDIRECT_WIDTH, True)
                else:
                    pay(v, w, False)
    return out, eq


def weighted_median(pairs):
    pairs = sorted(pairs, key=lambda p: p[1])
    tot = sum(w for w, _ in pairs)
    acc = 0
    for w, v in pairs:
        acc += w
        if acc >= tot / 2:
            return v
    return None


def cmd_drops(args):
    npc, drop, items = npc_table(), stb("ITEM_DROP"), Items()
    groups = collections.defaultdict(lambda: [collections.Counter(), [], 0.0, set()])
    for z, zname, count in zone_spawns():
        zone_cells = table_cells(drop, z)
        for r, k in count.items():
            k = k if args.weight == "regen" else 1
            lv = npc.i(r, COL_LEVEL)
            if not (args.min_level <= lv <= args.max_level) or npc.i(r, 9) <= 0:
                continue
            boss = npc.i(r, COL_MARK) in BOSS_MARKS
            odds, eq = kill_odds(items, table_cells(drop, npc.i(r, COL_DROP_TABLE)), zone_cells,
                                 npc.i(r, COL_DROP_MONEY), npc.i(r, COL_DROP_ITEM), lv,
                                 args.gap, args.world_drop, args.roll)
            key = (f"{lv // 10 * 10:3}-{lv // 10 * 10 + 9:3}" if args.by == "band"
                   else f"{z:3} {zname[:22]}")
            g = groups[(key, boss)]
            for c, p in odds.items():
                g[0][c] += p * k
            g[1].extend((w * k, off) for w, off in eq)
            g[2] += k
            g[3].add(r)
    print(f"per kill, % (roll: {args.roll}; level gap {args.gap}, world drop {args.world_drop}); "
          f"weight = {'REGEN count' if args.weight == 'regen' else 'one per species and zone'}")
    print(f"{'':27}{'weight':>6} " + " ".join(f"{c[:7]:>7}" for c in CATS) + "  eq lv")
    for key, boss in sorted(groups):
        odds, eq, k, rows = groups[(key, boss)]
        med = weighted_median(eq)
        label = f"{key} {'bosses' if boss else 'field'}"
        print(f"{label[:27]:27}{int(k):6} "
              + " ".join(f"{100 * odds[c] / k:7.1f}" for c in CATS)
              + (f"  {med:+4d}" if med is not None else "     -") + f"   ({len(rows)} mobs)")
    return 0


def cmd_zone_rows(args):
    npc, drop, items = npc_table(), stb("ITEM_DROP"), Items()
    users = collections.defaultdict(set)
    for r in range(1, npc.rows):
        if npc.get(r, 0).strip() and npc.i(r, COL_DROP_TABLE):
            users[npc.i(r, COL_DROP_TABLE)].add(r)
    spawns = zone_spawns()
    spawned = {r for _z, _n, c in spawns for r in c}
    print(f"{'zone':32} what the row is{'':24} one roll of the row alone, %")
    for z, zname, count in sorted(spawns, key=lambda t: t[0]):
        lv = [npc.i(r, COL_LEVEL) for r in count if npc.i(r, 9) > 0]
        if not lv or max(lv) < args.min_level:
            continue
        cells = table_cells(drop, z)
        own_users = users.get(z, set()) & spawned      # unspawned rows naming it are dead
        if not any(cells[1:]):
            kind = "BLANK"
        elif own_users - set(count):
            kind = f"SQUATTER: own table of {len(own_users - set(count))} mobs elsewhere"
        else:
            mirror = [t for t in sorted({npc.i(r, COL_DROP_TABLE) for r in count} - {z, 0})
                      if table_cells(drop, t)[1:] == cells[1:]]
            kind = (f"mirror of table {mirror[0]}" if mirror
                    else "own table of its monsters" if own_users else "a zone table")
        # the row on its own, at parity, col 20 of 100 (every roll reads it), no money
        odds, _ = kill_odds(items, cells, cells, 0, 100, max(lv))
        summary = " ".join(f"{c[:5]} {100 * odds[c]:.0f}" for c in CATS
                           if c != "nothing" and odds[c] >= 0.005)
        print(f"{z:3} {zname[:28]:28} {kind[:39]:39} {summary or '-'}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("bands", help="level band of every zone")
    b.add_argument("--min-level", type=int, default=185)
    b.add_argument("--rows", action="store_true", help="every spawned row, grouped by its zones")
    d = sub.add_parser("drops", help="what a kill pays, by level band or zone")
    d.add_argument("--by", choices=("band", "zone"), default="band")
    d.add_argument("--min-level", type=int, default=1)
    d.add_argument("--max-level", type=int, default=240)
    d.add_argument("--gap", type=int, default=0, help="player level minus monster level")
    d.add_argument("--world-drop", type=int, default=100)
    d.add_argument("--weight", choices=("species", "regen"), default="species")
    d.add_argument("--roll", choices=("today", "monster", "map"), default="today",
                   help="today's single roll, or one of the double drop's two rolls (1.3)")
    zr = sub.add_parser("zone-rows", help="each spawning zone's fallback drop row")
    zr.add_argument("--min-level", type=int, default=1)
    args = ap.parse_args()
    return {"bands": cmd_bands, "drops": cmd_drops, "zone-rows": cmd_zone_rows}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
