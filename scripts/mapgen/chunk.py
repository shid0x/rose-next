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
