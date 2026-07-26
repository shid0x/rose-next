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

### Combat Presentation Packet Rules

- `DamageEvent` fields: `event_id`, `defender_seq`, `attacker_id`, `defender_id`, `raw_damage`, `damage_value`, `hp_after`, `presentation_kind`, `lethal`, `skill_id`. `skill_id` (0 = normal attack/unknown) is **display metadata for the client damage meter only** — neither server combat logic nor client presentation may branch on it. It flows via `Give_DAMAGE(..., kind, nSkillIDX)` → `Send_combat_damage_event` → `build_damage_event_packet`; `Skill_START` SKILL_TYPE_06 passes `Get_ActiveSKILL()`, the shield counter passes `nShieldSKILL`, all other call sites default to 0. `CombatSwing`'s embedded event always carries 0 (normal attack).
- `presentation_kind` controls client timing: `MeleeHitFrame`, `ProjectileImpact`, `Immediate`, `StatusTick`, `MissingAttacker`.
- Normal attacks should send `CombatSwing` after damage is calculated. This guarantees the client queues the event before starting the swing animation that will consume it.
- Projectile-capable damage must use `ProjectileImpact` so the client waits for bullet collision. Immediate presentation is only for true immediate effects such as status ticks, shield counters, and missing-attacker fallback.
- Keep direct HP stat sync as reconciliation only. Combat HP decreases need a `DamageEvent` path so digits, HP, hit feedback, and death presentation stay in one transaction.
- **Lethal events double as summon-gauge sync.** The client decrements its `m_SummonedMobList` (summon gauge) when a lethal `CombatSwing`/`DamageEvent` arrives, so the `lethal` flag must be set on every kill. Because `send_packet_nearby` only covers 9 sectors around the broadcast center, `Send_combat_swing` / `Send_combat_damage_event` also call `mirror_lethal_packet_to_summon_owner` (anonymous namespace, `cobjchar.cpp`): when the dead defender is a summon (`GetCallerUsrIDX` + matching `GetCallerHASH`) and the owner is not `IsNEIGHBOR` of the broadcast center, the same packet is sent to the owner directly — mirroring what legacy `Send_gsv_DAMAGE2Sector` did. Non-combat despawns (owner death/zone change, bonfire distance) emit no combat event at all and still use the synthetic legacy `GSV_DAMAGE` in `CObjSUMMON::Proc`.
- Legacy `GSV_DAMAGE_OF_SKILL` remains in use for several skill types. It must include the defender's post-`Apply_DAMAGE` HP in `m_iHP_AFTER`; the client converts this into `DamageEvent.hp_after` and must not infer the checkpoint from visible HP.

### AI Script Guard

Monster AI script action `AIACT24` / `F_AIACT24` blocks hostile `btTarget == 0` condition-checked target skills unless that target is already the monster's current combat target. This prevents non-aggro scripted projectile attacks from sending early target-skill/damage packets. `btTarget == 1` current-target combat skills, `btTarget == 2` self skills, and allied/friendly skills remain allowed.

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
