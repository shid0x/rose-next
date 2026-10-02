"""Tile painting from terrain (phase 3).

The painter decides a brush for every tile corner (10 m lattice; see
tiles.py), then lets tiles.legalize insert the tileset's in-between brushes
and turns the lattice into TIL records.

Brush choice is driven by statistics from the retail zones, not by
hand-tuned thresholds. `brush_slope_stats` measures, for each slope band,
how often each land brush appears at a corner of the JG zones (the committed
table is scripts/mapgen/stats/jg_brush_by_slope.json). Retail does NOT
cover its steepest ground with rock: above 60 degrees 52% of corners are
bright grass and only 9% rock. Rock peaks at 30-50 degrees, and the cliffs
proper are rock objects (phase 5).

Each corner picks argmax over brushes of

    bias[slope band][brush] + spread * noise_brush(corner)

where every brush has its own seeded low-frequency noise field. Correlated
noise makes coherent patches instead of speckle. The bias starts at
log P(brush | band) and is calibrated in a few rounds, so each band's brush
shares match retail's. Plain log-probabilities under-pick rare brushes:
dark grass came out at 3% against retail's ~12%.

Paths are least-cost routes on the corner lattice (A*; cost grows with
slope, refused above `max_path_slope_deg`) painted with the path brush.
"""

import heapq
import json
import math
import os
import re

import numpy as np

from . import chunk, terrain, tiles

SLOPE_BANDS = [0, 10, 20, 30, 40, 50, 60, 90]
WATER_BRUSHES_JG = (6, 7, 8)        # sand, seabed, gravel: shores and water, phase 4


def corner_slopes(field):
    """Mean client-gradient slope (degrees) in the 10 m window around each
    tile corner (every 4th vertex). Shape (rows/4 + 1, cols/4 + 1)."""
    gx, gy = terrain.gradient(field)
    deg = np.degrees(np.arctan(np.hypot(gx, gy)))
    rows, cols = (field.shape[0] - 1) // 4 + 1, (field.shape[1] - 1) // 4 + 1
    out = np.zeros((rows, cols))
    for r in range(rows):
        for c in range(cols):
            vr, vc = r * 4, c * 4
            win = deg[max(0, vr - 2):vr + 2, max(0, vc - 2):vc + 2]
            out[r, c] = np.nanmean(win)
    return out


def brush_slope_stats(zone_dirs, ts):
    """{band: {brush: count}} over the corners of retail zones painted with ts."""
    counts = np.zeros((len(SLOPE_BANDS) - 1, ts.brushes), int)
    for zdir in zone_dirs:
        grid, have, (x0, y0) = tiles.load_zone_tiles(zdir)
        lattice, _ = tiles.lattice_votes(grid, ts, have)
        field = np.full((grid.shape[0] * 4 + 1, grid.shape[1] * 4 + 1), np.nan)
        for f in os.listdir(zdir):
            m = re.match(r"(\d+)_(\d+)\.him$", f, re.I)
            if not m:
                continue
            x, y = int(m.group(1)), 64 - int(m.group(2))
            with open(os.path.join(zdir, f), "rb") as fh:
                h = chunk.parse_him(fh.read()).heights[::-1]
            field[(y - y0) * 64:(y - y0) * 64 + 65, (x - x0) * 64:(x - x0) * 64 + 65] = h
        with np.errstate(invalid="ignore"):
            slopes = corner_slopes(field)
        for r in range(lattice.shape[0]):
            for c in range(lattice.shape[1]):
                if lattice[r, c] < 0 or np.isnan(slopes[r, c]):
                    continue
                band = min(np.digitize(slopes[r, c], SLOPE_BANDS) - 1, len(SLOPE_BANDS) - 2)
                counts[band, lattice[r, c]] += 1
    return counts


def save_stats(path, counts, ts, zones):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"_comment": "Corner brush counts per slope band in retail zones; regenerate with "
                               "`mapgen-zone.py stats`. Rows = slope bands (degrees), cols = brushes.",
                   "zones": zones, "slope_bands": SLOPE_BANDS,
                   "brushes": ts.brush_names, "counts": counts.tolist()}, f, indent=1)


def load_stats(path):
    with open(path, encoding="utf-8") as f:
        s = json.load(f)
    return np.array(s["counts"], float), s["slope_bands"]


def astar(cost, a, b):
    """Cheapest 8-connected path on a cost grid (inf = impassable); list of (r, c)."""
    rows, cols = cost.shape
    if not (np.isfinite(cost[a]) and np.isfinite(cost[b])):
        raise ValueError("path endpoint on impassable ground: %s -> %s" % (a, b))
    best = {a: 0.0}
    prev = {}
    heap = [(math.hypot(a[0] - b[0], a[1] - b[1]), 0.0, a)]    # (estimate, cost so far, node)
    while heap:
        _, d, p = heapq.heappop(heap)
        if p == b:
            break
        if d > best.get(p, math.inf):
            continue                                          # stale entry
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                if not (dr or dc):
                    continue
                q = (p[0] + dr, p[1] + dc)
                if not (0 <= q[0] < rows and 0 <= q[1] < cols) or not np.isfinite(cost[q]):
                    continue
                nd = d + math.hypot(dr, dc) * (cost[p] + cost[q]) / 2
                if nd < best.get(q, math.inf):
                    best[q] = nd
                    prev[q] = p
                    heapq.heappush(heap, (nd + math.hypot(q[0] - b[0], q[1] - b[1]), nd, q))
    if b not in best:
        raise ValueError("no path %s -> %s under the slope limit" % (a, b))
    path, p = [b], b
    while p != a:
        p = prev[p]
        path.append(p)
    return path[::-1]


def paint(field, ts, stats, cfg, seed, waypoints, forced=None):
    """(Corner lattice (row 0 = south) of brush ids, path-corner mask) for a heightfield.

    cfg is the spec's "paint" section. waypoints is a list of paths, each a
    list of lattice (row, col) points. `forced` (lattice-shaped, -1 = free)
    pins corners to a brush: lakes' seabed and sand. Forced corners are kept
    by legalize, routed around by paths, and left out of the calibration.
    """
    counts, bands = stats
    slopes = corner_slopes(field)
    rows, cols = slopes.shape
    land = [b for b in range(ts.brushes) if b not in cfg.get("exclude_brushes", WATER_BRUSHES_JG)]
    probs = counts[:, land] + 1.0                       # +1: no brush is ever impossible
    probs /= probs.sum(axis=1, keepdims=True)
    band = np.clip(np.digitize(slopes, bands) - 1, 0, len(bands) - 2)

    rng = np.random.default_rng(seed)
    spread = cfg.get("noise_spread", 2.5)
    wave_m = cfg.get("patch_wavelength_m", 60.0)
    noise = np.empty((len(land), rows, cols))
    for i in range(len(land)):
        # fbm measures wavelength on the 2.5 m vertex grid; corners are 4 vertices apart.
        noise[i] = spread * terrain.fbm((rows, cols), wave_m / 4.0, 3, 0.5, rng)

    path_brush = cfg.get("path_brush", 0)
    on_path = np.zeros((rows, cols), bool)
    if waypoints:
        cost = 1.0 + (slopes / cfg.get("path_slope_scale_deg", 12.0)) ** 2
        cost[slopes > cfg.get("max_path_slope_deg", 30.0)] = np.inf
        if forced is not None:
            cost[forced >= 0] = np.inf                  # paths never run into water or its beach
        for pts in waypoints:
            for a, b in zip(pts, pts[1:]):
                route = astar(cost, a, b)
                for p, q in zip(route, route[1:] + route[-1:]):
                    on_path[p] = True
                    if p[0] != q[0] and p[1] != q[1]:
                        # Diagonal step: also paint a side corner so the path
                        # is edge-connected. Corners touching only at a point
                        # become a dotted row of saddle tiles.
                        side = (p[0], q[1])
                        if np.isfinite(cost[side]):
                            on_path[side] = True
                        else:
                            on_path[(q[0], p[1])] = True

    priority = np.zeros(ts.brushes)
    for rank, b in enumerate(cfg.get("priority", [])):
        priority[b] = len(cfg["priority"]) - rank
    index = {b: i for i, b in enumerate(land)}

    fixed = on_path.copy()
    if forced is not None:
        fixed |= forced >= 0

    def realise(bias):
        lattice = np.array(land)[np.argmax(bias[band].transpose(2, 0, 1) + noise, axis=0)]
        lattice[on_path] = path_brush
        if forced is not None:
            lattice[forced >= 0] = forced[forced >= 0]
        tiles.legalize(lattice, ts, priority, allow_saddles=cfg.get("allow_saddles", False),
                       protected=fixed)
        return lattice

    # Argmax over noisy scores under-picks rare brushes, and legalize turns
    # clashing low-priority corners into in-between brushes. Calibrate a
    # per-band bias on the FINAL lattice (paths and legalize included) so each
    # band's brush shares come out near the retail table. Paths are excluded
    # from the measurement: they are layout, not ground cover.
    bias = np.log(probs)                                 # (bands, brushes)
    for _ in range(cfg.get("calibration_rounds", 8)):
        lattice = realise(bias)
        for k in np.unique(band):
            sel = (band == k) & ~fixed
            if not sel.any():
                continue
            got = np.bincount([index.get(b, 0) for b in lattice[sel]], minlength=len(land)) / sel.sum()
            bias[k] += np.clip(np.log((probs[k] + 1e-3) / (got + 1e-3)), -1.0, 1.0)
    return realise(bias), on_path
