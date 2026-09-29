# CLAUDE.md — Rose Next Game Server

## Overview

The game server (`sho_gameserver`) is the main server handling real-time gameplay: combat, movement, NPCs, AI, zones, parties, and chat. C++ built with VS2019 targeting x86. Uses IOCP for networking and PostgreSQL for persistence.

## Architecture

```
main.cpp → lib_gsmain (init) → Zone Threads (gs_threadzone)
                                    ↓
                              Per-zone game loop:
                              - Process player input packets
                              - Run AI (cobjavt / ai_lib)
                              - Compute combat (srv_common/)
                              - Broadcast state changes
                              - SQL thread for persistence
```

## Key Source Layout

| File/Dir | Purpose |
|----------|---------|
| `src/main.cpp` | Entry point |
| `src/lib_gsmain.cpp/h` | Server initialization and main loop |
| `src/gs_threadzone.cpp/h` | Zone thread — per-zone game ticks |
| `src/gs_user.cpp/h` | Player session management |
| `src/gs_listuser.cpp/h` | Connected user tracking |
| `src/gs_threadsql.cpp/h` | Async SQL operations |
| `src/gs_party.cpp/h` | Party system |
| `src/gs_socketlsv.cpp/h` | Inter-server communication (login/world) |
| `src/network.cpp/h` | Packet broadcasting helpers |
| `src/cobjchar.cpp/h` | Server-side character object |
| `src/cobjavt.cpp/h` | Avatar (player) object |
| `src/cobjnpc.cpp/h` | NPC/monster object |
| `src/cobjevent.cpp/h` | Event objects |
| `src/cobjitem.cpp/h` | Dropped item objects |
| `src/status_effects.cpp/h` | Buff/debuff system |
| `src/srv_common/` | Combat calculations, skill processing, damage application |
| `src/ai_lib/` | Server-side AI behavior |
| `src/common/` | Code shared with worldserver |

## Networking

- IOCP-based (inherited from `common-server/`)
- Packet handlers: `Recv_cli_*()` for client packets, `Send_gsv_*()` for server packets
- Broadcasting: `send_packet()` (single), `send_packet_party()` (party), `send_packet_nearby()` (sector-based, 9 adjacent sectors)
- FlatBuffers for some packet types (movement, stat updates)
- Inter-server: communicates with LoginServer and WorldServer via `gs_socketlsv`

## Zone System

- Each zone runs in its own thread (`gs_threadzone`)
- Zones divided into sectors for spatial queries
- `send_packet_nearby()` broadcasts to players in 9 adjacent sectors
- Zone data loaded from STB files in `data/` directory

## Combat Flow

Live combat is server-authoritative. The client presents server damage events; it must not calculate live combat damage.

1. Client sends attack/skill packet or monster AI chooses an attack.
2. Server rolls/calculates damage with the shared calculation code compiled server-side.
3. `CObjCHAR::Apply_DAMAGE()` applies authoritative HP immediately and produces the final `uniDAMAGE` result.
4. Server emits FlatBuffer combat presentation data:
   - `CombatSwing` for normal melee/bow/gun attacks that should start a confirmed client swing.
   - `DamageEvent` for skills, projectile impacts, status ticks, counters, missing-attacker fallback, and immediate damage.
5. `DamageEvent.damage_value` is the visible hit delta; `hp_after` is the authoritative checkpoint. Do not rely on `UpdateStats.hp` / `GSV_SET_HPnMP` to present combat HP decreases.

### A Repeat Attack Order Is A No-Op

`CObjAI::can_attack()` answers false for two different things: "may not attack this" (no target, disguised/transparent, no PvP) **and** "already attacking this target". `CObjAI::SetCMD_ATTACK`'s else-branch reads any false as the first kind — clears the target, sets `CMD_STOP` — and `CObjCHAR::SetCMD_ATTACK` then broadcasts `GSV_STOP`. So a second `CLI_ATTACK` on the same monster *cancelled* the attack the first one started: double-click or spam-click a mob while running towards it and the character stopped (alpha report, 2026-09-19). The client's own de-dupe guard (`CSevenHeartUserInput::SetTargetObject_Normal`) cannot cover it, because it tests for `CMD_ATTACK` and the avatar is in `CMD_MOVE` for the whole approach (`Recv_gsv_ATTACK` → `SetCombatAttackIntent` → `SetCMD_MOVE`). `SetCMD_ATTACK` now returns false up front when the order repeats the current `CMD_ATTACK` target: nothing is sent, state and cadence are untouched. `can_attack`'s clause stays — `SetCMD_RUNnATTACK` relies on it to keep mob AI from re-broadcasting every tick. Do not "fix" this client-side by widening the guard to `CMD_MOVE`: the server drops to `CMD_STOP` without a broadcast when a target is lost, so a stale match would swallow a legitimate click. `common/cobjai.cpp` is **CP949** — edit it as bytes, the Edit tool re-encodes every Korean comment.

### PVP Ally Rule (clan fields, party zones)

`ZONE_PVP_STATE` (LIST_ZONE col 18): 0 none, 1 `AllExceptClan` (Junon/Luna Clan Field, Colosseum), 2 `AllExceptParty`, 3 `All`, 11 clan house (safe). In a clan field the zone's entry trigger runs `REWD_020` → `Set_TeamNoFromClanIDX()` (team = `100 + ClanID`), and the **client** decides "enemy" from that team number alone (it has no `Is_ALLIED` override) — so it simply never sends `CLI_ATTACK` / `CLI_TARGET_SKILL` at a clan mate. That was the *only* enforcement: `CObjAVT::Is_ALLIED`'s clan branch tested `GetGUILD()`, an un-overridden stub that always returns NULL (`CGuild` is a bare forward declaration), so server-side a clan mate was an enemy. Invisible for targeted attacks, but an AOE has no client-side target to filter — victims come from a purely spatial sweep (`FindFirstCHAR`) gated only by `Skill_IsPassFilter` → `!Is_ALLIED` — so AOEs hit clan mates (alpha test #2, 2026-09-22). `Is_ALLIED` now compares `GetClanID()` and falls back to the team-number compare, so the server agrees with the client for clanless players and GM team overrides too; `SKILL_TARGET_FILTER_GUILD` uses the clan id as well (it could never pass before), and `CObjAI::can_attack` refuses a user attacking an allied user, so a crafted packet cannot do what the client UI refuses. Do not add a virtual `GetClanID` to `CObjCHAR` for this — vtable change in the widest header; cast to `CObjAVT` after an `IsUSER()` test.

### Combat Presentation Packet Rules

- `DamageEvent` fields: `event_id`, `defender_seq`, `attacker_id`, `defender_id`, `raw_damage`, `damage_value`, `hp_after`, `presentation_kind`, `lethal`, `skill_id`, `source_attacker_id`. The last two (0 = unknown) are **display metadata for the client damage meter only** — neither server combat logic nor client presentation may branch on them; presentation keys on `attacker_id`.
  - `skill_id` flows via `Give_DAMAGE(..., kind, nSkillIDX)` → `Send_combat_damage_event` → `build_damage_event_packet`; `Skill_START` SKILL_TYPE_06 passes `Get_ActiveSKILL()`, the shield counter passes `nShieldSKILL`, status ticks pass the applying skill, all other call sites default to 0. `CombatSwing`'s embedded event always carries 0 (normal attack).
  - `source_attacker_id` = who to *credit* when it differs from `attacker_id`: the DoT caster (status ticks put the victim in `attacker_id`; the poison tick passes `m_iTargetOBJ[ING_POISONED]` — `StatusEffects::m_iTargetOBJ` stores the **speller** for skill-applied statuses, `Skill_ApplyIngSTATUS` passes `pSpeller->Get_INDEX()`) or a summon's owner (`resolve_summon_owner_index`, anonymous namespace `cobjchar.cpp` — same `GetCallerUsrIDX` + `GetCallerHASH` validation as `mirror_lethal_packet_to_summon_owner`; auto-applied in `Send_combat_swing` and, when no explicit source is given, `Send_combat_damage_event`).
- `presentation_kind` controls client timing: `MeleeHitFrame`, `ProjectileImpact`, `Immediate`, `StatusTick`, `MissingAttacker`.
- **Both combat sends broadcast around the *defender*** (`Send_combat_swing` since 2026-09-23; `Send_combat_damage_event` always did). The clients that hold — and can present — a damage event are exactly the defender's 9 sectors; `recv_combat_swing` early-outs without the defender object, so an attacker-centered broadcast adds nothing and *misses* clients in the one-sector differential ring whenever attacker and defender straddle a sector boundary. That miss was permanent for a kill: a damage-killed mob's removal is never broadcast (`CObjMOB::Make_gsv_SUB_OBJECT` returns false at `HP <= 0` — the death packet is the only removal notification a client ever gets), so a client that missed the lethal swing kept a live-looking, unclickable monster forever ("some monsters he sees are actually dead server-side"). Keep any new combat/death send defender-centered, and keep `mirror_lethal_packet_to_summon_owner` beside it for owners outside that neighborhood.
- Normal attacks should send `CombatSwing` after damage is calculated. This guarantees the client queues the event before starting the swing animation that will consume it.
- Projectile-capable damage must use `ProjectileImpact` so the client waits for bullet collision. Immediate presentation is only for true immediate effects such as status ticks, shield counters, and missing-attacker fallback.
- Keep direct HP stat sync as reconciliation only. Combat HP decreases need a `DamageEvent` path so digits, HP, hit feedback, and death presentation stay in one transaction.
- **Lethal events double as summon-gauge sync.** The client decrements its `m_SummonedMobList` (summon gauge) when a lethal `CombatSwing`/`DamageEvent` arrives, so the `lethal` flag must be set on every kill. Because `send_packet_nearby` only covers 9 sectors around the broadcast center, `Send_combat_swing` / `Send_combat_damage_event` also call `mirror_lethal_packet_to_summon_owner` (anonymous namespace, `cobjchar.cpp`): when the dead defender is a summon (`GetCallerUsrIDX` + matching `GetCallerHASH`) and the owner is not `IsNEIGHBOR` of the broadcast center, the same packet is sent to the owner directly — mirroring what legacy `Send_gsv_DAMAGE2Sector` did. Non-combat despawns (owner death/zone change, bonfire distance) emit no combat event at all and still use the synthetic legacy `GSV_DAMAGE` in `CObjSUMMON::Proc`.
- Legacy `GSV_DAMAGE_OF_SKILL` remains in use for several skill types. It must include the defender's post-`Apply_DAMAGE` HP in `m_iHP_AFTER`; the client converts this into `DamageEvent.hp_after` and must not infer the checkpoint from visible HP.
- **Speed fields on the wire are totals, and the client applies them verbatim.** `gsv_AVT_CHAR.m_nPsvAtkSpeed`, `gsv_SPEED_CHANGED.m_nPsvAtkSPEED` and `UpdateStats.attack_speed` are all `total_attack_speed()` — `stats.attack_speed` (weapon + passives, from `update_speed()`) + `Adj_ATK_SPEED()` (buffs + goddess) + server.toml `base_attack_speed` for users. The 2005 "passive only" field names are historical. `Send_gsv_SPEED_CHANGED` is the 9-sector broadcast that keeps observers' copies current (every `classUSER::UpdateAbility`, buff apply/expire via `sync_visible_skill_stats_after_effect` / `Proc_IngSTATUS`, passive learn); `send_update_stats_all` is owner-only. Since 2026-09-19 the client adds nothing on top — it used to add its own buff delta again, so a buffed player's own screen ran faster than the server did.
- **`Set_MOTION` stores `m_fCurAniSPEED` on every attack-motion call**, not only when `Chg_CurMOTION` reports a new pointer. Consecutive swings from a standstill re-issue the same motion, so an attack-speed buff applied mid-fight used to reach the cadence only at the next motion *change* (expiry alone had a retune in `Proc_IngSTATUS`). The client's `Set_MOTION` mirrors this, so both sides pick up a new rate on the next swing.

### AI Script Guard

Monster AI script action `AIACT24` / `F_AIACT24` blocks hostile `btTarget == 0` condition-checked target skills unless that target is already the monster's current combat target. This prevents non-aggro scripted projectile attacks from sending early target-skill/damage packets. `btTarget == 1` current-target combat skills, `btTarget == 2` self skills, and allied/friendly skills remain allowed. **Since 2026-09-14 the block applies to non-monster casters only** (`Get_ObjTYPE() != OBJ_MOB`): a monster casting a hostile skill at a character its idle pattern found is how caster mobs *aggro* — Nigaki's `kh_2676.aip` has "enemy within 12 m → Voltage Jolt" as its only offensive action besides a 20 % chance to turn on whoever hit it — and blocking it left it a passive heal-bot that never fought back. The cast goes through and `SetCMD_Skill2OBJ` makes that character the current target, so every later cast is a normal combat skill. Logged as `non_aggro_script_skill_aggro`; the blocked case keeps `non_aggro_script_skill_blocked`. Note the server has no auto-aggro on damage: `Apply_DAMAGE` only fires `Do_DamagedAI`, so an AI file that relies on its source server's auto-targeting (a `beaten` pattern at 20 %) fights only as often as its AI says.

### A Slow Must Not Wrap The Move Speed (2026-09-29)

`CObjCHAR::total_move_speed()` is `stats.move_speed + Adj_RUN_SPEED()` returned as a
`uint16_t`, and a slow is a *flat* value sized from the speed at the moment it landed -- for
a monster in combat, its run speed. When the monster later walks (idle, walking home after a
kill), the same flat slow comes off its much lower walk speed: a Rot Tracker slowed by 392
(skill 1586, 49% of its 800 run) walked at 300 - 392 = -92, wrapped to **65444** -- 654 m/s.
The server's copy crossed 10 m a frame and every client followed it: the "teleport" that
always came right after the monster killed the player's hawk (the kill sends it walking
home). It is floored at 30% of the current base now (the strongest slow the house authors is
70%). Anything that adds a flat speed debuff must go through `total_move_speed()`, never
subtract from `stats.move_speed` directly. Client trace that named it: `CombatTrace position
jumped ... move_speed 65444`. Validated 2026-09-29: three fights with the slow and the hawk
used heavily, no jump.

### A Junk NPC Row Must Degrade, Not Kill

`CObjMOB::Init` derives max HP as `NPC_LEVEL * NPC_HP` and sizes the per-attacker saved-damage table (`m_SavedDAMAGED`, EXP/drop share) as `NPC_HP / 8 + 4` into a **short**. Three LIST_NPC rows (996 Moss Golem, 997 Nepenthes, 998 Turak — unspawned duplicates, HP column 7.9-13 million where the largest real value is 10,701) overflowed both: a negative max HP and a negative table size that `new[]` read as ~4 billion, so `/mon 996` killed the process with nothing in the log (2026-09-15). `ClampedMobMaxHP` / `ClampedSavedDamageCNT` (64-bit product clamped to `INT_MAX`; table capped at 4096) cover `Init` and `Change_CHAR`. The rows stay as they are — the balance passes leave them alone on purpose — and the Eldeon Moss Golem that actually spawns is **1591**. `/mon` and `/mon2` (`cheatcmd.cpp`, `MobRowRefusalReason`) now refuse such rows with a whisper saying why — no model, level 0, or an HP column over 20,000 — so a tester summoning by number cannot get an invisible or absurd monster. Survey 2026-09-15: of 1,426 named rows, 646 are placed on a map and 111 more are reachable through AI summons; of the 669 unreachable, 212 have junk stats, 183 share a name with a live row, 302 are coherent unused retail/event content. Ids are load-bearing (drop tables, AI, quests, dialogs), so rows are never deleted or renumbered.

### Skill Damage Presentation

`SKILL_TYPE_06` (projectile magic — Icebolt, Lightning, etc.) `Skill_START` must tag the `DamageEvent` with `ProjectileImpact`, not `Immediate`. `CObjCHAR::Give_DAMAGE` takes trailing `Packets::DamagePresentationKind kind = Immediate` and `short nSkillIDX = 0` parameters so the projectile-magic call site can pass `ProjectileImpact` + the active skill while shield-counter / status-tick / cheatcmd call sites keep the defaults (`nSkillIDX` is meter display metadata; see Combat Presentation Packet Rules). `CObjCHAR::IsProjectilePresentedSkill(short)` wraps the shared `Rose::Combat::is_projectile_presented_skill(skill_type, bullet_no)` helper, which the client also uses. The rule is: `05/06` projectile-presented, `03/19` projectile-presented only with a bullet id, and target-bound `09/11/13` never projectile-presented because their `BULLET_NO` is an effect graphic, not a tracked projectile.

### Status Effect Application

`Skill_ApplyIngSTATUS` returns `btSuccessBITS` (a per-slot bitmask) that is set **before** `UpdateIngSTATUS` is called. `UpdateIngSTATUS`'s own return value is consumed only to decide whether the *target's* current command should be cleared (`Del_ActiveSKILL` + `SetCMD_STOP`) — that's used for stun/sleep, which return non-zero specifically for `FLAG_ING_FAINTING | FLAG_ING_SLEEP`. Do not confuse this with packet suppression: `Skill_ChangeIngSTATUS` sends `GSV_EFFECT_OF_SKILL` whenever `btSuccessBITS != 0`, regardless of `UpdateIngSTATUS`'s return. Continuing-target debuffs (e.g. Fire Ring's `ING_DEC_DPOWER`) follow this path and persist for their `SKILL_DURATION`; `Get_DEF()` reads `Adj_DPOWER() = m_nAdjVALUE[ING_INC_DPOWER] - m_nAdjVALUE[ING_DEC_DPOWER] + m_nAruaRES`, so the reduction takes effect on every subsequent damage formula evaluation. Debug traces under `SkillStatusTrace` (apply site) and `defender_def` / `defender_dec_dpower` in the `CombatTrace server combat swing` line let you verify the debuff is being read on each swing.

## Configuration

`server.toml` in working directory (see `doc/server.toml.example`):
```toml
[database]
connection_string = "postgres://user:pass@localhost/rose-next"

[gameserver]
ip = "127.0.0.1"
port = 29200
server_name = "Channel 1"
data_dir = "C:\\path\\to\\data"
log_level = 2
```

Log levels: 0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error, 5=Off

## Summon Control (CTRL+Click)

Players can directly command their summons (phantom swords, companions) with CTRL+click. The client sends `CLI_SUMMON_CONTROL` (`net_prototype.h`, id `0x0776` — distinct from the legacy unused `CLI_SUMMON_CMD` stance packet) carrying a command byte (`SUMMON_CTRL_MOVE` / `SUMMON_CTRL_ATTACK`), a target object index, and a destination.

- `classUSER::Recv_cli_SUMMON_CONTROL` validates (has summons, not stunned, attack target is a live hostile mob in-zone, move within 150 m) and calls `CZoneTHREAD::CommandSummons_MoveTo` / `CommandSummons_Attack`.
- Those helpers iterate `m_ObjLIST` and match summons via `GetControllableSummon` (object is `OBJ_MOB`, alive, `GetCallerUsrIDX() == owner index`, **and** `GetCallerHASH()` matches — caller index alone is reused). There is no owner→summon list; iteration is the source of truth.
- The order applies to **all** of the player's summons at once. Stationary summons (Bonfire, walk+run speed 0) are excluded from move orders.

### CObjSUMMON manual-order window

`CObjSUMMON` has a manual-order window (`m_dwManualOrderUntil`, `MANUAL_ORDER_MS` = 5000, `SetManualOrder()` / `IsManualOrderActive()`). While active it suppresses **both** the continuous follow-the-owner re-plan in `CObjSUMMON::Proc()` **and** AIP auto-aggro (via the `Do_StopAI` override) so the player's explicit order is honored, then the summon resumes normal follow behavior. `SetManualOrder()` is called unconditionally on every order (not gated on the move succeeding) so the window is always set; a re-order refreshes it. The follow loop itself (toward the always-walkable owner) still uses `SetCMD_MOVE2D`.

### PlayerOrderMoveTo (do NOT use SetCMD_MOVE2D for player clicks)

Move orders go through `CObjSUMMON::PlayerOrderMoveTo`, **not** `SetCMD_MOVE2D`. `CObjCHAR::SetCMD_MOVE2D` rejects destinations where `GetZONE()->IsMovablePOS()` is false (non-walkable cells) and silently keeps the old command — which made CTRL+click move orders "randomly" ignored depending on the exact terrain cell clicked. The avatar's own click-to-move has no such gate (it relies on client CANTMOVE collision feedback). `PlayerOrderMoveTo` replicates SetCMD_MOVE2D minus the IsMovablePOS check (calls `CObjAI::SetCMD_MOVE2D` + the now-`protected` `Send_gsv_MOVE`), so player orders are tolerant of non-walkable clicks like the avatar. Attack orders use the AI's `SetCMD_RUNnATTACK` (chase + attack). Keep the strict IsMovablePOS gate only for internal AI pathing.

## Walkability, and why a missing .MOV killed every leash in Karkia

`CZoneFILE::LoadZONE` allocates the move-attribute grid and `FillAll()`s it — **1 =
blocked** — then clears bits from each map tile's `*.MOV`. `LoadMOV` returns silently
when the file does not exist. So a zone that ships **no `.MOV` at all** is blocked on
every cell, and `IsMovablePOS()` returns false everywhere in it.

That gates exactly one thing that matters: `CObjCHAR::SetCMD_MOVE2D` rejects every
destination. And `SetCMD_MOVE2D` is the move used by

- the **leash** — `AIACT_16` → `CObjMOB::Run_AWAY()` → "flee to within N m of my
  regen point",
- idle wandering (`AIACT_03`/`AIACT_04`), and
- fleeing on low HP.

**Chase does not go through it.** `CObjAI::ProcCMD_ATTACK` uses
`Goto_TARGET`/`Start_MOVE`, which has no walkability gate. The result in such a zone
is a monster that chases perfectly and can never give up — and since `Run_AWAY`
leaves the command as `CMD_ATTACK` when it fails, and nothing logs, it looks like
missing AI data rather than a missing asset. It is not missing AI data: 379 of our
509 `.aip` files declare a pattern-2 leash, and every Karkia monster has one
(typically "≥50-80 m from spawn → run back to within 1-5 m").

Since 2026-09-12 `LoadZONE` counts the tiles that actually loaded and, if **none** did,
opens the walkability grid and logs a warning. This is the server-side form of the engine's
"Missing Assets Must Degrade, Not Kill" rule. Things to know:

- **It opens only the cells under a map tile that actually loaded, not the whole grid.**
  The first version cleared all 2048×2048 cells, which also opened the void *outside* the
  map — and since `IsMovablePOS` gates nothing but `SetCMD_MOVE2D`, monsters promptly
  wandered off the terrain. A position off the map is what feeds garbage into the client's
  `CTERRAIN::GetPATCH`. `LoadMAP` records each tile in `m_bMapTileLOADED`; a tile is
  `PATCH_COUNT_PER_MAP_AXIS * 2` grid cells per axis (`LoadMOV`'s own stride), and
  `MAP_COUNT_PER_ZONE_AXIS` of those is exactly `MAP_MOVE_ATTR_GRID_CNT`.
- **Do not use `m_nMinMapX`/`m_nMaxMapX`/`m_nMinMapY`/`m_nMaxMapY`** (`zonefile.h`). They
  are declared and never assigned anywhere, so they hold uninitialised garbage. A per-tile
  flag is also correct for a non-rectangular zone, which a min/max box is not.

- It fires for all 9 Karkia zones and for **Lunar LZ02** — the only zones we ship with
  zero `.MOV`. Nothing else is affected; no zone has *partial* coverage, so the
  "none loaded" test can never half-apply to a map that meant to block something.
- Jrose never shipped Karkia's `.MOV` either, so there is nothing to import. Generating
  them from terrain is the only way to get real per-cell walkability there.
- The cost is that monsters in those zones can path into geometry a map author would
  have blocked. That is the accepted trade against an unbounded chase.
- `IsMovablePOS` has only two other callers and neither changes behaviour: a telemetry
  flag in `gs_socketlsv.cpp` and a GM readout in `cheatcmd.cpp`.
- Aggro radius and leash distance are **`.aip` data, not code** — metres in the file,
  ×100 at load (`cai_file.cpp`). There is no sight/aggro/chase column in `LIST_NPC.STB`
  and no server.toml knob. Karkia's idle-aggro radii are 13-22 m.
- Same gate, same trap as `CObjSUMMON::PlayerOrderMoveTo` above, which exists because
  player click-to-move must not be gated either.

## Dependencies

- `common-server` — IOCP sockets, SQL thread base
- `common` — shared game logic (calculations, items, quests)
- `common-lib` (Rust FFI) — logger, config parsing
- `lib_util` — C++ utilities
- Thirdparty: libpq (PostgreSQL), lua5, flatbuffers

## Conventions

- Same C++ conventions as client: `C` class prefix, `m_` members, `g_` globals
- Precompiled headers: `stdafx.h`
- Server-specific prefixes: `gs_` for gameserver modules, `srv_` for server-common
