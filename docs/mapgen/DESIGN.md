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
