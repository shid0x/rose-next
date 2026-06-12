# context.md — Rose Next Classic Deep Technical Reference

This document provides deep system-level context for AI-assisted development. It covers protocol internals, entity architecture, server tick loop, and complex system flows that are not obvious from reading individual source files.

---

## 1. Networking Protocol

### Packet Wire Format

Two packet formats coexist: **legacy structs** (majority) and **FlatBuffers** (modern additions).

#### Legacy Packets — Header (6 bytes, little-endian, `#pragma pack(1)`)

```
Offset | Size | Field
-------|------|------------------
0      | 2    | uint16 size       (total packet including header, min 6, max 4096)
2      | 2    | uint16 type       (command ID)
4      | 2    | uint16 reserved
6      | 4090 | uint8[] data      (payload)
```

Defined in `src/common/net_prototype.h` as `t_PACKET` — a 4096-byte union overlaying the header with 200+ named packet structs (e.g. `cli_LOGIN_REQ`, `gsv_DAMAGE`).

#### FlatBuffers Packets — Header (2 bytes)

```
Offset | Size | Field
-------|------|------------------
0      | 2    | uint16 size       (total including this 2-byte header)
2      | N    | FlatBuffer data   (PacketData root, union dispatch)
```

Used for: `LoginRequest`, `LoginReply`, `CharacterCreateRequest`, `CharacterMove`, `CharacterMoveAttack`, `UpdateStats`, `CombatSwing`, and `DamageEvent`. Schemas in `src/common-lib/packets/*.fbs`. Root type is `PacketData` with a `PacketType` union.

The `Rose::Network::Packet` C++ wrapper (`src/common/include/rose/network/packet.h`) handles FlatBuffer serialization into this format.

### Command ID Ranges

| Range | Direction | Prefix | Example |
|-------|-----------|--------|---------|
| 0x0700–0x07FF | Client → Server | `CLI_` | `CLI_ATTACK` (0x0798) |
| 0x0700–0x07FF | Server → Client | `GSV_`/`SRV_`/`WSV_`/`LSV_` | `GSV_DAMAGE` (0x0799) |
| 0x01F0–0x02FF | Server ↔ Server | `ZWS_`/`WLS_`/`OST_` | `ZWS_CONFIRM_ACCOUNT_REQ` (0x0211) |

Key packet IDs:
- **Login flow:** `CLI_LOGIN_REQ`/`LSV_LOGIN_REPLY` (0x0708), `CLI_SELECT_SERVER`/`LSV_SELECT_SERVER` (0x070a)
- **Zone entry:** `CLI_JOIN_ZONE`/`GSV_JOIN_ZONE` (0x0753)
- **Movement:** `GSV_MOVE` (0x0797)
- **Combat:** `CLI_ATTACK` (0x0798), `GSV_ATTACK` (0x0798), `GSV_DAMAGE` (0x0799)
- **Skills:** `CLI_SELF_SKILL` (0x07b2), `CLI_TARGET_SKILL` (0x07b3), `GSV_DAMAGE_OF_SKILL` (0x07b6)
- **Stats:** `GSV_SET_HPnMP` (0x07a0), `UpdateStats` (FlatBuffer — sent every server frame when HP changes)
- **Spawns:** `GSV_NPC_CHAR` (0x0791), `GSV_MOB_CHAR` (0x0792), `GSV_AVT_CHAR` (0x0793)

### Connection Flow

Client maintains two `CClientSOCKET` instances: `m_WorldSOCKET` (Login + World) and `m_ZoneSOCKET` (Game).

```
1. Client → LoginServer (m_WorldSOCKET)
   CLI_LOGIN_REQ (username + SHA256 password) → LSV_LOGIN_REPLY (server ID)
   CLI_CHANNEL_LIST_REQ → LSV_CHANNEL_LIST_REPLY (channels + user counts)
   CLI_SELECT_SERVER → LSV_SELECT_SERVER (game server IP:port)

2. Client → WorldServer (m_WorldSOCKET, new connection)
   CLI_JOIN_SERVER_REQ → SRV_JOIN_SERVER_REPLY
   CLI_CHAR_LIST → character list
   CLI_SELECT_CHAR → GSV_SELECT_CHAR (full character data)

3. Client → GameServer (m_ZoneSOCKET)
   CLI_JOIN_ZONE → GSV_JOIN_ZONE (player object index, world state)
```

### Server Socket Model (IOCP)

- Single `CreateIoCompletionPort` manages all sockets
- Worker threads block on `GetQueuedCompletionStatus()`
- Each socket (`iocpSOCKET`) has separate send/recv queues with critical sections
- `tagIO_DATA` wraps `OVERLAPPED` + `classPACKET` (~4120 bytes each), pooled via `CPoolSENDIO`
- Receive: `Recv_Complete()` reassembles partial packets, splits multi-packet buffers, calls virtual `HandlePACKET()`
- Send: `Send_Start()` queues to `m_SendList`, starts `WriteFile` if socket `m_bWritable`

---

## 2. Entity / Object System

### Server-Side Class Hierarchy

```
CGameOBJ                          Base: index, position, sector node
├── CObjITEM                      Dropped items (decay timer, party ownership)
└── CObjAI                        Abstract — AI foundation (stats, state machine)
    └── CObjCHAR                  Abstract — all characters (HP/MP, team, status effects)
        ├── CObjNPC               Standard NPCs
        ├── CObjMOB               Monsters (AI vars, target list, damage tracking for loot)
        └── CObjAVT               Player avatars (equipment, ride mode, summons)
            └── classUSER         Player session (socket + avatar + inventory + DB identity)
```

### Key Fields

| Class | Key Members |
|-------|-------------|
| `CGameOBJ` | `m_iIndex`, `m_PosCUR` (tPOINTF), `m_PosSECTOR` (grid coords), `m_pGroupSECTOR` |
| `CObjCHAR` | `m_iHP`, `m_iMP`, `m_iTeamNO` (100=MOB, 1=NPC, 2=USER), `m_wState`, `m_wCommand`, `m_IngSTATUS` (StatusEffects) |
| `CObjMOB` | `m_pCharMODEL` (stats template), `m_ulAICheckTIME[]`, `m_iAiVAR[]`, `m_TargetLIST`, `m_SavedDAMAGED` (loot allocation) |
| `classUSER` | `m_nZoneNO`, `m_dwDBID`, `m_HashACCOUNT`, `m_INV` (CInventory), `m_dwLastSkillSpellTIME[]` (per-skill cooldowns) |

### Object Type Enum

```cpp
OBJ_NULL=0, OBJ_ITEM=1, OBJ_CNST=2, OBJ_NPC=3, OBJ_MOB=4, OBJ_AVATAR=5, OBJ_SUMMON=6
```

### Client-Side Object Hierarchy

Client mirrors the server hierarchy but adds rendering, animation, and combat presentation state. Live combat damage is not calculated on the client.

```
CObjCHAR (client)
  m_CombatDamageQueue           — server-authored DamageEvent queue owned by defender
  m_iAuthoritativeHP            — latest server HP checkpoint from DamageEvent / stat sync
  m_iPendingCombatHPCorrection  — non-lethal lower-HP reconciliation folded into a later hit
  m_bPendingAuthoritativeDeath  — local-avatar server-dead state waiting for visual death
```

Retired client combat state: `m_DamageList`, `m_dwLastHPSyncTime`, missed-hit recovery, damage timeouts, and client-side `Apply_DAMAGE()` HP subtraction are not part of live combat anymore.

### Client-Side Bone Particle Budget

Passive character bone particle effects are a special client/engine path. Effects created only through `CCharMODEL::CreateBoneEFFECT` are registered in `CBoneEffectBudget` and managed independently of normal world effects. This was added for expensive looping cosmetic aura effects such as Frozen Thorn (`NPC 1528`), where several visible copies can otherwise run unbounded bone-attached emitters.

The budget is cost-based: configured particle capacity, configured emit rate, live active particles, emitter count, owner/NPC id, effect hash, and visibility/frustum/priority are tracked per bone-effect group. The manager assigns particle-only tiers (`Full`, `Reduced`, `Minimal`, `Off`) with hysteresis instead of binary distance hiding. Tiers forward through `CEffect` to engine runtime particle controls on `zz_particle_emitter` / `zz_particle_event_sequence`, which scale emit rate, cap update/render loops, skip minimal updates for 150 ms intervals, and delete particles above lowered caps. After particle batching validation, the active relaxed budget is 480 runtime particles, 360 emit/sec, and 3 full duplicate groups per same NPC/effect signature.

This system must remain gameplay-risk-minimal: no asset/STB/EFT/PTL edits, no mesh/sound tiering, and no registration of skill, hit, projectile, terrain, weather, weapon, or external gameplay effects. Debug HUD diagnostics are surfaced as the `BoneFx:` line.

### Client-Side Bone Particle Texture Batching

`CreateBoneEFFECT` also opts those passive cosmetic particles into additive texture batching with `CEffect::SetParticleBatchRenderHint(true)`. The hint is intentionally narrow: non-bone particles remain on the legacy render path unless separately audited.

`zz_particle_emitter::RenderParticleListWithBatching` batches only hinted emitters whose sequences are additive-compatible (`D3DBLENDOP_ADD`, destination blend `D3DBLEND_ONE`) and share texture/render-state keys. Eligible sequences append world-space vertices into shared dynamic buffers; incompatible/mixed emitters render normally. Diagnostics are surfaced as the `PartBatch:` HUD line with groups, particles, draw calls, fallback count, and estimated saved calls.

---

## 3. Sector System (Spatial Partitioning)

Zones are divided into a 2D grid of sectors. Sector size varies per zone (5000–12000 game units).

### Broadcasting

`send_packet_nearby()` iterates 9 adjacent sectors (3×3 grid centered on sender):

```
(-1,-1)  (0,-1)  (1,-1)
(-1, 0)  (0, 0)  (1, 0)
(-1, 1)  (0, 1)  (1, 1)
```

When objects cross sector boundaries, incremental update flags control which new sectors receive spawn/despawn packets:

```cpp
SECTOR_UPDATE_ALL=0x00  SECTOR_UPDATE_LEFT=0x01  SECTOR_UPDATE_RIGHT=0x02
SECTOR_UPDATE_UP=0x04   SECTOR_UPDATE_DOWN=0x08
// Diagonals: LU=0x05, RU=0x06, LD=0x09, RD=0x0a
```

Neighbor check: `abs(sector_x_diff) <= 1 && abs(sector_y_diff) <= 1`

Shout/yell uses `SHOUT_SECTOR = 3` (7×7 grid).

---

## 4. Server Tick Loop

### Frame Rate: 10 FPS (100ms per tick)

```cpp
#define _FRAME_PER_SECOND 10
#define _DELAY_PER_FRAME 100
```

### `CZoneTHREAD::Execute()` — Per-Zone Tick Order

```
1. Regeneration       — m_RegenLIST → CRegenPOINT::Proc() (mob/NPC respawn timers)
2. Delayed Spawns     — m_MobCALLED → RegenMOB() if time reached
3. Zone Entry Queue   — m_ObjWAIT → m_ObjLIST, AddObjectToSector(SECTOR_UPDATE_ALL)
4. Economy            — m_Economy.Proc() → broadcast rate changes
5. Packet Aggregation — m_ToSendPacketLIST → m_SendingPacketLIST
6. Frame Sync         — Sleep remaining time of 100ms budget
7. Time Update        — Cache m_dwTimeGetTIME, m_dwCurrentTIME, m_dwCurAbsSEC
8. Object Processing  — for each obj in m_ObjLIST: obj->Proc() (delete if returns false)
9. Trigger Execution  — m_TriggerLIST (quest/event callbacks)
10. Packet Cleanup    — Release sent packets from m_SendingPacketLIST
```

Step 8 is where all gameplay happens — `Proc()` is virtual: `CObjAVT::Proc()` handles player logic, `CObjMOB::Proc()` handles AI/pathfinding/combat.

### World Timer (10-second tick)

```
GS_TimerProc() every 10000ms:
  - Inc_WorldTIME()
  - Every 30s: Check_SocketALIVE()
  - Idle > 90s: CloseIdleSOCKET()
```

### SQL Thread

`GS_CThreadSQL` runs in a separate thread. Zone threads queue saves via `Add_BackUpUSER()` with critical section `m_csUserLIST`. The SQL thread picks up from `m_AddUserLIST` → `m_RunUserLIST` and executes `UpdateUserRECORD()` against PostgreSQL.

---

## 5. Combat Presentation Pipeline

Combat damage is server-authoritative. The server rolls and applies HP once, then sends explicit presentation data. The client never recalculates live combat damage.

### Combat Packets

`src/common-lib/packets/combat.fbs` defines:

```
DamageEvent {
  event_id, defender_seq, attacker_id, defender_id,
  raw_damage, damage_value, hp_after,
  presentation_kind, lethal
}

CombatSwing {
  attacker_id, defender_id, target_distance, target_pos, move_mode,
  damage: DamageEvent
}
```

`presentation_kind` values:
- `MeleeHitFrame` — consume on attack animation hit frame.
- `ProjectileImpact` — consume only when the projectile/bullet calls `Hitted()`.
- `Immediate` — present immediately for true instant effects.
- `StatusTick` — present immediately for HP-reducing status ticks.
- `MissingAttacker` — generic fallback/correction cue when no attacker animation owns the event.

### Server Side: Apply HP → Author Event

```
1. Attacker initiates CLI_ATTACK, skill packet, or CObjMOB AI decision.
2. Server calculates live damage with server-side calculation code.
3. CObjCHAR::Apply_DAMAGE() applies authoritative HP and returns final uniDAMAGE.
4. Server emits one presentation event for that HP change:
   - CombatSwing for normal confirmed attacks.
   - DamageEvent for skills, projectiles, counters, status ticks, death damage, and fallback.
5. UpdateStats.hp / GSV_SET_HPnMP remain reconciliation checkpoints, not combat presentation.
```

Normal attacks send `CombatSwing` only after damage is known, so the client queues the `DamageEvent` before starting the animation that will consume it. Projectile-capable attacks and skills must use `ProjectileImpact`; early `GSV_DAMAGE_OF_SKILL`/target-skill packet order must not cause HP/digits before projectile impact.

### Client Side: Queue → Hit Frame/Impact → Transaction

```
1. recv_combat_swing()        queues DamageEvent on defender, then starts confirmed swing.
2. recv_damage_event()        queues standalone DamageEvent; immediate kinds present now.
3. Hitted()                   consumes exactly one event for the current attacker.
4. ApplyPresentedCombatDamage()
   - updates authoritative checkpoint,
   - computes visible HP transaction,
   - plays hit/death feedback,
   - leaves floating digits at event.damage_value.
```

If `Hitted()` finds no matching event, it returns without HP change, digits, hit animation, vibration, or hit sound. This is intentional: missing server events must not be papered over by client math or timeout recovery.

### HP Reconciliation And Digits

`DamageEvent.damage_value` is the visible damage number. `DamageEvent.hp_after` and stat sync packets are authoritative checkpoints. When the server HP checkpoint is lower than the client would land after the displayed hit, the client folds that difference into visible HP only; it must not mutate `event.damage_value` or inflate floating digits.

Rules:
- HP increases from reconciliation apply immediately.
- Non-lethal HP decreases wait for a real damage presentation and fold into that hit.
- Lower reconciliation while damage is pending does not move visible HP early.
- Lethal `DamageEvent`s, `hp_after <= DEAD_HP`, or pending authoritative death drive the normal death AI/UI/motion flow.
- Local-avatar dead reconciliation marks `m_bPendingAuthoritativeDeath`; it does not create a synthetic huge damage digit.
- While pending-dead, the avatar/cart outgoing hit frames present MISS/no damage. If the target event is also lethal, the target dies with its normal digit and the avatar death presents immediately afterward.

### Cart / Castle Gear Attacks

Client-side cart combat splits state between the rider avatar and the `CObjCART`:

| Aspect | Location |
|---|---|
| Target / command queue | Cart — `CObjCHAR::SetCMD_ATTACK` forwards to `m_pObjCART->SetCMD_ATTACK` before rider-side command gating, rider returns early |
| Server-attributed attacker in damage packets | Rider (user's server index) |
| Motion that fires action-frame 21 | Cart (`CART_ANI_ATTACK01`); rider's `PETMODE_AVATAR_ANI_ATTACK01` is visual only |
| Stats for attack-speed scaling | Rider (cart `stats.attack_speed` is never synced; falls back to rider, then to 100) |

Because the attacker indirection differs between command side (cart) and packet side (rider), damage events are canonicalized once to the cart's client index when the attacker is a user with `GetPetMode() >= 0`. Damage must only be queued once under that mounted attacker index. There is no live combat damage timeout fallback. `ActionInFighting` case 21 and the rider-side ranged attack frames gate out rider `Hitted()` / projectile emission when `GetPetMode() >= 0 && !IsPET()` so hit timing comes solely from the cart motion.

The first mounted attack after `Drive Cart` has an extra pitfall: if the player attacks before moving once, the cart/castle gear can appear stuck or walk in place while the server still resolves the attack. The fix is to initialize the cart's runtime movement state immediately on mount creation and again before mounted attacks (`stats.move_speed`, `adjusted_move_speed`, run mode, drive move mode), instead of waiting for the first mounted move command to prime it.

See `src/client/CLAUDE.md` for the full cart-combat and cart-visual-loading invariants.

---

## 6. Status Effects

`StatusEffects` class on `CObjCHAR` stores active buff/debuff adjustments:

```
m_nAruaAtkSPD, m_nAruaRunSPD, m_nAruaCRITICAL, m_nAruaMaxHP, m_nAruaMaxMP,
m_nAruaRcvHP, m_nAruaRcvMP, m_nAruaRES, m_nAruaATK, m_dwSubStatusFLAG
```

Adjustment methods: `Adj_RUN_SPEED()`, `Adj_ATK_SPEED()`, `Adj_APOWER()`, `Adj_DPOWER()`, `Adj_RES()`, `Adj_HIT()`, `Adj_CRITICAL()`, `Adj_AVOID()`

These are applied during server-side damage calculation and affect the final `DamageEvent.damage_value` / `hp_after` authored by the server.

---

## 7. Data Loading

### Game Data Tables (STB)

STB files in `data/` define all static game data (items, NPCs, zones, skills, etc.). Loaded at server startup by `Load_BasicDATA()`. Client loads the same tables. Format is proprietary binary tabular data.

### FlatBuffers Code Generation

`src/common-lib/build.rs` compiles `.fbs` schemas using `flatc` binary from `bin/{profile}/thirdparty/`. Uses `PROFILE` env var (not `DEBUG`) to resolve the correct path. Generated code goes to `OUT_DIR`.

### Server Configuration

`server.toml` (TOML format) configures all three servers. See `doc/server.toml.example`. Key sections: `[database]` (connection_string), `[loginserver]`, `[worldserver]`, `[gameserver]` (each with ip, port, data_dir, log_level).

---

## 8. Key Constants

```cpp
MAX_PACKET_SIZE         = 4096
_FRAME_PER_SECOND       = 10       // Server tick rate
_DELAY_PER_FRAME        = 100      // ms per server frame
DEF_GAME_USER_POOL_SIZE = 8192
SOCKET_KEEP_ALIVE_TIME  = 300000   // 5 min idle timeout
WORLD_TIME_TICK         = 10000    // 10s world timer
TEAMNO_MOB = 100, TEAMNO_NPC = 1, TEAMNO_USER = 2
SHOUT_SECTOR = 3                   // 7×7 grid for chat
ITEM_OBJ_FREE_TIME                 // Drop ownership decay
```

---

## 9. Threading Model Summary

| Thread | Purpose | Key Data |
|--------|---------|----------|
| Main | Init, world timer, socket accept | `CLIB_GameSRV` singleton |
| IOCP Workers (N) | Socket I/O completion | `iocpSOCKET` send/recv queues |
| Zone Thread (per zone) | Game tick at 10 FPS | `m_ObjLIST`, `m_ppSECTOR[][]` |
| SQL Thread | Async DB writes | `m_AddUserLIST` → PostgreSQL |

Cross-thread communication uses critical sections (`CCriticalSection`) and doubly-linked list queues. Zone threads never directly access other zones' object lists.
