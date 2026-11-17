# Pathfinding and Z support: implementation plan

Status: plan only, nothing built (2026-10-28). It was put together from:
- a seven-report research sweep;
- three competing designs (our own layered grid, Recast/Detour, and a value-first version), each scored by three reviewers;
- two fact-checking passes against the code and data.

The full research is archived locally in `build/nav-research/` (not in git): `reports/` holds the seven reports, `design/` the proposals, verdicts and fact-checks, and `scripts/` the measurement scripts.

Path prefixes: `S/` = `src/sho_gameserver/src/`, `C/` = `src/client/`, `E/` = `src/engine/src/`. Other paths are from the repo root.

## 1. Executive summary

**What we build.** An offline Python bake (over `scripts/rosefmt` and `scripts/mapgen`) turns each zone's terrain and collision meshes into `<ZONFOLDER>.lnav`: a **layered walk grid with 1 m cells**, each cell listing the floors ("spans") at that X/Y. The game server uses it three ways:
- as a **height oracle**;
- for a **vertical band** on AoE and aggro (later, attack range);
- as the **A\* graph** for monsters and summons.

A zone without a file behaves as today.

**Key choices** (the alternatives are in §3.0):
1. **Data:** our own layered grid; Recast/Detour is the fallback.
2. **Floors:** slope plus reachability from seeds, never the collision flag.
3. **Z:** every vertical verdict uses the Z the client draws.
4. **AoE:** the 2D shape plus a flared band, |dz| ≤ 5 m + 0.7·d. A sphere measured worse: it loses 6–7% of hillside hits, and excludes fewer cliffs than a band that loses 1–2.4%.
5. **Paths:** straight first, A\* only when blocked, with waypoints sent as target-less `CharacterMove`s.
6. **Players:** never pathed; `m_nPosZ` untouched.
7. **Safety:** a switch and a live GM toggle for every behaviour, rolled out per zone.

| Phase | Ships | Client? | Player notices |
|---|---|---|---|
| 0 | Meters and a waypoint presentation probe | no | nothing |
| 1 | Bug fixes: AoE scan, Z reads (MOV docs only; MOV is retired in Phase 4) | no | AoE hits the whole pack every time |
| 2 | Bake; the server loads it and tracks Z | no | nothing (`/nav` GM tool) |
| 3 | Vertical band and roof-spawn rejection | no | **AoE from a bridge or cliff stops hitting monsters far below; monsters below stop aggroing; no monsters on Muris/Zant roofs** |
| 4 | Idle moves and spawns respect walls | no | idle monsters stop walking through houses, trees and cave walls; nothing spawns in rock or outside the Goblin tunnels |
| 5 | Pathfinding | no | **monsters and summons go around obstacles** |
| 6 | The client draws monsters on the server's floor | lockstep | monsters under a bridge stay under it; none on invisible wall tops |
| 7 | 3D attack/cast range, aiming onto decks (optional) | lockstep | attacks respect floors; ground AoE can target a deck |

**Cost (estimates):**
- **Code:** about 4,500 lines of C++ (about 700 of them client-side) plus 1,500 lines of Python.
- **Memory:** 45–55 MB of nav data (bound 75 MB) plus at most 27 MB of search scratch, in a server that is not large-address-aware.

## 2. What the research found

The numbers come from read-only scripts over `rosefmt` (see the Appendix).

**Server movement**
- **2D, straight lines, no clamp.** The server ticks at 10 Hz (`S/gs_threadzone.cpp:93-94`) and moves objects with one line, `m_PosCUR += m_MoveVEC * dt`. There is no walkability test and no arrival clamp (`S/cobjchar.cpp:2074-2091`). Arrival is tested after the step (`S/common/cobjai.cpp:618-633`), so a move overshoots by up to one step: about 33–85 cm at 100 ms for run speeds of 330–850 cm/s, about half that on average.
- **Object work stretches the tick.** The zone waits out 100 ms minus its pre-work, and only *then* runs every object's `Proc` (`S/gs_threadzone.cpp:175-180, 187-218`). Any time the planner spends lengthens the tick, and the uncapped `dwPassTIME` then lengthens every step. None of this is instrumented.
- **Scale.** About 67 zone threads run in one process. Their 6,103 regen points have summed caps of 13,868 monsters; Sikuku alone has 2,118 points with cap 1.
  - That is the cap sum, not a simulated population: the real count can exceed caps where `tacticPoint` is low (`scripts/CLAUDE.md`). Phase 0 counts live monsters.
  - The process is **not large-address-aware** (PE flags 0x102; no vcxproj sets it).
- **Thin walkability checks.** `IsMovablePOS` checks only the destination cell of `SetCMD_MOVE2D` moves (`S/cobjchar.cpp:921-935`), and it treats an off-grid X as walkable (`S/zonefile.h:131-136`). Chases, players, CTRL+click and spawns bypass it entirely.
- **No Z anywhere.**
  - Spawns are uniform in a ±range square with no checks (`S/gs_threadzone.cpp:761-786`).
  - IFO Z (regen, NPC, event objects) is skipped unread with an `fseek` (`S/zonefile.cpp:163`). ZON event-point Z is read and discarded (`:501-504`), and `tagEVENTPOS` has no Z (`S/zonefile.h:12-16`).
  - `.HIM` is never opened, and monster `m_nPosZ` is always 0 (`S/cobjnpc.cpp:71`).

**MOV mirror.** `LoadMOV` puts file row 0 at the tile's south edge (`S/zonefile.cpp:310-321`), but the data is north-first, like HIM. With the rows flipped:
- regen points on blocked cells fall from 85/736 to 0/736;
- the share of LZ01 wall cells that are blocked rises from 55% to 100%.

The proof is statistical; nobody has checked it in game yet. The same south-first reading is written into:
- `scripts/rosefmt/chunk.py:6`;
- `docs/mapgen/FORMATS.md:418-426`;
- `xadet/rose-online-map-editor/Map Editor/Engine/FileManager/Map/MOV.cs:5`, with its tests in `xadet/rose-online-map-editor/tests/MovementTests.cs`.

**MOV coverage is thin.**
- Only 18 zones have painted MOVs.
- A 19th, ODRP01, has 2 stray blocked cells and is counted with the 32 effectively all-zero zones (Shady Jungle, Sikuku, Eldeon…).
- 16 zones ship none.

The painted cells are brush bands 10–15 m wide along walls. **MOV cannot be the nav source.**

**What the client draws** (everything else has to fit around this)
- **The client re-snaps monsters and NPCs to `GetHeightTop` every frame.**
  - `GetHeightTop` = max(terrain, highest hit on any non-NOTMOVEABLE colliding node) (`C/cobjchar_collision.cpp:321-358`, `E/zz_interface.cpp:9067-9152`).
  - It covers objects in the frustum, and those within 40 m of the avatar (`C/cobjchar.cpp:5776-5823`). Others keep their last Z.
  - It samples at the model's centre of mass, not the server X/Y.
  - Spawns use the same rule via `DropFromSky` (`C/cobjchar.cpp:5150-5158`).
  - Any server Z for a monster is ignored.
- **The local avatar uses a relative rule:** the highest surface ≤ foot + 135 cm, no climbing at 54° or steeper, never below the terrain (`C/cobjchar_collision.cpp:1355-1469, 1525`). Unlike monsters, it **keeps flag-12 nodes** (:617-622). Its ray test is two-sided.
- **Monsters collide with nothing on the client** except 116 tree cylinders in imported tables. Server X/Y waypoints therefore need no client change.

**Where the layers really are**
- **Today's roof spawns happen in towns, not caves:**
  - Muris: 16.5% of spawn samples (9 of 37 points; one at 100%, up to +53 m);
  - Zant: 3.6%;
  - Fossil Sanctuary: 13 of 123 points;
  - Sikuku: 11 of 2,118 points.

  Goblin Cave and Shady Jungle are at 0%, because their ceilings and tree crowns are flag 12. Counting flag-12 surfaces as floors would put 40% of Goblin B1 spawn samples on ceilings.
- **Bridges are common:** 21 zones have a walk-on bridge at least 5 m up. Examples: Shady `dari` (deck +62–73 m), Zant +47 m, Eucar +45 m, Gorge +44 m, Arumic +37 m, Goblin B1 `domdari` (about +108 m), B2 +27 m, Ulverick +22 m.
- **Lump-11 collision panels are upright walls that are also walk-on (flag 4).**
  - Of 3,125 panels, 31 pass the bake's cos 54° surface test (17 JD04, 7 LP05, 5 VALLEY002, 1 ODGR01, 1 TOWN). The rest are walls.
  - Shady Jungle has 66 panels. 37 of them are 78.8 m tall and 4.5 m thick, centred on the ground, so a monster standing inside one is drawn about 39 m up. The others are 7–60 m tall.
  - The panels account for 27,619 m² of that zone's 31,519 m² "overhang".
  - The Cerberus lair's 7 panels may explain the hounds that were "heard, never seen".
  - Do not reflag `LIST_DECO_SPECIAL` object 2 to 12: the flat panels must stay walk-on.
- **Shady Jungle is mostly cliffs.** 81 of 392 regen points have terrain more than 10 m higher within 20 m. Regen centres are 187–200 m from the bridge.
- **Goblin Cave.** The floor is terrain. Walls and ceilings are single-sided, down-facing flag-12 meshes. B1's chamber stacks four surfaces at one X/Y. B3 has two regen points on its altar (+8.4 m and +8.8 m). Spawn squares spill 4–19% outside the tunnels.
- **Regen Z is a good layer hint** (93.9% within 50 cm of the terrain). The exceptions are 195 z = 0 points off the terrain: 190 written by importers (Desolate Cemetery 113, Spire Village 77), 4 in Muris and 1 in Ulverick.

**Range and AoE**
- **One test per scan.** Every circular scan is decided at `S/gs_threadzone.cpp:991`. All area damage goes through `Skill_DamageToAROUND` (`S/cobjchar.cpp:1628-1735`), which never re-tests plain circles.
- **The scan iterator is shared per zone and is not re-entrant** (`S/gs_threadzone.h:78-83`). The chain `Apply_DAMAGE` → `Do_DamagedAI` (`S/cobjchar.cpp:1223`) → `AI_Check` → COND02 → `FindFirstCHAR` overwrites the outer AoE scan. 238 of 541 AIP files scan on DAMAGED. This was traced in code, not observed in game.
- **Reach is capped by sector hysteresis:** 0.8·s around a point and 0.6·s around a character (40 m and 30 m in 46 zones), while skill radii reach 50 m (median 11.6 m).

**Protocol**
- **Waypoints must be target-less `CharacterMove` packets (74 bytes).**
  - Anything that carries a target makes the client home on it. `CharacterMove` goes through `C/network/cnetwork.cpp:1053-1077` into `CObjAI::SetCMD_MOVE` (`C/common/cobjai.cpp:855-862`); spawn packets go through `Recv_tag_ADD_CHAR` (`C/network/recvpacket.cpp:1134-1163`).
  - Both builders always write the target (`S/network.cpp:97`, `:118`).
- **The client never speeds up to catch up** (`C/cobjchar.cpp:142-206`).
  - Each corner costs either a 0–100 ms stop or a cut.
  - A non-player that falls more than 1 s of walking behind the server snaps forward, the known "position snapped forward" teleport (`:168-195`).
- **Monster and NPC spawn packets carry no Z.** Their variable-length tail forces any new field into the fixed part (`net_prototype.h:898-910`).
  - `gsv_AVT_CHAR.m_nPosZ` exists but is never written.
  - `Vec3.z` is always 0.
- **Player Z is unreliable:** `CANTMOVE` reads the wrong struct (`S/gs_user.cpp:2836`), and the client never writes the `JOIN_ZONE` Z.
- **`m_nPosZ` is broadcast to other clients** (`S/cobjchar.cpp:318`, `C/network/recvpacket.cpp:1885`).

**Tooling**
- **How data reaches a production server is not recorded in the repo.**
  - The repo's server dist script, `scripts/server-data.ps1:11-14`, copies only `*.him/*.mov/*.ifo/*.zon`, so no map meshes.
  - Its only caller is `scripts/dist.ps1:74`, which reads `bin/assets`. That folder does not exist in this checkout.
  - The game server loads from `server.toml`'s `data_dir`; dev servers read `data/` directly.
  - The bake therefore has to be a server-side artefact next to the ZON. Decision 14 asks how it gets deployed.
- The config FFI reads only ints and strings (`src/common-lib/src/ffi/toml.rs:36, 68`).

## 3. Design

### 3.0 Where the proposals disagreed

| Question | Chosen | Over | Why |
|---|---|---|---|
| Nav representation | layered 1 m grid | Recast/Detour | Recast's RAM (60–250 MB, unmeasured) is the largest risk in a non-LAA server, and its bake needs two languages. Floor classification is custom either way. Recast stays the fallback |
| Go/no-go experiment | `/nav trail` probe (Phase 0) | A\* on the painted MOV | MOV is unfit as nav data, and costs measured on 5 m cells do not carry over to 1 m. The probe tests corner presentation, the real risk, with no data at all |
| Monster Z before the client change | visual top (what is drawn) | nav continuity | keeps the server consistent with the screen |
| AoE vertical rule | flared band, plus an opt-in same-surface check | cylinder + surface check; Recast's gate; sphere | measured (§3.4). The surface check must ignore walls, or trees on a hillside exclude targets |
| Z on the wire | `gsv_MOB_CHAR` | `tag_ADD_CHAR` | `tag_ADD_CHAR` is also the base of `gsv_AVT_CHAR`, so player spawn packets would change too |
| Player server Z | new `m_nNavZ` | `m_nPosZ` | `m_nPosZ` is broadcast to other clients |
| Path state | pointer on the server `CObjCHAR` | side table | the class is server-only; a side table risks object-index reuse |
| Monster attack band | Phase 7, with the player band | Phase 2 or 4 | a band on one side only creates safe spots |
| Unreached floors | kept as Z-only (up-facing only) | dropped | players can stand on them; ceilings are still dropped to bound RAM |
| Whose view of a player's Z | the owner's (their own client keeps flag-12 floors) | observers' (they skip flag 12, `C/cobjchar_collision.cpp:625-633`) | a monster AoE is judged on where the targeted player sees themselves |
| Summon follow before A\* | straight fallback | line gate only | a summon must never be stranded |
| Spawn spread | square, as today | reachable circle | no visible change to how spawns spread |
| Bake versions | one bake, consumers staged | heights first, walls later | one body test covers both walls and headroom |

### 3.1 Nav data and bake

**Cells.** 1 m cells, each holding sorted spans `{int16 z_cm, u8 flags, u8 clearance}`.
- 50 cm cells would cost about 160–200 MB.
- 2.5 m cells leak one-cell faces.
- At 1 m, the avatar's 135 cm step equals a 53.5° slope on orthogonal links, which matches its 54° rule. Diagonals keep the 135 cm limit (about 44°) and need both side links.

**Bake.** The CLI is `scripts/nav-bake.py`, the logic lives in `scripts/mapgen/navbake.py`, and the codec is `scripts/rosefmt/lnav.py`, with a round-trip test.
- Game files are read only through existing codecs: LIST_ZONE columns 1/11/12, ZON event points, the stitched HIM (`prefab._zone_field`), IFO lumps 1/3 via `relight.Zone.placed()`, lump 11 as `LIST_DECO_SPECIAL` object 2, and water as a flag.
- **New readers are needed** for lump 10 (warp gates) and lump 12 (event objects). Each applies the same `objlight.client_accepts` filter:
  - warp gates are `LIST_DECO_SPECIAL.ZSC` object 1, collision 4, so walk-on (`C/object.cpp:588-620`);
  - event objects come from the event ZSC the client loads into `m_ModelEvent`.

  `relight.Zone.placed()` covers lumps 1 and 3 only (`scripts/mapgen/relight.py:283-297`), and `catalogue.Footprints` knows only the OBJECT, CNST and COLLISION ZSCs.
- ZSC parts with collision ≠ 0 go through `catalogue._to_model` (following the parent chain), together with their cylinders and ZMS triangles.
- MORPH is reported but not used.
- The server reads only our `.lnav`.

**Passes:**
1. **Terrain span** per cell: bilinear HIM, as in `CMAP::GetHEIGHT` (`C/io_terrain.cpp:1947-1957`) over `GetHeightsByCoordinates` (`:2890-2915`). It always exists.
2. **Classify each colliding triangle by its normal, two-sided:** a surface if |n_z| ≥ cos 54°, otherwise a wall. Flags do not decide.
3. **Rasterise surfaces** (at 33 cm or finer) and cluster each cell's samples by z; a gap over 50 cm starts a new span.
4. **Body test.** A span is blocked if a sample of another surface or wall lies 30–180 cm above it. This catches walls, low slabs, rock interiors and panel footprints. HEIGHTONLY parts (flag 36, stairs) never block; cylinders block discs.
5. **Links** join walkable neighbouring spans whose |Δz| ≤ 135 cm on terrain and HEIGHTONLY stairs, and ≤ 45 cm (about 24°) where an object floor is involved. (Since 2026-10-29: 60 cm, or up to 135 cm on a ramp or flight whose next floor starts within 60 cm of this one's top; terrain links out of a 54° quad only downhill. See Phase 5's B3 notes.)
   - The avatar's forward body sphere (45 cm up, 40 cm ahead, radius 30) stops it against any mesh rising more than about 20°, so a rock face is a wall even when it is not steep.
   - With 135 cm everywhere, the first Goblin Cave B1 bake leaked out of the tunnels over a 7 m rock (2026-10-28, corrected).
6. **Flood** from seeds: event points (start, revive and every warp landing), regen centres and NPCs, plus `scripts/nav/seeds/<ZONFOLDER>.json` (in git).
   - **Not warp gate records** (IFO lump 10): a gate's pivot can stand past the end of its corridor, outside the walls. One in Goblin Cave B1 seeded the whole outside (2026-10-28).
   - **Seed snapping:** first to where the client puts the seed today. Every spawn, NPC and warp landing is dropped onto `GetHeightTop`, so where the cell's visual top is raised, the seed takes the walkable span at most 3 m under it.
     - Muris' plaza is a platform 3.9 m over flat terrain, and z = 0 regen points had seeded the hidden ground beneath it (96,981 hazard floors; 164 after the fix).
     - Otherwise: the span nearest the authored Z within 3 m; then the terrain; then the nearest cell within 3 with a span within 3 m of that Z. Height comes before distance, because a Sikuku regen point on a pillar had snapped onto the roof 20 m up and seeded it (307,983 two-floor cells; 1,460 after the fix).
   - Reached spans get a component id.
   - Unreached **up-facing** spans are kept as Z-only. A player may stand there after a fall; paths and spawns ignore them.
   - Unreached down-facing spans (cave ceilings) are dropped and only provide headroom.
7. **Clearance class:** 1 = beside an obstacle, 2 = at least 1.5 m clear, 3 = at least 2.5 m. Ledges count as obstacles.
8. **Visual top and HAZARD.** For each cell, store where today's client draws a monster, using a `GetHeightTop` replica: an up-ray from z = 0 taking the farthest hit, otherwise a down-ray taking the nearest, over non-NOTMOVEABLE nodes only. It is stored sparsely, and a span whose visual top is more than 3 m above it is flagged **HAZARD**.

   The replica is close but not exact:
   - MORPH objects and runtime `GSV_ZONE_OBJECTS` are in the client's scene but not in the bake;
   - the client samples at a model's centre of mass, so large models at a ledge or deck edge can resolve a different layer than the server X/Y.

   Expect small drift there.
9. **Regen anchors:** each regen point's resolved span and Z.

**Result:**
- Goblin ceilings only supply headroom.
- Crowns and panel tops are unreachable.
- A roof counts only if stairs reach it.
- The `dari` deck, B1's bridges and the B3 altar become second layers.

**File.** `<ZONFOLDER>.lnav` sits next to the ZON with a **lowercase** extension, because the `pack.rs` globs are case-sensitive (`scripts/pack.ps1:50-61`).
- **Header:** version, bake parameters, CRC32 of each input HIM/IFO/ZON, and a tile table.
- **Per 160 m tile:**
  - terrain heights, 65×65 int16 (8,450 B). The server samples terrain from here, matching the client to the centimetre;
  - a 2-bit simple-cell mask (6,400 B): 0 = terrain not walkable, 1–3 = walkable main-component terrain with its clearance class;
  - a complex-cell bitmap and rank (3,520 B), plus span lists for every other cell;
  - sparse visual tops and sparse component ids.
- **Optional trailer:** DECO wall footprints, for dynamic objects.

**RAM.**
- **Total:** about 45–55 MB, bounded at about 75 MB.
  - 29 MB is fixed: 18.4 KB × 1,588 unique tiles. The bake covers 66 ZON folders for 67 zone rows, because zones that share a ZON load one read-only copy.
  - 15–25 MB is sparse data.
- **A\* scratch:** about 0.4 MB per zone thread.
- **Levers if memory is tight:**
  - drop Z-only spans more than 20 m above the highest reachable span in their column;
  - use 2 m cells in open zones;
  - enable zones one at a time;
  - make the server large-address-aware: one linker flag, giving 4 GB on 64-bit Windows. The earlier "LAA parked" decision covered only the client, so this is a separate question (decision 12).

**Shipping and checks.**
- **Shipping:**
  - the bake writes into `data/` next to each ZON, so dev servers pick it up with no change;
  - `pack.ps1` excludes `"*.lnav"` and `"*.LNAV"`;
  - the server runs locally on `data/`, so writing the file is the whole deploy (decision 14);
  - reports go to `build/nav/`.
- **Staleness:** a CRC mismatch logs `nav: zone N stale`; with `stale_disable = 1` the zone falls back to today's behaviour. `--check` lists stale zones.
- **Per-zone report and PNG:** failed seeds, unseeded islands over 500 m², spawn spill, HAZARD area.
- **MOV agreement:** the share of painted-blocked cells (after the flip) that the bake also blocks. The research expects 82–100%.
- **Deliberate mistakes** (see `doc/jrose-catacombs-collision-investigation.md`): without lump 11, the Shady edge walls must vanish; with one Goblin wall removed, the leak check must fire.
- **Offline A\*:** `nav-bake.py --path` cross-checks the server's paths.

### 3.2 Server runtime pathing

**Code.**
- New `S/nav/` with three units: `nav_data` (load, `SpansAt`, `FollowZ`, `SnapZ`, `VisualTopZ`, `LineWalk`, `SurfaceLinked`), `nav_path` (A\*, smoothing, per-zone search context) and `nav_agent` (leg follower, timers, budgets).
- The server `CObjCHAR` (server-only header) gains `short m_nNavZ` (−32768 = unknown) and `CNavAgent* m_pNav` (pooled; null when not on a path). This is one server-only layout change, so `sho_gameserver` needs a clean rebuild.

| Movement | Phase 4 (walls, no A\*) | Phase 5 (A\*) |
|---|---|---|
| Wander AIACT_03/04 (and AIACT_05, a move to a found character) | the destination must snap to a walkable span and `LineWalk` must succeed; at most 3 tries, then skip this idle tick | same; **wander never runs A\*** |
| Flee AIACT_08 | shortened to the last walkable point on the line | same |
| Leash `Run_AWAY` | a point reachable from home (at most 6 tries), otherwise straight as today | A\* home |
| Summon follow (`CObjSUMMON::Proc` loop, `S/cobjnpc.cpp:801-831`, and AIACT_29, `S/ai_lib/ai_action.cpp:846-867`) | line-checked, with a **straight fallback** so it is never stranded | A\* when blocked |
| CTRL+click move (`PlayerOrderMoveTo`, `S/cobjnpc.cpp:684-695`) | unchanged | A\* to the click, snapped within 5 m, otherwise straight; never refused |
| Chase, skill approach, CTRL+click attack | straight | line check, then A\* legs |
| Players | not pathed; Z tracked | not pathed |

**Hooks:**
- **`Goto_TARGET`** (`S/common/cobjai.cpp:606-615`) follows an active path and must not call `Restart_MOVE(pTarget->m_PosCUR)` while it does.
  - The target's position differs from the waypoint in `m_PosGOTO`, so the same-goal early return (:262) never fires.
  - The leg would otherwise be replaced by a straight move toward the target on every tick.
- **`ProcCMD_ATTACK`'s out-of-range branch** (:715-720) needs its own hook. It sets `m_PosGOTO` and calls `Start_MOVE` directly, bypassing `Goto_TARGET`.
- **`ProcCMD_MOVE` / `ProcCMD_PICK_ITEM`** (the latter also runs `CMD_RUNAWAY`) start the next leg on arrival.
- **`SetCMD_MOVE2D`** gets the nav gate (one point in, accept or refuse).
  - The **re-roll on refusal** belongs in the callers, which each draw a single point today: `F_AIACT03/04` (`S/ai_lib/ai_action.cpp:71-124`), `F_AIACT08` and `CObjMOB::Run_AWAY` (`S/cobjnpc.cpp:457-475`).
  - `PlayerOrderMoveTo` gets its own hook, because it bypasses `SetCMD_MOVE2D`.
- **`Init_ADD_CHAR`** (`S/cobjchar.cpp:732-750`) reports a monster on a leg as `CMD_MOVE` with target 0 and the current waypoint.
- **`Resync_MoveSPEED`** (:345-379) re-sends the leg with no target. It must also cover `CMD_RUNAWAY`: today it only re-sends for `CMD_ATTACK`/`CMD_MOVE` (:358-361), so a leash path that is slowed mid-way would drift.
- **`RealProc`** gets the step clamp and carries leftover time into the next leg (at most 2 legs per pass).
  - Movement dt gets **its own** 500 ms cap, tracked per `Proc` call. `Proc` splits one tick's `dwPassTIME` into several `RealProc` calls at motion-loop boundaries (`S/cobjchar.cpp:2209-2231`).
  - Do not cap `GetPassTIME` globally: the same value drives status timers.

**When path state is dropped:**
- on a **target change**, or when the **goal moves more than 4 m**;
- **when a cast starts**, because type-7 aim reuses `m_PosGOTO` (`S/cobjchar.cpp:2059-2063`).

It is **not** dropped on every `SetCMD_*`.
- `CObjSUMMON::Proc` re-issues `SetCMD_MOVE2D` every 300 ms while following (`S/cobjnpc.cpp:806-829`), and AI attack-move patterns re-issue `SetCMD_RUNnATTACK` on the same target. Dropping on each would re-plan constantly and burn the search budget.
- The follower's own legs never go through `SetCMD_*`.

Exclusion flags are checked before any move is issued.

**Chase.**
- Every 300 ms (staggered by index % 3), a chaser runs `LineWalk` toward its target. The line counts as clear only if it **ends on the target's own span**: a line from the ravine reaches the X/Y under a player on the deck, but ends on another span.
- A blocked line triggers A\*. A target in a different component is unreachable at once, with no search.
- While on a path, every 300 ms:
  - if the line has cleared, go back to a straight chase with one `CharacterMoveAttack`;
  - if the target is more than 4 m from the planned goal, re-plan.

**A\*.**
- 8 neighbours, octile costs 10/14, heuristic weighted ×1.2.
- A 128×128 window, with the start kept at least 8 cells inside it; at most 4 layers per cell.
- Generation-stamped arrays and a binary heap. When the search is exhausted it returns the best partial path, which is accepted only if it ends at least 2 m closer.
- Start snaps within 2 m, goal within 3 m.
- **Soft clearance:** a cell below the agent's preferred class costs ×3, so bosses prefer wide routes but never get stuck. The preferred class comes from NPC scale: under 120% → 1, up to 250% → 2, above that → 3.
- **Smoothing:** greedy string-pulling into at most 16 waypoints that carry Z, preferring class-2 cells. It is **Z-aware**: a leg is kept only if the span Z along it stays within ±100 cm of the straight chord.

**Budgets** (server.toml):
- 4,000 expansions per search.
- Per zone tick: 6 searches, 8,000 expansions and **3 ms of nav time measured with QPC**.
- Over budget: keep the current leg and retry next tick.
- A failed search walks straight for 2.5 s. This keeps today's reach, so pathing alone creates no safe spots.
- `NavStats` per zone every 60 s.

**Corners.**
- Leg k+1 is sent once no more than speed × `leg_lead_ms` of leg k remains (default 100 ms, 0 = off). The server itself still clamps exactly at the corner.
- The client cuts the corner by about 55 cm, which class-2 waypoints absorb, and each leg end re-syncs the two sides.
- A busy corridor costs about 2.2 KB/s per player.
- **Watch the 1 s forward snap.** A slow, a deferred leg (over budget) or a 2.5 s straight fallback can leave the client copy more than 1 s of walking behind. That shows up as the "position snapped forward" teleport, not a corner stop.

**Spawns.** `SnapSpawn` serves every spawn site:
- `RegenCharacter`/`RegenMOB` (`S/gs_threadzone.cpp:761-786, 693-719`);
- `Create_PET` and skill summons (`S/cobjchar.cpp:172-200, 1948-1955`);
- REWD_008 (`src/common/shared/io_quest.cpp:1667-1722`);
- Cerberus (`S/cerberus_lair.cpp:424-485`).

It draws in today's ±range **square** around the baked anchor. It rejects HAZARD spans (Phase 3) and points that `LineWalk` cannot reach from the centre (Phase 4). After 6 tries it uses the centre. A point that always falls back needs a data fix.

**`SnapSpawn` can run on another zone's thread.** REWD_008 with `btWho == 2` spawns into a different zone through `CZoneLIST::RegenCharacter(..., true)` (`S/zonelist.cpp:261-270`, `src/common/shared/io_quest.cpp:1718-1725`). That calls the target zone's `RegenMOB` on the caller's thread. Route such spawns through the delayed `m_MobCALLED` queue (`bImmediate = false`), so the target zone's dynamic overlay is only ever read by its own thread.

**Dynamic obstacles.** `SetZoneObjects` (`S/gs_threadzone.cpp:461`) holds a mutex, so it only queues the change. The zone thread applies the queue at the top of its Proc loop to a sparse overlay:
- kind-1 panels are rasterised from their placement;
- kind-0 objects use the footprint trailer.

**Config and GM tools.**
- `[nav]` sits beside `[game]` (`src/common/include/rose/common/server_config.h:142-146`). It uses ints and strings only (`aoe_band_slope_pct = 70`, `path_zones = "31,32,33,56,62"`), so the Rust FFI is unchanged.
- GM commands:
  - `/nav`: the spans, visual top and component at your position, plus the last client hint;
  - `/nav <switch> on|off`;
  - `/nav path x y`: marker items at the waypoints for 15 s;
  - `/nav stats`.

### 3.3 Z model

**Principle: every vertical verdict uses the Z the client draws.** Otherwise the server contradicts the screen: an AoE cast on a deck would miss a monster drawn on that deck.

| Entity | Stage A (Phases 2–5, unmodified client) | Stage B (Phase 6 on) |
|---|---|---|
| Monster, summon | `VisualTopZ(x, y)`, i.e. what `GetHeightTop` draws, looked up only when a band test needs it | `FollowZ` from the spawn span, kept in `m_nNavZ` and sent on the wire |
| Map NPC | `VisualTopZ`, as for monsters: the client draws `OBJ_NPC` through `AdjustHeight_Monster` (`C/cobjchar_collision.cpp:76-84`) | the authored IFO Z, snapped to a span |
| Player | `m_nNavZ`: `FollowZ` along the server's segment, re-anchored by client hints. Every span counts, Z-only and flag-12 spans included (the owner's view; observers skip flag 12). On zone-in: the visual top at the arrival point (mirrors `DropFromSky`) | same, plus the `JOIN_ZONE` Z once the client writes it |
| AoE centre | type 17: the caster's Z. Player type 7: the span at the aim X/Y nearest the caster's Z. Monster aimed type 7: the target's Z, copied with its X/Y at cast start | plus the client's aim Z (Phase 7) |

**`FollowZ`** applies the avatar's rule: at a new X/Y, take the highest span with z ≤ previous Z + 135 cm, otherwise the terrain. It runs **cell by cell along the step**, so a long tick cannot jump a ramp or tunnel under a deck.

**Client hints** (`MOUSECMD` at move start, the fixed `CANTMOVE`, and later `JOIN_ZONE`) only choose among real spans: the nearest one within 3 m. A forged hint can only pick a floor that exists at the player's own X/Y. `m_nPosZ` keeps its meaning and its broadcast.

**NavAudit** logs, rate-limited, every hint that is more than 1 m from every span. Ordinary play then audits the bake.

**Accepted Stage A gaps:**
- A monster that wanders under a walk-on deck is drawn on the deck, and the server agrees with that. HAZARD keeps spawns away from such spots, and Phase 6 fixes the rest.
- "The Z the client draws" holds only approximately. The client samples at the model's centre of mass and only re-snaps within the frustum or 40 m, and the bake has no MORPH or runtime objects. Large models at ledges can come out one layer off.

### 3.4 AoE and range in 3D

**Rule.** The 2D tests stay exactly as they are (`S/gs_threadzone.cpp:991`; cones and strips at `S/cobjchar.cpp:1683-1695`). One test is added: |z_target − z_centre| ≤ `base` + `slope`·d, where d is the horizontal distance to the centre (for cones and strips, to the caster). Defaults: 500 cm and 0.70.

**Measured against the sphere you suggested.** Shady Jungle (EJ01) terrain, 12,000 random caster–target pairs per radius:
- a *hillside pair* has a straight line between the two points that is never steeper than 54°;
- a *cliff pair* has no such line and dz > 5 m.

| Rule | Hillside hits wrongly lost (R 11.6 / 20 m) | Cliff pairs excluded, dz 5–10 m | 10–20 m | > 20 m | Today's hit pairs removed |
|---|---|---|---|---|---|
| Sphere of radius R | 6.8% / 5.7% | 44% / 14% | 100% / 63% | 100% / 100% | 14.7% / 15.5% |
| **Band 5 m + 0.7·d (default)** | **0.03% / 0.25%** | 10% / 4% | 80% / 47% | 100% / 100% | 6.5% / 9.5% |
| Band 4 m + 0.6·d | 0.5% / 1.4% | 38% / 15% | 98% / 71% | 100% / 100% | 8.9% / 12.4% |
| Band 4 m + 0.5·d | 1.2% / 2.4% | 53% / 21% | 100% / 85% | 100% / 100% | 10.2% / 14.4% |
| Cylinder H = max(4 m, R) | 0.13% / 0.06% | 0% / 0% | 75% / 0% | 100% / 100% | 5.9% / 6.0% |
| Gate 3 m + tan 54°·d | 0% / 0% | 5% / 2% | 34% / 11% | 100% / 68% | 3.9% / 4.8% |
| Band + same-surface check | 0.03% / 0.25% | 100% | 100% | 100% | ≈ 11% / 16% |

**Reading the table:**
- **The sphere is dominated.** On a hillside it keeps only cos θ of the disc. The 4 m + 0.5·d band excludes more cliffs and loses only a fifth to two-fifths as many hits.
- **Other zones agree.** Across the four hilliest zones at the median radius, the sphere loses 2.0–9.4% and the default band 0.01–0.19%.
- Cones and strips cannot be spheres.
- **Caveat:** the samples are terrain-only and uniform. Phase 3 re-measures with objects and regen weighting.

**What the default band still admits.** Of the 220 damage circles: none reaches the Shady, Gorge or Eucar decks; 2 reach Goblin B2; 14 reach Ulverick; 215 reach the B3 altar (beyond 4.9 m); 151 reach a 10 m cliff (beyond 7.1 m).

**Optional same-surface check** (`aoe_surface`, off by default):
- A candidate with |dz| > 4 m is kept only if `SurfaceLinked(centre span → target span)` holds.
- `SurfaceLinked` is a DDA that applies only the 135 cm link rule, **ignoring walls and clearance**, so trees and rocks on a hillside exclude nobody.
- It separates every layer at every radius, and it needs only the Phase 2 bake.
- Its price in Shady Jungle: about one random pair in nine is excluded, against one in fifteen with the band alone (decision 2).

`aoe_vertical` switches the rule live (0 off, 1 band, 2 sphere, 3 cylinder).

| Check | Phase | Rule |
|---|---|---|
| A1/A2 area damage, types 7/17, circles, cones, strips | 3 | band (+ surface check if on) |
| A4/A5 area statuses (`S/cobjchar.cpp:1582` serves types 3–6, 8/12, 9/13; `:1767` serves 10/11). Both sites mix friendly and hostile skills | 3 | band only the targets of hostile skills, chosen by the skill's target filter rather than by call site |
| Friendly area buffs and heals (149 rows) | — | 2D (decision 6) |
| Aggro scans C1/C4/C6/C7 (COND02/08/27, ACT06); help calls C9/C10 (ACT11/14/18) | 3 | band, for OBJ_MOB finders |
| Attack and cast range B1/B2 on both sides; COND04 | 7 | band (the client tests the full \|dz\|, the server adds 100 cm of slack); no surface check, since the client has no nav data |
| Leash, summon distance, despawn, party/trade/quest checks | — | 2D |

**Implementation.**
- **Collect, then apply (Phase 1).** `CollectCHARs(pos, radius, band, out[], ≤ 128)` stores object indices. Each target is re-fetched and its HP checked before damage is applied, and circles get an explicit radius re-test. Callers:
  - `S/cobjchar.cpp:1604, 1681, 1784`;
  - `src/common/shared/io_quest.cpp:2388` (server branch only);
  - `S/cheatcmd.cpp:1043`.
- **AI scans** do not call out, so they keep the zone iterator.
  - Their band goes into the iterator state (`S/gs_threadzone.h:78-83`): `FindFirstCHAR` stores the centre Z and band, and `FindNextCHAR` tests them next to the 2D test at `S/gs_threadzone.cpp:991`.
  - The server `AI_FindFirstOBJ` overloads (`S/cobjchar.cpp:120-136`) pass the finder's Z in.
  - `AI_FindNextOBJ` takes no parameters, so it cannot carry the band itself.
  - The shared `CAI_OBJ::AI_FindFirstOBJ` signature (`src/common/shared/cai_lib.h:123`, also compiled into the client) does not change.
- **Unknown Z** (no bake) means no band.
- The band is a post-filter and does not change sector reach.
- A log line every minute reports exclusions.

### 3.5 Line of sight

Out of scope, for three reasons:
- the server has no wall tops;
- a walk test is not a sight test (a fence stops walking, not arrows);
- an attack check without matching client logic would refuse swings silently.

Attacks and aggro through a thin wall within 2D range therefore stay possible. Phase 8 offers walk-line checks for monsters only.

### 3.6 Protocol changes

| Change | Lockstep? | Phase |
|---|---|---|
| `build_char_move_leg_packet` (target_id 0, leg length); the attack builder switches to `CharacterMoveAttackBuilder` (same bytes; today it uses `CharacterMoveBuilder`, `S/network.cpp:116`) | no | 0 |
| Zero `gsv_STOP.m_nPosZ` (heap garbage today, `S/cobjchar.cpp:293-296`) | no | 1 |
| `short m_nPosZ` in `gsv_MOB_CHAR` after `m_nQuestIDX`, in the fixed part, inherited by `gsv_NPC_CHAR` (−32768 = unknown). Not in `tag_ADD_CHAR`, so `gsv_AVT_CHAR` keeps its layout. Readers use `sizeof` (`C/network/recvpacket.cpp:1308, 1353, 1445`; `C/cobjchar.cpp:955, 4661, 4693`; `S/cobjnpc.cpp:138, 887`). **Caveat:** the summon path copies only `sizeof(gsv_MOB_CHAR)` into `m_SummonMob` and then reads the variable status tail from it (`C/cobjchar.cpp:4661, 4693`). That tail is already garbage today, so check that path by hand when the field goes in | **yes** | 6 |
| Fill `Vec3.z` in `CharacterMove`, `CharacterMoveAttack` and `CombatSwing`; fill `gsv_STOP.m_nPosZ` | no | 6 |
| The client writes `cli_JOIN_ZONE.m_nPosZ`; inline drops (`tag_DROPITEM`) take the dead monster's drawn Z | no | 6 |
| Size-gated Z on `gsv_ADD_FIELDITEM` (precedent: `gsv_SKILL_START.m_PosAIM`, `net_prototype.h:1555-1563`) | tolerant | 6, optional |
| Size-gated aim Z on `cli_POSITION_SKILL` and `gsv_SKILL_START`; band constants in the zone-join reply | tolerant / yes | 7 |

### 3.7 Client changes (Phases 6–7)

**Phase 6: monster and NPC height** (`AdjustHeight_Monster` and `DropFromSky`, only for `OBJ_MOB`/`OBJ_NPC` with a server Z):
- z = max(terrain, nearest **up-facing** hit below ref + 135 cm over all colliding nodes, NOTMOVEABLE included);
- **reject down-facing hits.** The engine's ray test is two-sided (`D3DXIntersectTri`), so a plain down-ray would land on the undersides of flag-12 cave ceilings, overhangs and low tree crowns within 135 cm, where the bake has no span. `shootRayVec3Ex` returns the contact normal, so the filter is cheap;
- ref = the previous drawn Z;
- the leg interpolation wins when it differs from ref by more than 3 m, or when the height data is stale (`C/cobjchar.cpp:5777-5827`);
- with no surface near ref, draw at ref;
- the helper is a down-only copy of `getWorldObjectHeightInScene` (`E/zz_interface.cpp:9067`), about 40 lines;
- with no server Z, behaviour is exactly today's. Avatars are untouched.

**Phase 7:** client `IsInRANGE` (`C/cobjchar.h:321-327`), the aim pick (`C/skillaim.cpp:87-126`) and telegraphs (`C/groundtelegraph.cpp:255-262`).

## 4. Phases

**Rules.** A phase is committed only after in-game validation. Each phase has its own switches and can be enabled zone by zone.

**Order.** The phases run in numbered order, Z first (decision 3). Possible merges: 2 with 3, and 6 with 7.

### Phase 0: measure and probe (go/no-go)

- **Goal:** before building anything, find out whether there is tick and address-space headroom, whether the iterator bug bites, and whether multi-leg moves look acceptable on the unmodified client.
- **Changes:**
  - QPC timing of each zone's Proc loop (`S/gs_threadzone.cpp:187-218`): average, p99 and max every 60 s;
  - Private Bytes, after load and then hourly; live monster count per zone;
  - counters for move rejections, `Run_AWAY` failures and iterator clobbering;
  - the leg follower (§3.2);
  - **the probe,** `/nav trail`: the targeted monster walks the GM's move corners from the last 60 s (`MOUSECMD` starts, `CANTMOVE` stops) as legs, then chases.
- **Test:**
  - an hour in Sikuku and Sunshine Coast with a party;
  - `/nav trail` round a Goblin B1 bend at walk and run speed, with a lead of 0, 100 and 150 ms;
  - a second account arriving mid-trail sees the legs, not a monster homing through rock;
  - a slow applied mid-leg;
  - CombatTrace orphan sweeps, and `client.log` "position snapped forward" lines.
- **Exit / decision:**
  - You judge the corners. If no lead value works, pathing waits for a client fix in Phase 6, and the Z track continues.
  - If Private Bytes + 110 MB exceeds 1.7 GB, pick a RAM lever.
  - If busy-zone p99 tick time exceeds 130 ms, halve the budgets.
- **Kill switch / size:** the probe is GM-only and the clamp is invisible. About 400 lines of C++, with one server-only layout change.
- **Player notices:** nothing.

**Status (2026-10-28): validated in game.** Server only: `src/sho_gameserver/src/nav/`.

**Results:**
- **Corners.** `lead 100` looked good, and even `lead 0` did not look bad. 100 ms stays the default, so **corner presentation is a go: no client change is needed for pathing.**
- **Slowed monster:** it stayed on the corners, just slower.
- **Chase hand-off** worked.
- **Second viewer:** an account arriving mid-route saw the monster on the correct legs.
- **Load:** the party hour was skipped; the server lines from the test session cover it.
  - 67 zones and 14,151 live monsters.
  - The worst per-tick object work was Sikuku Prison at a p99 of 3.4 ms. Goblin Cave B1 averages 0.04 ms.
  - The longest tick was 112 ms, which is timer granularity.
  - Memory: 450 MB private, 646 of 2,047 MB of address space.
  - So there is ample CPU and address-space headroom; the nav budgets and the 45–75 MB of nav data stand.
- **Open:** the test session's AoE-clobber count was 0, probably because nothing cast AoE. The meter keeps counting in normal play; check it before Phase 1's scan fix.
- The test server runs from `Exes/`; its log is `Exes/log/gameserver-<date>.log`.

What was built:
- **`nav_agent`, the leg follower.**
  - Target-less `CharacterMove` per leg (`build_char_move_leg_packet`).
  - Exact clamp at each corner, carrying leftover time into the next leg.
  - A 500 ms step cap.
  - `lead_ms` early send.
  - It is dropped as soon as anything re-commands the monster.
  - New viewers and speed re-syncs get the leg the clients were last told.
  - The step clamp applies only to path legs; ordinary moves are unchanged.
- **`nav_stats`, the zone meter.**
  - Per zone, once a minute while the zone has players (or when its p99 is 30 ms or more): `nav: zone N …` with object work avg/p99/max, longest tick, monsters, players, AI moves refused, leash points refused, and AoE casts a victim's AI scanned in (since Phase 1 this only counts; it no longer breaks the AoE).
  - Server-wide, after a minute and then every 5 minutes: `nav: server: …` with zones, monsters, worst p99, private bytes and address space used.
- **`nav_probe`, the `/nav` command:**
  - `/nav rec` starts recording your click-move corners;
  - `/nav mark` adds a corner where you stand;
  - `/nav trail [lead_ms] [walk|run] [chase]` makes your target, or the nearest monster within 30 m, walk the corners, and replays the same trail until the next `rec`;
  - `/nav stats` shows this zone's last minute plus the server line.
- `CharacterMoveAttack` is now built with its own builder (same bytes).

**In-game test (GM):**
1. **Corner feel.** In Goblin Cave B1, at a corridor bend:
   - `/nav rec`, click-walk round the bend (each click is a corner), then `/mon <id> 1` near the start of the route.
   - `/nav trail 0`, then `/nav trail 100`, then `/nav trail 150`, then `/nav trail 100 walk`.
   - Watch every corner: a short stop (lead 0) against a small cut (100/150). Note which lead looks best, or whether none does.
2. **Second viewer.** Have a second account arrive while the monster is mid-trail: it must see the monster walking the legs, not homing through rock.
3. **Slow mid-leg.** Slow the monster mid-trail: it keeps to the corners, with no teleport. Check `client.log` for "position snapped forward".
4. **Chase hand-off.** `/nav trail 100 chase`: at the last corner the monster runs at you and attacks normally. Combat looks normal, and CombatTrace shows no orphan sweeps.
5. **Meter.** Spend an hour with a party in Sikuku Prison and Sunshine Coast, then read the `nav: zone …` and `nav: server: …` lines. Also note the `AoE scans clobbered` count after AoE-ing packs.

### Phase 1: ground fixes

- **Changes:**
  - **no in-game MOV flip** (decision 8: MOV is retired in Phase 4). Only the docs are corrected: the `chunk.py` docstring, FORMATS.md, and the editor's `MOV.cs` and tests now say row 0 = north;
  - X bounds check in `IsMovablePOS`;
  - collect-then-apply in the five outer scans;
  - `CANTMOVE` reads its own Z and calls `UpdateSECTOR` (`S/gs_user.cpp:2836, 2848`);
  - `gsv_STOP` Z zeroed;
  - AIACT_08 zero-distance guard.
  - (The movement dt cap shipped in Phase 0 with the step clamp.)
- **Test:**
  - AoE a pack whose AIP scans on DAMAGED: CombatTrace shows every monster inside the radius hit, and none outside it;
  - a player stopped by a wall (`CANTMOVE`) keeps a sane Z and sector;
  - a monster fleeing from a target standing on its exact spot.
- **Exit:** no regression in AoE or movement.
- **Kill switch / size:** none needed; these are bug fixes. About 200 lines.
- **Player notices:** AoE hits the whole pack every time.

**Status (2026-10-28): validated in game.**
- **Test:** Arrow Shower (rank 10, 8 m) on packs in Goblin Cave B1–B3, plus running into walls. No issues.
- **Meter:** 0 AoE casts had a victim's AI scan mid-AoE over about 8 minutes. The traced iterator bug never fired with these monsters, so the fix is a safeguard for monster AI that does scan on DAMAGED.
- **Test character:** ScoutRain on test@local (`scripts/combatsim/builds/test-scout-aoe-100.json`).

What changed:
- **`CObjCHAR::AI_CollectOBJs`.** The AoE damage loop, both area-status loops (`Skill_ChangeIngSTATUS`, `Skill_START_10_11`), GM `/damage` and quest REWD_028 now collect every target before touching any.
  - The damage loop also skips a target that died since the scan.
  - The meter keeps counting the casts where a victim's AI scanned mid-AoE, which is what used to break them.
- **`CANTMOVE`** reads its own Z and updates the sector after the step back.
- **`gsv_STOP`** carries `m_nPosZ` instead of pooled-packet garbage.
- **AIACT_08** does nothing when the monster stands on its target, instead of dividing by zero.
- **`IsMovablePOS`** returns false off the grid.
- **MOV row order:** documented in `chunk.py` and FORMATS.md. The reader is unchanged (decision 8).

### Phase 2: bake and server Z (invisible)

- **Changes:**
  - the full bake (§3.1); walls are computed now but used only from Phase 4;
  - loader after the map loop (`S/zonefile.cpp:640-645`);
  - regen, NPC and event Z read (`tagEVENTPOS` gains a Z);
  - player `FollowZ` and client hints;
  - `/nav`, NavAudit, and the shipping lines.
- **Data:** bake all 67 zones. You review the PNGs of Goblin B1–B3, Shady Jungle, Junon Polis, Sikuku, Muris and Shadow Canyon's planks.
- **Test:** run `/nav` on and under the `dari` deck (two floors, about 72 m and 5 m), at B1 `domdari`, on the B3 altar (about 479 cm), on a Junon floor and in the street below it, on a Shady edge panel (no walkable top) and on a Muris roof. Then play for 20 minutes across 5 zones.
- **Exit:**
  - NavAudit misses over 1 m stay under 2% of hints, each one explained;
  - every seed resolves or is listed;
  - the bake without lump 11 drops about 27,600 m² of Shady panel tops;
  - Private Bytes rise by at most 80 MB and load time by at most 5 s;
  - no `.lnav` in the packed index. `pack.ps1` has no dry-run mode (its only parameters are in, out and pipeline), so list the index after a real pack, or add a `-WhatIf` flag.
- **Kill switch / size:** `enabled = 0`, or delete the file. About 1,000 lines of Python and 600 of C++.
- **Player notices:** nothing.

**Status (2026-10-28): validated in game.**
- **`/nav` checks** (server height = client height at each spot, floors as expected):
  - Shady Jungle `dari`: deck 74.7 m and ground 10.0 m, from both levels; the ground is ROOF-HAZARD.
  - Goblin B3 altar: 4.8 m over terrain at −4.1 m (unreached).
  - Junon: street −0.3 m, wall top 16.0 m, the ramp up to it.
  - Muris: plaza platform 3.9 m over unreached terrain; a terrace at 18.2 m.
- **Audit:** 1 of 452 client heights was more than 1 m from every floor, on a Muris stairway (17.2 m against 16.1 m).
- **Startup:** about 3 s more for zone loading. Every grid loaded, none stale.
- **Memory:** grids 119.7 MB resident; private memory 450 → 571 MB; address space 647 → 759 of 2,047 MB. That is over the 80 MB estimate, but the user approved the RAM.
- The one apparent mismatch was the readout, not the server. "Your client" is the height at the start of the last click-move, the only height a client sends, so on a slope it lags. The wording now says so.

**Bake** (committed `2dc17bfe`): `scripts/nav-bake.py [rows|--all]` writes `data/<zone folder>/<FOLDER>.lnav` plus `build/nav/<FOLDER>.png/.txt`. The 66 zone folders take about 15 minutes. The images were reviewed by the user, and four rules were corrected on them (§3.1 passes 5 and 6).

**Server:**
- `src/sho_gameserver/src/nav/nav_grid.*` loads the grid at the end of `LoadZONE` and logs `nav: zone N grid: … KB`, or `… has no walk grid`. If map files changed since the bake, it warns that the grid is stale.
- Queries: `TerrainZ`, `SpansAt`, `VisualTopZ`, `FollowZ`, `NearestSpan`.
- Players: `CObjCHAR::m_nNavZ` follows each move step, cell by cell, with the avatar's rule. It is re-anchored by the client's height on click-move and on `CANTMOVE` (the nearest floor within 3 m), and reset on zone join and on any jump over 20 m.
- The client's height is audited: more than 1 m from every floor is counted in the zone line and logged at most every 10 s.
- `m_nPosZ` keeps its meaning (the client's report, broadcast).
- `/nav` (or `/nav here`) whispers the floors under you and your target: terrain, the height the client draws monsters at, and your server height against your client's.
- The server line adds the total grid memory.

**Not done** (moved to the phase that needs it):
- reading IFO/ZON Z: regen anchors are in the grid, used in Phase 3;
- the `[nav]` config section: comes with the first behaviour switch in Phase 3.

The kill switch is to delete the `.lnav`. Nothing reads the heights yet except `/nav`.

### Phase 3: vertical band and roof spawns (server only)

- **Changes:**
  - Stage A Z, the band, AoE centres and aim Z (§3.3–3.4);
  - the surface check (off) and `aoe_vertical`;
  - `SnapSpawn` with HAZARD rejection and counters per regen point;
  - `scripts/nav-band-calibrate.py`.
- **Test:**
  - from a Shady cliff ledge, and from the `dari` deck with monsters pulled below, the largest AoE hits nothing below and draws no aggro from below;
  - at the B3 altar edge, AoE still hits monsters on the altar;
  - EJ02 and JD03 hillside packs take the same hits with the band on and off (try `aoe_vertical 2` to feel the sphere);
  - 10 minutes of respawns in Muris, Zant and Fossil Sanctuary: none on a roof.
- **Exit:**
  - hillside tests identical;
  - layer cases excluded;
  - exclusion rates reviewed against the calibration;
  - any regen point still at 100% fallback (Muris has one under a roof) is listed and moved as a data fix.
- **Kill switch / size:** `aoe_band`, `mob_layer_aggro`, `spawn_hazard`. About 400 lines of C++ and 200 of Python.
- **Player notices:** AoE from a bridge or cliff top no longer reaches far below; monsters below stop aggroing and stop answering help calls through floors; no monsters on Muris and Zant roofs.

**Status (2026-10-29): validated in game.**
- **Cliff test:** from the Shady Jungle valley at (5990, 5030) m, `/nav aoe 15` lists the pack as HIT. From the cliff top 25 m above, `/nav aoe 30` lists it as left out, and `/nav set aoe 0` brings back the 2D hit. Arrow Shower from the cliff edge does no damage below. With `aoe 0` it does.
- **Log:**
  - Muris re-rolled 5 roof spawns, none fell back to the centre;
  - Shady monster scans skipped 4–18 cross-cliff targets a minute;
  - the AoE rule excluded 3 targets in Shady and 0 in Muris;
  - client heights stayed on the grid.
- The arrow "stacking" seen on Muris lizards is not this phase: the rule excluded no area target there and does not touch normal attacks. Look at the client's projectile presentation if it comes back.
- `/nav aoe [m]` was added for the test: it lists the monsters an area centred on you would hit, or leave out by height.

What changed:
- **`nav/nav_settings`:** the rule (`InVerticalReach`) and its knobs, read from `server.toml` `[nav]` at start and on `reloadconfig`, and changed live with `/nav set` (see `doc/server.toml.example`). The defaults are the decisions: band 500 cm + 70%, monster scans on, roof re-roll on.
- **The rule's centre:** the AoE damage loop (types 7/17) checks every collected target against its centre. Cones and frontal strips start at the caster. A circle centred away from the caster (a ground target) takes the floor at the aim point nearest the caster's height. A monster's type-7 circle is centred on itself.
- **Area statuses** (`Skill_ChangeIngSTATUS`, `Skill_START_10_11`) apply the rule only for hostile filters (4, 5, 6, 8, 10, and 7 from a monster). Party buffs and heals stay 2D.
- **Monster scans** (`AI_FindFirstOBJ(int)` from an `OBJ_MOB`: aggro, help calls, the AI file's searches) pass the monster's height. `CZoneTHREAD::FindNextCHAR` applies the rule. Skill collects use a 2D scan and apply their own rule.
- **Regen spawns:** `CZoneTHREAD::NavPickSpawn` re-rolls a spot where the client would draw the monster more than 3 m above the floor nearest the point's baked anchor, up to 6 times, then uses the centre. A centre under a roof logs a warning; the bake predicts none.
- **Zone line:** adds "out of vertical reach: N in areas, M in monster scans; roof spawns re-rolled R, at the centre F".
- **Stage A caveat:** a monster's height is where the client draws it. Under a walk-on deck it counts as on the deck, so an AoE from the deck still hits it (as seen on screen). Phase 6 fixes that.

Predicted roof re-rolls (same rule offline): Muris 11 of 37 points (worst (5269, 5247) m, 44% of its square), Zant 10/29 (≤ 7%), Fossil Sanctuary 28/123 (≤ 17%), Sunshine Coast 13/327, Forest of Wandering 10/394, Shady Jungle 3/392. No centre is under a roof.

### Phase 4: walls for idle moves and spawns (server only)

- **Changes:**
  - `LineWalk` and the `SetCMD_MOVE2D` gate (§3.2 table), with straight fallbacks for leash and follow;
  - `SnapSpawn` reachability;
  - the footprint trailer;
  - MOV retired in baked zones: the bake replaces it as the walkability source, and its grid is freed (decision 8).
- **Data:** review walls, clearance, MOV agreement and both deliberate-mistake bakes; fix the rules and re-bake.
- **Test:** Goblin B1/B3 respawns stay out of rock and inside the tunnels. Watch wanderers near walls, Junon houses and Shady trees; wander in a Karkia zone with no MOV; and a summon following through a doorway.
- **Exit:** spawn fallback at most 1% per zone, with no point at 100%; MOV agreement at least 80%; no monster frozen for 60 s.
- **Kill switch / size:** `ai_moves`, `spawn_reach`. About 300 lines of Python and 400 of C++.
- **Player notices:** idle monsters stop walking through houses, trees and cave walls; nothing spawns in rock or outside the Goblin tunnels.

**Status (2026-10-29): validated in game.** Goblin Cave spawns stayed out of the rock, wanderers kept off the walls, leashed monsters ran home, and the aim circle lies on the Shady Jungle bridge planks.

What changed:
- **`Grid::LineWalk`** walks a straight line cell by cell over reached, walkable floors, with the bake's own step rule: 135 cm on terrain and stairs, 45 cm involving an object floor (now `Nav::StepLinks`, 2026-10-29).
  - The bake gained a STAIRS span flag (HEIGHTONLY floors) so stairs keep the 135 cm step at run time. All zones were re-baked.
  - The start may be any floor, so a walker steps out of a blocked cell.
- **Walk floor for monsters:** every moving character now keeps `m_nNavZ`. A regen spawn starts on its floor, and any other monster on the walkable floor nearest where the client drops it.
  - `Nav_Z()` for a monster still returns where it is drawn (Stage A, for the vertical rule).
  - `Nav_WalkZ()` returns the floor it walks on.
- **`CObjCHAR::SetCMD_MOVE2D`** checks the whole line on the grid, not the `.MOV` destination cell. The `.MOV` stays only for zones without a grid; every zone has one now.
  - Wander (AIACT_03/04) draws again up to 3 times, then rests that tick.
  - Flee (AIACT_08) goes as far as the line allows (`Nav_MoveTowards`).
  - "Move next to the found character" (AIACT_05) gives up that tick when the line is blocked, until A* (Phase 5) finds the way round.
  - The leash (`Run_AWAY`) tries 6 points, then walks straight anyway (`Nav_MoveOrStraight`), so a monster never chases forever.
  - Summon follow (the `CObjSUMMON::Proc` loop and AIACT_29) walks straight when its line is blocked, so it is never stranded.
  - Player orders and chases are unchanged, until Phase 5.
- **Regen spawns** (`NavPickSpawn`) also re-roll a spot with no reached walkable floor, or behind a wall from the point's centre (`LineWalk` from the anchor). The centre is the fallback.
- **Switches:** `ai_walls` and `spawn_reach` in `[nav]` and in `/nav set` (`walls`, `reach`).
- **Zone line:** adds the blocked leash and follow walks that went straight.
- **Readouts:** `/nav spawns` samples the nearest regen point's square 200 times and counts the spots in rock or off the walkable ground, behind a wall and under a roof. `/nav walk` says whether your target could walk straight to you, or where a wall stops it.
  - The worst Goblin spawn squares (share with no reached floor): B1 (5162, 5058) m at 8%, B3 (5243, 5357) m at 8%, B2 (5396, 5534) m at 6%.
- **Client, done early** (it was a Phase 7 item; the user asked for it):
  - The aim circle (`CSkillAim::MouseOnGround`) picks walkable map objects (decks, platforms) along the mouse ray, within the skill's reach plus 20 m, before the terrain.
  - Ground telegraphs (`CGroundTelegraphs::Height`): the player's aim, and any shape centred on a deck, take the surface nearest the centre's height, the terrain or the top of a walkable map object, within 3 m above and 5 m below. Past a deck's edge, with nothing in reach, the shape hangs flat. Boss telegraphs on the ground drape over the terrain as before.
    - The object tops are sampled by a ray down at each point of a 1 m world lattice, cached and blended. At most 40 points are sampled a frame, so a new aim settles over a few frames.
    - **First in-game test (2026-10-29):** the circle was at the deck's height but invisible on the planks. The engine drew the overlay right after the terrain without writing depth, so every map object drawn later painted over it. It is now drawn after the opaque objects, still z-tested, so it shows on planks and hides behind whatever stands in front of it.
  - When the range clamp moves an aim that is on a deck, it keeps the deck's height.

### Phase 5: pathfinding (server only)

- **Changes:**
  - `nav_path` and every §3.2 hook, skill chases included;
  - the QPC budget cap;
  - the dynamic overlay (decision 10);
  - `/nav path` and NavStats;
  - `path_zones` rollout: Goblin B1–B3, LZ01 and Shady Jungle first.
- **Test:**
  - in B1, a monster dragged round a 90° bend follows the corridor, with at most one visible stop per corner (A/B the lead);
  - Junon buildings; Adventurer's Plain rocks;
  - from the Shady deck, a valley monster climbs a ramp when one lies in the search window, and otherwise stays below as today;
  - summons follow across the deck; CTRL+click a summon behind a house;
  - Cerberus with the wall up;
  - Sikuku with a party.
- **Exit:** in Sikuku and Sunshine Coast, nav time averages at most 2 ms per tick and p99 tick time rises by at most 5 ms; deferrals under 5%; no chaser stuck for more than 5 s; no new orphan sweeps.
- **Kill switch / size:** `pathing`, `path_zones`, `/nav pathing off`. About 1,300 lines of C++.
- **Player notices:** monsters and summons go around obstacles; leashing monsters walk home along corridors. **Leashes trigger more often in caves**, because paths are longer than the straight-line leash distance.

**Status (2026-10-29): validated in game.** In Goblin Cave B1, monsters chased round the corridor instead of through the rock, leashed monsters walked home along it, and `/nav set path 0` brought back the straight cut.
- **Numbers:** 112 searches over the test, every one found (none partial, failed or deferred). The slowest took 0.46 ms, and nav time stayed at most 0.003 ms a tick.
- **Sikuku Prison** p99 is about 4.2 ms with a player in it, against 3.2–3.6 ms before Phases 4–5. Most of the rise is Phase 4's line checks: it refuses about 4,500 idle moves a minute, each drawn again.
- **Summons (tested after the commit):** CTRL+click orders mostly pathed, but now and then went straight through an obstacle.
  - The log showed 12 searches ending "no floor" in Goblin B3.
  - The order packet has no height, so the goal took the floor within 3 m of the summon's own height. A click up or down a slope found none, and the order fell back to straight.
  - A summon whose tracked height a straight walk had left on a rock top failed the same way at the start.
  - **Fix (built, awaiting the re-test):** a goal of unknown height takes the floor nearest the walker's at any height; clicks look up to 6 m round for a walkable spot; a start falls back to the nearest walkable floor at any height.
  - **Re-test:** better, but at the Goblin B3 revive platform summons still cut through a pillar and ended on its top.
    - The platform is an object floor 1 m over the cave floor. The bake's 45 cm object step leaves it a separate area (component 2, seeded only by the `restore` event at (5695, 5275) m), so every search between it and the cave floor was "unreachable" and went straight, across the 13 m pillars round it.
    - **First fix (not enough):** walk to the walker's own nearest cell, then straight. On the second re-test a CTRL+clicked summon stopped beside the platform (no follow tick to finish the hop), and one ordered off the platform stopped against a pillar (its nearest platform cell), never stepping down to go round.
    - **Fix (built, awaiting the re-test):**
      - A* also steps **down** ledges of up to 2.5 m, one way only. Off the platform it drops at an edge and walks round the pillar.
      - A goal on a floor it cannot climb to ends the search at its closest cell, and the goal follows as one straight leg when it is within 16 m. That covers the ledge up.
      - The component check no longer refuses a search; it only caps the cells: 1,200 for a goal above, 2,500 otherwise.
    - **Third re-test:** both cases work. A CTRL+clicked summon sometimes took an odd route onto the platform: with no way up, it walked round to the floor cell nearest the click, then climbed.
      - **Fix (built):** A* may also climb ledges of up to 1.5 m onto reached walkable floors, at 4x a step's cost. It climbs at the nearest edge and crosses the platform, as the follow loop's straight moves always did.
      - Only reached walkable floors are searched, so a climb never lands on a crate or a rock top.
      - Monsters may now hop up 1 m ledges instead of walking round to the stairs, when that is much shorter.
      - **Validated:** the summon goes up and down the platform sensibly.
      - **Open:** it never uses the platform's stairs. They are ordinary meshes with risers up to 53 cm. The bake's body test sees each tread's next step as a ceiling, and object floors link only within 45 cm, so the treads are not walkable.
      - **Bake follow-up:** a staircase's own risers should not count as a ceiling, and stair steps should link to about 60 cm. Then re-bake and re-check the B1 rock leak.
      - **Done (2026-10-29, full re-bake):**
        - Bake rule 3b: an object floor whose only obstruction lies at most 75 cm up, with an up-facing tread 15–75 cm up in its cell or a neighbour, is walkable.
        - Rule 4's object step went from 45 to 60 cm; the server's `OBJ_STEP_CM` matches.
        - `scripts/nav-view.py` showed the B3 treads unwalkable. It also showed that the Shady spiral ramp was fine: its foot links to the ground, all one component; the grey there is the ground under the ramp and the stump's base.
        - B3's revive platform, its treads and the cave floor became one component; the platform was component 2 before.
        - `scripts/nav-view.py --lnav <old.lnav> --tag before` renders an older bake to compare. The grids before it are in `build/nav/lnav-before-stairs/`.
      - **In-game check:** Skaaj and the El Verloon plaza fine. Four problems:
        - B3 monsters still climbed the platform's side. A* kept its 1.5 m climb at 4x a step's cost, about 4 m of walking, cheaper than going round to a stair.
        - Muris' lower stairs (5202, 5191) m were taken on a slant. Their bottom row was unwalkable: the first tread merges into the terrain floor, and the next riser blocks it; 3b was object-only.
        - On Muris' great stairs (5236, 5383) m a monster fell to the ground when it gave up. The flight rises 78 cm a metre, so its 1 m cells never linked at 60 cm. With no floor there, its path home started on the ground 20 m below.
        - Gorge of Silence plateaus (5041, 5393) and (4993, 5530) m were reached, though players cannot get up there. The grid climbed the canyon walls on terrain: 1 m bilinear steps stay under 135 cm where the quads are 56–71 degrees.
      - **Fixes (2026-10-29, validated in the checks below):**
        - **Server:** A* climbs and drops ledges only toward a goal in another component. Between floors the bake connected, it uses the bake's links only.
        - **Steep terrain.** The client refuses every move but downhill on a 2.5 m quad of 54 degrees or more (`CMAP::GetNormal`: from the quad's south-west corner along both edges; `ApplySliding`). A terrain link out of such a quad must head downhill, both ways. The Gorge's plateaus and the seabed round Mana Snowfields drop out; that seabed was the 0.27 km² Mana "plain".
        - **Ramps and flights.** The client walks up any mesh under 54 degrees. It stops on a steeper face only when the face rises above about 0.7 of its centre of mass over the feet (`UpdateFootHeight_Avatar`). An object link may therefore rise up to 135 cm when the higher floor's surface starts within 60 cm of the lower one's top.
        - Each floor now stores how far its surface reaches below and above its height (`.lnav` version 2). The server applies the same rule (`Nav::StepLinks`) and still reads version 1.
        - A ramp link is refused when a higher floor in the cell stepped into is within reach: the client lands on the highest surface under its feet + 135 cm. Without that check, a pyramid's skirt under Muris' plaza deck opened all the hidden ground beneath the city (+172%).
        - **3b on terrain** applies only below a floor of level treads, so a snow drift's sloped skin stays shut.
        - Re-baked only Gorge of Silence, Mana Snowfields, Goblin Cave B3 and Muris. The grids before are in `build/nav/lnav-before-ramps/`.
        - Reached cells: Gorge −35%, Mana −52% (the seabed), B3 +0.7%, Muris +2.6%.
        - Not re-baked yet: El Verloon −29% and Adventurer's Plain −21%, the outer mountains and cliff faces. Every zone changes like that at its next bake.
      - **Second in-game check:** Goblin Cave B1's stairs (5428, 5505) m, Muris' lower stairs and the walk down Muris' great stairs all work. The B3 and Gorge spots were not tested: `/tp` takes tens of metres, and the spots were given in metres. Two problems:
        - Monsters started up Muris' great stairs on the side ridge and only moved onto the steps a few metres up.
        - Monsters took the side ridges of the stairs by Muris' spawn (5200, 5214) m instead of the steps.
        - Both flights were striped in `scripts/nav-view.py`: every other row of treads was unwalkable. Their steps are 16–20 cm, so a 1 m cell holds four or five treads. The body test started 30 cm above the floor's average height, and the cell's own top risers fell in it, with the next row more than 3b's 75 cm up.
      - **Fix (2026-10-29, Muris re-baked, validated):** the body test starts 30 cm above the top of the floor's own up-facing surface, at most 60 cm over its height. Both flights are walkable from bottom to top. Bake only; the server is unchanged. The grid before is in `build/nav/lnav-before-risers/`.
        - Muris: reached cells 216,078 → 289,333. Nearly all of it is the outer rim, 114 m up: the cliff-top walkway (the dome, reached by the narrow stairs beside the sphinx) joins it at (5392, 5136) m, up a smooth 53-degree mesh slope. The client climbs any mesh under 54 degrees, so a player can probably walk up there too; cut it if not.
        - In memory, other zones gain 0.1–0.5%, in patches of at most 124 floors: B3 +766 floors, B1 +342, Gorge +533, Mana +638, Shady Jungle +1,144, El Verloon +414. Not re-baked.
      - **Third in-game check:** both Muris flights work, and so does the Gorge. Players and monsters can walk on the slopes beside the long stairs. B3: summons and monsters still do not use the platform's stairs, while B1's identical platform works.
        - It is the same model, but B3's sits about 0.5 m lower in the cave floor: its centre is 0.64 m over the floor, B1's 1.14 m. Each stair's bottom tread lies within 50 cm of the ground and merges into the terrain floor. Terrain floors kept no surface extents, so the next riser counted as a ceiling over that floor.
        - The stairs ended one cell short of the floor. The platform joined the cave only by 54 cm hops onto its bevelled sides at the stair cheeks.
      - **Fix (2026-10-29):** a terrain floor's body test starts from the top of the surface merged into it, with the same 60 cm cap. All four B3 stairs now reach the floor, two or three cells wide, like B1's. Only B3 was re-baked. **Validated:** B3's summons and monsters use the stairs. Its grid before is in `build/nav/lnav-before-risers/`. In memory, other zones gain patches of at most 43 floors (El Verloon +112). Muris was baked just before this rule and will change by a few cells at its next bake.
      - **Regen warnings (2026-10-29):** "even its centre is under a roof or off the walkable ground" fired for two points.
        - Spire Village (zone 88) at (5016.2, 5141.2) m: its centre is on a 60 cm step edge.
        - Sikuku Underground Prison (zone 65) at (5017.4, 4872.5) m: its centre is on a low ledge.
        - The old body test blocked both centre cells. `LineWalk` starts at the centre, so every reroll failed and all of the point's monsters spawned on that one spot.
        - The terrain-floor rule above clears both. Both zones were re-baked, and the grids before are in `build/nav/lnav-before-risers/`.
        - Spire Village drops from 612,584 to 217,605 reached cells: the steep-terrain rule cuts the bluffs and mountains round the village, where no regen point, NPC or warp lies.
        - The prison gains 0.4% and loses a ring of steep terrain round a pit hidden under its floor meshes.
      - **Full re-bake for the tester session (2026-10-29):** every zone, on the rules committed in 2ba765cf. The grids before are in `build/nav/lnav-before-full/`.
        - The first pass leaked in two zones. Goblin Cave B1 went from 66,235 to 305,343 reached cells: the whole rock mass round its tunnels. The Starlight Lounge went from 3,195 to 12,653.
        - B1's joint was one link at (5456, 5536–5537) m. It landed on a rock surface with an overhang 30 cm to 1.5 m above it. Both were within 50 cm, so they merged into one floor, and a floor's own samples never blocked it.
        - **Fix 1:** a down-facing sample above the floor's top always blocks.
        - **Fix 2:** a ramp link compares the two surfaces along the edge the cells share, 40 cm deep, instead of their whole height ranges.
        - The server still applies the whole-cell extents (`Nav::StepLinks`). That is looser, but it walks only floors the bake reached.
        - Second full pass: B1 has 66,805 cells, the lounge 3,469. Every validated spot still passes: Muris' three flights and the narrow stairs, B3's stair feet, the Shady stump ramp, the Spire Village regen centre.
        - Most other zones lose their outer mountains and cliff faces, 10–65%. Skaaj town loses 93%: the old grid reached the whole ground plane round the floating island.
        - No regen point lost its floor.
        - Gains: a Clan House side room with tiered steps (+30%), the Abandoned Church's curved ramp and stairs (+14%), and less than 9% in the Colosseum, Portal Room, Live House and lounge.
        - **Flagged, not changed:**
          - Luna Clan Field (LPVP01) gains 900%. The arena's outer walls are smooth 45-degree slopes, which the client climbs, so the outside is reached.
          - Lion's Plains (zone 8) loses 98%. Its start plateau's only exits are slopes of 54 degrees or more, which the client walks down but never up. Links must work both ways, so the map is cut from its two seeds. It has no regen points.
          - Oro desert: spawn squares on steep canyon slopes shrink. The worst is ODRP01's point at (5479.4, 5039.0) m, with 76% of its square unreached.
        - `scripts/nav-view.py --zmax` leaves out floors above a height, for a deck that hides the spot.
      - **Zant's fences (2026-10-30, tester report, approved):** monsters crossed the wooden fences beside the paved paths, for example at (5204, 5170) m. The grid had their tops as walkable floors.
        - The fence (`etc002.zms`, object 209) has two rails, 60 cm and 1.2 m up, and post caps at 1.5–1.8 m. Each is a strip 10–25 cm wide.
        - The rails and caps lie within 50 cm of each other in height, so they made one floor at their mean height, between the rails. Its edges passed for a ramp, and the lower rail alone is a 59 cm step from the ground.
        - **Rule 3c (bars):** a floor's top is a bar when all of these hold:
          - the solid under it is under 25 cm thick (its own underside that close below);
          - its surface is under 40 cm wide;
          - it is stacked over a gap of more than 30 cm in the floor's surface (most of it within 20 cm, in plan, over a lower surface of the floor), or is a lone flat strip (under 10 cm high).
          A bar links to no terrain floor: a fence stands on the ground. Zant's rails are 19–24 cm thick.
        - **Ramp continuity:** a ramp link up *from the ground* needs the higher floor to rise with no gap over 30 cm in its surface.
        - Rejected on the way:
          - an area threshold: a diagonal rail adds up to as much as a deck corner;
          - splitting floors wherever an underside sits over an upward surface: it would chop sloped slabs;
          - making bars unwalkable: it cut off the boarding steps onto Kenji Beach's ship (two boards down from the pier, 62 cm and 34 cm over the deck), which are as thin, narrow and stacked as a fence's rails;
          - continuity for every ramp: it cut stairs with 35–60 cm risers between object floors, onto Forest of Wandering's platform (2,000 cells), the Starlight Lounge's upper floor and a Mana Snowfields deck;
          - a 30 cm bar thickness: Junon Polis' stone stairs (slabs 28 cm thick, 44 cm risers, on a bottom step that fills the cell) were cut at the foot.
        - The first full re-bake with the stricter versions (`build/nav/all7.log`) lost those areas. The final rules were checked in memory against the grids before (`build/nav/lnav-before-bars/`): all four are back, and Zant's fence stays shut.
        - Zant alone was re-baked first; its grid from before is in `build/nav/lnav-before-fences/`. Then every zone was re-baked, twice.
        - Final bake (`build/nav/all9.log`): no zone changed by more than 0.1% against `build/nav/lnav-before-bars/`, except the Starlight Lounge (−1.6%). The checked spots are in `build/nav/fence-rule-checks.png`. The user approved the changes from the views: the Polis fountain rim, a Breezy Hills boulder top and a Wasteland structure top are no longer walkable; the ship steps, Polis stairs and Forest of Wandering platform are kept.
      - **Starlight Lounge leash (2026-10-30, tester report; fixed below by a per-map exception):** a monster that gave up a chase walked straight back through the hall's walls.
        - Almost the whole hall is unreached: 90 of its 7,540 floors at 2.37 m. Its stairways from the entrance ring start with 61–64 cm risers, over the 60 cm object step, though players climb them.
        - The monster's spawn point was in the hall, so every path home ended "no floor" (the zone's meter lines: 11 of 11 searches), and it walked straight.
        - **Tried and reverted:** `OBJ_STEP_CM` 65 in the bake and the server. The hall was reached (3,412 → 12,797 cells), but the monster still went under the stage going home. The change also reopened the Breezy Hills boulder top and the Wasteland structure top, and opened Dolphin Island's bamboo fence top. Players must not stand on any of them. A few Gorge rock tops opened too, an accepted quirk: the original map leaves an opening in the terrain there.
        - **The user's call:** no global rule change for one map. The Lounge is an imported map not meant to hold monsters. Grids restored from `build/nav/lnav-before-step65/`, server rebuilt at 60 cm.
      - **Per-map exception (2026-10-30, validated in game):**
        - `.lnav` version 3 adds the map's own object step. The bake sets it from `OBJ_STEP_BY_ZONE` (`{164: 65}`), and the server uses `Grid::ObjStepCm()` in `StepLinks`, `LineWalk` and A*. Older grids read as 60, so the other maps are untouched and were not re-baked.
        - Only the Lounge was re-baked. Its hall and tiers are reached, including the user's GM spawn spot on the tier at 4.3 m.
        - **Still under the stage on the walk home,** with a pause at the stage's small front stairs (5159, 5222). Re-teleporting showed the monster on the platform, so the server had it on the right floor and the client drew it wrong.
        - The client follows floors on its own (`MonsterFloorZ`) and took the server's floor (`SetServerZ`, sent at each move leg's start and at each stop) only when the two differed by more than 300 cm. Here they differed by 1.6–1.9 m, so the error never corrected.
        - Why the follow lost the small stairs is not found. The route climbs them in steps of 30–66 cm, within the follow's 135 cm reach. The Lounge's objects have no animated parts and zero cylinders.
        - **Client fix:** `RESYNC_CM` 300 → 135. A monster drawn more than a step off the server's floor is put back at the next corner or stop.
        - **Found with `/nav path`:** the route's last turn before the stage was (5159.5, 5221.5), the middle of the stairs' east column. The stairs end at x = 5159.09, so that 1 m cell holds 9 cm of them, yet the bake counted the whole cell as stairs. A monster cut through it beside the steps: the user saw it take "a small space beside the stairs". Going up, the client never found the steps and stayed at dance-floor height. The turn's floor (3.33 m) was within a step of the client's (2.37 m), so the resync did not fire.
        - **Bake rule 3d, Lounge only (`SLIVER_CM_BY_ZONE = {164: 20}`):** an object floor whose up-facing surface spans less than 20 cm across its cell is not walkable. 640 floors cut, all one component still. Views: `build/nav/view-bar-5160-5220-stairs-{before,after}.html`. `nav-view.py --mark x,y,z` draws a route's corners.
        - **Validated:** a GM-spawned monster left on the dance floor climbs the small stairs and walks home onto the tier.
      - **MOV removed (2026-10-30, validated in game: the server starts clean and monsters wander, chase and leash as before):** the server no longer loads `*.MOV`.
        - Its last use was `SetCMD_MOVE2D`'s fallback for a zone with no walk grid, or `/nav set walls 0`; every zone has a grid. Without one, AI walks now go unchecked, as Karkia's always did.
        - Two GM readouts lost their MOV "ground" field.
        - The 1,425 files are in `build/retired-mov/`, and about 34 MB of server RAM (a 512 KB bitmap per zone) is freed.
        - The map generator no longer writes MOVs; the bake dropped its MOV comparison.
        - `scripts/server-data.ps1` now copies the `.lnav` grids, which it never did, instead of the MOVs.
        - The map editor opens a zone without them.
      - **Sliver audit (2026-10-30): the 20 cm rule stays per map.** Every zone was baked in memory with rule 3d on and compared with its live grid.
        - Slivers are everywhere: every deck, rock and stair edge leaves part-filled cells. 271,898 such floors are in use across the maps.
        - The audit's "slivers in a flight" count (8,731) is mostly the sides of wide structures, such as Union War's amphitheatre stairs and Forest of Wandering's high walkway, which monsters cross in the middle. The Lounge's case needs a narrow flight approached at an angle, and the audit found no clear second one.
        - **Globally the rule would cut off real areas:** Luna Clan Field's outside (361,811 cells), Muris' rim (71,697), 1,778 cells in Desert of the Dead and 1,313 in Arumic Valley.
        - The client's 135 cm resync already limits the worst outcome on every map. A monster that loses a flight is put back at the next corner or stop.
        - Further cases are fixed per map as reported: `/nav path`, `nav-view.py --mark`, then `SLIVER_CM_BY_ZONE` or `OBJ_STEP_BY_ZONE`.
    - Whether a player can really climb that 1 m ledge in game is unchecked. If so, the bake should link it.
- **The first build crashed at startup:** freeing a `path_zones` string that `toml_get_str` never filled (no `[nav]` in `server.toml`) hits `FfiString`'s unchecked drop. It is now freed only when filled.

What was built:
- **`nav/nav_path`:** A* over the reached walkable floors, with these rules:
  - 8 neighbours, the bake's step rule between floors, and no corner cutting;
  - octile costs ×1.2, and cells narrower than the walker's class cost ×3;
  - a 128-cell window, with the start kept 8 cells inside;
  - generation-stamped arrays per zone thread; snapping within 2 m (start) and 3 m (goal);
  - a component check before searching, and the best partial path accepted when it ends 2 m closer;
  - bisection string-pulling into at most 16 waypoints, each leg checked with `LineWalk` to end on the path's own floor.
- **`TickBudget`** per zone tick: 6 searches, 8,000 cells, 3 ms of QPC time; 4,000 cells per search. A refused search keeps the current leg and asks again next tick.
- **Chase** (`CObjCHAR::Nav_Chase`, hooked into `Goto_TARGET` and `ProcCMD_ATTACK`'s out-of-range branch through a `CObjAI` virtual):
  - an attack chase checks its line every 300 ms, staggered; the line is clear only if it ends on the target's floor;
  - blocked, it plans and walks target-less legs while staying in `CMD_ATTACK`;
  - it re-plans when the target moves 4 m;
  - it goes back to running straight, with one `CharacterMoveAttack`, when the line clears, at the path's end, or once in range;
  - a failed search runs straight for 2.5 s.
- **Not pathed yet:** skill chases (`CMD_SKILL2OBJ`). Re-announcing them would mean re-sending `GSV_TARGET_SKILL`, which risks restarting the client's skill presentation.
- **Walks** (`Nav_PathTo`): the leash goes home on a path (`CMD_RUNAWAY` is accepted by the agent, and its arrival belongs to the agent); summon follow and AIACT_29 path to the owner's floor; CTRL+click orders path when blocked. All of them still walk straight when no path is found.
- **New viewers** see a pathing character as a target-less move to its announced leg. The speed resync covers leash paths.
- **Switches:** `pathing`, `path_zones` ("all" by default), the budgets and `path_lead_ms` in `[nav]`; `/nav set path|lead|cells`, `/nav set zones <list>`.
- **Readouts:** `/nav path [go]` plans from your target to you; the zone line adds searches by result, deferrals, cells, nav ms a tick, walks started, chases back to straight and re-plans.
- **Not built yet:** the dynamic overlay for the Cerberus ice wall (decision 10).

### Phase 6: the client draws the server's floor (lockstep)

- **Changes:**
  - server: `m_nNavZ` from `FollowZ` for monsters, summons and NPCs; the band switches to Stage B; the Phase 6 rows of §3.6;
  - client: §3.7;
  - clean-rebuild both and ship them together with the data.
- **Test:** a monster lured under the Shady bridge or a B1 bridge walks the floor below it, seen from both levels. The B3 altar is unchanged, nothing hangs on the Shady or Cerberus panels, stairs are smooth, and unbaked zones are unchanged.
- **Exit:** nothing drawn more than 1 m off its server layer at the test sites; leg-end snaps over 3 m are rare; CombatTrace clean.
- **Kill switch / size:** `send_z = 0` sends −32768, and the client falls back to `GetHeightTop`. About 350 lines of client and 150 of server.
- **Player notices:** monsters, NPCs and summons stand on the floor they are really on.

**Status (2026-10-29): validated in game.** Monsters under the Shady bridge are drawn and fought on the ground, maps look normal, and loot lies on the floor its monster died on.

What was built:
- **Server (Stage B, `[nav] send_z`, on by default):**
  - `Nav_Z()` of a monster, a summon or an NPC is its own walk floor (`m_nNavZ`, FollowZ), not where GetHeightTop draws it. The vertical band and the aggro scans now see a monster under a deck on the ground.
  - FollowZ (`Grid::Pick`) only stands on up-facing floors.
  - The floor goes out in three places, as -32768 for "unknown" (and for players):
    - **`gsv_MOB_CHAR.m_nPosZ`**, a new field after `m_nQuestIDX`, inherited by `gsv_NPC_CHAR`. **This is the lockstep change:** client and server must ship together.
    - **`target_pos.z` of `CharacterMove` / `CharacterMoveAttack`** and the path legs carry the mover's floor as the move starts. That is the slot the original game used ("the target's z is really the start height", client `Reset_Position`). Players still send 0.
    - **`gsv_STOP.m_nPosZ`** carries the floor for monsters.
  - **Spawn floors:** a spawn starts on its caller's floor before it is announced. Summon skills (`Nav_PlaceNear`), AI calls for help (`Create_PET`, immediate and delayed through `tagCalledMOB.m_fFloorZ`), GM `/mon` and regen points all pass one; Cerberus spawns keep the visual-top default.
  - `/nav set z 0|1` switches it live.
- **Engine:** `getFloorBelowInScene(x, y, fromZ, default)` returns the highest up-facing surface of the colliding objects at (x, y) at or below fromZ, "not moveable" ones included. A down-facing hit (a ceiling's underside) is passed through and cast again.
- **Client:**
  - `CObjCHAR::m_fNavRefZ` is the floor a monster or an NPC walks on. It is seeded by `SetServerZ`: the spawn packet places it; a move or stop replaces it only when none is held, when it is stale (not followed for 1 s, out of view) or more than 3 m off.
  - `CObjCHAR_Collision::MonsterFloorZ` follows it frame by frame with the server's rule: the highest up-facing surface no more than 135 cm above last frame's floor, never under the terrain.
  - Without a server floor (a client-only puppet, `send_z` off), it draws on GetHeightTop as before. Avatars are untouched. The move target's z stays the 0 it always was.
- **In-game test (2026-10-29):** the bridge and a few maps look normal and behave as expected.
- **Loot under a deck (validated after a fix):**
  - A field item gets its floor at the drop (`CObjITEM::m_fFloorZ`, from `CObjCHAR::Nav_FloorAt`): the up-facing floor at its spot nearest its dropper's (a dead monster, an AI drop, a player).
  - `gsv_ADD_FIELDITEM.m_nPosZ` (new, size-gated on the client) sends it.
  - **First version, resolved when announced:** the first announcement carried no floor. `Add_DIRECT` announces an item before `SetZONE`, so it had no zone and no walk grid yet. Loot landed on the deck and was right only on a re-announce after a respawn or teleport.
  - The client's `Add_GndITEM` uses its long-disabled reference-height branch, through `CObjCHAR_Collision::FloorBelow`. It serves both routes: the broadcast, with the server floor, and the inline drop, with the dead monster's height. Without one it uses GetHeightTop.
- **Shady Jungle spiral ramp (after the commit, validated in game):** a chaser sometimes ended on the ground 26 m below the ramp and fought from there.
  - **Client:** a leg grazing the ramp's edge found no mesh under its exact spot and dropped the monster. A fall deeper than 3 m now keeps a floor at its own height found within 80 cm (`MonsterFloorZ`).
  - **Server, the real fall:** a chaser checked its line only every 300 ms and ran straight at the target's current position in between. Near the ramp's inner edge that line crossed the gap by the stump, and FollowZ fell to the ground. Range is still 2D, so it attacked from below (Phase 7).
  - **Server fix:** the line is checked every tick. When it is blocked, the chaser keeps its last clear line until the next search, and also when the budget defers one. The 2.5 s straight walk after a failed search stays.
  - Shady with a player afterwards: object work avg 0.2–0.25 ms, p99 0.5–2.2 ms. Idle Sikuku Prison p99 ran 3.7–7.0 ms across the day's restarts, noisy and not affected by chaser checks.
- **Aim circle on the Shady Jungle rope bridge at (5776, 5220) m (validated in game):** the circle fell to the ground there.
  - The bridge is LIST_DECO object 98 with collision 12 (polygon, "not moveable"). GetHeightTop, and so the aim pick and the circle's surface sampling, skip such parts. The avatar walks it on its own foot collision, and the bake has it as a reached walkable floor, but its visual top is the terrain.
  - Tree crowns and cave ceilings are flag 12 too. So `IsWalkableNode(h, true)` takes them only while the avatar, or the shape's centre, stands on an object (1.5 m over the terrain), and the aim pick only within 3 m of the avatar's height.
- **Other players on the Shady stump and rope bridges (2026-10-31, validated in game with two players):** a player on the stump top was drawn falling to the ground on everyone else's screen, while the ramp looked fine.
  - The stump is LIST_DECO_EJ object 161, all four parts collision 12; its top is the root part, the ramp the children. The remote-player height path (`CObjCHAR_Collision::CollisionCallback_Other`) dropped "not moveable" nodes, but `CollectContact` asks only of nodes with user data, and a map object sets it on its root alone. So the root was dropped and the flagged children kept. The rope bridges (98) are a single root part and fell the same way.
  - **Fix:** the callback keeps them, as the avatar's own foot collision always did. Other players now stand where their own client puts them; this covers remote carts and castle gear, which use the same callback.
  - **Coming into view on a deck:** `CObjAVT::Create` places a player on GetHeightTop, which skips flag 12. `gsv_AVT_CHAR.m_nPosZ` existed but was never filled; the server now sends `Nav_Z()` (the floor it walks the player on, -32768 without a grid) and `Recv_gsv_AVT_CHAR` applies it before `Recv_tag_ADD_CHAR` builds a rider's cart. Client and server ship together: an older server leaves junk in the field.
- **Not changed: character shadows on decks.** The engine lets only terrain receive the dynamic character shadow (`zz_scene_octree::update_receive_shadow`, a 2005 "terrain only" test line). A character on a bridge casts its shadow on the ground below. This predates the plan.

### Phase 7: 3D attack range and aim (lockstep, optional)

- **Changes:**
  - the band in `IsInRANGE` and `Goto_TARGET` on both sides (server +100 cm slack), with the constants sent at zone join;
  - the monster attack band and the vertical give-up (decision 4);
  - the "another level" message;
  - aim picks on objects, aim Z tails and telegraph Z;
  - a cast band from the caster to the aim point.
- **Test:**
  - melee from the deck into the ravine is refused with the message, with no endless walk;
  - an archer on a 20 m cliff;
  - a monster under the deck neither hits upward nor waits forever;
  - hillside attacks are the same with the switch on or off;
  - ground AoE aimed at the deck lands there, and its telegraph draws there.
- **Exit:** no chase parks short of its target; both sides refuse layer cases identically.
- **Kill switch / size:** `vertical_range`, sent at zone join. About 350 lines of client and 250 of server.
- **Player notices:** attacks and casts respect floors; ground skills can be aimed onto decks.

**Status (2026-10-29): validated in game.**

Server only. In this codebase the server walks a player's attack and targeted skill in (the client follows `gsv_ATTACK` / `gsv_TARGET_SKILL` and halts on `gsv_STOP`), so the verdict lives there alone. No client change, no zone-join constants: both sides cannot disagree.

- **Attack and targeted-skill range** use `CObjCHAR::IsInATTACK_RANGE`: the 2D range plus `Nav::InAttackBand`, the AoE band (5 m + 70% of d) plus 1 m of slack. It is wired through `Goto_TARGET(..., bAttack)` (ProcCMD_ATTACK, ProcCMD_Skill2OBJECT) and the attacking branch of ProcCMD_ATTACK. NPC talk and shop ranges stay 2D.
- **Within 2D range but on another level** (`Nav_OtherLevel`):
  - a **player** is refused at once ("That target is on another level..."): stop, and a skill cancelled before it costs. No endless walk.
  - a **monster or summon** keeps going while a path takes it up or down. Stuck (no path) for `vertical_giveup_ms` (3 s), it gives up (decision 4): a monster walks home and takes hits on the way; a summon stops and its follow loop resumes. The zone line counts give-ups.
- **A ground skill aimed onto another level** is refused before it costs (decision 7). The area lands on the floor at the aim nearest the caster (Phase 3), and that floor must be in the band from the caster.
- **Switches:** `[nav] vertical_range`, `vertical_giveup_ms`; `/nav set range|giveup`.
- **First test:** fine overall. A ground skill aimed onto a lower level within reach played its effect at the caster's height: `Recv_gsv_POSITION_SKILL` set the target Z to the caster's.
  - **Fix (client):** `CObjCHAR_Collision::FloorBelow(aim, caster Z)` gives the highest surface at the aim no more than a step above the caster, never under the terrain. That matches the server's "floor at the aim nearest the caster".
- **Not done:** the client's own `IsInRANGE` (attack-walk halt, owed swings) stays 2D, since it follows the server's packets. COND04 (AI distance condition) stays 2D. There is no aim Z from the client: the aim floor is the one nearest the caster.

### Phase 8: optional, each on its own

- OBJ_MOB walk-line checks.
- Remote-player spawn Z via the unused `gsv_AVT_CHAR.m_nPosZ` (a change to player presentation, so it needs your sign-off).
- Drop edges.
- 5×5 sector scans for large radii.
- `CANTMOVE`/`SETPOS` validation.
- An OBJ_MOB `more_legs` flag.
- The Recast/Detour fallback: two small static-lib vcxprojs, like `thirdparty/fmt.vcxproj`.

## 5. Pre-existing bugs

| Bug | Evidence | Fixed in |
|---|---|---|
| MOV mirrored north–south inside each tile | `S/zonefile.cpp:310-321` | docs in 1; MOV retired in 4 (decision 8) |
| `IsMovablePOS` treats an off-grid X as walkable | `S/zonefile.h:131-136` | 1 |
| Shared scan iterator not re-entrant | `S/gs_threadzone.h:78-83`, `S/cobjchar.cpp:1223` | counted in 0, fixed in 1 |
| `CANTMOVE` reads Z through `cli_MOUSECMD`, and teleports without a sector update | `S/gs_user.cpp:2836, 2848` | 1 |
| `JOIN_ZONE` Z never written by the client | `C/network/sendpacket.cpp:209-214` | ignored from 2; written in 6 |
| `gsv_STOP.m_nPosZ` is heap garbage | `S/cobjchar.cpp:293-296` | zeroed in 1, filled in 6 |
| No arrival clamp (overshoot 33–85 cm); uncapped `dwPassTIME` | `S/cobjchar.cpp:2074-2091` | 0 / 1 |
| AIACT_08 divides by zero when both stand on the same spot (inferred) | `S/ai_lib/ai_action.cpp:224-255` | 1 |
| `CharacterMoveAttack` built with `CharacterMoveBuilder` | `S/network.cpp:116` | 0 |
| IFO, regen and event Z discarded | `S/zonefile.cpp:161-163, 501-504` | 2 |
| Quest COND_006/021 radius ×10,000 (15 COND_006 + 24 COND_021 = 39 triggers in 7 QSDs pass at almost any distance); the comment at :990-997 claiming 343 COND_021 is stale | `src/common/shared/io_quest.cpp:539, 998` (check-time ×100) and `:3170-3175` (load-time ×100 in `Init_COND`) | left alone (decision 11) |
| Item pickup has no range check | `S/gs_user.cpp:4417-4475` | out of scope |
| Shield reflect is 4,000 cm; the comment says 4 m | `S/cobjchar.cpp:1315` | out of scope |
| AIACT_37 range not ×100 at load | `src/common/shared/cai_file.cpp` | out of scope; check |
| Client CLAUDE.md says five `AdjustHeight_*` paths; there are seven | `C/cobjchar_collision.h:84-90` | doc fix |

## 6. Decisions (answered by the user, 2026-10-28)

These answers override anything earlier in this document that disagrees with them.

1. **Shady Jungle spot:** both the `dari` bridge and the cliff tops are tested.
2. **AoE vertical rule:**
   - the band, 5 m + 0.7·d, is the default (tighten to 4 m + 0.6·d if cliffs still feel wrong);
   - the sphere is available only as a switch value;
   - the same-surface check is off by default.
3. **Order:** Z first, so phases run in their numbered order, 0 → 7.
4. **Unreachable targets:** the hybrid, **without the heal.**
   - A failed search walks straight.
   - From Phase 7, the monster gives up after 3 s, but only when the target is outside the band.
   - A leashing monster still takes damage without fighting back. That is accepted as old-school exploitable fun.
5. **Phases 6 and 7 both happen,** one right after the other.
6. **Friendly area buffs and heals stay 2D.**
7. **Ground AoE from a deck aimed below** is allowed within the caster-to-aim band, and refused with the "another level" message outside it.
8. **MOV is retired once the bake drives walkability (Phase 4).**
   - **Phase 1 therefore drops the in-game row flip**, so there are fewer AI changes to validate and no 18-zone behaviour shift for a grid that is going away.
   - What stays:
     - the documentation fixes (`chunk.py` docstring, FORMATS.md, `MOV.cs` and its tests);
     - the bake's MOV-agreement metric, computed on flipped rows. If the bake agrees with the flipped rows 82–100% of the time, that confirms the orientation offline.
   - Zones with no bake keep today's MOV behaviour unchanged.
9. **Spawn fallback:** the regen centre, and points that keep falling back get their data fixed.
10. **Cerberus ice wall:** monsters path around it, behind its own switch, after one test run.
11. **Quest COND_006/021 radius bug: left alone.**
    - Today those 39 conditions ("be within N m of a spot or NPC") pass from almost anywhere.
    - Fixing them would make players really stand there, and any quest with a slightly wrong position in its data would become impossible to complete.
    - The user will not re-test 39 quests, so this stays a known quirk outside this project.
    - **It did break one thing (2026-11-17):** the Forgotten Temple's arrival chain moves a
      91-130 player standing near B1's entrance to the band's start point, which lay inside
      the 2 km "20 m", so every arrival warped again: an endless reload loop. Fixed in the
      data, not the code (`scripts/fix-temple-rewarp-radius.py`, radius 20 -> 1); an audit
      of every zone's arrival chain found no other same-zone warp.
12. **RAM:** approved; there is plenty of headroom.
13. **Water:** monsters keep walking lake beds as designed. No deep-water cost, so the Phase 8 deep-water item is dropped.
14. **Deployment:** the server runs locally and reads `data/` directly (`C:\Users\Thomas\Desktop\Rose\rose-next-classic\data`).
    - The bake writes `.lnav` next to each ZON under `data/`, and that is the whole deploy.
    - `server-data.ps1` is not involved.
    - The only shipping rule left is that `pack.ps1` must exclude `*.lnav` / `*.LNAV` from `rose.vfs`.

## 7. Risks and containment

| Risk | Containment |
|---|---|
| The bake misclassifies floors or walls | PNG review, NavAudit, deliberate mistakes, MOV agreement, manual seeds, per-zone kill |
| RAM in a non-LAA process | Phase 0 headroom, Phase 2 exit ≤ 80 MB, §3.1 levers |
| Tick stretch | expansion and QPC caps, no A\* for wander, Phase 5 p99 exit |
| Corner hitches | Phase 0 probe, `leg_lead_ms`, class-2 waypoints |
| Gameplay shifts (the band removes 6.5–9.5% of random Shady pairs, mostly across cliffs; more leashes in caves) | live switches for A/B, logged numbers, tunable constants |
| New safe spots | straight fallback; vertical give-up only once both sides are banded; no evade heal (decision 4, accepted) |
| Client and server Z disagree after Phase 6 | continuity with an interpolation fallback, Z-aware smoothing, leg-end snap, sentinel |
| An old client misreads `gsv_MOB_CHAR` | ship client, server and data together |
| Stale bake; MOV flip wrong | CRC check and `--check`; the flip's own switch |
| Nav file packed into `rose.vfs` | lowercase extension, excludes for both cases, dry run |
| Not modelled: moving platforms, MORPH, water depth | listed in the report; behaviour there is unchanged |

## 8. EVO-REFORGED: what we borrow and what we do differently

EVO has no licence. We borrow ideas and tuning values only; no code is copied or transcribed.

**Borrowed:**
- **Search:** line check first, A\* only when blocked; per-tick budgets; 2.5 s of straight walking after a failure.
- **A\* internals:** generation stamps, a centred window, best partial path, weighted octile heuristic.
- **Paths:** snap tolerances; smoothing to 16 waypoints; target-less legs shown to new viewers; a re-plan after the target moves 4 m.
- **Bake:** a body-height wall band, cylinders as walls, preview PNGs, a grid shared per ZON.

**Done differently:**
- **Data model:** layers instead of the max of the tops. Floors come from geometry and reachability, not from "flag 12 = wall". Clearance classes replace a point agent.
- **Obstacles:** lump 11, water, warps and event objects are all included.
- **Movement:** an arrival clamp, which EVO's arrival test relies on and our mover lacks.
- **Cost control:** budgets sized for 10 Hz, plus a wall-clock cap.
- **Hook coverage:** the hooks also catch CTRL+click orders and skill chases. Exclusion flags are checked before moving; `GS_BOSS.cpp:555` gets that order wrong.
- **Implementation:** `_to_model` transforms, an overlay on baked grids, and path state behind a pointer.
- **Z support:** EVO has none.

## Appendix: evidence

The measurement scripts are research scratch, archived **locally** in `build/nav-research/` (gitignored, not in the repo). They are read-only Python over `scripts/rosefmt`, run with `python -I`; some import siblings from their own folder.

Promote any of them into `scripts/` when a phase needs it. Phase 3 turns the band measurement into `scripts/nav-band-calibrate.py`.

| What | File (under `build/nav-research/`) |
|---|---|
| Research reports | `reports/` (server movement, range checks, client height, map data, EVO pathing, protocol, critic) |
| Proposals, judges, fact-checks | `design/` |
| AoE band and sphere table | `scripts/synth/band_frontier.py`, `design/band.py` |
| Panels | `design/panels_all.py` |
| Quest radius triggers | `design/qcond.py` |
| Roof spawns per zone | `scripts/proto_layers.py`, results in `scripts/critic/layers_<row>.txt` |
| `GetHeightTop` replica | `scripts/clientz_heighttop.py` |
| MOV row-order tests | `scripts/mov_orient2.py`, `scripts/mov_slope.py`, `scripts/mapfmt/mov_regen.py`, `scripts/mapfmt/mov_vs.py` |
| Layers and bridges | `scripts/mapfmt/layers.py`, `scripts/mapfmt/bridges.py`, `scripts/critic/bridges_all.py` |