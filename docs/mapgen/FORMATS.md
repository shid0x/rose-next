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

The GM command `/mm <zone> <x> <y>` warps to absolute world coordinates in
10 m units (`src/sho_gameserver/src/cheatcmd.cpp:327-342`, x/y × 1000 cm).

`LIST_ZONE` row rules learned in phase 1:

- **Editor visibility.** The editor's Open dialog lists a row only if game
  columns 1 (ZON), 2 (start), 3 (revive), 9 and 10 (minimap origin), 11 and
  12 (DECO/CNST ZSC) are non-blank (`xadet/.../MapManager.cs:647-682`; it
  labels 9/10 "IFO", they are the minimap origin).
- **No minimap.** `NOMAP` in col 8 gives an empty panel, as row 134 already
  does. A blank cell makes `SetMinimap` return before freeing, so the previous
  zone's minimap would stay up (`cminimapdlg.cpp:124-127`).
  - The client logs `interface: loadTexture() failed. file [NOMAP] not found`
    on entering the zone; it is harmless (user, 2026-10-02).
  - A generated minimap (phase 8) removes it.
- **Triggers.** Cols 22-24 are QSD trigger names; JG01's col 22 is
  `PvP1301-340`. Never inherit them from a template row.
- **Revive.** Cols 31-33 (revive zone/x/y) are unused; revive goes through the
  ZON `restore` event.

**Proven end to end (phase 1, 2026-10-02).** A zone built entirely by mapgen
works everywhere:

- it loads in the editor GUI, the gameserver and the client (`/mm` teleport,
  clean logs);
- the row it needs is listed above;
- the files are exactly what the editor's File > New writes, with no
  per-chunk plane lightmap: the client's `default_light.dds` fallback is
  confirmed working.
- **Map edge:** the player simply cannot walk past the last chunk; it behaves
  like a retail map edge (user, 2026-10-02). A generated map needs no barrier
  at its border for correctness. A ridge there is only for looks.

## ZON (one per zone)

Container: `i32 count`, then `count x (i32 type, i32 offset)`. Each lump
starts at its absolute offset. Lump types are `io_terrain.h:324-331`:
0 info, 1 event positions, 2 tile textures, 3 tile table (brushes), 4 economy.

- **Lump 0, info.** The client reads 28 bytes (`io_terrain.cpp:2999-3024`):
  - `i32 zone_type` — skipped by client and server; editor-only;
  - `i32 width`, `i32 height` — unused;
  - `i32 grid_per_patch`;
  - `f32 grid_size`;
  - `i32 start_x`, `i32 start_y` — the centre chunk. Effectively unused:
    `CTERRAIN::InitZONE` overwrites both from the player's position before any
    use (`io_terrain.cpp:3637-3660`).

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
- **Bounds layout and formula (settled 2026-10-02, phase 2).** Implemented
  in `scripts/mapgen/chunk.py` `compute_him_bounds`; reproduces all 661
  consistent retail files.
  - **Patches are stored south row first**, the opposite of the heights:
    patch k covers patch row k / 16 from the south and column k % 16 from the
    west. The client reads them into `m_PATCH[k/16][k%16]`
    (`io_terrain.cpp:2577-2583`), whose row index grows north (`:1320-1329`).
    Each value is (max, min) over the patch's 5x5 vertices.
  - **Quads** are the client's tree (`io_terrain.cpp:780-827`): node i has
    children 4i+1..4i+4 at (x,y), (x+h,y), (x+h,y+h), (x,y+h) in patch units
    from the south-west, on levels of 16/8/4/2 patches.
  - **Quad values** are the max/min of the node's patches, but seeded with
    max = -10 and min = 10000. That is the original tool's quirk, also in
    `editor/HIM.cs:317-340`, and it decides 255 retail files.
  - **Measured:**
    - patch bounds match south-first in 661 of 663 retail files with real
      bounds;
    - the two others (`JUNON/JD03/31_32`, `JUNON/JZ01_1/33_30`) are stale
      after a hand edit;
    - quads match in 663 / 663.
  - `mapgen-roundtrip.py --selftest` re-proves this on every run.
  - **Confirmed in game (user, 2026-10-02):** generated terrain carrying these
    real bounds never vanishes while the camera turns.

## TIL (one per chunk) — tiles

Read at `io_terrain.cpp:2602-2648`; editor `editor/TIL.cs:81-138`.

- `i32 width`, `i32 height` — 16 x 16.
- `width * height` records of `(u8 brush, u8 tile_index, u8 tile_set, i32 tile_id)`.
  **The first row in the file is the north edge** (`iH` runs from 15 down to 0, `:2631-2640`).
- The client uses **only `tile_id`**, an index into the ZON lump-3 tile table
  (`:2638`, `SetMATERIAL`). The other three bytes are editor state for the
  autotile brush (`xadet/.../ToolManager/Tools/Terrain/Brush.cs:173-200`):
  - `tile_index` = 4-bit corner mask (15 = full);
  - `tile_set` = k selects **game row k+1** of the zone type's
    `Table_Tileset_*.STB`. Row 0 is the `MAX_TILE_SET` header
    (`xadet/.../Data/TileSet.cs:48-65`; editor col = game col + 1);
  - `brush` is editor bookkeeping (the brush last painted). In retail JG:
    - full tiles always carry the set's `MinimumBrush`;
    - edge tiles carry it 90% of the time (17,710 vs 2,048 carrying
      `MaximumBrush`);
    - the client ignores it, and mapgen writes `MinimumBrush`.

    A full tile (mask 15) shows `MinimumBrush` and uses `TileNumberF`; mask 0
    shows `MaximumBrush` and uses `TileNumber0`;
  - `tile_id` is derived from them:
    - mask 15 → `TileNumberF`;
    - mask 0 → `TileNumber0`;
    - otherwise `TileNumber + (direction < 0 ? 15 - mask : mask) * TileCount`.

  Verified on the corpus with a probe (2026-10-02):
  - the rule holds for **100%** of tiles in the EJ, EZ, JD, JDT, JG, JG_mov,
    JZ, LP, LZ, ODG and JZP families;
  - 85-96% in port towns (JPT/JPTBG; hand-painted special tiles);
  - ~0% in Karkia, Skaaj and Shibuya, whose tilesets have no `Table_Tileset`.

**Junon grassland (JG) brushes**, from `Table_Tileset_JG.STB` game row 17,
decoded as CP949:

| # | Brush |
|---|---|
| 0 | dark soil |
| 1 | bright grass |
| 2 | dark grass |
| 3 | sparse grass |
| 4 | meadow cliff |
| 5 | bright soil |
| 6 | beach sand |
| 7 | **seabed** |
| 8 | stream gravel |

- **Plain grass** is brush 1, `tile_set` 1, `tile_index` 15, ids 5-9
  (`T002_01..05.dds`).
- JG01's most common full tile is set 13 / brush 7 (ids 35-39, `T008`), which
  is the *seabed* under its sea — **not** a ground tile.
- The editor's File > New writes brush 1 / set 1 / index 15 but `tile_id` 1,
  which draws *dark soil* (`T001_02.dds`). Its four bytes disagree; ours do not.

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
| 11 | COLLISION | — | `event_id` used (`:2345-2352`); invisible walls, see below |
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
- **`map_x` / `map_y`:** the object's 2.5 m cell in its chunk, as
  (column from the west, 63 - row counted from the south). This holds for
  98% of the 10,806 JG decorations (phase 5 survey); other lumps vary more,
  with some zeros. The game only logs them.
- **Every object is stored in the chunk that contains it** (all 10,806 JG
  decorations).
- **Decoration habits** (JG01-08, lump 1):
  - median 30 per chunk (p90 111, max 190; corpus max 278);
  - yaw-only rotation for 87% of trees, 57% of rocks, 68% of grass;
  - sunk into the ground (grass ~0.5 m, trees and rocks 0.3-1.8 m);
  - uniform scale, typically 1.2-3.8.
  - The big "waterfall rock" (`stone0211`) sits on ~62° slopes over the rock
    brush: retail covers its cliffs with rock objects.
- **Construction (lump 3) in JG** (phase 6 survey): `LIST_CNST_JG.ZSC`
  holds only the beach houses (`shouse`, `mhouse`, `lhouse`). Every other JG
  village building (farm houses, windmill, fences, crates, wells) is a
  **decoration** in `LIST_DECO_JG`, editor category VILLAGE or ETC. CNST
  records use the same header and `map_x` / `map_y` convention as lump 1,
  with `obj_type = 4`; the editor lists them from
  `3DDATA/STB/LIST_CNST_JG.STB`. Generated CNST records load in the editor's
  readers (phase 6).
- **Lump 7 (WATER):** always 16x16. `use = 0`, `height = 0`, `type = 1`,
  `index = 0`, `reserved = 0`, apart from 44 files with some `type = 0`.
  This is exactly what the editor's File > New writes.
- **Lump 0 (MAPINFO):** always `16, 16, cell_x = x, cell_y = y`, with name
  `"x_y"` of the file (two files carry a neighbour's name).
  - The world matrix is a Y/Z axis swap plus a translation of ±16,000 cm per
    chunk away from the zone centre: an exporter artefact.
  - The editor's File > New writes zeros, and the game ignores the lump.
- **Stale data:** some IFOs carry dead bytes past the end of a lump (see
  "Codec status").
- **Lump 11 (COLLISION) is retail's invisible wall** (phase 7c survey).
  - The client draws every record as `LIST_DECO_SPECIAL.ZSC` object 2
    whatever its `obj_id` (`Add_CollisionBox`, `object.cpp:635`):
    `Collision01`, a panel 1.2 m long (local X) x 0.06 m x 2.52 m tall,
    collision flag 4, texture `chake01.dds`. It is not drawn.
  - Retail has 2,889 of them. In JG they are uniformly scaled 14-31
    (17-37 m long, 35-77 m tall), yawed freely, standing on gentle ground:
    they close what slopes do not.
  - Records use the generic header with `obj_type = 11`, `obj_id = 11`.
    mapgen writes them along ledge edges (`levels.wall_line`: scale 8,
    9.6 m panels overlapping 1 m, sunk 1.5 m so no gap opens on a slope).
  - **Not yet confirmed in game:** that a generated panel blocks the
    player exactly like a retail one (phase 7c, handed to the user).
- **Bridges are ordinary decorations** (lump 1). JG has two kinds:
  - `field-bridge02` (id 175): deck along local X, ±8.84 m, arched from
    2.5 m at the ends to 4.1 m mid-span; retail scales 1.5-2.2, over
    streams 3-9 m below.
  - `guroomdari` (id 193): a suspension bridge, deck ±23.1 m, ends at
    0.1 m, sagging to -2.2 m; retail scale 0.7-1.1, over deep gaps (Anima
    Lake 40 m, Kenji Beach 29 m).
  - The slope rule does not apply on an object ("if not on the terrain, do
    nothing", `cobjchar_collision.cpp:1110`), so a deck is a link between
    its banks; `walk.analyse(links=...)` models it as a two-way connection.

## Water

- Lump-9 rectangles are drawn with one global material:
  `3DDATA\JUNON\Water\ocean01_01..25.dds`, loaded once in
  `src/client/io_basic.cpp:113-121` and shared by every zone.
- A lake is therefore always animated ocean.
- `size` sets how often the texture repeats: the client passes width / size
  and height / size as the repeat counts of one quad (`io_terrain.cpp:2109-2119`,
  `src/engine/src/zz_interface.cpp:6446-6486`).
- **The water plane is drawn from the start corner**, by (end - start). The
  editor draws the quad between the two stored corners
  (`xadet/.../MapManager/Terrain/Water.cs:150-183`), so both conventions show
  the same footprint.
- **Retail conventions** (phase 4 scan of 850 rectangles in 62 zones):
  - `size` is always 2000 and start z == end z;
  - start is the NW corner (min x, **max** y) in 777, the SW corner in 73;
  - 94% lie inside their own chunk;
  - 95% are whole 20 m widths;
  - 1-13 rectangles per chunk IFO;
  - the surface is typically 1-19 m above the deepest ground under it.
- **Nothing collides with water.** Character collision skips ocean nodes as
  it skips terrain (`cobjchar_collision.cpp:674`), so a player walks into a
  lake along its bed. Water is therefore not a barrier, and lake shores fall
  under the slope rule like any other ground. Confirmed in game: a player
  walks into a generated lake and back out (user, 2026-10-02).
- **A rectangle is loaded and unloaded with the chunk whose IFO holds it**
  (`m_OceanLIST`, `io_terrain.cpp:2124`, freed at `:2765-2773`). mapgen keeps
  each rectangle inside its own chunk.
- **Generated water confirmed in game and editor** (user, 2026-10-02): same
  waterline in both, no plane showing through the ground.

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

## Lightmaps — how the client uses them (phase 8 survey, 2026-10-03)

Settled from code; numbers from `scripts/mapgen-lightmap-survey.py`.

**Terrain (`x_y_PlaneLightingMap.dds`).**

- **Formula:** terrain = tiles x lightmap x 2, then x the zone light's
  diffuse colour. The game draws terrain fixed-function: `INIT.LUA:5` sets
  `usePixelShader(0)`. The texture stages are
  `zz_material_terrain.cpp:260-290`: stage 0 = tile (`SELECTARG1`, **no
  vertex lighting at all**), 1 = second tile, 2 = lightmap with the
  lightmap blend style, 4 = `TFACTOR` = light diffuse.
- **Blend style:** set per zone from `LIST_SKY` col 5 of the zone's sky
  (`io_terrain.cpp:3189`). It is 5 (`MODULATE2X`) in every row, as is
  `INIT.LUA:40`'s default. The unused pixel shader `TERRAIN.PSO` does the
  same `mul_x2` (decoded from the shipped bytecode).
- **Neutral value:** 128 leaves a tile as drawn. The lightmap is the only
  shading terrain ever gets.
- **Fallback:** `3DDATA\TERRAIN\default_light.dds`, 32 px, ~(132, 125, 132).
  That is ~1.0x, which is why generated maps look even and flat.
- **Mapping** (`zz_mesh_tool.cpp:1131-1150`): one image per chunk, covering
  exactly that chunk (`mapsize` 16,000 cm). u runs west to east; v = 1 - y,
  so **row 0 is the north edge**. Sampled with clamp addressing
  (`zz_material_terrain.cpp:604`), so edge texels hold to the border.
- **Retail files:** always DXT5 with a full mip chain, 512 px (10 mips;
  JG01, 02, 07, 08) or 256 px (9 mips; JG03-06). The chunk-folder name is
  the HIM's (`x_(64-y)`).

**What mapgen writes (phase 8 step 2):** one
`x_y/x_y_PLANELIGHTINGMAP.DDS` per chunk (the HIM's stem), 512 px, DXT5,
full mip chain, legacy DX9 header (texconv `-dx9 -m 0 -nowic -if BOX`),
349,680 bytes like retail's. Baked over the whole map, so edges agree; the
verify round-trip skips `.dds` (no codec) and `lightmap_checks` reads them
back instead.

**Objects (`LightMap/*.lit` + atlases).** Same `MODULATE2X` blend, on the
colormap material's stage 1 (`zz_material_colormap.cpp:248-313`). A
part's cell is placed through a per-part UV transform
(`ZZ_VSC_LIGHTMAP_TRANSFORM`) applied to the mesh's **second UV set**
(ZMS format bit 256). In retail JG every decoration has an entry (JG01-03
and JG07: 100%), and every lit part's mesh has UV1. Cells are 32 or 64 px
(a few 128); atlases are 256 or 512 px.

Settled from the client source and the shipped shaders (phase 8 step 3):

- **A listed part loses all vertex lighting.** `CMAP::LoadLightMapINFO`
  (`io_terrain.cpp:2403-2465`) reads the building `.lit` (lump 3) and then
  the object `.lit` (lump 1) and calls `CObjFIXED::SetLightMap`, which
  moves the part to `shader_lightmap_nolit`: the vertex shaders write the
  light diffuse as a constant (no n.l, no ambient), so
  **final = texture x light_01 diffuse x lightmap x 2**, the terrain's
  convention. The lightmap carries all the shading; 128 is neutral.
  Unlisted parts keep `simple_lit`: texture x saturate(ambient (0.86,
  0.83, 0.80) + max(n.l, 0) x diffuse), L toward the north-east at 35°.
- **The diffuse is the day/night colour** (`CDayNNightProc`, LIST_SKY
  cols 6-9; sky 0: day white, night (125, 135, 163)), so lit objects dim
  and tint at night with the terrain; unlit ones keep the 0.86 ambient.
- **Distance fade:** the lightmap shaders take their vertex alpha from the
  alpha-fog range, `simple_lit` from the fog range. Lit objects fade at
  110-220 m on the Normal view distance (LIST_CAMERA), unlit ones stay
  opaque to ~240 m.
- **Display presets:** 3/4 load the file's own mip count and halve the
  atlas; 5 turns lightmaps off and lit parts then draw flat (oD0 =
  diffuse), not vertex-lit.
- **Cell:** `col = pos % ppw`, `row = pos // ppw`; the shader samples
  `(uv1 + (col, row)) / ppw`, uv1 raw (v = 0 is the top row), with clamp
  addressing and trilinear filtering. The atlas must be square with side
  `ppw x cell`; `pixels_per_part`, `tga_name` and `lightmap_index` are
  never used, nor the trailing DDS list. Retail atlas alpha is a coverage
  mask the client ignores.
- **Indices:** `obj_index` = 1-based ordinal in the lump; `part_index`
  goes through `GetPartIndex` (identity when the root is part 0, true of
  every JG object). **Nothing is bounds-checked**: an ordinal past the
  lump, a lump that is absent, a part past the object's count or
  `ppw = 0` crashes or corrupts the client.
- **No shared cells:** the material cache key is `@P{x}x{y}@{path}`
  without the diffuse texture, so two parts on one cell draw with the
  first one's texture.
- **Meshes:** only ZMS formats 390 (position | normal | uv0 | uv1) and 386
  (no normal, forced) read uv1 correctly; every retail lit mesh is one of
  them. Three JG meshes (whealbone01, shellfish01, SumDoor03) have uv1
  outside their cell or overlapping.
- **Mips:** the client loads 3 levels (`setMipmapLevel(3)`); retail ships
  full chains, which bleed between cells at presets 3/4. House rule: 3.
- **Names:** `Object_<cell px>_<k>.dds` / `Building_<cell px>_<k>.dds`,
  `k` from 0 per size. Lookup is case-insensitive (retail LITs are mixed
  case, files upper case). `LightMap/LISTDATA.DAT` is an editor artefact
  nothing reads.
- **The editor draws object lightmaps** (tex x 2 x lightmap, same cell
  layout, by ZSC part index), so `shots` shows the bake. It keys entries
  by ordinal and never renumbers them on delete: saving a lit zone after
  deleting an object misassigns every later entry.
- **A record the client drops keeps its ordinal.** `CMAP::AddObject`
  (`io_terrain.cpp:1867-1877`) rejects a record whose f32 world position
  minus the chunk origin, in 10 m patches truncated toward zero, is
  outside 0..15 (only the east/north edge can trip it), and the next
  record reuses its object slot. A `.lit` entry for the dropped record
  lights that next object instead.

**What mapgen writes (phase 8 step 3):** per chunk, both `.lit` files and
`x_y/LIGHTMAP/OBJECT_<px>_<k>.DDS` / `BUILDING_<px>_<k>.DDS` atlases
(DXT5, legacy header, exactly 3 mips), every part of every record of lumps
1 and 3 listed, entries and catalogue as retail writes them. A layout with
`"lighting": {"objects": false}` writes the empty 8-byte LITs instead
(File > New's).

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
| HIM bounds | client culling | real values (`compute_him_bounds`); placeholders only for an editor-identical flat chunk |
| TIL `brush`, `tile_index`, `tile_set` | editor autotile brush | written from the corner lattice (`tiles.tiles_from_lattice`), so the editor can keep painting |
| IFO record `name` | nothing | empty |
| IFO record `obj_type` | log only | the per-lump value in the IFO section |
| IFO record `map_x` / `map_y` | log only | the 2.5 m grid cell (exact orientation not settled; 0 is also retail-valid) |
| IFO lump 0 MAPINFO | nothing | `16, 16, x, y`, name `"x_y"`, zero matrix (as the editor) |
| IFO lump 7 WATER | nothing; the client misparses and discards it | 16x16, `type = 1`, rest 0 — or omit the lump |
| LIT `tga_name`, `lightmap_index`, DDS catalogue | nothing | written as retail does (phase 8 step 3): tga `<mesh>_<Object\|Building>_<obj>_<part>_<x_y>_LightingMap.tga`, index into the catalogue (Building atlases first, then Object, by cell size then number) |

## Tile painting: corner brushes (settled 2026-10-02, phase 3)

Implemented in `scripts/mapgen/tiles.py`.

**A zone's painting is one lattice of corner brushes.** It has 16 × chunks
+ 1 corners per axis, 10 m apart, and every tile's (`tile_set`,
`tile_index`) encodes the brushes at its 4 corners.

- **Mask bits → corners:** 1 = SE, 2 = SW, 4 = NE, 8 = NW.
  - A set bit means that corner is the set's `MinimumBrush`, a clear bit its
    `MaximumBrush`.
  - Found by testing all 24 bit→corner assignments: this one makes
    neighbouring tiles agree on 99.82% of shared corners in JG01; the next
    best manages 70%.
  - It matches the editor's brush code (`Brush.cs:76-84` paints the 8
    neighbours with masks 1/3/2/5/10/4/12/8).
- **Sets come in mirror pairs**, (a,b) and (b,a), sharing tile ids. A tile
  with corners {a,b} uses either; mapgen takes the set whose min is the
  smaller brush id.
- **Chain table** (rows after `MAX_BRUSH_COUNT`): `chain[a][b] == 99` means
  a and b may share a tile; otherwise it names the brush to put between them.
  - In JG every brush pair is either direct or one step apart through bright
    grass (1) or dark soil (0).
  - No tile ever holds three brushes.
- **Re-derived on every retail zone** (`mapgen-zone.py tiles-selftest`):
  - neighbours agree on 98.5-100% of shared corners;
  - regenerating each tile from the lattice reproduces 99.22-99.96% of JG tiles
    (99%+ for EJ, LP, LZ and ODG too). Full tiles count as matching when
    they land in the same brush's variant range.
  - Karkia, Skaaj and Shibuya score ~0%: their tiles were not authored with
    the tileset their zone type names.
- **Confirmed in game (user, 2026-10-02):** generated tiles built this way
  render with clean transitions in the client, and the editor shows and keeps
  painting them.
- **Saddles are avoided.** A tile with SW == NE != SE == NW (masks 6 / 9)
  shows two opposite blobs, using the `*_04` textures with a two-blob alpha.
  Retail JG uses them on ~0.04% of tiles. A diagonal line of single corners
  produces a dotted row of them, which shows as a "ladder" or dotted line in
  the editor (user screenshot, 2026-10-02).
- **Brush mix on retail JG ground** (`scripts/mapgen/stats/jg_brush_by_slope.json`,
  regenerate with `mapgen-zone.py stats`):
  - rock is not a steep-slope texture: above 60° it is 9% of corners, against
    53% bright grass;
  - rock peaks at 30-50° (about 26%);
  - dark and sparse grass grow with slope;
  - sand and seabed sit at or below 0 m (shores, phase 4).

## Slope rule (who can walk where)

Settled 2026-10-02 in phase 2; implemented in `scripts/mapgen/walk.py`.

**Only the local player is slope-checked.** For `OBJ_USER`,
`AdjustHeight_Avatar` runs (`cobjchar_collision.cpp:88-92`, `:269-293`).
On bare terrain it goes through `CollisionResponseNone_Avatar` → `ApplySliding`
→ `StopMovingForCollision` if refused (`:1290-1301`); carts and castle gear
take the same path. Monsters (`AdjustHeight_Monster`) and other players
(`AdjustHeight_Other`) only follow terrain height.

**The gradient comes from three of the cell's four corners.** For the 2.5 m
cell under the character it is a forward difference from the cell's SW
corner (`CMAP::GetNormal`, `io_terrain.cpp:2038-2048`; corner order
`:2846-2849`):

    gx = (h(SE) - h(SW)) / 250
    gy = (h(NW) - h(SW)) / 250

The NE corner is ignored.

**A step is refused** (`ApplySliding`, `cobjchar_collision.cpp:1104-1180`)
when both hold:

- |g| >= tan(0.3π) = 1.376 (54°);
- the move has a non-negative component along g, the uphill direction.

Downhill moves are always allowed. On a steep cell the walkable directions are
the open half-plane g·d < 0, so a steep slope can be descended but never
climbed or traversed level.

**The NE-corner blind spot.** Because the NE corner is ignored, the cell just
outside a pit's south-west corner reads as flat even though its NE corner is
at the pit floor. That only matters for a path exactly through the corner
point; `walk.py` excludes it.

**No barrier is needed at the map border:** the player cannot walk past the
last chunk (user-verified, phase 1).

**Both halves confirmed in game (user, 2026-10-02):** the phase 2 ridge (up
to 69°) stops the player going up, and the player can walk back down it.

**The rule tests the cell the step lands in** (settled phase 7b from code).
`ApplySliding` takes the normal at `m_vCOM`, the model's centre of mass
*after* the move (`getModelCOMPositionWorld`, `cobjchar_collision.cpp:457`,
`:1128`), with the move direction `m_vNext - m_vPrevious`; a refused step
resets to the previous position. So a character can slide *along* a curved
steep face: each step is downhill by the gradient of the cell it lands in
while the path as a whole climbs. `walk.py`'s liberal reach (either cell
allows) includes exactly that, which is why it found ways up low organic
cliffs; it is not over-cautious there.

**The cell model over-states climbing** (phase 7b). Even with the
landing-cell rule, `walk.py` finds El Verloon Desert 98% reachable from its
start, Gorge of Silence with 24% "trap" cells and Canyon City of Zant with
4%, yet players cannot climb those mountains in game: it chains
8-direction steps that zig-zag up a curved face, which nobody steering by
mouse does. So (user's decision) traps are looked for inside the play area
only, and borders are judged by their steepness against retail
(`RETAIL_BORDER`, `border_profile` in `mapgen-zone.py`). Inside the play
area the check is unchanged.

**The NE-corner blind spot is a real way up a face** (phase 7b). On a
face rising toward the north-east, a cell's SW, SE and NW corners can sit
on one contour while its NE corner is metres higher; the client reads the
cell as flat and lets the player step up it diagonally (map 2: corners
28.1 / 27.0 / 28.8 m, NE 39.1 m, a walkable line up an 11 m-per-step
face). `close_ring` lifts the NW and SE corners of every such reachable
face cell to a steep slope from the original ground (once per corner,
within the face band) until gentle ground from the start ends at the play
area's edge; the border check verifies it.

**A generated cliff needs a wide margin over 54°** (phase 7b). Cliffs
following an organic outline, designed at a 64° peak (50 m over 36 m),
measured as low as 52-54° in some cells where the outline ran
diagonally, and the walk check found a way up. 80 m over 24 m (designed
79°) holds on all five phase 7 maps. The walk and `play_share` checks catch
any cliff that leaks. A low cliff (25-45 m) only seals with a barrier along
its top (boulders or a fence, `mapgen/barrier.py`): its steep band is two or
three cells, and the slide-along-the-face path above gets over it.

**A face one cell wide leaks at every corner; a trench one cell wide is
a wall** (phase 7c). A cell takes its slope from its corners, so:

- A 12 m step over one cell is steep along a straight edge, but where the
  outline turns, cells straddling the face pick up corners from both
  levels and read gentle: the first ledge leaked at every bend. Level
  faces are at least two cells (5 m) wide, and `seal` lifts only face
  vertices, capped at the face's own top.
- A gully or ramp needs two whole cells of floor (7 m): a 4 m trench is
  one vertex across, every cell in it touches a wall, and it reads steep
  end to end.
- A way out of a pit must start rising where the pit's floor ends, not at
  its centre: rising from the centre left a 1-4 m step at the floor's edge
  (`levels.gully(flat_m=...)`, measured along the bearing for a ravine,
  whose floor is wider than designed where its banks stand high).
- The liberal reach climbs any face one cell wide (the flat cell below
  "allows leaving" upward). `ravine_sealed` uses it as an over-estimate,
  which holds because a ravine's walls are about three cells wide.

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
  - It is property 29: `SWITCH_COLLISION` = 8 + `MAX_MESH_ANI_TYPE` (21),
    `io_model.cpp:51-52`.
  - In `LIST_DECO_JG`: rocks are 12 (polygon + not moveable) on every part;
    trees are 12 on one part, often the trunk but sometimes a crown; grass
    is 0.
  - The client tests collision with spheres around the character's feet and
    body (`cobjchar_collision.cpp:510-530`). So only colliding geometry
    near the ground blocks walking (`catalogue.collision_profile`).
  - Ground height under the character comes from a downward ray, which
    hits object floors too (`:1338-1461`). So flat colliding triangles are
    floors to stand on, and only near-vertical ones (|normal z| < 0.5) are
    walls. `catalogue.Footprints` rasterises those walls 25-250 cm above
    the ground onto the 2.5 m grid and fills enclosed interiors (phase 6;
    a circle per object boxed in free cells between houses).
  - **A radius from mesh vertices misses tall flat walls.** A fence plank
    has vertices only at its foot and top; sunk 1 m, neither lies in the
    body band, so `collision_profile` gives it radius 0 although its wall
    crosses the band. Collision checks must go through the triangles
    (`Footprints`), never skip an object for a zero radius.
  - **Walls block both ways; only slopes are one-way.** Object collision
    is symmetric, so a pocket walled in by objects is never a trap: if you
    can get in, you can get out the same way. `walk.analyse` applies walls
    identically to its "can get there" and "can get back" floods, and keeps
    the step rule one-sided.
  - **Confirmed in game** (user, 2026-10-02): generated rocks and trunks
    block, grass and flowers don't. Objects with no `.lit` entry render
    without baked lighting, which looks flat but is otherwise correct.
- The editor's `3DDATA/STB/LIST_TERRAIN_OBJECT_<type>.STB` names each DECO
  object:
  - row = object id;
  - col 0 = name in UTF-8 Korean;
  - col 1 = source path, whose folder is the category (TREE, STONE, GRASS,
    VILLAGE, ETC, SPECIAL).
  - **The GRASS folder is not all grass** (phase 7, classified by eye from
    the textures): flowers (114, 133-139), mushrooms (115, 116), leafy
    plants (120, 121) and grass tufts / foxtail (117-119, 122-124, 131,
    132), all named `grassNNN`. `scripts/mapgen/stats/jg_object_kinds.json`
    holds the split.
- Each ZSC part stores a material index into the file's material list,
  whose entries begin with the texture path (`catalogue.read_zsc` keeps it
  as the part's `texture`).
- Lifted village paint can be water paint: Adventurer's Plain's harbour
  village and Kenji Beach's houses stand on seabed (7) and sand (6)
  brushes in retail.
- Terrain collides through the heightfield (`src/client/cobjchar_collision.cpp`).

## Not verified

- **IFO lump 7:** whether the client's misparse of it is always harmless, or
  just harmless on the data we have. Retail only ever holds the 16x16
  default, so copying that is safe.
