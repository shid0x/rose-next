"""Build a complete zone folder in memory from parameters (phase 1: flat).

`build_zone(params, template_zon)` returns {relative path: bytes} for the
folder: the .ZON plus, per chunk, .HIM/.TIL/.IFO/.MOV and the two empty LIT
files. It is pure: no file system access, so `scripts/mapgen-zone.py` can
write the result anywhere, diff it against the editor's output, or install it.

Chunk addressing (docs/mapgen/FORMATS.md, "Units and coordinates"):

* a chunk slot (x, y) has y growing north;
* its files are named "<x>_<64-y>";
* positions in the ZON/IFO are relative to the centre of slot (32, 32),
  i.e. world - 520000 cm.

No per-chunk ground lightmap is written: the client falls back to
3DDATA\\TERRAIN\\default_light.dds, a uniform grey (132,125,132) close to a
retail JG chunk's mean (137,134,119).
"""

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import numpy as np

from . import chunk, ifo, lit, terrain, zon
from .container import Container, Region

CHUNK_CM = 16000            # 16 patches x 4 grids x 250 cm
ZONE_ORIGIN_CM = 32 * CHUNK_CM + CHUNK_CM // 2   # 520000: IFO/ZON origin
FLT_MAX = np.float32(3.4028234663852886e+38)


@dataclass
class Tile:
    """One TIL record. tile_id indexes the ZON tile table; the other three are
    the editor's autotile state (FORMATS.md, TIL)."""
    brush: int
    tile_set: int
    tile_index: int
    tile_id: int


@dataclass
class EventPoint:
    name: str
    world_x: float          # absolute world cm
    world_y: float
    height: float           # cm


@dataclass
class ZoneParams:
    folder: str                              # e.g. "MAPGEN01"; the ZON is <folder>.ZON
    x0: int                                  # south-west chunk slot
    y0: int
    width: int                               # chunks, east
    height: int                              # chunks, north
    ground_cm: float
    tile: Tile
    events: List[EventPoint]
    zone_type: int = 0                       # editor brush palette: 0 = JG
    start_chunk: Optional[Tuple[int, int]] = None   # ZON lump 0; client overwrites it
    economy: Optional[zon.Economy] = None    # None = copy the template's
    file_case_upper: bool = True             # chunk extensions, as retail/editor write them
    # Global heightfield (terrain.field_shape(width, height), row 0 = south, cm).
    # None = flat at ground_cm with the editor's placeholder bounds (phase 1).
    field: Optional[np.ndarray] = None
    # Global tile grid (16*height, 16*width) of chunk.TILE_DTYPE, row 0 = south
    # (phase 3, from paint + tiles.tiles_from_lattice). None = `tile` everywhere.
    tile_grid: Optional[np.ndarray] = None
    # Water rectangles per chunk slot (x, y): [(sx, sz, sy, ex, ez, ey), ...]
    # in zone-relative cm, as water.rects_for builds them (phase 4).
    water: Optional[dict] = None
    # Decoration records per chunk slot (x, y), as decorate.place builds them
    # (phase 5): IFO lump 1, obj_id into the zone's DECO ZSC.
    objects: Optional[dict] = None
    # Construction records per chunk slot (IFO lump 3, obj_id into the CNST ZSC), phase 6.
    cnst: Optional[dict] = None
    # Invisible collision boxes per chunk slot (IFO lump 11, the client's
    # Add_CollisionBox: LIST_DECO_SPECIAL.ZSC object 2), phase 7c.
    collision: Optional[dict] = None
    # Baked plane lightmaps (DDS bytes) per chunk slot, phase 8; None = the
    # client's default_light.dds.
    lightmaps: Optional[dict] = None
    # Baked object lightmaps per chunk slot, phase 8 step 3: {"OBJECT": lit
    # bytes, "CNST": lit bytes, "dds": {atlas name: DDS bytes}}; None = the
    # empty .lit files File > New writes (objects keep vertex lighting).
    object_lights: Optional[dict] = None

    def chunks(self):
        for y in range(self.y0, self.y0 + self.height):
            for x in range(self.x0, self.x0 + self.width):
                yield x, y


def chunk_stem(x, y):
    """File stem of chunk slot (x, y): '<x>_<64-y>'."""
    return "%d_%d" % (x, 64 - y)


def centre_world(p):
    """World (x, y) cm of the centre of the chunk rectangle."""
    return ((p.x0 + p.width / 2.0) * CHUNK_CM, (p.y0 + p.height / 2.0) * CHUNK_CM)


# --------------------------------------------------------------------- files


def make_him(ground_cm):
    """Flat chunk with the editor's +/-FLT_MAX placeholder bounds, exactly as
    File > New writes it (kept so the editor oracle stays byte-identical)."""
    heights = np.full((65, 65), ground_cm, dtype="<f4")
    pair = np.array([FLT_MAX, -FLT_MAX], dtype="<f4")
    bounds = chunk.HimBounds(b"quad", np.tile(pair, (256, 1)), np.tile(pair, (85, 1)))
    return chunk.Him(65, 65, 4, 250.0, heights, bounds)


def make_him_from(heights_file_order):
    """Chunk from real heights, with real culling bounds computed the retail way."""
    h = np.ascontiguousarray(heights_file_order, dtype="<f4")
    return chunk.Him(65, 65, 4, 250.0, h, chunk.compute_him_bounds(h))


def make_til(t):
    tiles = np.zeros((16, 16), dtype=chunk.TILE_DTYPE)
    tiles["brush"], tiles["tile_set"] = t.brush, t.tile_set
    tiles["tile_index"], tiles["tile_id"] = t.tile_index, t.tile_id
    return chunk.Til(16, 16, tiles)


def make_mov():
    return chunk.Mov(32, 32, np.zeros((32, 32), dtype="u1"))   # 0 = AI may move


def make_ifo(stem_x, stem_y, water_rects=(), objects=(), cnst=(), collision=()):
    """An empty chunk IFO laid out exactly as the editor's File > New writes it:
    all 13 lumps in enum order (editor/IFO.cs:1202-1500, New.xaml.cs:266-301)."""
    water = np.zeros(256, dtype=ifo.WATER_CELL)
    water["type"] = 1
    lumps = [
        (ifo.MAPINFO, ifo.MapInfo(16, 16, stem_x, stem_y, (0.0,) * 16,
                                  ("%d_%d" % (stem_x, stem_y)).encode())),
        (ifo.OBJECT, list(objects)), (ifo.MOB, []), (ifo.CNST, list(cnst)), (ifo.SOUND, []),
        (ifo.EFFECT, []), (ifo.MORPH, []),
        (ifo.WATER, ifo.WideWater(16, 16, water)),
        (ifo.REGEN, []),
        (ifo.OCEAN, ifo.Ocean(2000.0, list(water_rects))),   # 2000: retail/editor texture repeat
        (ifo.WARP, []), (ifo.COLLISION, list(collision)), (ifo.EVENT_OBJECT, []),
    ]
    c = Container()
    for i, (t, body) in enumerate(lumps):
        c.entries.append((t, i))
        c.regions.append(Region(t, body))
    return c


def make_zon(p, template):
    """ZON from parameters; textures, tile table and (by default) economy come
    from the template ZON, as the editor's File > New copies them."""
    sx, sy = p.start_chunk or (p.x0, p.y0)
    info = zon.ZoneInfo(p.zone_type, 64, 64, 4, 250.0, sx, sy,
                        np.zeros(64 * 64, dtype=zon.CELL_DTYPE))
    events = [zon.EventPos(e.world_x - ZONE_ORIGIN_CM, e.height, e.world_y - ZONE_ORIGIN_CM,
                           e.name.encode("ascii")) for e in p.events]
    textures = list(template.lump(zon.TEXTURES))
    tiles = np.array(template.lump(zon.TILES), copy=True)
    economy = p.economy or template.lump(zon.ECONOMY)
    c = Container()
    for i, (t, body) in enumerate([(zon.INFO, info), (zon.EVENTS, events),
                                   (zon.TEXTURES, textures), (zon.TILES, tiles),
                                   (zon.ECONOMY, economy)]):
        c.entries.append((t, i))
        c.regions.append(Region(t, body))
    return c


def build_zone(p, template_zon):
    """Return {relative path within the zone folder: bytes}."""
    tiles = template_zon.lump(zon.TILES)
    if p.field is not None and p.field.shape != terrain.field_shape(p.width, p.height):
        raise ValueError("field shape %s does not fit %dx%d chunks" % (p.field.shape, p.width, p.height))
    if not 0 <= p.tile.tile_id < len(tiles):
        raise ValueError("tile id %d is not in the template's %d-row tile table"
                         % (p.tile.tile_id, len(tiles)))
    if p.tile_grid is not None:
        if p.tile_grid.shape != (16 * p.height, 16 * p.width):
            raise ValueError("tile grid %s does not fit %dx%d chunks" % (p.tile_grid.shape, p.width, p.height))
        ids = p.tile_grid["tile_id"]
        if ids.min() < 0 or ids.max() >= len(tiles):
            raise ValueError("painted tile ids %d..%d outside the %d-row tile table"
                             % (ids.min(), ids.max(), len(tiles)))
    ext = (lambda e: e.upper()) if p.file_case_upper else (lambda e: e)
    files = {p.folder + ext(".zon"): zon.build(make_zon(p, template_zon))}
    flat_him = chunk.build_him(make_him(p.ground_cm)) if p.field is None else None
    til = chunk.build_til(make_til(p.tile))
    mov = chunk.build_mov(make_mov())
    empty_lit = lit.build_lit(lit.Lit(objects=[], dds_list=[]))
    for x, y in p.chunks():
        stem = chunk_stem(x, y)
        if p.field is None:
            files[stem + ext(".him")] = flat_him
        else:
            files[stem + ext(".him")] = chunk.build_him(make_him_from(
                terrain.chunk_heights(p.field, x - p.x0, y - p.y0)))
        if p.tile_grid is None:
            files[stem + ext(".til")] = til
        else:
            r0, c0 = (y - p.y0) * 16, (x - p.x0) * 16
            block = np.ascontiguousarray(p.tile_grid[r0:r0 + 16, c0:c0 + 16][::-1])   # TIL row 0 = north
            files[stem + ext(".til")] = chunk.build_til(chunk.Til(16, 16, block))
        files[stem + ext(".mov")] = mov
        files[stem + ext(".ifo")] = ifo.build(make_ifo(x, 64 - y, (p.water or {}).get((x, y), ()),
                                                       (p.objects or {}).get((x, y), ()),
                                                       (p.cnst or {}).get((x, y), ()),
                                                       (p.collision or {}).get((x, y), ())))
        if p.lightmaps and (x, y) in p.lightmaps:
            # the client's name: <chunk folder>\<x>_<64-y>_PlaneLightingMap.dds
            files["%s/%s_PLANELIGHTINGMAP.DDS" % (stem, stem)] = p.lightmaps[(x, y)]
        ol = (p.object_lights or {}).get((x, y))
        files["%s/LIGHTMAP/BUILDINGLIGHTMAPDATA.LIT" % stem] = ol["CNST"] if ol else empty_lit
        files["%s/LIGHTMAP/OBJECTLIGHTMAPDATA.LIT" % stem] = ol["OBJECT"] if ol else empty_lit
        for name, blob in (ol["dds"].items() if ol else ()):
            files["%s/LIGHTMAP/%s" % (stem, name.upper())] = blob
    return files
