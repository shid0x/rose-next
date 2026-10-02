"""Decoration placement (phase 5): grass, flowers, trees and rocks from retail statistics.

For every 10 m tile:

    expected objects of type o = density_o(slope band, brush)
                                 * cluster(x, y) * density multiplier

density_o is retail's: placements of o per tile corner with that slope band
and brush, in the catalogue's zones. Slope and brush are taken as
independent per object, since the catalogue keeps their counts separately.
cluster() is low-frequency noise with mean 1, so objects gather into groves
and leave clearings instead of spreading evenly.

Each candidate draws:

* a random spot in its tile;
* a scale and a sink depth from the middle 50% of retail's placements of
  that object;
* a random yaw (retail: 87% of trees and 57% of rocks are yaw-only).

It is then rejected if it lands in water, on or near a path, in the start
area, or too close to a colliding object. Colliding objects (rocks; tree
trunks) keep `collision_gap_m` of free ground between them, so they never
wall in a pocket of terrain. The walkability check in `blocked_cells` +
walk.analyse proves it.

Records follow retail (phase 5 survey of 10,806 JG placements):

* stored in the chunk that contains the object;
* obj_type 1 and an empty name;
* map_x / map_y = (column, 63 - row) of the object's 2.5 m cell in its chunk
  (98% of retail);
* uniform scale.
"""

import math

import numpy as np

from . import ifo, terrain, walk
from .catalogue import blocking_radius
from .paint import SLOPE_BANDS, corner_slopes

ORIGIN_CM = 520000
CHUNK_CM = 16000


def eligible(cat, cfg):
    """Catalogue entries the spec allows: chosen categories, enough retail use,
    not excluded by id or by source keyword (water plants)."""
    cats = set(cfg.get("categories", ["GRASS", "TREE", "STONE"]))
    words = [w.upper() for w in cfg.get("exclude_sources", ["DUCKWEED", "LOTUS"])]
    skip = set(cfg.get("exclude_ids", []))
    return [o for o in cat["objects"]
            if o["category"] in cats and o["uses"] >= cfg.get("min_retail_uses", 10)
            and o["id"] not in skip and not any(w in o["source"].upper() for w in words)
            and not o["missing_meshes"] and "radius_cm" in o]


def densities(cat, objs, smoothing=50.0):
    """d[o, band, brush]: expected placements of each object per tile corner."""
    land = np.array(cat["land_corners"], float) + smoothing
    d = np.zeros((len(objs),) + land.shape)
    for i, o in enumerate(objs):
        u = float(o["uses"])
        pb = np.array(o["slope_bands"], float) / u
        pk = np.array(o["brushes"], float) / u
        d[i] = u * np.outer(pb, pk) / land
    return d


def _sample(qs, rng, default):
    """Uniform between the 25th and 75th percentiles (qs = p10/25/50/75/90)."""
    return float(rng.uniform(qs[1], qs[3])) if qs else default


def place(field, lattice, x0, y0, cat, cfg, seed, avoid_vertices, path_corners, start_vertex,
          area_mult=None, kind_of=None):
    """Return (records by chunk slot, placed list).

    avoid_vertices: vertex mask where nothing may stand (water + margin).
    path_corners: lattice mask of path corners. start_vertex: (row, col).
    area_mult: {kind: lattice-shaped multiplier} from the layout's cover
    areas (forest = more TREE there); kind_of(object) names an object's
    kind (catalogue category, or a finer one such as FLOWER).
    """
    rng = np.random.default_rng(seed)
    objs = eligible(cat, cfg)
    dens = densities(cat, objs)
    slopes = corner_slopes(field)
    bands = np.clip(np.digitize(slopes, SLOPE_BANDS) - 1, 0, len(SLOPE_BANDS) - 2)
    rows, cols = lattice.shape
    cluster = terrain.fbm((rows, cols), cfg.get("cluster_wavelength_m", 80.0) / 4.0, 3, 0.5, rng)
    cluster = np.clip(1.0 + cfg.get("cluster_strength", 0.8) * cluster / max(1e-6, np.abs(cluster).max()), 0.0, None)
    mult = cfg.get("density", 1.0)
    max_slope = cfg.get("max_slope_deg")
    if max_slope is not None:
        sgx, sgy = terrain.gradient(field)
        slope_deg = np.degrees(np.arctan(np.hypot(sgx, sgy)))
    kmult = np.ones((len(objs), rows, cols))
    if area_mult:
        for i, o in enumerate(objs):
            m = area_mult.get(kind_of(o) if kind_of else o["category"])
            if m is not None:
                kmult[i] = m

    grid_cm = terrain.GRID_CM
    W, H = (cols - 1) * 4 * grid_cm, (rows - 1) * 4 * grid_cm         # map size, cm
    sr, sc = start_vertex
    start_xy = (sc * grid_cm, sr * grid_cm)
    start_clear = cfg.get("start_clear_m", 15.0) * 100
    path_clear = cfg.get("path_clear_m", 4.0) * 100
    gap = cfg.get("collision_gap_m", 3.0) * 100
    grass_gap = cfg.get("min_spacing_m", 1.5) * 100
    pr, pc = np.nonzero(path_corners)
    path_xy = np.stack([pc * 4 * grid_cm, pr * 4 * grid_cm], axis=1) if len(pr) else np.zeros((0, 2))

    bucket = {}                                     # 10 m spatial hash of placed objects
    placed = []

    def near(x, y, reach):
        k = int(math.ceil(reach / 1000.0))
        bx, by = int(x // 1000), int(y // 1000)
        for i in range(bx - k, bx + k + 1):
            for j in range(by - k, by + k + 1):
                yield from bucket.get((i, j), ())

    def height(x, y):
        vx, vy = x / grid_cm, y / grid_cm
        ix, iy = min(int(vx), field.shape[1] - 2), min(int(vy), field.shape[0] - 2)
        fx, fy = vx - ix, vy - iy
        return float(field[iy, ix] * (1 - fx) * (1 - fy) + field[iy, ix + 1] * fx * (1 - fy)
                     + field[iy + 1, ix] * (1 - fx) * fy + field[iy + 1, ix + 1] * fx * fy)

    for r in range(rows - 1):
        for c in range(cols - 1):
            b, k = bands[r, c], lattice[r, c]
            lam_o = dens[:, b, k] * cluster[r, c] * mult * kmult[:, r, c]
            lam = float(lam_o.sum())
            if lam <= 0:
                continue
            for _ in range(rng.poisson(lam)):
                o = objs[int(rng.choice(len(objs), p=lam_o / lam))]
                x = (c + rng.random()) * 4 * grid_cm
                y = (r + rng.random()) * 4 * grid_cm
                s = _sample(o["scale"], rng, 1.0)
                sink = _sample(o["sink_cm"], rng, 0.0)
                rc = blocking_radius(o.get("collision_profile", []), s, sink)
                rv = o["radius_cm"] * s
                if not (0 < x < W and 0 < y < H):
                    continue
                if avoid_vertices[int(round(y / grid_cm)), int(round(x / grid_cm))]:
                    continue
                if max_slope is not None and o["category"] != "STONE":
                    sr_ = min(int(y // grid_cm), slope_deg.shape[0] - 1)
                    sc_ = min(int(x // grid_cm), slope_deg.shape[1] - 1)
                    if slope_deg[sr_, sc_] > max_slope:
                        continue
                if math.hypot(x - start_xy[0], y - start_xy[1]) < start_clear + rc:
                    continue
                if len(path_xy) and np.min(np.hypot(path_xy[:, 0] - x, path_xy[:, 1] - y)) < path_clear + rc:
                    continue
                ok = True
                for q in near(x, y, max(rc, rv, 2000) + gap + 2000):
                    d = math.hypot(q["x"] - x, q["y"] - y)
                    if rc and q["rc"] and d < rc + q["rc"] + gap:
                        ok = False                  # colliders keep a walkable gap
                    elif (rc or q["rc"]) and d < max(rc, q["rc"]):
                        ok = False                  # nothing stands inside a collider
                    elif d < grass_gap:
                        ok = False
                    if not ok:
                        break
                if not ok:
                    continue
                z = height(x, y) + sink
                yaw = rng.uniform(0, 2 * math.pi)
                q = {"id": o["id"], "lump": "OBJECT", "x": x, "y": y, "z": z, "scale": (s, s, s), "yaw": yaw,
                     "rot": (0.0, 0.0, math.sin(yaw / 2), math.cos(yaw / 2)), "rc": rc, "rv": rv,
                     "sink": sink, "category": o["category"]}
                placed.append(q)
                bucket.setdefault((int(x // 1000), int(y // 1000)), []).append(q)

    cap = cfg.get("max_per_chunk")
    if cap:
        # Never more than retail's busiest chunk: thin over-full chunks by
        # removing small non-colliding objects (grass, flowers) at random,
        # evenly across the chunk, so a dense field keeps its look.
        by_chunk = {}
        for i, q in enumerate(placed):
            by_chunk.setdefault((int(q["x"] // CHUNK_CM), int(q["y"] // CHUNK_CM)), []).append(i)
        drop = set()
        for key in sorted(by_chunk):
            idx = by_chunk[key]
            extra = len(idx) - cap
            if extra > 0:
                # grass and flowers first: many trees (palms, leafy crowns)
                # have no ground collision either, and a dense forest's
                # chunks lost their trees to the cap
                small = [i for i in idx if not placed[i]["rc"] and placed[i]["category"] == "GRASS"]
                if len(small) < extra:
                    small += [i for i in idx if not placed[i]["rc"] and placed[i]["category"] != "GRASS"]
                take = small[:len(small)] if len(small) <= extra else \
                    [small[j] for j in sorted(rng.choice(len(small), size=extra, replace=False))]
                drop.update(int(i) for i in take)
        placed = [q for i, q in enumerate(placed) if i not in drop]
    return records_by_chunk(placed, x0, y0), placed


def records_by_chunk(placed, x0, y0):
    out = {}
    for q in placed:
        wx, wy = x0 * CHUNK_CM + q["x"], y0 * CHUNK_CM + q["y"]
        cx, cy = int(wx // CHUNK_CM), int(wy // CHUNK_CM)
        lx, ly = wx - cx * CHUNK_CM, wy - cy * CHUNK_CM
        rec = ifo.Record(name=b"", warp_id=0, event_id=0, obj_type=1, obj_id=q["id"],
                         map_x=int(lx // terrain.GRID_CM), map_y=63 - int(ly // terrain.GRID_CM),
                         rot=q["rot"], pos=(wx - ORIGIN_CM, wy - ORIGIN_CM, q["z"]),
                         scale=q["scale"])
        out.setdefault((cx, cy), []).append(rec)
    return out


def placed_from_records(records_by_chunk_, x0, y0, cat, field):
    """Rebuild the placed list from IFO records (for verify, from disk).
    The sink is re-derived as record z minus the terrain height there."""
    by_id = {o["id"]: o for o in cat["objects"]}
    g = terrain.GRID_CM
    out = []
    for recs in records_by_chunk_.values():
        for r in recs:
            o = by_id.get(r.obj_id, {})
            s = r.scale[0]
            x = r.pos[0] + ORIGIN_CM - x0 * CHUNK_CM
            y = r.pos[1] + ORIGIN_CM - y0 * CHUNK_CM
            vx, vy = x / g, y / g
            ix, iy = min(int(vx), field.shape[1] - 2), min(int(vy), field.shape[0] - 2)
            fx, fy = vx - ix, vy - iy
            ground = float(field[iy, ix] * (1 - fx) * (1 - fy) + field[iy, ix + 1] * fx * (1 - fy)
                           + field[iy + 1, ix] * (1 - fx) * fy + field[iy + 1, ix + 1] * fx * fy)
            sink = r.pos[2] - ground
            out.append({"id": r.obj_id, "lump": "OBJECT", "x": x, "y": y, "z": r.pos[2], "scale": tuple(r.scale),
                        "rot": tuple(r.rot), "sink": sink,
                        "rc": blocking_radius(o.get("collision_profile", []), s, sink),
                        "rv": o.get("radius_cm", 0.0) * s, "category": o.get("category", "?"), "rec": r})
    return out


def repair(placed, field, start, play_mask, max_rounds=20, fixed=(), footprints=None, trap_scope=None,
           links=()):
    """Remove colliding objects next to any trap cell until there is none.

    A tree on a >= 54 degree slope can block the only downhill way out of a
    steep cell, a real trap under the client's step rule. `fixed` colliders
    (village members) count as obstacles but are never removed. Returns
    (kept, removed count).
    """
    removed = 0
    g = terrain.GRID_CM
    for _ in range(max_rounds):
        blocked = blocked_cells(list(placed) + list(fixed), (field.shape[0] - 1, field.shape[1] - 1), footprints)
        a = walk.analyse(field, start, play_mask, blocked, trap_scope=trap_scope, links=links)
        if not a["trap_cells"]:
            return placed, removed
        tr, tc = np.nonzero(a["traps"])
        txy = np.stack([(tc + 0.5) * g, (tr + 0.5) * g], axis=1)
        keep = []
        for q in placed:
            near = np.min(np.hypot(txy[:, 0] - q["x"], txy[:, 1] - q["y"]))
            if footprints is not None:
                # by its own wall cells: "rc" misses tall flat walls
                hit = False
                if near < 2000:
                    own = np.zeros_like(blocked)
                    footprints.mark(own, q)
                    orr, occ = np.nonzero(own)
                    hit = len(orr) > 0 and np.min(np.hypot(orr[:, None] - tr[None, :],
                                                           occ[:, None] - tc[None, :])) <= 2
            else:
                hit = q["rc"] and near < q["rc"] + 2 * g
            if hit:
                removed += 1
            else:
                keep.append(q)
        placed = keep
    return placed, removed


def blocked_cells(placed, cells_shape, footprints=None):
    """2.5 m cells a colliding object blocks.

    With `footprints` (catalogue.Footprints): the cells its near-vertical
    colliding geometry covers between 0.25 and 2.5 m above the ground.
    Without: a disc of its blocking radius (coarser; houses then box in free
    cells between them)."""
    blocked = np.zeros(cells_shape, bool)
    g = terrain.GRID_CM
    for q in placed:
        if footprints is not None:
            # Every object, not only those with a radius: the vertex profile
            # behind "rc" misses tall flat walls (fences).
            footprints.mark(blocked, q)
            continue
        if not q["rc"]:
            continue
        r0, r1 = int((q["y"] - q["rc"]) // g), int((q["y"] + q["rc"]) // g) + 1
        c0, c1 = int((q["x"] - q["rc"]) // g), int((q["x"] + q["rc"]) // g) + 1
        for r in range(max(0, r0), min(cells_shape[0], r1 + 1)):
            for c in range(max(0, c0), min(cells_shape[1], c1 + 1)):
                if math.hypot((c + 0.5) * g - q["x"], (r + 0.5) * g - q["y"]) <= q["rc"]:
                    blocked[r, c] = True
    return blocked
