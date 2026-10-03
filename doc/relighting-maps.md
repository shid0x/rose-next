# Relighting a map

`scripts/relight-zone.py` bakes the lighting of a map you built or edited in
the map editor: the **ground lightmaps** (sunlight, shadows of hills and
objects, darkness under trees, water colour) and the **object lightmaps**
(each house, rock and tree shaded on its sunny and shaded sides, darker
underneath and in corners). It uses the same bakers as the map generator,
tuned against retail Junon, so a relit map matches the look of the
original game by default — and every part of that look can be changed.

## The short version

```powershell
python scripts/relight-zone.py show  --zone 29                  # what the bake will see
python scripts/relight-zone.py shots --zone 29 --label before   # editor screenshots, for comparison
python scripts/relight-zone.py bake  --zone 29                  # bake with the default look
python scripts/relight-zone.py shots --zone 29 --label after
```

Then re-bake the VFS (`scripts/pack.ps1`) and look in game. The servers do
not read lightmaps; no restart needed.

`--zone` takes a zone number (`LIST_ZONE` row) or the map folder name
(`JD04`). `python scripts/relight-zone.py list` shows every map.

Screenshots land in `build/relight/<FOLDER>/shots/<label>/`, with a
contact sheet `sheet.png`. By default they look at the six busiest parts of
the map; `--at 5120,5070` (repeatable) looks at a spot, in the world metres
the in-game minimap prints. The editor opens a window while it renders,
so this needs a desktop session.

## Changing the look

Every setting, its default and what it does:

```powershell
python scripts/relight-zone.py settings
```

Try values with `--set` (repeat it for several):

```powershell
python scripts/relight-zone.py bake --zone 29 --set sun_el=60 --set shadow_ratio=0.8
```

When you like the result, add `--save`: the settings that differ from the
defaults are written to `scripts/mapgen/relight/<FOLDER>.json`, and every
later bake of that map (yours or a teammate's) uses them. That file is the
record of how the map is lit — `data/` is not in git — so commit it.

A settings file of your own works too: `--settings my-evening.json`, with
`{"settings": {"sun_el": 25, "brightness": 0.9}}`. Order of precedence:
defaults, then the map's saved file, then `--settings`, then each `--set`.

### The settings that matter most

| Setting | Default | What it does |
|---|---|---|
| `brightness` | 1.0 | Overall brightness. 1.15 = 15% brighter. |
| `open_rgb` | 161,156,151 | Colour of sunlit ground; sets the tint of the whole map. 128,128,128 = textures as painted. |
| `sun_az` | 62 | Compass direction the sun shines **from** (0 north, 90 east). Every Junon map uses about 62; players read shadows that way. |
| `sun_el` | 45 | Sun height. Lower = longer shadows; higher = shorter, a brighter town. |
| `shadow_ratio` | 0.70 | How light full shadow is (share of sunlit ground). 0.8 = softer, 0.55 = deep. |
| `solid_shadow` | 0.8 | How much sun a rock or house blocks. |
| `leaf_shadow` | 0.6 | How much light one layer of leaves blocks. |
| `canopy_strength` | 0.45 | Extra darkness of the ground under trees. |
| `sky_occlusion` | 0.5 | How dark undersides, corners and the feet of objects get. |
| `object_gain` / `object_sun` | 0.8 / 1.2 | Brightness of objects in shade / on their sunny side. |
| `terrain` / `objects` | true | Rebake the ground / the objects. `false` keeps the map's current ones. |

### Recipes

- **A brighter town with short shadows** (tall buildings, narrow streets):
  `--set sun_el=65 --set shadow_ratio=0.8`.
- **Late afternoon**: `--set sun_el=28 --set open_rgb=170,150,125`.
- **Softer, friendlier shadows everywhere**: `--set shadow_ratio=0.8 --set solid_shadow=0.6`.
- **A cave or dungeon with no sun**: `--set shadow_ratio=1 --set brightness=0.8`.
  With no sun term, only the darkening in corners and under objects remains.
- **Only fix the objects** after editing a map whose ground lighting you
  want to keep (retail maps were hand-baked): `--set terrain=false`.

## Undo

```powershell
python scripts/relight-zone.py restore --zone 29
```

puts back the map's lightmaps from **before its first bake**, however many
times it was relit (the originals are kept in `build/relight/<FOLDER>/`).
`verify` checks that nothing changed the files since the last bake and that
they pass the safety checks.

`restore` refuses if the map's objects were edited since the first bake:
the original lightmaps are numbered for the old objects and could crash the
client. Bake again instead (`--force` restores anyway). A bake that stops
part-way (an error, Ctrl+C) puts back every file it had touched.

## Things to know

- **Relight after every edit in the map editor.** The editor numbers
  object lightmaps by the objects' order in the chunk and never renumbers
  them, so deleting or inserting an object makes every later object show
  its neighbour's lighting — or crash the client when the neighbour has
  fewer parts. A fresh bake rebuilds the numbering.
- **Every bake is checked before it is kept**: anything the client could
  crash on rolls the whole run back. `scripts/audit-lightmap-index.py`
  runs the same checks over every map in the game.
- **Grass and flowers are never shaded.** They get one open-air colour,
  under trees too, as in retail. The bake finds them through the editor's
  object list (the GRASS folder), or for imported maps without one, a
  GRASS folder in the model paths; `--set plants=12,40` /
  `--set not_plants=7` correct it per object id.
- **Glowing parts are left alone**: additive parts (light cones, neon) get
  no lightmap and cast no shadow, as in retail. Invisible parts (collision
  shells, walls with a transparent texture) cast no shadow either.
- **Cell sizes follow the map's own lightmaps**: each model keeps the
  lightmap resolution its original bake gave it (read from the old files,
  or from the atlas size for imported maps that do not record it).
- **Objects whose model has no second UV set cannot take an object
  lightmap** and stay vertex-lit; `show` lists them. They look a little
  different from their lit neighbours (brighter in shade, no night dimming).
- **What lightmaps change in game**: lit objects darken at night with the
  ground, and they fade out at the fog distance instead of later. At the
  lowest graphics preset lightmaps are off and lit objects draw flat.
- **Not supported**: point lights (torches, lamps, glowing crystals),
  coloured local lights, per-object overrides of the lighting, baking part
  of a map. Generated maps (`MAPGEN*`) are refused: their lighting lives
  in the layout's `"lighting"` section (`scripts/mapgen-zone.py`).
- A bake takes 1-8 minutes depending on the map (Shibuya, 49 chunks and
  2,200 objects: 7.5 minutes).
