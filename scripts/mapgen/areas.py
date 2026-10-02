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


def distance_m(mask):
    """Approximate distance (m) from every vertex to the nearest vertex of `mask`."""
    a = np.where(mask, 0.0, 1e9)
    d = terrain._cone_1d(terrain._cone_1d(a, 1.0, 1), 1.0, 0)
    return d / L1_TO_EUCLID * terrain.GRID_CM / 100.0


def _smooth(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3 - 2 * t)


def resolve(desc, shape, features):
    """(weight [0..1] per vertex, target (row, col) floats, radius in vertices)."""
    h, w = shape
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    side = min(h, w) - 1
    if desc.get("all"):
        return np.ones(shape), ((h - 1) / 2.0, (w - 1) / 2.0), side / 2.0
    if "point" in desc:
        fx, fy = desc["point"]
        r = desc.get("radius", 0.2) * side
        cr, cc = fy * (h - 1), fx * (w - 1)
        d = np.hypot(yy - cr, xx - cc)
        return _smooth((r - d) / max(1e-6, SOFT * r)), (cr, cc), r
    if "box" in desc:
        fx0, fy0, fx1, fy1 = desc["box"]
        r0, r1, c0, c1 = fy0 * (h - 1), fy1 * (h - 1), fx0 * (w - 1), fx1 * (w - 1)
        soft = SOFT * 0.5 * min(r1 - r0, c1 - c0)
        inside = np.minimum(np.minimum(yy - r0, r1 - yy), np.minimum(xx - c0, c1 - xx))
        return _smooth(inside / max(1e-6, soft) + 0.5), ((r0 + r1) / 2, (c0 + c1) / 2), \
            0.5 * min(r1 - r0, c1 - c0)
    if "edge" in desc:
        depth = desc.get("depth", 0.25) * side
        d = terrain.edge_distance(shape, (desc["edge"],))
        dx, dy = terrain.direction(desc["edge"])
        cr, cc = (h - 1) * (0.5 + dy * (0.5 - desc.get("depth", 0.25) / 2)), \
            (w - 1) * (0.5 + dx * (0.5 - desc.get("depth", 0.25) / 2))
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
