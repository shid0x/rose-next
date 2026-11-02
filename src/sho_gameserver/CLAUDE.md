# CLAUDE.md — Rose Next Game Server

## Overview

The game server (`sho_gameserver`) is the main server handling real-time gameplay: combat, movement, NPCs, AI, zones, parties, and chat. C++ built with VS2019 targeting x86. Uses IOCP for networking and PostgreSQL for persistence.

## Architecture

```
main.cpp → lib_gsmain (init) → Zone Threads (gs_threadzone)
                                    ↓
                              Per-zone game loop:
                              - Process player input packets
                              - Run AI (cobjnpc / common/cobjai / ai_lib)
                              - Compute combat (`common/calculation.cpp`, `cobjchar.cpp`)
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
| `src/gs_socketlsv.cpp/h` | Link to the world server (legacy "LSV" name; there is no login-server connection) |
| `src/network.cpp/h` | Packet broadcasting helpers |
| `src/cobjchar.cpp/h` | Server-side character object |
| `src/cobjavt.cpp/h` | Avatar (player) object |
| `src/cobjnpc.cpp/h` | NPC/monster object |
| `src/cobjevent.cpp/h` | Event objects |
| `src/cobjitem.cpp/h` | Dropped item objects |
| `src/status_effects.cpp/h` | Buff/debuff system |
| `src/srv_common/` | Chat rooms (`cchatroom`), `cobjvar.h` |
| `../common/calculation.cpp` | Combat math (server-only `#ifdef __SERVER`), compiled directly into this project. Skills can crit only for a Hawker/Raider holding a katar (`Is_KatarSkillCRITICAL`, x1.5, 2026-10-16), or when their row sets a critical burst (LIST_SKILL cols 49/50, `SKILL_CRITICAL_ROLL` / `SKILL_CRITICAL_RATE`, which replaces the katar rule for that skill; Shun'ei-zan, 2026-10-18). A skill that requires a shield (need weapon 261, matched against the off-hand) deals +2% per point of the shield's own Defense, item + refine grade, capped at +250% (`Is_ShieldSKILL` / `Get_ShieldDEF`; Shield Bash, 2026-10-18); `scripts/combatsim/formulas.py` mirrors every formula here -- change both |
| `src/ai_lib/` | Server-side AI behavior |
| `src/common/` | Server copies of `CObjAI`, regen areas, user data and the AI/motion/skill table readers (the client has its own) |

## Networking

- IOCP-based (inherited from `common-server/`)
- Packet handlers: `Recv_cli_*()` for client packets, `Send_gsv_*()` for server packets
- Broadcasting: `send_packet()` (single), `send_packet_party()` (party), `send_packet_nearby()` (sector-based, 9 adjacent sectors)
- FlatBuffers for some packet types (movement, stat updates, combat `DamageEvent` / `CombatSwing`, tuning preview)
- Inter-server: one connection, to the WorldServer (`[worldserver] ip` / `server_port`), via `gs_socketlsv`

## Zone System

- Each zone runs in its own thread (`gs_threadzone`)
- Zones divided into sectors for spatial queries
- `send_packet_nearby()` broadcasts to players in 9 adjacent sectors
- Zones listed in `LIST_ZONE.STB`; each zone's map data comes from its `.ZON` and per-tile `x_y.IFO` under `data_dir` (`zonefile.cpp`), and its walk grid `<FOLDER>.lnav` (`nav/nav_grid.cpp`)

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

### Revive Protection In PvP Zones (2026-11-02)

A player revived in a PvP zone carries `FLAG_ING_REVIVE` (LIST_STATUS row 57, 30 one-second
ticks, `Recv_cli_REVIVE_REQ`): `Apply_DAMAGE` turns every hit on them into
`SEND_DAMAGE_TO_NULL`, and dealing damage ends it. That second rule lived in `Give_DAMAGE`,
which normal swings (`Attack_START`) and the area / single-target skill loops never call, so a
revived player could swing at others for 30 s while nobody could hurt them. It is at the top
of `Apply_DAMAGE` now. `Attack_START` used to send no `CombatSwing` for a protected target,
and the client starts a swing only on one, so the attacker stood beside its target as if it
could not attack (tester report: castle gear vs a player revived in view). The swing goes out
as a miss now (damage 0). The client has no code for this status: no icon says the target is
protected.

### Combat Presentation Packet Rules

- `DamageEvent` fields: `event_id`, `defender_seq`, `attacker_id`, `defender_id`, `raw_damage`, `damage_value`, `hp_after`, `presentation_kind`, `lethal`, `skill_id`, `source_attacker_id`. The last two (0 = unknown) are **display metadata for the client damage meter only** — neither server combat logic nor client presentation may branch on them; presentation keys on `attacker_id`.
  - `skill_id` flows via `Give_DAMAGE(..., kind, nSkillIDX)` → `Send_combat_damage_event` → `build_damage_event_packet`; `Skill_START` SKILL_TYPE_06 passes `Get_ActiveSKILL()`, the shield counter passes `nShieldSKILL`, status ticks pass the applying skill, all other call sites default to 0. `CombatSwing`'s embedded event always carries 0 (normal attack).
  - `source_attacker_id` = who to *credit* when it differs from `attacker_id`: the DoT caster (status ticks put the victim in `attacker_id`; the poison tick passes `m_iTargetOBJ[ING_POISONED]` — `StatusEffects::m_iTargetOBJ` stores the **speller** for skill-applied statuses, `Skill_ApplyIngSTATUS` passes `pSpeller->Get_INDEX()`) or a summon's owner (`resolve_summon_owner_index`, anonymous namespace `cobjchar.cpp` — same `GetCallerUsrIDX` + `GetCallerHASH` validation as `mirror_lethal_packet_to_summon_owner`; auto-applied in `Send_combat_swing` and, when no explicit source is given, `Send_combat_damage_event`).
- `presentation_kind` controls client timing: `MeleeHitFrame`, `ProjectileImpact`, `Immediate`, `StatusTick`, `MissingAttacker`.
- **Both combat sends broadcast around the *defender*** (`Send_combat_swing` since 2026-09-23; `Send_combat_damage_event` always did). The clients that hold — and can present — a damage event are exactly the defender's 9 sectors; `recv_combat_swing` early-outs without the defender object, so an attacker-centered broadcast adds nothing and *misses* clients in the one-sector differential ring whenever attacker and defender straddle a sector boundary. That miss was permanent for a kill: a damage-killed mob's removal is never broadcast (`CObjMOB::Make_gsv_SUB_OBJECT` returns false at `HP <= 0` — the death packet is the only removal notification a client ever gets), so a client that missed the lethal swing kept a live-looking, unclickable monster forever ("some monsters he sees are actually dead server-side"). Keep any new combat/death send defender-centered, and keep `mirror_lethal_packet_to_summon_owner` beside it for owners outside that neighborhood.
- Normal attacks should send `CombatSwing` after damage is calculated. This guarantees the client queues the event before starting the swing animation that will consume it.
- Projectile-capable damage must use `ProjectileImpact` so the client waits for bullet collision. Immediate presentation is only for true immediate effects such as status ticks, shield counters, and missing-attacker fallback.
- **A normal attack is a projectile when the attacking weapon has a bullet** (`CObjCHAR::UsesProjectileAttackPresentation`): `WEAPON_BULLET_EFFECT` of the hand weapon, but for a riding player (`GetCur_MOVE_MODE() > MOVE_MODE_RUN`) `PAT_BULLET` of the mount's arms -- the client fires the arms' bullet, and speed/range/power already use the arms. Until 2026-10-13 it read the hand weapon while riding, so a shooting castle gear's shots went out `MeleeHitFrame` and a stun mid-flight lost the shot's digit.
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

### A Monster's Move Speed Must Reach The Client (2026-10-15)

A client learns a monster's move speed only from a move packet (`CharacterMove` /
`CharacterMoveAttack`), but the server starts and restarts moves silently: a chase
begins a tick *after* its packet went out, a monster re-closes on a target that
stepped out of range (`ProcCMD_ATTACK` -> `Start_MOVE`, no broadcast), a slow lands
or wears off. The repro: skill 988 slowed an Ikaness Worker (500 -> 260) in the same
tick it was ordered to chase, so its packet said 500 while the server ran it at 260;
for 14 s every client ran it 34 m ahead of the server's copy, and the move home that
ended the chase jumped it 30 m back (`position snapped forward`). Hooking only "slow
applied" and "slow expired" was tried first and missed it: at the apply the monster
was not moving yet.

The rule now: `build_char_move_packet` / `build_char_move_attack_packet` record the
speed they sent in `CObjCHAR::m_nBroadcastMoveSpeed`, and `CObjCHAR::Proc` calls
`Resync_MoveSPEED` every tick. A moving non-player whose `total_move_speed()` differs
from that gets `Start_MOVE` at the new speed and its move re-sent: `CharacterMoveAttack`
in `CMD_ATTACK`, `CharacterMove` in `CMD_MOVE`. Other commands are not re-sent, because
a move packet carrying a target (running away) would make the client chase that target.
Players are left out: their own client walks them and gets `GSV_SPEED_CHANGED`. Debug
line: `CombatTrace server move speed resync`. Client side, `CombatTrace move received`
logs each move packet for a monster in the local fight, with the speed it carries.

### A Junk NPC Row Must Degrade, Not Kill

`CObjMOB::Init` derives max HP as `NPC_LEVEL * NPC_HP` and sizes the per-attacker saved-damage table (`m_SavedDAMAGED`, EXP/drop share) as `NPC_HP / 8 + 4` into a **short**. Three LIST_NPC rows (996 Moss Golem, 997 Nepenthes, 998 Turak — unspawned duplicates, HP column 7.9-13 million where the largest real value is 10,701) overflowed both: a negative max HP and a negative table size that `new[]` read as ~4 billion, so `/mon 996` killed the process with nothing in the log (2026-09-15). `ClampedMobMaxHP` (64-bit product clamped to `INT_MAX`) covers `Init` and `Change_CHAR`; `ClampedSavedDamageCNT` (table capped at 4096) covers `Init`, the only place the table is allocated. The rows stay as they are — the balance passes leave them alone on purpose — and the Eldeon Moss Golem that actually spawns is **1591**. `/mon` and `/mon2` (`cheatcmd.cpp`, `MobRowRefusalReason`) now refuse such rows with a whisper saying why — no model, level 0, or an HP column over 20,000 — so a tester summoning by number cannot get an invisible or absurd monster. Survey 2026-09-15: of 1,426 named rows, 646 are placed on a map and 111 more are reachable through AI summons; of the 669 unreachable, 212 have junk stats, 183 share a name with a live row, 302 are coherent unused retail/event content. Ids are load-bearing (drop tables, AI, quests, dialogs), so rows are never deleted or renumbered.

### Skill Damage Presentation

`SKILL_TYPE_06` (projectile magic — Icebolt, Lightning, etc.) `Skill_START` must tag the `DamageEvent` with `ProjectileImpact`, not `Immediate`. `CObjCHAR::Give_DAMAGE` takes trailing `Packets::DamagePresentationKind kind = Immediate` and `short nSkillIDX = 0` parameters so the projectile-magic call site can pass `ProjectileImpact` + the active skill while shield-counter / status-tick / cheatcmd call sites keep the defaults (`nSkillIDX` is meter display metadata; see Combat Presentation Packet Rules). `CObjCHAR::IsProjectilePresentedSkill(short)` wraps the shared `Rose::Combat::is_projectile_presented_skill(skill_type, bullet_no)` helper, which the client also uses. The rule is: `05/06` projectile-presented, `03/19` projectile-presented only with a bullet id, and target-bound `09/11/13` never projectile-presented because their `BULLET_NO` is an effect graphic, not a tracked projectile.

### Area Skills: Damage At Action Start, Frontal Strips, Cones (2026-10-18)

**Cones and a monster's aim (2026-10-18).** A `SKILL_SCOPE` of 100000000 or more is a cone (`SKILL_SCOPE_CONE`: 100000000 + length x 10000 + full angle in degrees): what stands within its length and half-angle of the aim is hit, the point at the caster. A monster casts every skill on a target through `SetCMD_Skill2OBJ`, and `Goto_TARGET` resets `m_PosGOTO` to the caster once in range -- so a type-7 frontal or cone skill used to fall back to a circle. `CObjCHAR::Casting_START` now aims it: the target's position into `m_PosGOTO` (nothing writes it while the cast runs) and into `GSV_SKILL_START.m_PosAIM`, which only aimed casts carry (`m_nSize` = `sizeof(gsv_SKILL_START)`; every other cast sends `GSV_SKILL_START_SIZE`, the old packet). Players' type-7 skills use `SetCMD_Skill2POS` and keep their own aim.

The server applies a skill's damage **the moment its action motion starts** (`CObjAI::Do_SKILL`: `Set_MOTION(GetANI_Skill)` then `Skill_START`), not on a frame event, and the client shows an area hit at once when it arrives after the caster's cast (`Recv_gsv_DAMAGE_OF_SKILL`, "immediate presentation pop"). An area skill's digits therefore pop on its action's first frame: give it an action clip that fires early (Luna Stone's `extent_skill01` fires at frame 2), or split a long shot at its firing frame (`rosefmt.zmo.sliced`; Exceed Break). `Skill_DamageToAROUND` (types 7 and 17) also reads **frontal areas**: a `SKILL_SCOPE` >= 10000 is length x 10000 + width in cm, a rectangle from the caster toward `PosCENTER` (the aimed spot); it searches a circle around the rectangle's middle that just holds it (40 x 25 m needs 23.6 m, inside the 3x3 sectors `FindNextCHAR` walks) and keeps what falls inside. Aimed at its own feet (type 17) a frontal scope falls back to a circle of its length. The radius readers (`Skill_ChangeIngSTATUS`, `Skill_START_10_11`) go through `SKILL_SCOPE_RADIUS`, which is the scope itself below 10000.

**A monster's area miss is sent (2026-11-01).** `Skill_DamageToAROUND` sends a result only to a victim that took damage or a status -- except when the caster is a monster (not a player, not a player's summon): its clean misses go out as 0-damage results, so the player sees MISS when the area goes off. Before, a Cerberus breath that missed and did not burn showed nothing, and the next bite's MISS a second later read as the breath's, late. The client shows that digit without the skill's hit effect and sound (`CObjCHAR::IsAreaSkillMiss`: type 7/17, no damage, no status).

### Status Effect Application

`Skill_ApplyIngSTATUS` returns `btSuccessBITS` (a per-slot bitmask) that is set **before** `UpdateIngSTATUS` is called. `UpdateIngSTATUS`'s own return value is consumed only to decide whether the *target's* current command should be cleared (`Del_ActiveSKILL` + `SetCMD_STOP`) — that's used for stun/sleep, which return non-zero specifically for `FLAG_ING_FAINTING | FLAG_ING_SLEEP`. Do not confuse this with packet suppression: `Skill_ChangeIngSTATUS` sends `GSV_EFFECT_OF_SKILL` whenever `btSuccessBITS != 0`, regardless of `UpdateIngSTATUS`'s return. Continuing-target debuffs (e.g. Fire Ring's `ING_DEC_DPOWER`) follow this path and persist for their `SKILL_DURATION`; `Get_DEF()` reads `Adj_DPOWER() = m_nAdjVALUE[ING_INC_DPOWER] - m_nAdjVALUE[ING_DEC_DPOWER] + m_nAruaRES`, so the reduction takes effect on every subsequent damage formula evaluation. Debug traces under `SkillStatusTrace` (apply site) and `defender_def` / `defender_dec_dpower` in the `CombatTrace server combat swing` line let you verify the debuff is being read on each swing.

## Configuration

`server.toml` next to `sho_gameserver.exe`, or the path given by `--config <path>` (see `doc/server.toml.example`). Besides `[database]` and `[gameserver]` below, the game server reads `[worldserver] ip` / `server_port` (the world server it connects to) and the `[game]` base-stat table:
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

An attack order also sets the summon's AI variable `CObjSUMMON::SUMMON_ORDER_AI_VAR` (3) to 1 and runs its ATTACK-MOVE pattern once on the spot (`Do_AttackMoveAI`; a summon already in reach never chases, so the pattern would not run otherwise). It lets an AI file answer the order: the Terror Knight's `sur_guard.aip` taunts the clicked monster and clears the variable (`scripts/terror-knight-taunt.py`). No summon AI used variable 3 before; files that never read it are unaffected.

Move orders go through `CObjSUMMON::PlayerOrderMoveTo`, **not** `SetCMD_MOVE2D`. `CObjCHAR::SetCMD_MOVE2D` rejects destinations it cannot walk to (the walk grid; the `.MOV` cell before 2026-10-30) and silently keeps the old command — which made CTRL+click move orders "randomly" ignored depending on the exact terrain cell clicked. The avatar's own click-to-move has no such gate (it relies on client CANTMOVE collision feedback). `PlayerOrderMoveTo` replicates SetCMD_MOVE2D minus the walk check (calls `CObjAI::SetCMD_MOVE2D` + the now-`protected` `Send_gsv_MOVE`), so player orders are tolerant of non-walkable clicks like the avatar. Attack orders use the AI's `SetCMD_RUNnATTACK` (chase + attack). Keep the strict gate only for internal AI pathing.

## Walkability: the walk grid, not .MOV

The `*.MOV` walkability tiles are no longer read (2026-10-30). They bitmapped 5 m cells
(2048 x 2048 a zone, 512 KB each), were painted in only 18 zones, and their last use was a
fallback the walk grid (`.lnav`, the next section) had replaced. `IsMovablePOS`, `LoadMOV`
and the open-everything workaround for zones that shipped none are gone; the files sit in
`build/retired-mov/`.

What the old trap teaches, still true:

- `CObjCHAR::SetCMD_MOVE2D` is the move behind the **leash** (`AIACT_16` ->
  `CObjMOB::Run_AWAY()`), idle wandering (`AIACT_03`/`AIACT_04`) and fleeing. It refuses
  a destination the walk grid cannot reach in a straight line (`Nav_CanWalkTo`); with no
  grid (a zone not baked yet) or `/nav set walls 0`, it does not check at all.
- **Chase does not go through it.** `CObjAI::ProcCMD_ATTACK` uses
  `Goto_TARGET`/`Start_MOVE`. When a gate refused every destination (a zone with no .MOV
  used to), monsters chased perfectly and never gave up, and nothing logged it.
- Aggro radius and leash distance are **`.aip` data, not code** -- metres in the file,
  x100 at load (`cai_file.cpp`). There is no sight/aggro/chase column in `LIST_NPC.STB`
  and no server.toml knob. 347 of our 534 `.aip` files declare a leash.
- **Do not use `m_nMinMapX`/`m_nMaxMapX`/`m_nMinMapY`/`m_nMaxMapY`** (`zonefile.h`). They
  are declared and never assigned anywhere, so they hold uninitialised garbage.
- Player click-to-move and CTRL+click summon moves are not walk-checked
  (`CObjSUMMON::PlayerOrderMoveTo`, above).

## Pathfinding and Z (`src/nav/`, in progress)

This is the experimental project in `doc/pathfinding-and-z-plan.md`. Read its phase list
and its §6 decisions before touching movement, height or AoE shape. Phase 0 is in the tree.

- **`nav_agent`: the leg follower, `CObjCHAR::m_pNav`.**
  - Walks a character along waypoints, sending one **target-less** `CharacterMove` per
    leg (`build_char_move_leg_packet`). A move that carries a target makes the client
    home straight on it.
  - Clamps the server exactly onto each corner and carries the leftover time into the
    next leg. The plain mover overshoots by up to one step; the clamp applies to path
    legs only.
  - Caps a step at 500 ms.
  - Sends the next leg `lead_ms` early. 100 ms was validated in game, and 0 also looked
    fine: the client turns at the corner with no client change.
  - **It is dropped the moment anything else re-commands the character**: `Owns()`
    means `CMD_MOVE`, target 0, and `m_PosGOTO` = its current waypoint.
  - `Resync_MoveSPEED` and `Init_ADD_CHAR` report the leg the clients were last told.
- **`nav_stats`: `CZoneTHREAD::m_NavMeter`.** It times the object `Proc` loop, which runs
  *after* the frame wait, so its cost stretches the tick. It counts:
  - AI moves the grid refused, and refused leash points;
  - AoE casts during which a victim's AI ran its own scan (`GetFindGEN`). That used
    to take over the zone's single scan iterator and cut the AoE short. Since Phase 1,
    everything that acts on a scan collects its targets first
    (`CObjCHAR::AI_CollectOBJs`): the three skill loops, GM `/damage` and quest
    REWD_028. Do the same in any new loop that damages, buffs or warps what it finds.
    Pure AI scans, which do not call out, still walk the iterator directly.

  It logs `nav: zone …` once a minute while the zone has players, and `nav: server: …`
  every 5 minutes. Baseline on 2026-10-28: 14,151 monsters; worst p99 3.4 ms (Sikuku);
  646 of 2,047 MB of address space.
- **`nav_probe`: the GM `/nav rec|mark|trail [lead_ms] [walk|run] [chase]|stats` command.**
  A monster walks your recorded click corners. `/nav` (or `/nav here`) lists the floors under
  you and your target.
- **`nav_grid`: the layered walk grid (Phase 2).** `CZoneFILE::NavGrid()` holds
  `data/<zone folder>/<FOLDER>.lnav`, baked offline by `scripts/nav-bake.py` (format in
  `scripts/rosefmt/lnav.py`), or null.
  - Queries: `TerrainZ`, `SpansAt`, `VisualTopZ` (where the client draws a monster:
    `GetHeightTop`), `FollowZ` (the avatar's 135 cm step rule, cell by cell) and `NearestSpan`.
  - It loads at the end of `LoadZONE` and warns when map files changed since the bake.
    **Re-bake a zone after any map edit.**
  - About 120 MB for all 66 zones.
- **The vertical rule (Phase 3): `nav/nav_settings`.** `InVerticalReach`, defaulting to a
  band of |dz| <= 5 m + 70% of the horizontal distance.
  - Knobs come from `server.toml` `[nav]` (read at start and on `reloadconfig`) and the live
    `/nav set`.
  - Where it applies:
    - the AoE damage loop, against the area's centre: the caster for cones and strips; for a
      ground-target circle, the floor at the aim point nearest the caster;
    - hostile area statuses only;
    - monster scans, through `FindFirstCHAR(..., fBandZ)` from `AI_FindFirstOBJ(int)` of an
      `OBJ_MOB`.
  - Skill collects (`AI_CollectOBJs`) scan in 2D and apply the rule themselves.
  - Regen spawns re-roll spots the client would draw on a roof (`NavPickSpawn`, against the
    point's baked anchor).
  - **Stage A:** a monster's height is where the client draws it (`VisualTopZ`), so under a
    walk-on deck it counts as on the deck until Phase 6.
- **`CObjCHAR::m_nNavZ` is the server's own height for a player.**
  - It follows each move step (`Nav_TrackZ`). It is re-anchored to the nearest floor within
    3 m by the client's height on click-move and `CANTMOVE` (`Nav_HintZ`), which also audits
    the grid. It resets on zone join and on any jump over 20 m.
  - **`m_nPosZ` is not it:** that is the client's start-of-move report, broadcast to others as
    the remote-avatar Z hint. Never overwrite it with server Z.
  - For anyone but a player, `Nav_Z()` returns where the client draws them.
- **Walls for AI moves and spawns (Phase 4).** `Grid::LineWalk` walks a straight line cell by
  cell over reached walkable floors, stepping 135 cm on terrain and STAIRS floors and 45 cm
  where an object floor is involved (the bake's own link rule).
  - Every moving character keeps `m_nNavZ`. For a monster, `Nav_WalkZ()` is the floor it walks
    on, while `Nav_Z()` stays where it is drawn.
  - `CObjCHAR::SetCMD_MOVE2D` checks the whole line (`Nav_CanWalkTo`) on a gridded zone. The
    `.MOV` destination-cell check it replaced is gone with the files (2026-10-30).
  - **A move that must happen walks straight when blocked** (`Nav_MoveOrStraight`): the leash
    (`Run_AWAY`, after 6 points), summon follow, AIACT_29. Only optional moves (wander, flee)
    may give up, or a monster chases forever and a summon is stranded.
  - Player orders and chases are not checked until Phase 5 (A*).
  - Regen spawns also re-roll spots off the walkable ground or behind a wall from the point's
    centre (`NavPickSpawn`).
  - Switches: `[nav] ai_walls`, `spawn_reach`; `/nav set walls|reach`. Readouts: `/nav spawns`
    (the nearest regen square) and `/nav walk` (your target to you).
- **Pathfinding (Phase 5): `nav/nav_path` (A*), `nav/nav_agent` (the leg follower).**
  - **A chase on a path stays in `CMD_ATTACK` on its target.** Only `m_PosGOTO` walks the
    waypoints, and the clients get target-less legs. Ending the path for any reason while
    still chasing must re-send `CharacterMoveAttack`, or the clients leave the monster walking
    to its last leg (`Nav_ResumeChase`, `RealProc`'s drop).
  - The hook is `CObjAI::Nav_Chase`, called from `Goto_TARGET` and `ProcCMD_ATTACK`'s
    out-of-range branch. It acts only in `CMD_ATTACK`; skill chases still go straight.
  - Walks (leash, summon follow, CTRL+click) are target-less agents. The agent owns
    `CMD_MOVE` and `CMD_RUNAWAY`, and re-issuing a walk toward a goal within 4 m keeps it.
  - Search arrays are per thread (`thread_local`, about 2 MB each). Searches run only on
    zone threads, apart from the GM probe.
  - Switches: `[nav] pathing`, `path_zones`, `path_*` budgets; `/nav set path|zones|lead|cells`.
    Readout: `/nav path [go]`.
- **Vertical attack range (Phase 7, `[nav] vertical_range`, `/nav set range|giveup`).**
  - Attacks and targeted skills reach only within the band (`IsInATTACK_RANGE`, through
    `Goto_TARGET(..., true)`).
  - On another level, a player is refused with a whisper; a monster paths to the target's
    floor and gives up after 3 s stuck (`Nav_OtherLevel`). A ground skill aimed onto another
    level is refused in `Recv_cli_POSITION_SKILL` before it costs.
  - After `Goto_TARGET` returns false, callers must check the command is still theirs: a
    refusal or a give-up has already ended it.
- **Stage B (Phase 6, `[nav] send_z`, `/nav set z`): monsters walk their own floor.**
  - `Nav_Z()` of a non-player is `m_nNavZ` (FollowZ, up-facing floors only), and
    `Nav_SendZ()` sends it in `gsv_MOB_CHAR.m_nPosZ` (a new field: client and server ship
    together), in the `target_pos.z` of move packets (the mover's floor at the start), and in
    `gsv_STOP`. Players send 0 there as before.
  - **A spawn's floor must be set before the next zone pass announces it** (`m_ObjWAIT`).
    Pass the caller's floor: `RegenCharacter(..., fFloorZ)` / `RegenMOB` (queued calls carry
    it in `tagCalledMOB`), `Nav_PlaceNear` for a summon. Without one it falls back to the
    floor nearest where `GetHeightTop` would draw it, which is the deck above a summoner
    standing under a bridge.

## Dependencies

- `common-server` — IOCP sockets, SQL thread base
- `common` (common.lib) — STB reader, logging, network/string utils; the shared game logic (`calculation.cpp`, `shared/` items, quests, inventory, economy, AI file) is compiled directly into this project from `src/common/`
- `common-lib` (Rust FFI) — logger, config parsing
- `lib_util` — C++ utilities
- Thirdparty: libpq (PostgreSQL, via common-server), flatbuffers, fmt, json (headers)

## Conventions

- Same C++ conventions as client: `C` class prefix, `m_` members, `g_` globals
- Precompiled headers: `stdafx.h`
- Server-specific prefixes: `gs_` for gameserver modules, `srv_` for server-common

## Warp And Party Rules

- **`Recv_cli_RELAY_REPLY` now honours only the destination the server last
  sent** (`classUSER::m_bRelayPENDING`). Before, any client could warp anywhere
  through that packet. Every server-side recall already goes through
  `Send_gsv_RELAY_REQ`, which records it.
- A new party holds `party level / 5 + 4` members: the draw's party starts at level
  5 (`CParty::Raise_PartyLEV`) so it fits five.
