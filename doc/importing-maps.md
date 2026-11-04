# Importing a map from another dump

The order of work for bringing a zone across from a reference client, and the traps
each source has. The map itself comes first (terrain, decorations, lightmaps, the walk
grid); monsters, NPCs, gates and quests are later stages of the same script. For the
monsters, follow [importing-monsters.md](importing-monsters.md).

Worked examples, newest first:

| Script | Source | What it shows |
|---|---|---|
| `scripts/import-china-rose.py` | China Rose | **Start here for a China Rose map.** One spec per map; HA (zone 158, Haian Coast, 2026-11-04) is the first. Scenery only, walk grid baked by the install. |
| `scripts/import-valley002.py` | tsuki (China Rose's series) | Shadow Canyon 151, Mysterious Cavern 156, Starlight Lounge 164: a `.lit` repair, bridge-joint stitches, a void-walker for object floors, stair fills, a two-way gate. |
| `scripts/import-cerberus.py` | tsuki | A lair whose boss the server spawns. |
| `scripts/import-ulverick.py` | RoseZA | A dungeon whose only whole copy is RoseZA's; the exit gate re-pointed. |
| `scripts/import-karkia.py`, `import-skaaj.py`, `import-shibuya.py` | Jrose | A planet (9 zones), an island town, a city: staged map / gates / monsters / NPCs. |
| `scripts/import-oro-667.py` | 667 (+ RoseZA AI) | A planet from a dump that ships no AI. |

`scripts/mapgen-zone.py` *makes* maps rather than importing them (docs/mapgen/PLAN.md).

## The rules of the house

- **The script is the record.** `data/` is not in git. One `import-<name>.py` does the
  whole import with `--dry-run`, `--verify` (re-derive everything) and `--selftest`
  (every reader/writer round-trips the source byte for byte before anything is
  written). Its docstring says what the map is and why every change was made.
- **Never write a parser.** Every format has a codec in `scripts/rosefmt/`; `import
  rosedata as oro` gives the tables, `copy_files`, `effect_chain`, `zon_tiles`
  (scripts/CLAUDE.md, "Data File Codecs").
- **Stages, map first.** Stage 1 is the scenery with every live lump held back
  (spawns, NPCs, gates). Each later stage is testable in game on its own.
- **A source's ids mean the source's tables.** A WARP record names the source's
  WARP.STB row, a REGEN or MOB record its LIST_NPC row, a MORPH record its
  LIST_MORPH_OBJECT row, the zone row its LIST_SKY row. Ours hold other things at
  those numbers. Every id is re-pointed, held back or dropped -- never copied.
- **Ours wins on a shared name.** `rosedata.copy_files` copies only files we lack, so a
  retail file another dump re-encoded stays ours. Look at the ones that differ and
  matter (below: China Rose's T008, its sky).
- **No `.MOV`.** They are retired (2026-10-30); the walk grid replaced them. A new map
  needs `scripts/nav-bake.py`.
- **Commit after the in-game test**, not before.

## 1. Survey the source

All read-only, in a scratch folder. What to measure:

- **The chunks.** Names `x_y` (slot `(x, 64 - y)`), the span, heights per chunk
  (`rosefmt.chunk.parse_him`), which chunks have a lightmap folder and which have no
  record at all. A ring of flat, unlit, empty chunks is filler for the far terrain.
- **Every lump of every `.IFO`** (`rosefmt.ifo.parse`): counts of OBJECT, CNST, MOB
  (NPCs), REGEN (spawns), WARP, EVENT_OBJECT, SOUND, EFFECT, MORPH, COLLISION (invisible
  walls), OCEAN, WATER. Print the warp, effect, event and NPC records in full. Note the
  ids every REGEN/MOB/WARP/MORPH record names.
- **The `.ZON`**: event points (`start`, `restore`, `WARP-*` landings), the tile
  textures, the economy lump (often editor junk; harmless).
- **The zone row**: compare the source's LIST_ZONE column layout with ours before
  copying anything. Note the sky row, music, minimap path, cols 9/10 (the minimap's
  start chunk), the object tables (cols 11/12).
- **The object tables and art**: does the deco ZSC round-trip, which of its files we
  already have, and which of those differ in bytes (`hashlib`), then whether the
  difference is the picture or only the encoding (decode both to PNG and compare at a
  small size).
- **Morph and sky rows**: compare the source's rows with ours cell for cell. An
  identical row is shared; a new one goes to a free row.
- **Lightmaps**: run `audit-lightmap-index.py` against the dump itself (load it with
  `importlib`, set its `mz.DATA` to the dump, call `main()` with `--zone <source row>`).
  The canyon's 34_33 needed a `.lit` repair; HA was clean.
- **HIM culling bounds**: `rosefmt.chunk.compute_him_bounds(h.heights)` against the
  stored bounds. Real bounds that disagree would make terrain vanish; ±FLT_MAX ones
  (tsuki's re-saves) only mean "never height-culled" and are harmless.
- **Walkable decks** (bridges, platforms, ramps): see section 4.
- **Other copies of the same map.** When two dumps carry it, diff them (heights,
  placements, lumps) and take the original. tsuki's series maps are editor re-saves
  of China Rose's.
- **Look at it**: the dump's own `Map Editor.exe --shots` (run with cwd = the dump),
  and its minimap picture.

## 2. Rows and folders

- **Zone row**: keep the source's number when it is free here (the China Rose series
  keeps tsuki's numbers, 147-164). `Stb.grow_to` if the table is shorter.
- **Folder**: a planet folder of ours (`3DDATA\Maps\<PLANET>\<NAME>`); rewrite the
  zone row's cols 1 and 8 to it. The `.lit` paths are relative to the chunk; grep the
  map folder's bytes for its own path before moving it (HA's name none).
- **Name**: LIST_ZONE col 0 and a LIST_ZONE_S.STL key (col 26); the minimap title
  reads the STL, never col 0.
- **Music** (cols 5/6): the user's call. Tracks live loose in
  `ROSEProject\sound\bgm\`, not in `data/`.
- **Sky / morph rows**: share identical rows, put new ones in free rows (ours: sky
  11-13 and morph 47-50 are the imported ones; sky 14-16 and morph 51-64 are free).

## 3. The map folder

- `HIM`, `TIL`, `.lit` and the lightmap DDS verbatim (unless a repair is needed).
- **Every `.IFO` through `rosefmt.ifo`**: hold back spawn/NPC lumps (`recs.clear()`
  keeps the lump with count 0, so a later stage refills it), drop dead records, remap
  MORPH ids. **Fail on anything unexpected**: a record in a lump the spec does not
  name stops the install, so a changed source cannot slip through.
- **Never delete an OBJECT or CNST record** to drop it: `.lit` entries are keyed by
  ordinal. Records in other lumps take no lightmap entry and can go.
- `.ZON` verbatim, or with event points moved (a gate's arrival point).
- **The minimap**: our client draws 64 px a chunk with a one-chunk border before the
  chunk in cols 9/10 (`cminimapdlg.cpp`). A picture at another scale is resampled;
  labels in another language are erased at full size and redrawn in English at the
  final size (`minimap_bytes` in either importer).
- No `.MOV`.

## 4. Repairs a map can need

Each was found on a real import; check for the condition, apply only when it is there.

- **A broken `.lit` index** (an object deleted after the bake): renumber the one file
  (`import-valley002.py` `lit_fixes`). Never relight a whole map for it.
- **Open joints in a walkable deck**: rasterise the deck's top faces and walk along it
  looking for cross-sections that are open in the middle. **Measure at world scale**:
  HA's bridges are placed at 10-17x, so a 2 cm grid in model space is 20-35 cm in the
  world and sees nothing. Joints of 3-20 cm (the canyon's rope bridges) need a stitch
  part; hairlines under ~2 cm are inside the avatar's 15 cm foot sphere. **A stitch
  part must be a child of the root part** (scripts/CLAUDE.md, "Lessons From Zone
  Imports").
- **Floors that are objects over flat hidden terrain** (a cave): walk them from the
  arrival point with `import-valley002.py`'s `Walker` and close every void step
  (`void_fill`).
- **Stairs the body sphere stops on**: `stair_fill` (the Starlight Lounge).
- **Coplanar placements** (a wall that flashes while the camera moves):
  `fix-coplanar-object-overlaps.py --zone N`.
- **Objects the map names that our table leaves empty**: `fill-zsc-gaps.py`.
- **A tree you walk through**: look for a model whose trunk part has collision 0 while a
  small part carries the collision (HA's object 28; its fixed twin was object 81).
  Scan every placed part that stands on the ground, is tall and has collision 0; leave
  undergrowth walk-through. `import-china-rose.py` `collision_fixes`.
- **A pale outline round foliage as the camera moves**: mis-sorted overlapping alpha
  parts (src/engine/CLAUDE.md, Transparency Antialiasing). The engine's two-pass alpha
  parts (`[VIDEO] FOLIAGE_TWO_PASS`, `/alphasplit`) remove it; a texture with a very
  soft alpha gradient can also be made a hard cut-out, coverage kept per mip level
  (`import-china-rose.py` `cutout_textures`).

## 5. Art and tables

Copy the decoration and building tables and everything they name (meshes, textures,
`.eft` and their particle chain via `oro.effect_chain`), the tile textures, the files
of new morph and sky rows. Then the morph/sky rows, the zone row and its name.

## 6. The walk grid

`python scripts/nav-bake.py <zone>` (~20 s for 64 chunks) writes `<FOLDER>.lnav` beside
the map, `build/nav/<FOLDER>.png` (1 px = 1 m, legend in `mapgen/navbake.py`
`preview`) and `build/nav/<FOLDER>.txt`. Read the report:

- **`seeds: N, failed to snap: 0`**: seeds are the ZON's event points plus regen
  points and NPCs. A failed seed is an arrival point or spawn standing nowhere.
- **Reached area**: everything a seed links to. Grey (unreached) should be cliffs, the
  outside of the map and filler. A walkable place that shows grey is cut off.
- Hazards (a monster drawn 3 m+ higher than its floor) and floors on objects (the
  decks).

Then look at the doubtful spots in 3D: `python scripts/nav-view.py <zone> <x> <y>
--radius 30 --walkable --png` (world metres, the minimap's numbers). A map that needs
its own rule (a step height, a sliver rule) gets an entry in `OBJ_STEP_BY_ZONE` /
`SLIVER_CM_BY_ZONE`, never a global change (the user's rule). The grid records a CRC
of every input; the server warns when one changed, so **re-bake after any map edit,
and again when the spawn stage adds seeds**.

## 7. Checks after the install

```
python scripts/add-dds-mipmaps.py --dry-run --subdir <each new art folder>    then without --dry-run
python scripts/add-dds-mipmaps.py --subdir <map folder> --lightmaps
python scripts/import-<name>.py --verify
python scripts/audit-lightmap-index.py --zone <zone>
python scripts/fix-coplanar-object-overlaps.py --zone <zone> --dry-run
python scripts/audit-spawned-npcs.py --zone <zone>
python scripts/mapgen-roundtrip.py --selftest
```

Lightmaps never ship a mip chain and imported art often lacks one; a chainless
texture costs 0.2-11 ms to create on every load (`add-dds-mipmaps.py`'s docstring). The importer's
`--verify` should re-check the grid's freshness, the lightmap audit and the absence of
`.bak` files under `data/` (`pack.ps1` refuses to bake with one there).

## 8. Deploy and the first test

Bake (`scripts/pack.ps1`), restart the servers (they read LIST_ZONE, the maps' event
points and the grid at startup), go in with `/mm <zone> <x/10> <y/10>`. Look at:

- the minimap: the arrow on the spot you stand on, the labels;
- the edges: what the horizon shows from the high points (the client draws a 5x5
  window of chunks);
- every walkable deck and bridge, end to end;
- arrival height (`DropFromSky` puts you on the highest surface, a roof included);
- the morph objects (waterfalls) and effects play;
- frame rate on the densest view.

## 9. The later stages

- **Monsters**: [importing-monsters.md](importing-monsters.md). Stats are ours. Put
  the regen points back with our rows, then run `audit-spawned-npcs.py` and re-bake the
  grid (the points become seeds).
- **NPCs**: a placement's `.CON` name needs its extension; a placed NPC spawns only if
  its row's NPC_TYPE is 999 (scripts/CLAUDE.md).
- **Gates**: a WARP row of ours per direction; **a gate must never name a blank WARP
  row** (`Recv_cli_TELEPORT_REQ` calls it hacking); the trigger within a few metres of
  the ground; arrival points 10-15 m clear of the trigger (`import-valley002.py`,
  "The gate").

---

## China Rose (梦幻骑士)

`C:\Users\Thomas\Desktop\china rose`: a dead 2009 Chinese ROSE fork, sent by a third
party on 2026-11-02. `extracted\` holds every VFS file unpacked (paths decoded as GBK);
`installed\app` the installer's tree. **Never run `fkOnline.exe`**: its launcher
auto-updates from a dead domain anyone could register.

It is harder than the other dumps because its world is entirely its own: its tables
grew on their own from early on, so a row number rarely means the same thing in both
(the morph, sky, warp and NPC rows HA names), its text is Chinese, its minimap is at
another scale, and its maps carry live spawn and warp lumps that point into those
tables.
**What makes it workable is its client source**, a near-twin of ours: look up a column
or a constant there instead of guessing.

### The source code as a reference

`Client\Client\` (Korean comments in CP949, Chinese in GBK: `iconv -f cp949` / `-f gbk`
to read them). What it settled for HA:

- `Common\IO_STB.h:597-653`, the `ZONE_*` macros: LIST_ZONE cols 0-35 mean what ours
  mean. 36 is a world-map index, 37/38 a PK NPC; 39-41 (a navmesh flag, a scale, a
  navmesh path) are read by no file in this snapshot. Copy 0-35, drop the rest.
- `Interface\DLGs\CMinimapDLG.cpp`: `MINIMAP_RESOLUTION_PER_MAP 128`, and the same
  border and cols 9/10 anchoring as ours. So a China Rose minimap is exactly ours at
  twice the size.
- `IO_Terrain.cpp`: the map loader is ours (lumps, lightmaps, effects). Its
  `CMAP::AddEffect` applies a placement's rotation, ours did not (ported, 5f4abc01).
- `GameProc\BGMManager.cpp`: music is LIST_ZONE cols 5/6, nothing else. An empty cell
  is silence.

There is **no server source** and no engine source (headers and libs only): server
behaviour (spawns, warps) is inferred.

### Its data

- STB text is GBK, but every path is ASCII, so `Stb(path)` (latin-1) copies cells as
  bytes safely; read names with `encoding="gbk"` to look at them.
- STLs are UTF-8 with five languages; the series' zone keys (`LZON027` ...) carry no
  text in any of them.
- Table shapes: LIST_ZONE 42 columns (ours 36), LIST_MORPH_OBJECT 13 and LIST_SKY 22
  (same as ours). LIST_SKILL differs (statuses at 88/92/96, cols 100-113 new): matters
  for the monster stage. Weapons come in per-sex ZSCs. `LIST_DECO_TOWN.ZSC` (new_town2's)
  does not parse in `rosefmt.zsc` yet.
- `SOUND\BGM\` ships the series' music, including the originals of the maps we took
  from tsuki: `BGM_AYXG.ogg` (Shadow Canyon) and `BGM_AYDX.ogg` (the Shadow Cave, our
  Mysterious Cavern). The late maps (HA, YJ, Yulin, the volcano maps) have none.

### The series

**A warp point's M-number is a China Rose LIST_ZONE row**: `WARP-M027-M028` is the
landing in zone 27 for travellers from zone 28. Checked on every link we have:

| CR row | Folder | Name (CN) | Ours |
|---|---|---|---|
| 1 | new_town2 | 梦幻城堡 Dream Castle | -- |
| 12 | Snowfield001 | 镜花雪原 Mirror-Flower Snowfield | -- ("Mirage Snowfield" on two minimaps) |
| 14 | Valley002 | 暗影峡谷 Shadow Canyon | 151 (via tsuki) |
| 20 | ID09 | 暗影洞穴1层 Shadow Cave 1F | 156 Mysterious Cavern (via tsuki) |
| 22 | FZCY | 纷争草原 Plains of Strife | -- |
| 27 | HA | 海岸场景 coast scene | 158 Haian Coast |
| 28 | YJ | 遗迹场景 ruins scene (遗忘之都 on HA's minimap) | -- (HA's east gate) |
| 29 | Yulin | 雨林 Rainforest | -- (tsuki 160) |
| 40 | bar | 酒吧 the PK bar | 164 Starlight Lounge (via tsuki) |

Also in it: M002, M003, PK011, ID01-ID10 (the blue ice caves ID06-08), Hsfb01, RD, Hs
(volcano), ZP030_FW (12 small rooms), Z1, title. Editor renders of all of them:
`china rose\shots\`.

### China Rose against tsuki

tsuki carries series maps in its rows 147-164, as **editor re-saves**. HA's: heights
and placements agree with China Rose's to 0.02 cm and the lightmaps byte for byte,
but tsuki's copy lost the filler chunks, the spawn points and the gate, and carries
±FLT_MAX HIM bounds (so do our canyon, cave and lounge, taken from tsuki). The canyon
has 84 chunks in China Rose to tsuki's 36. China Rose's are the originals: take a
series map from it, keep tsuki's zone number and folder.

### The traps, as met on HA

1. **Filler chunks.** HA ships 64 chunks for a 42-chunk map: a ring of flat ground
   with no object and no lightmap (Valley002 has 84 for tsuki's 36). The client lights
   such a chunk with `3DDATA\TERRAIN\default_light.dds`. Kept on HA: it is the
   original's horizon, and its north corners carry the cliffs on.
2. **The minimap is twice our scale** and labelled in Chinese: halve, repaint (above).
   Check its size against cols 9/10, not against the chunk span: HA's picture covers
   x 28-35 / y 28-35 while its map spans y 28-34 (the top row sits in the border, as in
   China Rose).
3. **Warp records point at China Rose's WARP.STB.** HA's east gate names row 38,
   "M027 -> M028" to YJ; **our row 38 is Anima Lake -> Junon Polis**. Kept as shipped,
   the gate would have teleported players there. Drop the trigger (and its portal
   effect) until the far map exists; keep the ZON's `WARP-*` points for that day.
   The tsuki copies' invisible warp boxes (EVENT_OBJECT, triggers like `42-e-1`) fire
   QSD triggers that exist nowhere: dead too.
4. **Spawn points name China Rose's LIST_NPC** (HA: rows 130-139, 285 points). Hold
   them back for the monster stage.
5. **Morph and sky rows collide.** China Rose's morph 42/43 are waterfalls, ours cart
   abilities. Compare cells; HA shares 1-3 and 47, adds 50. Its sky row 0 is our 13 cell
   for cell -- and the two `MAPOBJ\Sky\day01.dds` differ in bytes only outside the dome
   cap's octagon, which the sky mesh never samples (decode both and look before
   deciding a sky row is new).
6. **Same name, different file.** HA's `_warp_gate03.eft`, a dozen particle textures,
   the water ripple textures, tile T008, the sky pictures: China Rose's differ from
   ours. Ours stay (copy never overwrites); check the ones the map shows. T008 is a
   brighter version of a green tile every Junon field uses, so it could not be
   replaced in place -- and no HA tile used it.
7. **Mip chains**: none on the lightmaps, and some art without one. HA: all 126
   lightmaps and 34 of its ~270 new textures given chains after the copy.
8. **No music** for the late maps; pick a placeholder and ask.

### Adding the next China Rose map

Add a spec to `MAPS` in `scripts/import-china-rose.py`:

| Field | Meaning |
|---|---|
| `key`, `src_zone`, `zone` | the `--map` name, China Rose's row, ours |
| `name`, `stl_key` | our name and its LIST_ZONE_S key (`LZON<zone>`) |
| `src_dir`, `dst_dir`, `zon` | China Rose's map folder, ours (a planet folder), the ZON's name |
| `deco`, `cnst` | the zone row's cols 11/12 (the building table must already be ours, byte for byte, or the install stops) |
| `music` | cols 5/6 |
| `morph_rows`, `sky_rows` | China Rose row -> ours, for every row the map uses (identical rows map to ours, new ones to free rows) |
| `held_lumps` | lumps kept for a later stage (`ifo.REGEN`; `ifo.MOB` for a map with NPCs) |
| `dead` | per chunk, the records to drop and what they must be (`{ifo.WARP: [warp ids], ifo.EFFECT: [eft names]}`) |
| `labels` | the minimap's label boxes on China Rose's picture and their English |
| `gm_way_in` | printed for the test |

Survey first (section 1); the install refuses anything the spec does not account
for. A map with buildings (new_town2), walkable object floors over hidden terrain (the
ID caves) or open-jointed decks needs the repairs of section 4 added to the script --
`import-valley002.py` has them, written for tsuki's copies of the same series.

## The other dumps, briefly

Their tables are the retail lineage's, so far fewer ids collide; their traps are in
scripts/CLAUDE.md "Lessons From Zone Imports" and the importers' docstrings, and the
zone histories in `doc/zones/`:

- **Jrose** (Karkia, Skaaj, Shibuya): ships its AI (`.aip`), NPCs and dialogs; its
  dialog check functions are rewritten, never trusted; a placement's `.CON` name lacks
  its extension; its warp gates are re-pointed at our WARP rows.
- **667** (Oro): ships no AI and no readable dialogs (RoseZA supplies them by file
  name); its `NPC_TYPE` codes are composite.
- **RoseZA** (Ulverick): instance bookkeeping in the AI is cut; a gate placed at a
  RoseZA event box's depth ended up 15 m under the desert, so check a gate's height.
- **tsuki** (Cerberus Lair; the China Rose series' copies): no AI, re-saved maps.
