"""Barrier lines along the edge of the play area (phase 7b): fences and
boulder rims.

ROSE limits terrain with more than cliffs: fences, bridges, pitfalls (user,
2026-10-02). A low cliff cannot seal a map alone: the client tests a step
against the slope of the cell it lands in, so a curved face of only two or
three steep cells offers a sideways ramp (traced in phase 7b). A barrier
along the cliff's top lip, or a fence line with no cliff at all, blocks
walking whatever the slope does, and the walk check sees it as walls.

A barrier is objects strung along a contour of the distance to the play
region: `offset_m` outside it (the top of the cliff, or just outside the
play area for a fence-only edge). Retail's farm fence (houseguard01, an
8.6 m model) stands at scale 1.5-1.6, sunk 2.25 m, every 12-13.7 m.
"""

import math

import numpy as np

from . import terrain

KINDS = {
    # object id, model length (m), retail scale range, sink (cm per unit of scale), spacing (m)
    "fence": {"id": 189, "length_m": 8.6, "scale": (1.5, 1.6), "sink_per_scale": -150.0, "overlap_m": 1.5},
    "rocks": {"id": 86, "length_m": 8.1, "scale": (0.9, 1.2), "sink_per_scale": -150.0, "overlap_m": 1.5},
}


def contour(values, level):
    """Polylines (lists of (row, col) floats) where `values` crosses `level`
    (marching squares, chained through shared edges)."""
    v = values - level
    rows, cols = v.shape
    seg = {}

    def edge_point(key):
        kind, r, c = key
        if kind == "h":                     # between (r, c) and (r, c + 1)
            a, b = v[r, c], v[r, c + 1]
            t = a / (a - b) if a != b else 0.5
            return (r, c + t)
        a, b = v[r, c], v[r + 1, c]         # "v": between (r, c) and (r + 1, c)
        t = a / (a - b) if a != b else 0.5
        return (r + t, c)

    def link(a, b):
        seg.setdefault(a, []).append(b)
        seg.setdefault(b, []).append(a)

    for r in range(rows - 1):
        for c in range(cols - 1):
            s0, s1, s2, s3 = v[r, c] > 0, v[r, c + 1] > 0, v[r + 1, c + 1] > 0, v[r + 1, c] > 0
            edges = []
            if s0 != s1:
                edges.append(("h", r, c))           # bottom
            if s1 != s2:
                edges.append(("v", r, c + 1))       # right
            if s3 != s2:
                edges.append(("h", r + 1, c))       # top
            if s0 != s3:
                edges.append(("v", r, c))           # left
            if len(edges) == 2:
                link(edges[0], edges[1])
            elif len(edges) == 4:                   # saddle: pair by the cell's centre
                centre = (v[r, c] + v[r, c + 1] + v[r + 1, c + 1] + v[r + 1, c]) / 4 > 0
                if centre == s0:
                    link(edges[0], edges[1]); link(edges[2], edges[3])
                else:
                    link(edges[0], edges[3]); link(edges[1], edges[2])
    lines, seen = [], set()
    for start in list(seg):
        if start in seen or len(seg[start]) != 1:
            continue
        lines.append(_walk(start, seg, seen))
    for start in list(seg):                         # closed loops
        if start not in seen:
            lines.append(_walk(start, seg, seen))
    return [[edge_point(k) for k in line] for line in lines if len(line) >= 2]


def _walk(start, seg, seen):
    line, prev, cur = [start], None, start
    seen.add(start)
    while True:
        nxt = [n for n in seg[cur] if n != prev and n not in seen]
        if not nxt:
            if len(line) > 2 and start in seg[cur] and prev is not None:
                line.append(start)                  # close the loop
            return line
        prev, cur = cur, nxt[0]
        seen.add(cur)
        line.append(cur)


def place(field, play, offset_m, kind, rng, skip=None):
    """Barrier objects along the contour `offset_m` outside `play`. Returns
    placed dicts (decorate.place format: lump, id, x, y, z, rot, scale,
    sink, category BARRIER). Points over `skip` (vertex mask: water) are
    left open."""
    from .areas import distance_m
    spec = KINDS[kind]
    d = distance_m(play)
    g = terrain.GRID_CM
    out = []
    for line in contour(d, offset_m):
        pts = np.array(line) * g                    # (row, col) in cm
        if len(pts) < 2:
            continue
        seglen = np.hypot(*(np.diff(pts, axis=0).T))
        total = float(seglen.sum())
        if total < 300:
            continue
        cum = np.concatenate([[0.0], np.cumsum(seglen)])
        s_min, s_max = spec["scale"]
        pos = 0.0
        while pos < total:
            sc = float(rng.uniform(s_min, s_max))
            length = spec["length_m"] * sc * 100
            step = length - spec["overlap_m"] * 100
            mid = min(pos + length / 2, total)
            a, b = min(pos, total), min(pos + length, total)

            def at(sv):
                i = min(int(np.searchsorted(cum, sv, side="right") - 1), len(seglen) - 1)
                t = (sv - cum[i]) / max(1e-6, seglen[i])
                return pts[i] + (pts[i + 1] - pts[i]) * t

            pa, pb, pm = at(a), at(b), at(mid)
            yaw = math.atan2(pb[0] - pa[0], pb[1] - pa[1])   # model x along the line
            if kind == "rocks":
                yaw = float(rng.uniform(0, 2 * math.pi))
            r, c = pm[0] / g, pm[1] / g
            ri, ci = int(round(r)), int(round(c))
            if not (0 <= ri < field.shape[0] and 0 <= ci < field.shape[1]):
                pos += step
                continue
            if skip is not None and skip[ri, ci]:
                pos += step
                continue
            ground = float(_bilinear(field, r, c))
            sink = spec["sink_per_scale"] * sc
            out.append({"lump": "OBJECT", "id": spec["id"], "name": kind, "x": float(pm[1]), "y": float(pm[0]),
                        "z": ground + sink, "rot": (0.0, 0.0, math.sin(yaw / 2), math.cos(yaw / 2)),
                        "scale": (sc, sc, sc), "sink": sink, "rc": 0.0, "rv": 0.0, "category": "BARRIER"})
            pos += step
    return out


def _bilinear(field, r, c):
    r0, c0 = min(int(r), field.shape[0] - 2), min(int(c), field.shape[1] - 2)
    fr, fc = r - r0, c - c0
    return (field[r0, c0] * (1 - fr) * (1 - fc) + field[r0, c0 + 1] * (1 - fr) * fc
            + field[r0 + 1, c0] * fr * (1 - fc) + field[r0 + 1, c0 + 1] * fr * fc)
