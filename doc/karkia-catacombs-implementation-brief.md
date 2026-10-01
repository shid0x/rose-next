# Karkia catacombs: implementation brief

**Status, 2026-10-01: redesigned, nothing built.** This version replaces the
2026-09-09 brief (still in git history), which was organised around reproducing
Jrose's catacombs. **Jrose is inspiration, not a spec**: we do not port its
generator, its floor tables, its entry bookkeeping or its packets. The three
Jrose reports (`jrose-catacombs-investigation.md`,
`jrose-catacombs-collision-investigation.md`,
`jrose-tower-of-sorrow-investigation.md`) stay useful as an art catalogue and for
ideas, nothing more.

The goal is the smallest version that plays well, built so each later step adds
to it rather than replacing it.

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

These are what keep v1 small. Each one removes a whole class of engine work.

1. **One shared zone, one maze per day.** The gameserver has no instancing
   (`CZoneLIST` is a flat array indexed by zone number, one thread per zone), and
   we do not need any.
2. **The floor is terrain. Only walls are generated.** The zone is a flat terrain.
   Height, click-to-move, monster heights and the avatar's height clamp all work as
   in every other zone. That removes the ground-height policy, floor picking and
   floor residency, which were the three hardest items in the old brief.
3. **Walls are ordinary map objects.** Client collision against fixed objects
   already works, and so does the correction that follows: the client sends
   `CLI_CANTMOVE` and the server pulls its copy of the avatar back
   (`classUSER::Recv_cli_CANTMOVE`, [gs_user.cpp](../src/sho_gameserver/src/gs_user.cpp)).
   Every building wall in the game already relies on this.
4. **Cells are always open; only the walls between cells change.** Every cell centre
   is reachable on every day. So everything *placed*, such as monster spawn points,
   the entrance hall, the boss room, pillars, the outer wall and decoration, is
   authored **once** in the map file and stays valid whatever the seed. The seed only
   decides which interior edges carry a wall.
5. **Our own generator, our own PRNG.** It is small, deterministic and has a test
   vector. It is not a port of anything.

## 3. The maze model

**Grid.** 10 x 10 cells to start (tunable). **The cell size comes from the wall
art**: one wall mesh spans exactly one cell edge, so pick the wall first and size
the cell to it (around 15 m is a good target). At 15 m the maze is 150 m across and
fits inside one 160 m map block.

**What is fixed, in the `.IFO`:**
- the outer boundary walls;
- a pillar at every grid vertex. Pillars hide wall joints and are always present,
  whatever the seed;
- floor decoration;
- monster regen points at cell centres;
- the entrance hall and the boss room. These are blocks of cells, for example 2 x 2,
  that the generator is told to leave open inside;
- the revive point in the entrance hall.

**What is generated, from the seed:** the interior edges. A 10 x 10 grid has 180
interior edges. A perfect maze (a spanning tree over 100 cells) opens 99 of them,
so 81 carry a wall.

**Generator.**
1. Carve a perfect maze, using a recursive backtracker or randomised Prim's, with the
   fixed rooms pre-opened.
2. **Braid it**: remove a share of the remaining walls (start around 10-15%, mostly
   at dead ends) so there are loops. Pure perfect mazes are tedious to walk and,
   once PvP arrives, give nobody an escape route.

The output is one edge bitset. Client and server consume exactly that.

**Seed.** `seed = hash(salt, maze_id, reset_day)`. `reset_day` is the server's
calendar day counted from the reset time, not from midnight. It is computed **on the
server only** and sent to the client (§7, M3). The client never reads its own clock
for this, because clocks and timezones differ (`client.log` is even in UTC). Because
the seed comes from the date, any past day's maze can be regenerated to check a
report. `maze_id` leaves room for a second crypt.

**Determinism.**
- Write the PRNG ourselves (splitmix64 or xorshift).
- Never use `rand()`. Never use `std::*_distribution` either, whose output is
  implementation-defined.
- Keep a **Python mirror** of the generator in `scripts/`. It writes the M2 static
  layout, prints any day's maze as ASCII for debugging, and provides the **test
  vectors**: a few `(seed → edge-bitset hash)` pairs, written into this doc once the
  generator exists. The C++ version is done when it reproduces them.

**Placement rules that will bite:**
- **One wall per edge, never one per cell.** Two cells each placing "their" wall puts
  two walls in exactly the same plane: the wall-flicker bug from
  `fix-coplanar-object-overlaps.py`.
- Walls stop at the pillar faces, or overlap into the pillar with no face in the same
  plane as a pillar face.
- Horizontal and vertical edges use the same mesh, rotated 90°.
- The wall mesh's ZSC parts must carry a collision flag. Check that before choosing
  the art.

## 4. The daily cycle (server)

A small state machine owned by the catacomb zone's thread. Timings are constants for
now; move them to `server.toml` later if needed.

| State | When | What happens |
|---|---|---|
| OPEN | normal | keeper accepts the toll |
| WARNING | reset - 10 min | zone-wide announcements, repeated |
| CLOSED | reset - 5 min | keeper refuses (toll **not** taken) |
| EVICT | reset | every player in the zone is warped to the crypt mouth in zone 87 |
| REROLL | right after | kill all monsters, delete ground items, reset regen points, compute the new seed, back to OPEN |

**How the keeper knows the crypt is closed, with no new quest machinery.**
- Quest conditions cannot read the clock, but they can read an **NPC event value**.
  That is how Cornell's timed quest works (client `CLAUDE.md`, "Timed quests").
- The cycle sets the keeper's event value: 1 = open, 0 = closed. The entry trigger's
  condition checks it, so a refused entry never takes the hearts.
- Set it from the C++ cycle, not from the keeper's `.aip`, so there is **one clock**.
  Cornell's AI also shows the trap of opening on an edge: a server started after the
  edge time skips the whole day.

**GM commands (needed from M1, nobody can wait a day to test):**
- `/catacomb status` shows the state, seed and time to reset;
- `/catacomb reset` runs WARNING → EVICT → REROLL now, with short timers;
- `/catacomb seed N` forces a seed.

**Edge cases:**
- **Dying** → revive point in the entrance hall. The zone's revive point is resolved
  as the nearest one (`CZoneFILE::Get_RevivePOS`), and there is only one.
- **Logging in inside** already lands you on that revive point, +/-5 m
  (`gs_threadsql.cpp`, login path). With the hall at least one cell wide, that is
  always open floor, whatever the day.
- **Offline at the reset**: such a player is not evicted, and logs into *today's*
  maze in the entrance hall without paying. Accepted for the alpha. The fix, if it
  ever matters, is to store the entry day per character.
- **Entering during WARNING** is allowed, and the keeper's text says when the crypt
  seals.

## 5. Entry

- **Keeper NPC** at the crypt mouth in the Cemetery, with a crypt entrance object
  placed in the Cemetery's IFO (or, for M1, just the NPC).
- **Toll = Cemetery hearts**, already dropped there (`add-karkia-drops.py`
  `MAT_CEMETERY`): Black 151, Green 152, Blue 153. Amount to be tuned.
- **The payment is a QSD trigger**, so the server enforces it. The trigger:
  - checks the keeper's event value is 1 (open);
  - checks the hearts are in the bag;
  - removes them;
  - warps the player to the entrance hall.

  The dialog option goes in through the QEX1 appendix (quest-editor `con-warp` /
  `con-append`). This uses existing machinery only.

## 6. Content

**Zone number: 90.** It is free in `LIST_ZONE`, its `ITEM_DROP` row is empty, and no
monster's drop column points at it (checked 2026-10-01). Under `TEST_ZONE_NO` (250),
so it is served. Skaaj took 89.

**Zone files.** All of them are new:
- `LIST_ZONE` row;
- `.ZON` with the start and revive events;
- flat terrain;
- `.IFO`;
- `.MOV`;
- name via `add-zone-name.py`.

No zone has been authored from scratch here before; every one so far was imported.
The likely cheapest route is to **clone a small existing zone's files and flatten
its heightmap with a script**. Decide at the start of M1.

**Monsters.**
- Start by **reusing Cemetery undead** at regen points in the cell centres. No import
  is needed and the theme fits.
- Keep aggro range under one cell and the leash short, so monsters stay in their own
  cell until §7 M4.
- Remember the spawn rules in the root `CLAUDE.md`: the regen point's `tacticPoint`
  at 100, a per-slot count well under `limitCNT`.
- Importing the Jrose catacomb monster art (31 NPC rows, under 1 MB) is a later
  identity pass, not a prerequisite.

**Boss.**
- One regen point in the boss room, count 1, cap 1. Respawn interval to be decided
  (a few hours, or once per day). REROLL resets it either way.
- Boss loot = **unique weapons**:
  - new imports via `import-item.py --art-only --template-row N`;
  - or the Jrose weapons already imported (1381-1453), after checking whether
    anything sells them.

  Item ids up to **2047** can drop since the drop-code widening
  (`drop_item_code.h`).

**Drop tables.**
- One table for the catacomb monsters, one for the boss, written by a script in the
  style of `add-karkia-drops.py`, with `--simulate` to check rates.
- **Fill row 90 too**: table ids and zone ids are one namespace, and with `col 20` at
  80, about 20% of every roll goes to the zone's row. Left empty, a fifth of all
  rolls drop nothing.

**Level band.** Cemetery and up, to be decided. New monster rows whose level falls in
60-199 must be added to `scripts/balance-trend-exclude.py`, and every balance pass
re-verified (root `CLAUDE.md`, Monster Balance).

## 7. Milestones

Each milestone is playable on its own. Most of the engine risk sits in **M3**, and by
then it comes down to one question.

**M1: zone, entry, daily cycle (PvE, empty maze).** A flat, empty zone 90 with an
entrance hall, a keeper with the toll, the cycle and the GM commands.

*Done when:*
- paying warps you in;
- dying and relogging both put you in the hall;
- `/catacomb reset` warns, closes (the keeper refuses and keeps your hearts), evicts
  to the crypt mouth, clears the zone, and reopens.

**M2: one static maze with its content.** The Python generator writes one fixed
seed's walls into the IFO, with pillars, the outer wall, regen points, the boss room,
the drop tables and the first unique weapon. **Almost no C++.**

*Done when:*
- it plays end to end;
- wall collision holds at walking and running speed;
- monsters behave in corridors;
- the camera cannot spoil the maze from above. Wall height, fog and zoom limits are
  tuned here; this is the main open question about the look.

**M3: walls change daily.**
- The generator goes into `src/common`, shared by client and server, and reproduces
  the Python test vectors.
- The server sends the seed.
- The client generates the wall placements and adds them **through the same
  object-creation path the IFO's objects take**, so culling (bounds from mesh),
  residency and collision treat them as ordinary map objects. The generated walls
  have no lightmap; flat lighting suits a crypt, and Shibuya already ships unlit.
- The interior walls leave the IFO; the fixed parts stay.

*First investigation:* do the walls go in **before** the map loads, with the seed
sent ahead of the zone load on both entry paths (login and teleport)? Or can they be
added to an already-loaded map whenever the seed packet arrives? The second is more
robust to packet ordering and makes `/catacomb seed` live, but only if late insertion
into loaded patches is safe.

*Done when:*
- two clients see the same walls;
- `/catacomb reset` produces a different layout;
- the Python and C++ test vectors agree.

**M4: monsters respect walls.** The server checks a monster's straight-line move
against the closed edges. The walls lie on grid lines, so the check is a short walk
through the cells the line crosses. A blocked chase stops at the wall or gives up.
Aggro and leash can then be relaxed.

**M5 and later: PvP and polish, in any order.**
- `ZONE_PVP_STATE` 2 (everyone except your party). Check whether that mode needs a
  zone entry trigger to assign teams, as the clan fields do (gameserver `CLAUDE.md`,
  "PVP Ally Rule"), and whether the entrance hall needs protection from spawn
  camping.
- A minimap drawn from the seed: the UI2 minimap is RmlUi and could draw the edge
  bitset.
- Catacomb monster art, chests, several floors, a second crypt (`maze_id`).

## 8. Accepted limitations (v1)

- **Ranged attacks and area skills go through walls.** ROSE has no line-of-sight
  check anywhere.
- **Monsters can clip through walls until M4.** This is limited by keeping their
  aggro range and leash tight.
- **The server trusts the client's collision** for players, as in every zone. A
  modified client can walk through walls.
- **The usual minimap shows nothing useful.** No minimap until M5, which is arguably
  right for a maze.
- **Generated walls are unlit** (no lightmap).
- **A player offline at reset gets today's maze for free** (§4).

## 9. Traps from our codebase that apply here

- **A zone with no `.MOV` blocks every cell**, which silently disables monster
  leashing, wandering and fleeing. Ship an all-walkable `.MOV`.
- **Drop-table ids are zone ids**, so fill row 90 (§6).
- **The keeper's dialog** (`.CON`, QEX1) and **`UI_strID.ID`-style loose files** are
  client data. Ship client and data together, and bake after data edits
  (`scripts/pack.ps1`).
- **No `.bak` under `data/`**: `pack.rs` would bake it.
- **Restart the servers after any STB edit**; they cache tables at startup.
- **Write generator scripts as files, not bash heredocs**: `\n` and `\0` become real
  bytes.

## 10. Open decisions

1. **Wall art:** an existing wall mesh from our data for M2, or import the Jrose
   KCatacomb pieces straight away (46 files, 0.74 MB, walls only, without the floor
   parts)? This fixes the cell size.
2. **Toll amount**, and which hearts.
3. **Level band** for the monsters and the boss.
4. **Boss respawn:** once per day, or every few hours.
5. **Braid ratio and room sizes:** tune in M2.
6. **Reset time:** any fixed server time; a constant for now.
