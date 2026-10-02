"""Seeded terrain heightfields (phase 2).

A zone's terrain is one global heightfield sampled every 2.5 m (the HIM
grid). Shape is (64 * chunks_y + 1, 64 * chunks_x + 1); row 0 = SOUTH,
column 0 = WEST; values in cm. Chunks are slices of it, so neighbouring
chunks share their edge vertices exactly.

Pipeline (all deterministic from the seed):

1. Hills: fractal gradient (Perlin) noise.
2. Slope cap for the play area. A min-plus "cone" envelope cuts every peak
   steeper than `max_play_slope_deg`:

       H'(p) = min_q H(q) + k * L1(p, q)

   It is exact and separable, so four cumulative-min sweeps compute it
   (`slope_cap`). It bounds both forward differences the client's slope
   test uses (io_terrain.cpp:2038-2048), so the interior can contain no
   wall a player cannot climb.
3. Ridge: an optional smooth rise along the map border, steep enough in its
   middle to be impassable. It is scenery only: the client already stops
   the player at the last chunk (user-verified, phase 1).
"""

import math

import numpy as np

GRID_CM = 250.0                 # HIM vertex spacing
VERTS_PER_CHUNK = 64            # 16 patches x 4 grids


def field_shape(chunks_x, chunks_y):
    return (VERTS_PER_CHUNK * chunks_y + 1, VERTS_PER_CHUNK * chunks_x + 1)


def perlin(shape, cell_verts, rng):
    """2D gradient noise in about [-0.7, 0.7]; one lattice cell = `cell_verts` vertices."""
    h, w = shape
    gy = int(math.ceil((h - 1) / cell_verts)) + 2
    gx = int(math.ceil((w - 1) / cell_verts)) + 2
    ang = rng.uniform(0.0, 2.0 * math.pi, size=(gy, gx))
    gvx, gvy = np.cos(ang), np.sin(ang)
    off = rng.uniform(0.0, 1.0, size=2)                  # random lattice phase
    y = np.arange(h)[:, None] / cell_verts + off[0]
    x = np.arange(w)[None, :] / cell_verts + off[1]
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0

    def dot(ix, iy, dx, dy):
        return gvx[iy, ix] * dx + gvy[iy, ix] * dy

    n00 = dot(x0, y0, fx, fy)
    n10 = dot(x0 + 1, y0, fx - 1, fy)
    n01 = dot(x0, y0 + 1, fx, fy - 1)
    n11 = dot(x0 + 1, y0 + 1, fx - 1, fy - 1)
    u = fx * fx * fx * (fx * (fx * 6 - 15) + 10)         # quintic fade
    v = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
    return (n00 * (1 - u) + n10 * u) * (1 - v) + (n01 * (1 - u) + n11 * u) * v


def fbm(shape, wavelength_m, octaves, persistence, rng):
    """Fractal noise normalised to [-1, 1]."""
    total = np.zeros(shape)
    amp, cell = 1.0, wavelength_m * 100.0 / GRID_CM
    for _ in range(octaves):
        total += amp * perlin(shape, max(cell, 2.0), rng)
        amp *= persistence
        cell /= 2.0
    m = np.abs(total).max()
    return total / m if m > 0 else total


def _cone_1d(a, step, axis):
    """Min-plus with |i-j|*step along one axis (both directions)."""
    a = np.moveaxis(a, axis, -1)
    idx = np.arange(a.shape[-1]) * step
    fwd = np.minimum.accumulate(a - idx, axis=-1) + idx
    rev = (np.minimum.accumulate((fwd + idx)[..., ::-1], axis=-1))[..., ::-1] - idx
    return np.moveaxis(np.minimum(fwd, rev), -1, axis)


def slope_cap(h, max_slope_deg):
    """Largest field <= h whose x and y forward differences are all
    <= tan(max) / sqrt(2) per grid. The client's gradient magnitude is then
    <= tan(max) (io_terrain.cpp:2043-2044)."""
    step = math.tan(math.radians(max_slope_deg)) / math.sqrt(2.0) * GRID_CM
    out = _cone_1d(h, step, 1)
    return _cone_1d(out, step, 0)


EDGES = ("north", "south", "east", "west")


def edge_distance(shape, edges=EDGES):
    """Distance in vertices to the nearest of the given map edges."""
    h, w = shape
    yy = np.arange(h)[:, None] + np.zeros((1, w))
    xx = np.arange(w)[None, :] + np.zeros((h, 1))
    d = np.full(shape, np.inf)
    for e, v in (("south", yy), ("north", h - 1 - yy), ("west", xx), ("east", w - 1 - xx)):
        if e in edges:
            d = np.minimum(d, v)
    return d


def ridge(shape, height_cm, width_m, edges=EDGES):
    """Border rise along the given edges: 0 inside, `height_cm` at the edge,
    smoothstep over `width_m`. The peak slope is 1.5 * height / width
    (smoothstep's derivative). A coast leaves its sea edge out."""
    wv = width_m * 100.0 / GRID_CM
    t = np.clip(1.0 - edge_distance(shape, edges) / wv, 0.0, 1.0)
    return height_cm * t * t * (3 - 2 * t)


def direction(name):
    """Unit (dx east, dy north) for a compass name: north, south-east, ..."""
    v = {"north": (0, 1), "south": (0, -1), "east": (1, 0), "west": (-1, 0)}
    dx = sum(v[p][0] for p in name.split("-"))
    dy = sum(v[p][1] for p in name.split("-"))
    n = math.hypot(dx, dy)
    return dx / n, dy / n


def tilt(shape, rise_cm, toward):
    """A plane rising by `rise_cm` across the map toward a compass direction."""
    h, w = shape
    dx, dy = direction(toward)
    yy, xx = np.mgrid[0:h, 0:w]
    u = (xx / (w - 1) - 0.5) * dx + (yy / (h - 1) - 0.5) * dy        # -0.5..0.5 on an axis
    span = abs(dx) * 0.5 + abs(dy) * 0.5
    return rise_cm * (u / (2 * span) + 0.5)


def bump(shape, centre, radius_m, height_cm, top_m=0.0, rng=None, roughness=0.0):
    """A rise of `height_cm` around `centre` (vertex row, col): flat within
    `top_m`, falling to 0 at `radius_m` along a smoothstep. With `roughness`,
    fractal noise scales it (mountains), so peaks are not perfect domes."""
    h, w = shape
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.hypot(yy - centre[0], xx - centre[1]) * GRID_CM / 100.0
    t = np.clip((radius_m - d) / max(1e-6, radius_m - top_m), 0.0, 1.0)
    out = height_cm * t * t * (3 - 2 * t)
    if roughness and rng is not None:
        out *= 1.0 + roughness * fbm(shape, max(30.0, radius_m / 2.0), 3, 0.5, rng)
    return out


def generate(chunks_x, chunks_y, t, seed, features=()):
    """Global heightfield (row 0 = south) from the spec's "terrain" section.

    `features` are resolved terrain features: dicts with "kind" ("hill":
    walkable, added before the slope cap; "mountains": scenery, added after
    it like the ridge, so their flanks can be too steep to climb), "centre"
    (vertex), "radius_m", "height_cm", "top_m", "roughness".
    """
    shape = field_shape(chunks_x, chunks_y)
    rng = np.random.default_rng(seed)
    hills = t.get("hills", {})
    field = np.full(shape, float(t.get("base_cm", 0.0)))
    if hills.get("amplitude_cm", 0):
        field += hills["amplitude_cm"] * fbm(shape, hills.get("wavelength_m", 200.0),
                                             hills.get("octaves", 4),
                                             hills.get("persistence", 0.45), rng)
    tl = t.get("tilt")
    if tl and tl.get("rise_cm"):
        field += tilt(shape, tl["rise_cm"], tl["toward"])
    frng = np.random.default_rng(seed + 50)
    for f in features:
        if f["kind"] == "hill":
            field += bump(shape, f["centre"], f["radius_m"], f["height_cm"], f.get("top_m", 0.0),
                          frng, f.get("roughness", 0.0))
    if "max_play_slope_deg" in t:
        field = slope_cap(field, t["max_play_slope_deg"])
    for f in features:
        if f["kind"] == "mountains":
            field = field + bump(shape, f["centre"], f["radius_m"], f["height_cm"], f.get("top_m", 0.0),
                                 frng, f.get("roughness", 0.3))
    r = t.get("ridge")
    if r and r.get("height_cm", 0):
        field = field + ridge(shape, r["height_cm"], r.get("width_m", 40.0), tuple(r.get("edges", EDGES)))
    return np.round(field.astype("<f4"), 1).astype("<f4")   # 1 mm steps: stable bytes


def chunk_heights(field, cx, cy):
    """(65, 65) heights of chunk (cx, cy) counted from the field's SW chunk,
    in HIM FILE order (row 0 = north)."""
    r0, c0 = cy * VERTS_PER_CHUNK, cx * VERTS_PER_CHUNK
    return np.ascontiguousarray(field[r0:r0 + 65, c0:c0 + 65][::-1])


def gradient(field):
    """Per-cell gradient as the client computes it: forward differences from
    the cell's south-west corner (io_terrain.cpp:2038-2048, :2846-2849).
    Shape (rows-1, cols-1), dimensionless (cm per cm)."""
    gx = (field[:-1, 1:] - field[:-1, :-1]) / GRID_CM
    gy = (field[1:, :-1] - field[:-1, :-1]) / GRID_CM
    return gx, gy


def pick_start(field, max_slope_deg=10.0, radius_cells=2, avoid=None, target=None):
    """The vertex nearest `target` (default: the field's centre) whose
    surrounding cells are all gentler than `max_slope_deg` and not in `avoid`
    (a vertex mask, e.g. water and its shore). Returns (row, col) of a vertex."""
    gx, gy = gradient(field)
    steep = np.hypot(gx, gy) > math.tan(math.radians(max_slope_deg))
    h, w = field.shape
    cy, cx = ((h - 1) / 2.0, (w - 1) / 2.0) if target is None else target
    best, best_d = None, None
    for r in range(radius_cells, h - 1 - radius_cells):
        for c in range(radius_cells, w - 1 - radius_cells):
            d = (r - cy) ** 2 + (c - cx) ** 2
            if best_d is not None and d >= best_d:
                continue
            if avoid is not None and avoid[r, c]:
                continue
            if not steep[r - radius_cells:r + radius_cells, c - radius_cells:c + radius_cells].any():
                best, best_d = (r, c), d
    if best is None:
        raise ValueError("no gentle spot for the start point")
    return best
