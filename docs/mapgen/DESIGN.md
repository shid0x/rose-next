# Map generator — level-design guide

What makes a generated map read like ROSE, collected from measurements of
the retail maps and from the user's review of generated ones. Claude reads
this before writing a layout file. Each entry says what was seen, the
evidence, and what the generator does about it (or "open").

## Map shape

- **Most of a ROSE map is not walkable** (user, 2026-10-02: "our maps are
  all square; ROSE bends that around and a lot could be blocked").
  Measured (phase 7b): only 24-55% of a Junon map is walkable dry land; the
  rest is cliffs, highlands and sea, reaching in from the edges unevenly
  (median 8-50% of the map's side, varying a lot along the edge). Chunk
  sets themselves are full rectangles (3 of 57 retail maps miss a chunk).
  Phase 7 maps were 62-90% walkable behind an even 36 m ring.
  **Done:** layouts get an organic play area by default (`shape`: organic,
  basin, winding, valley north-south / east-west; `square` = the old ring;
  `play`: small / medium / large = 30 / 45 / 60%). Outside it: an 80 m
  cliff, then highlands. Compass words refer to the play area.
- **A coast is an open edge.** The play area reaches that edge and the sea
  fills it; cliffs run down to the water on either side.
- **Cliffs don't need to be high, and maps don't need to be big** (user,
  2026-10-02, phase 7b review). The 80 m walls were chosen for safety, not
  looks. A map without mountains everywhere can simply be smaller.
  **Done:** `cliffs`: low (25 m, default) / medium (45 m) / high (80 m) /
  none; a map with one village, one lake and no mountains defaults to 3x3.
- **ROSE limits terrain with more than cliffs: bridges, pitfalls, fences**
  (user, 2026-10-02). **Fences done:** `rim`: rocks (default on low and
  medium cliffs) or fence along the cliff top, or a fence line alone
  (`cliffs: none`). A low cliff needs its rim to seal (FORMATS.md, slope
  rule). Bridges, pitfalls and walkable highground: done in phase 7c, see
  "Levels" below.
- **No boulder rims; fences belong to villages and cities** (user,
  2026-10-02, second phase 7b review). Mountains need no rim: "mountains
  cannot be climbed after a certain slope anyways".
- **Natural borders are mountains, not walls: low, but unclimbable** (user,
  2026-10-02, with El Verloon Desert as the example). Its border is a
  range of irregular peaks and ridges, not high, steep enough that nobody
  climbs it, and "more pleasing to player eyes" than our uniform cliff band
  with a plateau behind it. Measured outside the walk region of eight
  retail zones: 79-92% of cells 5-10 m out are too steep to climb, 41-64%
  at 10-20 m, and the ground stands 18-30 m above the edge there (Gorge of
  Silence 40 m). **Done:** the default border is a mountain range, a 30 m
  face over 10 m and a body of sharp ridges (`border`: mountains, height
  medium or high; `fence` for villages and cities). Checked against those
  numbers on every build. Lower ranges did not hold (tried 19-28 m faces):
  ours stand ~37-40 m 10-20 m out, as tall as Gorge of Silence.
- **Range tops are highground, not wavy terrain** (user, 2026-10-02,
  fourth review: "the ranges look natural, steep, you can't climb that";
  the ridged top was unneeded). **Done:** a plateau top with 4 m of gentle
  variation (`top`: plateau, default; ridges still available). Walkable
  highground inside the play area: ledges (phase 7c, "Levels").
- **No grass or shrubs on cliff faces** (user, 2026-10-02). **Done:** only
  rocks on slopes over 45°.
- **The walk check was stricter than the game** (user's choice 2026-10-02:
  "match retail"). Under the cell model, retail maps are 87-100% reachable
  with up to 24% "trap" cells (Gorge of Silence), while players cannot
  climb them in game. Traps are now looked for inside the play area only,
  and the border is judged by steepness against retail.

## Levels: ravines, pits, ledges (phase 7c)

The user's three descriptions of 2026-10-02 (bridges, pitfalls, walkable
highground). One rule set covers all three, read from the client first
(FORMATS.md, slope rule): a player can always walk *down* a slope, never
up one over 54°.

- **A level the player may fall into needs a way out, or it is a trap.**
  Pits and ravines each get a gully: a trench at 26°, 7 m wide (two whole
  cells), level across the floor and rising from its edge. Its side
  decides which way the player comes out: the ravine's gullies climb to
  the west bank only, so the east bank is reached by the bridges alone.
- **A level the player may not leave by dropping needs an invisible
  wall.** A ledge edge is a cliff the player could step off; retail closes
  such edges with collision boxes (IFO lump 11, 2,889 of them in retail),
  and so does `levels.wall_line`, open only at the ramps. "Players on the
  ledges can see the road below" is why it is a wall, not a fence or
  rocks.
- **Walls not to be climbed are sealed** (`seal`), the NE-corner blind spot
  included, and checked from the low ground: pit walls, the ravine's east
  wall, the ledge cliffs.
- **Bridges are retail objects, sized to retail.** `bridge` (wooden,
  `field-bridge02`, scaled 1.5-2.2) on the main road, `footbridge` (rope,
  `guroomdari`, 0.7-1.1) for a narrow crossing. The ravine narrows under
  each to what the deck spans at retail's largest scale (~29 m and ~43 m),
  and both banks are levelled to the deck ends. A road `via` a bridge is
  painted to each end of the deck.
- **Pits sit beside a road that is laid first**, alternating sides, ~7 m
  off it, with the gully leading away from the road. "Partly hidden":
  overgrown cover on the rim (`rim of pit1`). Nothing tall on faces, so a
  pit's own walls stay bare and readable.
- **Ledges come from a valley shape**: the floor is the play area, cliffs
  (12 m, two cells wide) rise from its rim to ledges 20-38 m wide, then the
  mountains. One ramp at each end of each ledge, along the cliff foot,
  where the valley proper starts (not in the exit corridors). A landmark
  (`lookout tower`, Junon's unused `guardpost01`) can stand on a ledge,
  and a path can run along it from ramp to landmark to ramp.
- **Exits:** `exits: [south, north]` opens a corridor through the border to
  the middle of each edge (a road leaves the map there; warps are not
  generated). Roads may start or end at `exit:<edge>`.
- **Open:** in-game confirmation of the invisible walls and the bridge
  decks; ravines along other axes than north-south with bends; pits
  without a road; tunnels (the description's "tunnel-like" gully is an
  open-topped trench: the terrain is a heightfield).

## Lighting: what retail lightmaps look like (phase 8 survey)

Measured over JG01-JG08 (`scripts/mapgen-lightmap-survey.py`; pictures
in `build/mapgen/lightmap-survey/`).

- **A retail plane lightmap is a top-down render of the whole lit scene,**
  not a terrain-only bake:
  - water is in it (the river bed is tinted blue);
  - trees show as their green canopy over dappled shadow;
  - the ground under a rock is black;
  - buildings and posts cast block shadows.

  Terrain relief shading is faint by comparison. Most of the contrast comes
  from objects and water.
- **Open ground is baked bright:** luminance 157-190, i.e. 1.23-1.49x the
  tile in game (JG08: 1.10). Generated maps currently get ~1.0x from the
  fallback, so they render 20-35% darker than retail ground as well as
  flat.
- **One sun, from the north-east, in every zone:** light from bearing
  54-66° (east-north-east), shadows falling to the south-west.
  - Elevation is between ~30° (tree shadow lengths, terrain cast-shadow
    match) and ~45° (slope-brightness fit).
  - Step 2 settles it by casting shadows from real object geometry and
    matching retail.
- **Slopes:** luminance = 118-137 + 40-53 x (n.l) where the fit holds
  (JG01, JG02). So the full swing from a slope facing away to one facing
  the sun is only ~25%.
- **Shadows are soft and partial:**
  - Terrain shadow: 0.5-0.8 of open ground.
  - Tree, averaged over 266 JG02 trees: 0.5-0.6 at the trunk in every
    direction (contact darkening, gone by ~5 m), and a cast shadow toward
    243° still ~0.85 at 18 m.
  - Rocks: 0.15 over their footprint.
- **Water:** blue, roughly (40-100, 70-115, 120-190).
- **Each zone has its own tint and era:** grey-white (JG01, JG02), green
  (JG03), cream with heavy brown darkening round rocks (JG07), bluish
  (JG08). A generated map should pick one tint for the whole map.
- **Retail chunks were baked separately:** edges disagree by 1.5-7
  luminance steps, a faint seam. A bake over the whole map at once has
  none.
- **Retail objects are lightmapped too, and darker than today's
  vertex-lit ones:** covered texels median 83-112 by zone (128 = the
  texture as drawn).
  - Faces in sun ~133, turned away ~70, in shadow ~45, undersides ~32;
    the feet of objects and their nooks go near black (retail's baker had
    sky occlusion).
  - Leaves barely follow their normal (median 97-122).
  - Grass and flowers are near-flat.

- **What the generator bakes (phase 8 step 2):** the retail Junon look by
  default.
  - Sun from 62° at 45°.
  - Open ground x1.23 (luminance ~157), full shadow 0.70 of it.
  - Soft object shadows from the real geometry; leaves dapple.
  - Darkness under and round rocks and trunks, leaf colour under canopies,
    blue under water.
  - A layout can change any setting under `"lighting"` (for instance a
    greener `open_rgb` for a JG03-like zone, or a lower sun). Keep the sun
    in the north-east unless the map has a reason: every Junon map shares
    it, and players read shadows that way.
- **What it bakes on objects (phase 8 step 3):** the same sun and open
  light, objects at x0.8 of it, plus sky occlusion at half strength.
  - Faces in sun ~124, turned away ~58, in shadow ~56, undersides ~44
    (JG02 window; retail 130 / 58 / 32 / 36): retail's shape, its shadows
    lighter.
  - Leaves flat-ish, a little darker inside a canopy.
  - **Grass and flowers are never shaded** (user, in game, 2026-10-03):
    one open-air value (~133, about as today) under trees and in the open
    alike. Retail leaves grass unshaded under trees, and per-clump shading
    read as random dark patches.
  - Every object is lit or none is (`"objects": false`). A lit object
    beside an unlit one shows two shading models: the unlit one keeps a
    0.86 ambient and no night.
- **Shadows should read soft, never as black shapes** (user, in game,
  2026-10-03: "very dark, strange").
  - The ground under a solid object is hidden by it, so darkness there
    costs nothing.
  - Darkness beyond an object's outline is what players see: keep it
    short and light, and never let a footprint span empty ground (a hull
    over several trunks, a bridge deck).
  - Err lighter than retail's averages. Dark ground beside a bright
    object looks pasted on; since step 3 our objects darken in shade too,
    and their shadows also stay lighter than retail's.
  - A solid object casts one shadow, not one per surface, and only what
    really touches the ground darkens it: players see under a raised cart
    (user, 2026-10-03, farm carts).

## Cliff and mountain material

- **Cliffs take the map's theme, not the ground at their foot** (user,
  2026-10-02): a rock cliff beside a green meadow looks strange; El
  Verloon's mountains are all its desert rock. One material for the whole
  map's mountains, from its theme (green for Junon grassland), unless a
  layout names another. **Done:** `cliff_material` grass (default) / rock /
  earth for the whole map; the per-stretch choice from the cover is still
  available as `auto`.

## What makes a map memorable

- **Map 5 (the valley) is the most solid of the phase 7 maps** (user,
  2026-10-02): a route hierarchy (a road between settlements, a path
  branching off to the hut), memorable landmarks (the rocky ground with
  the standing stones), distinct combat spaces (forest, rocks, open
  meadow). It is also the most detailed description: specific places,
  relations and routes. Short descriptions should be filled out along
  those lines, not left to defaults.
- **Design to the four goals, and state them as checks** (phase 7d). A
  layout written "at best" names, for each map:
  - **Route hierarchy:** one main road between the start and the goal
    (`style: road`), a secondary route that is longer, narrower or higher
    (`path`, a footbridge, a ledge path), and ground with no route at all.
    A level forces the hierarchy: a ravine admits roads only at bridges,
    ledges only at ramps.
  - **Landmarks:** something tall or singular at each decision point: a
    village on a hill, a bridge, a tower on a ledge, a forest, the ravine.
  - **Sightline control** (`sightlines`, checked): show the goal from the
    start to pull the player (Riverwatch's village across the ravine);
    hide the reward behind a forest or a rise (its outpost, Ledgewood's
    east end); give the high ground the view (Ledgewood's tower sees both
    settlements over the forest). A forest hides only if the line really
    crosses it: put it *between*, not near; three canopies make a wall.
  - **Combat spaces:** contrasting covers along the routes: open meadow
    (exposed), boulder fields (broken cover), dense forest on a path (close
    quarters), pits with tall grass at the rim (a hazard), an open ledge
    against a wooded one.
  Worked examples: `layouts/p7d-1-riverwatch.json`, `p7d-2-ledgewood.json`
  (their `notes` say which element serves which goal).
- **Look at the map, not only the numbers** (phase 7d). After installing,
  run `mapgen-zone.py shots SPEC` and read the contact sheet and the views:
  the checks passed Ledgewood while two of its pits were invisible. Judge
  each view by the description: is the landmark legible, does the hidden
  thing stay hidden, does a level read as a level.
- **A level must read in its paint** (phase 7d). Terrain gets little
  shading, so a pit painted like the meadow around it looks flat even 8 m
  deep. Pits and gullies are bare earth; ravine walls already show sand and
  soil from the river. Keep any new level's faces in a contrasting brush.

## Ground paint

- **Retail paints in large, coherent patches, especially on slopes.**
  Neighbouring corners agree 75.5% (all ground), 83.6% (>= 45°), 88.8%
  (>= 60°). The user saw "weird tiles used seemingly at random on
  mountains" (2026-10-02) when ours were at 65-72% with 24-35% soil on
  cliffs (retail 11-12%). The brush mix was right; lone speckled corners
  each grew a ring of in-between soil. **Done:** despeckle, longer patches,
  a cliff palette (bright grass / dark grass / rock) above 40°; 74.5 / 80.1
  / 83.8% now, checked on every build.
- **Still not coherent enough on cliffs** (user, 2026-10-02, phase 7b
  review): faces still show a pattern of grass and earth stripes. A cliff
  should read as one material along its run, green in green country, earth
  or rock where the surroundings call for it. Retail Junon varies from sand
  to greenery and from forest to dirt; using only the green grassland set
  makes that harder. **Done:** one material per stretch of cliff (grass,
  rock or earth), taken from the cover within ~40 m of it, or named by the
  layout (`cliff_material`); 98% of neighbouring cliff corners now agree.
  Other biomes' tilesets (sand, desert) remain Beyond v1.
- **Retail water is compact.** With 37-41% water, only 5-11% of retail land
  lies within 30 m of a shore (seas and big lakes). Flooding hilly terrain
  as-is made a maze of inlets (62%), each with its beach and soil rings.
  **Done:** flooded water forms compact basins (29% on a half-water map).

## Villages

- **Harbour villages stand on stilts over water** (user, 2026-10-02).
  Adventurer's Plain's village: 67 of 69 ground corners under ~1 m of water,
  decks 4.6-5.0 m above it, the ramp end on land; Kenji Beach's houses the
  same (65 of 81). **Done:** such prefabs are placed at the water's edge,
  turned so the ramp lands on the shore, decks at their height above the
  water; the compiler refuses them inland. For a village on dry ground use
  the land variants (`adventurer_houses`, `kenji_houses_land`).
- **The ramp must land on land** (user, 2026-10-02, phase 7b review): the
  stilt village sat over the water's edge, but its access ramp ended in
  the water. **Done:** extraction records each ramp foot that stood on dry
  ground in retail (one per village: `pad04`'s ramp); placement needs it
  over land, the ground there is made shore, and a check confirms it. The
  ground under the decks is a uniform shelf ~1 m under water, as in retail.
- **The same beach houses also stand on land in retail** (Sunshine Coast),
  9-26 cm into the ground: that is where the land variants' heights come
  from.
- **Lifted villages on flattened pads are fine** on gentle ground; on
  jagged terrain the round flat pad stands out (phase 7, map 3). Open.

## Decoration

- **Flower density at x6 of the ground's default reads right for "a
  flower-dense region"** (user, 2026-10-02, map 2).
- **No chunk holds more than retail's busiest (278 objects).** Dense covers
  are thinned evenly to 250 per chunk.
