"""Per-chunk grid files: .HIM (heights), .TIL (tiles), .MOV (AI walkability).

Layouts and sources: docs/mapgen/FORMATS.md. Row order matters and differs:

* HIM and TIL: the first row in the file is the NORTH edge.
* MOV: the first row is the SOUTH edge.

The arrays below are kept in file order. Helpers that flip them belong to the
generator, not the codec.
"""

from dataclasses import dataclass
from typing import Optional

import numpy as np

from .binio import FormatError, Reader, Writer

# ---------------------------------------------------------------------- HIM


@dataclass
class HimBounds:
    """Culling bounds after the heights (io_terrain.cpp:2565-2591).

    Pairs are (max, min) in cm. Retail ships real values or the editor's
    +/-FLT_MAX placeholders.
    """
    name: bytes                 # "quad"
    patches: np.ndarray         # (patch_count, 2) float32, normally 256
    quads: np.ndarray           # (quad_count, 2) float32, normally 85


@dataclass
class Him:
    width: int                  # 65
    height: int                 # 65
    grid_per_patch: int         # 4
    patch_size: float           # the HIM's own copy; 250 in retail files, not used for layout
    heights: np.ndarray         # (height, width) float32 cm, row 0 = north
    bounds: Optional[HimBounds] = None
    tail: bytes = b""


def compute_him_bounds(heights):
    """Culling bounds exactly as retail tools wrote them (verified 2026-10-02:
    reproduces all 663 retail HIMs that carry real bounds, patches and quads).

    `heights` is the HIM's (65, 65) array in FILE order (row 0 = north).

    * Patches are stored SOUTH row first: patch k covers patch row k // 16
      from the south, column k % 16 from the west. The client reads them into
      m_PATCH[k/16][k%16] (io_terrain.cpp:2577-2583), whose row index grows
      north (:1320-1329). Each is (max, min) over the patch's 5x5 vertices.
    * The 85 quads are the client's tree (io_terrain.cpp:780-827): node i has
      children 4i+1..4i+4 at (x,y), (x+h,y), (x+h,y+h), (x,y+h) in patch units
      from the south-west, levels of 16/8/4/2 patches. Each is the max/min of
      its patches, seeded with max = -10 and min = 10000. That is the original
      tool's quirk, also in editor/HIM.cs:317-340, and it decides 255 retail
      files.
    """
    s = np.asarray(heights, dtype="<f4")[::-1]          # row 0 = south
    pmax = np.empty((16, 16), "<f4")
    pmin = np.empty((16, 16), "<f4")
    for r in range(16):
        for c in range(16):
            block = s[r * 4:r * 4 + 5, c * 4:c * 4 + 5]
            pmax[r, c], pmin[r, c] = block.max(), block.min()
    quads = np.empty((85, 2), "<f4")

    def node(i, level, size, x, y):
        if level >= 4:
            return
        quads[i, 0] = max(np.float32(-10.0), pmax[y:y + size, x:x + size].max())
        quads[i, 1] = min(np.float32(10000.0), pmin[y:y + size, x:x + size].min())
        h = size // 2
        node(4 * i + 1, level + 1, h, x, y)
        node(4 * i + 2, level + 1, h, x + h, y)
        node(4 * i + 3, level + 1, h, x + h, y + h)
        node(4 * i + 4, level + 1, h, x, y + h)

    node(0, 0, 16, 0, 0)
    return HimBounds(b"quad", np.stack([pmax.ravel(), pmin.ravel()], axis=1), quads)


def parse_him(data):
    r = Reader(data)
    w, h, g = r.i32(), r.i32(), r.i32()
    ps = r.f32()
    if not (0 < w <= 1025 and 0 < h <= 1025):
        raise FormatError("implausible HIM size %dx%d" % (w, h))
    heights = r.array("<f4", w * h).reshape(h, w)
    him = Him(w, h, g, ps, heights)
    if r.remaining():
        start = r.pos
        try:
            name = r.pstr()
            pc = r.i32()
            patches = r.array("<f4", pc * 2).reshape(pc, 2)
            qc = r.i32()
            quads = r.array("<f4", qc * 2).reshape(qc, 2)
            him.bounds = HimBounds(name, patches, quads)
        except FormatError:
            r.pos = start
            him.bounds = None
    him.tail = r.rest()
    return him


def build_him(him):
    w = Writer()
    w.i32(him.width)
    w.i32(him.height)
    w.i32(him.grid_per_patch)
    w.f32(him.patch_size)
    w.array(him.heights.astype("<f4", copy=False))
    if him.bounds is not None:
        b = him.bounds
        w.pstr(b.name)
        w.i32(len(b.patches))
        w.array(b.patches.astype("<f4", copy=False))
        w.i32(len(b.quads))
        w.array(b.quads.astype("<f4", copy=False))
    w.raw(him.tail)
    return w.getvalue()


# ---------------------------------------------------------------------- TIL

TILE_DTYPE = np.dtype([("brush", "u1"), ("tile_index", "u1"), ("tile_set", "u1"),
                       ("tile_id", "<i4")])   # 7 bytes, packed


@dataclass
class Til:
    width: int                  # 16
    height: int                 # 16
    tiles: np.ndarray           # (height, width) TILE_DTYPE, row 0 = north
    tail: bytes = b""


def parse_til(data):
    r = Reader(data)
    w, h = r.i32(), r.i32()
    if not (0 < w <= 256 and 0 < h <= 256):
        raise FormatError("implausible TIL size %dx%d" % (w, h))
    tiles = r.array(TILE_DTYPE, w * h).reshape(h, w)
    return Til(w, h, tiles, r.rest())


def build_til(til):
    w = Writer()
    w.i32(til.width)
    w.i32(til.height)
    w.array(til.tiles.astype(TILE_DTYPE, copy=False))
    w.raw(til.tail)
    return w.getvalue()


# ---------------------------------------------------------------------- MOV


@dataclass
class Mov:
    width: int                  # 32
    height: int                 # 32
    cells: np.ndarray           # (height, width) uint8, row 0 = SOUTH; 0 = AI may move
    tail: bytes = b""


def parse_mov(data):
    r = Reader(data)
    w, h = r.i32(), r.i32()
    if not (0 < w <= 1024 and 0 < h <= 1024):
        raise FormatError("implausible MOV size %dx%d" % (w, h))
    return Mov(w, h, r.array("u1", w * h).reshape(h, w), r.rest())


def build_mov(mov):
    w = Writer()
    w.i32(mov.width)
    w.i32(mov.height)
    w.array(mov.cells.astype("u1", copy=False))
    w.raw(mov.tail)
    return w.getvalue()
