"""Build, install, verify and uninstall a generated zone (mapgen phase 1+).

    python scripts/mapgen-zone.py oracle                # byte-compare with the editor's File > New
    python scripts/mapgen-zone.py preview SPEC          # terrain checks + build/mapgen/preview-<folder>.png
    python scripts/mapgen-zone.py build SPEC --out DIR  # write the zone folder anywhere
    python scripts/mapgen-zone.py install SPEC [--zone N] [--dry-run]
    python scripts/mapgen-zone.py verify SPEC
    python scripts/mapgen-zone.py uninstall SPEC [--dry-run]

SPEC is a JSON file such as scripts/mapgen/specs/phase1-flat.json.

What `install` writes into data/:

* the zone folder data/3DDATA/MAPS/<planet_dir>/<folder>/ (it refuses if the
  folder already exists);
* one LIST_ZONE.STB row, copied from the template zone's row and overridden
  where generated zones must differ (see ROW_OVERRIDES);
* one LIST_ZONE_S.STL key, "LZON%03d" % zone.

Everything is recorded in build/mapgen/installed/<folder>.json, with the
previous value of every cell, so `uninstall` is cell-level and never reverts
another script's edits. Whole-file copies of the two tables also go to
build/mapgen/backup/<stamp>/ for emergencies.

Nothing is ever written beside a file under data/: a .bak there would be
baked into the VFS.

Zone number: --zone N, or the lowest row below 250 that is blank in both
LIST_ZONE and ITEM_DROP (drop-table ids share the zone namespace) and has no
LZONnnn key. Rows past the end of LIST_ZONE are not used: growing the table
is avoidable.

After install: restart the servers (they read data/ directly) and re-bake the
VFS for the client (LIST_ZONE.STB, LIST_ZONE_S.STL and the map files are
packed). Then `/mm <zone> <x> <y>` with x/y in 10 m units (verify prints
them).
"""

import argparse
import datetime
import hashlib
import importlib.util
import json
import math
import os
import shutil
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DATA = os.path.join(REPO, "data")
BUILD = os.path.join(REPO, "build", "mapgen")
sys.path.insert(0, HERE)

import mapgen  # noqa: E402
from mapgen import catalogue, chunk, decorate, ifo, paint, prefab, preview, terrain, tiles, walk, water, zon  # noqa: E402
from mapgen.zone import (CHUNK_CM, EventPoint, Tile, ZoneParams, build_zone,  # noqa: E402
                         centre_world, chunk_stem)


def _load_oro():
    spec = importlib.util.spec_from_file_location("import_oro", os.path.join(HERE, "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, ["import-oro.py", "--help"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = argv
    return mod


oro = _load_oro()      # STB / STL codecs (parse + to_bytes only; never its save())

ZONE_STB = r"3DDATA\STB\LIST_ZONE.STB"
ZONE_STL = r"3DDATA\STB\LIST_ZONE_S.STL"
DROP_STB = r"3DDATA\STB\ITEM_DROP.STB"
SKY_STB = r"3DDATA\STB\LIST_SKY.STB"
TEST_ZONE_NO = 250     # rows >= this are never served (lib_gsmain.cpp:38)

# LIST_ZONE game columns (src/common/include/rose/io/stb.h:472-519).
COL_NAME, COL_ZON, COL_START, COL_REVIVE = 0, 1, 2, 3
COL_SKY, COL_MINIMAP, COL_MM_X, COL_MM_Y = 7, 8, 9, 10
COL_DECO, COL_CNST = 11, 12
COL_JOIN, COL_KILL, COL_DEAD = 22, 23, 24
COL_STL = 26
COL_REVIVE_ZONE, COL_REVIVE_X, COL_REVIVE_Y = 31, 32, 33


def P(rel):
    return os.path.join(DATA, rel.replace("\\", os.sep))


def sha(b):
    return hashlib.sha256(b).hexdigest()


# --------------------------------------------------------------------- spec


def load_spec(path):
    with open(path, encoding="utf-8") as f:
        s = json.load(f)
    s["_path"] = os.path.abspath(path)
    return s


def zone_dir_rel(s):
    return "3DDATA\\MAPS\\%s\\%s" % (s["planet_dir"], s["folder"])


def zon_rel(s):
    return "%s\\%s.ZON" % (zone_dir_rel(s), s["folder"])


def template_zon(s, zstb):
    rel = zstb.get(s["template_zone_row"], COL_ZON).decode("latin-1")
    with open(P(rel), "rb") as f:
        return zon.parse(f.read())


def params_from_spec(s):
    c = s["chunks"]
    p = ZoneParams(folder=s["folder"], x0=c["x0"], y0=c["y0"], width=c["width"],
                   height=c["height"], ground_cm=float(s.get("ground_cm", 0)),
                   tile=Tile(**{k: v for k, v in s["tile"].items() if not k.startswith("_")}),
                   events=[])
    t = s.get("terrain")
    p.lakes, p.avoid = [], None
    if t:
        p.field = terrain.generate(p.width, p.height, t, t["seed"])
    if s.get("water"):
        if p.field is None:
            raise SystemExit("'water' needs a 'terrain' section")
        add_lakes(s, p)
    p.villages = []
    if s.get("villages"):
        if p.field is None:
            raise SystemExit("'villages' needs a 'terrain' section")
        add_villages(s, p)
    anchor = start_vertex(s, p)
    for e in s["events"]:
        dc, dr = (int(round(v / terrain.GRID_CM)) for v in e.get("offset_cm", [0, 0]))
        if e.get("at") == "centre":
            r, col = (p.height * 32, p.width * 32)
        elif e.get("at") == "start_spot":
            r, col = anchor
        else:
            raise SystemExit("event 'at' must be 'centre' or 'start_spot'")
        r, col = r + dr, col + dc
        z = p.ground_cm if p.field is None else float(p.field[r, col])
        p.events.append(EventPoint(e["name"], p.x0 * CHUNK_CM + col * terrain.GRID_CM,
                                   p.y0 * CHUNK_CM + r * terrain.GRID_CM, z))
    p.start_chunk = (c["x0"] + c["width"] // 2, c["y0"] + c["height"] // 2)
    p.lattice = None
    cfg = s.get("paint")
    if cfg:
        if p.field is None:
            raise SystemExit("'paint' needs a 'terrain' section")
        ts = load_tileset(cfg)
        stats = paint.load_stats(os.path.join(STATS_DIR, cfg["stats"]))
        rows, cols = 16 * p.height + 1, 16 * p.width + 1
        paths = []
        for path in cfg.get("paths", []):
            pts = []
            for pt in path["points"]:
                if pt == "start":
                    pts.append((int(round(anchor[0] / 4)), int(round(anchor[1] / 4))))
                else:
                    pts.append((int(round(pt[1] * (rows - 1))), int(round(pt[0] * (cols - 1)))))
            paths.append(pts)
        forced = lake_brushes(s, p, (rows, cols))
        path_block = forced >= 0
        start_corner = (int(round(anchor[0] / 4)), int(round(anchor[1] / 4)))
        for v in p.villages:
            for r, c, b in v["brushes"]:
                if 0 <= r < rows and 0 <= c < cols and forced[r, c] < 0:
                    forced[r, c] = b
                    path_block[r, c] = True     # a path through the village would be painted over
            path_block |= v["corner_block"]
        pb = cfg.get("path_brush", 0)
        for v in p.villages:
            if not v["connect"]:
                continue
            # The route in from the entrance paints over the village's own
            # brushes, from the gate inwards until it meets ground the
            # village already paints with the path brush.
            own = {(r, c): b for r, c, b in v["brushes"]}
            for corner in reversed(v["inner_path"]):
                if not (0 <= corner[0] < rows and 0 <= corner[1] < cols):
                    continue
                if own.get(corner) == pb and corner != v["inner_path"][-1]:
                    break
                forced[corner] = pb
        paths = [[free_corner(pt, path_block) for pt in pts] for pts in paths]
        for v in p.villages:
            if v["connect"]:
                paths.append([free_corner(v["gate"], path_block), free_corner(start_corner, path_block)])
        seed = cfg.get("seed", t["seed"] + 1)
        p.lattice, p.path_corners = paint.paint(p.field, ts, stats, cfg, seed, paths, forced, path_block)
        p.tile_grid = tiles.tiles_from_lattice(p.lattice, ts, np.random.default_rng(seed + 1))
    p.placed = None
    dcfg = s.get("decorate")
    if dcfg:
        if p.lattice is None:
            raise SystemExit("'decorate' needs a 'paint' section")
        cat = catalogue.load(os.path.join(STATS_DIR, dcfg["catalogue"]))
        avoid = water_margin(s, p, dcfg) | village_mask(p, dcfg.get("village_clear_m", 4.0))
        _, placed = decorate.place(p.field, p.lattice, p.x0, p.y0, cat, dcfg,
                                   dcfg.get("seed", t["seed"] + 7), avoid, p.path_corners, anchor)
        placed, p.deco_removed = decorate.repair(placed, p.field, anchor, play_mask(s, p),
                                                 fixed=village_members(p), footprints=footprints(s))
        p.placed = placed
        p.objects = decorate.records_by_chunk(placed, p.x0, p.y0)
    members = village_members(p)
    if members:
        vdeco, vcnst = prefab.records(members, p.x0, p.y0)
        p.objects = dict(p.objects or {})
        for k, recs in vdeco.items():
            p.objects[k] = list(p.objects.get(k, [])) + recs
        p.cnst = vcnst
    return p


def add_villages(s, p):
    """Place each spec village: pick a flat corner-aligned site, flatten a pad,
    instantiate the (turned) prefab. Fills p.villages; widens p.avoid."""
    rng = np.random.default_rng(s["terrain"]["seed"] + 300)
    band_m = ((s.get("terrain") or {}).get("ridge") or {}).get("width_m", 0)
    taken = []
    rows, cols = 16 * p.height + 1, 16 * p.width + 1
    if p.avoid is None:
        p.avoid = np.zeros(p.field.shape, bool)
    for v in s["villages"]:
        pf = prefab.load(os.path.join(PREFAB_DIR, v["prefab"] + ".json"))
        k = int(rng.integers(4)) if v.get("rotation", "auto") == "auto" else int(v["rotation"]) // 90
        pfr = prefab.rotate(pf, k)
        pad_m = pf["radius_m"] + v.get("pad_margin_m", 6.0)
        skirt = v.get("skirt_m", 30.0)
        centre = prefab.pick_site(p.field, pad_m, skirt, p.avoid, band_m + 10, taken)
        p.field, height = prefab.flatten(p.field, centre, pad_m, skirt)
        placed, brushes = prefab.instantiate(pfr, centre, height)
        cells = (p.field.shape[0] - 1, p.field.shape[1] - 1)
        fp = footprints(s)
        for q in placed:
            q["rc"] = prefab.footprint_radius(fp, q, cells)
        taken.append((centre[0], centre[1], pad_m + skirt))
        grow = int((pad_m + v.get("keep_clear_m", 6.0)) * 100 / terrain.GRID_CM)
        yy, xx = np.mgrid[0:p.field.shape[0], 0:p.field.shape[1]]
        pad_mask = np.hypot(yy - centre[0], xx - centre[1]) <= grow
        p.avoid |= pad_mask
        # corners paths may not cross: under any blocking member
        cb = np.zeros((rows, cols), bool)
        for q in placed:
            if q["rc"]:
                rr = int(math.ceil((q["rc"] + 150) / 1000.0))
                qr, qc = q["y"] / 1000.0, q["x"] / 1000.0
                for r in range(int(qr) - rr, int(qr) + rr + 2):
                    for c in range(int(qc) - rr, int(qc) + rr + 2):
                        if 0 <= r < rows and 0 <= c < cols and math.hypot(c - qc, r - qr) * 1000 <= q["rc"] + 150:
                            cb[r, c] = True
        # Entrance: the named member's bearing (spec "entrance"), else the
        # middle of the widest open arc in the village's walls, else the
        # side facing the map centre. The path then runs on the walk grid
        # from the centre out through it, and on to the start from there.
        blocked = decorate.blocked_cells(placed, cells, fp)
        cr, cc = centre[0] // 4, centre[1] // 4
        arcs = prefab.openings(blocked, centre, pad_m)
        if v.get("entrance"):
            named = [q for q in placed if q["name"] == v["entrance"]]
            if not named:
                raise SystemExit("village %s has no member named %r" % (v["prefab"], v["entrance"]))
            g = terrain.GRID_CM
            bearing = math.degrees(math.atan2(np.mean([q["y"] for q in named]) - centre[0] * g,
                                              np.mean([q["x"] for q in named]) - centre[1] * g)) % 360
            how = "member %s" % v["entrance"]
        elif arcs:
            bearing, how = prefab.arc_middle(arcs[0]), "widest opening %d-%d deg" % arcs[0]
        else:
            bearing, how = math.degrees(math.atan2(rows / 2 - cr, cols / 2 - cc)) % 360, "facing the map centre"
        route = prefab.inner_route(blocked, centre, bearing, pad_m)
        if route is None:
            raise SystemExit("village %s: no walkable way out of the centre" % v["prefab"])
        inner = prefab.route_corners(route)
        p.villages.append({"prefab": v["prefab"], "centre": centre, "height": height, "pad_m": pad_m,
                           "turns": k, "members": placed, "brushes": brushes, "corner_block": cb,
                           "gate": inner[-1], "inner_path": inner, "entrance_deg": bearing,
                           "entrance_how": how, "openings": arcs, "connect": v.get("connect", True)})


_fp_cache = {}


def footprints(s):
    """catalogue.Footprints for the spec's template zone (DECO + CNST ZSCs)."""
    key = s["template_zone_row"]
    if key not in _fp_cache:
        zstb, _, _ = tables()
        rel = lambda col: zstb.get(key, col).decode("latin-1").replace("\\\\", "\\")
        _fp_cache[key] = catalogue.Footprints(DATA, rel(COL_DECO), rel(COL_CNST))
    return _fp_cache[key]


def village_members(p):
    return [q for v in getattr(p, "villages", []) or [] for q in v["members"]]


def village_mask(p, margin_m):
    """Vertices inside any village pad plus `margin_m`."""
    m = np.zeros(p.field.shape, bool)
    yy, xx = np.mgrid[0:p.field.shape[0], 0:p.field.shape[1]]
    for v in getattr(p, "villages", []) or []:
        m |= np.hypot(yy - v["centre"][0], xx - v["centre"][1]) * terrain.GRID_CM / 100 <= v["pad_m"] + margin_m
    return m


def village_checks(s, p, members, field, start, check):
    """Phase 6 checks on placed village members (from the spec or from disk)."""
    if not members:
        return
    zstb, _, _ = tables()
    ok_ids, missing = True, set()
    for lump_name, col in (("OBJECT", COL_DECO), ("CNST", COL_CNST)):
        rel = zstb.get(s["template_zone_row"], col).decode("latin-1").replace("\\\\", "\\")
        meshes, objs = catalogue.read_zsc(P(rel))
        for q in members:
            if q["lump"] != lump_name:
                continue
            if not 0 <= q["id"] < len(objs) or not objs[q["id"]]["parts"]:
                ok_ids = False
                continue
            missing.update(catalogue.object_box(DATA, meshes, objs[q["id"]])[2])
    check(ok_ids, "every village object id exists in its ZSC (decoration or construction)")
    check(not missing, "every mesh of every village object exists on disk %s" % (sorted(missing)[:3] or ""))
    g = terrain.GRID_CM
    rough, unreachable = [], []
    blocked = decorate.blocked_cells(members + (p.placed or []), (field.shape[0] - 1, field.shape[1] - 1),
                                     footprints(s))
    a = walk.analyse(field, start, play_mask(s, p), blocked)
    fp = footprints(s)
    for q in members:
        if not q["rc"]:
            continue
        # flatness under the cells the member's walls actually stand on (a
        # disc of its radius reaches past a long fence into the skirt)
        own = np.zeros_like(blocked)
        fp.mark(own, q)
        orr, occ = np.nonzero(own)
        if not len(orr):
            continue
        vr = np.concatenate([orr, orr, orr + 1, orr + 1])
        vc = np.concatenate([occ, occ + 1, occ, occ + 1])
        hts = field[vr, vc]
        if float(hts.max() - hts.min()) > 5.0:
            rough.append("%s#%d (%.0f cm)" % (q["name"], q["id"], float(hts.max() - hts.min())))
        reach = q["rc"] + 300
        cr0, cr1 = int((q["y"] - reach) // g), int((q["y"] + reach) // g) + 1
        cc0, cc1 = int((q["x"] - reach) // g), int((q["x"] + reach) // g) + 1
        home = a["home"][max(0, cr0):cr1 + 1, max(0, cc0):cc1 + 1]
        if not home.any():
            unreachable.append("%s#%d" % (q["name"], q["id"]))
    check(not rough, "ground under every village building is flat within 5 cm %s" % (rough[:3] or ""))
    check(not unreachable, "every village building can be reached from the start %s" % (unreachable[:3] or ""))
    check(a["trap_cells"] == 0, "with village and decoration collision: no trap (%d cells)" % a["trap_cells"])
    lat = getattr(p, "lattice", None)
    if lat is not None and s.get("paint"):
        pb = s["paint"].get("path_brush", 0)
        bad = []
        for v in getattr(p, "villages", []) or []:
            if not v["connect"]:
                continue
            # the way out of the centre is walkable with every object in place ...
            if prefab.inner_route(blocked, v["centre"], v["entrance_deg"], v["pad_m"]) is None:
                bad.append(v["prefab"] + " (no walkable route)")
                continue
            # ... and the path brush runs unbroken from the start to the entrance
            seen, todo = set(), [(int(round(start[0] / 4)), int(round(start[1] / 4)))]
            todo = [q for q in todo if lat[q] == pb]
            while todo:
                q = todo.pop()
                if q in seen:
                    continue
                seen.add(q)
                for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    n = (q[0] + d[0], q[1] + d[1])
                    if 0 <= n[0] < lat.shape[0] and 0 <= n[1] < lat.shape[1] and lat[n] == pb and n not in seen:
                        todo.append(n)
            if v["inner_path"][-1] not in seen:
                bad.append(v["prefab"])
        check(not bad, "a path runs from the start into every village's entrance %s" % (bad or ""))
    for v in getattr(p, "villages", []) or []:
        print("        village %s: %d members (%d blocking), pad %.0f m at %.1f m, turned %d deg, "
              "entrance %.0f deg (%s), path in %d corners"
              % (v["prefab"], len(v["members"]), sum(1 for q in v["members"] if q["rc"]), v["pad_m"],
                 v["height"] / 100, v["turns"] * 90, v["entrance_deg"], v["entrance_how"], len(v["inner_path"])))

def water_margin(s, p, dcfg):
    """Vertices where no decoration may stand: lakes plus `water_clear_m`."""
    m = np.zeros(p.field.shape, bool)
    steps = int(dcfg.get("water_clear_m", 3.0) * 100 / terrain.GRID_CM)
    for lk in getattr(p, "lakes", []) or []:
        g = lk["mask"].copy()
        for _ in range(steps):
            n = g.copy()
            n[1:] |= g[:-1]
            n[:-1] |= g[1:]
            n[:, 1:] |= g[:, :-1]
            n[:, :-1] |= g[:, 1:]
            g = n
        m |= g
    return m


def deco_checks(s, p, placed, field, start, check, zdir=None):
    """Phase 5 checks; `placed` from the spec or rebuilt from the IFOs on disk."""
    if not placed:
        return None
    dcfg = s["decorate"]
    zstb, _, _ = tables()
    deco_rel = zstb.get(s["template_zone_row"], COL_DECO).decode("latin-1").replace("\\\\", "\\")
    meshes, objects = catalogue.read_zsc(P(deco_rel))
    used = sorted({q["id"] for q in placed})
    bad_ids = [i for i in used if not 0 <= i < len(objects) or not objects[i]["parts"]]
    missing = sorted({m for i in used if i not in bad_ids
                      for m in catalogue.object_box(DATA, meshes, objects[i])[2]})
    check(not bad_ids, "every object id exists in the zone's DECO ZSC %s" % (bad_ids or ""))
    check(not missing, "every mesh of every placed object exists on disk %s" % (missing[:3] or ""))

    per, outside = {}, 0
    for q in placed:
        cx = int((p.x0 * 16000 + q["x"]) // 16000)
        cy = int((p.y0 * 16000 + q["y"]) // 16000)
        per[(cx, cy)] = per.get((cx, cy), 0) + 1
        if zdir and "rec" in q and not any(r is q["rec"] for r in zdir_records(zdir, cx, cy)):
            outside += 1
    counts = [per.get(k, 0) for k in p.chunks()]
    check(max(counts) <= 278, "objects per chunk %d..%d, median %d (retail JG median 30, corpus max 278)"
          % (min(counts), max(counts), int(np.median(counts))))
    if zdir:
        check(outside == 0, "every object is stored in the chunk that contains it (%d not)" % outside)

    avoid = water_margin(s, p, dcfg)
    in_water = sum(bool(avoid[int(round(q["y"] / terrain.GRID_CM)), int(round(q["x"] / terrain.GRID_CM))])
                   for q in placed)
    sx, sy = start[1] * terrain.GRID_CM, start[0] * terrain.GRID_CM
    clear = dcfg.get("start_clear_m", 15.0) * 100
    in_start = sum(bool(np.hypot(q["x"] - sx, q["y"] - sy) < clear) for q in placed)
    pr, pc = np.nonzero(p.path_corners)
    if len(pr):
        pxy = np.stack([pc * 4 * terrain.GRID_CM, pr * 4 * terrain.GRID_CM], axis=1)
    else:
        pxy = np.zeros((0, 2))
    pclear = dcfg.get("path_clear_m", 4.0) * 100
    on_path = sum(bool(len(pxy) and np.min(np.hypot(pxy[:, 0] - q["x"], pxy[:, 1] - q["y"])) < pclear)
                  for q in placed)
    check(in_water == 0 and in_start == 0 and on_path == 0,
          "nothing in the water, the start area or on the path (%d / %d / %d)" % (in_water, in_start, on_path))

    gap = dcfg.get("collision_gap_m", 3.0) * 100
    coll = [q for q in placed if q["rc"]]
    tight = 0
    for i, a in enumerate(coll):
        for b in coll[i + 1:]:
            if abs(a["x"] - b["x"]) < 6000 and abs(a["y"] - b["y"]) < 6000:
                if np.hypot(a["x"] - b["x"], a["y"] - b["y"]) < a["rc"] + b["rc"] + gap - 1:
                    tight += 1
    check(tight == 0, "colliding objects keep a %.0f m walkable gap (%d pairs closer)" % (gap / 100, tight))

    blocked = decorate.blocked_cells(placed + village_members(p), (field.shape[0] - 1, field.shape[1] - 1),
                                     footprints(s))
    a = walk.analyse(field, start, play_mask(s, p), blocked)
    need = (s.get("terrain") or {}).get("min_connected_fraction", 0.95)
    check(a["trap_cells"] == 0,
          "with object collision: no area you can walk into but not out of (%d)" % a["trap_cells"])
    check(a["connected_fraction"] >= need,
          "with object collision: start connected to %.1f%% of the gentle play area"
          % (100 * a["connected_fraction"]))
    by = {}
    for q in placed:
        by[q["category"]] = by.get(q["category"], 0) + 1
    print("        %d objects (%s); %d colliding, blocking %d cells; %d colliders removed to clear traps"
          % (len(placed), ", ".join("%s %d" % kv for kv in sorted(by.items())), len(coll), int(blocked.sum()),
             getattr(p, "deco_removed", 0)))
    return blocked


_zdir_cache = {}


def zdir_records(zdir, cx, cy):
    key = (zdir, cx, cy)
    if key not in _zdir_cache:
        path = os.path.join(zdir, chunk_stem(cx, cy) + ".IFO")
        recs = []
        if os.path.isfile(path):
            with open(path, "rb") as f:
                recs = ifo.parse(f.read()).lump(ifo.OBJECT) or []
        _zdir_cache[key] = recs
    return _zdir_cache[key]


def load_tileset(cfg):
    return tiles.Tileset(tiles.tileset_path(DATA, cfg["tileset"]))


def brush_colours(ts, template):
    """Mean RGB of each brush's first full-tile texture, for previews."""
    from PIL import Image
    table, textures = template.lump(zon.TILES), template.lump(zon.TEXTURES)
    out = {}
    for b, (_, first, _) in ts.full.items():
        row = table[first]
        path = P(textures[row[0] + row[2]].decode("latin-1"))
        try:
            out[b] = tuple(np.asarray(Image.open(path).convert("RGB")).reshape(-1, 3).mean(0))
        except OSError:
            out[b] = (255, 0, 255)
    return out


def start_vertex(s, p):
    """(row, col) of the start point in the field: the gentle spot nearest the
    centre (terrain.pick_start) away from water, or the centre for flat ground."""
    if p.field is None:
        return (p.height * 32, p.width * 32)
    return terrain.pick_start(p.field, avoid=getattr(p, "avoid", None))


def add_lakes(s, p):
    """Carve the spec's lakes into p.field; fill p.lakes and p.water.

    Each lake: {"centre": "auto" | [fx, fy] (fractions of the map, x east,
    y north), "radius_m", "depth_cm", "shore_m", "shore_slope_deg",
    "irregularity", "ring_m", "margin_cm"}. p.avoid marks the water and a
    `keep_clear_m` band around it, where the start may not go.
    """
    w = s["water"]
    band_m = ((s.get("terrain") or {}).get("ridge") or {}).get("width_m", 0)
    rows, cols = p.field.shape
    p.avoid = np.zeros(p.field.shape, bool)
    for i, lake in enumerate(w.get("lakes", [])):
        rng = np.random.default_rng(s["terrain"]["seed"] + 100 + i)
        if lake.get("centre", "auto") == "auto":
            centre = water.pick_centre(p.field, lake["radius_m"], band_m + 10,
                                       lake.get("ring_m", 25.0), p.avoid)
        else:
            fx, fy = lake["centre"]
            centre = (int(round(fy * (rows - 1))), int(round(fx * (cols - 1))))
        p.field, level = water.carve(p.field, lake, rng, centre)
        mask = water.lake_mask(p.field, level, centre)
        p.lakes.append({"centre": centre, "level": level, "mask": mask})
        clear = int(w.get("keep_clear_m", 15) * 100 / terrain.GRID_CM)
        grown = mask.copy()
        for _ in range(clear):
            g = grown.copy()
            g[1:] |= grown[:-1]; g[:-1] |= grown[1:]; g[:, 1:] |= grown[:, :-1]; g[:, :-1] |= grown[:, 1:]
            grown = g
        p.avoid |= grown
    p.water = {}
    for lk in p.lakes:
        for key, rects in water.rects_for(lk["mask"], lk["level"], p.x0, p.y0, p.width, p.height).items():
            p.water.setdefault(key, []).extend(rects)


def lake_brushes(s, p, shape):
    """Lattice of forced brushes (-1 = free): seabed under the water, beach sand
    on the shore. In JG the chain table then puts dark soil between sand and
    grass, the way retail coasts are painted."""
    forced = np.full(shape, -1, int)
    w = s.get("water") or {}
    if not getattr(p, "lakes", None):
        return forced
    seabed, sand = w.get("seabed_brush", 7), w.get("sand_brush", 6)
    corners = p.field[::4, ::4]
    for lk in p.lakes:
        L, wet = lk["level"], lk["mask"][::4, ::4]
        near = wet.copy()
        for _ in range(w.get("beach_corners", 2)):           # 10 m per corner step
            g = near.copy()
            g[1:] |= near[:-1]; g[:-1] |= near[1:]; g[:, 1:] |= near[:, :-1]; g[:, :-1] |= near[:, 1:]
            near = g
        forced[near & (corners < L + w.get("beach_above_cm", 150))] = sand
        forced[wet & (corners < L - w.get("seabed_below_cm", 50))] = seabed
    return forced


def free_corner(pt, blocked):
    """Nearest lattice corner to `pt` that paths may use (path endpoints).
    `blocked` is a bool mask, or a forced-brush lattice (-1 = free)."""
    if blocked.dtype != bool:
        blocked = blocked >= 0
    if not blocked[pt]:
        return pt
    free = np.argwhere(~blocked)
    d = np.hypot(free[:, 0] - pt[0], free[:, 1] - pt[1])
    return tuple(int(v) for v in free[np.argmin(d)])


def play_mask(s, p):
    """Cells inside the ridge band: the area the connectivity check is about."""
    rows, cols = p.height * 64, p.width * 64
    r = (s.get("terrain") or {}).get("ridge") or {}
    band = int(r.get("width_m", 0) * 100 / terrain.GRID_CM) if r.get("height_cm") else 0
    m = np.zeros((rows, cols), bool)
    m[band:rows - band, band:cols - band] = True
    return m


def terrain_checks(s, p, field, start, check):
    """The phase 2 terrain checks; `field` may come from the spec or from disk."""
    a = walk.analyse(field, start, play_mask(s, p))
    need = (s.get("terrain") or {}).get("min_connected_fraction", 0.95)
    check(a["trap_cells"] == 0,
          "no area the player can walk into but not climb out of (%d trap cells)" % a["trap_cells"])
    check(a["connected_fraction"] >= need,
          "start connected to %.1f%% of the gentle play area (need %.0f%%)"
          % (100 * a["connected_fraction"], 100 * need))
    print("        steepest cell %.1f deg; %d cells >= 54 deg; %d cells reachable"
          % (a["max_slope_deg"], a["steep_cells"], a["reach_cells"]))
    return a


def water_checks(p, field, rects_by_chunk, check):
    """Phase 4 checks; `rects_by_chunk` from the spec or read back from the IFOs."""
    if not rects_by_chunk:
        return None
    for ok, msg in water.check(field, rects_by_chunk, p.x0, p.y0):
        check(ok, msg)
    cov, lvl = water.coverage(rects_by_chunk, field.shape, p.x0, p.y0)
    wet = cov & (field < lvl)
    n = sum(len(r) for r in rects_by_chunk.values())
    depth = float(np.nanmax(np.where(wet, lvl - field, np.nan))) if wet.any() else 0.0
    print("        %d water rectangles in %d chunks; %.0f m2 of water, deepest %.1f m; levels %s cm"
          % (n, len(rects_by_chunk), wet.sum() * (terrain.GRID_CM / 100) ** 2, depth / 100,
             sorted({r[1] for rs in rects_by_chunk.values() for r in rs})))
    return wet


def water_from_disk(zdir, p):
    out = {}
    for x, y in p.chunks():
        with open(os.path.join(zdir, chunk_stem(x, y) + ".IFO"), "rb") as f:
            o = ifo.parse(f.read()).lump(ifo.OCEAN)
        if o is not None and o.rects:
            out[(x, y)] = [tuple(r) for r in o.rects]
    return out


def tile_checks(ts, grid, have, check):
    """Phase 3 checks on a tile grid (row 0 = south)."""
    agreement, matched, total, illegal = tiles.regenerate_check(grid, have, ts)
    check(illegal == 0, "every tile is one brush or a legal brush pair (%d illegal)" % illegal)
    check(agreement == 1.0, "neighbouring tiles agree on every shared corner (%.2f%%)" % (100 * agreement))
    check(matched == total, "every tile id is the one its corners call for (%d / %d)" % (matched, total))
    m = grid["tile_index"][have]
    saddles = int(np.isin(m, (6, 9)).sum())
    check(saddles <= 0.002 * m.size,
          "checkerboard (saddle) tiles %d = %.2f%% (retail JG ~0.04%%; allow <= 0.2%%)"
          % (saddles, 100.0 * saddles / max(1, m.size)))


def field_from_disk(zdir, p):
    """Rebuild the global field from installed HIMs; also return seam mismatches."""
    field = np.full(terrain.field_shape(p.width, p.height), np.nan, "<f4")
    seams = []
    for x, y in p.chunks():
        with open(os.path.join(zdir, chunk_stem(x, y) + ".HIM"), "rb") as f:
            h = chunk.parse_him(f.read()).heights[::-1]                 # row 0 = south
        r0, c0 = (y - p.y0) * 64, (x - p.x0) * 64
        dst = field[r0:r0 + 65, c0:c0 + 65]
        known = ~np.isnan(dst)
        if known.any() and not np.array_equal(dst[known], h[known]):
            seams.append(chunk_stem(x, y))
        field[r0:r0 + 65, c0:c0 + 65] = h
    return field, seams


def build_files(s, zstb):
    return build_zone(params_from_spec(s), template_zon(s, zstb))


def row_values(s, zone_no, zstb):
    """The LIST_ZONE row: the template's row with ROW_OVERRIDES applied."""
    t = s["template_zone_row"]
    c = s["chunks"]
    row = [zstb.get(t, c) for c in range(zstb.cols)]
    overrides = {
        COL_NAME: s["name"],
        COL_ZON: zon_rel(s),
        COL_START: "start",
        COL_REVIVE: "restore",
        COL_MINIMAP: s.get("minimap", "NOMAP"),   # NOMAP: empty panel; blank would keep the last zone's
        # Minimap origin as a chunk file stem (x, 64-y) of the north-west chunk.
        # Meaningless with NOMAP, but the editor's IsValidMap hides a row whose
        # cols 9/10 are blank (MapManager.cs:665-671, editor cols 10/11).
        COL_MM_X: str(c["x0"]),
        COL_MM_Y: str(64 - (c["y0"] + c["height"] - 1)),
        COL_JOIN: "", COL_KILL: "", COL_DEAD: "",  # template triggers are QSD names: never inherit
        COL_STL: "LZON%03d" % zone_no,
        COL_REVIVE_ZONE: "", COL_REVIVE_X: "", COL_REVIVE_Y: "",
    }
    for c, v in overrides.items():
        row[c] = v.encode("latin-1")
    return row


ROW_OVERRIDES = "name, ZON path, start/revive names, minimap, triggers, STL key, revive zone/x/y"


# ------------------------------------------------------------------- tables


def tables():
    return (oro.Stb(P(ZONE_STB)), oro.Stl(P(ZONE_STL)), oro.Stb(P(DROP_STB)))


def zone_is_free(n, zstb, zstl, dstb):
    return (0 < n < min(TEST_ZONE_NO, zstb.rows) and not zstb.occupied(n)
            and not (n < dstb.rows and dstb.occupied(n)) and not zstl.has("LZON%03d" % n))


def pick_zone(zstb, zstl, dstb):
    for n in range(1, min(TEST_ZONE_NO, zstb.rows)):
        if zone_is_free(n, zstb, zstl, dstb):
            return n
    raise SystemExit("no free LIST_ZONE row below %d" % TEST_ZONE_NO)


def stl_remove(stl, key):
    k = key.encode("latin-1")
    j = next((i for i, (x, _) in enumerate(stl.keys) if x == k), None)
    if j is None:
        return None
    text = stl.langs[0][j][0]
    del stl.keys[j]
    for rows in stl.langs:
        del rows[j]
    return text


def manifest_path(s):
    return os.path.join(BUILD, "installed", s["folder"] + ".json")


# ----------------------------------------------------------------- commands


def cmd_build(s, out):
    zstb, _, _ = tables()
    files = build_files(s, zstb)
    for rel, blob in files.items():
        dst = os.path.join(out, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(blob)
    print("wrote %d files to %s" % (len(files), out))
    return 0


ZONETYPE_STB = r"3DDATA\TERRAIN\TILES\ZONETYPEINFO.STB"
ZONETYPE_TILESET_COL = 5          # game col; the editor reads editor col 6 (MapManager.cs:552)


def zone_tileset_name(zone_type, zt_cells):
    """'JG' from 'Table_Tileset_JG.STB' for a ZON's zone type, or None."""
    if not 0 <= zone_type < len(zt_cells):
        return None
    name = zt_cells[zone_type][ZONETYPE_TILESET_COL].strip()
    if not name.lower().startswith("table_tileset_"):
        return None
    return name[len("table_tileset_"):].rsplit(".", 1)[0]


def cmd_tiles_selftest():
    """Phase 3's first check, on retail data.

    For every zone whose type has a tileset, it rebuilds the corner-brush
    lattice from the zone's own tiles and measures how often neighbours agree
    on shared corners. It then regenerates every tile from that lattice with
    the painter's rule and compares the result with the file. Junon grassland
    (JG) zones must reach 99%.
    """
    zt = tiles.read_stb_cells(P(ZONETYPE_STB))
    sets = {}
    worst_jg, failed = 1.0, []
    print("%-22s %-6s %6s  %8s  %8s  %s" % ("zone", "tset", "tiles", "corners", "regen", "illegal"))
    for planet in sorted(os.listdir(P(r"3DDATA\MAPS"))):
        pdir = os.path.join(P(r"3DDATA\MAPS"), planet)
        for zone_name in sorted(os.listdir(pdir)):
            zdir = os.path.join(pdir, zone_name)
            zons = [f for f in os.listdir(zdir) if f.lower().endswith(".zon")]
            if not zons or not any(f.lower().endswith(".til") for f in os.listdir(zdir)):
                continue
            with open(os.path.join(zdir, zons[0]), "rb") as f:
                ztype = zon.parse(f.read()).lump(zon.INFO).zone_type
            name = zone_tileset_name(ztype, zt)
            if not name or not os.path.isfile(tiles.tileset_path(DATA, name)):
                continue
            ts = sets.setdefault(name, tiles.Tileset(tiles.tileset_path(DATA, name)))
            grid, have, _ = tiles.load_zone_tiles(zdir)
            if np.any(grid["tile_set"][have] >= len(ts.sets)):
                print("%-22s %-6s  tile_set outside the tileset: not authored with it" % (planet + "/" + zone_name, name))
                continue
            agreement, matched, total, illegal = tiles.regenerate_check(grid, have, ts)
            frac = matched / max(1, total)
            print("%-22s %-6s %6d  %7.2f%%  %7.2f%%  %d" % (planet + "/" + zone_name, name, total,
                                                             100 * agreement, 100 * frac, illegal))
            if name == "JG" and "MAPGEN" not in zone_name:
                worst_jg = min(worst_jg, frac)
                if frac < 0.99:
                    failed.append(zone_name)
    print("\nJG zones: worst regeneration %.2f%% (need 99%%) %s"
          % (100 * worst_jg, "PASS" if not failed else "FAIL %s" % failed))
    return 1 if failed else 0


STATS_DIR = os.path.join(HERE, "mapgen", "stats")
JG_ZONES = ["JG01", "JG02", "JG03", "JG04", "JG05", "JG06", "JG07", "JG08"]


def cmd_stats():
    """Regenerate scripts/mapgen/stats/jg_brush_by_slope.json from retail JG zones."""
    ts = tiles.Tileset(tiles.tileset_path(DATA, "JG"))
    dirs = [P(r"3DDATA\MAPS\JUNON\%s" % z) for z in JG_ZONES]
    counts = paint.brush_slope_stats(dirs, ts)
    os.makedirs(STATS_DIR, exist_ok=True)
    out = os.path.join(STATS_DIR, "jg_brush_by_slope.json")
    paint.save_stats(out, counts, ts, JG_ZONES)
    print("wrote %s" % out)
    cat = catalogue.build(DATA, "JG", dirs, ts)
    out_cat = os.path.join(STATS_DIR, "jg_decoration.json")
    catalogue.save(out_cat, cat)
    used = [o for o in cat["objects"] if o["uses"]]
    print("wrote %s: %d objects, %d used in retail JG (%d placements)"
          % (out_cat, len(cat["objects"]), len(used), sum(o["uses"] for o in used)))
    land = [b for b in range(ts.brushes) if b not in paint.WATER_BRUSHES_JG]
    print("land-brush share per slope band (what the painter aims for):")
    print("  %-9s" % "deg" + "".join("%8s" % ("b%d" % b) for b in land))
    for i in range(len(paint.SLOPE_BANDS) - 1):
        row = counts[i, land].astype(float)
        row /= max(1.0, row.sum())
        print("  %2d-%-6d" % (paint.SLOPE_BANDS[i], paint.SLOPE_BANDS[i + 1]) + "".join("%7.1f%%" % (100 * v) for v in row))
    print("  brushes: %s" % ", ".join("b%d=%s" % (b, ts.brush_names[b]) for b in land))
    return 0


PREFAB_DIR = os.path.join(HERE, "mapgen", "prefabs")


def cmd_prefab_extract(a):
    """Lift a retail cluster into scripts/mapgen/prefabs/<name>.json."""
    zstb, _, _ = tables()
    src, tmpl = a.zone_row, a.template_row
    norm = lambda c, r: zstb.get(r, c).decode("latin-1").replace("\\\\", "\\").upper()
    for col in (COL_DECO, COL_CNST):
        if norm(col, src) != norm(col, tmpl):
            raise SystemExit("zone %d's col %d (%s) differs from template %d's (%s): its object ids would "
                             "point at different objects" % (src, col, norm(col, src), tmpl, norm(col, tmpl)))
    zon_rel = zstb.get(src, COL_ZON).decode("latin-1")
    zone_dir = os.path.dirname(P(zon_rel))
    with open(P(zon_rel), "rb") as f:
        ztype = zon.parse(f.read()).lump(zon.INFO).zone_type
    ts_name = zone_tileset_name(ztype, tiles.read_stb_cells(P(ZONETYPE_STB)))
    ts = tiles.Tileset(tiles.tileset_path(DATA, ts_name)) if ts_name else None
    cat = catalogue.load(os.path.join(STATS_DIR, "jg_decoration.json"))
    pf = prefab.extract(DATA, zone_dir, norm(COL_DECO, src), norm(COL_CNST, src), cat,
                        (a.centre[0] * 1000.0, a.centre[1] * 1000.0), a.radius, tileset=ts)
    pf = dict({"_comment": "Village prefab lifted by `mapgen-zone.py prefab-extract`; see scripts/mapgen/prefab.py.",
               "name": a.name, "source_zone_row": src,
               "source_zone": zstb.get(src, COL_NAME).decode("latin-1"),
               "source_centre_mm": a.centre, "tileset": ts_name,
               "deco_zsc": norm(COL_DECO, src), "cnst_zsc": norm(COL_CNST, src)}, **pf)
    os.makedirs(PREFAB_DIR, exist_ok=True)
    out = os.path.join(PREFAB_DIR, a.name + ".json")
    prefab.save(out, pf)
    from collections import Counter
    comp = Counter("%s:%s" % (m["lump"][0], m["name"]) for m in pf["members"])
    blocking = sum(1 for m in pf["members"] if prefab.blocking_radius_of(m) > 0)
    print("wrote %s: %d members (%d blocking), %d brush corners, ground relief %.1f m (tileset %s)"
          % (out, len(pf["members"]), blocking, len(pf["brushes"]), pf["ground_relief_cm"] / 100, ts_name))
    print("   " + ", ".join("%s x%d" % kv for kv in comp.most_common()))
    return 0


def cmd_preview(s):
    """Generate, analyse and render without installing anything."""
    p = params_from_spec(s)
    if p.field is None:
        raise SystemExit("spec has no 'terrain' section")
    check = Checks()
    a = terrain_checks(s, p, p.field, start_vertex(s, p), check)
    wet = water_checks(p, p.field, p.water, check)
    deco_checks(s, p, p.placed, p.field, start_vertex(s, p), check)
    village_checks(s, p, village_members(p), p.field, start_vertex(s, p), check)
    os.makedirs(BUILD, exist_ok=True)
    out = preview.render(p.field, a, os.path.join(BUILD, "preview-%s.png" % s["folder"]), wet=wet)
    lo, hi = float(p.field.min()), float(p.field.max())
    print("  heights %.1f .. %.1f m; walkability preview %s" % (lo / 100, hi / 100, out))
    if p.lattice is not None:
        ts = load_tileset(s["paint"])
        zstb, _, _ = tables()
        colours = brush_colours(ts, template_zon(s, zstb))
        out2 = preview.render_tiles(p.field, p.lattice, colours, a,
                                    os.path.join(BUILD, "preview-%s-tiles.png" % s["folder"]), wet=wet,
                                    objects=(p.placed or []) + village_members(p))
        share = np.bincount(p.lattice.ravel(), minlength=ts.brushes) / p.lattice.size
        # Retail target for THIS terrain: the table's per-band mix weighted by
        # how many of our corners fall in each slope band.
        counts, bands = paint.load_stats(os.path.join(STATS_DIR, s["paint"]["stats"]))
        land = [b for b in range(ts.brushes) if b not in s["paint"].get("exclude_brushes", paint.WATER_BRUSHES_JG)]
        mix = counts[:, land] / counts[:, land].sum(axis=1, keepdims=True)
        sl = paint.corner_slopes(p.field)
        w = np.bincount(np.clip(np.digitize(sl, bands) - 1, 0, len(bands) - 2).ravel(), minlength=len(bands) - 1)
        target = (w[:, None] * mix).sum(0) / w.sum()
        print("  brush share, painted vs retail mix for this terrain's slopes:")
        for i, b in enumerate(land):
            print("     %-14s %5.1f%%  (retail %5.1f%%)" % (ts.brush_names[b], 100 * share[b], 100 * target[i]))
        print("  tile preview %s" % out2)
        tile_checks(ts, p.tile_grid, np.ones(p.tile_grid.shape, bool), check)
    again = params_from_spec(s)
    same = np.array_equal(again.field.view("u4"), p.field.view("u4")) and again.water == p.water
    if p.tile_grid is not None:
        same &= np.array_equal(again.tile_grid, p.tile_grid)
    same &= again.objects == p.objects
    check(same, "same spec + seed -> bit-identical terrain, water, tiles and objects")
    return 1 if check.failed else 0


def cmd_install(s, zone_arg, dry):
    if os.path.exists(manifest_path(s)):
        raise SystemExit("%s is already installed (%s); uninstall first" % (s["folder"], manifest_path(s)))
    zdir = P(zone_dir_rel(s))
    if os.path.exists(zdir):
        raise SystemExit("zone folder already exists and is not ours: %s" % zdir)
    zstb, zstl, dstb = tables()
    n = zone_arg if zone_arg is not None else pick_zone(zstb, zstl, dstb)
    if not zone_is_free(n, zstb, zstl, dstb):
        raise SystemExit("zone %d is not free (LIST_ZONE / ITEM_DROP row, or LZON%03d key)" % (n, n))

    files = build_files(s, zstb)
    p = params_from_spec(s)
    if p.field is not None:
        gate = Checks()
        print("terrain checks before install:")
        terrain_checks(s, p, p.field, start_vertex(s, p), gate)
        if gate.failed:
            raise SystemExit("terrain fails its checks; nothing installed (see `preview`)")
    row = row_values(s, n, zstb)
    cells = {c: [zstb.get(n, c).decode("latin-1"), row[c].decode("latin-1")]
             for c in range(zstb.cols) if zstb.get(n, c) != row[c]}
    key = "LZON%03d" % n

    print("zone %d  folder %s  (%d files, %d chunks)" % (n, zdir, len(files), s["chunks"]["width"] * s["chunks"]["height"]))
    print("LIST_ZONE row %d: %d cells; LIST_ZONE_S +%s %r" % (n, len(cells), key, s["name"]))
    if dry:
        for c in sorted(cells):
            print("   col %2d  %r" % (c, cells[c][1]))
        print("dry run: nothing written")
        return 0

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    bdir = os.path.join(BUILD, "backup", stamp)
    os.makedirs(bdir, exist_ok=True)
    for rel in (ZONE_STB, ZONE_STL):
        shutil.copyfile(P(rel), os.path.join(bdir, os.path.basename(rel)))

    for c in range(zstb.cols):
        zstb.set(n, c, row[c])
    zstl.append(key, n, s["name"])
    stb_blob, stl_blob = zstb.to_bytes(), zstl.to_bytes()

    manifest = {"zone": n, "folder": s["folder"], "zone_dir": zone_dir_rel(s), "spec": s["_path"],
                "stl_key": key, "stl_text": s["name"], "cells": {str(c): v for c, v in cells.items()},
                "files": {rel: sha(b) for rel, b in files.items()}, "backup": bdir,
                "installed": stamp}
    os.makedirs(os.path.dirname(manifest_path(s)), exist_ok=True)
    with open(manifest_path(s), "w", encoding="utf-8") as f:     # first, so a crash is recoverable
        json.dump(manifest, f, indent=1)
    for rel, blob in files.items():
        dst = os.path.join(zdir, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(blob)
    with open(P(ZONE_STB), "wb") as f:
        f.write(stb_blob)
    with open(P(ZONE_STL), "wb") as f:
        f.write(stl_blob)
    print("installed; manifest %s; table backups %s" % (manifest_path(s), bdir))
    return cmd_verify(s)


def cmd_uninstall(s, dry):
    mp = manifest_path(s)
    if not os.path.exists(mp):
        raise SystemExit("not installed (no %s)" % mp)
    with open(mp, encoding="utf-8") as f:
        m = json.load(f)
    n, zdir = m["zone"], P(m["zone_dir"])
    zstb, zstl, _ = tables()
    restored, kept = 0, []
    for c, (old, new) in m["cells"].items():
        c = int(c)
        if zstb.get(n, c) == new.encode("latin-1"):
            zstb.set(n, c, old)
            restored += 1
        else:
            kept.append(c)
    removed_text = stl_remove(zstl, m["stl_key"])
    gone, changed = [], []
    for rel, h in m["files"].items():
        path = os.path.join(zdir, rel.replace("/", os.sep))
        if os.path.exists(path):
            with open(path, "rb") as f:
                (gone if sha(f.read()) == h else changed).append(path)
    print("zone %d: restore %d cells%s; remove STL %s (%r); delete %d files%s" % (
        n, restored, " (left changed cols %s)" % kept if kept else "", m["stl_key"], removed_text,
        len(gone), "; KEEP %d edited files" % len(changed) if changed else ""))
    if dry:
        print("dry run: nothing written")
        return 0
    with open(P(ZONE_STB), "wb") as f:
        f.write(zstb.to_bytes())
    with open(P(ZONE_STL), "wb") as f:
        f.write(zstl.to_bytes())
    for path in gone:
        os.remove(path)
    for dp, _, _ in sorted(os.walk(zdir), key=lambda t: -len(t[0])):
        if not os.listdir(dp):
            os.rmdir(dp)
    if not changed:
        os.remove(mp)
    print("uninstalled" + ("" if not changed else "; manifest kept because of edited files"))
    return 0


class Checks:
    def __init__(self):
        self.failed = 0

    def __call__(self, ok, what):
        print("  %s %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            self.failed += 1


def cmd_verify(s):
    mp = manifest_path(s)
    if not os.path.exists(mp):
        raise SystemExit("not installed (no %s)" % mp)
    with open(mp, encoding="utf-8") as f:
        m = json.load(f)
    n, zdir = m["zone"], P(m["zone_dir"])
    check = Checks()
    zstb, zstl, dstb = tables()
    print("verify zone %d (%s)" % (n, zdir))

    # Tables: the row, its references, and that both tables still round-trip.
    for rel, obj in ((ZONE_STB, zstb), (ZONE_STL, zstl)):
        with open(P(rel), "rb") as f:
            check(obj.to_bytes() == f.read(), "%s re-serialises byte-identical" % rel)
    check(n < TEST_ZONE_NO, "zone %d < %d (served by the gameserver)" % (n, TEST_ZONE_NO))
    check(not (n < dstb.rows and dstb.occupied(n)), "ITEM_DROP row %d is blank" % n)
    row = row_values(s, n, zstb)
    check(all(zstb.get(n, c) == row[c] for c in range(zstb.cols)), "LIST_ZONE row %d matches the spec" % n)
    # The editor's Open dialog lists a row only if these game columns are
    # non-blank (MapManager.IsValidMap, MapManager.cs:647-682; editor col = game col + 1).
    blank = [c for c in (COL_ZON, COL_START, COL_REVIVE, COL_MM_X, COL_MM_Y, COL_DECO, COL_CNST)
             if not zstb.get(n, c).strip()]
    check(not blank, "editor IsValidMap: required columns non-blank %s" % (blank or ""))
    check(zstl.has(m["stl_key"]) and zstl.name(m["stl_key"]) == s["name"],
          "LIST_ZONE_S %s = %r" % (m["stl_key"], s["name"]))
    zon_path = P(zstb.get(n, COL_ZON).decode("latin-1"))
    check(os.path.isfile(zon_path), "col %d ZON exists: %s" % (COL_ZON, zon_path))
    for c in (COL_DECO, COL_CNST):
        rel = zstb.get(n, c).decode("latin-1").replace("\\\\", "\\")
        check(os.path.isfile(P(rel)), "col %d ZSC exists: %s" % (c, rel))
    sky = oro.Stb(P(SKY_STB))
    sky_row = int(zstb.get(n, COL_SKY) or b"0")
    check(0 <= sky_row < sky.rows and sky.occupied(sky_row), "col %d sky row %d exists in LIST_SKY" % (COL_SKY, sky_row))

    # Files: present, unchanged since install, and round-trip through the codecs.
    bad_hash, bad_rt = [], []
    for rel, h in m["files"].items():
        path = os.path.join(zdir, rel.replace("/", os.sep))
        if not os.path.isfile(path):
            bad_hash.append(rel + " (missing)")
            continue
        with open(path, "rb") as f:
            blob = f.read()
        if sha(blob) != h:
            bad_hash.append(rel)
        parse, build = mapgen.CODECS[os.path.splitext(rel)[1].lower()]
        if build(parse(blob)) != blob:
            bad_rt.append(rel)
    check(not bad_hash, "%d files present and unchanged since install %s" % (len(m["files"]), bad_hash or ""))
    check(not bad_rt, "every file round-trips through the mapgen codecs %s" % (bad_rt or ""))

    # Content: chunk set, grid sizes, tile ids and textures, events.
    with open(zon_path, "rb") as f:
        z = zon.parse(f.read())
    tile_table, textures = z.lump(zon.TILES), z.lump(zon.TEXTURES)
    p = params_from_spec(s)
    stems = {chunk_stem(x, y) for x, y in p.chunks()}
    on_disk = {os.path.splitext(f)[0] for f in os.listdir(zdir) if f.upper().endswith(".HIM")}
    check(on_disk == stems, "chunk set on disk = spec %s" % sorted(stems))
    used_tex, problems = set(), []
    for st in sorted(stems):
        missing = [ext for ext in (".HIM", ".TIL", ".IFO", ".MOV")
                   if not os.path.isfile(os.path.join(zdir, st + ext))]
        if missing:
            problems.append("%s missing %s" % (st, missing))
            continue
        with open(os.path.join(zdir, st + ".HIM"), "rb") as f:
            him = chunk.parse_him(f.read())
        if (him.width, him.height) != (65, 65):
            problems.append("%s.HIM is %dx%d" % (st, him.width, him.height))
        with open(os.path.join(zdir, st + ".TIL"), "rb") as f:
            til = chunk.parse_til(f.read())
        ids = np.unique(til.tiles["tile_id"])
        if ids.min() < 0 or ids.max() >= len(tile_table):
            problems.append("%s.TIL tile ids %s outside the %d-row tile table" % (st, ids.tolist(), len(tile_table)))
            continue
        for tid in ids:
            r = tile_table[int(tid)]
            used_tex.update({int(r[0] + r[2]), int(r[1] + r[3])})
    check(not problems, "every chunk: HIM/TIL/IFO/MOV present, HIM 65x65, tile ids in the ZON tile table %s"
          % (problems or ""))
    missing_tex = [textures[i].decode("latin-1") for i in sorted(used_tex)
                   if not os.path.isfile(P(textures[i].decode("latin-1")))]
    check(not missing_tex, "the %d tile textures used exist on disk %s" % (len(used_tex), missing_tex or ""))
    names = [e.name for e in z.lump(zon.EVENTS)]
    for c in (COL_START, COL_REVIVE):
        check(zstb.get(n, c) in names, "col %d event %r is in the ZON" % (c, zstb.get(n, c)))
    for e in z.lump(zon.EVENTS):
        wx, wy = e.x + 520000, e.y + 520000
        inside = chunk_stem(int(wx // CHUNK_CM), int(wy // CHUNK_CM)) in stems
        check(inside, "event %r at world (%.0f, %.0f) lies on a chunk" % (e.name, wx, wy))
    start = next(e for e in z.lump(zon.EVENTS) if e.name == b"start")

    # Terrain, re-derived from the files on disk rather than from the spec.
    field, seams = field_from_disk(zdir, p)
    check(not seams, "neighbouring chunks share identical edge heights %s" % (seams or ""))
    bad_bounds = []
    for x, y in p.chunks():
        with open(os.path.join(zdir, chunk_stem(x, y) + ".HIM"), "rb") as f:
            him = chunk.parse_him(f.read())
        placeholder = bool(np.all(np.abs(him.bounds.patches) > 1e30))
        flat = float(him.heights.min()) == float(him.heights.max())
        if placeholder and flat:
            continue                       # phase-1 style flat chunk: editor placeholders
        want = chunk.compute_him_bounds(him.heights)
        if not (np.array_equal(want.patches, him.bounds.patches) and np.array_equal(want.quads, him.bounds.quads)):
            bad_bounds.append(chunk_stem(x, y))
    check(not bad_bounds, "HIM culling bounds are real and match the retail formula %s" % (bad_bounds or ""))
    sv = (int(round((start.y + 520000 - p.y0 * CHUNK_CM) / terrain.GRID_CM)),
          int(round((start.x + 520000 - p.x0 * CHUNK_CM) / terrain.GRID_CM)))
    check(abs(float(field[sv]) - start.z) < 1.0, "start event height %.1f cm = terrain %.1f cm" % (start.z, field[sv]))
    terrain_checks(s, p, field, sv, check)
    on_disk_water = water_from_disk(zdir, p)
    if s.get("water") or on_disk_water:
        check(on_disk_water == (p.water or {}), "water rectangles on disk = the spec's (%d chunks)" % len(on_disk_water))
        water_checks(p, field, on_disk_water, check)
    if s.get("decorate"):
        cat = catalogue.load(os.path.join(STATS_DIR, s["decorate"]["catalogue"]))
        on_disk = {k: zdir_records(zdir, *k) for k in p.chunks()}
        placed_disk = decorate.placed_from_records(on_disk, p.x0, p.y0, cat, field)
        vm = village_mask(p, 0.0)
        g = terrain.GRID_CM
        in_village = [bool(vm[min(int(round(q["y"] / g)), vm.shape[0] - 1), min(int(round(q["x"] / g)), vm.shape[1] - 1)])
                      for q in placed_disk]
        deco_disk = [q for q, iv in zip(placed_disk, in_village) if not iv]
        n_vdeco = sum(1 for q in village_members(p) if q["lump"] == "OBJECT")
        check(len(deco_disk) == len(p.placed or []) and len(placed_disk) - len(deco_disk) == n_vdeco,
              "objects on disk = the spec's (%d decorations + %d village decorations)"
              % (len(deco_disk), len(placed_disk) - len(deco_disk)))
        deco_checks(s, p, deco_disk, field, sv, check, zdir)
    if s.get("villages"):
        cnst_disk = []
        for k in p.chunks():
            path = os.path.join(zdir, chunk_stem(*k) + ".IFO")
            with open(path, "rb") as f:
                cnst_disk += ifo.parse(f.read()).lump(ifo.CNST) or []
        want = sum(1 for q in village_members(p) if q["lump"] == "CNST")
        check(len(cnst_disk) == want, "construction records on disk = the spec's (%d)" % len(cnst_disk))
        village_checks(s, p, village_members(p), field, sv, check)
    if s.get("paint"):
        ts = load_tileset(s["paint"])
        zt = tiles.read_stb_cells(P(ZONETYPE_STB))
        check(zone_tileset_name(z.lump(zon.INFO).zone_type, zt) == s["paint"]["tileset"],
              "ZON zone type selects the %s tileset in ZONETYPEINFO (editor brushes match)" % s["paint"]["tileset"])
        grid, have, _ = tiles.load_zone_tiles(zdir)
        tile_checks(ts, grid, have, check)

    print("\n  GM warp: /mm %d %d %d" % (n, round((start.x + 520000) / 1000), round((start.y + 520000) / 1000)))
    print("  %s" % ("ALL CHECKS PASSED" if not check.failed else "%d CHECK(S) FAILED" % check.failed))
    return 1 if check.failed else 0


def cmd_oracle():
    """Byte-compare mapgen with the editor's File > New for identical inputs."""
    out = os.path.join(BUILD, "oracle-out")
    ps1 = os.path.join(HERE, "mapgen", "oracle", "Run-NewZoneOracle.ps1")
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ps1,
                        "-OutDir", out, "-SizeX", "2", "-SizeY", "2", "-ZoneFile", "ORACLE.ZON"],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit("oracle failed")
    zstb, _, _ = tables()
    with open(P(r"3DDATA\MAPS\JUNON\JG01\JG01.ZON"), "rb") as f:
        tmpl = zon.parse(f.read())
    # The editor's New: chunk files "{y+30}_{x+30}" (first number = slot x),
    # i.e. slots x 30-31, y 33-34; start (4930 m, 5470 m), restore
    # (4910 m, 5500 m) in its 5200 m-shifted metres; ZON start 30/30; tile
    # brush 1 / set 1 / index 15 / id 1; economy constants (New.xaml.cs:241-452).
    p = ZoneParams(folder="ORACLE", x0=30, y0=33, width=2, height=2, ground_cm=0.0,
                   tile=Tile(brush=1, tile_set=1, tile_index=15, tile_id=1),
                   events=[EventPoint("start", 493000.0, 547000.0, 0.0),
                           EventPoint("restore", 491000.0, 550000.0, 0.0)],
                   start_chunk=(30, 30),
                   economy=zon.Economy(b"0", 0, b"button1", b"button2",
                                       [35, 500, 30, 10, 20, 20, 10, 10, 10, 10, 10, 30, 10]))
    ours = build_zone(p, tmpl)
    theirs = {}
    for dp, _, fs in os.walk(out):
        for fn in fs:
            rel = os.path.relpath(os.path.join(dp, fn), out).replace(os.sep, "/")
            with open(os.path.join(dp, fn), "rb") as f:
                theirs[rel] = f.read()
    ok = True
    for rel in sorted(set(ours) | set(theirs)):
        a, b = ours.get(rel), theirs.get(rel)
        same = a == b
        ok &= same
        print("  %s %-44s %s" % ("same" if same else "DIFF", rel,
                                  "" if same else "ours %s / editor %s" % (
                                      len(a) if a else "missing", len(b) if b else "missing")))
    print("\n%s (the editor also writes a 512x512 plane lightmap per chunk; mapgen does not, by design)"
          % ("BYTE-IDENTICAL to the editor's File > New" if ok else "DIFFERS from the editor"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("oracle")
    sub.add_parser("walk-selftest")
    sub.add_parser("tiles-selftest")
    sub.add_parser("stats")
    pe = sub.add_parser("prefab-extract")
    pe.add_argument("name")
    pe.add_argument("--zone-row", type=int, required=True)
    pe.add_argument("--centre", type=float, nargs=2, required=True, help="world position in /mm units (10 m)")
    pe.add_argument("--radius", type=float, required=True, help="metres")
    pe.add_argument("--template-row", type=int, default=22, help="the zone row whose ZSCs the prefab must match")
    pv = sub.add_parser("preview")
    pv.add_argument("spec")
    b = sub.add_parser("build")
    b.add_argument("spec")
    b.add_argument("--out", required=True)
    i = sub.add_parser("install")
    i.add_argument("spec")
    i.add_argument("--zone", type=int)
    i.add_argument("--dry-run", action="store_true")
    v = sub.add_parser("verify")
    v.add_argument("spec")
    u = sub.add_parser("uninstall")
    u.add_argument("spec")
    u.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    if a.cmd == "oracle":
        return cmd_oracle()
    if a.cmd == "tiles-selftest":
        return cmd_tiles_selftest()
    if a.cmd == "stats":
        return cmd_stats()
    if a.cmd == "prefab-extract":
        return cmd_prefab_extract(a)
    if a.cmd == "walk-selftest":
        print("Walkability checker on synthetic terrain with known answers:")
        fails = walk.selftest()
        print("  %s" % ("ALL PASSED" if not fails else "FAILED: %s" % fails))
        return 1 if fails else 0
    s = load_spec(a.spec)
    if a.cmd == "preview":
        return cmd_preview(s)
    if a.cmd == "build":
        return cmd_build(s, a.out)
    if a.cmd == "install":
        return cmd_install(s, a.zone, a.dry_run)
    if a.cmd == "verify":
        return cmd_verify(s)
    return cmd_uninstall(s, a.dry_run)


if __name__ == "__main__":
    sys.exit(main())
