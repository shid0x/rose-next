#!/usr/bin/env python3
"""Place the Cerberus Lair's ice wall and prove it seals the crater.

The lair (zone 49) has no walkability data of its own -- its `.MOV` is open
everywhere and every wall is client collision -- and the "crater rim" is a
north-south terrain ridge at x ~5068 m with a few rocks on it, open over the ridge
and around its south end. The wall is invisible collision panels
(`LIST_DECO_SPECIAL.ZSC` object 2, what the map's own IFO collision boxes draw)
along the ridge, and a row of Luna ice crystals (`LIST_DECO_LP.ZSC` objects
44/46/49, the ones with a solid colliding part) across the southern gap -- the
only place the wall is meant to be seen (2026-10-10, after the first test: ice
everywhere on the ridge looked wrong). Spawned at run start by cerberus_lair.cpp
as dynamic zone objects (GSV_ZONE_OBJECTS) and taken down when the Warden of the
Seal dies.
doc/cerberus-ice-wall.md has the design.

This script is the proof and the source of the C++ constants:

  1. walks SEGMENTS (world metres): a panel every BOX_LEN m on a "box" segment
     (sunk BOX_SINK m, BOX_HEIGHT m tall, BOX_THICK m thick, turned along the
     segment), a crystal every SPACING m on an "ice" segment, at the terrain
     height, with a rotation about Z that varies per piece;
  2. rasterises each piece's REAL colliding geometry the way the map generator
     does (mapgen.catalogue.Footprints: steep triangles within the body's height
     band on the 2.5 m grid) onto the lair's own collision footprint;
  3. floods the walkable cells from the lair's start point and refuses to emit
     anything unless the Warden's spot is still reachable and Cerberus's is not;
  4. prints the placements as the C++ initialiser for ICE_WALL in cerberus_lair.cpp
     (tagZONE_OBJECT: object id, kind 0 = zone deco / 1 = special collision panel,
     world cm x/y/z, rotation about Z in degrees, scale % per axis).

usage: cerberus-wall.py [--spacing M] [--seed N] [--emit]
"""
import argparse
import importlib.util
import inspect
import math
import os
import random
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from mapgen import catalogue, lighting, terrain   # noqa: E402

ZONE = "49"
# The cut, world metres: the ridge from the northern rock mass down to the
# ridge's south end (invisible), then across to the south-eastern rocks (ice).
SEGMENTS = [("box", [(5068.0, 5592.0), (5068.0, 5500.0)]),
            ("ice", [(5068.0, 5500.0), (5112.0, 5478.0)])]
# Single-spike crystals only: mauice05 (48, 8 m wide, 19 m tall) and mauice07 (50,
# 12 m, 45 m tall, kept at 60 % height). The clusters (44/45/46/47/49) are several
# shards with open ground between them, which read as holes in the wall (2026-10-10).
PIECES = [48, 50, 48, 48, 50, 48]      # cycled along an ice segment
PIECE_SCALE = {50: (1.0, 1.0, 0.6)}
SPACING = 5.0                          # m between crystal centres: the spikes interlock
ICE_ROWS, ICE_ROW_GAP = 2, 3.5         # a second, staggered row behind closes slits seen at an angle
BOX_ID = 2                             # LIST_DECO_SPECIAL object: the invisible collision panel
BOX_LEN, BOX_THICK, BOX_HEIGHT, BOX_SINK = 8.0, 1.0, 14.0, 4.0   # m; sunk so a slope leaves no gap under it
START = (4839.8, 5550.0)               # the lair's `start` event point
WARDEN = (5032.0, 5537.0)              # cerberus_lair.cpp RUN_SPAWNS[2]
CRATER = (5108.0, 5547.2)              # CRATER_X/Y


def load_zone():
    spec = importlib.util.spec_from_file_location("relight_zone", os.path.join(ROOT, "scripts", "relight-zone.py"))
    rz = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, ["relight-zone.py"]
    spec.loader.exec_module(rz)
    sys.argv = argv
    loader = next(f for _n, f in inspect.getmembers(rz, inspect.isfunction) if "same[0][3]" in inspect.getsource(f))
    return loader(ZONE)


def quat_z(deg):
    a = math.radians(deg) / 2
    return (0.0, 0.0, math.sin(a), math.cos(a))


def box_model(fp):
    """(length, thickness, height, z_lo) m of the special collision panel, from its
    colliding geometry: the scale per axis is wanted size / this."""
    meshes, objs = fp.zsc["COLLISION"]
    lo, hi, _ = catalogue.object_box(fp.data_dir, meshes, objs[BOX_ID], colliding_only=True)
    return (hi[0] - lo[0]) / 100, (hi[1] - lo[1]) / 100, (hi[2] - lo[2]) / 100, lo[2] / 100


def placements(zone, fp, spacing, seed):
    """[(kind, id, world x m, world y m, world z m, rot deg, sx, sy, sz)] along
    SEGMENTS; scales as factors (1.0 = the model's size)."""
    rnd = random.Random(seed)
    ox, oy = zone.x0 * 160.0, zone.y0 * 160.0
    blen, bthick, bheight, bzlo = box_model(fp)
    out, k = [], 0

    def ground(x, y):
        return float(lighting.ground_cm(zone.field_f, np.array([(x - ox) * 100]), np.array([(y - oy) * 100]))[0]) / 100

    for kind, pts in SEGMENTS:
        step = BOX_LEN if kind == "box" else spacing
        for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
            length = math.hypot(x2 - x1, y2 - y1)
            heading = math.degrees(math.atan2(y2 - y1, x2 - x1))
            n = max(1, int(math.ceil(length / step)))
            for i in range(n):
                t = (i + 0.5) / n if kind == "box" else min(1.0, i * step / length)
                x, y = x1 + (x2 - x1) * t, y1 + (y2 - y1) * t
                if kind == "box":
                    sx, sy, sz = (length / n) / blen, BOX_THICK / bthick, BOX_HEIGHT / bheight
                    z = ground(x, y) - BOX_SINK - bzlo * sz
                    out.append(("box", BOX_ID, round(x, 1), round(y, 1), round(z, 2), round(heading) % 360,
                                round(sx, 3), round(sy, 3), round(sz, 3)))
                else:
                    # rows sit across the segment's direction, staggered by half a step
                    nx, ny = -(y2 - y1) / length, (x2 - x1) / length
                    for row in range(ICE_ROWS):
                        shift = (row - (ICE_ROWS - 1) / 2) * ICE_ROW_GAP
                        along = step / 2 if row % 2 else 0.0
                        px, py = x + nx * shift + (x2 - x1) / length * along, y + ny * shift + (y2 - y1) / length * along
                        oid = PIECES[k % len(PIECES)]
                        sx, sy, sz = PIECE_SCALE.get(oid, (1.0, 1.0, 1.0))
                        out.append(("ice", oid, round(px, 1), round(py, 1), round(ground(px, py), 2),
                                    rnd.randrange(0, 360, 15), sx, sy, sz))
                        k += 1
    return out


def rock_footprint(zone, fp):
    H, W = zone.height * 64, zone.width * 64
    rock = np.zeros((H + 1, W + 1), bool)
    for q in zone.placed(set()):
        fp.mark(rock, q)
    return rock


def drop_buried(zone, rock, pieces):
    """Pieces whose centre cell and its four neighbours are already rock would only
    clip into the rock: leave them out."""
    g = terrain.GRID_CM
    ox, oy = zone.x0 * 160.0, zone.y0 * 160.0
    keep = []
    for p in pieces:
        r, c = int((p[3] - oy) * 100 // g), int((p[2] - ox) * 100 // g)
        around = [rock[r + dr, c + dc] for dr, dc in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1))
                  if 0 <= r + dr < rock.shape[0] and 0 <= c + dc < rock.shape[1]]
        if not all(around):
            keep.append(p)
    return keep


def check(zone, fp, rock, pieces, show=True, strict=False):
    """strict: each ice footprint is eroded by one cell first, so the seal must
    survive the raster's optimism (a cell counts as blocked when any wall point lands
    in it, so a real gap narrower than 2.5 m could hide inside one)."""
    g = terrain.GRID_CM
    H, W = zone.height * 64, zone.width * 64
    ox, oy = zone.x0 * 160.0, zone.y0 * 160.0
    ice, box = np.zeros_like(rock), np.zeros_like(rock)
    for kind, oid, x, y, z, deg, sx, sy, sz in pieces:
        # a box is placed by its sunk centre: its "ground" for the band is the terrain
        sink = 0.0 if kind == "ice" else -BOX_SINK
        fp.mark(ice if kind == "ice" else box,
                {"lump": "OBJECT" if kind == "ice" else "COLLISION", "id": oid,
                 "x": (x - ox) * 100, "y": (y - oy) * 100, "z": z * 100,
                 "rot": quat_z(deg), "scale": (sx, sy, sz), "sink": sink})
    if strict:
        # Crystals only: a blob marks cells it barely grazes. A panel is one continuous
        # quad from this script's own geometry, and panels on a segment abut; eroding a
        # one-cell line would erase it whole.
        padded = np.pad(ice | rock, 1, constant_values=True)
        core = padded[1:-1, 1:-1] & padded[:-2, 1:-1] & padded[2:, 1:-1] & padded[1:-1, :-2] & padded[1:-1, 2:]
        ice = ice & core
    ice |= box
    solid = rock | ice | np.isnan(zone.field[:H + 1, :W + 1])

    def cell(p):
        return int((p[1] - oy) * 100 // g), int((p[0] - ox) * 100 // g)

    reach = np.zeros_like(solid)
    stack = [cell(START)]
    reach[stack[0]] = True
    while stack:
        r, c = stack.pop()
        for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            rr, cc = r + dr, c + dc
            if 0 <= rr <= H and 0 <= cc <= W and not solid[rr, cc] and not reach[rr, cc]:
                reach[rr, cc] = True
                stack.append((rr, cc))
    warden_ok, crater_cut = bool(reach[cell(WARDEN)]), not reach[cell(CRATER)]
    if show:
        c0, c1 = cell((5000.0, 0))[1], cell((5135.0, 0))[1]
        r0, r1 = cell((0, 5440.0))[0], cell((0, 5600.0))[0]
        print("      " + "".join(str(int(ox + c * g / 100) // 10 % 10) for c in range(c0, c1 + 1)))
        for r in range(r1, r0 - 1, -1):
            line = ""
            for c in range(c0, c1 + 1):
                ch = "#" if rock[r, c] else ("=" if ice[r, c] else ("," if reach[r, c] else "."))
                if np.isnan(zone.field[r, c]):
                    ch = " "
                for mark, p in (("S", START), ("W", WARDEN), ("C", CRATER)):
                    if (r, c) == cell(p):
                        ch = mark
                line += ch
            print("%4d %s" % (int(oy + r * g / 100), line))
    return warden_ok, crater_cut, int(ice.sum())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--spacing", type=float, default=SPACING)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--emit", action="store_true", help="print the C++ initialiser")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--loose", action="store_true", help="skip the eroded (strict) check")
    a = ap.parse_args()
    zone = load_zone()
    fp = catalogue.Footprints(zone.data_dir, zone.deco_rel, zone.cnst_rel)
    rock = rock_footprint(zone, fp)
    pieces = drop_buried(zone, rock, placements(zone, fp, a.spacing, a.seed))
    warden_ok, crater_cut, cells = check(zone, fp, rock, pieces, show=not a.quiet)
    print("%d pieces (%d panels, %d crystals), %d blocked cells; Warden reachable from start: %s; Cerberus cut off: %s"
          % (len(pieces), sum(p[0] == "box" for p in pieces), sum(p[0] == "ice" for p in pieces),
             cells, warden_ok, crater_cut))
    if not a.loose:
        s_ok, s_cut, _ = check(zone, fp, rock, pieces, show=False, strict=True)
        print("strict (footprints eroded by one cell): Warden reachable %s; Cerberus cut off: %s" % (s_ok, s_cut))
        warden_ok, crater_cut = warden_ok and s_ok, crater_cut and s_cut
    if not (warden_ok and crater_cut):
        print("NOT SEALED -- nothing emitted")
        return 1
    if a.emit:
        print("\n// scripts/cerberus-wall.py --spacing %g --seed %d --emit: %d pieces" % (a.spacing, a.seed, len(pieces)))
        print("const tagZONE_OBJECT ICE_WALL[] = {   // id, kind (1 = invisible panel), world cm, rotation deg, scale % x/y/z")
        for kind, oid, x, y, z, deg, sx, sy, sz in pieces:
            print("    {%d, %d, %.0f.f, %.0f.f, %.0f.f, %d, %d, %d, %d},"
                  % (oid, 1 if kind == "box" else 0, x * 100, y * 100, z * 100, deg,
                     round(sx * 100), round(sy * 100), round(sz * 100)))
        print("};")
    return 0


if __name__ == "__main__":
    sys.exit(main())
