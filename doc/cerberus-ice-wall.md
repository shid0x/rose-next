# Cerberus Lair: the ice wall (dynamic zone objects)

Plan and running log. Started 2026-10-10. The lair itself is in
[cerberus-lair-brief.md](cerberus-lair-brief.md).

## Goal

An ice wall between the Warden of the Seal and the crater where Cerberus sleeps. It
stands while the Warden lives and fades away when it dies, so the crater cannot be
entered early. Light on the server; reusable for the Karkia catacombs maze.

## Facts the design rests on (surveyed 2026-10-10)

- **Client collision needs two things only**: the object is in the scene and a part
  carries a collision level from the ZSC (`zz_visible::gather_collidable`:
  `inscene && collision_level != ZZ_CL_NONE`). `CObjectMANAGER::Add_GndTREE(id, pos,
  rot, scale)` + `CObjFIXED::InsertToScene()` give both without a terrain chunk; the
  avatar's body collision (`GetCollisionStateAvatar` -> `StopMovingForCollision`)
  then stops it like any map wall. `CObjectMANAGER::Clear` on a zone change deletes it.
- **Fade**: `setVisibilityRecursive(root, 0..1)` sets the engine's object alpha;
  `CChangeVisibility` (gameproc) is a timed fader for characters to mirror.
  Risk: `get_transparent()` sorts by texture alpha only, so a fading mesh may draw in
  the wrong order for a moment. Cosmetic; checked in the test.
- **Model**: the lair uses `LIST_DECO_LP.ZSC` (LIST_ZONE col 11), which has
  `mauicewall01` (object 57, 83 x 36 x 44 m) and `mauice01-07` (44-50, 10-41 m),
  all with a colliding part. No art import.
- **The server never enforces a wall** (it checks a move's destination cell on the
  `.MOV` grid, nothing else), so a client-only wall matches every existing wall.
  The lair's `.MOV` is open everywhere; its rock walls are client collision only.
- **The "crater rim" is terrain**: a north-south ridge at x ~5068 m, 8 m high, with
  a few rocks on it; the bowl is open over the ridge and around its south end
  (`scratchpad/laircut.py`, a flood fill over the real collision footprint from
  `mapgen/catalogue.Footprints`). The line `(5068,5592) -> (5068,5500) ->
  (5112,5478)` (~140 m, ~35 m of it already rock) makes Cerberus unreachable from the
  Warden and leaves the west side intact.
- **Server hooks**: `classUSER::Send_gsv_JOIN_ZONE` (gs_user.cpp ~1494-1583) runs once
  per zone entry with the zone set; a packet sent after the join reply arrives before
  the sector objects. `CZoneTHREAD::SendPacketToZONE` is thread-safe and reaches a
  player who joined the same frame. Legacy ids `0x07f1-0x07fe` are free
  (`common/net_prototype.h`); `CLI_SUMMON_CONTROL` 0x0776 is the template.
- **Client**: `CNetwork::Proc_ZonePacket` dispatches legacy packets; the zone is
  always loaded before `CLI_JOIN_ZONE` is sent (`CGameStateMain::Enter`), so a
  packet after the join reply can create objects at once.

## Design

- **Packet `GSV_ZONE_OBJECTS` (0x07f1)**: `BYTE group; BYTE state; BYTE count;`
  then `count` x `{WORD decoId; float x, y, z (world cm); WORD rotDeg; WORD scalePct}`.
  The server carries the placements; the client is generic.
- **Server**: `CZoneTHREAD` keeps `group -> {placements, state}` (empty in every zone
  but the lair). `SetZoneObjects(group, placements, on)` updates and broadcasts;
  `Send_gsv_JOIN_ZONE` sends each non-empty group after the join reply. Nothing per tick.
- **Controller**: wall up when the run's monsters spawn (before the players arrive),
  down when the Warden dies (where Cerberus is spawned), cleared on `EndRun`/reset.
  Placements are constants next to `RUN_SPAWNS`, emitted by the placement script.
- **Client**: `Recv_gsv_ZONE_OBJECTS` -> `CZoneObjects`: state 1 creates the pieces
  (idempotent), state 0 fades ~1.5 s then deletes; cleared on zone change.
- **GM**: `/cerberus wall up|down`.
- Out of scope: server path enforcement; monsters respecting the wall (they collide
  with tree cylinders only).

## Steps

| # | Step | Status |
|---|---|---|
| 1 | `scripts/cerberus-wall.py`: place ice pieces along the cut, rasterise their real footprints, flood-fill, emit the C++ constants | done |
| 2 | Server: packet, zone registry, join hook, controller, GM command | built, untested |
| 3 | Client: handler + `CZoneObjects` (create, fade, delete) | built, untested |
| 4 | Test: `/cerberus wall up`, walk the ridge and the south end, relog inside, `wall down`, one real run | passed 2026-10-10 (all five checks); cosmetic change requested |
| 4b | Invisible panels on the ridge, ice only across the southern gap; re-test | passed 2026-10-10 |
| 4c | Single-spike crystals, two staggered rows, no see-through holes; re-test | passed 2026-10-10 |
| 4d | Cerberus asleep in view from the start (neutral team while sealed) | passed 2026-10-10 (two runs; cannot be focused from the ridge) |
| 5 | Commit; CLAUDE.md + brief | done 2026-10-10 |

## Log

- 2026-10-10: survey done (two code surveys, terrain/footprint raster, editor shots at
  (5060,5540), (5032,5560), (5032,5515), (5085,5560)); plan written.
- 2026-10-10, step 1 done: `scripts/cerberus-wall.py`. Pieces `mauice03/06/01` (46/49/44)
  every 12 m along the cut, rotation varied per piece, buried pieces (centre already in
  rock) dropped: **11 pieces, 414 blocked cells**; the Warden stays reachable from the
  start, Cerberus is cut off, and that holds with every ice footprint eroded by one
  cell (`--strict` is the default; 14 m fails it, 17 m fails even the plain check).
  Emitted table (`--emit`): deco id, world cm x/y/z, rotation deg:
  46 (5068,5592,9.9,150), 49 (5068,5580,11.1,60), 44 (5068,5568,9.6,180),
  46 (5068,5556,10.5,300), 49 (5068,5544,7.9,15), 46 (5068,5532,6.1,30),
  49 (5068,5520,7.6,255), 46 (5071.6,5498.2,2.9,165), 49 (5082.3,5492.8,0.9,270),
  46 (5093,5487.5,0.2,15), 49 (5103.8,5482.1,0.1,240).
- 2026-10-10, step 2 built: `GSV_ZONE_OBJECTS` 0x07f1 + `tagZONE_OBJECT` in
  `common/net_prototype.h`; `CZoneTHREAD::SetZoneObjects` / `SendZoneObjects` /
  `IsZoneObjectsOn` (mutexed map, `SendPacketToZONE` broadcast); `Send_gsv_JOIN_ZONE`
  sends the standing groups after the join reply; `CCerberusLair::SetWall` (up in
  `Draw`, down on the seal breaking and in `EndRun`), `GmWall`, `/cerberus wall up|down`.
- 2026-10-10, step 3 built: `src/client/zoneobjects.{h,cpp}` (`CZoneObjects`: `Apply` builds
  pieces with `Add_GndTREE` + `InsertToScene`, idempotent up; down fades 2 s with
  `setVisibilityRecursive`, still blocking, then `Del_Object`; `Forget` from
  `CObjectMANAGER::Clear`; `Update` from `CObjectMANAGER::ProcOBJECT`),
  `Recv_gsv_ZONE_OBJECTS` + dispatch. World cm straight through
  (`MAGNIFICATION_RATE` is 1).
- 2026-10-10, step 4: **everything works end to end** (user): `wall up`/`down`, the fade,
  a full run with the wall dropping on the Warden's death. Cosmetic request: ice
  everywhere on the ridge looks wrong; invisible walls on the high ground, ice only
  at the southern chokepoint.
- 2026-10-10, step 4b built: `tagZONE_OBJECT` gained `m_btKind` (1 = the map's invisible
  collision panel, `LIST_DECO_SPECIAL` object 2 through `Add_CollisionBox`) and per-axis
  scale. The script's `SEGMENTS`: panels every 8 m on the ridge (1 m thick, 14 m tall,
  sunk 4 m so a slope leaves no gap under them; two dropped inside rock), crystals
  every 12 m across the southern gap: 10 panels + 5 crystals, sealed, strict too (the
  erosion now applies to crystals only -- a one-cell panel line would erode to nothing).
- 2026-10-10, step 4b passed (user): invisible walls hold on the ridge, ice appears and
  fades. Cosmetic request: the crystal clusters have see-through holes between shards.
- 2026-10-10, step 4c built: the ice segment uses only the single-spike models
  (`mauice05` 48 and `mauice07` 50 at 60 % height; the base outlines at 50 cm showed
  44/45/46/47/49 as several shards with open ground between them), every 5 m in two
  rows 3.5 m apart, staggered: 20 spikes + the 10 panels, sealed, strict too. Server
  data only; the packet grew to 30 records (690 bytes).
- 2026-10-10, step 4c passed (user: "Perfect"). Then: with a real seal, Cerberus no
  longer needs hiding -- it sleeps in full view from the run's start as a tease and a
  direction. Risk found first: Hawk Shot / Aim Point (36-37 m) reach a scale-8 Cerberus
  from the ridge (40 m), a hit wakes it, and monsters do not collide with the panels,
  so it would charge through the wall and the Warden could be skipped; and the sleeping
  form has its own HP, so it could be chipped to death from the ridge, ending the run.
- 2026-10-10, step 4d built: the sleeper is spawned with the whelps and the Warden on
  `TEAMNO_NPC` (1): `Is_ALLIED` makes team 1 allied with everyone (same team, or a
  product <= 100), so it cannot be attacked (server `Recv_cli_ATTACK` refuses allied
  mobs, client `IsEnemy` too) and its "enemies near" wake (AICOND02 counts enemies)
  never fires. No AI change. On the seal break the neutral sleeper is removed silently
  (`Sub_DIRECT` + `delete`, the GM `npc DEL` pattern: the remove packet, no death) and a
  `TEAMNO_MOB` sleeper is spawned in its place; a kill counts only after the seal
  breaks. There is no live team-change packet, hence the swap.
- 2026-10-10, step 4d passed (user, two runs; the sleeper cannot be focused from the
  ridge); both logs clean. Step 5: CLAUDE.md ("Dynamic Zone Objects" + the lair's
  bullets), the brief, this log; committed.
