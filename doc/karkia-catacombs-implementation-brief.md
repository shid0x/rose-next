# Karkia catacombs: implementation brief

**Status, 2026-11-09: revised, nothing built.** The 2026-10-01 design stands: one
shared zone, a new maze every day, the floor is terrain, only the walls are generated,
and the generator is our own. This revision builds that design on what has landed
since:
- **dynamic zone objects** (`GSV_ZONE_OBJECTS`, 2026-10-10; root `CLAUDE.md`), built
  for the Cerberus ice wall with this maze in mind ([cerberus-ice-wall.md](cerberus-ice-wall.md));
- **the walk grid and pathfinding** ([pathfinding-and-z-plan.md](pathfinding-and-z-plan.md),
  phases 0-7); `.MOV` has been retired since 2026-10-30;
- **`quest-editor con-toll`** and the event-controller recipe ([making-events.md](making-events.md));
- **the map tools**: `mapgen`, `nav-bake.py`, `nav-view.py` and the checklist in
  [importing-maps.md](importing-maps.md).

What that changes:
- **The client generates nothing.** The server builds the walls and sends them as zone
  objects. That removes the shared C++ generator, the seed packet and the old M3
  question of whether walls go in before or after the map loads.
- **The server knows where the walls are.** The same edge bitset blocks cells in the
  walk grid and drives a maze router. Monsters then wander, spawn, leash and chase
  inside the maze. Before, M4 offered only a straight-line check.
- **Walls can block sight here.** In the open world a walk test is not a sight test
  (nav plan §3.5: a fence stops walking, not arrows). Maze walls are full height and lie
  on known lines, so here it is. That removes the two worst v1 limitations, ranged
  attacks and aggro through walls (§7, M4).

The 2026-09-09 and 2026-10-01 versions are in git history. **Jrose is inspiration, not
a spec**: we do not port its generator, its floor tables, its entry bookkeeping or its
packets. The three Jrose reports (`jrose-catacombs-investigation.md`,
`jrose-catacombs-collision-investigation.md`, `jrose-tower-of-sorrow-investigation.md`)
are still useful as an art catalogue and for ideas, nothing more.

The goal is the smallest version that plays well, built so each later step adds to it
rather than replacing it.

---

## 1. The feature, from the player's side

- A crypt mouth in the Desolate Cemetery (zone 87). A keeper NPC stands in front.
- Hand the keeper Cemetery hearts → you are warped into the catacombs.
- The catacombs are a **maze that is different every day**. One shared zone, everyone
  in it together, no instances. Monsters in the corridors, a boss in the far room.
- **Why go in: challenge and loot.** The boss drops unique weapons; ordinary
  monsters drop good things too.
- Dying puts you back **inside**, at the entrance hall.
- A few minutes before the daily reset the crypt closes, everyone still inside is
  sent back to the crypt mouth, and the next day's maze takes its place.
- PvPvE is the goal but **comes later** (§7, M5). Everything before that ships as PvE.

## 2. Design principles

These keep v1 small. Each one removes a whole class of engine work.

1. **One shared zone, one maze per day.** The gameserver has no instancing
   (`CZoneLIST` is a flat array indexed by zone number, one thread per zone). EVO's
   per-party instances could be ported (surveyed 2026-11-03), but a shared maze is the
   point: PvPvE needs strangers in the same corridors.
2. **The floor is terrain. Only walls are generated.** The zone is a flat terrain.
   Height, click-to-move, monster heights and the avatar's height clamp all work as in
   every other zone. The walk grid agrees: the bake sees one flat floor.
3. **Walls are dynamic zone objects.** `GSV_ZONE_OBJECTS` puts placements into the scene
   with the ZSC's collision level. The avatar's body collision stops against them as
   against any map wall, and the usual correction follows: the client sends
   `CLI_CANTMOVE` and the server pulls its copy back
   (`classUSER::Recv_cli_CANTMOVE`, [gs_user.cpp](../src/sho_gameserver/src/gs_user.cpp)).
   The Cerberus ice wall proved it in game: up, down, the fade, a relog inside and a
   full run. The server sends the standing groups to every arrival after the join
   reply, so a late join or a relog sees today's walls with no extra protocol.
4. **Cells are always open; only the walls between cells change.** Every cell centre is
   reachable on every day. So everything *placed*, such as monster spawn points, the
   entrance hall, the boss room, pillars, the outer wall and decoration, is authored
   **once** in the map file and stays valid whatever the seed. The seed only decides
   which interior edges carry a wall. Because the maze is always connected, the walk
   grid's baked connectivity also stays valid every day.
5. **One generator, on the server.** It is small, deterministic and has a test vector.
   A Python mirror provides previews and the vectors. The client never sees the seed.
6. **The edge bitset is the only source.** Everything that needs the walls reads it:
   the placements sent to clients, the cells blocked in the walk grid, the maze router,
   and (M4) the sight test.

## 3. The maze model

**Grid.** 10 x 10 cells to start (tunable). **The cell size comes from the wall
art**: one wall mesh spans exactly one cell edge, so pick the wall first and size the
cell to it. The proposed art (below) gives 15 m. At 15 m the maze is 150 m across and
fits inside one 160 m map block.

**Art (proposed, surveyed 2026-11-09).** Jrose's own catacomb set (`3Ddata/KARKIA/KCatacomb/`,
not imported yet) is built for 15 m cells: 1 m meshes that its client scales x15. It
comes in two moods, one per Jrose crypt:
- **Reminiscence** (`KCatacomb`): warm brown stone, burial niches, amber lamps. One wall
  texture, `wall_010.dds`.
- **Sorrow** (`KCatacomb2`): cold teal stone, a carved frieze, cyan lamps, a vine overlay.
  Six wall textures and small decorative parts.

Proposed: Reminiscence for this crypt, and Sorrow for a second one (`maze_id`). How each
piece is used:
- **Walls: wall C** (object 113 of `KCATACOMB0101.ZSC`), the straight piece: one wall
  along one cell edge, 108 x 25 x 150 units. At x15 that is 16.2 m long (it overlaps the
  next piece by 1.2 m), 3.75 m thick and 22.5 m tall. Its collision flag is set. A, B and
  D are Jrose's corner and end pieces; edge walls do not need them. The lamps are
  painted into the texture, so the wall looks lit with no lightmap, which suits
  unlit zone objects. A zone object scales each axis, so the height can come down
  if 22.5 m fights the camera (M2).
- **Pillars**: the set has none. Use a square cut of wall C (scaled down in length) or
  a column from our Karkia decoration; pick in M2.
- **Floor**: Jrose's floor textures are made one per 15 m cell, with trims along the
  sides that assume Jrose's wall layout. Our walls move daily, so a trim would sometimes
  cross an open doorway. Our own Karkia terrain tiles are outdoor (dirt, grass) or
  the Church's blue diamond tile; none fits. Plan:
  - paint the terrain with a seamless flagstone tile cut from the trim-free middle of
    the catacomb floor textures;
  - place the round floor medallions (floor E, four variants) as fixed, non-colliding
    decoration quads in the entrance hall, the boss room and a few junctions.
- **Ceilings**: not used. Jrose's have no collision either, and they would block the
  camera.
- **Import**: wall C, its texture, the floor E quads and textures, and the floor textures
  the tile is cut from. That is a small part of the 46 files.

Contact sheets of the textures and renders of the wall pieces were made in the
2026-11-09 session (scratch only); re-create them before choosing.

**What is fixed, in the `.IFO`:**
- the outer boundary walls;
- a pillar at every grid vertex. Pillars hide wall joints and are always present,
  whatever the seed;
- floor decoration;
- monster regen points at cell centres;
- the entrance hall and the boss room. These are blocks of cells, for example 2 x 2,
  that the generator is told to leave open inside;
- the revive point in the entrance hall.

The zone is baked (`nav-bake.py`) with every interior edge open.

**What is generated, from the seed:** the interior edges. A 10 x 10 grid has 180
interior edges. A perfect maze (a spanning tree over 100 cells) opens 99 of them, so 81
carry a wall.

**Generator.**
1. Carve a perfect maze, using a recursive backtracker or randomised Prim's, with the
   fixed rooms pre-opened.
2. **Braid it**: remove a share of the remaining walls (start around 10-15%, mostly at
   dead ends) so there are loops. Pure perfect mazes are tedious to walk and, once PvP
   arrives, give nobody an escape route.

**The output is one edge bitset**, which only the server uses:
- **placements**: one `tagZONE_OBJECT` per closed edge, giving the wall's decoration id,
  the edge midpoint, and a rotation of 0 or 90°;
- **the walk-grid overlay**: every 1 m cell that a closed edge's wall touches is blocked
  (§7, M3);
- **the maze router**: a breadth-first search over the 100 cells (§7, M3);
- **the sight test** (§7, M4).

**Seed.** `seed = hash(salt, maze_id, reset_day)`. `reset_day` is the server's calendar
day counted from the reset time, not from midnight. It is computed and used **on the
server only**; `/catacomb status` prints it. Because the seed comes from the date, any
past day's maze can be regenerated to check a report. `maze_id` leaves room for a
second crypt.

**Determinism.**
- Write the PRNG ourselves (splitmix64 or xorshift).
- Never use `rand()`. Never use `std::*_distribution` either, whose output is
  implementation-defined.
- Keep a **Python mirror** of the generator in `scripts/`. It writes the M2 static
  layout through `rosefmt.ifo` (or `mapgen/ifo.py`, if the zone comes from mapgen),
  prints any day's maze as ASCII, and provides the **test vectors**: a few
  `(seed → edge-bitset hash)` pairs, written into this doc once the generator exists.
  The C++ version is done when it reproduces them. Client and server no longer need to
  agree on anything; the vectors now guarantee that the Python preview of a day is the
  maze the server built, which is what answers a player's report.

**Placement rules that will bite:**
- **One wall per edge, never one per cell.** Two cells each placing "their" wall puts
  two walls in exactly the same plane: the wall-flicker bug from
  `fix-coplanar-object-overlaps.py`.
- Walls stop at the pillar faces, or overlap into the pillar with no face in the same
  plane as a pillar face.
- Horizontal and vertical edges use the same mesh, rotated 90°.
- The wall mesh's ZSC parts must carry a collision flag. Check that before choosing the
  art.
- **The wall and the pillar must be objects of the zone's decoration ZSC** (`LIST_ZONE`
  col 11): a zone object of kind 0 names an object there.
- **No colliding decoration inside a cell or across a doorway.** The router walks
  straight from doorway to doorway, and a rock there would stop players (client
  collision) but not monsters on their router legs. Decoration with collision goes
  against walls or pillars.

## 4. The daily cycle (server)

A controller on the pattern in [making-events.md](making-events.md) §1: a singleton
with a `ProcZone`, owned by zone 90's thread. GM commands typed in another zone record
a request under its mutex. Timings are constants for now; move them to `server.toml`
later if needed.

| State | When | What happens |
|---|---|---|
| OPEN | normal | keeper accepts the toll |
| WARNING | reset - 10 min | zone-wide announcements, repeated |
| CLOSED | reset - 5 min | keeper refuses (toll **not** taken) |
| EVICT | reset | every player in the zone is warped to the crypt mouth in zone 87 |
| REROLL | right after | kill all monsters, delete ground items, compute the new seed, take the wall groups down and put the new ones up, re-stamp the walk-grid overlay, **then** reset regen points (so spawns re-roll against the new walls), back to OPEN |

**How the keeper knows the crypt is closed, with no new quest machinery.**
- Quest conditions cannot read the clock, but they can read an **NPC event value**.
  That is how Cornell's timed quest works (client `CLAUDE.md`, "Timed quests").
- The cycle sets the keeper's event value with `CObjNPC::Set_ObjVAR(0, v)`, which sends
  `GSV_SET_EVENT_STATUS` on a change: 1 = open, 0 = closed. The entry trigger's
  condition (COND_011) checks it, so a refused entry never takes the hearts.
- Set it from the C++ cycle, not from the keeper's `.aip`, so there is **one clock**.
  Cornell's AI also shows the trap of opening on an edge: a server started after the
  edge time skips the whole day.

**GM commands (needed from M1, nobody can wait a day to test):**
- `/catacomb status` shows the state, the seed, the time to reset and the wall count;
- `/catacomb reset` runs WARNING → EVICT → REROLL now, with short timers;
- `/catacomb seed N` forces a seed, live: the groups go down, then up with the new
  placements (§9).

**Edge cases:**
- **Dying** → revive point in the entrance hall. The zone's revive point is resolved as
  the nearest one (`CZoneFILE::Get_RevivePOS`), and there is only one.
- **Logging in inside** already lands you on that revive point, +/-5 m
  (`gs_threadsql.cpp`, login path). With the hall at least one cell wide, that is always
  open floor, whatever the day. The standing wall groups follow the join reply.
- **Offline at the reset**: such a player is not evicted, and logs into *today's* maze
  in the entrance hall without paying. Accepted for the alpha. The fix, if it ever
  matters, is to store the entry day per character.
- **Entering during WARNING** is allowed, and the keeper's text says when the crypt
  seals.

## 5. Entry

- **Keeper NPC** at the crypt mouth in the Cemetery, placed in the Cemetery's IFO with
  its `.CON` (making-events §4), and a crypt entrance object (or, for M1, just the NPC).
- **Toll = Cemetery hearts**, already dropped there (`add-karkia-drops.py`
  `MAT_CEMETERY`): Black 151, Green 152, Blue 153. Amount to be tuned.
- **The payment is a QSD trigger**, so the server enforces it. The trigger:
  - checks the keeper's event value is 1 (COND_011);
  - checks the hearts are in the bag;
  - removes them;
  - warps the player to the entrance hall.
- **The dialog is `quest-editor con-toll`**: the offer fires the trigger when its
  conditions pass, a "lack" answer shows otherwise (no silent failure), and
  `--open-when 1 --closed T` shows the "sealed" answer while the value is 0. The
  client's COND_011 always passes, so the dialog checks the value itself; con-toll does
  that. Existing machinery only.

## 6. Content

**Zone number: 90.** It is free in `LIST_ZONE`, and no monster's drop column points at
it (checked 2026-10-01). Re-checked 2026-11-09: the `LIST_ZONE` row is still empty, and
`ITEM_DROP` row 90 holds only a stale label, "(Stat Cap Raise to 400)", in its name
column; overwrite it. Under `TEST_ZONE_NO` (250), so it is served. Skaaj took 89.

**Zone files.** All of them are new:
- `LIST_ZONE` row;
- `.ZON` with the start and revive events;
- flat terrain;
- `.IFO`;
- **the walk grid** `<FOLDER>.lnav`, from `nav-bake.py` (`.MOV` is retired);
- name via `add-zone-name.py`.

[importing-maps.md](importing-maps.md) §2-§8 lists the rows, the folder, the walk grid
and the checks. It is written for imports, but every step applies to a new zone. No zone
has been authored from scratch and kept so far. Two routes; decide at the start of M1:
- **mapgen**: `mapgen-zone.py` installs a layout (flat ground, the outer wall, pillars,
  rooms, regen points). This is probably the cheaper route now.
- **clone** a small existing zone's files and flatten its heightmap with a script.

Either way, bake lighting with object lighting off or a sunless profile, so that M2's
walls (in the IFO) look like M3's (zone objects, no lightmap).

**Monsters.**
- Start by **reusing Cemetery undead** at regen points in the cell centres. No import is
  needed and the theme fits.
- **What the walk grid gives them** (from M2, through the bake; from M3, through the
  overlay):
  - wandering stays in the corridor (`LineWalk` refuses a line through a wall);
  - spawns land on their side of a wall (`NavPickSpawn` re-rolls a spot behind a wall
    from the regen centre), so a regen range up to half a cell does not spill into the
    next corridor (`/nav spawns`);
  - chases and the leash go round corners (M3's router).
- **Aggro still goes through walls until M4.** Keep aggro around one cell (~15 m) and the
  leash around two until then.
- Remember the spawn rules in the root `CLAUDE.md`: the regen point's `tacticPoint` at
  100, a per-slot count well under `limitCNT`.
- Importing the Jrose catacomb monster art (31 NPC rows, under 1 MB) is a later identity
  pass, not a prerequisite.

**Boss. Requirement (2026-11-09): there is always a boss in the far room, and it fights
with attacks to dodge.** A boss that stands there and hits is not good enough. Jrose's
own catacomb bosses are that kind: their AIs cast instant 20-45 m circles and a
sleep, with nothing to move out of.

- **In M2 a regen point is enough**: count 1, cap 1, a short respawn.
- **From M3 the controller owns the boss** instead. That way it can:
  - bring it back N minutes after a kill, so a later party never finds an empty room;
  - reset it (full HP, back to its spot) when the room has been empty for a while;
  - run its phases at HP thresholds (adds, a new attack), the way the Cerberus Lair
    calls the hounds at 66% and 33%;
  - announce it, and clear it at REROLL.
- **The seed also picks today's boss from a pool**, so the far room changes with the
  maze. Start with one boss and grow the pool one boss at a time.
- **Every pool boss has:**
  - at least two telegraphed attacks of different shapes (circle round the caster,
    cone, strip, fan of strips; [making-monsters.md](making-monsters.md) §6);
  - a dodge window of about 2 s, and hits worth 30-45% of a typical player's HP, sized
    with `combat-sim.py`;
  - a phase change at an HP threshold.
- **Bodies, cheapest first:**
  1. **Necromancer** (2414, from tsuki): imported and validated, but spawned nowhere,
     and it fits the crypt. It has Soul Siphon and one skill clip (a three-pulse
     strike). It needs attacks to dodge, with cast and release clips cut from what it
     has, and stats for the level band.
  2. **Jrose 2771, a green crystal spider** (Reminiscence's boss): new meshes and
     textures (three each) on a skeleton and clips we already ship (`spiderE`,
     `s_spider1`). Row 2771 is free here.
  3. **Jrose 2274, the Ancient Tree Khasiana** (Sorrow's boss): a purple thorn tree on
     a new rig, 8.5 m tall and slow. Rooted in place, it suits a fight about the ground
     (fans of roots, circles). Our row 2274 is taken (Armastyx), so it needs another
     number.
  4. **Jrose's catacomb mid-bosses** (2773-2776): an Evil Fairy, a Cursed Murderous
     Head Chef, D=Kratos (a purple larva) and Vampire Düsseldorf, on the `grandduke`
     model we ship. They could be elites at junctions, or join the pool.

  Survey each with `survey-monster.py` and import it the Shadow Soldier way
  ([importing-monsters.md](importing-monsters.md)). Their Jrose AIs are a starting point
  only.
- **Two new shapes would make the fights better**, and each is reusable elsewhere:
  - **a circle where the target stood** (an eruption): today a monster's area circle
    is always centred on the caster;
  - **hide behind a pillar** (after M4): a room-wide blast that the sight test stops, so
    the boss room's pillars become cover. The test must then count pillars as blockers.
- **Room to dodge**: a 2 x 2 boss room is 30 m across. A fan leaves gaps only from
  about 6 m out, and Cerberus's cone reaches 18 m. A 3 x 3 room (45 m) may be needed
  (open decision 5).
- Boss loot = **unique weapons**:
  - new imports via `import-item.py --art-only --template-row N`;
  - or the Jrose weapons already imported (1381-1453), after checking whether anything
    sells them.

  Item ids up to **2047** can drop since the drop-code widening (`drop_item_code.h`).

**Drop tables.**
- One table for the catacomb monsters, one for the boss, written by a script in the
  style of `add-karkia-drops.py`, with `--simulate` to check rates.
- **Fill row 90 too**: table ids and zone ids are one namespace, and with `col 20` at
  80, about 20% of every roll goes to the zone's row. Left empty, a fifth of all rolls
  drop nothing.

**Level band.** Cemetery and up, to be decided. New monster rows whose level falls in
60-199 must be added to `scripts/balance-trend-exclude.py`, and every balance pass
re-verified (root `CLAUDE.md`, Monster Balance).

## 7. Milestones

Each milestone is playable on its own. M2 needs no C++; M3 and M4 are server-only.

**M1: zone, entry, daily cycle (PvE, empty maze).** A flat, empty zone 90 with an
entrance hall, a keeper with the toll, the cycle and the GM commands. The zone is baked.

*Done when:*
- paying warps you in;
- dying and relogging both put you in the hall;
- `/catacomb reset` warns, closes (the keeper refuses and keeps your hearts), evicts to
  the crypt mouth, clears the zone, and reopens;
- `/nav` answers in zone 90 (the grid loaded, no stale warning).

**M2: one static maze with its content. No C++.** The Python generator writes one fixed
seed's walls into the IFO, with pillars, the outer wall, regen points, the boss room,
the first boss with its attacks to dodge, the drop tables and the first unique weapon.
Then the zone is re-baked.

**The bake makes the walls real for monsters**, for free: wandering, spawns, short
chases and the leash already respect them. Two things to learn here:
- how monsters play in corridors;
- **how far the 1 m A\* gets in a maze.** A search covers a 128 m window
  (`nav_path.cpp` `WIN`) and expands at most 4,000 cells (`[nav] path_expansions`). A
  15 m maze cell is 225 grid cells, so a chase longer than a few maze cells comes back
  Partial or Failed (`/nav path` shows it). A failed chase goes straight for a moment
  (`NAV_STRAIGHT_MS`), and monsters do not collide with walls on the client, so on
  screen it walks through a wall. Expected: M3's router replaces this. Until then, keep
  aggro and the leash tight.

Keep the M2 grid (`build/nav/`): it is **the reference** for M3's overlay.

*Done when:*
- it plays end to end;
- wall collision holds at walking and running speed;
- monsters wander inside their corridor, chase round a corner, and `/nav spawns` shows
  no spot behind a wall;
- the camera cannot spoil the maze from above. Wall height, fog and zoom limits are
  tuned here; this is the main open question about the look.

**M3: walls change daily. Server only.**
- **The generator in C++, in the gameserver.** It no longer needs `src/common`, since
  only the server uses it. It reproduces the Python test vectors.
- **The cycle sends the walls as zone objects**: at most 50 placements per group, so
  two groups. **No client change.** The Cerberus wall settled the old open question:
  groups added to a loaded map at any time are safe, and a joining player gets them
  right after the join reply.
- **The walk-grid overlay.** A block mask over the maze area, set at REROLL and applied
  in `Grid::SpansAt` by clearing `SPAN_WALK` in blocked cells. `SpansAt` is the one call
  that `LineWalk`, A\*, `FollowZ` and `NearestSpan` all go through, so every AI move,
  spawn re-roll and `/nav` readout sees the walls at once. A wall one cell thick is
  enough: `LineWalk` steps 4-connected through every cell its line crosses, and A\*
  never cuts a corner. The zone thread is the only writer. The mask is generic: the
  Cerberus ice wall can use it too (an open item of the nav plan).
- **The maze router.** A breadth-first search over the 100 cells through the open edges
  gives the doorways in order. The walker goes straight from doorway midpoint to doorway
  midpoint, then to the goal. Each leg lies inside one open, convex cell, 7.5 m from the
  pillars. `Nav::Agent` already walks a waypoint list. In zone 90 the router replaces
  `Nav::FindPath` at its two call sites, `CObjCHAR::Nav_Chase` (after `Nav_LineTo`
  fails) and `CObjCHAR::Nav_PathTo` (leash, summon follow, CTRL+click), whenever start
  and goal are in different maze cells. It has no budget and no window, and it never
  fails, because the maze is connected. A\* still handles anything inside one cell.
- **The interior walls leave the IFO**; re-bake. The pillars, the outer wall and the
  rooms stay.

*Done when:*
- two clients see the same walls;
- `/catacomb reset` produces a different layout;
- the Python and C++ test vectors agree;
- **for M2's seed, the overlay blocks the cells the M2 bake blocked**: a GM dump of the
  mask diffed against the kept `.lnav` by a script, allowing one cell at wall ends;
- a monster chases across the maze along one route, and the leash walks home the same
  way (`/nav path`);
- the frame rate holds with ~80 zone objects up.

**M4: walls block sight. Server only, zones with a maze only.**
- **The test**: walk the maze cells that the segment crosses (a DDA on the 15 m grid)
  and check each edge it crosses. It is exact and cheap: at most about 20 edges. A
  segment through a vertex counts as blocked if either edge there is closed.
- **Monster scans** skip a target behind a wall. No aggro through walls, so aggro can
  then grow.
- **Monster attacks and skills** need a clear line. Without one the monster keeps
  routing until it has one, the way Phase 7 handles a target on another level.
- **Player attacks and targeted skills** are refused with a whisper, as Phase 7 refuses a
  target on another level. The server walks a player's attack in, so the verdict lives
  there alone, and the client needs no change.
- **Area skills** leave out the victims behind a wall from the area's centre, as a
  post-filter like the vertical band. A ground skill aimed past a wall (caster to aim
  point) is refused before it costs.

*Done when:* a monster in the next corridor does not aggro; an archer cannot shoot
through a wall; an area hit stops at a wall.

**M5 and later: PvP and polish, in any order.**
- `ZONE_PVP_STATE` 2 (everyone except your party). Check whether that mode needs a zone
  entry trigger to assign teams, as the clan fields do (gameserver `CLAUDE.md`, "PVP
  Ally Rule"), and whether the entrance hall needs protection from spawn camping.
- **The server holds players to the walls**: clamp a click-move's destination to
  `LineWalk`'s last clear point (`pLastX`/`pLastY`), so a modified client cannot cross a
  wall on the server. This matters once players fight each other. It is the
  zone-limited form of the nav plan's Phase 8 `CANTMOVE`/`SETPOS` validation.
- A minimap: the client already holds the placements, so the UI2 minimap (RmlUi) can
  draw them without the seed.
- Catacomb monster art, chests, several floors, a second crypt (`maze_id`).

## 8. Accepted limitations (v1)

- **Ranged attacks and area skills go through walls until M4.** ROSE has no
  line-of-sight check anywhere else either.
- **The server trusts the client's collision for players until M5**, as in every zone.
  A modified client can walk through walls.
- **Click-to-move does not path.** Clicking past a wall walks into it, so the player
  walks the maze. The avatar's movement does not change.
- **Leftover straight walks.** A move that must happen walks straight when its line is
  blocked (`Nav_MoveOrStraight`). The router makes this rare; the zone's `nav:` log line
  counts them.
- **The usual minimap shows nothing useful.** No minimap until M5, which is arguably
  right for a maze.
- **Generated walls are unlit** (no lightmap). Flat lighting suits a crypt, and Shibuya
  already ships unlit.
- **A player offline at reset gets today's maze for free** (§4).

## 9. Traps from our codebase that apply here

- **A zone with no walk grid leaves AI walks unchecked.** Bake it with `nav-bake.py`,
  and re-bake after any IFO edit and when the spawn stage adds seeds. The server warns
  when a grid is stale.
- **The 1 m A\* is not a maze router**: a 128 m window and 4,000 expansions per search
  (M2).
- **Monsters never collide with zone objects**: only the avatar's body collision stops
  against them (monsters collide with tree cylinders only). A monster knows a wall only
  through the walk grid, which is why M3's overlay exists.
- **An "up" on a standing group is ignored** (`CZoneObjects::Apply`, "already up"). To
  swap walls with players inside, send down, then up with the new placements: an up
  during the fade rebuilds at once.
- **`SetZoneObjects` takes at most 50 placements per group.** Its comment still counts
  18-byte records; since per-axis scale they are 23 bytes, so 50 come to 1,150, well
  inside the 4 KB packet.
- **Drop-table ids are zone ids**, so fill row 90 (§6).
- **The keeper's dialog** (`.CON`) and **`UI_strID.ID`-style loose files** are client
  data. Ship client and data together, and bake after data edits (`scripts/pack.ps1`).
- **No `.bak` under `data/`**: `pack.rs` would bake it.
- **Restart the servers after any STB edit**; they cache tables at startup.
- **Write generator scripts as files, not bash heredocs**: `\n` and `\0` become real
  bytes.

## 10. Open decisions

1. **Art:** Reminiscence (warm, proposed) or Sorrow (cold) for the first crypt (§3,
   "Art"). Either way the cell is 15 m, and the wall goes into the zone's decoration
   ZSC.
2. **Toll amount**, and which hearts.
3. **Level band** for the monsters and the boss.
4. **Boss return:** how many minutes after a kill before the next boss stands in the
   room, and how long an empty room waits before the boss resets.
5. **Braid ratio and room sizes:** tune in M2. The boss room may need 3 x 3 cells for
   its dodges.
6. **Reset time:** any fixed server time; a constant for now.
7. **Sight for players (M4):** refuse an attack through a wall with a whisper (Phase 7's
   way, the proposal), or let players' attacks through and block only monsters and area
   hits.
8. **The first boss:** the Necromancer (cheapest, proposed), the crystal spider, or the
   tree; then the order the pool grows in.
