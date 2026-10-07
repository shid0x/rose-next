"""Bring Oro's monster density down to Karkia's, without re-running the Oro import.

Alpha testers found Oro overcrowded. Measured the way Karkia was tuned -- every
regen point filled through CRegenPOINT::Proc's real branch table, bodies counted
per occupied 100 m cell against JG07 (32.2, our densest field zone) -- the five
Oro field maps sat at 0.47-1.01x where Karkia sits at 0.27-0.43x, with spawn
points ~12 m apart where Karkia keeps camps ~38 m apart (outside the aggro
radius, so a pull is one camp). The Oro import had merged only the
single-species carpets of the Golden Ring and the Fossil Sanctuary; the
RoseZA-style nests (cap 10, counts of 6-7, a 5 s tick, 20-30 m spread) passed
through untouched, and in the Fossil Sanctuary they held 1,240 of its 1,786
bodies. The Fearsome Terrasaurus King's three points also shipped at cap 5 on
a 30-50 minute tick, stacking up to five kings per point.

The rules live in `import-oro-667.py` (`CAMP_ZONES`, `consolidate_camps`,
`BOSS_MIN_INTERVAL`, `plan_spawns`) so a future import reproduces them. Its
`--stage 3` cannot be re-run to apply them, though: it also rewrites the Oro
monster rows from 667, which would undo rebalance-oro-667.py, the drop pass and
the target marks. This script replays only the spawn step: it builds the plan
from the 667 source and writes each map file's REGEN lump, leaving every other
lump alone.

Backups are the original REGEN lump bytes, per file, in
`build/oro-spawn-density/` -- never the whole .IFO, so `--restore` cannot revert
another script's edit to the same file (fix-coplanar-object-overlaps.py moves
OBJECT records in these maps). The first run's bytes are kept; later runs never
overwrite them.

Servers read the spawn lumps at startup: restart the gameserver. The client
does not read them, so no bake is needed for this alone.

    python scripts/fix-oro-spawn-density.py --dry-run
    python scripts/fix-oro-spawn-density.py
    python scripts/fix-oro-spawn-density.py --verify
    python scripts/fix-oro-spawn-density.py --restore

`--verify` re-simulates every map, prints the density table and fails if a camp
map leaves Karkia's band, a boss can hold more than one alive, a boss species
sits in a camp, or a species the source spawned is lost.

`data/` is gitignored, so this file and the importer are the only committed record.
"""

import argparse
import collections
import hashlib
import importlib.util
import json
import math
import os
import statistics
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
DATA = os.path.join(REPO, "data")
BACKUP_DIR = os.path.join(REPO, "build", "oro-spawn-density")
MANIFEST = os.path.join(BACKUP_DIR, "manifest.json")
BOSS_SIDECAR = os.path.join(DATA, "3DDATA", "STB", "LIST_NPC.oro667-bosses.json")
JG07_DIR = os.path.join(DATA, "3DDATA", "MAPS", "JUNON", "JG07")

# Karkia's measured band (import-karkia.py SPAWN_CAMPS): 0.27x-0.43x of JG07.
# A camp map outside it, or with camps closer than this, fails --verify.
KARKIA_MAX_RATIO = 0.45
MIN_CAMP_GAP_M = 30.0


def load(name, fname):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fname))
    m = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [fname]
    try:
        spec.loader.exec_module(m)
    finally:
        sys.argv = saved
    return m


imp = load("import_oro_667", "import-oro-667.py")
oro, kk = imp.oro, imp.kk


def ifo_path(key):
    return os.path.join(DATA, imp.MAPS_REL, key[0], key[1])


def regen_block(path):
    buf, bounds = oro.read_ifo(path)
    off, end = oro.lump_block(bounds, oro.LUMP_REGEN)
    return buf, bounds, (buf[off:end] if off is not None else None)


def backup_name(key):
    return os.path.join(BACKUP_DIR, key[0], key[1] + ".regen")


def load_manifest():
    if os.path.exists(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as fh:
            return json.load(fh)
    return {"files": {}}


# ------------------------------------------------------------- simulation
class Npc:
    def __init__(self):
        self.t = oro.Stb(os.path.join(DATA, r"3DDATA\STB\LIST_NPC.STB"))

    def spawns(self, i):
        """The guard CZoneTHREAD::RegenCharacter applies: a name, and not an NPC."""
        if i < 1 or i >= self.t.rows or not self.t.get(i, 0).strip():
            return False
        ty = self.t.get(i, oro.NPC_TYPE_COL).strip()
        return not (ty and int(ty) >= 900)

    def name(self, i):
        return self.t.get(i, 0).decode("latin-1") if 0 < i < self.t.rows else "?"


def fill(o, npc, ticks=400):
    """CRegenPOINT::Proc run with nobody killing: bodies alive once it stops."""
    basic, tactics = kk.regen_roster_of(o["extra"])
    _iv, cap, _rng, tp = kk.regen_get_params(o["extra"])
    tp = max(tp, 1)
    live, cur = 0, 1

    def regen(slots, k, d=0):
        nonlocal live
        if len(slots) > k:
            i, c = slots[k]
            if npc.spawns(i) and c + d >= 1:
                live += c + d

    for _ in range(ticks):
        if live >= cap:
            cur = max(cur - 1, 1)
            continue
        v = ((cap * 2 - live) * cur * 50) // (cap * tp)
        if v <= 10:
            cur += 12; regen(basic, 0)
        elif v <= 15:
            cur += 15; regen(basic, 0, -2); regen(basic, 1)
        elif v <= 25:
            cur += 12; regen(basic, 2)
        elif v <= 30:
            cur += 15; regen(basic, 0, -1); regen(basic, 2)
        elif v <= 40:
            cur += 12; regen(basic, 3)
        elif v <= 50:
            cur += 12; regen(basic, 1); regen(basic, 2, -2)
        elif v <= 65:
            cur += 20; regen(basic, 2); regen(basic, 3, -2)
        elif v <= 73:
            cur += 15; regen(basic, 3); regen(basic, 4)
        elif v <= 85:
            cur += 15; regen(basic, 0); regen(tactics, 0, -1); regen(basic, 4, -2)
        elif v <= 92:
            cur = 1; regen(basic, 1); regen(tactics, 0); regen(tactics, 1)
        else:
            cur = 7; regen(basic, 4); regen(tactics, 0, 1); regen(tactics, 1)
        cur = min(cur, 500)
    return live


def density(points, npc):
    """(bodies, occupied 100 m cells, bodies per cell, median nearest-point gap m)."""
    cells, xy, bodies = collections.Counter(), [], 0
    for o in points:
        n = fill(o, npc)
        x, y, _z = struct.unpack_from("<fff", o["fixed"], kk.REGEN_POS_OFF)
        x, y = x / 100.0, y / 100.0
        bodies += n
        xy.append((x, y))
        cells[(math.floor(x / 100), math.floor(y / 100))] += n
    occ = sum(1 for v in cells.values() if v > 0)
    gaps = [min((math.hypot(x - a, y - b) for j, (a, b) in enumerate(xy) if j != i), default=0.0)
            for i, (x, y) in enumerate(xy)]
    return bodies, occ, (bodies / occ if occ else 0.0), (statistics.median(gaps) if gaps else 0.0)


def read_points(d):
    pts, seen = [], set()
    for f in sorted(os.listdir(d)):
        if not f.lower().endswith(".ifo") or f.lower() in seen:
            continue
        seen.add(f.lower())
        buf, bounds = oro.read_ifo(os.path.join(d, f))
        off, _end = oro.lump_block(bounds, oro.LUMP_REGEN)
        if off is None or buf[off:off + 4] == b"\0\0\0\0":
            continue
        objs, _ = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        pts += objs or []
    return pts


def species_of(objs):
    out = set()
    for o in objs:
        out.update(oro.regen_mob_ids(o["extra"]))
    return out


# ------------------------------------------------------------------ verbs
def build_plan():
    if not os.path.isdir(imp.SRC_667):
        raise SystemExit(f"667 source not found: {imp.SRC_667}")
    src = imp.read_spawn_source(imp.SRC_667)
    final = imp.plan_spawns(src)
    if imp.report_spawn_sanity(final):
        raise SystemExit("the plan fails its own sanity check; nothing written")
    return src, final


def apply(dry):
    src, final = build_plan()
    manifest = load_manifest()
    changed = 0
    for key in sorted(final):
        path = ifo_path(key)
        if not os.path.isfile(path):
            raise SystemExit(f"{path}: missing (the Oro import has not run)")
        buf, bounds, cur = regen_block(path)
        if cur is None:
            raise SystemExit(f"{path}: no REGEN lump to replace")
        blob = imp.regen_blob(src, final, key)
        if cur == blob:
            continue
        changed += 1
        if dry:
            continue
        rel = "/".join(key)
        bp = backup_name(key)
        if rel not in manifest["files"]:
            os.makedirs(os.path.dirname(bp), exist_ok=True)
            with open(bp, "wb") as fh:
                fh.write(cur)
            manifest["files"][rel] = hashlib.sha256(cur).hexdigest()
            os.makedirs(BACKUP_DIR, exist_ok=True)
            with open(MANIFEST, "w", encoding="utf-8") as fh:
                json.dump(manifest, fh, indent=1, sort_keys=True)
        with open(path, "wb") as fh:
            fh.write(oro.build_ifo(bounds, buf, {oro.LUMP_REGEN: blob}))
    print(f"{'would rewrite' if dry else 'rewrote'} the REGEN lump of {changed} of {len(final)} map files")
    if not dry and changed:
        return verify()
    return 0


def restore(dry):
    manifest = load_manifest()
    if not manifest["files"]:
        print("nothing to restore (no backups)")
        return 0
    n = 0
    for rel, sha in sorted(manifest["files"].items()):
        key = tuple(rel.split("/"))
        with open(backup_name(key), "rb") as fh:
            saved = fh.read()
        if hashlib.sha256(saved).hexdigest() != sha:
            raise SystemExit(f"{backup_name(key)}: backup does not match its manifest hash")
        path = ifo_path(key)
        buf, bounds, cur = regen_block(path)
        if cur == saved:
            continue
        n += 1
        if not dry:
            with open(path, "wb") as fh:
                fh.write(oro.build_ifo(bounds, buf, {oro.LUMP_REGEN: saved}))
    print(f"{'would restore' if dry else 'restored'} {n} REGEN lumps from {BACKUP_DIR}")
    return 0


def verify():
    src, final = build_plan()
    npc = Npc()
    bad = 0
    for key in sorted(final):
        _buf, _bounds, cur = regen_block(ifo_path(key))
        if cur != imp.regen_blob(src, final, key):
            bad += 1
            print(f"  !! {key[0]}/{key[1]}: REGEN lump differs from the plan")

    with open(BOSS_SIDECAR, encoding="utf-8") as fh:
        bosses = {int(k) for k in json.load(fh)}
    _b, _c, jg07, _g = density(read_points(JG07_DIR), npc)
    print(f"\n  JG07 yardstick: {jg07:.1f} bodies per occupied 100 m cell")
    print(f"  {'map':8s} {'points':>6s} {'boss':>4s} {'bodies':>6s} {'cells':>5s} {'/cell':>6s} "
          f"{'xJG07':>6s} {'gap m':>6s}  species")
    by_folder = collections.defaultdict(list)
    for key in final:
        buf, bounds = oro.read_ifo(ifo_path(key))
        live, _ = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        by_folder[key[0]] += live or []
    src_by_folder = collections.defaultdict(list)
    for key, (objs, _t) in src.items():
        src_by_folder[key[0]] += objs
    for folder in sorted(by_folder):
        pts = by_folder[folder]
        kinds = collections.Counter(imp.point_kind(o) for o in pts)
        bodies, occ, per, gap = density(pts, npc)
        camps = [o for o in pts if imp.point_kind(o) == "camp"]
        _cb, _co, _cp, camp_gap = density(camps, npc) if camps else (0, 0, 0, 0)
        lost = species_of(src_by_folder[folder]) - species_of(pts)
        ratio = per / jg07 if jg07 else 0
        print(f"  {folder:8s} {len(pts):6d} {kinds['boss']:4d} {bodies:6d} {occ:5d} {per:6.1f} "
              f"{ratio:6.2f} {gap:6.1f}  {len(species_of(pts))}"
              f"{'  LOST ' + str(sorted(lost)) if lost else ''}")
        if lost:
            bad += 1
        if folder in imp.CAMP_ZONES:
            if ratio > KARKIA_MAX_RATIO:
                bad += 1
                print(f"  !! {folder}: {ratio:.2f}x JG07 is above Karkia's band ({KARKIA_MAX_RATIO}x)")
            if camp_gap < MIN_CAMP_GAP_M:
                bad += 1
                print(f"  !! {folder}: camps {camp_gap:.1f} m apart (Karkia keeps >= {MIN_CAMP_GAP_M:.0f} m)")
        for o in pts:
            ids = set(oro.regen_mob_ids(o["extra"]))
            kind = imp.point_kind(o)
            cap = kk.regen_get_params(o["extra"])[1]
            if ids & bosses and kind != "boss":
                bad += 1
                print(f"  !! {folder}: boss {sorted(ids & bosses)} in a {kind} point")
            if kind == "boss" and cap != 1:
                bad += 1
                print(f"  !! {folder}: boss {sorted(ids)} can hold {cap} alive")
    # every boss the source spawned still has its points
    src_boss = collections.Counter(n for objs in src_by_folder.values() for o in objs
                                   for n in set(oro.regen_mob_ids(o["extra"])) & bosses)
    now_boss = collections.Counter(n for objs in by_folder.values() for o in objs
                                   for n in set(oro.regen_mob_ids(o["extra"])) & bosses)
    print("\n  bosses (points, one alive each): "
          + ", ".join(f"{npc.name(b)} x{now_boss[b]}" for b in sorted(now_boss)))
    if src_boss != now_boss:
        bad += 1
        print(f"  !! boss points changed: source {dict(src_boss)} now {dict(now_boss)}")
    print(f"\n{'OK' if not bad else f'FAIL ({bad})'}")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    a = ap.parse_args()
    if a.verify:
        return verify()
    if a.restore:
        return restore(a.dry_run)
    return apply(a.dry_run)


if __name__ == "__main__":
    sys.exit(main())
