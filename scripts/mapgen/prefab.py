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
    out = {"radius_m": radius_m, "members": members, "brushes": brushes,
           "ground_relief_cm": round(float(vals.max() - vals.min()), 1) if len(vals) else 0.0,
           "ground_mean_cm": round(float(vals.mean()), 1) if len(vals) else 0.0}
    wat = _source_water(zone_dir, field, x0, y0, centre_snap, brushes)
    if wat:
        wat["landings"] = _landings(data_dir, dm, do, members, field, x0, y0, centre_snap, wat["level_cm"])
        # A harbour village: part of it stood over the water on stilts.
        # Members keep their height above the WATER, which is what a deck on
        # stilts is built to (their height above the seabed is incidental).
        out["water"] = wat
        for mb in members:
            mb["above_water_cm"] = round(mb["above_ground_cm"] + _height(field, x0, y0, centre_snap[0] + mb["dx"],
                                                                        centre_snap[1] + mb["dy"]) - wat["level_cm"], 1)
    return out


def _landings(data_dir, meshes, objs, members, field, x0, y0, centre_snap, level):
    """Where a stilt village's ramps come down to the shore: the feet of
    sloped walking faces near the waterline (within 1.5 m of it) whose
    source ground stood above the water. Other feet near the waterline
    (steps down into the water) stood over water and are not landings.
    Offsets from the prefab centre, cm."""
    out = []
    for m in members:
        if m["lump"] != "OBJECT" or not 0 <= m["id"] < len(objs):
            continue
        tris = []
        parts = objs[m["id"]]["parts"]
        for i, part in enumerate(parts):
            if not 0 <= part["mesh"] < len(meshes):
                continue
            t = catalogue.mesh_triangles(data_dir, meshes[part["mesh"]])
            if t is not None and len(t):
                tris.append(catalogue._to_model(parts, i, t.reshape(-1, 3)))
        if not tris:
            continue
        aw = m["above_ground_cm"] + _height(field, x0, y0, centre_snap[0] + m["dx"], centre_snap[1] + m["dy"]) - level
        w = ((np.concatenate(tris) * np.array(m["scale"])) @ catalogue._quat(m["rot"])
             + np.array([m["dx"], m["dy"], aw])).reshape(-1, 3, 3)
        nrm = np.cross(w[:, 1] - w[:, 0], w[:, 2] - w[:, 0])
        ln = np.linalg.norm(nrm, axis=1)
        ok = ln > 1e-6
        nz = np.abs(np.where(ok, nrm[:, 2] / np.where(ok, ln, 1.0), 0.0))
        low = w[:, :, 2].min(1)
        foot = ok & (nz > 0.5) & (nz < 0.97) & (low > -150) & (low < 150)
        if not foot.any():
            continue
        pts = w[foot].reshape(-1, 3)
        pts = pts[pts[:, 2] < 150]
        fx, fy = float(pts[:, 0].mean()), float(pts[:, 1].mean())
        if _height(field, x0, y0, centre_snap[0] + fx, centre_snap[1] + fy) > level:
            out.append([round(fx, 1), round(fy, 1)])
    return out


def _source_water(zone_dir, field, x0, y0, centre_snap, brushes):
    """The retail water under a prefab: which of its brush corners were wet
    (ground below a covering water rectangle), the waterline, and how far
    its dry ground stood above it and its wet ground below. None if dry."""
    level = np.full(field.shape, np.nan)
    for f in os.listdir(zone_dir):
        if not f.lower().endswith(".ifo"):
            continue
        with open(os.path.join(zone_dir, f), "rb") as fh:
            o = ifo.parse(fh.read()).lump(ifo.OCEAN)
        for sx, sz, sy, ex, ez, ey in (o.rects if o is not None else []):
            c0 = int(round((min(sx, ex) + ORIGIN_CM) / terrain.GRID_CM)) - x0 * 64
            c1 = int(round((max(sx, ex) + ORIGIN_CM) / terrain.GRID_CM)) - x0 * 64
            r0 = int(round((min(sy, ey) + ORIGIN_CM) / terrain.GRID_CM)) - y0 * 64
            r1 = int(round((max(sy, ey) + ORIGIN_CM) / terrain.GRID_CM)) - y0 * 64
            level[max(0, r0):r1 + 1, max(0, c0):c1 + 1] = sz
    wet, dry, levels = [], [], []
    for dr, dc, _b in brushes:
        wx, wy = centre_snap[0] + dc * 1000.0, centre_snap[1] + dr * 1000.0
        vr, vc = int(round(wy / terrain.GRID_CM)) - y0 * 64, int(round(wx / terrain.GRID_CM)) - x0 * 64
        if not (0 <= vr < field.shape[0] and 0 <= vc < field.shape[1]) or np.isnan(field[vr, vc]):
            continue
        if not np.isnan(level[vr, vc]) and field[vr, vc] < level[vr, vc]:
            wet.append((dr, dc, float(field[vr, vc])))
            levels.append(float(level[vr, vc]))
        else:
            dry.append(float(field[vr, vc]))
    if len(wet) < 3:
        return None
    L = float(np.median(levels))
    return {"level_cm": round(L, 1), "wet_corners": [[dr, dc] for dr, dc, _ in wet],
            "wet_ground_below_water_cm": round(L - float(np.median([h for _, _, h in wet])), 1),
            "dry_ground_above_water_cm": round(float(np.median(dry)) - L, 1) if dry else 100.0}


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
    if prefab.get("water"):
        wc = []
        for dr, dc in prefab["water"]["wet_corners"]:
            for _ in range(k):
                dr, dc = dc, -dr
            wc.append([dr, dc])
        lands = []
        for x, y in prefab["water"].get("landings", []):
            for _ in range(k):
                x, y = -y, x
            lands.append([x, y])
        out["water"] = dict(prefab["water"], wet_corners=wc, landings=lands)
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


def pick_site(field, pad_m, skirt_m, avoid, border_m, taken=(), allow=None, target=None, target_cm_per_m=3.0,
              avoid_reach_m=None):
    """Corner-aligned centre vertex for a prefab: the lowest ground relief
    within pad + skirt, clear of `avoid` (vertex mask) and of `taken`
    (earlier sites as (row, col, reach_m)), inside `border_m`. With `allow`
    (vertex mask) the centre must lie in it; with `target` (vertex), each
    metre away from it costs `target_cm_per_m` of relief. `avoid_reach_m`
    (default: pad + skirt) is how far from the centre `avoid` must be clear:
    a village by the water keeps only its pad off the shore."""
    reach = (pad_m + skirt_m) * 100 / terrain.GRID_CM
    pad = int(math.ceil(reach + border_m * 100 / terrain.GRID_CM))
    rows, cols = field.shape
    best, best_score = None, None
    step = 4                                          # tile corners: brushes stay aligned
    for r in range(pad - pad % step, rows - pad, step):
        for c in range(pad - pad % step, cols - pad, step):
            if allow is not None and not allow[r, c]:
                continue
            if any(math.hypot(r - tr, c - tc) * terrain.GRID_CM / 100 < reach * terrain.GRID_CM / 100 + tm
                   for tr, tc, tm in taken):
                continue
            k = int(reach)
            win = field[r - k:r + k + 1:2, c - k:c + k + 1:2]
            yy, xx = np.mgrid[-k:k + 1:2, -k:k + 1:2]
            disc = np.hypot(yy, xx) <= reach
            if avoid_reach_m is None:
                if avoid[r - k:r + k + 1:2, c - k:c + k + 1:2][disc].any():
                    continue
            else:
                ka = int(avoid_reach_m * 100 / terrain.GRID_CM)
                yy2, xx2 = np.mgrid[-ka:ka + 1, -ka:ka + 1]
                if avoid[r - ka:r + ka + 1, c - ka:c + ka + 1][np.hypot(yy2, xx2) <= ka].any():
                    continue
            vals = win[disc]
            score = float(vals.max() - vals.min())
            if target is not None:
                score += target_cm_per_m * math.hypot(r - target[0], c - target[1]) * terrain.GRID_CM / 100
            if best_score is None or score < best_score:
                best, best_score = (r, c), score
    if best is None:
        raise ValueError("no site for a prefab of %s m + %s m skirt" % (pad_m, skirt_m))
    return best


def flatten(field, centre, pad_m, skirt_m, min_height=None):
    """Ground inside pad_m set to one height (the mean there, at least
    `min_height`: a pad by the water stays dry), blended back to the
    original over skirt_m with a smoothstep. Returns (field, pad height)."""
    rows, cols = field.shape
    yy, xx = np.mgrid[0:rows, 0:cols]
    d = np.hypot(yy - centre[0], xx - centre[1]) * terrain.GRID_CM / 100.0
    inside = d <= pad_m
    pad = float(np.round(field[inside].mean(), 1))
    if min_height is not None:
        pad = max(pad, float(np.round(min_height, 1)))
    t = np.clip((d - pad_m) / max(1e-6, skirt_m), 0.0, 1.0)
    w = 1.0 - t * t * (3 - 2 * t)
    out = field + (pad - field) * w
    return np.round(out.astype("<f4"), 1).astype("<f4"), pad


DECK_OBJECTS = ("pad01", "pad02", "pad03", "pad04", "pad06", "padplan")


def land_variant(pf, land_prefabs, stack_m=0.8):
    """A stilt village rebuilt on dry ground (DESIGN.md, Villages): the deck
    and stilt objects (DECK_OBJECTS) are dropped; every other member stands
    on the ground at the height retail gives that object on land (median
    above-ground of the same object in `land_prefabs`, e.g. Sunshine Coast's
    huts at -9 to -26 cm; 0 if never seen on land). A member stacked on
    another (within `stack_m` horizontally and higher, as crates on crates)
    keeps its height over the one beneath."""
    land = {}
    for lp in land_prefabs:
        if lp.get("water"):
            continue
        for m in lp["members"]:
            land.setdefault((m["lump"], m["id"]), []).append(m["above_ground_cm"])
    cnst_default = float(np.median([v for (lump, _), vs in land.items() if lump == "CNST" for v in vs] or [-15.0]))
    keep = [m for m in pf["members"] if m["name"] not in DECK_OBJECTS]
    keep.sort(key=lambda m: m["above_water_cm"])
    out = []
    for m in keep:
        base = land.get((m["lump"], m["id"]))
        h = float(np.median(base)) if base else (cnst_default if m["lump"] == "CNST" else 0.0)
        under = [q for q in out if math.hypot(q["dx"] - m["dx"], q["dy"] - m["dy"]) <= stack_m * 100
                 and m["above_water_cm"] - q["_aw"] > 30]
        if under:
            q = max(under, key=lambda q: q["_aw"])
            h = q["above_ground_cm"] + (m["above_water_cm"] - q["_aw"])
        n = {k: v for k, v in m.items() if k != "above_water_cm"}
        n["above_ground_cm"] = round(h, 1)
        n["_aw"] = m["above_water_cm"]
        out.append(n)
    for n in out:
        del n["_aw"]
    res = {k: v for k, v in pf.items() if k not in ("water", "members")}
    res["members"] = out
    return res


def harbour_score(field, pfr, centre, level):
    """How well a stilt prefab (turned) fits at `centre` against water at
    `level`: the share of its corners whose ground already agrees (wet
    corners low, at most 1.5 m above the water; dry corners above it)."""
    cr, cc = centre[0] // 4, centre[1] // 4
    wet = {(dr, dc) for dr, dc in pfr["water"]["wet_corners"]}
    ok = n = 0
    for dr, dc, _b in pfr["brushes"]:
        r, c = (cr + dr) * 4, (cc + dc) * 4
        if not (0 <= r < field.shape[0] and 0 <= c < field.shape[1]):
            return -1.0
        h = field[r, c]
        ok += (h < level + 150) if (dr, dc) in wet else (h > level)
        n += 1
    score = ok / max(1, n)
    # Each ramp must come down on land (user, phase 7b review: the ramp
    # ended in the water). A landing over water halves the score.
    g = terrain.GRID_CM
    for x, y in pfr["water"].get("landings", []):
        r, c = int(round(centre[0] + y / g)), int(round(centre[1] + x / g))
        if not (0 <= r < field.shape[0] and 0 <= c < field.shape[1]) or field[r, c] <= level + 30:
            score *= 0.5
    return score


def pick_harbour(field, pf, level, allow, avoid, border_m, taken=(), target=None):
    """(centre vertex, quarter turns, score): the shore site and rotation
    where a stilt prefab's wet part lies over the water and its dry corners
    (the ramp) on land. Tie-break: nearer `target`."""
    rows, cols = field.shape
    pad = int(math.ceil((pf["radius_m"] + border_m) * 100 / terrain.GRID_CM))
    best = None
    turned = [rotate(pf, k) for k in range(4)]
    for r in range(pad - pad % 4, rows - pad, 4):
        for c in range(pad - pad % 4, cols - pad, 4):
            if not allow[r, c] or avoid[r, c]:
                continue
            if any(math.hypot(r - tr, c - tc) * terrain.GRID_CM / 100 < tm for tr, tc, tm in taken):
                continue
            for k, pfr in enumerate(turned):
                sc = harbour_score(field, pfr, (r, c), level)
                key = (round(sc, 3), -(math.hypot(r - target[0], c - target[1]) if target is not None else 0))
                if best is None or key > best[0]:
                    best = (key, (r, c), k, sc)
    if best is None:
        raise ValueError("no shore site for a stilt village")
    return best[1], best[2], best[3]


def shape_harbour(field, pfr, centre, level, skirt_m):
    """Ground for a stilt village: under its wet corners a shelf
    `wet_ground_below_water_cm` below the water, as in the source,
    its dry corners (the ramp landing) at `dry_ground_above_water_cm` above
    it, as in the source zone; blended back to the original ground over
    `skirt_m` beyond the prefab's radius. Returns the new field."""
    w = pfr["water"]
    wet = {(dr, dc) for dr, dc in w["wet_corners"]}
    depth = max(100.0, w["wet_ground_below_water_cm"])
    dry_h = level + max(50.0, w["dry_ground_above_water_cm"])
    rows, cols = field.shape
    lat = field[::4, ::4].astype(float).copy()
    cr, cc = centre[0] // 4, centre[1] // 4
    g = terrain.GRID_CM
    land = []
    for x, y in w.get("landings", []):
        land.append(((centre[0] + y / g) / 4.0, (centre[1] + x / g) / 4.0))
    for dr, dc, _b in pfr["brushes"]:
        r, c = cr + dr, cc + dc
        if 0 <= r < lat.shape[0] and 0 <= c < lat.shape[1]:
            near_landing = any(math.hypot(r - lr, c - lc) <= 1.0 for lr, lc in land)   # within 10 m
            # a shelf at the source's depth (retail: ~1 m), not the seabed
            # wherever it was deeper: a deep pit beside the raised ramp
            # landing made a step nobody could climb back up
            lat[r, c] = dry_h if near_landing or (dr, dc) not in wet else level - depth
    for lr, lc in land:                      # landings beyond the prefab's corners
        for r in range(int(lr) - 1, int(lr) + 3):
            for c in range(int(lc) - 1, int(lc) + 3):
                if 0 <= r < lat.shape[0] and 0 <= c < lat.shape[1] and math.hypot(r - lr, c - lc) <= 1.0:
                    lat[r, c] = max(lat[r, c], dry_h)
    # bilinear up to vertices
    yy, xx = np.mgrid[0:rows, 0:cols] / 4.0
    y0, x0 = np.minimum(yy.astype(int), lat.shape[0] - 2), np.minimum(xx.astype(int), lat.shape[1] - 2)
    fy, fx = yy - y0, xx - x0
    up = (lat[y0, x0] * (1 - fx) * (1 - fy) + lat[y0, x0 + 1] * fx * (1 - fy)
          + lat[y0 + 1, x0] * (1 - fx) * fy + lat[y0 + 1, x0 + 1] * fx * fy)
    d = np.hypot(np.mgrid[0:rows, 0:cols][0] - centre[0], np.mgrid[0:rows, 0:cols][1] - centre[1]) \
        * terrain.GRID_CM / 100.0
    t = np.clip((d - pfr["radius_m"]) / max(1e-6, skirt_m), 0.0, 1.0)
    wgt = 1.0 - t * t * (3 - 2 * t)
    out = field + (up - field) * wgt
    return np.round(out.astype("<f4"), 1).astype("<f4")


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


def wall_extent_m(pf, footprints, cells_shape):
    """How far (m) from the prefab centre any member's walls reach: the pad
    must cover them all, or a wide member (Adventurer's Plain's wooden
    platforms) stands half on the skirt slope."""
    g = terrain.GRID_CM
    centre = (cells_shape[0] // 2 // 4 * 4, cells_shape[1] // 2 // 4 * 4)
    placed, _ = instantiate(pf, centre, 0.0)
    local = np.zeros(cells_shape, bool)
    for q in placed:
        footprints.mark(local, q)
    rr, cc = np.nonzero(local)
    if not len(rr):
        return 0.0
    return float(np.max(np.hypot((cc + 0.5) * g - centre[1] * g, (rr + 0.5) * g - centre[0] * g))) / 100.0


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


def arc_toward(arcs, bearing, min_width_deg=12, margin_deg=6):
    """The bearing to leave by when a road heads toward `bearing`: inside the
    open arc nearest that direction (at least `min_width_deg` wide), as
    close to `bearing` as the arc allows. None if no arc is wide enough."""
    best, best_d = None, None
    for a, b in arcs:
        width = (b - a) % 360 + 1
        if width < min_width_deg:
            continue
        lo, hi = a + margin_deg, a + width - 1 - margin_deg      # unwrapped
        if lo > hi:
            lo = hi = a + (width - 1) / 2.0
        x = (bearing - a) % 360 + a                               # bearing unwrapped into [a, a+360)
        if lo <= x <= hi:
            return bearing % 360
        for edge in (lo, hi):
            d = min(abs((bearing - edge) % 360), abs((edge - bearing) % 360))
            if best_d is None or d < best_d:
                best, best_d = edge % 360, d
    return best


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
