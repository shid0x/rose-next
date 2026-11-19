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

Scope comes from the map REGEN lumps (audit-spawned-npcs.py), never from LIST_NPC alone:
the table also holds quest actors, summons and unspawned duplicates.
"""
import argparse
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

from rosefmt import ifo  # noqa: E402

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


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("bands", help="level band of every zone")
    b.add_argument("--min-level", type=int, default=185)
    b.add_argument("--rows", action="store_true", help="every spawned row, grouped by its zones")
    args = ap.parse_args()
    return {"bands": cmd_bands}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
