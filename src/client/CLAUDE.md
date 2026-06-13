# CLAUDE.md — Rose Next Client

## Overview

DirectX 9 Win32 game client. Single-threaded game loop with packet-based server communication. All code is C++ built with VS2019 targeting x86.

## Key Subsystems

| Directory | Purpose |
|-----------|---------|
| `network/` | TCP socket management, packet send/recv (CNetwork singleton) |
| `interface/` | UI dialogs and HUD elements |
| `gameproc/` | Game state machine and processing |
| `gamedata/` | Runtime game data management |
| `ai_lib/` | Client-side AI behavior |
| `event/` | Event scripting |
| `sfx/` | Special effects |
| `sound/` | Audio playback |
| `terrain/` | Terrain rendering |
| `system/` | System-level (input, window) |
| `scripts/` | Lua scripting integration |
| `objectcommand/` | Character action/command processing |

## Important Classes

- **CObjCHAR** (`cobjchar.cpp/h`) — Base character object (player, NPC, monster). Manages server-authored damage presentation, HP reconciliation, pending death state, and animation states. Most combat display logic lives here.
- **CObjUSER** (`cobjuser.cpp/h`) — Player character (extends CObjCHAR)
- **CGame** (`game.cpp/h`) — Main game loop and state
- **CNetwork** (`network/cnetwork.h`) — Singleton, manages Login/World/Game server connections
- **CGameOBJ** (`cgameobj.cpp/h`) — Base game object

## Networking

- Separate socket connections per server phase: Login → World → Game
- Packet handlers: `Recv_gsv_*()` for incoming, `Send_cli_*()` for outgoing
- FlatBuffers used for some packets (stat updates, movement)
- Packet receive: `network/recvpacket.cpp`
- Packet send: `network/sendpacket.cpp`

## Combat Presentation System

Critical architecture: live combat damage is server-authored only. Client presentation is allowed to decide *when* to show a hit, but never *how much* damage combat did.

- **Damage source:** FlatBuffer `DamageEvent` contains `event_id`, `defender_seq`, canonical `attacker_id`, `defender_id`, `raw_damage`, `damage_value`, `hp_after`, `presentation_kind`, and `lethal`. `CombatSwing` wraps a `DamageEvent` plus motion data for confirmed normal attacks. Client-local `queued_at_ms` is stamped in `PushCombatDamageEvent()` only; it is not wire data and must not be serialized.
- **Queue ownership:** every `CObjCHAR` owns `m_CombatDamageQueue`. `recv_combat_swing()` queues the event on the defender before calling `StartConfirmedCombatSwing()`. Standalone `DamageEvent` packets are queued and either wait for a hit frame/projectile impact or present immediately depending on `presentation_kind`.
- **Presentation timing:** `Hitted()` consumes exactly one matching event for the current attacker. `NoEvent` means no HP change, no digits, no hit animation, no vibration, and no hit sound. Never resurrect timeout or missed-hit recovery behavior.
- **Projectile timing:** `ProjectileImpact` events stay queued until a bullet/skill projectile impact calls `Hitted()`. Generic immediate presentation must reject projectile events and push them back.
- **Ranged skill classification:** `IsProjectilePresentedSkillDamage()` decides whether `Recv_gsv_DAMAGE_OF_SKILL` queues a FlatBuffer event for projectile-impact presentation or pushes a payload onto `m_EffectedSkillList` for immediate display. `SKILL_ACTION_IMMEDIATE` with `SKILL_BULLET_NO > 0` (e.g. Twin Shot — a "melee immediate" type whose visual is actually a gun shot) is projectile-presented: the gun/bow fire animation has no action frame 25 hit moment, so `ProcEffectedSkill()` is never called and the bullet impact is the only timing signal. Without this, the skill fires, the bullet reaches the target, no damage digit / hit feedback appears, and the monster dies silently when the server applies the kill.
- **Target-bound continuing skills are NOT projectile-presented:** `SKILL_ACTION_TARGET_BOUND_DURATION` (9), `SKILL_ACTION_TARGET_BOUND` (11), `SKILL_ACTION_TARGET_STATE_DURATION` (13) — e.g. Fire Ring (defense ↓), single-target buffs/debuffs — must be excluded from `IsProjectilePresentedSkillDamage()` regardless of `BULLET_NO`. `BULLET_NO` on these rows points at an effect-graphic ID for the visual ring/aura; no tracked `CBulletDIRECTION` is spawned, so no impact callback ever runs. These skills are drained at the caster's action frame in `cobjchar_actionframe.cpp` case 25 (`SKILL_ACTION_TARGET_BOUND_DURATION` / `_BOUND` / `_STATE_DURATION` → `ProcEffectedSkill()` immediate, without `bProjectileImpact`). Marking them projectile-presented orphans the queued effect-of-skill payload forever: `ProcEffectedSkill()` at action frame skips entries with `bWaitForProjectileImpact=true`, no `AddEnduranceEntity` runs, the debuff visual never appears, and the user reads it as "the skill does nothing" — even though the server side already applied the status.
- **Server-client `presentation_kind` agreement:** server `CObjCHAR::IsProjectilePresentedSkill` and client `CObjCHAR::IsProjectilePresentedSkillDamage` both call the shared `Rose::Combat::is_projectile_presented_skill(skill_type, bullet_no)` helper. Keep wrappers thin: `05/06` are projectile-presented, `03/19` are projectile-presented only with a bullet id, and target-bound `09/11/13` are never projectile-presented even if `BULLET_NO` is set.
- **HP authority:** `UpdateStats.hp` and legacy `GSV_SET_HPnMP` call `Reconcile_HP()` and update `m_iAuthoritativeHP`. HP increases apply immediately. Lower HP is folded into a later real damage presentation; it must not silently jump the visible bar.
- **Legacy skill HP checkpoints:** `GSV_DAMAGE_OF_SKILL` carries server-authored `m_iHP_AFTER`, and `ConvertDamageOfSkillToDamage()` copies that into the queued `DamageEvent.hp_after`. Do not derive legacy skill `hp_after` from current visible HP; `UpdateStats.hp` can arrive while the skill payload is still waiting on the caster action frame, and visible-HP synthesis double-applies reconciliation drift.
- **Digits vs HP:** `DamageEvent.damage_value` is the floating digit and feedback damage. Folded HP correction/checkpoint drift uses a local HP delta inside `ApplyPresentedCombatDamage()` and must not mutate `event.damage_value` or inflate `event.raw_damage`. Apply the displayed damage first, then clamp/fold only the remaining difference to the authoritative checkpoint; never add a pending correction blindly if the displayed hit already reached or passed `hp_after`.
- **Death:** death is explicit if `event.lethal`, `event.hp_after <= DEAD_HP`, or pending authoritative death is folded into an incoming hit. For the local avatar, `Reconcile_HP(DEAD_HP)` marks `m_bPendingAuthoritativeDeath` instead of showing synthetic huge damage. While pending-dead, avatar/cart outgoing hit frames present MISS/no damage unless the target event is also lethal; mutual death kills the avatar immediately after the monster's normal lethal presentation.
- **Pending-death backstop (never strand the avatar):** the pending-death mechanism normally relies on a *future incoming hit* to present the death. Two paths can leave the avatar flagged pending-dead with no incoming hit ever arriving: (1) the avatar kills its **only/last** attacker mid-swing (handled instantly — see drain-on-death below), and (2) a **no-attacker server kill** — DoT, fall damage, or an attacker that despawned/went out of range — that arrives via `Reconcile_HP(DEAD_HP)`. Without a safety net the player is alive-client / dead-server and frozen (server rejects input, nothing presents). `CObjCHAR::Proc()` therefore runs a backstop: if the avatar is `m_bPendingAuthoritativeDeath` and still alive client-side for longer than `kPendingAuthoritativeDeathTimeoutMs` (1500 ms), it forces `PresentPendingAuthoritativeDeath(NULL, "pending death timeout")`. `m_dwPendingAuthoritativeDeathTime` is stamped **only on the rising edge** in `MarkPendingAuthoritativeDeath` — the server can re-assert `hp=0` every frame, and re-stamping each call would push the timeout out forever so it never fires. `NULL` attacker is safe: the avatar's AI index is 0 (`AI_Dead` early-returns) and the lethal branch of `ApplyPresentedCombatFeedback` guards every `pAtkOBJ` deref. The timeout is intentionally short insurance, not the primary path — Layer-1 mutual-death-on-kill handles the common case with no delay, so the backstop should rarely fire. It is **not** a revival of timeout/missed-hit damage recovery: it only presents a death the server already committed.
- **Spectator stale-death fallback:** remote/non-avatar defenders can also be left visually alive if this client receives a lethal `CombatSwing`/legacy melee death, queues it for the killer's hit frame, but the local representation of that killer never reaches the consumer frame (interrupted animation, command mismatch, already-past frame, missing attacker state). `PushCombatDamageEvent()` pre-marks non-avatar lethal defenders with `m_bDead = true`, and `CObjCHAR::Proc()` pops stale lethal `MeleeHitFrame` events after `kPendingAuthoritativeDeathTimeoutMs` (1500 ms) via `CombatPresentationQueue::pop_stale_lethal()`, then runs normal `ApplyPresentedCombatDamage()`/`Dead()` with the original final-hit digit. This is deliberately limited to `MeleeHitFrame`: `ProjectileImpact` deaths must wait for bullet/skill impact and are not generic-timed-out. If projectile creation/impact is explicitly discarded, `DiscardQueuedCombatDamageFromAttacker()` presents lethal remote defender deaths, but still silently discards nonlethal projectile damage and keeps avatar pending-death behavior unchanged.
- **Attacker dies mid-swing (drain-on-death):** the server applies a normal attack's HP at swing *start* (`Attack_START` → `Apply_DAMAGE`), while the client only presents it at the attacker's *hit frame*. If the player kills a monster before its swing reaches that frame, `Dead()` → `SetCMD_DIE()` interrupts the animation and the monster's already-applied `DamageEvent` orphans in the avatar's `m_CombatDamageQueue` forever. A lingering orphan keeps `has_pending_damage()` true, which gates off **both** reconciliation paths (`DeferCombatHPDriftIfIdle` and the checkpoint fold in `ApplyPresentedCombatDamage`), so the bars stay desynced (client HP > server HP) until the next unrelated hit/`UpdateStats`. `CObjCHAR::Dead()` therefore calls `g_pAVATAR->DrainQueuedCombatDamageFromAttacker(this)`: it discards every event the dying attacker queued on the avatar, lowers authoritative HP via `SetAuthoritativeHPFromDamageEvent` (which only ever lowers, so out-of-order drains are safe), and stages the lost HP as drift so the *next* presented hit silently folds it in (no phantom digit). A lethal orphan on the avatar is a **mutual death**: the attacker that just killed us is being interrupted into its own death motion this very frame and will never reach a hit frame, so no future incoming hit can fold our death in. The drain therefore both `MarkPendingAuthoritativeDeath` **and** `PresentPendingAuthoritativeDeath(pAtkOBJ, ...)` immediately — we already hold the authoritative lethal event and the killer object, so the avatar drops on the same frame as the kill (no freeze, no waiting on a server death packet). This is safe even though it runs inside the dying monster's `Dead()`: the avatar's own `Dead()` skips the drain re-entry via its `this != g_pAVATAR` guard. The `Proc()` backstop (above) still covers the case where this immediate present is somehow not reached. This is avatar-centric on purpose — each client reconciles its own bar; remote bars stay server-reconciled via `UpdateStats`. It is **not** a revival of timeout/missed-hit recovery: it only honors a *real* server event whose presentation animation was destroyed, mirroring the existing `DiscardQueuedCombatDamageFromAttacker` use for bullets that fail to spawn / projectile timeouts. `Dead()` is the single visual-death chokepoint (the `m_bDead = true` flags in `recvpacket.cpp` are pre-death markers; actual death is presented through `Dead()`), so one hook covers all kill paths.
- **No client damage math:** live client combat must not call `CCal::Get_DAMAGE()` or `CCal::Get_SkillDAMAGE()`. Those APIs are server-only for live combat; client usage belongs only in explicit offline/test paths.
- Key methods: `PushCombatDamageEvent()`, `Hitted()`, `ApplyPresentedCombatDamage()`, `PresentImmediateCombatDamage()`, `Reconcile_HP()`, `MarkPendingAuthoritativeDeath()`, `PresentPendingAuthoritativeDeath()`, `DrainQueuedCombatDamageFromAttacker()`, `DiscardQueuedCombatDamageFromAttacker()`

### AI Chase Movement (CMD_MOVE with target)

`Recv_gsv_ATTACK` routes monster attacks through `CObjCHAR::SetCombatAttackIntent` → `SetCMD_MOVE(target=avatar)`, **not** `SetCMD_ATTACK`. The monster walks toward the player in `CMD_MOVE` state and the actual swing is started later by `CombatSwing` / `StartConfirmedCombatSwing`. This means `CObjAI::ProcCMD_MOVE` runs for chasing monsters, not just for click-to-walk player input.

`CObjAI::ProcCMD_MOVE` has two distinct paths and the dispatch matters for combat sync:

- **USER mover** (`this->IsUSER()`): use `Goto_TARGET(pTarget, AVT_CLICK_EVENT_RANGE | NPC_CLICK_EVENT_RANGE)` — these click-event ranges (1000 / 250 in `datatype.h`) are UX values for "I clicked on someone to talk to them", not combat ranges. They are correct **only** for player-initiated walks.
- **Non-USER mover (monster, NPC, summon)** with a `pTarget`: use `Goto_TARGET(pTarget, this->Get_AttackRange())` so the chase re-reads `pTarget->m_PosCUR` each tick via `Restart_MOVE` and follows the live player position, halting at the monster's own attack range where the server starts its swing.
- **No target** (server-side move to a position): fall through to `Goto_POSITION()`.

Do not collapse these paths. The historical bug: every mover used `AVT_CLICK_EVENT_RANGE` (1000) and called `SetCMD_STOP()` when crossing that ring, freezing brawlers ~10 m from the player while the server kept advancing them and landing real hits — visible as "monster suddenly stops, then resumes" with client HP > server HP. Sister bug if the non-USER path is changed to `Goto_POSITION()`: the chase walks to a stale snapshot of the player position at packet-arrival time and only re-aims when the next `gsv_ATTACK`/`gsv_MOVE` arrives, producing the kiting-to-first-position visual artefact.

### Cart / Castle Gear Combat

When `GetPetMode() >= 0`, combat state is split between the rider avatar and the `CObjCART`:

- `CObjCHAR::SetCMD_ATTACK` must route mounted attacks to `m_pObjCART->SetCMD_ATTACK` before the rider-side `CanApplyCommand()` gate. If the rider queues the first post-mount attack on itself, the server can already be processing `CLI_ATTACK` while the client still looks idle.
- Server still attributes some legacy damage packets to the rider (user index). `Recv_gsv_DAMAGE` re-keys the damage event to the cart's client index when `pAtkOBJ->GetPetMode() >= 0 && pAtkOBJ->IsUSER()`. Damage must only be queued once under that canonical cart/castle-gear attacker index.
- Legacy damage timeout and missed-hit recovery are retired for live combat. If a hit frame has no matching server event, no presentation occurs.
- `ActionInFighting` case 21 and the rider-side `ActionBow` / `ActionGun` attack frames must skip `Hitted()` / projectile spawning when `GetPetMode() >= 0 && !IsPET()`. The rider's attack motion is visual only; the cart/castle gear is the source of truth for hit timing.
- The first mounted attack after `Drive Cart` can fail if the player attacks before moving even once. Root cause: cart runtime movement state was only being fully primed by mounted move flow. `CreateCart` and `SetCMD_PET_ATTACK` now copy the rider's move speed / move mode into the cart up front so the first attack can immediately path toward the target.
- `CObjCART::Get_fAttackSPEED()` falls back to the rider's `stats.attack_speed` (then a hard 100 default) because the cart's own `stats.attack_speed` is never synced from the server. A 0 attack speed produces motion speed 0 → NaN bone transforms → `zz_octree` min <= max assertion crash.

### Cart / Castle Gear Visual Loading

Cart visual creation (`CreateCart` / `CreateCartFromMyData` → `CObjCART::Create`) has several silent early-returns. Key gotchas:

- The union at `cobjchar.h` around line 1348 has **inverted** field names vs `RIDE_PART_*` indices: `m_sEngineIDX` is actually slot 0 (BODY) and `m_sBodyIDX` is slot 1 (ENGINE). Rest of the client follows the same inversion, so data round-trips — but it reads wrong.
- When `list_pat_skeleton.stb` has no skeleton entry for a cart body, `io_basic.cpp` synthesizes one from the body-item part data. Without this fallback, cart creation returns false while the server-side drive-mode speed is already applied (`recvpacket.cpp` unconditionally writes `move_speed` from the `TOGGLE_TYPE_DRIVE` packet before `RideCartToggle(true)` runs) — the player ends up moving at cart speed with no cart model.

### Zone Warp / Terrain Streaming

Zone changes (`CGameStateWarp` → `CTERRAIN::LoadZONE` / `FreeZONE`) and per-frame streaming in `SetCenterPosition` are sensitive to stale state. Containers below are **zone-local**; if they survive a teleport the client keeps loading/freeing old-zone terrain for many frames after warp. `ResetStreamingState()` (called from `FreeZONE` after `SubMAP(MOVE_UPDATE_ALL)`) clears all of them.

**Load / unload queues**
- `m_LoadOneMapData` — deferred per-map loads drained by `SetCenterPosition()`.
- `m_DirtyMapList` — recycle list of freed `CMAP*` entries.
- Both drains are **time-budgeted** (`kMinMapLoadIntervalMs` / `kMinDirtyFreeIntervalMs` = 150 ms) via `m_dwLastMapLoadTick` / `m_dwLastDirtyFreeTick`. Never replace with a frame-count gate — at 60 fps the old `% 5` drain produced visible post-stop hitches for several seconds.

**Map-level hysteresis (curve-back prevention)**
- Map unloads are deferred, not immediate: `DeferSubMAP` pushes doomed cells onto `m_PendingUnloadList`; `DrainPendingUnloads` commits them only when the player is > `kMapUnloadHysteresisRadius` (2) maps away OR `kMapUnloadGraceMs` (3 s) has elapsed.
- `AddMAP` fast-path: if the target cell is still pending unload, `CancelPendingUnload` removes the entry and **skips the disk-load enqueue entirely**. Do not re-enqueue loads for cells that are still live.
- `DoActualUnload` is the only place that calls `pMAP->Free()` + nulls `m_pMAPS` for deferred unloads. The old direct `SubMAP` path only runs on zone teardown.

**Patch keep-alive (camera-rotation prevention)**
- `CPatchManager::Delete_UnvisiblePatch` uses `kPatchEvictGraceFrames` = 30 frames (~0.5 s at 60 fps; `m_wLastViewFRAME` is `uint16_t`; subtract with wrap-safe arithmetic). Do not evict on `m_wLastViewFRAME != view_frame` — that re-introduces free-look stutter.
- `Update_VisiblePatchManager(nMappingX, nMappingY)` runs the proximity pass (`Update_VisiblePatch`) after frustum culling so nearby terrain stays in-scene regardless of camera angle. Free-look (right-click drag) rotates yaw + pitch and can reveal many quadrants at once — without proximity keep-alive each re-entering patch pays `InsertToScene` = walk `m_FixObjLIST` + `m_EffectLIST`.
- Ring radius is `kProximityRingRadius` = 12 (~452 patches via `g_pCRange->GetStartIndex(12)`). The earlier 22 covered 1513 points (~66% of the 48×48 grid), stamped every ring patch's `m_wLastViewFRAME` to the current frame every frame, and so defeated the eviction grace — `resPatch` pinned at 2304 and FPS collapsed in open zones. Any raise above 12 should be measured against `ring=` on the Scene HUD line and `resPatch`.
- First-time proximity inserts are capped at `kMaxProximityInsertsPerFrame` = 4 so teleport warm-up cannot itself cause a burst. Frustum-visible patches are never budget-gated (must render this frame or you get holes).
- `CPatchManager::s_nRingStampsThisFrame` counts ring hits per frame (both stamp and budgeted insert). Reset in `CTERRAIN::SetCenterPosition` next to `CMAP_PATCH::s_nInsertThisFrame`; surfaced on the HUD as `ring=` in the Scene: line.
- `CPatchManager::GetFrustumPatchCount()` exposes `m_nSubPATCH` (patches selected by the quadtree frustum pass after `CalculateViewFrustumCulling`). Surfaced on the HUD as `sub=` on the same Scene: line so `sub` vs `resPatch` distinguishes real frustum set from proximity keep-alive + eviction-grace overhang. If `sub ≈ resPatch` in an open zone, the frustum isn't rejecting and the location is content-bound (not a streaming bug).

**Patch index staleness (latent; exposed by the proximity pass)**
- `CPatchManager::m_ppPATCH[48×48]` holds patch pointers for the current 3×3 map set. It is populated only by `ReOrginazationPatch` and does **not** track per-frame map-slot rotation in `SetCenterPosition`. The frustum path reads only `m_ppQuadPatchManager[3][3]` (which `UpdatePatchManager` refreshes), so this was latent until proximity was wired in.
- After any map-slot change the stale entries point into `CMAP::m_PATCH` storage that `CMAP::Free()` has `ZeroMemory`'d — deref yields a zeroed `CMAP_PATCH` with a null vtable, and the first virtual call (`InsertToScene` → `RegisterToNZIN`) crashes.
- `m_bPatchIndexDirty` flag on `CTERRAIN` is set by `SetMapPTR` and `DoActualUnload`; `SetCenterPosition` calls `ReOrginazationPatch` lazily when dirty, just before `Update_VisiblePatchManager`. **Any future code that directly reads `m_ppPATCH` must run after this refresh.**

**Debugging**
- Logs around `LoadZONE` / `FreeZONE` report queued-load and dirty-map counts; they must return to 0 after zone teardown.
- If post-warp hitches come back, check queue sizes first. If free-look hitches come back, check whether the proximity pass is still running (`Update_VisiblePatch` called from `Update_VisiblePatchManager`) and whether `m_bPatchIndexDirty` is being refreshed.

## Bone-Attached Particle Budgeting

Character model bone effects created by `CCharMODEL::CreateBoneEFFECT` are passive cosmetic effects and are registered with `CBoneEffectBudget` (`BoneEffectBudget.cpp/h`) using owner, NPC id, bone index, and effect hash. Registration is intentionally narrow: do not add skill particles, hit effects, bullets, terrain/weather effects, weapon effects, or general `g_pEffectLIST` effects to `BoneFx`.

The budget manager runs once per main-frame update after scene update. It prioritizes player/current target, visible + in-frustum owners, nearest distance to avatar, then duplicate groups. The budget is cost-based (`configured particle capacity`, `configured emit rate`, emitter count, active particles) rather than raw effect-count based. Duplicate full-tier groups are capped per NPC/effect signature, so several copies of the same monster can still show an aura while later duplicates degrade gracefully. After additive particle texture batching proved stable in-game, the current relaxed budget is 480 runtime particles, 360 emit/sec, and 3 full duplicate groups per same NPC/effect signature.

Particle tiers are stored on `CEffect` and applied particle-only:
- `Full`: normal emit/update.
- `Reduced`: emit scale 0.35, runtime cap 40% with a minimum of 8 particles per emitter.
- `Minimal`: emit scale 0.08, runtime cap 12% clamped to 2-6 particles per emitter, update every 150 ms.
- `Off`: stop emitter and clear live particles.

`CEffect::StartEffect()` must preserve the stored particle tier, especially `Off`, so relink/start paths do not restart disabled particles. Mesh and sound parts inside the same effect stay unchanged. Engine-side runtime caps live in `zz_particle_emitter` / `zz_particle_event_sequence`; when caps are lowered, particles above the cap are deleted so old full-tier particles do not keep updating.

The debug HUD line starts with `BoneFx:` and reports groups, effects, emitters, active particles, runtime cap, configured emit rate, tier counts, and top NPC id. Use it to validate cases like Frozen Thorn (`NPC 1528`) without relying on the generic `Fx` line.

## Bone Particle Texture Batching

`CCharMODEL::CreateBoneEFFECT` also marks its `CEffect` particles with `SetParticleBatchRenderHint(true)`. This is the only client opt-in path for particle batching; gameplay particles and normal world effects must not be hinted unless they are separately audited.

The engine batches only hinted particle emitters that are additive-safe (`D3DBLENDOP_ADD` with destination blend `D3DBLEND_ONE`) and whose sequences can share the same texture/render-state key. Mixed or incompatible emitters fall back to the original render path. This was validated with Frozen Thorn (`NPC 1528`) aura particles, which use additive `_shine_03.dds` sequences and previously paid roughly one draw call per particle.

Batching lives in `zz_particle_emitter::RenderParticleListWithBatching` and sequence vertex append helpers in `zz_particle_event_sequence`. It writes world-space particle vertices into shared dynamic buffers, then draws one or more chunks per texture/render-state group. The debug HUD line starts with `PartBatch:` and reports batch groups, particles, draw calls, fallback count, and estimated saved draw calls.

## Particle Emit-Accumulator (death-then-spawn timing)

`zz_particle_event_sequence::update` accumulates fractional emit budget in `m_fNumNewPartsExcess` and spawns when it crosses 1.0. The accumulator is **only consumed when a spawn actually succeeds** — i.e., inside the `while (m_fNumNewPartsExcess >= 1.0f && free_elements > 0 && ...)` loop, the `m_fNumNewPartsExcess -= 1.0f` happens immediately after a successful `CreateNewParticle`. This matches the Rust port's pattern at `rose-offline-client/src/systems/particle_sequence_system.rs:336-394`.

Do not restore the older pattern that decremented the accumulator unconditionally when it crossed 1.0 regardless of whether `free_elements` allowed the spawn. That bug produced a visible synchronised flash on every cosmetic emitter capped at `num_particles=1` (smoke-on-buildings in town hubs), because the emit accumulator's "ready to spawn" moment drifted out of phase with the death frame — sometimes the spawn fired the same frame the slot freed (no gap), but more often the next spawn-trigger was 1-5 frames away, leaving the slot empty for ~16-83 ms per cycle. SMOKE_04 on JZ01 chimneys was the canonical repro.

For finite emitters (`m_Loops > 0`), `remaining_for_loops` enforces the `m_Loops * m_iNumParticles` cap and the accumulator is clamped to that remaining budget so it cannot overshoot after the loop completes.

`m_fNumNewPartsExcess` is allowed to grow beyond 1.0 while the slot is full, and that is correct — it preserves the "spawn debt" so the replacement particle fires on the death frame instead of waiting for the accumulator to refill from zero. Don't add a cap unless you have a measured runaway case; for an infinite emitter at `emit_rate * life` the steady-state ceiling is bounded.

## Input Dispatch

Windows messages arrive in `CGame::AddWndMsgQ` (called from `WndProc`). For each message the active state's `ProcWndMsgInstant` runs **synchronously on the Win32 thread**; if it returns non-zero the message is consumed, otherwise it is queued in `m_WndMsgQ` and drained next frame by `CGame::ProcInput` → `ProcMouseInput` / `ProcKeyboardInput`.

The queued path for mouse messages calls `g_itMGR.MsgProc` → `CITStateNormal::Process`, which iterates **every open dialog** (`m_Dlgs`) and **every icon** (`m_Icons`) in reverse, calling `pDlg->Process(uiMsg, wParam, lParam)` on each for hit-testing. Cost is O(n_dialogs × n_controls) per drained message.

**Right-click camera drag**: `CGameStateMain::ProcWndMsgInstant` handles `WM_MOUSEMOVE` with `MK_RBUTTON` by calling `Add_YAW` / `Add_PITCH` (cheap, dirty-flag only) and **returning 1 to consume the message**. Windows delivers mousemove at 100–500 Hz during active drag; without the early return, each one would queue and pay the full dialog hit-test walk on the next frame, producing micro-stutters. No UI reacts to mousemove while RMB is held for camera control, so the consumption is safe. `m_ptCurrMouse` is updated in `AddWndMsgQ` **before** `ProcWndMsgInstant`, so global cursor tracking isn't affected.

Left-click, hover, and non-drag cursor motion all still flow through the queued path (UI must see them). Only `WM_MOUSEMOVE + MK_RBUTTON` is consumed early.

## Frame Timing & Timer Precision

`g_GameDATA.GetElapsedFrameTime()` ([game.cpp:172,180](src/client/game.cpp#L172)) is built on `timeGetTime()`. By default Windows quantizes `timeGetTime()` to the system tick (~15.6 ms), which dominates anything driven by per-frame dt above 60 fps:
- 50 Hz (20 ms real frame): measured dt = 15.6 or 31.2 → ±25% variance around mean.
- 75 Hz (13.3 ms real frame): measured dt = 0 or 15.6 → ±100%+ variance, even when frames are perfectly smooth.

`WinMain` instantiates `HighResTimerScope` ([winmain.cpp:19-30](src/client/winmain.cpp#L19-L30)) which calls `timeBeginPeriod(1)` for the lifetime of the process and `timeEndPeriod(1)` on exit. Do not add a second `timeBeginPeriod(1)` somewhere else; the WinMain RAII covers everything. Do not remove it — the symptoms it cures (jittery RMB camera, jerky dialog-drag, "stutter that only goes away at 50 Hz") look like a frame-pacing or driver bug but are pure timer-quantization downstream of `GetElapsedFrameTime()`.

**Diagnostic**: if a stutter seems to span unrelated subsystems (camera + UI + animation) and gets worse as monitor refresh rate climbs above the 64 Hz timer rate, suspect timer precision before swap-chain queue depth or driver state. Confirm with `QueryPerformanceCounter` traces of actual frame durations alongside `timeGetTime()` deltas — they should agree.

## Fullscreen Swap Chain

`zz_renderer_d3d.cpp` fullscreen present parameters ([zz_renderer_d3d.cpp:629-642](src/engine/src/zz_renderer_d3d.cpp#L629-L642)) intentionally use:
- `BackBufferCount = 1` — minimum flip-queue depth, makes it harder for a missed-vsync frame to lock the pipeline into a half-rate beat.
- `SwapEffect = D3DSWAPEFFECT_DISCARD` (regardless of FSAA) — runtime-managed back buffers instead of `D3DSWAPEFFECT_FLIP`'s driver-managed flip queue, which on AMD could pin the present cadence after a single overrun.

These were tightened while diagnosing the sticky-stutter bug. Neither change alone fixed the perceived stutter — the timer-precision fix above did — but both narrow the surface for residual driver-side queue pathologies and are kept defensively. Do not raise `BackBufferCount` back to 2 or restore `D3DSWAPEFFECT_FLIP` without a measured reason.

## Build

Built as part of `rose-next.sln` (x86/Win32). Depends on:
- `engine` — 3D rendering
- `common` — shared game logic
- `tgamectrl` — UI controls
- `lib_util` — utilities
- `common-lib` (Rust) — FFI staticlib
- Thirdparty: DirectX 9, lua4, imgui, ogg/vorbis, flatbuffers, sqlite, zlib

## Conventions

- Classes prefixed with `C` (CObjCHAR, CNetwork)
- Members prefixed with `m_` (m_CombatDamageQueue, m_iAuthoritativeHP)
- Globals prefixed with `g_` (g_GameDATA, g_pNet)
- Precompiled headers: `stdafx.h`
- Resource files: `client.rc`, `res/`

## Client Launch

```
rosenext.exe --server <IP>
rosenext.exe --server 127.0.0.1 --username user --password pass --auto-connect-server 1 --auto-connect-channel 1 --auto-connect-character CharName
```

Working directory: `dev/game/` (set up via `just dev-setup`)
