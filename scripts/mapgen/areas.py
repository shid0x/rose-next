"""Named areas of a map (phase 7).

A layout says where things go in words ("the north edge", "north-east of
the centre", "near the lake", "the lake's north shore"). layout.py turns the
words into the numeric descriptors below; this module resolves a descriptor
on a built map into

* a weight per vertex in [0, 1] (1 = fully inside, fading to 0 at a soft
  edge), used for cover (decoration density, brush bias);
* a target point (vertex row, col), used to place one thing (a lake, a
  village, the start);
* a radius (vertices) around the target, for "pick a site inside".

Descriptors (fractions are of the map: x east, y north, 0..1; radii are
fractions of the shorter map side):

    {"point": [fx, fy], "radius": fr}       disc, soft edge
    {"box": [fx0, fy0, fx1, fy1]}           rectangle, soft edge
    {"edge": "north", "depth": fd}          band along a map edge
    {"near": "name", "extra_m": m}          around a placed feature
    {"shore": "name", "side": "north"}      a point on a lake's shore
    {"all": true}                           the whole map

"near" and "shore" refer to features placed earlier in the build (lakes,
water, villages, hills), so descriptors are resolved during the build, in
build order.

Distances to a feature's outline use the exact separable L1 transform
(terrain._cone_1d) divided by 1.2: between L1 and Euclidean, close enough
for a fuzzy area.
"""

import math

import numpy as np

from . import terrain

SOFT = 0.35          # soft edge as a share of the radius / depth
L1_TO_EUCLID = 1.2


def _edt_1d(f, want_index=False):
    """Squared distance transform of one line (Felzenszwalb & Huttenlocher);
    with `want_index`, also the index each position's minimum came from."""
    n = len(f)
    src = np.zeros(n, int)
    d = np.empty(n)
    v = np.zeros(n, int)
    z = np.empty(n + 1)
    k = 0
    v[0] = 0
    z[0], z[1] = -np.inf, np.inf
    for q in range(1, n):
        if f[q] >= 1e17:
            continue
        while True:
            p = v[k]
            if f[p] >= 1e17:
                s = -np.inf
            else:
                s = ((f[q] + q * q) - (f[p] + p * p)) / (2.0 * q - 2.0 * p)
            if s <= z[k] and k > 0:
                k -= 1
                continue
            if f[p] >= 1e17:
                v[k] = q
                z[k] = -np.inf
                z[k + 1] = np.inf
            else:
                k += 1
                v[k] = q
                z[k] = s
                z[k + 1] = np.inf
            break
    k = 0
    for q in range(n):
        while z[k + 1] < q:
            k += 1
        p = v[k]
        d[q] = (q - p) ** 2 + f[p] if f[p] < 1e17 else 1e18
        src[q] = p
    return (d, src) if want_index else d


def nearest(mask):
    """(distance m, row, col of the nearest `mask` vertex) for every vertex:
    the exact Euclidean feature transform (two 1-D passes)."""
    h, w = mask.shape
    f = np.where(mask, 0.0, 1e18)
    d1 = np.empty((h, w))
    r1 = np.empty((h, w), int)
    for c in range(w):
        d1[:, c], r1[:, c] = _edt_1d(f[:, c], True)
    d2 = np.empty((h, w))
    c2 = np.empty((h, w), int)
    for r in range(h):
        d2[r, :], c2[r, :] = _edt_1d(d1[r, :], True)
    rows = r1[np.arange(h)[:, None], c2]
    return np.sqrt(d2) * terrain.GRID_CM / 100.0, rows, c2


def distance_m(mask):
    """Exact Euclidean distance (m) from every vertex to the nearest vertex of
    `mask` (two 1-D passes). An L1 approximation used before made walls
    built on it (terrain.shape_walls) dip under the 54-degree cliff slope
    where an outline ran at a shallow angle to the grid, leaving gaps."""
    if not mask.any():
        return np.full(mask.shape, 1e9)
    f = np.where(mask, 0.0, 1e18)
    rows = np.array([_edt_1d(f[:, c]) for c in range(f.shape[1])]).T
    out = np.array([_edt_1d(rows[r, :]) for r in range(f.shape[0])])
    return np.sqrt(out) * terrain.GRID_CM / 100.0


def _smooth(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3 - 2 * t)


def _radial(fx, fy, play):
    """A map-fraction point mapped onto the play region: the centre goes to
    its centroid, and a point at distance t (0.5 = the rectangle's edge) in
    a direction goes t / 0.5 of the way to the region's own extent that
    way, times 0.8. "North-west" lands inside an organic blob, not in the
    bounding box's empty corner, with room for a lake before the cliffs."""
    rr, cc = np.nonzero(play)
    c0r, c0c = rr.mean(), cc.mean()
    vx, vy = fx - 0.5, fy - 0.5
    t = math.hypot(vx, vy)
    if t < 1e-9:
        return c0r, c0c
    ux, uy = vx / t, vy / t
    reach = float(np.max((cc - c0c) * ux + (rr - c0r) * uy))
    k = min(1.0, t / 0.5) * reach * 0.8       # corners ~63% of the way out, sides ~48%: room for a lake
    return c0r + uy * k, c0c + ux * k


def resolve(desc, shape, features, frame=None, play=None):
    """(weight [0..1] per vertex, target (row, col) floats, radius in vertices).

    `frame` (r0, r1, c0, c1), the play region's bounding box when the map
    has a shape: compass words then mean the play area's north, centre ...,
    not the map rectangle's (much of which is cliffs)."""
    h, w = shape
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    r0f, r1f, c0f, c1f = frame if frame is not None else (0, h - 1, 0, w - 1)
    fh, fw = r1f - r0f, c1f - c0f
    side = min(fh, fw)
    if desc.get("all"):
        return np.ones(shape), ((r0f + r1f) / 2.0, (c0f + c1f) / 2.0), side / 2.0
    if "point" in desc:
        fx, fy = desc["point"]
        r = desc.get("radius", 0.2) * side
        cr, cc = (r0f + fy * fh, c0f + fx * fw) if play is None else _radial(fx, fy, play)
        d = np.hypot(yy - cr, xx - cc)
        return _smooth((r - d) / max(1e-6, SOFT * r)), (cr, cc), r
    if "box" in desc:
        fx0, fy0, fx1, fy1 = desc["box"]
        r0, r1, c0, c1 = r0f + fy0 * fh, r0f + fy1 * fh, c0f + fx0 * fw, c0f + fx1 * fw
        soft = SOFT * 0.5 * min(r1 - r0, c1 - c0)
        inside = np.minimum(np.minimum(yy - r0, r1 - yy), np.minimum(xx - c0, c1 - xx))
        return _smooth(inside / max(1e-6, soft) + 0.5), ((r0 + r1) / 2, (c0 + c1) / 2), \
            0.5 * min(r1 - r0, c1 - c0)
    if "edge" in desc:
        depth = desc.get("depth", 0.25) * side
        d = {"south": yy - r0f, "north": r1f - yy, "west": xx - c0f, "east": c1f - xx}[desc["edge"]]
        dx, dy = terrain.direction(desc["edge"])
        cr, cc = r0f + fh * (0.5 + dy * (0.5 - desc.get("depth", 0.25) / 2)), \
            c0f + fw * (0.5 + dx * (0.5 - desc.get("depth", 0.25) / 2))
        return _smooth((depth - d) / max(1e-6, SOFT * depth) + 0.5), (cr, cc), depth / 2
    if "near" in desc:
        f = _feature(features, desc["near"])
        extra = desc.get("extra_m", 40.0)
        if "mask" in f:
            d = distance_m(f["mask"])
        else:
            d = np.hypot(yy - f["centre"][0], xx - f["centre"][1]) * terrain.GRID_CM / 100.0 - f.get("radius_m", 0)
            d = np.maximum(d, 0.0)
        weight = _smooth((extra - d) / max(1e-6, SOFT * extra))
        cr, cc = f["centre"]
        return weight, (cr, cc), (f.get("radius_m", 0) + extra) * 100 / terrain.GRID_CM
    if "shore" in desc:
        f = _feature(features, desc["shore"])
        dx, dy = terrain.direction(desc["side"])
        cr, cc = f["centre"]
        if "mask" in f:
            # walk out from the centre in that direction to the first dry vertex
            step = 0
            while True:
                r, c = int(round(cr + dy * step)), int(round(cc + dx * step))
                if not (0 <= r < h and 0 <= c < w) or not f["mask"][r, c]:
                    break
                step += 1
            tr, tc = cr + dy * step, cc + dx * step
        else:
            k = f.get("radius_m", 0) * 100 / terrain.GRID_CM
            tr, tc = cr + dy * k, cc + dx * k
        r = desc.get("radius_m", 25.0) * 100 / terrain.GRID_CM
        d = np.hypot(yy - tr, xx - tc)
        return _smooth((r - d) / max(1e-6, SOFT * r)), (tr, tc), r
    raise ValueError("unknown area descriptor %r" % (desc,))


def _feature(features, name):
    if name not in features:
        raise ValueError("area refers to %r, which is not placed (yet); known: %s"
                         % (name, ", ".join(sorted(features)) or "none"))
    return features[name]


def fade(weight, shape, toward, low):
    """Scale a weight so it goes from 1 to `low` across its own extent toward
    a compass direction ("dense in the north, thinning toward the south")."""
    h, w = shape
    dx, dy = terrain.direction(toward)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    u = xx / (w - 1) * dx + yy / (h - 1) * dy
    sel = weight > 0.05
    if not sel.any():
        return weight
    lo, hi = float(u[sel].min()), float(u[sel].max())
    t = np.clip((u - lo) / max(1e-6, hi - lo), 0.0, 1.0)
    return weight * (1.0 - (1.0 - low) * t)
