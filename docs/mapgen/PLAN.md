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
| LLM step | **Spec file for now.** The LLM writes or helps write the spec; the tool never calls an API | Keeps every phase reproducible from a file; the API step is phase 7 at the earliest. |
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
- **Status:** NOT STARTED.

### Phase 6 — village

Prefab building clusters lifted from retail zones, placed on flattened ground
and joined by paths.

- **Checks:** ground under each footprint is flat within a few cm; every
  building is reachable from the start.
- **Status:** NOT STARTED.

### Phase 7 — text input

Description → spec → zone.

- **Checks:** the same spec and seed give byte-identical files; five prompts
  pass every validator unattended.
- **Status:** NOT STARTED.

### Phase 8 — polish (optional)

Baked terrain lightmap, generated `.MOV`, minimap.

- **Check:** visual comparison with a retail zone.
- **Status:** NOT STARTED.

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
