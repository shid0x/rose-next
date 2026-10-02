"""Lakes: basin carving, water rectangles and their checks (phase 4).

How the game draws water (docs/mapgen/FORMATS.md, "Water"):

* IFO lump 9 holds flat rectangles at one height, each a quad tiled with
  Junon's animated sea texture every `size` (always 2000 cm in retail).
* Nothing collides with water (cobjchar_collision.cpp:674), so a player
  walks along the lake bed.
* A lake is therefore a basin in the terrain plus rectangles at the water
  level. The plane is hidden by the depth test wherever the ground is
  higher.

Retail conventions, from 850 rectangles in 62 zones:

* size 2000;
* start z == end z;
* start = north-west corner (min x, MAX y) in 777 of them;
* 94% inside their own chunk, 95% in whole 20 m widths.

Generated water follows all of these. It is split per chunk on 20 m cells
aligned to the chunk grid, so a rectangle never outlives its chunk when the
client streams chunks.

A lake is carved, not found:

1. Outline: a noisy circle around the centre.
2. Level L: below the lowest ground in a ring outside the outline (`ring_m`,
   at least one water cell wide), so everything near the lake stays dry.
3. Surface: inside, L - depth * smoothstep(distance in / shore). Outside,
   the ground is pulled down towards a gentle cone L + tan(shore_slope) *
   distance, fully within `bank_m` and fading out by `skirt_m`, so the lake
   reshapes only its surroundings.
4. Site: the flattest low area (pick_centre). A site needing more than
   `max_cut_cm` of extra cutting is refused.
"""

import math

import numpy as np

from . import terrain

CELL_CM = 2000                      # water texture repeat = rectangle grid (retail `size`)
CELL_VERTS = int(CELL_CM / terrain.GRID_CM)   # 8 vertices per 20 m cell
ORIGIN_CM = 520000                  # zone-relative origin of IFO/ZON coordinates


def pick_centre(field, radius_m, border_m, ring_m=25.0, avoid=None, allow=None, target=None,
                target_cm_per_m=3.0):
    """The flattest low site for a lake: the smallest height range within
    radius + ring of the centre, ties broken by lower ground.

    Choosing the merely lowest spot put the first lake at the foot of 30-40 m
    hills, and shaping its shore carved a crater over a quarter of the map.
    Searched on the 10 m corner lattice; returns a vertex (row, col). With
    `allow` (vertex mask) the centre must lie in it; with `target` (vertex),
    each metre away from it costs `target_cm_per_m` of height range.
    """
    lat = field[::4, ::4]
    rows, cols = lat.shape
    win = int(math.ceil((radius_m + ring_m) / 10.0))
    pad = int(math.ceil((radius_m + border_m) / 10.0))
    best, best_score = None, None
    for r in range(pad, rows - pad):
        for c in range(pad, cols - pad):
            if avoid is not None and avoid[r * 4, c * 4]:
                continue
            if allow is not None and not allow[r * 4, c * 4]:
                continue
            w = lat[max(0, r - win):r + win + 1, max(0, c - win):c + win + 1]
            score = (float(w.max() - w.min()) + 0.05 * float(w.mean())) / 100.0
            if target is not None:
                score += target_cm_per_m * math.hypot(r * 4 - target[0], c * 4 - target[1])                     * terrain.GRID_CM / 100 / 100.0
            if best_score is None or score < best_score:
                best, best_score = (r * 4, c * 4), score
    if best is None:
        raise ValueError("no room for a lake of radius %s m" % radius_m)
    return best


def carve(field, lake, rng, centre):
    """Carve one lake into `field` (row 0 = south, cm). Returns (new field, level cm)."""
    rows, cols = field.shape
    yy, xx = np.mgrid[0:rows, 0:cols].astype(float)
    dy, dx = yy - centre[0], xx - centre[1]
    dist = np.hypot(dx, dy) * terrain.GRID_CM / 100.0            # metres
    theta = np.arctan2(dy, dx)
    # Noisy radius: a few random harmonics.
    wobble = np.zeros_like(theta)
    irregular = lake.get("irregularity", 0.25)
    for k in (2, 3, 5):
        wobble += rng.uniform(-1, 1) * np.cos(k * theta + rng.uniform(0, 2 * math.pi)) / k
    radius = lake["radius_m"] * (1.0 + irregular * wobble)
    d_out = dist - radius                                        # >0 outside the outline

    ring_m = max(lake.get("ring_m", 25.0), CELL_CM / 100.0 + 5.0)
    ring = (d_out > 0) & (d_out <= ring_m)
    level = float(np.floor(field[ring].min() - lake.get("margin_cm", 30.0)))

    depth = lake.get("depth_cm", 400.0)
    shore = lake.get("shore_m", 14.0)
    t = np.clip(-d_out / shore, 0.0, 1.0)
    inside = level - depth * t * t * (3 - 2 * t)
    k = math.tan(math.radians(lake.get("shore_slope_deg", 12.0))) * 100.0   # cm per m
    cone = level + k * np.maximum(d_out, 0.0)
    # Outside: pull the ground down towards the gentle shore cone, fully up
    # to `bank_m` from the water and fading to nothing at `skirt_m`, so the
    # lake reshapes only its own surroundings. Ground stays >= the cone, and
    # the cone is >= the level, so nothing outside floods.
    bank, skirt = lake.get("bank_m", 10.0), lake.get("skirt_m", 40.0)
    u = np.clip((d_out - bank) / max(1e-6, skirt - bank), 0.0, 1.0)
    weight = 1.0 - u * u * (3 - 2 * u)
    excess = np.maximum(field - cone, 0.0)
    out = np.where(d_out <= 0, np.minimum(field, inside), field - excess * weight)
    cut = float(np.max(field - out))
    max_cut = lake.get("max_cut_cm", 800.0)
    if cut - depth > max_cut:
        raise ValueError("lake at %s would cut the ground by %.1f m (limit %.1f m): pick a flatter site"
                         % (centre, (cut - depth) / 100, max_cut / 100))
    return np.round(out.astype("<f4"), 1).astype("<f4"), level


def lake_mask(field, level, centre):
    """Vertices below `level` connected (4-way) to the centre."""
    below = field < level
    mask = np.zeros_like(below)
    if not below[centre]:
        return mask
    stack = [centre]
    mask[centre] = True
    rows, cols = below.shape
    while stack:
        r, c = stack.pop()
        for rr, cc in ((r + 1, c), (r - 1, c), (r, c + 1), (r, c - 1)):
            if 0 <= rr < rows and 0 <= cc < cols and below[rr, cc] and not mask[rr, cc]:
                mask[rr, cc] = True
                stack.append((rr, cc))
    return mask


def components(mask):
    """4-connected components of a vertex mask: list of index arrays (rows, cols)."""
    seen = np.zeros_like(mask, bool)
    rows, cols = mask.shape
    out = []
    for r0, c0 in zip(*np.nonzero(mask)):
        if seen[r0, c0]:
            continue
        stack, comp = [(r0, c0)], []
        seen[r0, c0] = True
        while stack:
            r, c = stack.pop()
            comp.append((r, c))
            for rr, cc in ((r + 1, c), (r - 1, c), (r, c + 1), (r, c - 1)):
                if 0 <= rr < rows and 0 <= cc < cols and mask[rr, cc] and not seen[rr, cc]:
                    seen[rr, cc] = True
                    stack.append((rr, cc))
        a = np.array(comp)
        out.append((a[:, 0], a[:, 1]))
    return out


def flood(field, region, fraction=None, level=None, min_depth_cm=80.0, min_m2=1200.0, fill_above_cm=20.0):
    """Water by level instead of by carving: everything below one level is
    water ("half the map is water", a coast below a tilted land).

    The level is the one that puts `fraction` of the `region` vertices under
    water (bisection; filling changes the share), or `level` as given.
    Basins shallower than `min_depth_cm` or smaller than `min_m2` are filled
    to `fill_above_cm` over the level instead: puddles read as mistakes, and
    a basin with no point 50 cm deep breaks the waterline check's seeding.
    Returns (field, level, [{"centre", "mask"}] per kept basin).
    """
    vert_m2 = (terrain.GRID_CM / 100.0) ** 2

    def apply(L):
        f = field.copy()
        kept = []
        for rr, cc in components(field < L):
            depth = L - float(field[rr, cc].min())
            if depth < min_depth_cm or len(rr) * vert_m2 < min_m2:
                f[rr, cc] = L + fill_above_cm
                continue
            m = np.zeros(field.shape, bool)
            m[rr, cc] = True
            i = int(np.argmin(field[rr, cc]))
            kept.append({"centre": (int(rr[i]), int(cc[i])), "mask": m})
        return f, kept

    if level is None:
        vals = np.sort(field[region])
        lo, hi = float(vals[0]), float(vals[-1])
        for _ in range(18):                            # ~0.01% of the height range
            mid = (lo + hi) / 2.0
            f, kept = apply(mid)
            wet = sum(int(k["mask"][region].sum()) for k in kept) / float(region.sum())
            if wet < fraction:
                lo = mid
            else:
                hi = mid
        level = float(np.floor(hi))
    f, kept = apply(level)
    return np.round(f.astype("<f4"), 1).astype("<f4"), level, kept


def rects_for(mask, level, x0, y0, chunks_x, chunks_y):
    """Water rectangles covering `mask`, per chunk.

    Returns {(chunk_x, chunk_y): [(sx, sz, sy, ex, ez, ey), ...]} in
    zone-relative cm, start = NW corner (retail majority). A 20 m cell is
    wet if any of its vertices (borders included) is in the mask.
    """
    out = {}
    per = 64 // CELL_VERTS                                  # 8 cells per chunk axis
    for cy in range(chunks_y):
        for cx in range(chunks_x):
            wet = np.zeros((per, per), bool)
            for j in range(per):
                for i in range(per):
                    r0 = cy * 64 + j * CELL_VERTS
                    c0 = cx * 64 + i * CELL_VERTS
                    wet[j, i] = mask[r0:r0 + CELL_VERTS + 1, c0:c0 + CELL_VERTS + 1].any()
            rects = []
            done = np.zeros_like(wet)
            for j in range(per):                            # greedy: widest run, then grow north
                i = 0
                while i < per:
                    if not wet[j, i] or done[j, i]:
                        i += 1
                        continue
                    i1 = i
                    while i1 + 1 < per and wet[j, i1 + 1] and not done[j, i1 + 1]:
                        i1 += 1
                    j1 = j
                    while j1 + 1 < per and wet[j1 + 1, i:i1 + 1].all() and not done[j1 + 1, i:i1 + 1].any():
                        j1 += 1
                    done[j:j1 + 1, i:i1 + 1] = True
                    wx0 = ((x0 + cx) * 64 + i * CELL_VERTS) * terrain.GRID_CM - ORIGIN_CM
                    wx1 = ((x0 + cx) * 64 + (i1 + 1) * CELL_VERTS) * terrain.GRID_CM - ORIGIN_CM
                    wy0 = ((y0 + cy) * 64 + j * CELL_VERTS) * terrain.GRID_CM - ORIGIN_CM
                    wy1 = ((y0 + cy) * 64 + (j1 + 1) * CELL_VERTS) * terrain.GRID_CM - ORIGIN_CM
                    rects.append((float(wx0), float(level), float(wy1), float(wx1), float(level), float(wy0)))
                    i = i1 + 1
            if rects:
                out[(x0 + cx, y0 + cy)] = rects
    return out


def coverage(rects_by_chunk, field_shape, x0, y0):
    """Vertex mask covered by any rectangle, plus each covered vertex's level."""
    cov = np.zeros(field_shape, bool)
    lvl = np.full(field_shape, np.nan)
    for rects in rects_by_chunk.values():
        for sx, sz, sy, ex, ez, ey in rects:
            c0 = int(round((min(sx, ex) + ORIGIN_CM) / terrain.GRID_CM)) - x0 * 64
            c1 = int(round((max(sx, ex) + ORIGIN_CM) / terrain.GRID_CM)) - x0 * 64
            r0 = int(round((min(sy, ey) + ORIGIN_CM) / terrain.GRID_CM)) - y0 * 64
            r1 = int(round((max(sy, ey) + ORIGIN_CM) / terrain.GRID_CM)) - y0 * 64
            cov[r0:r1 + 1, c0:c1 + 1] = True
            lvl[r0:r1 + 1, c0:c1 + 1] = sz
    return cov, lvl


def check(field, rects_by_chunk, x0, y0, chunk_bounds_ok=True):
    """The phase 4 water checks. Returns a list of (ok, message)."""
    results = []
    all_rects = [r for rs in rects_by_chunk.values() for r in rs]
    results.append((all(r[0] < r[3] and r[2] > r[5] for r in all_rects),
                    "every rectangle starts at its NW corner (retail convention)"))
    results.append((all(r[1] == r[4] for r in all_rects), "start z == end z for every rectangle"))
    results.append((all(abs(r[3] - r[0]) % CELL_CM == 0 and abs(r[2] - r[5]) % CELL_CM == 0 for r in all_rects),
                    "every rectangle is whole 20 m cells (texture repeats cleanly)"))
    inside = True
    for (cx, cy), rects in rects_by_chunk.items():
        lo_x, lo_y = cx * 16000 - ORIGIN_CM, cy * 16000 - ORIGIN_CM
        for sx, _, sy, ex, _, ey in rects:
            inside &= lo_x <= sx < ex <= lo_x + 16000 and lo_y <= ey < sy <= lo_y + 16000
    results.append((inside, "every rectangle lies inside the chunk whose IFO holds it"))

    cov, lvl = coverage(rects_by_chunk, field.shape, x0, y0)
    levels = sorted({r[1] for r in all_rects})
    # Waterline consistency per lake: every vertex below a covering level must
    # be connected (through below-level vertices) to that lake's deep water,
    # and every below-level vertex connected to a lake must be covered.
    dry_holes = shows_outside = 0
    for L in levels:
        mine = cov & (lvl == L)
        below = field < L
        # components of `below`, seeded from covered deepest points
        seeds = mine & (field < L - 50)
        comp = np.zeros_like(below)
        stack = list(zip(*np.nonzero(seeds)))
        for p in stack:
            comp[p] = True
        while stack:
            r, c = stack.pop()
            for rr, cc in ((r + 1, c), (r - 1, c), (r, c + 1), (r, c - 1)):
                if 0 <= rr < field.shape[0] and 0 <= cc < field.shape[1] and below[rr, cc] and not comp[rr, cc]:
                    comp[rr, cc] = True
                    stack.append((rr, cc))
        dry_holes += int((comp & ~cov).sum())                     # lake water with no plane over it
        shows_outside += int((mine & below & ~comp).sum())        # plane over a separate hollow
    results.append((dry_holes == 0, "every lake vertex below the waterline is covered by water (%d not)" % dry_holes))
    results.append((shows_outside == 0,
                    "water shows nowhere outside its basin: under every rectangle the ground is either "
                    "the lake or above the surface (%d vertices not)" % shows_outside))
    return results
