"""x_y.IFO: one per chunk, everything placed on it.

Layout and sources: docs/mapgen/FORMATS.md, "IFO". Lump numbers follow the
client enum (io_terrain.h:223-238). The editor's names differ: its WideWater
is lump 7 and its Water is lump 9 (OCEAN here).
"""

from dataclasses import dataclass, field
from typing import List, Tuple

import numpy as np

from . import container
from .binio import FormatError

MAPINFO, OBJECT, MOB, CNST, SOUND, EFFECT, MORPH, WATER, REGEN, OCEAN, WARP, \
    COLLISION, EVENT_OBJECT = range(13)

LUMP_NAMES = {MAPINFO: "MAPINFO", OBJECT: "OBJECT", MOB: "MOB", CNST: "CNST",
              SOUND: "SOUND", EFFECT: "EFFECT", MORPH: "MORPH", WATER: "WATER",
              REGEN: "REGEN", OCEAN: "OCEAN", WARP: "WARP", COLLISION: "COLLISION",
              EVENT_OBJECT: "EVENT_OBJECT"}


@dataclass
class Record:
    """The header every object lump shares (io_terrain.cpp:2157-2179)."""
    name: bytes = b""
    warp_id: int = 0
    event_id: int = 0
    obj_type: int = 0
    obj_id: int = 0
    map_x: int = 0
    map_y: int = 0
    rot: Tuple[float, float, float, float] = (0.0, 0.0, 0.0, 1.0)   # x, y, z, w
    pos: Tuple[float, float, float] = (0.0, 0.0, 0.0)               # zone-centre cm
    scale: Tuple[float, float, float] = (1.0, 1.0, 1.0)


@dataclass
class Mob(Record):
    ai: int = 0
    con_file: bytes = b""


@dataclass
class Sound(Record):
    wav: bytes = b""
    range_cm: int = 0
    interval_s: int = 0


@dataclass
class Effect(Record):
    eft: bytes = b""


@dataclass
class RegenMob:
    name: bytes
    mob: int
    count: int


@dataclass
class Regen(Record):
    point_name: bytes = b""
    basic: List[RegenMob] = field(default_factory=list)
    tactic: List[RegenMob] = field(default_factory=list)
    interval_s: int = 0
    limit: int = 0
    range_m: int = 0
    tactic_point: int = 0


@dataclass
class EventObject(Record):
    trigger: bytes = b""
    con_file: bytes = b""


@dataclass
class MapInfo:
    """Lump 0. Neither the client nor the server reads it."""
    width: int
    height: int
    cell_x: int
    cell_y: int
    world: Tuple[float, ...]      # 16 floats
    name: bytes


# Lump 7: u8 use, f32 height, i32 type, i32 index, i32 reserved (editor/IFO.cs:953-979).
WATER_CELL = np.dtype([("use", "u1"), ("height", "<f4"), ("type", "<i4"),
                       ("index", "<i4"), ("reserved", "<i4")])


@dataclass
class WideWater:
    x: int
    y: int
    cells: np.ndarray


@dataclass
class Ocean:
    """Lump 9: flat water rectangles, corners stored x, z(height), y."""
    size: float
    rects: List[Tuple[float, ...]]    # 6 floats: sx, sz, sy, ex, ez, ey


def _read_header(r, cls):
    rec = cls()
    rec.name = r.bstr()
    rec.warp_id = r.i16()
    rec.event_id = r.i16()
    rec.obj_type, rec.obj_id, rec.map_x, rec.map_y = r.i32s(4)
    rec.rot = r.f32s(4)
    rec.pos = r.f32s(3)
    rec.scale = r.f32s(3)
    return rec


def _write_header(w, rec):
    w.bstr(rec.name)
    w.i16(rec.warp_id)
    w.i16(rec.event_id)
    w.i32s((rec.obj_type, rec.obj_id, rec.map_x, rec.map_y))
    w.f32s(rec.rot)
    w.f32s(rec.pos)
    w.f32s(rec.scale)


def _count(r):
    n = r.i32()
    if n < 0 or n > 100000:
        raise FormatError("implausible record count %d" % n)
    return n


def _list_lump(cls, read_extra=None, write_extra=None):
    def parse(r):
        out = []
        for _ in range(_count(r)):
            rec = _read_header(r, cls)
            if read_extra:
                read_extra(r, rec)
            out.append(rec)
        return out

    def write(w, recs):
        w.i32(len(recs))
        for rec in recs:
            _write_header(w, rec)
            if write_extra:
                write_extra(w, rec)
    return parse, write


def _rx_mob(r, m):
    m.ai = r.i32()
    m.con_file = r.bstr()


def _wx_mob(w, m):
    w.i32(m.ai)
    w.bstr(m.con_file)


def _rx_sound(r, s):
    s.wav = r.bstr()
    s.range_cm = r.i32()
    s.interval_s = r.i32()


def _wx_sound(w, s):
    w.bstr(s.wav)
    w.i32(s.range_cm)
    w.i32(s.interval_s)


def _rx_effect(r, e):
    e.eft = r.bstr()


def _wx_effect(w, e):
    w.bstr(e.eft)


def _read_mobs(r):
    return [RegenMob(r.bstr(), r.i32(), r.i32()) for _ in range(_count(r))]


def _write_mobs(w, mobs):
    w.i32(len(mobs))
    for m in mobs:
        w.bstr(m.name)
        w.i32(m.mob)
        w.i32(m.count)


def _rx_regen(r, g):
    g.point_name = r.bstr()
    g.basic = _read_mobs(r)
    g.tactic = _read_mobs(r)
    g.interval_s, g.limit, g.range_m, g.tactic_point = r.i32s(4)


def _wx_regen(w, g):
    w.bstr(g.point_name)
    _write_mobs(w, g.basic)
    _write_mobs(w, g.tactic)
    w.i32s((g.interval_s, g.limit, g.range_m, g.tactic_point))


def _rx_event(r, e):
    e.trigger = r.pstr()
    e.con_file = r.pstr()


def _wx_event(w, e):
    w.pstr(e.trigger)
    w.pstr(e.con_file)


def _p_mapinfo(r):
    w_, h_, cx, cy = r.i32s(4)
    world = r.f32s(16)
    return MapInfo(w_, h_, cx, cy, world, r.pstr())


def _w_mapinfo(w, m):
    w.i32s((m.width, m.height, m.cell_x, m.cell_y))
    w.f32s(m.world)
    w.pstr(m.name)


def _p_water(r):
    x, y = r.i32(), r.i32()
    if x < 0 or y < 0 or x * y > 65536:
        raise FormatError("implausible water grid %dx%d" % (x, y))
    return WideWater(x, y, r.array(WATER_CELL, x * y))


def _w_water(w, ww):
    w.i32(ww.x)
    w.i32(ww.y)
    w.array(ww.cells.astype(WATER_CELL, copy=False))


def _p_ocean(r):
    size = r.f32()
    return Ocean(size, [r.f32s(6) for _ in range(_count(r))])


def _w_ocean(w, o):
    w.f32(o.size)
    w.i32(len(o.rects))
    for rect in o.rects:
        w.f32s(rect)


PARSERS, WRITERS = {MAPINFO: _p_mapinfo, WATER: _p_water, OCEAN: _p_ocean}, \
                   {MAPINFO: _w_mapinfo, WATER: _w_water, OCEAN: _w_ocean}
for _t, (_cls, _rx, _wx) in {
        OBJECT: (Record, None, None),
        CNST: (Record, None, None),
        MORPH: (Record, None, None),
        WARP: (Record, None, None),
        COLLISION: (Record, None, None),
        MOB: (Mob, _rx_mob, _wx_mob),
        SOUND: (Sound, _rx_sound, _wx_sound),
        EFFECT: (Effect, _rx_effect, _wx_effect),
        REGEN: (Regen, _rx_regen, _wx_regen),
        EVENT_OBJECT: (EventObject, _rx_event, _wx_event)}.items():
    PARSERS[_t], WRITERS[_t] = _list_lump(_cls, _rx, _wx)


def parse(data):
    return container.parse(data, PARSERS, WRITERS)


def build(ifo):
    return container.build(ifo, WRITERS)
