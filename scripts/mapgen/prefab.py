"""Village prefabs: building clusters lifted from retail zones (phase 6).

A prefab is a retail cluster of village objects saved as relative data:

* each member's lump (IFO lump 1 decoration or lump 3 construction), object
  id, offset from the prefab centre, rotation, scale and height above the
  ORIGINAL ground (z - terrain there), plus a collision profile from its
  meshes (catalogue.collision_profile);
* the tile brushes painted under it (corner lattice offsets), so its plaza
  and yards come with it.

Rules for placing one:

* Only zones whose LIST_ZONE row names the same DECO and CNST ZSCs as the
  generated zone can donate: object ids are indices into those lists.
* Placement turns a prefab by a multiple of 90 degrees, so the lifted brush
  lattice turns with it exactly.
* The ground under the prefab is flattened to one pad height. Members keep
  their height above ground, so a sloped original loses its slope; prefer
  flat sources (extract reports the relief).
"""

import json
import math
import os
import re

import numpy as np

from . import catalogue, chunk, ifo, terrain, tiles

ORIGIN_CM = 520000
CHUNK_CM = 16000


def _zone_field(zdir):
    """Heightfield of a zone folder (row 0 = south) and its SW chunk slot."""
    found = {}
    for f in os.listdir(zdir):
        m = re.match(r"(\d+)_(\d+)\.him$", f, re.I)
        if m:
            with open(os.path.join(zdir, f), "rb") as fh:
                found[(int(m.group(1)), 64 - int(m.group(2)))] = chunk.parse_him(fh.read()).heights[::-1]
    xs, ys = [k[0] for k in found], [k[1] for k in found]
    x0, y0 = min(xs), min(ys)
    field = np.full(((max(ys) - y0 + 1) * 64 + 1, (max(xs) - x0 + 1) * 64 + 1), np.nan)
    for (x, y), h in found.items():
        field[(y - y0) * 64:(y - y0) * 64 + 65, (x - x0) * 64:(x - x0) * 64 + 65] = h
    return field, (x0, y0)


def _height(field, x0, y0, wx, wy):
    """Bilinear terrain height at world cm (absolute)."""
    vx, vy = wx / terrain.GRID_CM - x0 * 64, wy / terrain.GRID_CM - y0 * 64
    ix, iy = int(vx), int(vy)
    if not (0 <= iy < field.shape[0] - 1 and 0 <= ix < field.shape[1] - 1):
        return float("nan")
    fx, fy = vx - ix, vy - iy
    return float(field[iy, ix] * (1 - fx) * (1 - fy) + field[iy, ix + 1] * fx * (1 - fy)
                 + field[iy + 1, ix] * (1 - fx) * fy + field[iy + 1, ix + 1] * fx * fy)


def extract(data_dir, zone_dir, deco_zsc, cnst_zsc, deco_cat, centre_world, radius_m,
            keep_categories=("VILLAGE", "ETC"), tileset=None):
    """Prefab dict from the objects within `radius_m` of `centre_world` (absolute cm).

    Construction objects are always kept. Decorations are kept when their
    catalogue category is in `keep_categories`: grass, trees and rocks are
    left to the decoration pass.
    """
    field, (x0, y0) = _zone_field(zone_dir)
    dm, do = catalogue.read_zsc(os.path.join(data_dir, deco_zsc))
    cm, co = catalogue.read_zsc(os.path.join(data_dir, cnst_zsc))
    cats = {o["id"]: o for o in deco_cat["objects"]}
    cx, cy = centre_world
    r = radius_m * 100
    members = []
    for f in sorted(os.listdir(zone_dir)):
        if not f.lower().endswith(".ifo"):
            continue
        with open(os.path.join(zone_dir, f), "rb") as fh:
            m = ifo.parse(fh.read())
        for lump, recs in ((ifo.OBJECT, m.lump(ifo.OBJECT) or []), (ifo.CNST, m.lump(ifo.CNST) or [])):
            for rec in recs:
                wx, wy = rec.pos[0] + ORIGIN_CM, rec.pos[1] + ORIGIN_CM
                if math.hypot(wx - cx, wy - cy) > r:
                    continue
                if lump == ifo.OBJECT:
                    c = cats.get(rec.obj_id, {})
                    if c.get("category") not in keep_categories:
                        continue
                    meshes, objs, name = dm, do, c.get("source", "").split("\\")[-2:-1]
                else:
                    meshes, objs, name = cm, co, ["cnst"]
                if not 0 <= rec.obj_id < len(objs):
                    continue
                ground = _height(field, x0, y0, wx, wy)
                members.append({
                    "lump": "CNST" if lump == ifo.CNST else "OBJECT", "id": rec.obj_id,
                    "name": (name or ["?"])[0],
                    "dx": round(wx - cx, 1), "dy": round(wy - cy, 1),
                    "above_ground_cm": round(rec.pos[2] - ground, 1),
                    "rot": [round(v, 6) for v in rec.rot], "scale": [round(v, 4) for v in rec.scale],
                    "collision_profile": catalogue.collision_profile(data_dir, meshes, objs[rec.obj_id]),
                })
    # ground relief and painted brushes under the footprint
    g = terrain.GRID_CM
    vr0, vc0 = int((cy - r) / g) - y0 * 64, int((cx - r) / g) - x0 * 64
    sub = field[max(0, vr0):vr0 + int(2 * r / g) + 2, max(0, vc0):vc0 + int(2 * r / g) + 2]
    yy, xx = np.mgrid[0:sub.shape[0], 0:sub.shape[1]]
    inside = np.hypot((xx + max(0, vc0) + x0 * 64) * g - cx, (yy + max(0, vr0) + y0 * 64) * g - cy) <= r
    vals = sub[inside & ~np.isnan(sub)]
    brushes = []
    if tileset is not None:
        grid, have, (tx0, ty0) = tiles.load_zone_tiles(zone_dir)
        lattice, _ = tiles.lattice_votes(grid, tileset, have)
        ccr, ccc = round(cy / 1000.0) - ty0 * 16, round(cx / 1000.0) - tx0 * 16     # centre corner
        k = int(math.ceil(radius_m / 10.0))
        for dr in range(-k, k + 1):
            for dc in range(-k, k + 1):
                if math.hypot(dr, dc) * 10 > radius_m:
                    continue
                rr, cc = ccr + dr, ccc + dc
                if 0 <= rr < lattice.shape[0] and 0 <= cc < lattice.shape[1] and lattice[rr, cc] >= 0:
                    brushes.append([dr, dc, int(lattice[rr, cc])])
        centre_snap = (ccc + tx0 * 16) * 1000.0, (ccr + ty0 * 16) * 1000.0
    else:
        centre_snap = (cx, cy)
    # Member offsets relative to the corner the brushes are anchored to, so
    # objects and paint stay aligned after placement.
    for mb in members:
        mb["dx"] = round(mb["dx"] + cx - centre_snap[0], 1)
        mb["dy"] = round(mb["dy"] + cy - centre_snap[1], 1)
    return {"radius_m": radius_m, "members": members, "brushes": brushes,
            "ground_relief_cm": round(float(vals.max() - vals.min()), 1) if len(vals) else 0.0,
            "ground_mean_cm": round(float(vals.mean()), 1) if len(vals) else 0.0}


def save(path, prefab):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(prefab, f, indent=1, ensure_ascii=False)


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------- placement


def rotate(prefab, quarter_turns):
    """Members and brushes turned by quarter_turns * 90 degrees counter-clockwise (seen from above)."""
    k = quarter_turns % 4
    out = dict(prefab)
    yaw = k * math.pi / 2
    qz = (0.0, 0.0, math.sin(yaw / 2), math.cos(yaw / 2))
    mem = []
    for m in prefab["members"]:
        x, y = m["dx"], m["dy"]
        for _ in range(k):
            x, y = -y, x
        mem.append(dict(m, dx=x, dy=y, rot=list(_qmul(qz, m["rot"]))))
    br = []
    for dr, dc, b in prefab["brushes"]:
        for _ in range(k):
            dr, dc = dc, -dr                         # (row=y, col=x): (x, y) -> (-y, x)
        br.append([dr, dc, b])
    out["members"], out["brushes"] = mem, br
    return out


def _qmul(a, b):
    """Quaternion product a*b, (x, y, z, w): rotation b, then a about world axes."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def blocking_radius_of(member):
    return catalogue.blocking_radius(member["collision_profile"], member["scale"][0], member["above_ground_cm"])


def pick_site(field, pad_m, skirt_m, avoid, border_m, taken=()):
    """Corner-aligned centre vertex for a prefab: the lowest ground relief
    within pad + skirt, clear of `avoid` (vertex mask) and of `taken`
    (earlier sites as (row, col, reach_m)), inside `border_m`."""
    reach = (pad_m + skirt_m) * 100 / terrain.GRID_CM
    pad = int(math.ceil(reach + border_m * 100 / terrain.GRID_CM))
    rows, cols = field.shape
    best, best_score = None, None
    step = 4                                          # tile corners: brushes stay aligned
    for r in range(pad - pad % step, rows - pad, step):
        for c in range(pad - pad % step, cols - pad, step):
            if any(math.hypot(r - tr, c - tc) * terrain.GRID_CM / 100 < reach * terrain.GRID_CM / 100 + tm
                   for tr, tc, tm in taken):
                continue
            k = int(reach)
            win = field[r - k:r + k + 1:2, c - k:c + k + 1:2]
            yy, xx = np.mgrid[-k:k + 1:2, -k:k + 1:2]
            disc = np.hypot(yy, xx) <= reach
            if avoid[r - k:r + k + 1:2, c - k:c + k + 1:2][disc].any():
                continue
            vals = win[disc]
            score = float(vals.max() - vals.min())
            if best_score is None or score < best_score:
                best, best_score = (r, c), score
    if best is None:
        raise ValueError("no site for a prefab of %s m + %s m skirt" % (pad_m, skirt_m))
    return best


def flatten(field, centre, pad_m, skirt_m):
    """Ground inside pad_m set to one height (the mean there), blended back to
    the original over skirt_m with a smoothstep. Returns (field, pad height)."""
    rows, cols = field.shape
    yy, xx = np.mgrid[0:rows, 0:cols]
    d = np.hypot(yy - centre[0], xx - centre[1]) * terrain.GRID_CM / 100.0
    inside = d <= pad_m
    pad = float(np.round(field[inside].mean(), 1))
    t = np.clip((d - pad_m) / max(1e-6, skirt_m), 0.0, 1.0)
    w = 1.0 - t * t * (3 - 2 * t)
    out = field + (pad - field) * w
    return np.round(out.astype("<f4"), 1).astype("<f4"), pad


def instantiate(pf, centre, pad_height):
    """Placed members (x, y in field-local cm; z absolute) and brush corners."""
    g = terrain.GRID_CM
    cx, cy = centre[1] * g, centre[0] * g
    placed = []
    for m in pf["members"]:
        s = m["scale"][0]
        placed.append({"lump": m["lump"], "id": m["id"], "name": m["name"],
                       "x": cx + m["dx"], "y": cy + m["dy"], "z": pad_height + m["above_ground_cm"],
                       "lump": m["lump"],
                       "rot": tuple(m["rot"]), "scale": tuple(m["scale"]),
                       "rc": blocking_radius_of(m), "rv": 0.0, "category": "VILLAGE", "sink": m["above_ground_cm"]})
    cr, cc = centre[0] // 4, centre[1] // 4
    brushes = [(cr + dr, cc + dc, b) for dr, dc, b in pf["brushes"]]
    return placed, brushes


def footprint_radius(footprints, q, cells_shape):
    """Blocking radius (cm) of a placed member from its wall cells
    (catalogue.Footprints). The vertex profile (blocking_radius_of) misses
    tall flat walls: a fence plank has vertices only at its foot and top,
    and sunk 1 m neither is in the body band, so it read as 0."""
    local = np.zeros(cells_shape, bool)
    footprints.mark(local, q)
    rr, cc = np.nonzero(local)
    if not len(rr):
        return 0.0
    g = terrain.GRID_CM
    return float(np.max(np.hypot((cc + 0.5) * g - q["x"], (rr + 0.5) * g - q["y"]))) + g / 2


def openings(blocked, centre, pad_m, step_deg=1):
    """Arcs of bearings (degrees, 0 = east, counter-clockwise) along which a
    ray from the centre vertex reaches the pad edge without crossing a
    blocked cell. List of (first, last) inclusive, widest first; empty when
    the village is closed or its centre is walled in."""
    g = terrain.GRID_CM
    cy, cx = centre[0] * g, centre[1] * g
    n = 360 // step_deg
    free = np.zeros(n, bool)
    for i in range(n):
        a = math.radians(i * step_deg)
        ok = True
        for d in np.arange(0.0, pad_m * 100, g / 4):
            r, c = int((cy + math.sin(a) * d) // g), int((cx + math.cos(a) * d) // g)
            if not (0 <= r < blocked.shape[0] and 0 <= c < blocked.shape[1]) or blocked[r, c]:
                ok = False
                break
        free[i] = ok
    if free.all():
        return [(0, 360 - step_deg)]
    k = int(np.argmin(free))                          # start the scan on a closed bearing
    runs, cur = [], None
    for j in range(1, n + 1):
        i = (k + j) % n
        if free[i]:
            cur = (i, i) if cur is None else (cur[0], i)
        elif cur is not None:
            runs.append(cur)
            cur = None
    if cur is not None:
        runs.append(cur)
    width = lambda run: (run[1] - run[0]) % n + 1
    runs.sort(key=width, reverse=True)
    return [(a * step_deg, b * step_deg) for a, b in runs]


def arc_middle(run):
    a, b = run
    return (a + ((b - a) % 360) / 2.0) % 360


def inner_route(blocked, centre, bearing_deg, pad_m):
    """Walk-grid route (list of cells, centre first) from the village centre
    to the pad edge at `bearing_deg`, through free cells inside the pad. A
    diagonal step needs a free side cell, as in walk.py. None if there is no
    way out."""
    g = terrain.GRID_CM
    rows, cols = blocked.shape
    k = int(pad_m * 100 / g) + 1
    cr, cc = centre
    inside = lambda r, c: (0 <= r < rows and 0 <= c < cols and not blocked[r, c]
                           and math.hypot(r + 0.5 - cr, c + 0.5 - cc) * g <= pad_m * 100)
    cand = [(math.hypot(r + 0.5 - cr, c + 0.5 - cc), (r, c))
            for r in range(cr - k, cr + k) for c in range(cc - k, cc + k) if inside(r, c)]
    if not cand:
        return None
    src = min(cand)[1]                                # free cell nearest the centre
    a = math.radians(bearing_deg)
    d = pad_m * 100 - g
    tr, tc = cr + math.sin(a) * d / g, cc + math.cos(a) * d / g
    dst = min((math.hypot(r + 0.5 - tr, c + 0.5 - tc), (r, c)) for _, (r, c) in cand)[1]
    prev = {src: None}
    frontier = [src]
    while frontier and dst not in prev:
        nxt = []
        for r, c in frontier:
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    q = (r + dr, c + dc)
                    if (dr or dc) and q not in prev and inside(*q) and \
                            (not (dr and dc) or inside(r + dr, c) or inside(r, c + dc)):
                        prev[q] = (r, c)
                        nxt.append(q)
        frontier = nxt
    if dst not in prev:
        return None
    route, q = [], dst
    while q is not None:
        route.append(q)
        q = prev[q]
    return route[::-1]


def route_corners(route):
    """Tile corners (10 m lattice) along a walk-grid route, edge-connected:
    a diagonal step also takes a side corner (see paint.paint)."""
    out = []
    for r, c in route:
        p = (int(round((r + 0.5) / 4)), int(round((c + 0.5) / 4)))
        if out and p == out[-1]:
            continue
        if out and p[0] != out[-1][0] and p[1] != out[-1][1]:
            out.append((out[-1][0], p[1]))
        out.append(p)
    return out


def records(placed, x0, y0):
    """(decoration records by chunk, construction records by chunk) for placed members."""
    deco, cnst = {}, {}
    for q in placed:
        wx, wy = x0 * CHUNK_CM + q["x"], y0 * CHUNK_CM + q["y"]
        cx, cy = int(wx // CHUNK_CM), int(wy // CHUNK_CM)
        lx, ly = wx - cx * CHUNK_CM, wy - cy * CHUNK_CM
        is_cnst = q["lump"] == "CNST"
        rec = ifo.Record(name=b"", warp_id=0, event_id=0, obj_type=4 if is_cnst else 1, obj_id=q["id"],
                         map_x=int(lx // terrain.GRID_CM), map_y=63 - int(ly // terrain.GRID_CM),
                         rot=q["rot"], pos=(wx - ORIGIN_CM, wy - ORIGIN_CM, q["z"]), scale=q["scale"])
        (cnst if is_cnst else deco).setdefault((cx, cy), []).append(rec)
    return deco, cnst
