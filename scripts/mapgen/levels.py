"""Multi-level terrain (phase 7c): pits, ravines, ledges, ramps, bridges and
invisible walls.

Three kinds of levels share one set of rules, all measured or read from the
client before being designed:

* A player can always walk DOWN a steep slope (ApplySliding refuses only a
  step with an uphill component, cobjchar_collision.cpp:1104-1180). So a
  level the player may not leave by dropping (a clifftop ledge) needs an
  invisible wall along its edge, as retail does: 2,889 collision boxes
  (IFO lump 11, a 1.2 x 0.1 x 2.5 m panel uniformly scaled, 17-37 m long in
  JG) stand along retail boundaries.
* A level the player may fall into (a pit, a ravine) needs a way out, or it
  is a trap: a ramp or a gully, a narrow trench rising from its floor to
  the ground around at a walkable slope.
* Walls that must not be climbed are sealed (mapgen-zone.seal), including
  the client's NE-corner blind spot.

Bridges are objects: the client skips its slope rule on them, so the walk
check gets a link from one bank to the other (walk.analyse(links=...)).
Retail pairings: field-bridge02 (17.7 m deck, arched 2.5 -> 4.1 m, scaled
1.5-2.2) over streams; guroomdari (46 m suspension deck, ends level, sagging
2.2 m) over deep gaps (Anima Lake 40 m deep, Kenji Beach 29 m).

Positions are field-local: vertices (row, col), heights cm, distances m.
"""

import math

import numpy as np

from . import terrain

G = terrain.GRID_CM

BRIDGES = {
    # id, deck half-length (model cm), deck height at the ends (model cm), retail scales
    "field-bridge02": {"id": 175, "half_cm": 850.0, "end_cm": 250.0, "scales": (1.5, 2.2)},
    "guroomdari": {"id": 193, "half_cm": 2310.0, "end_cm": 10.0, "scales": (0.7, 1.1)},
}


def _grid(shape):
    return np.mgrid[0:shape[0], 0:shape[1]].astype(float)


def gully(field, start, bearing_deg, floor_cm, slope_deg=26.0, width_m=7.0, wall_deg=70.0, max_len_m=120.0,
          flat_m=0.0):
    """Cut a trench that leaves `start` (vertex, at height `floor_cm`)
    along `bearing_deg` (0 = east, counter-clockwise), level for `flat_m`
    (to the edge of the floor it leaves: rising from the centre left a step
    where it met the floor's edge), then rising at `slope_deg` until it
    meets the ground. Its sides are walls at `wall_deg`; `width_m` must
    hold two whole cells (7 m), or every cell of it touches a wall and
    reads steep. Returns (field, mask of the trench's floor vertices, end
    vertex)."""
    yy, xx = _grid(field.shape)
    a = math.radians(bearing_deg)
    ux, uy = math.cos(a), math.sin(a)
    du = ((xx - start[1]) * ux + (yy - start[0]) * uy) * G / 100.0          # m along
    dv = np.abs(-(xx - start[1]) * uy + (yy - start[0]) * ux) * G / 100.0  # m across
    rise = math.tan(math.radians(slope_deg)) * 100.0                         # cm per m
    floor = floor_cm + rise * np.clip(du - flat_m, 0.0, None)
    walls = floor + math.tan(math.radians(wall_deg)) * 100.0 * np.clip(dv - width_m / 2.0, 0.0, None)
    span = (du >= -width_m) & (du <= max_len_m)
    cut = span & (walls < field)
    out = np.where(cut, walls, field)
    trench = span & (dv <= width_m / 2.0) & (floor < field + 30.0)
    # the end: where the trench floor first reaches the original ground
    along = np.where(trench, du, -1.0)
    end_u = float(along.max()) if trench.any() else 0.0
    end = (int(round(start[0] + uy * end_u * 100.0 / G)), int(round(start[1] + ux * end_u * 100.0 / G)))
    return np.round(out.astype("<f4"), 1).astype("<f4"), trench, end


def pit(field, centre, radius_m, depth_cm, wall_deg=68.0):
    """A round pit: flat floor `depth_cm` below its rim (the mean ground on
    the rim), walls at `wall_deg` up to `radius_m`. Returns (field, floor
    height, mask of the pit's inside)."""
    yy, xx = _grid(field.shape)
    d = np.hypot(yy - centre[0], xx - centre[1]) * G / 100.0
    rim = (d >= radius_m - 1.5) & (d <= radius_m + 1.5)
    rim_h = float(np.median(field[rim]))
    floor_h = rim_h - depth_cm
    wall_w = depth_cm / 100.0 / math.tan(math.radians(wall_deg))
    r_floor = max(2.0, radius_m - wall_w)
    surf = floor_h + math.tan(math.radians(wall_deg)) * 100.0 * np.clip(d - r_floor, 0.0, None)
    out = np.where(d <= radius_m + 2.0, np.minimum(field, surf), field)
    inside = d <= radius_m
    return np.round(out.astype("<f4"), 1).astype("<f4"), floor_h, inside


def polyline_distance(shape, pts):
    """(lateral distance m, along-position 0..1, signed side) of every vertex
    to a polyline of vertices [(row, col), ...]."""
    yy, xx = _grid(shape)
    best = np.full(shape, np.inf)
    along = np.zeros(shape)
    side = np.zeros(shape)
    seg = [math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(pts, pts[1:])]
    total = sum(seg)
    acc = 0.0
    for (a, b), L in zip(zip(pts, pts[1:]), seg):
        ar, ac = a
        dr, dc = b[0] - a[0], b[1] - a[1]
        t = np.clip(((yy - ar) * dr + (xx - ac) * dc) / max(1e-9, L * L), 0.0, 1.0)
        pr, pc = ar + t * dr, ac + t * dc
        dist = np.hypot(yy - pr, xx - pc)
        sgn = np.sign(dc * (yy - ar) - dr * (xx - ac))            # +1 left of the direction
        m = dist < best
        best = np.where(m, dist, best)
        along = np.where(m, (acc + t * L) / total, along)
        side = np.where(m, sgn, side)
        acc += L
    return best * G / 100.0, along, side


def ravine(field, axis, width_m, depth_cm, wall_deg=70.0, floor_m=10.0, narrow=()):
    """A ravine along `axis` (vertices): a flat floor `floor_m` wide, steep
    walls at `wall_deg` up to the banks; its top width is `width_m`, except
    near each (along 0..1, width m) in `narrow` (bridge crossings). The
    floor is one height along its whole length: `depth_cm` under the
    lowest bank, so a river along it has one level. Returns (field, floor
    height, inside mask, lateral distance, along, side)."""
    lat, along, side = polyline_distance(field.shape, axis)
    half_top = np.full(field.shape, width_m / 2.0)
    for at, w in narrow:
        k = np.clip(1.0 - np.abs(along - at) / 0.06, 0.0, 1.0)
        half_top = half_top - (half_top - w / 2.0) * (k * k * (3 - 2 * k))
    bank = (lat >= half_top) & (lat <= half_top + 6.0)
    floor_h = float(np.min(field[bank])) - depth_cm
    tan_w = math.tan(math.radians(wall_deg)) * 100.0
    half_floor = np.maximum(floor_m / 2.0, half_top - (field - floor_h) / tan_w)
    surf = floor_h + tan_w * np.clip(lat - half_floor, 0.0, None)
    out = np.minimum(field, surf)
    inside = lat <= half_top
    return np.round(out.astype("<f4"), 1).astype("<f4"), floor_h, inside, lat, along, side


def wall_line(points, ground, scale=8.0, overlap_m=1.0, sink_cm=150.0):
    """Invisible collision panels (IFO lump 11) along a polyline of points
    in field-local cm [(x, y), ...]: each the client's special collision
    object scaled `scale` (1.2 m * scale long, 2.5 m * scale tall), yawed
    along the line, sunk `sink_cm` so no gap opens on a slope. `ground(x,
    y)` gives the terrain height. Returns placed dicts (lump COLLISION)."""
    out = []
    length = 120.0 * scale
    step = length - overlap_m * 100.0
    pts = np.array(points, float)
    if len(pts) < 2:
        return out
    seg = np.hypot(*(np.diff(pts, axis=0).T))
    total = float(seg.sum())
    cum = np.concatenate([[0.0], np.cumsum(seg)])

    def at(sv):
        i = min(int(np.searchsorted(cum, sv, side="right") - 1), len(seg) - 1)
        t = (sv - cum[i]) / max(1e-6, seg[i])
        return pts[i] + (pts[i + 1] - pts[i]) * t

    pos = 0.0
    while pos < total:
        a, b = at(pos), at(min(pos + length, total))
        mid = (a + b) / 2.0
        yaw = math.atan2(b[1] - a[1], b[0] - a[0])
        z = ground(mid[0], mid[1]) - sink_cm
        out.append({"lump": "COLLISION", "id": 2, "name": "wall", "x": float(mid[0]), "y": float(mid[1]),
                    "z": float(z), "rot": (0.0, 0.0, math.sin(yaw / 2), math.cos(yaw / 2)),
                    "scale": (scale, scale, scale), "sink": -sink_cm, "rc": 0.0, "rv": 0.0, "category": "WALL"})
        pos += step
    return out


def bridge(kind, centre_cm, bearing_deg, span_m, bank_cm):
    """A bridge object over a gap of `span_m` at `centre_cm` (x, y field
    cm), deck ends at `bank_cm`. Scaled so its deck reaches 3 m past each
    bank, within retail's scale range when possible. Returns (placed dict,
    (end A, end B) in field cm, scale)."""
    b = BRIDGES[kind]
    need = (span_m + 6.0) * 100.0 / 2.0
    s = need / b["half_cm"]
    a = math.radians(bearing_deg)
    ux, uy = math.cos(a), math.sin(a)
    half = b["half_cm"] * s
    ends = ((centre_cm[0] - ux * half, centre_cm[1] - uy * half), (centre_cm[0] + ux * half, centre_cm[1] + uy * half))
    q = {"lump": "OBJECT", "id": b["id"], "name": kind, "x": float(centre_cm[0]), "y": float(centre_cm[1]),
         "z": float(bank_cm - b["end_cm"] * s), "rot": (0.0, 0.0, math.sin(a / 2), math.cos(a / 2)),
         "scale": (s, s, s), "sink": 0.0, "rc": 0.0, "rv": 0.0, "category": "BRIDGE"}
    return q, ends, s


def deck_link(ends, shape):
    """The walk-check link for a bridge: the cells just past each end of the
    deck, 1.5 m onto the bank."""
    (ax, ay), (bx, by) = ends
    L = math.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / L, (by - ay) / L
    pa = (ax - ux * 150.0, ay - uy * 150.0)
    pb = (bx + ux * 150.0, by + uy * 150.0)
    ca = (min(int(pa[1] // G), shape[0] - 1), min(int(pa[0] // G), shape[1] - 1))
    cb = (min(int(pb[1] // G), shape[0] - 1), min(int(pb[0] // G), shape[1] - 1))
    return ca, cb
