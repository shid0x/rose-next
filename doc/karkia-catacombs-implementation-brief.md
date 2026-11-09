# Karkia catacombs: implementation brief

**Status, 2026-11-09: M1 in progress.** Built and awaiting the first in-game test: the two
zones, the keeper, the openings, the daily registration and the GM commands
(`scripts/import-catacombs.py`, `src/sho_gameserver/src/catacombs.*`, migration 0006).
Not built yet: the on-screen timer, then M2 onwards. "Built" notes below mark where the
build differs from the plan.

**The design, revised 2026-11-09.** The 2026-10-01 design stands: one
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

**The user's decisions, 2026-11-09:**
- **Both art sets, switching.** Reminiscence and Sorrow alternate daily, one zone each
  (§2, §3).
- **Openings, not a day-long crypt.** The catacombs open every 2 hours, at even hours
  server time, for 1 h 50 min. The maze still changes once a day.
- **Registration once a day, for one opening.** You give the keeper the materials and
  are registered for the current opening. You re-enter free until it ends, and cannot
  register again until the next day. "That's the catch, you can't spam it." (§4, §5)
- **The boss comes back about 15 minutes after a kill, with a timer on screen** (§6).
- **The first boss is the Necromancer**; the spider and the tree come next (§6).
- **Level band 235-240: our endgame content** (§6).
- **Player attacks through walls: later.** M4 does the monster side first (§7).
- **Room sizes, the empty-room reset and the boss pools**: left to the brief's
  recommendations (§3, §6).

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
- **The catacombs open every 2 hours (even hours), for 1 h 50 min.** Hand the keeper
  Cemetery hearts to register for the current opening, and you are warped in. You can go
  back in free until that opening ends. **You can register once a day**, so after your
  opening the crypt is closed to you until the next day's maze.
- **Endgame: levels 235-240.**
- The catacombs are a **maze that is different every day**, and **the crypt changes with
  it**: Reminiscence (warm stone, amber lamps) one day, Sorrow (cold teal stone, cyan
  lamps) the next. One shared zone per crypt, everyone in it together, no instances.
  Monsters in the corridors, a boss in the far room.
- **There is always a boss.** It is there when an opening starts, and returns about 15
  minutes after each kill. A timer on screen says when.
- **Why go in: challenge and loot.** The boss drops red weapons; ordinary monsters
  drop good things too.
- Dying puts you back **inside**, at the entrance hall.
- When an opening ends, everyone still inside is sent back to the crypt mouth, and the
  next opening starts 10 minutes later. On one of those breaks each day, the next day's
  maze and crypt take over.
- PvPvE is the goal but **comes later** (§7, M5). Everything before that ships as PvE.

## 2. Design principles

These keep v1 small. Each one removes a whole class of engine work.

1. **One shared zone per crypt, one maze per day.** Reminiscence is zone 90 and Sorrow
   is zone 91. Only today's crypt is open; the other stands empty. The gameserver has no
   instancing (`CZoneLIST` is a flat array indexed by zone number, one thread per zone).
   EVO's per-party instances could be ported (surveyed 2026-11-03), but a shared maze
   is the point: PvPvE needs strangers in the same corridors.
   - **Why two zones and not one zone that switches its art:** zone objects can switch
     the walls, but not the terrain's floor tile, the fog and light colours or the music,
     which are fixed per zone. Two zones also let each crypt keep its own monsters and
     boss pool later, as Jrose's two crypts did.
   - Both zones come from **one script and one layout**, so their fixed parts, spawn
     points and walk grids match. They differ in art, floor tile, fog, light and music.
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

**Both are used (user, 2026-11-09)**: Reminiscence dresses zone 90, Sorrow zone 91, and
they alternate daily (§2). How each piece is used:
- **Walls: wall C** (object 113 of `KCATACOMB0101.ZSC` and of `KCATACOMB0201.ZSC`), the
  straight piece: one wall along one cell edge, 108 units long and 150 tall.
  Reminiscence's is 25 thick. At x15 that is 16.2 m long (it overlaps the next piece by
  1.2 m), 3.75 m thick and 22.5 m tall. Sorrow's is a 16-unit wall (2.4 m) plus a
  non-colliding carved panel. Both carry collision. Stamp each set's own footprint into
  the walk-grid overlay. A, B and D are Jrose's corner and end pieces; edge walls do
  not need them. The lamps are
  painted into the texture, so the wall looks lit with no lightmap, which suits
  unlit zone objects. A zone object scales each axis, so the height can come down
  if 22.5 m fights the camera (M2).
- **Pillars**: the set has none. Use a square cut of wall C (scaled down in length) or
  a column from our Karkia decoration; pick in M2.
- **Floor**: Jrose's floor textures are made one per 15 m cell. Reminiscence's have
  trims along the sides that assume Jrose's wall layout. Our walls move daily, so a
  trim would sometimes cross an open doorway. Sorrow's are plain mossy flagstones. Our
  own Karkia terrain tiles are outdoor (dirt, grass) or the Church's blue diamond tile;
  none fits. Plan, per zone:
  - paint the terrain with a seamless flagstone tile from that set: cut from the
    trim-free middle of Reminiscence's floors, and taken from Sorrow's plain ones;
  - place that set's round floor medallions (floor E, four variants) as fixed,
    non-colliding decoration quads in the entrance hall, the boss room and a few
    junctions.
- **Music** per zone (built): `Ramesses.ogg` for Reminiscence, as in Jrose, and
  `KChurch.ogg` for Sorrow.
- **Fog cannot differ per zone today.** The client sets the fog colour from the time
  of day alone (`s_FogColor` in `cdaynnightproc.cpp`: grey by day, near black at
  night), not from any table. Both zones are "underground" rows on the dungeon sky
  (`LIST_SKY` 1). A warm or cold light per crypt would be two new `LIST_SKY` rows; a
  fog colour per crypt needs client code. Decide in M2, once the walls can be seen.
- **Ceilings**: not used. Jrose's have no collision either, and they would block the
  camera.
- **Import**, for each set: wall C and its textures, the floor E quads and textures, and
  the floor textures the tile comes from. That is a small part of the 46 files.

Contact sheets of the textures and renders of the wall pieces were made in the
2026-11-09 session (scratch only); re-create them before choosing.

**What is fixed, in the `.IFO`:**
- the outer boundary walls;
- a pillar at every grid vertex. Pillars hide wall joints and are always present,
  whatever the seed;
- floor decoration;
- monster regen points at cell centres;
- the entrance hall and the boss room, blocks of cells that the generator is told to
  leave open inside. **Recommended (accepted 2026-11-09):**
  - the hall is 2 x 2 cells (30 m), with no regen point in it or next to it, so a
    revived player does not stand up in a fight;
  - the boss room is 3 x 3 cells (45 m). 30 m is too tight to dodge in: a fan leaves
    gaps only from about 6 m out, and Cerberus's cone reaches 18 m. Its four inner
    pillars stay, as cover now and for the "hide behind a pillar" attack later (§6);
  - they sit in opposite corners, so the boss is the far end of the maze;
- the revive point in the entrance hall.

The zone is baked (`nav-bake.py`) with every interior edge open.

**What is generated, from the seed:** the interior edges. A 10 x 10 grid has 180
interior edges. A perfect maze (a spanning tree over 100 cells) opens 99 of them, so 81
carry a wall.

**Generator.**
1. Carve a perfect maze, using a recursive backtracker or randomised Prim's, with the
   fixed rooms pre-opened.
2. **Braid it**: open a wall at dead ends, one at a time, until only **2 dead ends**
   remain (user, 2026-11-09: "hard to say without playing, maybe 2 for now"; a
   constant, tuned by playing). That makes loops. Pure perfect mazes are tedious to walk
   and, once PvP arrives, give nobody an escape route. A perfect 10 x 10 maze has
   roughly two dozen dead ends, so this removes about a third of its 81 walls.
   - **Recommended: braiding never opens a wall of the boss room.** It keeps the single
     door the carve gave it, so the boss stays the far end of the maze, and later a PvP
     choke point.

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
past day's maze can be regenerated to check a report. `maze_id` is the crypt (90 or
91). Today's crypt is `reset_day` modulo 2, so it is known from the date too.

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

## 4. The cycle: openings and the daily change (server)

A controller on the pattern in [making-events.md](making-events.md) §1: a singleton
with a `ProcZone`, driven by today's crypt's thread. GM commands typed in another zone
record a request under its mutex. Timings are constants for now; move them to
`server.toml` later if needed.

**An opening every 2 hours at even hours, server time** (user, 2026-11-09): 0:00, 2:00 ...
22:00, 12 a day.

| State | When, in each 2-hour period | What happens |
|---|---|---|
| OPEN | 0:00 - 1:40 | the keeper registers (once a day per character, for this opening) and lets this opening's registered back in; the boss is up when the opening starts |
| WARNING | 1:40 - 1:50 | zone-wide announcements at 10, 5 and 1 minutes; entry still allowed |
| BREAK | 1:50 - 2:00 | the keeper refuses (toll **not** taken); everyone inside is warped to the crypt mouth in zone 87; this opening's registrations end; all monsters are killed, ground items deleted and regen points reset |

**The daily change** happens on the 23:50 - 0:00 break (user, 2026-11-09), so a day's
maze runs from 0:00 to 23:50 and `reset_day` is the server's calendar date. On
top of the break's work, it:
1. computes the new seed;
2. takes the wall groups down in today's crypt;
3. switches to the other crypt (90 ↔ 91), so Reminiscence and Sorrow alternate;
4. puts the new wall groups up there and re-stamps its walk-grid overlay;
5. **then** resets that zone's regen points, so spawns re-roll against the new walls.

**Registration: once a day, for one opening** (user, 2026-11-09: "everyone who
registered can enter, provided they gave materials"; "the toll should only be payable
once per day, that's the catch, you can't spam it"):
- A payment registers the character for **the current opening only**. It re-enters
  free until that opening ends, and cannot register again until the daily change. The
  day is the maze's `reset_day`.
- **The record lives in the database**: one row per character, `(reset_day, opening)`,
  in a small table of its own, as `union_points` was added (migration 0005). It is
  **saved at the payment**, not at logout. Then a crash or a restart can neither allow
  a second payment that day nor lock out someone who has paid.
- The current opening's list is the characters whose record is (today, this opening),
  read with the character at login. Nothing is lost on a restart.
- **Every tick, anyone in a crypt zone whose record is not (today, this opening) is
  warped to the crypt mouth.** That covers a relog after the opening ended, a GM
  teleport and a crash. It also closes the old "offline at the reset, back in free"
  hole. GMs are exempt.

**How the keeper knows the crypt is closed, with no new quest machinery.**
- Quest conditions cannot read the clock, but they can read an **NPC event value**.
  That is how Cornell's timed quest works (client `CLAUDE.md`, "Timed quests").
- The cycle sets the keeper's event value with `CObjNPC::Set_ObjVAR(0, v)`, which sends
  `GSV_SET_EVENT_STATUS` on a change: 1 = open (OPEN and WARNING), 0 = the break. The
  entry triggers' condition (COND_011) checks it, so a refused entry never takes the
  hearts.
- Set it from the C++ cycle, not from the keeper's `.aip`, so there is **one clock**.
  Cornell's AI also shows the trap of opening on an edge: a server started after the
  edge time skips the whole day. The cycle works out its state from the clock at any
  moment, so a start mid-opening simply finds it open.

**GM commands (nobody can wait 2 hours to test).** Built: they shift the controller's
clock (a skew added to the wall clock), so every test runs the real code path.
- `/catacomb status` shows the state, today's crypt, the day and opening, the time to
  the next change, who is registered, and any clock shift;
- `/catacomb close` jumps to one minute before the break: the one-minute warning, then
  the seal and the evictions;
- `/catacomb open` jumps to the start of the next opening (earlier registrations are
  then spent, as after a real opening);
- `/catacomb daily` jumps one day ahead: the other crypt, and every pass is spent;
- `/catacomb clock` drops the shift and returns to the real clock;
- `/catacomb pass [name]` clears a character's daily record (your own with no name), so
  a tester can register again.
- Later: `/catacomb seed N` (M3) and `/catacomb boss` (M2).

**Edge cases:**
- **Dying** → revive point in the crypt's entrance hall. The zone's revive point is
  resolved as the nearest one (`CZoneFILE::Get_RevivePOS`), and there is only one.
- **Logging in inside** lands you on that revive point, +/-5 m (`gs_threadsql.cpp`,
  login path). With the hall at least one cell wide, that is always open floor,
  whatever the day. The standing wall groups follow the join reply. If your record is
  not this opening's, the next tick sends you to the crypt mouth.
- **Entering during WARNING** is allowed, and the keeper's text says when the crypt
  seals.

## 5. Entry

- **Keeper NPC** at the crypt mouth in the Cemetery, placed in the Cemetery's IFO with
  its `.CON` (making-events §4), and a crypt entrance object (or, for M1, just the NPC).
- **Toll = a few Cemetery hearts, as a placeholder** (user, 2026-11-09: "doesn't matter
  much for now, just some Karkia drop as a temp thing"). They already drop there
  (`add-karkia-drops.py` `MAT_CEMETERY`): Black 151, Green 152, Blue 153. Keep the item
  and count as constants, so they are easy to change.
- **Paid once a day** (§4): one payment buys one opening, with free re-entry until it
  ends.
- **One dialog option, one QSD trigger** (built; the plan had two). `Catacomb-Enter`
  checks only that the keeper's event value is 1 (COND_011). **It takes nothing.** One
  option is enough because the controller decides what the click means, and a second
  "I am registered" option added nothing a player needs.
- **The controller does the rest, not the QSD** (`CCatacombs::OnEnter`). The
  once-a-day rule needs the database record, the destination changes daily (zone 90 or
  91), and a QSD would take the hearts before any C++ could refuse. The hook is the
  trigger's name in `classUSER::Do_QuestTRIGGER`, after the trigger succeeded
  (making-events §1). A trigger with conditions and no rewards does count as succeeded.
  - registered for this opening: warped in, free;
  - paid today for an opening that has ended: refused with a whisper, **nothing taken**;
  - otherwise: the toll is taken across stacks (`classUSER::Sub_ItemNSend`), the record
    saved, and the character warped to today's entrance hall;
  - without the toll: a whisper names the price. The dialog cannot show a "lack"
    answer here, because a registered player with no hearts left must still get in.
  - A trigger sent from anywhere but the keeper's side (20 m) is ignored.
- **The dialog** is `quest-editor con-toll`: the offer names the price and the
  once-a-day rule, and `--open-when 1 --closed T` shows the "sealed" answer while the
  value is 0. The client's COND_011 always passes, so the dialog checks the value
  itself; con-toll does that.
- **Built**: the keeper is [Crypt Keeper] Osric, `LIST_NPC` 4020, the plague doctor's
  look, beside the Columbarium in the Cemetery, where Jrose put its Reminiscence
  entrance. The toll is five Black Hearts (`LIST_NATURAL` 151).

## 6. Content

**Zone numbers: 90 (Reminiscence) and 91 (Sorrow).** Both were free on 2026-11-09:
their `LIST_ZONE` rows are empty, no monster's drop column (`LIST_NPC` col 18) points at
either, and their `ITEM_DROP` rows hold only stale labels in the name column, "(Stat Cap
Raise to 400)" and "Karkia Town (Lvl 215)"; overwrite them. Both are under
`TEST_ZONE_NO` (250), so they are served. Skaaj took 89; 92 is free too.

**Zone files**, for each of the two zones; all of them are new:
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
- Start by **reusing the Cemetery undead's models** at regen points in the cell centres,
  in both crypts, as **new rows at 235-240**. The Cemetery's own rows keep their level.
  No art import is needed and the theme fits. Later each crypt can take its own Jrose
  roster (`KCC_` for Reminiscence, `KCC2_` for Sorrow).
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

- **Timing (user, 2026-11-09): the boss returns about 15 minutes after each kill, with a
  timer on screen.** It is up when an opening starts. With 1 h 50 min openings, that
  is up to about seven kills an opening.
- **So the controller owns the boss from M2, not a regen point.** It:
  - spawns it at the opening, and again 15 minutes (a constant) after each kill;
  - resets it (full HP, back to its spot) when the boss room has been empty for a
    minute. This is not the respawn. Monsters do not regain HP on their own (the
    recovery call in `cobjchar.cpp` is commented out), so a boss that a party hurts and
    then abandons, by a wipe or a retreat, would otherwise wait half-dead for the next
    party (recommended, accepted 2026-11-09);
  - runs its phases at HP thresholds (adds, a new attack), the way the Cerberus Lair
    calls the hounds at 66% and 33%;
  - announces it in the zone ("stirs in 5 minutes", "1 minute", "has risen");
  - removes it at the break. When the opening will end before the 15 minutes are up,
    the timer shows the seal instead, and the next opening starts with the boss up.
- **The timer on screen is new, small and generic.** Today the only countdown a player
  sees is a timed quest's, in the journal. That counts from each player's own start,
  so it cannot show a shared boss timer. Proposed:
  - a packet, at a free legacy id next to `GSV_ZONE_OBJECTS` (0x07f1), carries a few
    labelled countdowns (an id, a label, seconds left; 0 hides one);
  - the server sends the standing timers on every arrival, as it does the zone
    objects, and a change at once;
  - a UI2 HUD element under the minimap shows them: "The Necromancer returns in 12:31",
    "The crypt seals in 1:42:10";
  - the classic UI gets the announcements only;
  - client and server ship together. The Cerberus Lair's run and other events can use
    it too.
- **Each crypt has a boss pool, and the seed picks today's boss from it**, so the far
  room changes with the maze. The Necromancer comes first (user, 2026-11-09) and serves
  both crypts until they have their own: the spider for Reminiscence and the tree for
  Sorrow, as in Jrose (recommended, accepted 2026-11-09). Grow each pool one boss at a
  time.
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
     has, and boss stats at level 240 (it is level 120 today).
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
- **Room to dodge**: the boss room is 3 x 3 cells, 45 m across (§3).
- **Boss loot: red weapons for now, tailored later** (user, 2026-11-09: "don't overfocus
  on the loot"). The 2026-10-01 pick was the black blood-stained Jrose set (1456-1468,
  already imported), with a red name from prefix 22 and placeholder stats. Item ids up
  to **2047** can drop since the drop-code widening (`drop_item_code.h`).

**Drop tables.**
- One table for the catacomb monsters, one for the boss, written by a script in the
  style of `add-karkia-drops.py`, with `--simulate` to check rates.
- **Fill rows 90 and 91 too**: table ids and zone ids are one namespace, and with
  `col 20` at 80, about 20% of every roll goes to the zone's row. Left empty, a fifth of
  all rolls drop nothing.
- **Size the boss table per player-day.** A player gets one opening a day, so at most
  about seven boss kills. The boss itself dies up to about 80 times a day across all
  players.

**Level band: 235-240, our endgame content** (user, 2026-11-09).
- Every new row, monsters and boss, sits at 235-240.
- That is above the balance trend's 60-199 window, so the rows need no
  `scripts/balance-trend-exclude.py` entry. Still re-verify the balance passes after
  adding them (root `CLAUDE.md`, Monster Balance).
- Characters well below 235 will mostly miss: the level gate in `Get_SuccessRATE`
  (`src/common/calculation.cpp`). That is right for endgame content. Test with a
  character at 235-240 (making-monsters §6).

## 7. Milestones

Each milestone is playable on its own. M2's C++ is the boss alone; M3 and M4 are
server-only.

**M1: the two zones, registration, the openings, the timer (PvE, empty mazes).** Flat,
empty zones 90 and 91, each with an entrance hall and its own art, floor, fog and music.
Then the keeper with both options, the opening cycle, the daily switch, the GM commands,
and the on-screen timer (the seal countdown first). Both zones are baked.

*Done when:*
- paying registers you and warps you into today's crypt; talking to the keeper again
  during the same opening lets you back in free;
- after your opening ends, the keeper refuses you until the next day and takes
  nothing; `/catacomb pass` clears it for a tester;
- the record survives a server restart: a registered player who logs back in during
  their opening is still in;
- dying and relogging during the opening both put you in the hall; relogging after it
  ended puts you at the crypt mouth;
- `/catacomb close` warns, seals (the keeper refuses and keeps your hearts), evicts to
  the crypt mouth, clears the zone, and `/catacomb open` reopens it;
- `/catacomb daily` switches to the other crypt;
- two clients show the same seal countdown;
- `/nav` answers in both zones (the grid loaded, no stale warning).

**M2: one static maze per crypt, its content and the first boss.** The Python generator
writes one fixed seed's walls into both IFOs, with pillars, the outer wall, regen
points and the boss room. Add the Necromancer with its attacks to dodge, the drop tables
and the first red weapon, then re-bake both zones. The only C++ is the boss: the
controller spawns it, brings it back 15 minutes after a kill, resets it, and feeds the
timer.

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
  tuned here; this is the main open question about the look;
- the boss fight is worth going in for: both attacks can be read and dodged, it comes
  back on time, and the timer agrees with it.

**M3: walls change daily. Server only.**
- **The generator in C++, in the gameserver.** It no longer needs `src/common`, since
  only the server uses it. It reproduces the Python test vectors.
- **The cycle sends the walls as zone objects** into today's crypt, in that crypt's
  art: at most 50 placements per group, so two groups. **No client change.** The Cerberus wall settled the old open question:
  groups added to a loaded map at any time are safe, and a joining player gets them
  right after the join reply.
- **The walk-grid overlay.** A block mask over the maze area, set at the daily change and applied
  in `Grid::SpansAt` by clearing `SPAN_WALK` in blocked cells. `SpansAt` is the one call
  that `LineWalk`, A\*, `FollowZ` and `NearestSpan` all go through, so every AI move,
  spawn re-roll and `/nav` readout sees the walls at once. A wall one cell thick is
  enough: `LineWalk` steps 4-connected through every cell its line crosses, and A\*
  never cuts a corner. The zone thread is the only writer. The mask is generic: the
  Cerberus ice wall can use it too (an open item of the nav plan).
- **The maze router.** A breadth-first search over the 100 cells through the open edges
  gives the doorways in order. The walker goes straight from doorway midpoint to doorway
  midpoint, then to the goal. Each leg lies inside one open, convex cell, 7.5 m from the
  pillars. `Nav::Agent` already walks a waypoint list. In the crypt zones the router replaces
  `Nav::FindPath` at its two call sites, `CObjCHAR::Nav_Chase` (after `Nav_LineTo`
  fails) and `CObjCHAR::Nav_PathTo` (leash, summon follow, CTRL+click), whenever start
  and goal are in different maze cells. It has no budget and no window, and it never
  fails, because the maze is connected. A\* still handles anything inside one cell.
- **The interior walls leave the IFO**; re-bake. The pillars, the outer wall and the
  rooms stay.

*Done when:*
- two clients see the same walls;
- `/catacomb daily` produces a different layout, in the other crypt's art;
- the Python and C++ test vectors agree;
- **for M2's seed, the overlay blocks the cells the M2 bake blocked**: a GM dump of the
  mask diffed against the kept `.lnav` by a script, allowing one cell at wall ends;
- a monster chases across the maze along one route, and the leash walks home the same
  way (`/nav path`);
- the frame rate holds with ~80 zone objects up.

**M4: walls block monsters' sight. Server only, zones with a maze only.** The player side
comes later (user, 2026-11-09: "we'll deal with it later").
- **The test**: walk the maze cells that the segment crosses (a DDA on the 15 m grid)
  and check each edge it crosses. It is exact and cheap: at most about 20 edges. A
  segment through a vertex counts as blocked if either edge there is closed.
- **Monster scans** skip a target behind a wall. No aggro through walls, so aggro can
  then grow.
- **Monster attacks and skills** need a clear line. Without one the monster keeps
  routing until it has one, the way Phase 7 handles a target on another level.
- **Monster area skills** leave out the victims behind a wall from the area's centre,
  as a post-filter like the vertical band. With pillars counted as blockers, this is
  what the boss's "hide behind a pillar" attack needs (§6).

*Done when:* a monster in the next corridor does not aggro, and a boss's area hit stops
at a wall.

**Later, the player side** (deferred by the user): the same test for players.
- **Attacks and targeted skills** would be refused with a whisper, as Phase 7 refuses a
  target on another level. The server walks a player's attack in, so the verdict lives
  there alone, and the client needs no change.
- **Area skills** would leave out victims behind a wall.
- **A ground skill aimed past a wall** would be refused before it costs.

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
- Catacomb monster art (each crypt its own Jrose roster), chests, several floors.

## 8. Accepted limitations (v1)

- **Monsters see, aggro and hit through walls until M4.** ROSE has no line-of-sight
  check anywhere else either.
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
- **Players' attacks and area skills go through walls** until the player side of M4 is
  done (deferred).
- **The on-screen timer is UI2 only.** The classic UI gets the zone announcements.

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
- **Drop-table ids are zone ids**, so fill rows 90 and 91 (§6).
- **Two zones, one layout**: generate both from the same script and bake both after any
  change, or their spawn points and grids drift apart.
- **The keeper's dialog** (`.CON`) and **`UI_strID.ID`-style loose files** are client
  data. Ship client and data together, and bake after data edits (`scripts/pack.ps1`).
- **No `.bak` under `data/`**: `pack.rs` would bake it.
- **Restart the servers after any STB edit**; they cache tables at startup.
- **Write generator scripts as files, not bash heredocs**: `\n` and `\0` become real
  bytes.

## 10. Open decisions

**Decided by the user, 2026-11-09:**
- **Art**: both sets, alternating daily, one zone each (90 Reminiscence, 91 Sorrow).
- **Openings**: every 2 hours at even hours, server time, for 1 h 50 min. The maze
  changes once a day.
- **Toll**: payable once a day, and it buys one opening, with free re-entry until it
  ends. "That's the catch, you can't spam it." The amount is a placeholder: a few
  Cemetery hearts.
- **Dead ends**: 2 for now, tuned by playing.
- **Daily change**: on the 23:50 - 0:00 break.
- **Boss loot**: red weapons, tailored later.
- **Level band**: 235-240, our endgame content.
- **Boss return**: about 15 minutes after a kill, with a timer on screen.
- **First boss**: the Necromancer; the spider and the tree are good next ones.
- **Player sight through walls**: later. M4 does the monster side.
- **Left to the brief's recommendations**:
  - rooms: hall 2 x 2, boss room 3 x 3, in opposite corners;
  - the boss resets when its room has been empty for a minute;
  - pools: the spider goes to Reminiscence, the tree to Sorrow.

**Still open:** nothing blocks M1. Tuning waits for play: the dead-end count, the
toll, the boss's numbers and the loot.
