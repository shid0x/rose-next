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
import re
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
from mapgen import areas, barrier, catalogue, chunk, decorate, ifo, layout, paint, prefab, preview, terrain, tiles, walk, water, zon  # noqa: E402
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
    """A spec file, or a layout file ("layout": 1) compiled to a spec."""
    with open(path, encoding="utf-8") as f:
        s = json.load(f)
    if s.get("layout") == 1:
        try:
            s = layout.compile_layout(s)
        except layout.LayoutError as e:
            raise SystemExit("layout %s: %s" % (path, e))
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
    """Build everything a zone needs from a spec, in dependency order:
    terrain (+ features) -> water -> villages -> start -> events -> cover
    areas -> paint (+ roads) -> decoration -> records. Every phase 7 key
    (terrain features / tilt / ridge edges, flood, lake and village "where",
    roads, start, cover) is optional, so earlier specs build unchanged."""
    c = s["chunks"]
    p = ZoneParams(folder=s["folder"], x0=c["x0"], y0=c["y0"], width=c["width"],
                   height=c["height"], ground_cm=float(s.get("ground_cm", 0)),
                   tile=Tile(**{k: v for k, v in s["tile"].items() if not k.startswith("_")}),
                   events=[])
    t = s.get("terrain")
    p.lakes, p.avoid, p.features = [], None, {}
    p.play_v, p.frame = None, None
    if t:
        # The play region first (terrain.shape): features, compass words and
        # every placement live inside it; outside it is cliffs and highlands.
        p.play_v = terrain.shape_mask(p.width, p.height, t, t["seed"])
        if p.play_v is not None:
            rr, cc = np.nonzero(p.play_v)
            p.frame = (int(rr.min()), int(rr.max()), int(cc.min()), int(cc.max()))
        # the border (mountains) is built after the water, from the final rim
        p.shape_cfg = t.get("shape")
        p.field = terrain.generate(p.width, p.height, t, t["seed"], terrain_features(s, p), play=p.play_v,
                                   walls=not s.get("water"))
        if p.play_v is not None:
            # nothing may be placed within 15 m of the cliff foot
            p.avoid = areas.distance_m(~p.play_v) < 15.0
    if s.get("water"):
        if p.field is None:
            raise SystemExit("'water' needs a 'terrain' section")
        add_lakes(s, p)
        if p.play_v is not None and t["shape"].get("wall_cm", 5000.0) > 0:
            # the mountains rise from the shore the water left (the flood
            # keeps 12 m off the play edge, so the rim is dry); built before
            # the water, a low face could sit under the waterline
            p.field = np.round(terrain.shape_walls(p.field, p.play_v, t["shape"],
                                                   np.random.default_rng(t["seed"] + 61)).astype("<f4"), 1).astype("<f4")
            refresh_water(s, p)
    p.villages = []
    if s.get("villages"):
        if p.field is None:
            raise SystemExit("'villages' needs a 'terrain' section")
        add_villages(s, p)
    p.barrier = add_barrier(s, p)
    p.start_v = pick_start_vertex(s, p)
    if p.play_v is not None and t["shape"].get("wall_cm", 5000.0) > 0:
        # after the start exists: the border is closed for the ground a
        # player can actually reach from it (lifts only corners outside the
        # play area, which nothing placed later depends on)
        p.ring_fixes = close_ring(p)
    anchor = p.start_v
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
    cover_mult, cover_bias = cover_fields(s, p)
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
        # A harbour village lifted from the coast (Adventurer's Plain, Kenji
        # Beach) is painted with sand and seabed; placed inland, those become
        # land brushes so no seabed shows on a hilltop. By the water it keeps
        # its own paint.
        inland = {int(k): b for k, b in cfg.get("inland_remap", {"6": 5, "7": 5, "8": 0}).items()}
        for v in p.villages:
            for r, c, b in v["brushes"]:
                if not v.get("near_water"):
                    b = inland.get(b, b)
                if 0 <= r < rows and 0 <= c < cols and forced[r, c] < 0:
                    forced[r, c] = b
                    path_block[r, c] = True     # a path through the village would be painted over
            path_block |= v["corner_block"]
        pb = cfg.get("path_brush", 0)
        for v in p.villages:
            if not v["has_path"]:
                continue
            # The route in from the entrance paints over the village's own
            # brushes, from the gate inwards until it meets ground the
            # village already paints with the path brush.
            own = {(r, c): (b if v.get("near_water") else inland.get(b, b)) for r, c, b in v["brushes"]}
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
        for road in s.get("roads", []):
            paths.append([road_end(p, road["from"], path_block), road_end(p, road["to"], path_block)])
        seed = cfg.get("seed", t["seed"] + 1)
        try:
            p.lattice, p.path_corners = paint.paint(p.field, ts, stats, cfg, seed, paths, forced, path_block,
                                                    area_bias=cover_bias)
        except ValueError as e:
            raise SystemExit("paths: %s (lattice corners row, col; water, beaches, villages and slopes over "
                             "max_path_slope_deg block them)" % e)
        p.tile_grid = tiles.tiles_from_lattice(p.lattice, ts, np.random.default_rng(seed + 1))
    p.placed = None
    dcfg = s.get("decorate")
    if dcfg:
        if p.lattice is None:
            raise SystemExit("'decorate' needs a 'paint' section")
        cat = catalogue.load(os.path.join(STATS_DIR, dcfg["catalogue"]))
        avoid = water_margin(s, p, dcfg) | village_mask(p, dcfg.get("village_clear_m", 4.0)) | barrier_mask(p)
        _, placed = decorate.place(p.field, p.lattice, p.x0, p.y0, cat, dcfg,
                                   dcfg.get("seed", t["seed"] + 7), avoid, p.path_corners, anchor,
                                   area_mult=cover_mult, kind_of=kind_of(s))
        placed, p.deco_removed = decorate.repair(placed, p.field, anchor, play_mask(s, p),
                                                 fixed=village_members(p) + p.barrier, footprints=footprints(s),
                                                 trap_scope=trap_scope(s, p))
        p.placed = placed
        p.objects = decorate.records_by_chunk(placed, p.x0, p.y0)
    members = village_members(p)
    if members:
        vdeco, vcnst = prefab.records(members, p.x0, p.y0)
        p.objects = dict(p.objects or {})
        for k, recs in vdeco.items():
            p.objects[k] = list(p.objects.get(k, [])) + recs
        p.cnst = vcnst
    if p.barrier:
        p.objects = dict(p.objects or {})
        for k, recs in decorate.records_by_chunk(p.barrier, p.x0, p.y0).items():
            p.objects[k] = list(p.objects.get(k, [])) + recs
    if p.objects or getattr(p, "cnst", None):
        separate_coplanar(s, p)
    return p


_cop = {}


def separate_coplanar(s, p):
    """Run fix-coplanar-object-overlaps.py's solver on the records before they
    are written: placements whose faces share a plane flicker (CLAUDE.md,
    "Coplanar Placements Flicker"). A prefab lifted from sloped ground and
    set on a flat pad can bring two faces together that retail kept apart
    (Adventurer's Plain's wooden platforms). Same detection, plan and
    thresholds as the tool, so `verify`'s run of it finds nothing. Only the
    positions of the moved records change (a centimetre or two)."""
    import argparse as _ap
    import dataclasses
    if "mod" not in _cop:
        spec = importlib.util.spec_from_file_location("fix_coplanar", os.path.join(HERE, "fix-coplanar-object-overlaps.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        _cop["mod"] = mod
    cop = _cop["mod"]
    zstb, _, _ = tables()
    rel = lambda col: zstb.get(s["template_zone_row"], col).decode("latin-1").replace("\\\\", "\\")
    tabs = {cop.LUMP_OBJECT: cop.load_zsc(rel(COL_DECO)), cop.LUMP_CNST: cop.load_zsc(rel(COL_CNST))}
    placements, refs = [], []
    for code, recs_by in ((cop.LUMP_OBJECT, p.objects or {}), (cop.LUMP_CNST, getattr(p, "cnst", None) or {})):
        for key in sorted(recs_by):
            for i, r in enumerate(recs_by[key]):
                tab = tabs[code]
                if not tab or not 0 <= r.obj_id < len(tab["objects"]):
                    continue
                raw = tuple(cop.f32(v) for v in r.pos)
                o = {"path": "", "file": "%d_%d" % key, "lump": code, "ordinal": i, "id": r.obj_id,
                     "rot": tuple(cop.f32(v) for v in r.rot), "scale": tuple(cop.f32(v) for v in r.scale),
                     "raw_pos": raw, "orig_raw": raw, "zsc": tab}
                o["pos"] = cop.world_pos(raw)
                o["tris"] = cop.placement_triangles(tab, o, o["pos"])
                placements.append(o)
                refs.append((recs_by, key, i))
    args = _ap.Namespace(min_sep=1.0, step=0.5, sink=10000.0, min_area=400.0, only=[])
    left = cop.solve_zone(placements, args, verbose=False)
    moved = 0
    for o, (recs_by, key, i) in zip(placements, refs):
        if o["raw_pos"] != o["orig_raw"]:
            recs_by[key][i] = dataclasses.replace(recs_by[key][i], pos=o["raw_pos"])
            moved += 1
    p.coplanar_moved, p.coplanar_left = moved, len(left)


def field_shape(p):
    return terrain.field_shape(p.width, p.height)


def snap4(rc):
    """Nearest tile-corner vertex (multiple of 4): prefab brushes stay aligned."""
    return (int(round(rc[0] / 4.0)) * 4, int(round(rc[1] / 4.0)) * 4)


def terrain_features(s, p):
    """Resolve terrain.features (hills, mountains) to vertex centres; register them."""
    out = []
    for f in (s.get("terrain") or {}).get("features", []):
        _, target, _ = areas.resolve(f["area"], field_shape(p), {}, frame=getattr(p, "frame", None), play=getattr(p, "play_v", None))
        centre = snap4(target)
        f = dict(f)
        for v in s.get("villages", []):
            if v.get("on") == f["name"] and v.get("pad_from_walls"):
                # the hill's flat top must hold the whole pad, which is sized
                # from the prefab's walls (add_villages), not its radius
                pf = prefab.load(os.path.join(PREFAB_DIR, v["prefab"] + ".json"))
                cells0 = (field_shape(p)[0] - 1, field_shape(p)[1] - 1)
                pad_m = max(pf["radius_m"] + v.get("pad_margin_m", 6.0),
                            prefab.wall_extent_m(pf, footprints(s), cells0) + 4.0)
                f["top_m"] = max(f.get("top_m", 0.0), pad_m + 2.0)
                f["radius_m"] = max(f["radius_m"], f["top_m"] + 35.0)
        out.append(dict(f, centre=centre))
        p.features[f["name"]] = {"kind": f["kind"], "centre": centre, "radius_m": f["radius_m"],
                                 "height_cm": f["height_cm"], "top_m": f.get("top_m", 0.0)}
    return out


def area_allow(desc, p, core=0.5, fallback=0.15):
    """(allowed vertex mask, target vertex) for placing one thing in an area:
    the area's core, or its fuzzy edge if the core is too strict."""
    w, target, _ = areas.resolve(desc, p.field.shape, p.features, frame=getattr(p, 'frame', None), play=getattr(p, 'play_v', None))
    return w >= core, w >= fallback, target


def kind_of(s):
    """Object -> kind: the kinds table (FLOWER, MUSHROOM, ...) or its category."""
    name = (s.get("decorate") or {}).get("kinds")
    if not name:
        return None
    with open(os.path.join(STATS_DIR, name), encoding="utf-8") as f:
        table = json.load(f)["kinds"]
    by_id = {i: k for k, ids in table.items() for i in ids}
    return lambda o: by_id.get(o["id"], o["category"])


def cover_fields(s, p):
    """Lattice-shaped decoration multipliers per kind and brush biases from
    the spec's "cover" areas. Later areas paint over earlier ones where they
    overlap (blend by weight), so "rocky north-west" can override "forest
    along the north edge"."""
    cover = s.get("cover")
    if not cover or p.field is None:
        return None, None
    shape = p.field.shape
    rows, cols = (shape[0] - 1) // 4 + 1, (shape[1] - 1) // 4 + 1
    mult, bias = {}, np.zeros((rows, cols, 16))
    p.cover_mult = mult
    p.cover_weights = []
    for e in cover:
        w, _, _ = areas.resolve(e["area"], shape, p.features, frame=getattr(p, 'frame', None), play=getattr(p, 'play_v', None))
        if e.get("fade"):
            w = areas.fade(w, shape, e["fade"]["toward"], e["fade"].get("to", 0.3))
        wl = w[::4, ::4]
        p.cover_weights.append(w)
        for kind, x in e.get("mult", {}).items():
            m = mult.setdefault(kind, np.ones((rows, cols)))
            mult[kind] = m * (1 - wl) + x * wl
        for b, x in e.get("brush_bias", {}).items():
            bias[:, :, int(b)] = bias[:, :, int(b)] * (1 - wl) + x * wl
    return mult, bias


def path_land(s, p, extra_block=None):
    """Label lattice corners a path may cross (dry, not beach, not village
    paint, no steeper than paint.max_path_slope_deg) into connected
    components. Returns (labels: -1 = blocked, sizes by label)."""
    rows, cols = 16 * p.height + 1, 16 * p.width + 1
    cfg = s.get("paint") or {}
    block = paint.corner_slopes(p.field) > cfg.get("max_path_slope_deg", 30.0)
    if s.get("water") and p.lakes:
        block |= lake_brushes(s, p, (rows, cols)) >= 0
    for v in p.villages:
        for r, c, b in v["brushes"]:
            if 0 <= r < rows and 0 <= c < cols:
                block[r, c] = True
        block |= v["corner_block"]
    if extra_block is not None:
        block |= extra_block
    labels = np.full((rows, cols), -1, int)
    sizes = []
    for r0, c0 in zip(*np.nonzero(~block)):
        if labels[r0, c0] >= 0:
            continue
        lab, stack, n = len(sizes), [(r0, c0)], 0
        labels[r0, c0] = lab
        while stack:
            r, c = stack.pop()
            n += 1
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    rr, cc = r + dr, c + dc
                    if 0 <= rr < rows and 0 <= cc < cols and not block[rr, cc] and labels[rr, cc] < 0:
                        labels[rr, cc] = lab
                        stack.append((rr, cc))
        sizes.append(n)
    return labels, sizes


def ground_at(field, x, y):
    """Bilinear terrain height (cm) at field-local x, y (cm)."""
    g = terrain.GRID_CM
    vx, vy = x / g, y / g
    ix, iy = min(max(int(vx), 0), field.shape[1] - 2), min(max(int(vy), 0), field.shape[0] - 2)
    fx, fy = vx - ix, vy - iy
    return float(field[iy, ix] * (1 - fx) * (1 - fy) + field[iy, ix + 1] * fx * (1 - fy)
                 + field[iy + 1, ix] * (1 - fx) * fy + field[iy + 1, ix + 1] * fx * fy)


def place_stilts(s, p, v, pf, name, skirt, band_m, taken, k_default):
    """Place a stilt village (a prefab with "water": it stood over water in
    retail; DESIGN.md, Villages). By the water (near_water and the map has
    water): the shore site and rotation where its wet corners lie over the
    water and its dry ones (the ramp) on land (prefab.pick_harbour), at that
    water's level. Otherwise it brings its own shallow pond, at its site's
    ground level. The ground is shaped as in the source (prefab.
    shape_harbour) and members stand at their height above the water.
    Returns (centre, turns, turned prefab, water level, placed, brushes, how)."""
    others = getattr(p, "pads", np.zeros(p.field.shape, bool))
    near = bool(v.get("near_water")) and bool(p.lakes)
    if near:
        where = v.get("where") or {"all": True}
        if "near" in where:
            where = dict(where, extra_m=where.get("extra_m", 40.0) + pf["radius_m"])
        wgt, target, _ = areas.resolve(where, p.field.shape, p.features, frame=getattr(p, 'frame', None), play=getattr(p, 'play_v', None))
        wet_all = np.zeros(p.field.shape, bool)
        for lk in p.lakes:
            wet_all |= lk["mask"]
        dist = areas.distance_m(wet_all)
        lake = min(p.lakes, key=lambda lk: float(areas.distance_m(lk["mask"])[int(target[0]), int(target[1])]))
        level = lake["level"]
        allow = (wgt >= 0.15) & (dist <= pf["radius_m"] * 0.6)
        if getattr(p, "play_v", None) is not None:
            # the whole village and its shaped shelf stay inside the play
            # area: placed by the play edge, the shelf carved a channel
            # through the cliff and opened the map
            allow &= areas.distance_m(~p.play_v) >= pf["radius_m"] + skirt + 5.0
        try:
            centre, k, score = prefab.pick_harbour(p.field, pf, level, allow, others, band_m + 10, taken, target)
        except ValueError:
            raise SystemExit("village %s: no shore in its area for a stilt village" % name)
        how = "over the water's edge (%.0f%% of its ground already fits)" % (100 * score)
    else:
        k = k_default
        if v.get("on"):
            centre = snap4(p.features[v["on"]]["centre"])
        elif v.get("where"):
            core, edge, target = area_allow(v["where"], p)
            centre = None
            for allow in (core, edge):
                try:
                    centre = prefab.pick_site(p.field, pf["radius_m"], skirt, p.avoid, band_m + 10, taken,
                                              allow=allow, target=target)
                    break
                except ValueError:
                    continue
            if centre is None:
                raise SystemExit("village %s: no site in its area" % name)
        else:
            centre = prefab.pick_site(p.field, pf["radius_m"], skirt, p.avoid, band_m + 10, taken)
        # the pond's level sits below the lowest ground in a ring around it
        # (as water.carve does), or on uneven ground it leaks down the valleys
        yy, xx = np.mgrid[0:p.field.shape[0], 0:p.field.shape[1]]
        dd = np.hypot(yy - centre[0], xx - centre[1]) * terrain.GRID_CM / 100
        ring = (dd > pf["radius_m"]) & (dd <= pf["radius_m"] + skirt)
        level = float(np.floor(p.field[ring].min() - 30.0))
        how = "over a pond of its own (no water near)"
    pfr = prefab.rotate(pf, k)
    p.field = prefab.shape_harbour(p.field, pfr, centre, level, skirt)
    placed, brushes = prefab.instantiate(pfr, centre, 0.0)
    for q, m in zip(placed, pfr["members"]):
        q["z"] = level + m["above_water_cm"]
        q["sink"] = q["z"] - ground_at(p.field, q["x"], q["y"])
        q["stilts"] = True
    if not near:
        yy, xx = np.mgrid[0:p.field.shape[0], 0:p.field.shape[1]]
        inside = np.hypot(yy - centre[0], xx - centre[1]) * terrain.GRID_CM / 100 <= pf["radius_m"]
        deep = np.unravel_index(np.argmin(np.where(inside, p.field, np.inf)), p.field.shape)
        mask = water.lake_mask(p.field, level, deep)
        p.lakes.append({"centre": deep, "level": level, "mask": mask, "banks": {}})
        p.features[name + "_pond"] = {"kind": "lake", "centre": deep, "radius_m": pf["radius_m"], "mask": mask}
    refresh_water(s, p)
    wet = np.zeros(p.field.shape, bool)
    for lk in p.lakes:
        wet |= lk["mask"]
    p.avoid |= wet
    return centre, k, pfr, level, placed, brushes, how


def add_barrier(s, p):
    """A fence or boulder line along the edge of the play area (terrain.shape
    "edge"): on the cliff's top lip, or just outside the play area when the
    shape has no cliff. See mapgen/barrier.py. Returns placed objects."""
    cfg = ((s.get("terrain") or {}).get("shape") or {})
    kind = cfg.get("edge", "none")
    if kind == "none" or getattr(p, "play_v", None) is None:
        return []
    offset = cfg.get("edge_offset_m", (cfg.get("wall_m", 0.0) + 1.5) if cfg.get("wall_cm", 0) else 2.0)
    wet = np.zeros(p.field.shape, bool)
    for lk in p.lakes or []:
        wet |= lk["mask"]
    rng = np.random.default_rng(s["terrain"]["seed"] + 400)
    placed = barrier.place(p.field, p.play_v, offset, kind, rng, skip=wet)
    cells = (p.field.shape[0] - 1, p.field.shape[1] - 1)
    fp = footprints(s)
    for q in placed:
        q["rc"] = prefab.footprint_radius(fp, q, cells)
    return placed


def barrier_mask(p):
    """Vertices within 4 m of a barrier object: decoration keeps off."""
    m = np.zeros(p.field.shape, bool)
    g = terrain.GRID_CM
    for q in getattr(p, "barrier", None) or []:
        r, c = int(round(q["y"] / g)), int(round(q["x"] / g))
        k = int((q["rc"] + 400) / g) + 1
        m[max(0, r - k):r + k + 1, max(0, c - k):c + k + 1] = True
    return m


def close_ring(p, rounds=40):
    """Make sure no gentle ground leads out of the play area through the
    border. The client takes a cell's slope from its SW, SE and NW corners
    only (io_terrain.cpp:2038-2048): on a face rising toward the north-east
    those three sit on one contour and the cell reads flat while its NE
    corner is metres higher, so the face can be walked up diagonally (the
    NE-corner blind spot, FORMATS.md; seen on map 2: 28.1 / 27.0 / 28.8 m
    with the NE corner at 39.1 m). Each round finds the gentle cells in the
    face band (outside the play area, within the face's width) that a
    player can reach from the start, 8-way, and lifts their NW and SE
    corners to the steep slope measured from the ORIGINAL ground, once per
    corner: lifting from already-lifted corners compounded into a runaway.
    Returns the number of corners lifted; p.ring_open is True if a leak
    remains (reported by the border check)."""
    g = terrain.GRID_CM
    play = p.play_v
    f0 = p.field.astype(float).copy()
    band_m = ((p.shape_cfg or {}).get("wall_m", 10.0) if getattr(p, "shape_cfg", None) else 10.0) + 6.0
    dist = areas.distance_m(play)
    need = walk.BLOCK_SLOPE * 1.15 * g          # a margin over 54 degrees, cm per cell
    done = np.zeros(play.shape, bool)
    lifted = 0
    p.ring_open = False
    for _ in range(rounds):
        f = p.field.astype(float)
        gx, gy = terrain.gradient(f)
        gentle = np.hypot(gx, gy) < walk.BLOCK_SLOPE
        h, w = gentle.shape
        s0 = (min(p.start_v[0], h - 1), min(p.start_v[1], w - 1))
        if not gentle[s0]:
            return lifted
        seen = np.zeros_like(gentle)
        seen[s0] = True
        stack = [s0]
        while stack:
            r, c = stack.pop()
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    rr, cc = r + dr, c + dc
                    if 0 <= rr < h and 0 <= cc < w and gentle[rr, cc] and not seen[rr, cc]:
                        seen[rr, cc] = True
                        stack.append((rr, cc))
        leak = seen & ~play[:-1, :-1] & (dist[:-1, :-1] > 0)
        if not leak.any():
            return lifted
        front = leak & (dist[:-1, :-1] <= band_m)
        moved = 0
        for r, c in zip(*np.nonzero(front)):
            for rr, cc in ((r + 1, c), (r, c + 1)):                # NW and SE corners
                if rr < f.shape[0] and cc < f.shape[1] and not play[rr, cc] and not done[rr, cc]:
                    want = f0[r, c] + need
                    if f[rr, cc] < want:
                        f[rr, cc] = want
                        moved += 1
                    done[rr, cc] = True
        lifted += moved
        p.field = np.round(f.astype("<f4"), 1).astype("<f4")
        if not moved:
            p.ring_open = True
            return lifted
    p.ring_open = True
    return lifted


def refresh_water(s, p):
    """Re-derive every water outline after the ground changed near it (a
    village pad flattened by the shore): carved lakes keep their level and
    centre, the flood keeps its level and is re-filtered."""
    w = s.get("water") or {}
    fl = w.get("flood")
    kept_lakes = [lk for lk in p.lakes if not lk.get("flood")]
    p.lakes = []
    for lk in kept_lakes:
        lk = dict(lk, mask=water.lake_mask(p.field, lk["level"], lk["centre"]))
        p.lakes.append(lk)
        for name, f in p.features.items():
            if f["kind"] == "lake" and f["centre"] == lk["centre"]:
                f["mask"] = lk["mask"]
    if fl and getattr(p, "flood_level", None) is not None:
        region = play_vertices(s, p)
        p.field, level, kept = water.flood(p.field, region, level=p.flood_level,
                                           min_depth_cm=fl.get("min_depth_cm", 80.0),
                                           min_m2=fl.get("min_m2", 1200.0))
        union = np.zeros(p.field.shape, bool)
        for kb in kept:
            p.lakes.append({"centre": kb["centre"], "level": level, "mask": kb["mask"], "banks": {}, "flood": True})
            union |= kb["mask"]
        name = fl.get("name", "water")
        if name in p.features:
            p.features[name]["mask"] = union
    p.water = {}
    for lk in p.lakes:
        for key, rects in water.rects_for(lk["mask"], lk["level"], p.x0, p.y0, p.width, p.height).items():
            p.water.setdefault(key, []).extend(rects)


def road_end(p, name, path_block):
    """Lattice corner a road starts or ends at: a village's gate, the start
    spot, or a feature's centre."""
    for v in p.villages:
        if v["name"] == name:
            return free_corner(v["gate"], path_block)
    if name == "start":
        return free_corner((int(round(p.start_v[0] / 4)), int(round(p.start_v[1] / 4))), path_block)
    if name in p.features:
        r, c = p.features[name]["centre"]
        return free_corner((int(round(r / 4)), int(round(c / 4))), path_block)
    raise SystemExit("road end %r is neither a village, 'start' nor a feature (%s)"
                     % (name, ", ".join(sorted(p.features))))


def road_partners(s, name):
    out = []
    for road in s.get("roads", []):
        if road["from"] == name:
            out.append(road["to"])
        elif road["to"] == name:
            out.append(road["from"])
    return out


def partner_point(s, p, name):
    """Where a road partner is or will be (vertex), to aim a village's
    entrance at it before the partner is placed."""
    for v in p.villages:
        if v["name"] == name:
            return v["centre"]
    if name in p.features:
        return p.features[name]["centre"]
    for v in s.get("villages", []):
        if v.get("name", v["prefab"]) == name:
            if v.get("on") in p.features:
                return p.features[v["on"]]["centre"]
            if v.get("where"):
                return areas.resolve(v["where"], p.field.shape, p.features, frame=getattr(p, 'frame', None), play=getattr(p, 'play_v', None))[1]
    st = s.get("start") or {}
    if name == "start" and st.get("near"):
        return partner_point(s, p, st["near"]) if st["near"] != "start" else None
    return ((p.field.shape[0] - 1) / 2.0, (p.field.shape[1] - 1) / 2.0)


def add_villages(s, p):
    """Place each spec village: a flat corner-aligned site (inside its
    "where" area, or on its "on" hill), a flattened pad, the (turned)
    prefab, an entrance and a walk-grid route out through it. Fills
    p.villages and p.features; widens p.avoid."""
    rng = np.random.default_rng(s["terrain"]["seed"] + 300)
    band_m = ((s.get("terrain") or {}).get("ridge") or {}).get("width_m", 0)
    taken = []
    rows, cols = 16 * p.height + 1, 16 * p.width + 1
    if p.avoid is None:
        p.avoid = np.zeros(p.field.shape, bool)
    for v in s["villages"]:
        name = v.get("name", v["prefab"])
        pf = prefab.load(os.path.join(PREFAB_DIR, v["prefab"] + ".json"))
        k = int(rng.integers(4)) if v.get("rotation", "auto") == "auto" else int(v["rotation"]) // 90
        pfr = prefab.rotate(pf, k)
        pad_m = pf["radius_m"] + v.get("pad_margin_m", 6.0)
        if v.get("pad_from_walls"):
            cells0 = (p.field.shape[0] - 1, p.field.shape[1] - 1)
            pad_m = max(pad_m, prefab.wall_extent_m(pfr, footprints(s), cells0) + 4.0)
        skirt = v.get("skirt_m", 30.0)
        # By the water: only the pad keeps off the shore, and the pad stays
        # 1.5 m above the highest waterline (water is re-derived afterwards).
        near_water = bool(v.get("near_water")) and bool(p.lakes)
        reach = pad_m + 8.0 if near_water else None
        min_h = max(lk["level"] for lk in p.lakes) + 150.0 if near_water else None
        hard = p.avoid
        if near_water:
            # the water itself and other pads, not the start's keep-clear band
            hard = np.zeros(p.field.shape, bool)
            for lk in p.lakes:
                hard |= lk["mask"]
            hard |= getattr(p, "pads", hard)
        stilts = bool(pf.get("water"))
        if stilts:
            # A harbour village on stilts (it stood over water in retail):
            # over the water's edge, or over a pond of its own (place_stilts).
            centre, k, pfr, height, placed, brushes, site_how = place_stilts(s, p, v, pf, name, skirt, band_m,
                                                                             taken, k)
            near_water = True                # keeps its own (seabed, sand) paint
        else:
            site_how = None
            if v.get("on"):
                centre = snap4(p.features[v["on"]]["centre"])
            elif v.get("where"):
                where = v["where"]
                if "near" in where:      # "near the sea" is about the village's edge, not its centre
                    where = dict(where, extra_m=where.get("extra_m", 40.0) + pad_m + 8.0)
                core, edge, target = area_allow(where, p)
                centre = None
                for allow in (core, edge):
                    try:
                        centre = prefab.pick_site(p.field, pad_m, skirt, hard, band_m + 10, taken,
                                                  allow=allow, target=target, avoid_reach_m=reach)
                        break
                    except ValueError:
                        continue
                if centre is None:
                    raise SystemExit("village %s: no flat enough site in its area" % name)
            else:
                centre = prefab.pick_site(p.field, pad_m, skirt, p.avoid, band_m + 10, taken)
            p.field, height = prefab.flatten(p.field, centre, pad_m, skirt, min_height=min_h)
            if near_water:
                refresh_water(s, p)
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
        p.pads = pad_mask | getattr(p, "pads", np.zeros(p.field.shape, bool))
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
        # Entrance: the named member's bearing (spec "entrance"); else, when
        # roads lead here, the open arc nearest the first road partner; else
        # the middle of the widest open arc; else the side facing the map
        # centre. The path runs on the walk grid from the centre out through
        # it, and on along the roads (or to the start) from there.
        blocked = decorate.blocked_cells(placed, cells, fp)
        cr, cc = centre[0] // 4, centre[1] // 4
        arcs = prefab.openings(blocked, centre, pad_m)
        partners = road_partners(s, name)
        bearing = None
        if v.get("entrance"):
            named = [q for q in placed if q["name"] == v["entrance"]]
            if not named:
                raise SystemExit("village %s has no member named %r" % (name, v["entrance"]))
            g = terrain.GRID_CM
            bearing = math.degrees(math.atan2(np.mean([q["y"] for q in named]) - centre[0] * g,
                                              np.mean([q["x"] for q in named]) - centre[1] * g)) % 360
            how = "member %s" % v["entrance"]
        elif partners and arcs:
            pt = partner_point(s, p, partners[0])
            if pt is not None:
                want = math.degrees(math.atan2(pt[0] - centre[0], pt[1] - centre[1])) % 360
                bearing = prefab.arc_toward(arcs, want)
                how = "toward %s (%.0f deg)" % (partners[0], want)
        if bearing is None and near_water and arcs:
            # By the water and no road: leave through the opening onto the
            # largest stretch of ground a path can cross, so the way in (and
            # the start beside it) is on the mainland, not on a beach or a
            # steep-sided knoll. "Away from the nearest water" fails on a
            # peninsula.
            mine = {"brushes": brushes, "corner_block": cb}
            labels, sizes = path_land(s, p, extra_block=None)
            own = np.zeros_like(labels, bool)
            for r, c, b in brushes:
                if 0 <= r < own.shape[0] and 0 <= c < own.shape[1]:
                    own[r, c] = True
            best = None
            for a0, a1 in arcs:
                for step in range(0, (a1 - a0) % 360 + 1, 5):
                    bb = (a0 + step) % 360
                    d = (pad_m + 12.0) / 10.0
                    oc = (int(round(cr + math.sin(math.radians(bb)) * d)), int(round(cc + math.cos(math.radians(bb)) * d)))
                    if not (0 <= oc[0] < labels.shape[0] and 0 <= oc[1] < labels.shape[1]):
                        continue
                    lab = labels[oc] if not own[oc] and not cb[oc] else -1
                    size = sizes[lab] if lab >= 0 else 0
                    if best is None or size > best[0]:
                        best = (size, bb)
            if best and best[0] > 0:
                bearing, how = float(best[1]), "onto the mainland (%d path corners)" % best[0]
        if bearing is None and arcs:
            bearing, how = prefab.arc_middle(arcs[0]), "widest opening %d-%d deg" % arcs[0]
        elif bearing is None:
            bearing, how = math.degrees(math.atan2(rows / 2 - cr, cols / 2 - cc)) % 360, "facing the map centre"
        route = prefab.inner_route(blocked, centre, bearing, pad_m)
        if route is None:
            raise SystemExit("village %s: no walkable way out of the centre" % name)
        inner = prefab.route_corners(route)
        starts_here = (s.get("start") or {}).get("near") == name
        connect = v.get("connect", not partners and not starts_here)
        p.villages.append({"name": name, "prefab": v["prefab"], "centre": centre, "height": height,
                           "pad_m": pad_m, "turns": k, "members": placed, "brushes": brushes,
                           "corner_block": cb, "gate": inner[-1], "inner_path": inner, "route": route,
                           "entrance_deg": bearing, "entrance_how": how, "openings": arcs,
                           "connect": connect, "has_path": connect or bool(partners) or starts_here,
                           "near_water": near_water, "stilts": stilts, "site_how": site_how,
                           "water_level": height if stilts else None,
                           "landings": [(centre[1] * terrain.GRID_CM + x, centre[0] * terrain.GRID_CM + y)
                                        for x, y in (pfr.get("water") or {}).get("landings", [])] if stilts else []})
        p.features[name] = {"kind": "village", "centre": centre, "radius_m": pad_m}


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
    blocked = decorate.blocked_cells(members + (p.placed or []) + (getattr(p, "barrier", None) or []),
                                     (field.shape[0] - 1, field.shape[1] - 1),
                                     footprints(s))
    a = walk.analyse(field, start, play_mask(s, p), blocked, trap_scope=trap_scope(s, p))
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
        if float(hts.max() - hts.min()) > 5.0 and not q.get("stilts"):    # stilts stand in the seabed
            rough.append("%s#%d (%.0f cm)" % (q["name"], q["id"], float(hts.max() - hts.min())))
        reach = q["rc"] + 300
        cr0, cr1 = int((q["y"] - reach) // g), int((q["y"] + reach) // g) + 1
        cc0, cc1 = int((q["x"] - reach) // g), int((q["x"] + reach) // g) + 1
        home = a["home"][max(0, cr0):cr1 + 1, max(0, cc0):cc1 + 1]
        if not home.any():
            unreachable.append("%s#%d" % (q["name"], q["id"]))
    check(not rough, "ground under every village building is flat within 5 cm %s" % (rough[:3] or ""))
    wet_ramps = ["%s (%.1f m)" % (v["name"], (ground_at(field, x, y) - v["water_level"]) / 100)
                 for v in getattr(p, "villages", []) or [] if v.get("stilts")
                 for x, y in v["landings"] if ground_at(field, x, y) <= v["water_level"] + 30]
    if any(v.get("stilts") for v in getattr(p, "villages", []) or []):
        check(not wet_ramps, "every stilt village's ramp comes down on dry ground %s" % (wet_ramps or ""))
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
        if v.get("site_how"):
            print("          on stilts, %s" % v["site_how"])


# Retail borders, measured outside the walk region of eight zones (JG01-05,
# JD01-03, phase 7b): 5-10 m out 79-92% steep, 10-20 m out 41-64% (Gorge of
# Silence 28%), 18-30 m above the edge at 10-20 m (Kenji Beach 18). A
# generated border must be at least as closed as the more open retail ones.
RETAIL_BORDER = {"steep_5_10": 0.75, "steep_10_20": 0.35, "rise_10_20": 15.0}
# (steep_10_20 is no longer required since the third phase 7b review: a
# plateau top is flat on purpose; the closed-ring test replaces it.)


def gentle_escape_m(field, start, play):
    """How far (m) outside the play area gentle ground (< 54 deg, 8-connected,
    so diagonal steps between two steep cells count) reaches from `start`."""
    if play is None:
        return 0.0
    gx, gy = terrain.gradient(field)
    gentle = np.hypot(gx, gy) < walk.BLOCK_SLOPE
    h, w = gentle.shape
    seen = np.zeros_like(gentle)
    s0 = (min(start[0], h - 1), min(start[1], w - 1))
    stack = [s0]
    seen[s0] = True
    while stack:
        r, c = stack.pop()
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                rr, cc = r + dr, c + dc
                if 0 <= rr < h and 0 <= cc < w and gentle[rr, cc] and not seen[rr, cc]:
                    seen[rr, cc] = True
                    stack.append((rr, cc))
    d = areas.distance_m(play)[:-1, :-1]
    return float(d[seen].max()) if seen.any() else 0.0


def border_profile(field, start, wet=None):
    """The walk region (gentle cells joined to `start`, water counted as
    walkable, inner steep spots filled) and, outside it, the share of steep
    cells and the median rise above its edge at 0-5, 5-10, 10-20 and 20-40 m.
    The same measure as the retail survey."""
    gx, gy = terrain.gradient(field)
    steep = np.hypot(gx, gy) >= walk.BLOCK_SLOPE
    gentle = ~steep
    if wet is not None:
        gentle |= wet[:-1, :-1]
    s0 = (min(start[0], gentle.shape[0] - 1), min(start[1], gentle.shape[1] - 1))
    region = np.zeros_like(gentle)
    for rr, cc in water.components(gentle):
        m = np.zeros_like(gentle)
        m[rr, cc] = True
        if m[s0]:
            region = m
            break
    outside = np.zeros_like(region)
    for rr, cc in water.components(~region):
        if rr.min() == 0 or cc.min() == 0 or rr.max() == region.shape[0] - 1 or cc.max() == region.shape[1] - 1:
            outside[rr, cc] = True
    region = ~outside
    d, nr, nc = areas.nearest(region)
    cellf = field[:-1, :-1]
    rise = (cellf - cellf[nr, nc]) / 100.0
    out = {"share": float(region.mean())}
    for a, b in ((0, 5), (5, 10), (10, 20), (20, 40)):
        sel = (d > a) & (d <= b)
        out["steep_%d_%d" % (a, b)] = float(steep[sel].mean()) if sel.any() else 1.0
        out["rise_%d_%d" % (a, b)] = float(np.median(rise[sel])) if sel.any() else 99.0
    return out, region


_jg_cache = {}


def expected_by_kind(s, field, lattice):
    """{kind: (tiles) expected objects per 10 m tile with no cover area},
    from the catalogue's retail densities for each tile's slope band and
    brush, times the spec's global density (decorate.place without cover)."""
    dcfg = s.get("decorate") or {}
    if not dcfg:
        return {}
    cat = catalogue.load(os.path.join(STATS_DIR, dcfg["catalogue"]))
    objs = decorate.eligible(cat, dcfg)
    dens = decorate.densities(cat, objs)
    bands = np.clip(np.digitize(paint.corner_slopes(field), paint.SLOPE_BANDS) - 1, 0, len(paint.SLOPE_BANDS) - 2)
    kof = kind_of(s) or (lambda o: o["category"])
    rows, cols = lattice.shape
    out = {}
    b, k = bands[:rows - 1, :cols - 1], lattice[:rows - 1, :cols - 1]
    for i, o in enumerate(objs):
        out.setdefault(kof(o), np.zeros((rows - 1, cols - 1)))
        out[kof(o)] += dens[i][b, np.clip(k, 0, dens.shape[2] - 1)] * dcfg.get("density", 1.0)
    return out


def jg_per_ha():
    """Decoration per hectare of dry land, all retail JG zones together (profiles)."""
    if "v" not in _jg_cache:
        path = os.path.join(STATS_DIR, "jg_zone_profiles.json")
        with open(path, encoding="utf-8") as f:
            _jg_cache["v"] = json.load(f).get("jg_per_ha", {})
    return _jg_cache["v"]


def print_features(s, p, zone=None):
    """Where everything named ended up, with GM warp coordinates."""
    def mm(rc):
        return "/mm %s %d %d" % (zone if zone is not None else "<zone>",
                                 round((p.x0 * CHUNK_CM + rc[1] * terrain.GRID_CM) / 1000),
                                 round((p.y0 * CHUNK_CM + rc[0] * terrain.GRID_CM) / 1000))
    if not p.features and not getattr(p, "start_v", None):
        return
    print("  placed:")
    for name, f in sorted(p.features.items()):
        extra = ""
        if f["kind"] in ("lake", "water"):
            extra = "%d m2 of water" % int(f["mask"].sum() * (terrain.GRID_CM / 100) ** 2)
        elif f["kind"] == "village":
            v = next(v for v in p.villages if v["name"] == name)
            extra = "%s, pad %.1f m, entrance %.0f deg (%s)" % (v["prefab"], v["height"] / 100, v["entrance_deg"],
                                                                 v["entrance_how"])
        else:
            extra = "%s, %.0f m across, %.0f m high" % (f["kind"], 2 * f["radius_m"], f.get("height_cm", 0) / 100)
        print("     %-12s %-20s %s" % (name, mm(f["centre"]), extra))
    if getattr(p, "start_v", None):
        print("     %-12s %-20s" % ("start", mm(p.start_v)))
    if getattr(p, "ring_fixes", 0):
        print("  border: %d face corners lifted where the client would read the face as walkable "
              "(NE-corner blind spot)" % p.ring_fixes)
    if getattr(p, "coplanar_moved", 0) or getattr(p, "coplanar_left", 0):
        print("  coplanar faces: %d placement(s) moved 0.5-6 cm apart, %d pair(s) left"
              % (p.coplanar_moved, p.coplanar_left))


def intent_checks(s, p, field, placed, lattice, check):
    """Does the map do what the description asked? (phase 7)

    The spec's "intent" list comes from the layout compiler, one entry per
    claim the description made, each with the description's own words as
    its label. Run on the spec's build (preview) and on the files on disk
    (verify: field, decorations and tiles read back).
    """
    intents = s.get("intent") or []
    if not intents:
        return
    g2 = (terrain.GRID_CM / 100.0) ** 2
    play = play_vertices(s, p)
    wet = np.zeros(field.shape, bool)
    for lk in p.lakes or []:
        wet |= lk["mask"]
    pads = village_mask(p, 0.0)
    land = play & ~wet & ~pads
    kof = kind_of(s) or (lambda o: o["category"])
    pb = (s.get("paint") or {}).get("path_brush", 0)
    deco_off = np.zeros(field.shape, bool)
    if s.get("decorate"):
        dcfg = s["decorate"]
        deco_off = water_margin(s, p, dcfg) | village_mask(p, dcfg.get("village_clear_m", 4.0)) | barrier_mask(p)
        if getattr(p, "path_corners", None) is not None:
            pr, pc = np.nonzero(p.path_corners)
            k = int(dcfg.get("path_clear_m", 4.0) * 100 / terrain.GRID_CM) + 1
            for r, c in zip(pr * 4, pc * 4):
                deco_off[max(0, r - k):r + k + 1, max(0, c - k):c + k + 1] = True
    for it in intents:
        kind, label = it["check"], it.get("label", "")
        if kind == "tilt":
            dx, dy = terrain.direction(it["toward"])
            h, w = field.shape
            yy, xx = np.mgrid[0:h, 0:w]
            u = (xx / (w - 1) - 0.5) * dx + (yy / (h - 1) - 0.5) * dy
            span = abs(dx) * 0.5 + abs(dy) * 0.5
            # fit the ground's rise per unit of the map's span over the play
            # area (a shaped map's play area spans only part of the rectangle)
            sel = play & ~wet
            b = float(np.polyfit(u[sel] / (2 * span), field[sel], 1)[0]) if sel.any() else 0.0
            check(b >= it["min_rise_cm"], "%s: ground rises at %.1f m per map width toward the %s (want >= %.1f m)"
                  % (label, b / 100, it["toward"], it["min_rise_cm"] / 100))
        elif kind == "higher":
            wgt = areas.resolve(it["area"], field.shape, p.features, frame=getattr(p, 'frame', None), play=getattr(p, 'play_v', None))[0]
            inner, rest = (wgt >= 0.5) & ~wet, play & (wgt < 0.1) & ~wet
            diff = float(np.median(field[inner]) - np.median(field[rest]))
            check(diff >= it["min_cm"], "%s: %.1f m above the rest of the map (want >= %.1f m)"
                  % (label, diff / 100, it["min_cm"] / 100))
        elif kind == "water_share":
            share = float((wet & play).sum()) / float(play.sum())
            check(abs(share - it["target"]) <= it.get("tol", 0.05), "%s: %.0f%% of the map is water (want %.0f%% +/- %.0f)"
                  % (label, 100 * share, 100 * it["target"], 100 * it.get("tol", 0.05)))
        elif kind == "play_share":
            # the region a player can actually walk (slopes under 54 deg,
            # connected to the start, water included) as a share of the map
            # with every object's walls in place: the rim of boulders or the
            # fence line is what closes a low cliff
            # the region a player walks, measured as for retail: gentle cells
            # (water included) joined to the start, inner steep spots filled
            _, region = border_profile(field, p.start_v, wet)
            share = float(region.mean())
            check(abs(share - it["target"]) <= it.get("tol", 0.12),
                  "%s: %.0f%% of the map is walkable (want %.0f%% +/- %.0f; retail 24-55%% dry)"
                  % (label, 100 * share, 100 * it["target"], 100 * it.get("tol", 0.12)))
        elif kind == "border_like_retail":
            prof, _ = border_profile(field, p.start_v, wet)
            ok = prof["steep_5_10"] >= RETAIL_BORDER["steep_5_10"] and prof["rise_10_20"] >= RETAIL_BORDER["rise_10_20"]
            check(ok, "%s: outside the walk region %.0f%% of cells 5-10 m out are too steep to climb, %.1f m up at "
                      "10-20 m (want >= %.0f%%, %.0f m; retail 79-92%%, 18-30 m)"
                  % (label, 100 * prof["steep_5_10"], prof["rise_10_20"],
                     100 * RETAIL_BORDER["steep_5_10"], RETAIL_BORDER["rise_10_20"]))
            # and the ring is closed: gentle ground reachable from the start
            # (diagonal steps between corners included) stays in the play area
            esc = gentle_escape_m(field, p.start_v, getattr(p, "play_v", None))
            check(esc <= 6.0, "%s: walkable ground ends within %.1f m of the play area's edge (want <= 6 m)"
                  % (label, esc))
        elif kind == "wet_in":
            wgt = areas.resolve(it["area"], field.shape, p.features, frame=getattr(p, 'frame', None), play=getattr(p, 'play_v', None))[0]
            sel = (wgt >= 0.5) & play                     # the strip's playable part, not its cliffs
            share = float((wet & sel).sum()) / max(1, int(sel.sum()))
            check(share >= it["min"], "%s: %.0f%% of that strip is water (want >= %.0f%%)"
                  % (label, 100 * share, 100 * it["min"]))
        elif kind == "inside":
            wgt = areas.resolve(it["area"], field.shape, p.features, frame=getattr(p, 'frame', None), play=getattr(p, 'play_v', None))[0]
            f = p.features.get(it["feature"])
            ok = f is not None and float(wgt[f["centre"]]) >= it.get("min_weight", 0.3)
            check(ok, "%s: %s is where the description put it (area weight %.2f)"
                  % (label, it["feature"], float(wgt[f["centre"]]) if f else -1))
        elif kind == "density":
            wgt = areas.resolve(it["area"], field.shape, p.features, frame=getattr(p, 'frame', None), play=getattr(p, 'play_v', None))[0]
            # only ground decoration may use (not pads, paths, water margins,
            # barriers): a village in the forest is not a thin forest
            inner, outer = land & (wgt >= 0.8) & ~deco_off, land & (wgt < 0.1) & ~deco_off
            km = (getattr(p, "cover_mult", None) or {}).get(it["kind"])
            if km is not None:
                # compare with ground no cover area changed for this kind
                plain = np.kron(np.abs(km - 1.0) < 0.15, np.ones((4, 4), bool))[:field.shape[0], :field.shape[1]]
                if (outer & plain).sum() > 400:
                    outer = outer & plain
            n_in = n_out = 0
            for q in placed or []:
                if kof(q) != it["kind"]:
                    continue
                r = min(int(round(q["y"] / terrain.GRID_CM)), field.shape[0] - 1)
                c = min(int(round(q["x"] / terrain.GRID_CM)), field.shape[1] - 1)
                n_in += bool(inner[r, c])
                n_out += bool(outer[r, c])
            d_in = n_in / max(1e-6, inner.sum() * g2 / 1e4)          # per hectare
            # Reference: what this same ground would get with no cover area,
            # from retail's density per slope band and brush (decorate.densities)
            # over the area's core tiles. Retail's flat average would be unfair
            # to steep ground, where retail itself puts little grass.
            exp = expected_by_kind(s, field, lattice)
            core_t = inner[:-1:4, :-1:4] & inner[4::4, 4::4]
            expected = float(exp.get(it["kind"], np.zeros(core_t.shape))[core_t].sum())
            ratio = n_in / max(1e-6, expected)
            # What the area asked for there: its mean multiplier over the core
            # (fades and later areas included). At least half of that change
            # must show; counts are Poisson, so 2 sigma of slack ("2 trees
            # where 4 were due" proves nothing either way).
            mbar = float(km[:-1, :-1][core_t].mean()) if km is not None and core_t.any() else it["ratio"]
            want = 1 + 0.5 * (mbar - 1) if mbar >= 1 else mbar + 0.5 * (1 - mbar)
            due = want * expected
            slack = 2.0 * math.sqrt(max(due, 1.0))
            ok = n_in >= due - slack if mbar >= 1 else n_in <= due + slack
            check(ok, "%s: %d %s there = x%.2f what that ground gets by default (%.0f); the area asked x%.2f, want %s "
                      "x%.2f (%s %.0f with 2-sigma noise); %.1f/ha"
                  % (label, n_in, it["kind"].lower(), ratio, expected, mbar, ">=" if mbar >= 1 else "<=", want,
                     ">=" if mbar >= 1 else "<=", due - slack if mbar >= 1 else due + slack, d_in))
        elif kind == "road":
            ends = []
            for name in (it["from"], it["to"]):
                v = next((v for v in p.villages if v["name"] == name), None)
                if v is not None:
                    ends.append(v["gate"])
                elif name == "start":
                    ends.append((int(round(p.start_v[0] / 4)), int(round(p.start_v[1] / 4))))
                else:
                    r, c = p.features[name]["centre"]
                    ends.append((int(round(r / 4)), int(round(c / 4))))
            near = lambda q, e: max(abs(q[0] - e[0]), abs(q[1] - e[1])) <= 2
            todo = [tuple(q) for q in np.argwhere(lattice == pb) if near(tuple(q), ends[0])]
            seen = set()
            while todo:
                q = todo.pop()
                if q in seen:
                    continue
                seen.add(q)
                for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    n = (q[0] + d[0], q[1] + d[1])
                    if 0 <= n[0] < lattice.shape[0] and 0 <= n[1] < lattice.shape[1] and lattice[n] == pb:
                        todo.append(n)
            check(any(near(q, ends[1]) for q in seen), "%s: a path runs from %s to %s" % (label, it["from"], it["to"]))
        elif kind == "shore":
            f, lk = p.features[it["feature"]], p.features[it["lake"]]
            d = float(areas.distance_m(lk["mask"])[f["centre"]])
            bear = math.degrees(math.atan2(f["centre"][0] - lk["centre"][0], f["centre"][1] - lk["centre"][1]))
            dx, dy = terrain.direction(it["side"])
            off = abs((bear - math.degrees(math.atan2(dy, dx)) + 180) % 360 - 180)
            check(d <= it["max_m"] and off <= 60, "%s: %s is %.0f m from the water, %.0f deg off due %s of %s"
                  % (label, it["feature"], d, off, it["side"], it["lake"]))
        elif kind == "near":
            a = p.start_v if it["feature"] == "start" else p.features[it["feature"]]["centre"]
            own = 0.0 if it["feature"] == "start" else p.features[it["feature"]].get("radius_m", 0.0)
            bf = p.features[it["to"]]
            if "mask" in bf:                                  # water: distance to its edge
                d = float(areas.distance_m(bf["mask"])[tuple(int(v) for v in a)])
            else:
                d = math.hypot(a[0] - bf["centre"][0], a[1] - bf["centre"][1]) * terrain.GRID_CM / 100 \
                    - bf.get("radius_m", 0)
            d -= own                                          # from the feature's own edge
            check(d <= it["max_m"], "%s: %s is %.0f m from %s's edge (want <= %.0f m)"
                  % (label, it["feature"], max(0.0, d), it["to"], it["max_m"]))
        elif kind == "raised":
            f = p.features[it["feature"]]
            h, w = field.shape
            yy, xx = np.mgrid[0:h, 0:w]
            d = np.hypot(yy - f["centre"][0], xx - f["centre"][1]) * terrain.GRID_CM / 100 - f.get("radius_m", 0)
            ring = play & ~wet & (d >= 30) & (d <= 90)
            diff = float(field[f["centre"]] - np.median(field[ring]))
            check(diff >= it["min_cm"], "%s: %s stands %.1f m above its surroundings (want >= %.1f m)"
                  % (label, it["feature"], diff / 100, it["min_cm"] / 100))
        else:
            check(False, "unknown intent check %r" % kind)


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

    blocked = decorate.blocked_cells(placed + village_members(p) + (getattr(p, "barrier", None) or []),
                                     (field.shape[0] - 1, field.shape[1] - 1),
                                     footprints(s))
    a = walk.analyse(field, start, play_mask(s, p), blocked, trap_scope=trap_scope(s, p))
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
    """(row, col) of the start point in the field (computed once per build)."""
    v = getattr(p, "start_v", None)
    return v if v is not None else pick_start_vertex(s, p)


def pick_start_vertex(s, p):
    """The gentle spot nearest the start target, away from water and village
    pads: the spec's start.near (a village: just outside its entrance; any
    other feature: its centre) or start.area, else the map centre. Flat
    ground without terrain: the centre."""
    if p.field is None:
        return (p.height * 32, p.width * 32)
    st = s.get("start") or {}
    target = None
    allow = None
    if st.get("near"):
        v = next((v for v in p.villages if v["name"] == st["near"]), None)
        if v is not None:
            # In the village, on its walk route ~10 m in from the entrance:
            # flat (the pad), free of walls (the route avoids them), and
            # joined to the outside by the route's painted path. Retail
            # spawns players in towns too.
            want = max(0.0, v["pad_m"] - 10.0) * 100 / terrain.GRID_CM
            cr, cc = v["centre"]
            cell = min(v["route"], key=lambda q: abs(math.hypot(q[0] + 0.5 - cr, q[1] + 0.5 - cc) - want))
            return (int(cell[0]), int(cell[1]))
        elif st["near"] in p.features:
            target = p.features[st["near"]]["centre"]
        else:
            raise SystemExit("start.near %r is not a village or feature" % st["near"])
    elif st.get("area"):
        target = areas.resolve(st["area"], p.field.shape, p.features, frame=getattr(p, 'frame', None), play=getattr(p, 'play_v', None))[1]
    avoid = getattr(p, "avoid", None)
    if allow is not None:
        avoid = (~allow) if avoid is None else (avoid | ~allow)
    return terrain.pick_start(p.field, avoid=avoid, target=target)


def add_lakes(s, p):
    """Carve the spec's lakes into p.field, then flood (if asked); fill
    p.lakes, p.water and p.features.

    Each lake: {"name", "centre": "auto" | [fx, fy] (fractions of the map, x
    east, y north), or "where": area descriptor, "radius_m", "depth_cm",
    "shore_m", "shore_slope_deg", "irregularity", "ring_m", "margin_cm",
    "banks": {"west": "rocky", ...}}. "flood": {"name", "fraction" or
    "level_cm", ...} puts everything below one level under water
    (water.flood). p.avoid marks the water and a `keep_clear_m` band around
    it, where the start may not go.
    """
    w = s["water"]
    band_m = ((s.get("terrain") or {}).get("ridge") or {}).get("width_m", 0)
    rows, cols = p.field.shape
    if p.avoid is None:
        p.avoid = np.zeros(p.field.shape, bool)
    clear = int(w.get("keep_clear_m", 15) * 100 / terrain.GRID_CM)

    def keep_clear(mask):
        grown = mask.copy()
        for _ in range(clear):
            g = grown.copy()
            g[1:] |= grown[:-1]; g[:-1] |= grown[1:]; g[:, 1:] |= grown[:, :-1]; g[:, :-1] |= grown[:, 1:]
            grown = g
        p.avoid |= grown

    for i, lake in enumerate(w.get("lakes", [])):
        rng = np.random.default_rng(s["terrain"]["seed"] + 100 + i)
        lake_avoid = p.avoid
        if getattr(p, "play_v", None) is not None:
            # a lake and its ring stay inside the play area, off the cliffs
            lake_avoid = p.avoid | (areas.distance_m(~p.play_v) < lake["radius_m"] + lake.get("ring_m", 25.0) + 15.0)
        if lake.get("where"):
            core, edge, target = area_allow(lake["where"], p)
            centre = None
            for allow in (core, edge):
                try:
                    centre = water.pick_centre(p.field, lake["radius_m"], band_m + 10, lake.get("ring_m", 25.0),
                                               lake_avoid, allow=allow, target=target)
                    break
                except ValueError:
                    continue
            if centre is None:
                raise SystemExit("lake %s: no room in its area" % lake.get("name", i + 1))
        elif lake.get("centre", "auto") == "auto":
            centre = water.pick_centre(p.field, lake["radius_m"], band_m + 10,
                                       lake.get("ring_m", 25.0), p.avoid)
        else:
            fx, fy = lake["centre"]
            centre = (int(round(fy * (rows - 1))), int(round(fx * (cols - 1))))
        p.field, level = water.carve(p.field, lake, rng, centre)
        mask = water.lake_mask(p.field, level, centre)
        p.lakes.append({"centre": centre, "level": level, "mask": mask, "banks": lake.get("banks", {})})
        p.features[lake.get("name", "lake%d" % (i + 1))] = {"kind": "lake", "centre": centre,
                                                           "radius_m": lake["radius_m"], "mask": mask}
        keep_clear(mask)
    fl = w.get("flood")
    if fl:
        region = play_vertices(s, p)
        if getattr(p, "play_v", None) is not None:
            # water keeps a shore strip off the play area's edge, where the
            # cliff foot and any fence line stand on dry ground (an open
            # coastal edge is the map border, which this leaves alone)
            region = region & (areas.distance_m(~p.play_v) >= fl.get("edge_clear_m", 12.0))
        if fl.get("compact_m"):
            p.field, level, kept = water.flood_compact(p.field, region, fl["fraction"], smooth_m=fl["compact_m"],
                                                       dry=getattr(p, "play_v", None))
        else:
            p.field, level, kept = water.flood(p.field, region, fraction=fl.get("fraction"), level=fl.get("level_cm"),
                                               min_depth_cm=fl.get("min_depth_cm", 80.0),
                                               min_m2=fl.get("min_m2", 1200.0))
        union = np.zeros(p.field.shape, bool)
        p.flood_level = level
        for kb in kept:
            p.lakes.append({"centre": kb["centre"], "level": level, "mask": kb["mask"], "banks": {}, "flood": True})
            union |= kb["mask"]
        if kept:
            big = max(kept, key=lambda kb: int(kb["mask"].sum()))
            p.features[fl.get("name", "water")] = {"kind": "water", "centre": big["centre"], "radius_m": 0.0,
                                                    "mask": union}
        keep_clear(union)
    p.water = {}
    for lk in p.lakes:
        for key, rects in water.rects_for(lk["mask"], lk["level"], p.x0, p.y0, p.width, p.height).items():
            p.water.setdefault(key, []).extend(rects)


def play_vertices(s, p):
    """Vertices of the play region: the terrain shape, else inside the ridge band."""
    if getattr(p, "play_v", None) is not None:
        return p.play_v
    r = (s.get("terrain") or {}).get("ridge") or {}
    m = np.ones(p.field.shape, bool)
    if r.get("height_cm"):
        band = r.get("width_m", 0) * 100 / terrain.GRID_CM
        m &= terrain.edge_distance(p.field.shape, tuple(r.get("edges", terrain.EDGES))) >= band
    return m


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
        beach = near & (corners < L + w.get("beach_above_cm", 150))
        forced[beach] = sand
        # "a rocky west bank": rock instead of sand on that side of the lake
        rr, cc = np.mgrid[0:forced.shape[0], 0:forced.shape[1]]
        bear = np.degrees(np.arctan2(rr - lk["centre"][0] / 4.0, cc - lk["centre"][1] / 4.0))
        for side, style in (lk.get("banks") or {}).items():
            if style != "rocky":
                continue
            dx, dy = terrain.direction(side)
            want = math.degrees(math.atan2(dy, dx))
            off = np.abs((bear - want + 180) % 360 - 180)
            forced[beach & ~wet & (off <= w.get("bank_half_angle_deg", 50))] = w.get("rock_brush", 4)
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


def trap_scope(s, p):
    """Where traps are looked for: the play area on a shaped map (mountains
    are judged by steepness, border_like_retail), everywhere otherwise."""
    return play_mask(s, p) if getattr(p, "play_v", None) is not None else None


def play_mask(s, p):
    """Cells inside the play region (the terrain shape, else the ridge band):
    the area the connectivity check is about."""
    pv = getattr(p, "play_v", None)
    if pv is not None:
        return pv[:-1, :-1] & pv[1:, 1:] & pv[1:, :-1] & pv[:-1, 1:]
    rows, cols = p.height * 64, p.width * 64
    r = (s.get("terrain") or {}).get("ridge") or {}
    band = int(r.get("width_m", 0) * 100 / terrain.GRID_CM) if r.get("height_cm") else 0
    m = np.zeros((rows, cols), bool)
    m[band:rows - band, band:cols - band] = True
    return m


def terrain_checks(s, p, field, start, check):
    """The phase 2 terrain checks; `field` may come from the spec or from disk."""
    a = walk.analyse(field, start, play_mask(s, p), trap_scope=trap_scope(s, p))
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


def paint_coherence(lattice, field):
    """% of 4-neighbour corner pairs on the same brush: all land, and >= 45 deg."""
    sl = paint.corner_slopes(field)
    rows, cols = lattice.shape
    out = []
    for lo in (0.0, 45.0):
        st = (sl >= lo) & (lattice >= 0) & ~np.isin(lattice, (6, 7, 8))
        same = tot = 0
        for dr, dc in ((0, 1), (1, 0)):
            a = st[:rows - dr, :cols - dc] & st[dr:, dc:]
            same += int((lattice[:rows - dr, :cols - dc][a] == lattice[dr:, dc:][a]).sum())
            tot += int(a.sum())
        out.append(100.0 * same / max(1, tot))
    return out


def coherence_check(s, lattice, field, check):
    tgt = (s.get("paint") or {}).get("coherence_target")
    if not tgt or lattice is None:
        return
    a, b = paint_coherence(lattice, field)
    tol = tgt.get("tolerance", 6.0)
    check(a >= tgt["all"] - tol and b >= tgt["steep"] - tol,
          "paint in retail-sized patches: neighbouring corners agree %.1f%% (retail %.1f), %.1f%% on slopes >= 45 deg "
          "(retail %.1f; allow -%.0f)" % (a, tgt["all"], b, tgt["steep"], tol))


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
    intent_checks(s, p, p.field, p.placed, p.lattice, check)
    print_features(s, p)
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
                                    objects=(p.placed or []) + village_members(p) + (p.barrier or []))
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
        coherence_check(s, p.lattice, p.field, check)
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
        bkeys = {(round(q["x"] / 100), round(q["y"] / 100)) for q in (p.barrier or [])}
        is_barrier = [(round(q["x"] / 100), round(q["y"] / 100)) in bkeys for q in placed_disk]
        check(sum(is_barrier) == len(p.barrier or []), "barrier objects on disk = the spec's (%d)" % sum(is_barrier))
        deco_disk = [q for q, iv, ib in zip(placed_disk, in_village, is_barrier) if not iv and not ib]
        placed_disk = [q for q, ib in zip(placed_disk, is_barrier) if not ib]
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
        lat_disk, _ = tiles.lattice_votes(grid, ts, have)
        coherence_check(s, lat_disk, field, check)
        if s.get("intent"):
            deco_now = deco_disk if s.get("decorate") else None
            intent_checks(s, p, field, deco_now, lat_disk, check)

    if s.get("decorate") or s.get("villages"):
        # Coplanar placements flicker (CLAUDE.md, "Coplanar Placements
        # Flicker"); a lifted prefab on a flattened pad can create one.
        out = subprocess.run([sys.executable, os.path.join(HERE, "fix-coplanar-object-overlaps.py"),
                              "--zone", str(n), "--dry-run"], capture_output=True, text=True)
        mt = re.search(r"fighting-pairs=\s*(\d+)", out.stdout)
        pairs = int(mt.group(1)) if mt else -1
        check(pairs == 0, "no coplanar (flickering) object pairs (fix-coplanar-object-overlaps.py: %s)"
              % (pairs if pairs >= 0 else "did not run: " + (out.stderr.strip().splitlines() or ["?"])[-1]))
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


PROFILE_ZONES = {"JG01": 22, "JG02": 23, "JG03": 25, "JG04": 26, "JG05": 27, "JG06": 3, "JG07": 20, "JG08": 11}


def cmd_profiles():
    """Measure each retail JG zone for a layout's "like": median slope (and
    the terrain character whose generated slopes come closest), water
    share, and decoration per hectare of dry land by kind, also as a ratio
    to all JG zones together. Writes stats/jg_zone_profiles.json."""
    zstb, zstl, _ = tables()
    kinds_path = os.path.join(STATS_DIR, "jg_object_kinds.json")
    with open(kinds_path, encoding="utf-8") as f:
        kt = json.load(f)["kinds"]
    by_id = {i: k for k, ids in kt.items() for i in ids}
    cat = catalogue.load(os.path.join(STATS_DIR, "jg_decoration.json"))
    cat_of = {o["id"]: o["category"] for o in cat["objects"]}
    kinds = ["TREE", "STONE", "GRASS", "FLOWER", "PLANT", "MUSHROOM"]

    # what each terrain character's slopes look like once generated
    char_slope = {}
    for name, ch in layout.CHARACTER.items():
        t = {"seed": 1, "base_cm": 1000, "max_play_slope_deg": ch["max_slope"],
             "hills": {k: ch[k] for k in ("amplitude_cm", "wavelength_m", "octaves", "persistence")}}
        f = terrain.generate(4, 4, t, 1)
        gx, gy = terrain.gradient(f)
        char_slope[name] = float(np.median(np.degrees(np.arctan(np.hypot(gx, gy)))))

    out, totals = {}, {"ha": 0.0, **{k: 0 for k in kinds}}
    for folder, row in PROFILE_ZONES.items():
        zdir = os.path.dirname(P(zstb.get(row, COL_ZON).decode("latin-1")))
        field, (x0, y0) = prefab._zone_field(zdir)
        wet = np.zeros(field.shape, bool)
        counts = {k: 0 for k in kinds}
        objs = []
        for fn in os.listdir(zdir):
            if not fn.lower().endswith(".ifo"):
                continue
            with open(os.path.join(zdir, fn), "rb") as fh:
                m = ifo.parse(fh.read())
            o = m.lump(ifo.OCEAN)
            for sx, sz, sy, ex, ez, ey in (o.rects if o is not None else []):
                c0 = int(round((min(sx, ex) + 520000) / terrain.GRID_CM)) - x0 * 64
                c1 = int(round((max(sx, ex) + 520000) / terrain.GRID_CM)) - x0 * 64
                r0 = int(round((min(sy, ey) + 520000) / terrain.GRID_CM)) - y0 * 64
                r1 = int(round((max(sy, ey) + 520000) / terrain.GRID_CM)) - y0 * 64
                sub = field[max(0, r0):r1 + 1, max(0, c0):c1 + 1]
                with np.errstate(invalid="ignore"):
                    wet[max(0, r0):r1 + 1, max(0, c0):c1 + 1] |= sub < sz
            objs += m.lump(ifo.OBJECT) or []
        have = ~np.isnan(field)
        land = have & ~wet
        with np.errstate(invalid="ignore"):
            gx, gy = terrain.gradient(field)
            deg = np.degrees(np.arctan(np.hypot(gx, gy)))
        cell_land = land[:-1, :-1] & land[1:, :-1] & land[:-1, 1:] & ~np.isnan(deg)
        med = float(np.median(deg[cell_land]))
        for r in objs:
            k = by_id.get(r.obj_id, cat_of.get(r.obj_id))
            if k not in counts:
                continue
            vr = int(round((r.pos[1] + 520000) / terrain.GRID_CM)) - y0 * 64
            vc = int(round((r.pos[0] + 520000) / terrain.GRID_CM)) - x0 * 64
            if 0 <= vr < field.shape[0] and 0 <= vc < field.shape[1] and land[vr, vc]:
                counts[k] += 1
        ha = float(land.sum()) * (terrain.GRID_CM / 100) ** 2 / 1e4
        totals["ha"] += ha
        for k in kinds:
            totals[k] += counts[k]
        name = zstl.name(zstb.get(row, COL_STL).decode("latin-1")) if zstb.get(row, COL_STL) else folder
        out[folder] = {"zone_row": row, "name": name, "median_slope_deg": round(med, 2),
                       "character": min(char_slope, key=lambda c: abs(char_slope[c] - med)),
                       "water_share": round(float(wet.sum()) / float(have.sum()), 3),
                       "per_ha": {k: round(counts[k] / ha, 2) for k in kinds}}
    for folder, z in out.items():
        z["density_vs_jg"] = {k: round(z["per_ha"][k] / max(1e-6, totals[k] / totals["ha"]), 3) for k in kinds}
        print("  %-5s %-22s slope %4.1f deg -> %-7s water %4.1f%%  trees %.1f/ha (x%.2f) rocks %.1f/ha flowers %.1f/ha"
              % (folder, z["name"][:22], z["median_slope_deg"], z["character"], 100 * z["water_share"],
                 z["per_ha"]["TREE"], z["density_vs_jg"]["TREE"], z["per_ha"]["STONE"], z["per_ha"]["FLOWER"]))
    path = os.path.join(STATS_DIR, "jg_zone_profiles.json")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"_comment": "Retail JG zones measured by `mapgen-zone.py profiles` for layout files' \"like\". "
                               "character = the layout terrain character whose generated median slope is "
                               "closest; density_vs_jg = per-hectare density over all JG zones together.",
                   "character_median_slope_deg": {k: round(v, 2) for k, v in char_slope.items()},
                   "jg_per_ha": {k: round(totals[k] / totals["ha"], 3) for k in kinds},
                   "zones": out}, f, indent=1, ensure_ascii=False)
    print("wrote %s" % path)
    return 0


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
    pl = sub.add_parser("prefab-land", help="derive a dry-ground variant of a stilt village prefab")
    pl.add_argument("name")
    pl.add_argument("--from", dest="src", required=True)
    pv = sub.add_parser("preview")
    pv.add_argument("spec")
    cp = sub.add_parser("compile", help="show the spec a layout file compiles to")
    cp.add_argument("layout")
    cp.add_argument("--out")
    sub.add_parser("profiles", help="measure retail JG zones for layouts' \"like\"")
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
    if a.cmd == "prefab-land":
        src = prefab.load(os.path.join(PREFAB_DIR, a.src + ".json"))
        if not src.get("water"):
            raise SystemExit("%s does not stand on stilts" % a.src)
        lands = [prefab.load(os.path.join(PREFAB_DIR, f)) for f in sorted(os.listdir(PREFAB_DIR))
                 if f.endswith(".json") and f[:-5] != a.src]
        out = prefab.land_variant(src, lands)
        out["name"] = a.name
        out["_comment"] = ("Dry-ground variant of %s (stilt village) made by `mapgen-zone.py prefab-land`: deck "
                           "and stilt objects dropped, members at retail's land heights (prefab.land_variant)."
                           % a.src)
        prefab.save(os.path.join(PREFAB_DIR, a.name + ".json"), out)
        from collections import Counter
        print("wrote %s: %d of %d members kept; %s" % (a.name, len(out["members"]), len(src["members"]),
              ", ".join("%s x%d" % kv for kv in Counter(m["name"] for m in out["members"]).most_common())))
        return 0
    if a.cmd == "walk-selftest":
        print("Walkability checker on synthetic terrain with known answers:")
        fails = walk.selftest()
        print("  %s" % ("ALL PASSED" if not fails else "FAILED: %s" % fails))
        return 1 if fails else 0
    if a.cmd == "profiles":
        return cmd_profiles()
    if a.cmd == "compile":
        sp = load_spec(a.layout)
        sp.pop("_path", None)
        text = json.dumps(sp, indent=1, ensure_ascii=False) + "\n"
        if a.out:
            with open(a.out, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print("wrote %s" % a.out)
        else:
            sys.stdout.write(text)
        return 0
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
