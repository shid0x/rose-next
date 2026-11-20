### The Karkia Catacombs Are A Daily Maze Run By The Server (zones 90 / 91, opened to testers 2026-11-16)

Our own dungeon on Jrose's two crypt art sets: `scripts/import-catacombs.py` (with its
`catacomb_*.py` modules) plus a game-server controller
(`src/sho_gameserver/src/catacombs.*`, `catacomb_maze.*`). This page is how it stands
and how to run it; the design, every decision and the build history are in
[doc/karkia-catacombs-implementation-brief.md](../karkia-catacombs-implementation-brief.md).

**What a player meets**

- Four [Crypt Keeper]s stand at the Columbariums of the Desolate Cemetery (zone 87;
  Osric at (4809, 4790) m is `/tp 87 481 479`). All four lead to the crypt of the day.
- **An opening every two hours, on the even hours of the server's clock**: open for
  1 h 50, announced at 10, 5 and 1 minutes, then sealed for ten minutes and everyone
  inside is put back at the crypt mouth. A timer on screen counts it down (UI2 only;
  the classic interface gets the announcements).
- **The price is one Key of Remorse, once a day**, and it buys the opening running at
  that moment, with free re-entry until it seals. The key (LIST_NATURAL 150) drops in
  the Cemetery and the Burned Forest (20 monsters, lv 226-231 since the 2026-11-19
  re-level, `relevel-200-band.py`) and from the Spire Village's nine Alphas (lv 234-239),
  about one kill in twenty. A monster pays nobody ten levels above it, so a level-240
  character gets the key from the Alphas and from the Cemetery's level-231 monsters only.
  The Cemetery was briefly 230-235 so that it paid at 240 too; it went back down the same
  day because the player-level guide showed Karkia then only comfortable from 234, and
  Karkia is meant to open at 230. The Spire Alphas are the cap's key source.
- **One crypt a day, turn about**: the Catacombs of Reminiscence (zone 90, warm stone)
  and of Sorrow (zone 91, cold teal). **The maze is different every day**, a 10 x 10
  grid of 15 m cells with the entrance hall in one corner and the boss room in the
  opposite one. One shared zone, no instance, no PvP.
- Six monsters of levels 235-240 in the corridors (LIST_NPC 2550-2555). They do not
  see, call each other or cast through the walls, and they walk the maze to reach you.
- **The minimap draws the maze as it is walked** (UI2), and keeps what was seen while
  the day's walls stand.
- **A boss in the far room, one of five, drawn for the day.** It is there when the
  crypt opens, returns 15 minutes after a kill (a second timer), and is replaced by a
  fresh one when its room has been empty for a minute. It calls the dead at 60% and
  30% of its HP. Most of each fight is on the floor: red shapes to leave, green ones
  to stand in, and a line above the middle of the screen says what to do for the
  moves with one safe place.

  | boss | LIST_NPC | the fight |
  |---|---|---|
  | The Crypt Necromancer | 2556 | Grave Burst (a circle), Soul Lances (five lines at one player) |
  | Argentia, the Mad Giant | 2558 | never walks; the room's sixteen tiles, three cones, a disc then its ring |
  | Ferdinando, the Eternal | 2557 | iai cuts in blood: fans of lines, dashes through players, Severance |
  | Gladiomurus | 2559 | walls of swords that stay and cut the room up; Valhalla's Gate |
  | The Ikaness Greater Assassin | 2560 | Black Sun (circles somebody must stand in), Execution (stack in the mark) |

- **Loot (interim, 2026-11-13).** Each boss pays one class line of the level-240 gear:
  Necromancer staff / wand + the Eir set; Ferdinando one-handed sword / blunt + the
  lv240 shield and charms; Argentia gun / launcher / crossbow + Forseti; Gladiomurus
  two-handed sword / spear / axe + Fenrir; the Assassin katar / dual swords / bow +
  Sleipnir. Each also pays the red (lv230) weapon of its types, which nothing else
  drops. The corridors pay the plain lv230 weapons and OG armour.
  `scripts/add-karkia-drops.py` (`CRYPT_BOSSES`, `crypt_field`; tables 912-917, zone
  rows 90 / 91).
- Dying puts you back in the entrance hall, inside.

**Running it (GM, in chat)**

```
/item 12 150 10          ten Keys of Remorse
/tp 87 481 479           beside Crypt Keeper Osric
/catacomb                status: open or sealed, which crypt, time left, how many are registered
/catacomb open           the next opening starts now (earlier registrations are spent)
/catacomb pass [name]    that character may pay again today (no name: yourself)
/catacomb close          seal in one minute
/catacomb daily          one day later: the other crypt, a new maze, a new boss
/catacomb clock          back on the real clock
/catacomb boss <necromancer|argentia|ferdinando|gladiomurus|assassin>
/catacomb boss day       back to the boss drawn for the day
/catacomb boss           a fresh boss, the same one
/catacomb maze           the day's maze, drawn in chat
/catacomb seed [n]       rebuild the maze from a seed (no number: the day's own)
/tp 90 516 516           the boss room, from the code's numbers (zone 91 for Sorrow)
```

**A GM enters as a player does**: whoever stands in the crypt without a registration
for the running opening is put back at the crypt mouth, so pay the keeper first
(`/catacomb pass` after `/catacomb open` or for a second run in a day). The GM
commands only skew the controller's clock, so they run the real code; a forced boss
stays forced until `/catacomb boss day` or a restart.

**Putting a build up**

- Ship together: `rosenext.exe` + `znzin.dll`, `data.idx` + every `rose*.vfs`, the
  loose `3DDATA\rmlui\` files (minimap, zone timers) and `sho_gameserver.exe`. The
  packets are new on both sides (zone objects, timers, warnings, blink, alerts), and an
  older client does not know them.
- The database needs migration `0006-catacomb-entry`. Without the table the catacombs
  still run, but the day's registrations are forgotten at a restart.
- Data order: `import-catacombs.py`, then `add-karkia-drops.py --rewrite`, both with
  `--verify`, `audit-spawned-npcs.py`, then `scripts/pack.ps1`. Restart the servers.
- At start the game server logs `[catacomb] maze generator self-test`: the C++
  generator checked against the Python one's vectors.

Things that will bite:

- **Numbers that live in two places**, each pair to keep in step: the toll
  (`catacombs.cpp` `TOLL_*`, `catacomb_toll.py`), the keepers' spots (`KEEPERS`,
  `import-catacombs.py --emit` prints the table), every boss's skill rows and cast
  times (`catacombs.cpp`, the boss's `catacomb_<name>.py`), the maze generator
  (`catacomb_maze.cpp`, `catacomb_maze.py`) and the bosses' drop rows
  (`catacomb_pool.py` `DROP_TABLE`, `add-karkia-drops.py` `NPC_TABLE_FIX`).
- **The controller owns the boss and its adds**, not a regen point; the corridor
  monsters are ordinary regen points that re-roll against the day's walls.
- **The walls are zone objects the server also blocks on the walk grid**
  (`Nav::Grid::SetBlockMask`); both crypts share one layout, so bake both walk grids
  after any change to either map.
- **Cast speeds of a boss's animation rows are multiples of 10%**: the engine plays a
  clip at whole frames per second and the server times a mechanic from the cast.
- **A redirect in a drop table re-rolls over five cells and a blank one is a dead
  roll**: short lists repeat until the window is full.

Decided, not to be re-proposed: no PvP in the crypts (the corridors are too narrow),
players' area skills through the walls stay as they are, a party does not share its
map. Left: the loot's refinement, the bosses' own sounds.
