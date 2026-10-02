# Zone file formats — what is verified

What a loadable zone is made of, how the files reference each other, and the
exact binary layouts, as read from the code that consumes them. Line
references are to the tree as of 2026-10-02. **The client and server loaders
are the authority**; the xadet editor (`xadet/rose-online-map-editor/Map
Editor/Engine/FileManager/Map/*.cs`) and Revise (`Revise/Revise.Files/`) are
second opinions and differ from the game in places, called out below.

Paths below are abbreviated:

- `io_terrain.cpp` = `src/client/io_terrain.cpp`
- `zonefile.cpp` = `src/sho_gameserver/src/zonefile.cpp`
- `editor/X.cs` = `xadet/rose-online-map-editor/Map Editor/Engine/FileManager/Map/X.cs`

All integers are little-endian. `i32`/`i16`/`u8`/`f32` as usual.

- `bstr`: `u8 len` + bytes, no terminator.
- `pstr`: the client's `ReadPascalString`, `src/client/util/cfilesystemtriggervfs.cpp:326-356`.
  The first byte is the length if bit 7 is clear. Otherwise the length is
  `(second << 7) | (first - 0x80)`, i.e. LEB128 for lengths < 16384.

The editor reads *every* string as .NET `BinaryReader.ReadString` (LEB128). That
is identical to `bstr` for lengths < 128.

## Units and coordinates

- World units are centimetres. Grid = 250 cm, 4 grids per patch, 16 patches
  per chunk, so a chunk ("map") is 16,000 cm = 160 m. These come from the ZON,
  not constants. Every zone in `data/` uses grid 4 / 250
  (`io_terrain.cpp:3008-3017`).
- A zone is a 64 x 64 grid of chunk slots (`src/client/terrain/terraindef.h:8`,
  `MAP_COUNT_PER_ZONE_AXIS`).
- Chunk files are named `<x>_<64-y>` where `(x, y)` is the chunk slot with y
  growing north (`io_terrain.cpp:2984-2995`, `zonefile.cpp:545-558`).
  Retail zones sit around slot (30-35, 30-35).
- **IFO and ZON positions are relative to the centre of chunk (32,32).** The
  loaders add `32 * 16000 + 8000 = 520,000` cm on x and y
  (`io_terrain.cpp:2187-2189`, `zonefile.cpp:168-173`). Height is absolute.
- ZON event positions and IFO water (lump 9) corners are stored **x, z(height), y**
  (`io_terrain.cpp:3043-3048`, `:2091-2092`). IFO object records store x, y, z.
- The world origin (0,0) is the south-west corner of chunk slot (0,0). Chunk
  slot `(x, y)` covers `[x*16000, (x+1)*16000)` on X
  (`io_terrain.cpp:1325-1329`).

## What a zone needs

| Piece | Read by | Required? | Notes |
|---|---|---|---|
| `LIST_ZONE.STB` row | client, server, editor | yes | Columns in `src/common/include/rose/io/stb.h:472-519`. |
| `LIST_ZONE_S.STL` key (col 26) | client | no | A missing key gives a blank name above the minimap. |
| `<zone>.ZON` | client, server | yes | Client shows an error box and fails if it is missing (`io_terrain.cpp:3193-3197`). |
| `x_y.HIM` | client | yes, per chunk | **Defines which chunks exist on the client:** `CMAP::Load` returns false if it is missing (`io_terrain.cpp:2493-2498`). |
| `x_y.TIL` | client | yes if the HIM exists | Missing → error box (`io_terrain.cpp:2605-2609`). |
| `x_y.IFO` | client, server | yes if the HIM exists | Client: error box (`:2660-2664`). **Defines which chunks exist on the server** (`zonefile.cpp:340-354`). |
| `x_y.MOV` | server only | no | No MOV anywhere → server opens every loaded chunk (`zonefile.cpp:671-695`). |
| `x_y/x_y_PlaneLightingMap.dds` (`.tga` accepted) | client | no | Falls back to `3DDATA\TERRAIN\default_light.dds` (`io_terrain.cpp:2500-2513`). |
| `x_y/LightMap/BuildingLightMapData.lit`, `ObjectLightMapData.lit` + atlases | client | no | Missing → objects load unlit, silently (`io_terrain.cpp:2403-2409`, `:2713-2718`). |
| DECO / CNST ZSC (`LIST_ZONE` cols 11 / 12) | client | yes | Loaded per zone by `CBasicDATA::LoadZoneData` (`src/client/io_basic.cpp:155-163`). |
| Minimap (col 8) | client | no | A failed load leaves an empty panel (`src/client/interface/dlgs/cminimapdlg.cpp:120-161`). |
| Sky (col 7, `LIST_SKY` row) | client, editor | yes for the editor | A blank cell is sky 0 for the client. |
| `ZONETYPEINFO.STB` + `ESTB/Table_Tileset_*.STB` | editor only | no | Gives the editor's brush palette. ZON lump-0 zone type indexes it (`editor/../../MapManager/MapManager.cs:547-564`). |

Server zone validity: a `LIST_ZONE` row is served if its ZON file exists
(`src/sho_gameserver/src/zonelist.cpp:82-89`) and the row number is < 250
(`src/sho_gameserver/src/lib_gsmain.cpp:38`, `:563-565`).

## ZON (one per zone)

Container: `i32 count`, then `count x (i32 type, i32 offset)`. Each lump
starts at its absolute offset. Lump types are `io_terrain.h:324-331`:
0 info, 1 event positions, 2 tile textures, 3 tile table (brushes), 4 economy.

- **Lump 0, info.** The client reads 28 bytes (`io_terrain.cpp:2999-3024`):
  - `i32 zone_type` — skipped by client and server; editor-only;
  - `i32 width`, `i32 height` — unused;
  - `i32 grid_per_patch`;
  - `f32 grid_size`;
  - `i32 start_x`, `i32 start_y` — the centre chunk.

  Classic files then carry a `width x height` table of `(u8 used, f32 x, f32 y)`
  cells (36,892 bytes in all; `editor/ZON.cs:522-534`) that the game never
  reads. Late Jrose ZONs stop after the 28 bytes.
- **Lump 1, event positions** (`io_terrain.cpp:3027-3071`,
  `zonefile.cpp:480-541`): `i32 count`, then `count x (f32 x, f32 z, f32 y, bstr name)`.
  - The server requires the names in `LIST_ZONE` col 2 (start) and col 3
    (revive). It takes the nearest revive point.
- **Lump 2, tile textures** (`io_terrain.cpp:3074-3102`): `i32 count`, then
  `count x bstr path`. These are terrain texture DDS files.
- **Lump 3, tile table** (`io_terrain.cpp:3105-3123`; struct `io_terrain.h:384-392`):
  `i32 count`, then `count x 7 i32`:
  1. `texture1_id`
  2. `texture2_id`
  3. `texture1_offset`
  4. `texture2_offset`
  5. `is_blending`
  6. `orientation` (1 normal, 2 LR, 3 TB, 4 LRTB, 5 rot+90, 6 rot-90)
  7. `type`

  A tile's bottom texture is `texture1_id + texture1_offset`, its top texture
  `texture2_id + texture2_offset` (`io_terrain.cpp:1339-1343`). `orientation`
  is passed to the terrain block (`:1364-1378`).
- **Lump 4, economy:**
  - Client (`io_terrain.cpp:3126-3157`): `bstr zone_name`, `i32 is_dungeon`,
    `bstr bgm`, `bstr sky`. The economy body that follows is read only under
    `__VIRTUAL_SERVER`.
  - Server (`zonefile.cpp:450-476` → `src/common/shared/ceconomy.cpp:70-98`):
    the same four fields, then `i32 town_counter`, `i32 pop_base`, `i32 dev_base`,
    and `10 x i32` consumption (`MIN_PRICE_TYPE 1 .. MAX_PRICE_TYPE 11`,
    `src/common/shared/datatype.h:218-219`).
  - A ZON without this lump leaves the server economy uninitialised. Jrose ZONs
    lack it; `scripts/import-karkia.py` splices one in.
  - The client stores zone name, BGM and sky (`io_terrain.cpp:3136-3150`) but
    nothing else in `src/client` references them. Music actually comes from
    `LIST_ZONE`.

Lump 3's `is_blending` and `type` columns are read (`io_terrain.cpp:3118-3120`)
but nothing in `src/client` uses them (grep, 2026-10-02).

Measured on our 65 ZONs (phase 0):

- all have lumps 0-4 and grid 4 / 250;
- 53 carry the lump-0 cell table, 12 do not.

## HIM (one per chunk) — heights

Read at `io_terrain.cpp:2489-2593`; editor `editor/HIM.cs:216-364`.

- `i32 width`, `i32 height` — must be 65 = `16 * grid_per_patch + 1`.
  Otherwise a message box appears and the values are clamped (`:2541-2553`).
- `i32 grid_per_patch`, `f32 patch_size` — the HIM's own copies; not used for
  layout.
- `width * height` `f32` heights in cm. **The first row in the file is the
  north edge**: the client fills `m_ppHeight[iH]` from `iH = height-1` down to
  0 (`:2555-2563`), and row index grows north.
- **Neighbouring chunks share their edge row/column.** The 65th sample equals
  the neighbour's first.
- Trailer (culling bounds):
  - `pstr name` (read into a 4-byte buffer, always `"quad"`);
  - `i32 patch_count` (256), then `16 x 16 x (f32 max, f32 min)`;
  - `i32 quad_count` (85), then `85 x (f32 max, f32 min)`.

  The client puts them into each patch's / quad's AABB z range
  (`io_terrain.cpp:2568-2591`). The editor writes `±FLT_MAX` placeholders
  instead of real values (`editor/HIM.cs:303-340`).

  Retail ships both kinds; measured by the phase 0 check:
  - bounds: 663 HIMs real, 875 placeholder;
  - counts: always 256 patches and 85 quads;
  - name: `"quad"` in 1,347 files, empty in 191 (the client discards it).
- **Which way the patch bounds are indexed** (patch row 0 = south or north) is
  not yet verified; see "Not verified".

## TIL (one per chunk) — tiles

Read at `io_terrain.cpp:2602-2648`; editor `editor/TIL.cs:81-138`.

- `i32 width`, `i32 height` — 16 x 16.
- `width * height` records of `(u8 brush, u8 tile_index, u8 tile_set, i32 tile_id)`.
  **The first row in the file is the north edge** (`iH` runs from 15 down to 0, `:2631-2640`).
- The client uses **only `tile_id`**, an index into the ZON lump-3 tile table
  (`:2638`, `SetMATERIAL`). The other three bytes are editor state for the
  autotile brush (`xadet/.../ToolManager/Tools/Terrain/Brush.cs:173-200`):
  - `tile_index` = 4-bit corner mask (15 = full);
  - `tile_set` = row of the zone type's `Table_Tileset_*.STB`;
  - `tile_id` is derived from them:
    - mask 15 → `TileNumberF`;
    - mask 0 → `TileNumber0`;
    - otherwise `TileNumber + (direction < 0 ? 15 - mask : mask) * TileCount`.

  Verified on the corpus with a probe (2026-10-02):
  - the rule holds for **100%** of tiles in the EJ, EZ, JD, JDT, JG, JG_mov,
    JZ, LP, LZ, ODG and JZP families;
  - 85-96% in port towns (JPT/JPTBG; hand-painted special tiles);
  - ~0% in Karkia, Skaaj and Shibuya, whose tilesets have no `Table_Tileset`.

## IFO (one per chunk) — placed things

Container: same shape as ZON (`i32 count`, `count x (i32 type, i32 offset)`).
Lump types are `io_terrain.h:223-238`; the server's enum is
`zonefile.cpp:10-24`.

**Generic record header**, shared by every object lump (`io_terrain.cpp:2157-2179`,
`zonefile.cpp:129-166`):

- `bstr name`;
- `i16 warp_id`, `i16 event_id`;
- `i32 obj_type`, `i32 obj_id`;
- `i32 map_x`, `i32 map_y`;
- `f32 qx, qy, qz, qw` — the rotation quaternion, read straight into a
  `D3DXQUATERNION` (x, y, z, w);
- `f32 px, py, pz` — position, zone-centre-relative x/y, absolute z;
- `f32 sx, sy, sz` — scale.

Each lump is `i32 count` + `count` records, plus per-type extras:

| # | Lump | Extras after the header | Client use |
|---|---|---|---|
| 0 | MAPINFO | not a record list: `i32 w, h, cell_x, cell_y; 16 x f32 world matrix; str name` (`editor/IFO.cs:707-740`) | ignored (`io_terrain.cpp:2052-2073`); Jrose IFOs have none |
| 1 | OBJECT (decoration) | — | `obj_id` → DECO ZSC (`:2247-2256`) |
| 2 | MOB (NPC) | `i32 ai`, `bstr con_file` | NPC only if `NPC_TYPE == 999` |
| 3 | CNST (building) | — | `obj_id` → CNST ZSC (`:2258-2268`) |
| 4 | SOUND | `bstr wav`, `i32 range_cm`, `i32 interval_s` | |
| 5 | EFFECT | `bstr eft` | |
| 6 | MORPH (animated) | — | `obj_id` → `LIST_MORPH_OBJECT` |
| 7 | WATER ("wide water", editor `WideWater`) | not a record list: `i32 x, i32 y, x*y x (u8 use, f32 height, i32 type, i32 index, i32 reserved)` (`editor/IFO.cs:953-979`) | **the client still runs `ReadObjINFO` over it and discards the result** (`io_terrain.cpp:2688-2699`, `:2209-2225`) |
| 8 | REGEN | `bstr name`, `i32 nb` + `nb x (bstr, i32 mob, i32 count)`, `i32 nt` + same, `i32 interval_s, limit, range_m, tactic_point` (`src/sho_gameserver/src/common/cregenarea.cpp:52-135`) | server |
| 9 | OCEAN (editor `Water`) | not a record list: `f32 size`, `i32 count`, `count x (f32 x, z, y start; f32 x, z, y end)` (`io_terrain.cpp:2077-2129`) | flat water rectangles |
| 10 | WARP | — | `warp_id` → `WARP.STB` |
| 11 | COLLISION | — | `event_id` used (`:2345-2352`) |
| 12 | EVENT_OBJECT | `pstr trigger`, `pstr con_file` (`:2355-2381`) | |

Lightmap linkage:

- `.lit` entries refer to objects by **1-based ordinal within lumps 1 / 3**
  (`io_terrain.cpp:2414-2457`).
- Deleting a record shifts every later object's lightmap; move or sink a
  record instead.

What retail files contain (phase 0 corpus scan, 1,538 IFOs):

- **Lumps are all optional.** 1,316 IFOs carry all 13 lumps (editor-saved).
  Jrose-origin IFOs carry only what they use. 30 have zero lumps (a 4-byte
  file: `JUNON/SW_SBY` x24, `KARKIA/KSPIREVIL` x6) and load fine.
- **`name` is empty in every record of every lump.**
- **`obj_type`** follows the lump, with 0 also common:

  | Lump | `obj_type` |
  |---|---|
  | OBJECT | 1 |
  | MOB | 3 |
  | CNST | 4 |
  | SOUND | 6 |
  | EFFECT | 7 |
  | MORPH | 8 |
  | REGEN | 9 |
  | WARP | 10 |
  | COLLISION | 11 |
  | EVENT_OBJECT | 12 |

  The game only logs it (`io_terrain.cpp:2390-2397`).
- **`warp_id`** is non-zero only on WARP records (and 5 stray OBJECT records).
  **`event_id`** is non-zero only on 20 EVENT_OBJECT records.
- **`map_x` / `map_y`:** about 72% equal the record's 2.5 m grid cell inside
  the chunk in one of four orientations; about 11% are 0; the rest are
  something else. The game only logs them.
- **Lump 7 (WATER):** always 16x16. `use = 0`, `height = 0`, `type = 1`,
  `index = 0`, `reserved = 0`, apart from 44 files with some `type = 0`.
  This is exactly what the editor's File > New writes.
- **Lump 0 (MAPINFO):** always `16, 16, cell_x = x, cell_y = y`, with name
  `"x_y"` of the file (two files carry a neighbour's name).
- **Stale data:** some IFOs carry dead bytes past the end of a lump (see
  "Codec status").

## Water

- Lump-9 rectangles are drawn with one global material:
  `3DDATA\JUNON\Water\ocean01_01..25.dds`, loaded once in
  `src/client/io_basic.cpp:113-121` and shared by every zone.
- A lake is therefore always animated ocean.
- `size` is the tessellation cell size: width / size quads
  (`io_terrain.cpp:2109-2119`).

## MOV (one per chunk, server only)

- `i32 width`, `i32 height` — 32 x 32, 5 m cells — then `width * height` bytes
  (`zonefile.cpp:289-326`).
- Zero = AI may move there; any other value blocks.
- **Rows run south → north** here, the opposite of HIM/TIL
  (`editor/MOV.cs:5-6`, `zonefile.cpp:311-321`).
- Only `SetCMD_MOVE2D` consults it (monster leash/wander/flee); players and
  chasing do not (`src/sho_gameserver/src/zonefile.h:130-136`).

## LIT (two per chunk) — object lightmap index

Read at `io_terrain.cpp:2403-2465`; editor `editor/LIT.cs:173-250`.

`i32 obj_count`, then per object:

- `i32 part_count`, `i32 obj_index` (1-based ordinal in the lump);
- per part:
  - `bstr tga_name` — skipped;
  - `i32 part_index`;
  - `bstr dds_name`;
  - `i32 lightmap_index` — skipped;
  - `i32 pixels_per_part`, `i32 parts_per_width`, `i32 position_in_map`.

Then optionally `i32 count` + `count x bstr` DDS names, which the game never
reads. Retail files may end without it.

## Codec status (phase 0)

Codecs live in `scripts/mapgen/`. The modules are `zon.py`, `ifo.py`,
`chunk.py` (HIM/TIL/MOV) and `lit.py`, on top of `binio.py` and `container.py`.

Each is `parse(bytes) -> model` / `build(model) -> bytes`, and strings stay
`bytes`. Container offsets are recomputed on build, never copied.

`python scripts/mapgen-roundtrip.py --selftest` proves byte-exact round-trip
on all 9,035 map files: 65 ZON, 1,538 HIM, 1,538 IFO, 1,547 TIL, 1,369 MOV and
2,978 LIT. It also checks that an edited field survives build + re-parse.

The 9 extra TILs are a stray `JUNON/AGIT01/TEMP/` folder. The client never
probes it, since chunk files are looked up beside the ZON.

### Bytes carried without interpretation

These round-trip, but the generator cannot produce them from fields:

| Where | Files | Bytes | What it is | Generator |
|---|---|---|---|---|
| IFO, after the physically last lump (COLLISION, EVENT_OBJECT) | 74 | 65 KB | Fragments of older records left when a tool rewrote a shorter lump in place (one shows a scale of 1.6 and the name `Untitled`) | never written: no loader reads past a lump's count |
| IFO, after lump 6 MORPH, before lump 7 (`JUNON/JD01`, `JUNON/JG02`) | 21 | 47 KB | Whole MORPH records (obj_type 8, valid quaternions and positions) behind a MORPH count of **0**. The game never loads them, so those zones show no morph objects where they were once placed | never written |
| ZON, after the economy lump (`JUNON/AGIT01` zeros, `LUNAR/LZ02` an older economy block) | 2 | 284 B | Stale bytes | never written |

There are no raw lumps (every lump parsed), no gaps after a lump table, no
lump-table entries pointing outside the file, and no trailing bytes after any
HIM, TIL, MOV or LIT.

### Fields parsed but not used by the game

What the generator should write in each:

| Field | Used by | Generator writes |
|---|---|---|
| ZON lump 0 `zone_type` | editor (brush palette) | 0 (JG) |
| ZON lump 0 `width` / `height` | editor (cell-table size) | 64, 64 |
| ZON lump 0 cell table | nothing | 64x64 zeros, as the editor's File > New |
| ZON lump 3 `is_blending`, `type` | nothing in client | copied with the tile table from JG01 |
| ZON lump 4 name / BGM / sky strings | client stores, never uses | copied from JG01 |
| HIM `grid_per_patch`, `patch_size` | nothing | 4, 250 |
| HIM trailer name | nothing | `"quad"` |
| HIM bounds | client culling | placeholders until the indexing is verified; real values in phase 2 |
| TIL `brush`, `tile_index`, `tile_set` | editor autotile brush | phase 3, from the JG tileset, so the editor can keep painting |
| IFO record `name` | nothing | empty |
| IFO record `obj_type` | log only | the per-lump value in the IFO section |
| IFO record `map_x` / `map_y` | log only | the 2.5 m grid cell (exact orientation not settled; 0 is also retail-valid) |
| IFO lump 0 MAPINFO | nothing | `16, 16, x, y`, name `"x_y"`, matrix not yet checked |
| IFO lump 7 WATER | nothing; the client misparses and discards it | 16x16, `type = 1`, rest 0 — or omit the lump |
| LIT `tga_name`, `lightmap_index`, DDS catalogue | nothing | not written in v1 (no object lightmaps): empty LIT files as File > New writes, or none |

## How the files reference each other

```text
LIST_ZONE row ──col 1──> <zone>.ZON ──folder──> x_y.HIM / .TIL / .IFO / .MOV
   │ col 11/12 ──> DECO / CNST .ZSC <── IFO lump 1 / 3 obj_id
   │ col 2/3 ──> ZON lump 1 event names
   │ col 7 ──> LIST_SKY row      col 8 ──> minimap DDS      col 26 ──> LIST_ZONE_S key
ZON lump 2 textures <── lump 3 tile table <── TIL tile_id
ZON lump 0 zone_type ──(editor only)──> ZONETYPEINFO.STB col 6 ──> ESTB/Table_Tileset_*.STB
IFO lump 1/3 ordinal <── LightMap/*.lit ──> LightMap/*.dds atlases
```

Collision has no file of its own:

- Objects collide by a per-part ZSC property (`src/client/io_model.cpp:326-333`,
  `:417`).
- Terrain collides through the heightfield (`src/client/cobjchar_collision.cpp`).

## Not verified

- **HIM trailer indexing:** whether patch bound `(i, j)` in the trailer is
  counted from the south or the north row, and the order of the 85 quadtree
  nodes. It matters once we write real bounds; placeholders are safe meanwhile.
- **TIL autotile neighbour rule:** the tile-id formula is verified on the
  corpus, but not yet that neighbouring tiles agree on shared corners
  (phase 3's first check).
- **Slope limit:** `ApplySliding` blocks an uphill step at >= 0.3π (54°)
  (`cobjchar_collision.cpp:1104-1180`). Not yet traced through every caller.
- **Map edge:** what happens when a player walks off the last chunk.
- **Water:** whether anything stops a player walking into water.
- **Editor output in our client:** whether the editor's File > New output loads
  in our client unchanged.
- **IFO lump 7:** whether the client's misparse of it is always harmless, or
  just harmless on the data we have. Retail only ever holds the 16x16
  default, so copying that is safe.
- **IFO `map_x` / `map_y`:** which of the four grid-cell orientations is the
  convention; about 17% of records match none.
- **IFO lump 0 world matrix:** what retail writes in it (not inspected; the
  game ignores the lump).
