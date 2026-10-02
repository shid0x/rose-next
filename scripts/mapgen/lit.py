"""LightMap/BuildingLightMapData.lit and ObjectLightMapData.lit.

Which lightmap atlas cell each object part uses. Layout and sources:
docs/mapgen/FORMATS.md, "LIT". Objects are keyed by their 1-based ordinal
in IFO lump 3 (building file) or lump 1 (object file).
"""

from dataclasses import dataclass, field
from typing import List, Optional

from .binio import FormatError, Reader, Writer


@dataclass
class LitPart:
    tga_name: bytes             # skipped by the client
    part_index: int
    dds_name: bytes             # atlas file in the chunk's LightMap folder
    lightmap_index: int         # skipped by the client
    pixels_per_part: int
    parts_per_width: int
    position_in_map: int        # cell = (pos % per_width, pos // per_width)


@dataclass
class LitObject:
    obj_index: int              # 1-based ordinal in the IFO lump
    parts: List[LitPart] = field(default_factory=list)


@dataclass
class Lit:
    objects: List[LitObject] = field(default_factory=list)
    dds_list: Optional[List[bytes]] = None   # optional catalogue the game never reads
    tail: bytes = b""


def _count(r, what):
    n = r.i32()
    if n < 0 or n > 100000:
        raise FormatError("implausible %s count %d" % (what, n))
    return n


def parse_lit(data):
    r = Reader(data)
    lit = Lit()
    for _ in range(_count(r, "object")):
        pc = _count(r, "part")
        obj = LitObject(r.i32())
        for _ in range(pc):
            tga = r.bstr()
            pi = r.i32()
            dds = r.bstr()
            li, ppp, ppw, pos = r.i32s(4)
            obj.parts.append(LitPart(tga, pi, dds, li, ppp, ppw, pos))
        lit.objects.append(obj)
    if r.remaining() >= 4:
        start = r.pos
        try:
            lit.dds_list = [r.bstr() for _ in range(_count(r, "dds"))]
        except FormatError:
            r.pos = start
            lit.dds_list = None
    lit.tail = r.rest()
    return lit


def build_lit(lit):
    w = Writer()
    w.i32(len(lit.objects))
    for obj in lit.objects:
        w.i32(len(obj.parts))
        w.i32(obj.obj_index)
        for p in obj.parts:
            w.bstr(p.tga_name)
            w.i32(p.part_index)
            w.bstr(p.dds_name)
            w.i32s((p.lightmap_index, p.pixels_per_part, p.parts_per_width, p.position_in_map))
    if lit.dds_list is not None:
        w.i32(len(lit.dds_list))
        for d in lit.dds_list:
            w.bstr(d)
    w.raw(lit.tail)
    return w.getvalue()
