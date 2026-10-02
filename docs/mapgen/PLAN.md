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
- **Status:** NOT STARTED.

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
- **Status:** NOT STARTED.

### Phase 3 — tile painting

Autotile painting with the JG tileset: slope → cliff, low → shore, paths.

- **Checks:** first regenerate an existing JG zone's TIL from its own corner
  brushes and match >= 99%; on generated maps every tile id exists in the ZON
  tile table, neighbours agree on shared corners, only legal brush pairs (the
  tileset's chain table).
- **Status:** NOT STARTED.

### Phase 4 — water

A lake at a set level with shore tiles.

- **Checks:** same waterline in editor and client; the plane does not show
  outside the basin.
- **Status:** NOT STARTED.

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
