"""<zone>.ZON: one per zone. Layout and sources: docs/mapgen/FORMATS.md, "ZON".

Lumps: 0 info, 1 event positions, 2 tile textures, 3 tile table, 4 economy
(io_terrain.h:324-331).
"""

from dataclasses import dataclass, field
from typing import List, Optional

import numpy as np

from . import container
from .binio import FormatError

INFO, EVENTS, TEXTURES, TILES, ECONOMY = 0, 1, 2, 3, 4

# Classic lump 0 carries a width x height table the game never reads
# (editor/ZON.cs:522-534): u8 used + f32 x + f32 y per cell, x-major.
CELL_DTYPE = np.dtype([("used", "u1"), ("x", "<f4"), ("y", "<f4")])


@dataclass
class ZoneInfo:
    zone_type: int          # editor-only: ZONETYPEINFO.STB row
    width: int              # unused by the game
    height: int
    grid_per_patch: int     # 4 in every zone we ship
    grid_size: float        # 250.0 cm
    start_x: int            # centre chunk slot
    start_y: int
    cells: Optional[np.ndarray] = None   # None in late-Jrose ZONs (28-byte lump)


@dataclass
class EventPos:
    x: float                # zone-centre-relative cm
    z: float                # height, absolute cm
    y: float
    name: bytes


@dataclass
class Economy:
    zone_name: bytes
    is_dungeon: int
    bgm: bytes
    sky: bytes
    values: List[int]       # town_counter, pop_base, dev_base, 10 x consumption


ECONOMY_INTS = 3 + 10       # ceconomy.cpp:70-98, datatype.h:218-219


def _p_info(r):
    zi = ZoneInfo(r.i32(), r.i32(), r.i32(), r.i32(), r.f32(), r.i32(), r.i32())
    n = zi.width * zi.height
    if n > 0 and r.remaining() >= n * CELL_DTYPE.itemsize:
        zi.cells = r.array(CELL_DTYPE, n)
    return zi


def _w_info(w, zi):
    w.i32(zi.zone_type); w.i32(zi.width); w.i32(zi.height)
    w.i32(zi.grid_per_patch); w.f32(zi.grid_size)
    w.i32(zi.start_x); w.i32(zi.start_y)
    if zi.cells is not None:
        w.array(zi.cells.astype(CELL_DTYPE, copy=False))


def _p_events(r):
    n = r.i32()
    if n < 0 or n > 10000:
        raise FormatError("implausible event count %d" % n)
    out = []
    for _ in range(n):
        x, z, y = r.f32s(3)
        out.append(EventPos(x, z, y, r.bstr()))
    return out


def _w_events(w, evs):
    w.i32(len(evs))
    for e in evs:
        w.f32s((e.x, e.z, e.y))
        w.bstr(e.name)


def _p_textures(r):
    n = r.i32()
    if n < 0 or n > 100000:
        raise FormatError("implausible texture count %d" % n)
    return [r.bstr() for _ in range(n)]


def _w_textures(w, texs):
    w.i32(len(texs))
    for t in texs:
        w.bstr(t)


def _p_tiles(r):
    n = r.i32()
    if n < 0 or n > 100000:
        raise FormatError("implausible tile count %d" % n)
    # columns: texture1_id, texture2_id, texture1_offset, texture2_offset,
    #          is_blending, orientation, type  (io_terrain.h:384-392)
    return r.array("<i4", n * 7).reshape(n, 7)


def _w_tiles(w, tiles):
    w.i32(len(tiles))
    w.array(np.asarray(tiles, dtype="<i4"))


def _p_economy(r):
    return Economy(r.bstr(), r.i32(), r.bstr(), r.bstr(), list(r.i32s(ECONOMY_INTS)))


def _w_economy(w, e):
    w.bstr(e.zone_name); w.i32(e.is_dungeon); w.bstr(e.bgm); w.bstr(e.sky)
    w.i32s(e.values)


PARSERS = {INFO: _p_info, EVENTS: _p_events, TEXTURES: _p_textures,
           TILES: _p_tiles, ECONOMY: _p_economy}
WRITERS = {INFO: _w_info, EVENTS: _w_events, TEXTURES: _w_textures,
           TILES: _w_tiles, ECONOMY: _w_economy}


def parse(data):
    return container.parse(data, PARSERS, WRITERS)


def build(zon):
    return container.build(zon, WRITERS)
