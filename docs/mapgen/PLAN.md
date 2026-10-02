# Map generator — plan

An experimental tool that generates new ROSE zones (maps) from a short
description plus a size and a seed, and writes a zone folder that loads in the
xadet map editor and in the game client with no manual fixes.

This file and [FORMATS.md](FORMATS.md) are the project's memory. A session
with no history must be able to continue from them alone. **Update both at the
end of every phase**: set the phase status, record what changed from the plan,
and move items out of FORMATS.md's "Not verified" list once they are settled.

## Scope

In scope for v1:

- terrain heightmaps and overall topography;
- tile/texture painting consistent with the terrain (slopes, shores, paths);
- decoration and building objects placed from the existing asset lists;
- water.

Out of scope for now: monster spawns, NPCs, quests, warps, event triggers, new
models or new textures (existing assets only), object lightmaps.

Input: a spec file (biome, landmarks, layout) plus map size and seed.
Output: a complete zone folder plus the table rows that register it.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Approach | Hybrid: a structured spec drives deterministic procedural code; parameters and village prefabs come from the original maps | 62 zones / 1,538 chunks is far too little to train a generative model, and tiles follow a deterministic autotile rule (nothing to learn). Statistics and lifted set-pieces are what the corpus can actually give. |
| LLM step | **The AI is Claude in the chat (Claude Code), never an API call** (user, 2026-10-02). The user describes a map; Claude writes the layout file and runs the tool | The user has a Claude subscription and will not pay for API use, and expects every user of the tool to be in the same case. Also keeps every build reproducible from a file. |
| Biome for v1 | **Junon grassland only** (zone type 0, `Table_Tileset_JG.STB`) | Best-covered tileset: JG01-JG08 + JPVP04 + SUM_EVENT, 289 chunks, and the autotile rule held for 100% of their tiles (FORMATS.md, TIL). |
| Lighting | **Flat-lit is acceptable for v1** | Object lightmaps cannot be baked by us yet; terrain falls back to `default_light.dds`. A terrain lightmap is optional phase 8. |
| Language / home | **Python**, package `scripts/mapgen/` | The editor's C# classes convert units on load/save (positions `/100 + 5200`, heights `/100`) and `HIM.Save` replaces the stored height bounds, so they cannot round-trip byte-exactly without a rewrite. Our STB/STL/ZSC/mesh tooling, numpy and Pillow are already Python. The editor stays an *independent oracle* for loading. |
| Villages | Prefabs lifted from retail zones are allowed | Rules alone look random; only 769 building placements exist in the whole corpus. |
| Size | 2x2 chunks first, 4x4 target | One chunk is 160 m x 160 m. |
| Zone number | Any unused `LIST_ZONE` row | Must be < 250 (`TEST_ZONE_NO`, rows >= 250 are never served). `ITEM_DROP` table ids share the zone-id namespace, so also pick a row no drop table uses. Chosen in phase 1. |
| Frozen lake | Tiles or objects, not a water plane | The water material is global and hardcoded to Junon's animated ocean (FORMATS.md, Water). Not needed for Junon grassland anyway. |
| Extra corpus | Reference dumps in `C:\Users\Thomas\Desktop\Testclients\` and the Jrose dump may be used for statistics | User approved 2026-10-02. |
| Docs location | `docs/mapgen/` | User's choice. Note the rest of the repo keeps docs in `doc/`. |

## Working rules

- **All work for this project is committed on the `mapgen` branch**, not
  `master` (user, 2026-10-02). Merging back is the user's call.
- `data/` is gitignored: **the scripts and these docs are the only committed
  record** of any data change. Put reasoning in script docstrings.
- Anything that writes into `data/` must be reversible (cell-level sidecar or
  manifest, `--dry-run` / `--verify` / `--restore`, like the other importers).
  Back up before writing; never leave `.bak` files under `data/` (the VFS
  packer bakes them).
- After installing a zone: restart the servers (they cache STBs and read
  `data/` directly) and re-bake the VFS with `scripts/pack.ps1` for the client
  (it reads maps from `rose.vfs` only).
- The editor is run from `data/Map Editor.exe` and logs to
  `data/Map Editor.log`; "Loading Completed" there is the editor-side load check.

## Phases

Each phase ends in something that can be loaded and looked at.

### Phase 0 — codecs and round-trip

Python read/write codecs for ZON, HIM, TIL, IFO (all lumps), MOV and LIT in
`scripts/mapgen/`, built from the client/server loaders, plus a re-runnable
round-trip check.

- **Check:** every `.ZON`, `.HIM`, `.TIL`, `.IFO`, `.MOV` and `.LIT` under
  `data/3DDATA/MAPS` is parsed into a model and re-serialised from that model,
  and the bytes are identical. Every file that is not identical is reported
  with the reason.
- **Rerun:** `python scripts/mapgen-roundtrip.py --selftest` (see its
  `--help`; `--root` points it at a reference dump, `--json` saves per-file
  results). Exit status 0 = pass. Rerun after any change to `scripts/mapgen/`.
- Writes nothing to `data/`.
- **Status:** DONE 2026-10-02.
  - All 9,035 files under `data/3DDATA/MAPS` re-serialise byte-identical:
    65 ZON, 1,538 HIM, 1,538 IFO, 1,547 TIL, 1,369 MOV, 2,978 LIT.
  - The edit-a-field selftest passes for all six formats.
  - No lump needed a raw fallback.
  - The only bytes carried uninterpreted are stale leftovers no loader reads:
    112 KB in 95 IFOs and 284 B in 2 ZONs. See FORMATS.md, "Codec status".
- **Changes from the plan:**
  - The codecs also model the lumps and fields the game ignores (IFO lumps 0
    and 7, the ZON cell table, the TIL editor bytes). This was not required, but
    it is what makes exact round-trip possible and lets the generator write
    editor-compatible files.
  - Code layout:
    - `scripts/mapgen/binio.py` — reader/writer primitives;
    - `container.py` — the ZON/IFO lump container;
    - `zon.py`, `ifo.py`;
    - `chunk.py` — HIM/TIL/MOV;
    - `lit.py`;
    - `scripts/mapgen-roundtrip.py` — the check.

### Phase 1 — flat map

A 2x2-chunk zone: flat terrain, one tile, no objects, `start` / `restore`
event positions, `LIST_ZONE` row and `LIST_ZONE_S` key in an unused zone
number. Installed and uninstalled by script.

- **Check:** our files match what the editor's File > New produces for the same
  inputs (modulo documented differences); editor log reaches "Loading
  Completed"; gameserver logs `Adding zone: #N`; `/mm N` in game; walk across
  every chunk seam with no new lines in `error.txt` / `client.log`.
- **Status:** DONE 2026-10-02. The user confirmed:
  - the editor GUI loads map 12;
  - the gameserver loads zone #12;
  - after a bake, `/mm 12` teleports into the zone in the client with no
    errors in the logs.

  Walking the seams and the outer-edge behaviour were not reported
  separately; the edge stays in FORMATS.md "Not verified".
  - **Installed as zone 12, "Mapgen Test"**, folder
    `data/3DDATA/MAPS/JUNON/MAPGEN01/`:
    - chunk slots x 32-33, y 32-33 (files `32_31`, `32_32`, `33_31`, `33_32`);
    - flat at 0 cm, JG bright grass (tile 5);
    - `start` at world (528000, 528000), `restore` 3 m east;
    - `LIST_ZONE_S` key `LZON012`.
    - Warp: `/mm 12 528 528`.
  - **Done:**
    - `mapgen-zone.py oracle` — with File > New's own inputs (2x2, JG01 tile
      file), all 25 files are **byte-identical** to what the editor's writers
      produce.
    - `mapgen-zone.py verify` — all checks pass: tables, references, files,
      chunk set, tile ids and textures, events inside chunks, editor
      `IsValidMap`.
    - `Run-NewZoneOracle.ps1 -LoadDir <folder>` — every file loads through the
      editor's own readers.
    - Full round-trip with the new zone included: 9,060 / 9,060.
    - `uninstall` tested: both tables returned to their exact pre-install bytes.
  - **Confirmed in the real programs (user):** editor GUI load; gameserver
    zone load; client teleport with clean logs.
- **How:**
  - Spec: `scripts/mapgen/specs/phase1-flat.json`.
  - Builder: `scripts/mapgen/zone.py` (pure: params → {path: bytes}).
  - CLI: `scripts/mapgen-zone.py {oracle,build,install,verify,uninstall}`.
  - Oracle: `scripts/mapgen/oracle/` (C# against `data/Map Editor.exe`).
  - Manifest with every overwritten cell: `build/mapgen/installed/MAPGEN01.json`.
  - Emergency table copies: `build/mapgen/backup/<stamp>/`.
- **Changes from the plan / decisions made:**
  - Zone 12 = lowest row free in `LIST_ZONE` and `ITEM_DROP` with no `LZONnnn`
    key. The installer refuses a row past the table's end rather than grow it.
  - The `LIST_ZONE` row is copied from JG01 (row 22). These columns are
    overridden:
    - name, ZON path, start/revive names;
    - minimap → `NOMAP`, minimap origin → NW chunk;
    - triggers 22-24 → blank (JG01 has a PvP trigger);
    - STL key;
    - revive 31-33 → blank.
  - Tiles, textures and economy come from JG01.ZON, as File > New copies them
    (JG01's economy block *is* the editor's default block).
  - No per-chunk plane lightmap: the client falls back to `default_light.dds`,
    a uniform (132,125,132) grey close to a retail JG chunk's mean.
  - HIM culling bounds are the editor's placeholders, which suit flat ground.
    Real bounds come in phase 2.
  - First install left minimap cols 9/10 blank; the editor would then hide the
    map. Caught by reading `IsValidMap`, fixed, reinstalled; verify now checks it.

### Phase 2 — terrain

Seeded height generation (hills, an enclosing ridge), shared chunk edges, HIM
height bounds computed, top-down preview PNG.

- **Checks:**
  - shared edges between neighbouring chunks are equal;
  - the start point is connected to the play area on climbable slopes (< 54°);
  - **no area the player can walk into but not climb out of** (the client
    refuses uphill moves on slopes >= 54° but allows downhill, so a steep-sided
    pit or basin is a trap): every cell reachable from the start must be able
    to reach the start back using only climbable steps;
  - ground never culls when turning the camera;
  - click-to-move works on slopes.
- **Status:** DONE 2026-10-02. The user confirmed in game:
  - the ground never vanishes while turning the camera;
  - moving up and down the hills works;
  - the ridge stops the player;
  - the only new log line is `interface: loadTexture() failed. file [NOMAP]
    not found`, harmless (see FORMATS.md, `LIST_ZONE` rules).

  Not separately tested: walking back down after getting onto the ridge face.
  The model says it always works.
  - **Zone 12 now holds the phase 2 terrain**, replacing the flat zone in the
    same folder:
    - 4x4 chunks (640 m), slots x/y 30-33;
    - seeded rolling hills, heights -15..70 m;
    - an impassable border ridge, 45 m high, 36 m wide.
    - Warp: `/mm 12 512 512`.
  - **Automated checks — `mapgen-zone.py verify`, all re-derived from the
    files on disk:**
    - shared chunk edges identical;
    - HIM culling bounds real and equal to the retail formula;
    - start height equals the terrain;
    - **0 trap cells**;
    - start connected to 100% of the gentle play area.
  - **Other checks:**
    - `walk-selftest` passes its 8 synthetic cases: walled pit = trap, pit
      with ramp = none, cliff drop with and without a ramp back, plateau,
      53° slope, 56° bowl;
    - same spec and seed give a bit-identical field;
    - editor readers load every file;
    - full round-trip passes at 9,132 files;
    - the bounds formula is re-proved on every `--selftest`.
  - **Interior slopes** are median 10.8°, p90 19°, max 30°. Retail JG zones run
    median 11-16°, with about 15% of cells >= 54° as cliffs; ours are the ridge
    only, about 11%.
  - **Preview:** `mapgen-zone.py preview SPEC` writes
    `build/mapgen/preview-<folder>.png`. Green = walkable and connected,
    red = >= 54°, magenta = trap, white = chunk borders, blue = start.
- **How:**
  - `scripts/mapgen/terrain.py`: Perlin fBm hills, an exact min-plus slope
    cap (`slope_cap`), a smoothstep border ridge, start spot = nearest gentle
    spot to the centre.
  - `scripts/mapgen/walk.py`: the client's step rule on 2.5 m cells, liberal
    reach / strict escape (FORMATS.md, "Slope rule").
  - `scripts/mapgen/preview.py`.
  - `chunk.compute_him_bounds`.
  - Spec: `scripts/mapgen/specs/phase2-terrain.json`.
- **Changes from the plan / decisions made:**
  - Went straight to the 4x4 target size: a ridge plus hills needs more than
    320 m.
  - The ridge is scenery: the border already blocks the player (user,
    phase 1).
  - Height bounds are now real values, computed the retail way, not
    placeholders.
  - The trap check had to be built carefully. Its first version missed a
    walled pit: the client ignores a cell's NE corner, which opens a fake
    diagonal escape. The second version invented traps at a pit's rim because
    it allowed no diagonal escape at all. The final rule requires a diagonal
    step to pass through a side cell, and the selftest pins all three
    behaviours.
  - `install` refuses terrain that fails the checks.

### Phase 3 — tile painting

Autotile painting with the JG tileset: slope → cliff, low → shore, paths.

- **Checks:** first regenerate an existing JG zone's TIL from its own corner
  brushes and match >= 99%; on generated maps every tile id exists in the ZON
  tile table, neighbours agree on shared corners, only legal brush pairs (the
  tileset's chain table).
- **Status:** DONE 2026-10-02. The user re-checked in the editor after the
  saddle fix: "looks better", with a continuous path and no dotted rows.
  - **Seen in that screenshot, not yet judged:** the sparse-grass (`T004`)
    patches show as squarish yellow, woven-looking blocks with fairly hard
    edges in the editor. It may be the texture or its transition tiles (set
    4/5, ids 72-86). Worth a look in game when the layout is reworked.
  - **User review (in-game and editor screenshots):** the tile transitions
    render cleanly in game and the path reads as a path; no issue.
  - **The layout looks random** (user): expected, since phase 3 tested the
    tile mechanics. The spatial design is open work below.
  - **Saddles found from the editor overview.** The editor showed a dotted
    line along the path and a "ladder" on a slope: checkerboard tiles
    (masks 6/9, two opposite blobs). They are legal, but retail JG uses them on
    ~0.04% of tiles and we had 1.3%.
    - Fixed: diagonal path steps now also paint a side corner, and
      `tiles.remove_saddles` removes the rest without breaking legality.
    - Now 0 saddles; `verify` allows at most 0.2%.
  - **Retail check** (`mapgen-zone.py tiles-selftest`): every JG zone's tiles
    regenerate from its own corner brushes at **99.22-99.96%** (need 99%).
    Neighbours agree on 99.5-99.96% of shared corners.
  - **Zone 12 now** is the phase 2 terrain painted with the JG tileset, with a
    dirt path from the south-west through the start to the north-east.
    `verify` re-derives from disk:
    - 0 illegal tiles;
    - 100% corner agreement;
    - 4096/4096 tile ids as their corners call for;
    - the zone type selects the JG tileset for the editor.
  - **Brush mix vs retail's** (weighted by this terrain's slopes):

    | Brush | Ours | Retail |
    |---|---|---|
    | bright grass | 55% | 57% |
    | dark grass | 19% | 13% |
    | dark soil | 9% | 16% |
    | rock | 15% | 10% |
    | sparse grass | 2% | 2% |
    | bright soil | 0.4% | 1.5% |
  - **Open for later (not a phase 3 failure):** the layout matches retail's
    *proportions*, not its *structure*. Patches are noise blobs, where retail
    puts brushes with purpose: rock under cliffs, dirt around buildings and
    along roads. Revisit with objects (phase 5) and villages (phase 6). Knobs
    already in the spec: `patch_wavelength_m`, `noise_spread`, the priority
    order.
  - **Previews:** `preview` also writes
    `build/mapgen/preview-<folder>-tiles.png` (brush colours from the real
    textures, hillshaded).
  - **Waiting on the user:** a screenshot and impressions in game — do the
    tile transitions and the rock patches look right?
- **How:**
  - `scripts/mapgen/tiles.py`: tileset, corner↔tile, `legalize` (chain
    table), `regenerate_check`.
  - `scripts/mapgen/paint.py`: the brush-by-slope statistics, calibrated
    noisy argmax, A* paths.
  - The statistics table: `scripts/mapgen/stats/jg_brush_by_slope.json`.
  - Spec: `scripts/mapgen/specs/phase3-paint.json`.
- **Changes from the plan / decisions made:**
  - **Painting follows retail statistics, not slope thresholds.** The corpus
    showed retail does not put rock on its steepest ground (above 60°: 53%
    grass, 9% rock); cliffs are presumably rock objects, phase 5.
  - **Calibration on the final lattice.** Brush shares are calibrated after
    paths and `legalize`. Plain log-probabilities left dark grass at 3%
    against 13%.
  - **Paths are dark soil**, which borders grass directly in JG; bright soil
    would need a dark-soil rim.
  - **Bugs caught by the checks before install:**
    - A* compared its estimate with the cost so far and expanded nothing;
    - tile fields were assigned positionally into a struct whose field order
      differs;
    - the brush names were read from a header row shifted by one;
    - a local variable in `verify` shadowed the `tiles` module.
  - **Saddle removal had to be a separate, guarded pass.** Folding it into the
    in-between-brush fixer made the two undo each other forever. Each accepted
    change now strictly reduces the saddle count and keeps every touched tile
    legal; path corners are never changed.

### Phase 4 — water

A lake at a set level with shore tiles.

- **Checks:** same waterline in editor and client; the plane does not show
  outside the basin.
- **Status:** DONE 2026-10-02. The user confirmed with editor and in-game
  screenshots:
  - the same waterline in both;
  - walking along the shore is normal;
  - you can walk into the lake and back out.

  The pale band at the water's edge is shallow water over sand brightened by
  the sea material's "lighten" blend. Its slightly polygonal outline is the
  flat plane meeting the 2.5 m terrain grid, as on retail shores.
  - **Zone 12 now** is the phase 3 map plus one lake:
    - about 5,000 m² and 4 m deep, water level 8.57 m;
    - carved into the flattest low site (south-west quarter);
    - a seabed → sand → dark soil → grass shore.
    - Warps: lake `/mm 12 498 501`; walk in from the east at
      `/mm 12 504 501`.
  - **`verify`, re-read from the IFOs on disk:**
    - rectangles on disk equal the spec's;
    - each rectangle starts at its NW corner, has start z == end z, is whole
      20 m cells, and lies inside its own chunk;
    - every lake vertex is covered;
    - water shows nowhere outside its basin;
    - 0 traps.
  - **Also:** the editor's readers see 1 water plane in each of the two lake
    chunks; all regression checks pass (oracle, both selftests, retail tiles,
    9,132-file round-trip).
- **How:**
  - `scripts/mapgen/water.py`: site choice, carving, lake mask, per-chunk
    20 m rectangles, checks.
  - The painter takes `forced` brushes (seabed under water, sand on the
    beach). Paths route around them; calibration ignores them.
  - The start avoids water plus a 15 m band.
  - Spec: `scripts/mapgen/specs/phase4-water.json`.
- **Changes from the plan / decisions made:**
  - **Water conventions copied from retail's 850 rectangles:**
    - `size` 2000;
    - start = NW corner (min x, max y; 777 of 850);
    - equal z;
    - 20 m cells;
    - one chunk per rectangle (retail: 94%) so streaming never drops a
      rectangle that another chunk needs.
  - **Water level = just under the lowest ground in a ring outside the
    outline.** The ground under every rectangle is then either lake or above
    the surface by construction.
  - **First attempt carved a crater over a quarter of the map:** it picked
    the lowest spot (at the foot of 30-40 m hills) and let the shore cone run
    until it met the ground. It passed every check (nothing unwalkable) but
    looked wrong in the preview. Fixed:
    - the flattest low site is chosen instead;
    - the shore shaping fades out over a 40 m skirt;
    - a site needing more than `max_cut_cm` of extra cutting is refused.
  - **Players walk into water along the bed** (nothing collides with ocean
    nodes), so lake shores fall under the same trap check as any slope.

### Phase 5 — decoration

Trees and rocks from a catalogue built from the JG decoration ZSC, footprints
from mesh headers (the ZSC's stored boxes are wrong).

- **Checks:** no missing meshes; `scripts/fix-coplanar-object-overlaps.py
  --dry-run` reports 0 pairs; decoration per chunk within the retail range
  (max 278); collision works in game.
- **Status:** DONE 2026-10-02. The user tested in game and in the editor:
  - **collision** works on trees, grass and rocks: what should stop you does,
    what shouldn't doesn't;
  - **density** is fine as a baseline ("some could be denser, some lighter");
  - **the look is flat** without baked lighting, which was expected and can
    come later.

  Noted, not changed:
  - large rocks placed near the lake can lean into the water, because
    clearance is measured from the object's centre;
  - the orange "mushroom" (`grass002`) is a retail grass-category object.
  - **Zone 12 now** is the phase 4 map plus 1,236 decorations: 1,022 grass
    and flower clumps, 166 trees, 48 rocks; 44-121 per chunk.
    - Warps: nearest tree `/mm 12 510 514`, nearest rock `/mm 12 512 515`.
  - **`verify` from disk:**
    - objects on disk = the spec's;
    - every id exists and every mesh is on disk;
    - every object is stored in the chunk that contains it;
    - nothing in the water, the start area or on the path;
    - colliders keep a 3 m gap;
    - with object collision: 0 traps and 100% connected.
  - **Also:**
    - `fix-coplanar-object-overlaps.py --zone 12 --dry-run`: 0 fighting pairs;
    - the editor's readers see the decorations in every chunk;
    - all regression checks pass.
- **How:**
  - `scripts/mapgen/catalogue.py` builds `stats/jg_decoration.json` (via
    `mapgen-zone.py stats`): per object its category (the editor table's
    source folder), name, real size and reach from the mesh headers, a
    collision profile from the mesh vertices, and retail's uses, scale and
    sink percentiles, slope-band and brush counts.
  - `scripts/mapgen/decorate.py`:
    - Poisson placement per 10 m tile at retail's density for that slope band
      and brush, times low-frequency cluster noise;
    - object choice by the same densities;
    - scale and sink from retail's middle 50%; random yaw;
    - exclusions (water + 3 m, path + 4 m, start + 15 m) and spacing;
    - `repair` removes any collider next to a trap cell.
  - `walk.analyse` takes the blocked cells.
  - Spec: `scripts/mapgen/specs/phase5-decorate.json`.
- **Changes from the plan / decisions made:**
  - **Categories** come from the editor's `LIST_TERRAIN_OBJECT_JG.STB`
    source folders (TREE / STONE / GRASS / VILLAGE / ETC / SPECIAL). v1 places
    GRASS, TREE and STONE objects that retail uses at least 10 times,
    excluding water plants (duckweed, lotus).
  - **Collision footprints come from mesh vertices, not boxes.** The client
    collides with spheres around the feet and body. Only colliding geometry
    0-2.5 m above the ground (after scale and sink) blocks walking. With the
    bounding box a big tree blocked a 10 m circle and created 5 false traps.
    Now a leafy tree blocks 0.36 m, a palm 0 (collision only in its crown),
    big trees and rocks 3-6.5 m.
  - **A real trap class exists:** a tree on a >= 54° slope can block the only
    downhill way out of a steep cell. `decorate.repair` removes such
    colliders; none were needed on this map once footprints were real.
  - **Yaw-only rotation for v1**, although retail tilts 43% of rocks.
  - **No lightmap entries** for the objects; they render without baked
    lighting (flat-lit v1). How that looks is part of the in-game check.

### Phase 6 — village

Prefab building clusters lifted from retail zones, placed on flattened ground
and joined by paths.

- **Checks:** ground under each footprint is flat within a few cm; every
  building is reachable from the start.
- **Status:** DONE 2026-10-02. The user confirmed the entrance fix in the
  editor: the path leaves the farm through its archway and runs on to the
  huts. No in-game re-test needed (paint only; collision unchanged).
  - **First install, tested by the user in game and in the editor:**
    - collision around houses, fences and the windmill is normal;
    - nothing floats or is half-buried;
    - the farm's fields and fences are acceptable on flat ground;
    - the pad edges blend into the hills;
    - but **the paths did not reach the entrances** (user and assistant both
      saw it). The path ended at the pad-edge corner facing the map centre:
      for the farm, bearing -53° by the end of the fence ring, with the
      `farmgate` archway at 175°.
  - **Fixed (second install):** each village has an entrance, and a path
    runs from its centre out through the entrance and on to the start (see
    "How", step 5). Fixing it exposed two checker defects, both fixed
    (see "Changes"): fences were invisible to the collision checks, and
    pockets walled in by several objects read as traps.
  - **Zone 12 now** is the phase 5 map plus two villages:
    - `breezy_farm` (Breezy Hills farm, windmill and fields): 55 members,
      52 of them blocking (fences now count); 96 m pad at -1.8 m; turned
      90°; entrance = the `farmgate` archway at 175° (named in the spec).
      Warp `/mm 12 507 519`.
    - `sunshine_huts` (Sunshine Coast huts): 8 members, 8 blocking,
      2 of them construction records; 68 m pad at 16.0 m; turned 0°;
      entrance = the middle of its widest opening (333-22°, so 358°). Warp
      `/mm 12 526 500`.
    - Decoration is placed after the villages and keeps 4 m off the pads:
      1,173 decorations (the new paths moved the paint, and decoration
      follows the paint), none removed by `repair`.
  - **`verify` from disk:**
    - decoration and construction records on disk = the spec's (2 CNST);
    - every village object id exists in its ZSC and every mesh is on disk;
    - ground under every village member's walls is flat within 5 cm;
    - every blocking village member can be reached from the start;
    - with village and decoration collision: 0 traps;
    - a path-brush trail runs unbroken from the start into every village's
      entrance, and the way from the centre out through it is walkable with
      every object in place;
    - all phase 2-5 checks still pass (and the phase 5 spec still passes).
  - **Also:** `fix-coplanar-object-overlaps.py --zone 12 --dry-run`: 0 pairs
    among 1,236 placements; oracle, walk and tile selftests and the
    round-trip (9,132 / 9,132) pass.
- **How:**
  - `mapgen-zone.py prefab-extract NAME --zone-row R --centre X Y --radius M`
    lifts every lump 1 / lump 3 record within the radius of a retail zone,
    plus the corner brushes under it, into `scripts/mapgen/prefabs/NAME.json`
    (`scripts/mapgen/prefab.py`). Members are stored relative to the centre,
    with their height above the original ground.
  - Placement (`add_villages`) runs after the lakes are carved and before
    the start spot, painting and decoration (the start then avoids the
    pads):
    1. `prefab.rotate` turns the prefab by a multiple of 90°, so its brush
       lattice turns with it exactly (`rotation: "auto"` = seeded).
    2. `prefab.pick_site` chooses the tile-corner vertex with the lowest
       relief within pad + skirt, clear of the lakes, the ridge band and
       earlier villages.
    3. `prefab.flatten` sets the pad (prefab radius + `pad_margin_m`) to its
       mean height and blends back over `skirt_m` with a smoothstep.
    4. `prefab.instantiate` places the members at pad height + their stored
       height above ground; the prefab's brushes are forced in `paint`.
    5. With `connect`, the village gets a path in:
       - **entrance:** the member named by the spec's `entrance` (the farm's
         `farmgate`); else the middle of the widest arc of bearings along
         which a ray from the centre reaches the pad edge without crossing a
         wall cell (`prefab.openings`); else the side facing the map centre;
       - **inner route:** a walk-grid route (2.5 m cells, so it fits a 7 m
         archway the 10 m tile lattice cannot see) from the centre to the
         pad edge at that bearing (`prefab.inner_route`). Its tile corners
         are forced to the path brush over the village's own paint, from the
         pad edge inwards until it meets ground the village already paints
         with the path brush;
       - **outer route:** the usual A* path from the route's outer end to the
         start. Paths may not cross village paint (it would cover them) or
         corners under blocking members.
  - Spec: `scripts/mapgen/specs/phase6-village.json`.
- **Changes from the plan / decisions made:**
  - **Prefabs are lifted, not authored.** Five were extracted from JG zones
    whose `LIST_ZONE` row names the same DECO / CNST ZSCs (object ids are
    indices into those): `breezy_farm`, `sunshine_huts`, `sunshine_cove`,
    `anima_camp`, `kenji_houses`. The spec uses the first two.
  - **Most village buildings are decoration, not construction.** JG's houses,
    windmill, fences and crates live in `LIST_DECO_JG` (editor categories
    VILLAGE / ETC). `LIST_CNST_JG` holds only the beach houses (`shouse`,
    `mhouse`, `lhouse`); the Sunshine Coast prefabs carry them.
  - **Collision from triangles, not circles.** With houses in play, the
    phase 5 circle footprints boxed in a free cell between the windmill, a
    house and crates (one false trap). `catalogue.Footprints` now rasterises
    the near-vertical colliding triangles of each placement (25-250 cm above
    the ground) onto the 2.5 m grid, then fills enclosed interiors: an
    unfilled house is a ring around unreachable cells, which first read as
    21 traps.
  - **Fences were invisible to the checks** (found with the entrance fix).
    An object's radius came from its mesh vertices, and a fence plank has
    vertices only at its foot and top: sunk 1 m, neither is in the body
    band, so the radius was 0 and `blocked_cells` skipped it. Now
    `blocked_cells` and `repair` use every object's wall triangles
    (`Footprints`), whatever its radius, and village members get their
    radius from those cells (`prefab.footprint_radius`). The flatness check
    measures the ground under a member's wall cells.
  - **Walls now block both trap floods alike** (`walk.analyse`). The
    liberal "can get there" flood ignored blocked cells, so any pocket walled
    in by several objects (a fence corner against the wine press: 5 cells)
    read as a trap. A wall blocks both ways: a walled pocket you can enter
    you can also leave. Only the slope rule is one-sided, so only it differs
    between the floods. The selftest's "walled pocket = trap (conservative)"
    case is now "unreachable, no trap", plus two new cases: walls touching at
    a corner point seal, and a wall gap into a 15 m pit is still a trap.
  - **The farm's source ground had 16 m of relief.** Members keep their
    height above the original ground, so on a flat pad its fields and fences
    sit level instead of on a slope. Whether that reads well is part of the
    in-game check; `extract` prints the relief so flatter sources can be
    preferred.
  - **No lightmap entries** for village objects either (flat-lit v1).

### Phase 7 — text input

Description → layout file → spec → zone. Design agreed with the user
2026-10-02:

- **The AI decides layout and intent only, never details.** Claude (in the
  chat) turns the user's description into a short **layout file** in a
  closed vocabulary (terrain flat / rolling / hilly; sizes small / medium /
  large; cover forest / meadow / rocky / bare; places as compass areas,
  "near X" or map fractions). Unknown words are rejected.
- **A deterministic compiler** (Python, no AI) turns the layout into the full
  spec, mapping each word to retail-calibrated numbers.
- The existing generator and checks run unchanged. A failure is reported in
  plain words ("no flat site for the farm near lake 1"); Claude adjusts the
  layout or asks the user.
- **Generator work it needs:** density per area (user chose this, "option
  2", over one global knob); placing lakes / villages / start inside a named
  area; roads between named features; optionally terrain character per
  area; **intent checks** (the lake really is in the south-west, the forest
  really is denser).
- **Checks:** the same layout and seed give byte-identical files; five
  descriptions **written by the user** pass every validator and intent check
  with no hand fixes.
- **Status:** INSTALLED 2026-10-02; all automated checks pass. Reviewed by
  the user in the editor (2026-10-02): flower density right; three things
  to improve, recorded in [DESIGN.md](DESIGN.md) (the new level-design
  guide): stripes of mixed tiles on mountains (measured: patches too small
  vs retail), harbour villages on stilts placed on dry land, and maps that
  are all square where ROSE bends the play area. Not committed.
  - **The user's five descriptions**, as layouts in
    `scripts/mapgen/layouts/`, each installed as its own zone:

    | # | Description (short) | Layout | Zone | Warp |
    |---|---|---|---|---|
    | 1 | a small coastal village | `p7-1-coastal.json` | 13 | `/mm 13 515 501` |
    | 2 | flower field along water, mountains NW and NE | `p7-2-flowers.json` | 30 | `/mm 30 512 512` |
    | 3 | an abandoned city, jagged terrain | `p7-3-abandoned.json` | 38 | `/mm 38 516 510` |
    | 4 | like Adventure Plain but 50% water | `p7-4-adventure-water.json` | 39 | `/mm 39 531 520` |
    | 5 | the detailed valley (lake, hill village, farm, roads, hut) | `p7-5-valley.json` | 40 | `/mm 40 530 498` |

    Zone 12 still holds phase 6. Each layout's `notes` record what Claude
    chose, substituted or could not do. **#3 is only partly possible:** the
    Junon grassland set has no city buildings, walls or ruins, and every
    building is intact, so "city" became three unconnected village clusters
    on overgrown ground.
  - **Every description passes, preview and verify from disk:** 51-68 checks
    each, 0 failures, including its intent checks.
  - **Also:** walk / tile selftests and the editor oracle pass; the
    round-trip covers 9,617 files; the editor's readers load the new zones;
    `fix-coplanar-object-overlaps.py` finds 0 pairs (now a `verify` check);
    two builds of a layout are byte-identical; phase 5 and 6 specs build
    unchanged (zone 12 still verifies).
- **How to make a map from a description (for Claude):**
  1. Read `scripts/mapgen/layout.py`'s docstring (the vocabulary) and the
     prefab list (`scripts/mapgen/prefabs/`). Lift a new prefab with
     `prefab-extract` if none fits (survey retail clusters first).
  2. Write `scripts/mapgen/layouts/<name>.json`: the description verbatim,
     `notes` for every choice or substitution, a new `folder` (MAPGENnn).
  3. `mapgen-zone.py preview <layout>`: every check plus the intent checks
     and a "placed:" list. Look at `build/mapgen/preview-<folder>-tiles.png`.
     A failure comes back in plain words; change the layout (and say why in
     `notes`), or fix the generator if the failure is the tool's.
  4. `mapgen-zone.py install <layout>` picks a free zone row and verifies;
     the user bakes (`scripts/pack.ps1`) and restarts the servers.
  `mapgen-zone.py compile <layout>` shows the spec a layout becomes.
- **How (the generator):**
  - `scripts/mapgen/layout.py` compiles a layout to a spec. Words map to
    numbers in its tables: terrain `CHARACTER` (flat … rugged), `RISE_CM`,
    `FEATURE` (hill / broad hill / mountains), `LAKE` sizes, `COVER` (forest,
    woods, meadow, flowers, rocky, bare, overgrown: per-kind multipliers and
    brush biases), `DENSITY` (a power on the multipliers, so "dense meadow"
    means even fewer trees). Places compile to `areas.py` descriptors. Each
    claim also becomes an intent check, labelled with the layout's words.
  - `scripts/mapgen/areas.py` resolves a place on the built map to a soft
    weight per vertex, a target point and a radius; "near X" / "X north
    shore" refer to features placed earlier in the build.
  - New spec sections, all optional (phase 1-6 specs build unchanged):
    `terrain.tilt`, `terrain.features` (hills before the slope cap,
    mountains after it), `terrain.ridge.edges`, `water.flood` (one level for
    a share of the map; puddles filled), lake `where` / `banks`, village
    `where` / `on` / `near_water` / `pad_from_walls`, `roads`, `start`,
    `cover`, `intent`, `decorate.kinds`.
  - Build order (`params_from_spec`): terrain + features → lakes, flood →
    villages → start → events → cover → paint (+ roads) → decoration →
    records → coplanar separation.
  - `mapgen-zone.py profiles` measures the retail JG zones (slope, water,
    decoration per kind) into `stats/jg_zone_profiles.json`, for "like JG01".
  - `stats/jg_object_kinds.json`: the GRASS folder split by eye from the
    textures into flowers, mushrooms, grass tufts and leafy plants.
- **Changes from the plan / decisions made:**
  - **Villages by the water** (coast, lake shore) keep only their pad off the
    water, sit 1.5 m above the waterline, and re-derive the water outlines
    after flattening. Before this, every village kept pad + skirt + 15 m
    from water, so "a coastal village" could not be placed.
  - **Entrances and the start follow the paths.** A village with roads leaves
    through the opening nearest its first road partner. One by the water
    with no road leaves onto the largest stretch of ground a path can cross
    ("away from the nearest water" points at more water on a peninsula).
    "The player starts at X" puts the start inside village X, on its walk
    route 10 m in from the entrance: flat, free of walls, on the path.
  - **The flat pad is sized from the prefab's walls**, not its extraction
    radius, and a hill a village sits on gets a top that size. Adventurer's
    Plain's wooden platforms reached past the old pad onto the skirt slope.
  - **Harbour prefabs inland are repainted:** sand / seabed → bright soil,
    stream gravel → dark soil (`paint.inland_remap`). Adventurer's Plain and
    Kenji Beach houses stood on seabed paint.
  - **Coplanar faces are separated during the build** with
    `fix-coplanar-object-overlaps.py`'s own solver: a prefab lifted from 5.8 m
    of relief onto a flat pad brought two platforms' faces within 0.06 cm. A
    hand nudge of one member just created a fight with another; the solver
    plans them together.
  - **"Rugged" was re-tuned:** at 55 m amplitude over 120 m the slope cap
    rewrote 83% of the map into planar cone facets (it looked like
    pyramids). Each character now keeps its noise mostly under its own cap
    (rugged 15%, hilly 19%, rolling 0%).
  - **Density intent is judged against the same ground's default:** the
    catalogue's retail density for each tile's slope band and brush, with
    2-sigma Poisson slack. Comparing with "the rest of the map" fails when
    cover areas fill the map, and retail's flat average is unfair to steep
    ground.

### Phase 7b — review fixes: paint, stilt villages, map shape

The user's review of the phase 7 maps (DESIGN.md) found three things; the
user asked for all three fixed before any new maps.

- **Status:** INSTALLED 2026-10-02 over the same zones (13, 30, 38, 39, 40;
  new warps: `/mm 13 490 493`, `/mm 30 511 511`, `/mm 38 515 508`,
  `/mm 39 500 498`, `/mm 40 518 508`); all five pass every check from disk;
  regressions pass (selftests, oracle, round-trip 9,617 / 9,617, phase 5/6
  specs unchanged, zone 12 verifies, byte-identical double build, editor
  readers). In-game review handed to the user. Not committed.
- **1. Paint in retail-sized patches** (`paint.py`; layout defaults in
  `layout.base_spec`):
  - Measured first. Retail corners agree with their neighbours 75.5% (all),
    83.6% (>= 45°), 88.8% (>= 60°); ours 65-72%. Cliffs carried 24-35%
    soil against retail's 11-12%.
  - Cause: `legalize` always converts the lower-priority corner and bright
    grass ranks lowest, so one speckled bright-soil corner turns up to eight
    grass neighbours into dark soil. The noise *picked* the right mix; the
    in-between rules quadrupled the soil on cliffs.
  - Fix: `despeckle` (lone corners take their neighbourhood's brush) before
    legalize; longer patches (90 m); 16 calibration rounds; a **cliff
    palette** above 40° (bright grass 50%, dark grass 35%, rock 15%, 200 m
    patches, out of the calibration). Result over the five maps: 74.5 /
    80.1 / 83.8% agreement, cliff soil 10-14%, rock 10-12%.
  - Check: `coherence_check` (within 6 points of retail) in preview and verify.
  - Flooded water re-done as **compact basins** (`water.flood_compact`):
    flooding raw hilly terrain made a maze where 62% of the land was within
    30 m of a shore (retail 5-11%); choosing the water on a 60 m low-passed
    copy and shaping the ground to agree brings it to 29%.
  - Decoration capped at 250 per chunk (retail max 278), thinning small
    non-colliding objects evenly.
- **2. Stilt villages stand over water** (`prefab.py`):
  - `prefab-extract` now records the source water: Adventurer's Plain's
    village had 67 of 69 corners under 1 m of water, decks at 4.6-5.0 m
    above it; Kenji Beach 65 of 81. Members store `above_water_cm`.
  - By the water, `pick_harbour` chooses the shore site **and rotation**
    where the wet part lies over water and the ramp corners on land (97%
    of the ground already fits in map 4); `shape_harbour` sets the ground
    as in the source; members stand at their height above the water.
  - The compiler refuses a stilt village away from water unless the layout
    asks for `"pond": true` (the pond option works on flat ground; on jagged
    ground it dug pits with traps, so it is opt-in).
  - `prefab-land` derives dry-ground variants (`adventurer_houses`,
    `kenji_houses_land`): decks and stilts dropped, members at retail's land
    heights, stacked crates kept stacked. Used for the hill village (#5)
    and the inland quarters (#3).
- **3. Map shape** (`terrain.play_shape`, `shape_walls`; layout `shape`,
  `play`):
  - Measured first. Retail chunk sets are full rectangles (3 of 57 maps
    miss a chunk), but only 24-55% of a Junon map is walkable dry land; the
    blocked ground reaches in from the edges unevenly (median 8-50% of the
    side). Ours were 62-90% walkable behind an even 36 m band.
  - The play region is an organic mask (`organic`, `basin`, `winding`,
    `valley north-south / east-west`; `square` = the old ring) covering
    `play` small / medium / large = 30 / 45 / 60% of the map; an `open`
    edge (a coast) is reached through a decaying bonus.
  - Outside it the ground is **rebuilt from the rim**: nearest rim height +
    an 80 m cliff over 24 m + highlands; beyond the cliff the rim is
    smoothed (no streaks). Built on top of the terrain instead, hilly ground
    falling away outward turned the cliff into ramps.
  - Compass words mean the play area's own north, south-west …:
    `areas._radial` maps them onto the region (corners ~63% of the way out).
    Lakes keep radius + 40 m inside it.
  - `areas.distance_m` / `areas.nearest` are now exact Euclidean (feature
    transform); the L1 approximation left gaps in cliffs.
  - Check: `play_share` (walkable share within 12 points of the target).
    The five maps measure 47-49%.
- **Second review round** (user, 2026-10-02: ramp in the water, cliff
  stripes, cliffs too high, maps too big, use fences). INSTALLED over the
  same zones; new warps `/mm 13 529 511` (now 3x3), `/mm 30 511 511`,
  `/mm 38 515 508`, `/mm 39 499 514`, `/mm 40 518 508`; all five pass
  56-73 checks from disk, regressions pass (round-trip 9,575 / 9,575).
  - **Ramp on land:** `prefab._landings` records the ramp foot that stood on
    dry ground (one per stilt village); `pick_harbour` halves a site's score
    per landing over water; `shape_harbour` makes shore under it and a
    uniform 1 m shelf under the decks (a deeper seabed beside the raised
    landing made an unclimbable step). Check: ramp on dry ground.
  - **Cliff material:** one per stretch (grass / rock / earth) from the
    cover within ~40 m (`paint.cliff.materials`, `material: auto`); cliff
    corners agree 98%.
  - **Cliff height** `low` 25 m (default) / `medium` 45 m / `high` 80 m /
    `none`, and **rims** (`mapgen/barrier.py`): boulders (default) or a
    fence along the cliff top, or a fence line alone. The client tests the
    cell a step lands in (now settled from code, FORMATS.md), so a low
    curved cliff can be climbed sideways; the rim seals it. Tested on all
    five maps with every height and rim: 44-50% walkable, 0-1 trap cells.
  - On the way: compact flooding no longer carves through the cliffs and
    lifts every outside vertex above the waterline (water had spilled over
    low cliffs); water keeps 12 m off the play edge; a stilt village keeps
    its whole shelf inside the play area (it had cut a channel through the
    cliff); the per-chunk cap thins grass before trees; density intent
    judges what each area asked (mean multiplier, fades included) on ground
    decoration may use.
  - **Size by content:** one village, at most one lake and no mountains
    give a 3x3 map unless the layout says otherwise (map 1 is now 3x3).
- **Third review round** (user, 2026-10-02: ramp OK; cliffs should follow
  the map theme; no boulder rims, fences only for villages and cities;
  borders like El Verloon's, low but unclimbable). INSTALLED; warps
  `/mm 13 528 508`, `/mm 30 511 511`, `/mm 38 515 508`, `/mm 39 503 498`,
  `/mm 40 518 508`; all five pass 57-74 checks from disk; regressions pass.
  - **Walk standard (user's decision, "match retail"):** the cell model
    finds retail maps 87-100% reachable with up to 24% trap cells, so traps
    count inside the play area only (`walk.analyse(trap_scope=...)`), and
    borders must match retail's measured steepness (`border_like_retail`:
    >= 75% of cells steep 5-10 m out, >= 35% at 10-20 m, >= 15 m up).
    Play-area size is measured as the retail walk region.
  - **Mountain border** (`terrain.shape_walls` profile `range`): rim + a
    30 m face over 10 m + a body of ridged noise (45 m ridges, 15 m body);
    `high` = 38 m / 12 m / 26 m. Built *after* the water, from the final
    rim (built before, a compact flood flattened the face foot and opened
    the flooded map). No rim; `border: fence` for villages and cities.
  - Cliff material: one per map, from the theme (grass by default).
  - Compact flooding at 90 m (60 m left the half-water map's paint short).
- **Fourth review round** (user, 2026-10-02: ranges natural and
  unclimbable; grass props on faces; tops don't need to be wavy and could
  be highground). INSTALLED; warps unchanged (`/mm 13 528 508`,
  `/mm 30 511 511`, `/mm 38 515 508`, `/mm 39 503 498`, `/mm 40 518 508`);
  all five pass 58-75 checks from disk; regressions pass.
  - Plateau tops (`top: plateau`, 4 m of gentle variation).
  - Decoration: only rocks on slopes over 45° (`decorate.max_slope_deg`).
  - Border check: steep face + rise as before, the 10-20 m steepness
    dropped (a flat top is intended), and a closed-ring test: gentle ground
    reachable from the start (8-way) ends within 6 m of the play area.
  - `close_ring` closes the NE-corner blind spot (FORMATS.md) by lifting
    face corners after the start is chosen (2-1,928 corners per map).
  - The compact flood lifts every dry play vertex above the waterline
    (`dry=`): the 12 m edge strip had stayed 28 m under it on hilly ground
    and the border was built from it.

### Phase 8 — polish (optional)

Baked terrain lightmap, generated `.MOV`, minimap.

Lightmaps (user, 2026-10-02): either the user gets an existing baking
script shared by someone, or we write our own later. Ask before starting.

- **Check:** visual comparison with a retail zone.
- **Status:** NOT STARTED.

### Beyond v1 — level design (user, 2026-10-02)

The user expects v1 not to give the best results as is: ROSE has big cities,
countryside towns, green plains, canyons, forests, beaches, jungle and
wastelands, and the tool needs a real grasp of level design for each.
"Training" here cannot mean training a model (Claude is used through the
chat). It means teaching the pipeline from the retail maps:

- **Per-biome statistics and catalogues** (tileset, decoration, brush mix,
  water habits) — the phase 3-5 measurements repeated per biome.
- **New landform generators** that noise alone cannot make: canyons (carved
  channels, terraces), coasts, plateaus, city grounds.
- **More and larger prefabs**: city blocks, plazas, walls, gates, ruins.
- **Retail maps described in the layout vocabulary** — worked examples
  Claude reads before writing a layout, and a test of whether the
  vocabulary can express real ROSE maps at all.
- **A level-design guide** (`docs/mapgen/DESIGN.md`, to write): measured
  patterns from retail (road widths, how towns sit against hills, where
  forests stop) plus the user's review notes from each generated map.

Phase 7 keeps to Junon grassland but shapes the layout vocabulary so these
can be added without rewriting it.

## Change log

- 2026-10-02 — Plan agreed. Python chosen over a C# CLI on the editor's
  writers. Phase 2 gained the "no inescapable area" check. Phase 0 started.
- 2026-10-02 — Phase 0 done. Next: phase 1 (pick an unused zone row < 250
  that no `ITEM_DROP` table uses; template the tile table, textures and
  economy from JG01.ZON the way the editor's File > New copies them). Nothing
  committed yet; the user decides when.
- 2026-10-02 — Phase 0 committed on the new `mapgen` branch (`d34c0011`).
  Phase 1 built and installed as zone 12; automated checks pass; editor GUI /
  server / in-game checks handed to the user.
- 2026-10-02 — Phase 1 confirmed by the user (editor, server, client) and
  committed. Next: phase 2 (terrain). Its first job is to settle the HIM
  trailer indexing (FORMATS.md, "Not verified") before writing real bounds.
- 2026-10-02 — The map edge simply blocks the player (user). Phase 2 built:
  HIM bounds format and the client's slope rule settled from code + corpus;
  terrain generator, trap checker and preview written; zone 12 reinstalled
  with 4x4 terrain; automated checks pass. In-game checks handed to the user.
- 2026-10-02 — Phase 2 confirmed in game by the user and committed. Next:
  phase 3 (tile painting with the JG tileset). Its first check is to
  regenerate an existing JG zone's TIL from its own corner brushes. Phase 8's
  minimap will also retire the harmless `NOMAP` texture warning.
- 2026-10-02 — The ridge can be walked down (user): both halves of the slope
  rule confirmed in game. Phase 3 built and installed over zone 12; automated
  checks pass; in-game look handed to the user. Not committed yet.
- 2026-10-02 — Phase 3 reviewed by the user in game and in the editor: tiles
  render correctly; the layout is random, which is expected at this stage.
  Saddle tiles found in the editor view, removed, zone 12 reinstalled. Next:
  phase 4 (water).
- 2026-10-02 — Saddle fix confirmed by the user in the editor; phase 3
  committed.
- 2026-10-02 — Phase 4 built: retail water conventions measured, lake
  carving + rectangles + checks written, zone 12 reinstalled with a lake;
  automated checks pass; in-game/editor waterline check handed to the user.
- 2026-10-02 — Phase 4 confirmed by the user (waterline, shore, walk in/out)
  and committed. Next: phase 5 (decoration: trees and rocks from the JG
  decoration catalogue).
- 2026-10-02 — Phase 5 built: decoration catalogue from the ZSC, editor table
  and retail placements; statistical placement with collision-aware trap
  checks; zone 12 reinstalled with 1,236 objects; automated checks pass;
  in-game look and collision handed to the user.
- 2026-10-02 — Phase 5 confirmed in game by the user: collision correct,
  density fine as a baseline, flat look expected. Open for later: object
  and terrain lightmaps (phase 8 or a dedicated lighting phase), per-spec
  density tuning. Next: phase 6 (village prefabs).
- 2026-10-02 — Phase 5 committed. Phase 6 built: five prefabs extracted
  from JG zones, site picking + pad flattening + paths, geometry-based
  collision footprints; zone 12 reinstalled with two villages; automated
  checks pass; in-game and editor look handed to the user. Not committed.
- 2026-10-02 — Phase 6 reviewed by the user in game and in the editor:
  collision, placement and blend fine; paths did not reach the village
  entrances. Fixed before commit: entrances (named member or widest
  opening) with a walk-grid route in. Two checker defects fixed on the way
  (fences skipped; walled pockets read as traps). Zone 12 reinstalled;
  automated checks pass; path re-test handed to the user.
- 2026-10-02 — Entrance fix confirmed by the user in the editor; phase 6
  committed. Next: phase 7 (text → spec).
- 2026-10-02 — Phase 7 design agreed: Claude in the chat is the AI (no
  API); layout file in a closed vocabulary, compiled deterministically;
  density per area. "Beyond v1 — level design" added for the user's
  concern that every ROSE landscape type needs its own design knowledge.
- 2026-10-02 — Phase 7 built: layout vocabulary + compiler, area resolver,
  flood water, terrain features and tilt, cover areas, roads, start, intent
  checks, retail zone profiles, two new prefabs (adventurer_village,
  beach_hut). The user's five descriptions installed as zones 13, 30, 38,
  39, 40; all checks pass; in-game review handed to the user. "City" and
  "ruins" are not possible with the Junon grassland set.
- 2026-10-02 — Phase 7 reviewed and committed (`29d83240`). Phase 7b: the
  three review findings fixed, each measured against retail first (paint
  coherence, stilt villages over water, organic play-area shapes); the five
  maps reinstalled; all checks pass. In-game review handed to the user.
- 2026-10-02 — Phase 7b second round after the user's review: ramp lands on
  shore, one cliff material per stretch, low cliffs with boulder or fence
  rims (and fence-only edges), size by content. Next (user): bridges and
  pitfalls, from new descriptions.
- 2026-10-02 — Third round: the user chose to match retail's walk standard
  (traps inside the play area; borders judged by steepness against retail);
  El Verloon-style mountain ranges replace cliffs and rims; one cliff
  material per map. All five reinstalled and passing.
- 2026-10-02 — Fourth round: plateau tops, no props on faces, closed-ring
  border check, the client's NE-corner blind spot closed on faces. All
  five reinstalled and passing. Next: commit phase 7b (user), then bridges,
  pitfalls and walkable highground.
- 2026-10-02 — Fourth round confirmed by the user in the editor (bare faces,
  plateau tops read as highground); phase 7b committed. Next: bridges,
  pitfalls and walkable highground, from the user's descriptions.
