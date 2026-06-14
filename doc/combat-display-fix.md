# Combat Display Fix

## Overview

This document describes the fixes applied to the client-side combat display system to resolve three interrelated bugs: false MISS indicators, HP snapback, and missing damage numbers.

## Background

ROSE Online's combat system uses a **damage queue** architecture. The server computes damage and sends a `gsv_DAMAGE` packet, which the client stores in a per-character `m_DamageList` queue. The client's animation system fires `Hitted()` on specific keyframes, which pops entries from the queue and applies them visually (damage digits, HP subtraction, hit effects).

A separate **HP sync** mechanism exists via the `UpdateStats` flatbuffer packet, sent by the server every frame when a character's HP changes (e.g. from recovery ticks in `Check_PerFRAME()`). This creates a dual-path HP update system that is the root cause of the bugs.

## Bugs Fixed

### 1. False MISS

**Symptom:** The client displays "MISS" but the player actually takes damage. HP decreases despite the MISS indicator.

**Root causes:**
- **Multi-hit weapons:** Some attack animations have multiple hit frames, but the server sends a single `gsv_DAMAGE` packet. The first `Hitted()` call consumes the entire queue entry; subsequent hit frames find an empty queue and display MISS.
- **Packet-animation timing:** The `gsv_DAMAGE` packet can arrive after the animation hit frame fires, so the queue is empty at the moment of the hit check.
- **Pet/cart attacker ID mismatch:** When a pet attacks, the damage packet uses the pet's server object index, but `Hitted()` looks up the owner avatar's client object index. The attacker ID mismatch causes the queue lookup to fail.

**Fix:**
- Added a `bDamageFound` output parameter to `PopCurrentAttackerDamage()`. When the queue has no entry for the attacker, `Hitted()` now skips the damage digit display entirely instead of showing a "0" (MISS).
- Server-side: changed `Apply_DAMAGE()` to return `SEND_DAMAGE_TO_TARGET` instead of `SEND_DAMAGE_TO_NULL` for misses, so the server sends explicit miss packets rather than silently dropping them.
- Client-side: `Recv_gsv_DAMAGE()` now pushes damage entries under both the pet's and the owner's client object index when the avatar has an active pet, ensuring the queue lookup succeeds regardless of which ID `Hitted()` uses.

### 2. HP Snapback

**Symptom:** After taking damage, the player's HP bar momentarily drops, then snaps back up, then drops again.

**Root cause:** The `recv_update_stats` handler receives the server's authoritative HP (which already reflects the damage) and calls `Set_HP()` directly. Later, when the animation fires `Hitted()`, it pops the damage entry from the queue and calls `Apply_DAMAGE()` → `Sub_HP()`, subtracting the damage a second time. The HP drops below the correct value, and the next `UpdateStats` packet corrects it back up — creating the visual "snapback".

### 3. Missing Damage Numbers

**Symptom:** The player takes damage (HP visibly decreases) but no damage number or MISS indicator appears on screen.

**Root cause:** An earlier fix attempted to resolve HP snapback by calling `ClearAllDamage()` in `recv_update_stats` before `Set_HP()`. This cleared the damage queue before `Hitted()` could fire, so the animation hit frame found an empty queue and (with the `bDamageFound` fix) displayed nothing. The HP had already been set by the server packet, so damage was applied invisibly.

## Solution: Timestamp-Based Sync Detection

Instead of clearing the damage queue on HP sync, the fix records *when* the sync happened and uses that information to prevent double-subtraction while preserving visual feedback.

### Mechanism

1. **`m_dwLastHPSyncTime`** — A new `DWORD` field on `CObjCHAR` (protected, with public setter `SetLastHPSyncTime()`). Records the game time when `recv_update_stats` last set HP.

2. **`recv_update_stats`** — Instead of `ClearAllDamage()`, calls `SetLastHPSyncTime(g_GameDATA.GetGameTime())` before `Set_HP()`.

3. **`PopCurrentAttackerDamage()`** — Gains a `bool* pHPSynced` output parameter. While iterating the queue, if any matching entry has `m_dwCreateTime <= m_dwLastHPSyncTime`, sets `bHPSynced = true`. This means the server's HP value already accounts for this damage.

4. **`Hitted()`** — Both the normal attack branch and the `FIRE_BULLET` skill branch check `bHPSynced`:
   - If **synced**: skip `Apply_DAMAGE()` (HP already correct from server), but still call `CreateDamageDigit()` (visual feedback preserved).
   - If **not synced**: apply damage normally via `Apply_DAMAGE()` and show digit.

5. **`ProcDamageTimeOut()`** — When entries expire after 5 seconds, the same sync check is applied: skip `Apply_DAMAGE()` for synced entries, but always show the damage digit.

### Why This Works

The key insight is that damage queue entries created *before* the last HP sync are already accounted for in the server's HP value. By skipping `Sub_HP()` for those entries but still displaying the damage digit, we get:

- **No false MISS:** The `bDamageFound` flag ensures we only display damage when the queue actually had an entry.
- **No HP snapback:** Synced entries don't subtract HP again, so there's no double-subtraction to correct.
- **Damage numbers always shown:** The digit effect is created regardless of sync status, so the player always sees visual feedback for damage taken.

## Solution: Missed-Hit Recovery for Ghost Hits

### Problem

With slower monster attack animations, the `gsv_DAMAGE` packet can arrive *after* the animation's hit keyframe fires `Hitted()`. The queue is empty at lookup time, so with the `bDamageFound` fix, nothing is displayed — the player sees the monster connect but gets no MISS or damage number ("ghost hit"). The late packet then sits in the queue and gets consumed by the next attack, causing early/misattributed damage display.

### Mechanism

1. **`m_iLastMissedHitAttacker` / `m_dwLastMissedHitTime`** — Two new fields on `CObjCHAR` that record the attacker index and game time when `Hitted()` fires but finds no matching queue entry.

2. **`Hitted()`** — Both branches now record a missed hit in the `else` path when `bDamageFound` is false:
   ```cpp
   } else {
       m_iLastMissedHitAttacker = pFromOBJ->Get_INDEX();
       m_dwLastMissedHitTime = g_GameDATA.GetGameTime();
   }
   ```

3. **`PushDamageToList()`** — Before queuing a new entry, checks if there's a pending missed hit for the same attacker within a 2-second window. If matched:
   - Clears the missed-hit tracker
   - Calls `CreateImmediateDigitEffect()` to show the damage number or MISS immediately
   - Only calls `Apply_DAMAGE()` if the HP sync timestamp is older than the missed-hit timestamp (meaning the server hasn't already accounted for this damage via `UpdateStats`)
   - Returns early without queuing — prevents the entry from being consumed by the next attack's `Hitted()` call

### Why This Works

The missed-hit tracker acts as a "reservation" left by the animation. When the late packet arrives, it fulfills the reservation immediately instead of waiting for the next animation cycle. This eliminates ghost hits while preserving the existing sync-aware HP logic.

Multi-hit weapons are safe: secondary hit frames may record a missed hit, but no second damage packet exists to match, so the tracker simply expires or gets overwritten by the next real attack.

## Current Solution: Authoritative Skill HP Checkpoints

The modern combat presenter also supports legacy `GSV_DAMAGE_OF_SKILL` packets. Those packets now carry the server's post-damage HP in `m_iHP_AFTER`, and the client copies that value into `DamageEvent.hp_after` when converting the legacy packet.

This fixes an intermittent skill HP snapback where `UpdateStats.hp` could arrive while the skill payload was still waiting on the caster's action frame. The old client synthesized `hp_after` as `visible_hp - damage`, then later folded the pending reconciliation correction into the same skill hit. The bar could drop too far and snap back up on the next stat sync. The client now applies the displayed damage first and clamps/folds only the remaining difference to the authoritative checkpoint.

Projectile skill classification is shared through `Rose::Combat::is_projectile_presented_skill(skill_type, bullet_no)`: skill types `05/06` are projectile-presented, `03/19` are projectile-presented only when they have a bullet id, and target-bound `09/11/13` are never projectile-presented because their bullet column refers to an effect graphic rather than a tracked projectile.

## Current Solution: Heal-in-Flight Checkpoint Staleness

`DamageEvent.hp_after` is an *absolute* checkpoint — the authoritative HP at the instant the server applied the hit. But presentation is deferred to the animation/impact frame, and a heal / potion / regen `UpdateStats` (or `GSV_SET_HPnMP`) sync can arrive on the same TCP game socket during that window, raising HP above the checkpoint. Presenting the hit would then drop the bar to the stale (lower) checkpoint and snap it back up on the next sync. This is most visible when a pot heals to full while a monster's skill is mid-charge.

Two layers handle it:

1. **Arrival-order guard.** `DamageEvent.arrival_seq` is a client-local monotonic stamp captured at packet *receive* via `CObjCHAR::NextHPAuthoritySeq()` — in `PushEffectedSkillToList` for deferred skills so the source-packet order survives `ConvertDamageOfSkillToDamage`, and in `PushCombatDamageEvent` for immediate paths. `Reconcile_HP()` stamps `m_dwLastAuthoritativeSyncSeq` on every authoritative sync. In `ApplyPresentedCombatDamage`, the checkpoint is treated as stale-healed when `m_dwLastAuthoritativeSyncSeq > event.arrival_seq && m_iAuthoritativeHP > event.hp_after`. The presenter then honors the fresher authoritative HP instead of the stale checkpoint; the digit still shows the full `damage_value`. The guard never fires for normal hits (no later sync) or when the later sync is *lower* (more damage — handled by the existing checkpoint fold), so single-socket TCP arrival order is the only ordering assumption. `arrival_seq` is client-local and is not serialized.

2. **Visible floor with hits still in flight.** The overshoot-clamp that raises the bar to the fresher authoritative HP only runs when `m_CombatDamageQueue` is otherwise empty — the checkpoint fold is gated on `!has_pending_damage()` to avoid double-applying corrections across multiple in-flight hits. With another hit still queued (a multi-hit skill, or a melee swing queued alongside the skill) that block is skipped, so a stale-healed checkpoint would still dip the bar by the digit and snap back up on the next reconcile. After the guard proves the checkpoint is stale-healed, `ApplyPresentedCombatDamage` floors the visible bar at `min(visibleBefore, m_iAuthoritativeHP)` regardless of queue state. The floor is bounded by `visibleBefore` so a damage presentation never visibly heals, the digit is unchanged, and lethal hits are excluded (death already forces the bar empty).

This keeps the bar tracking the freshest authoritative HP through a heal-during-deferred-hit without inventing client HP math: it only ever *declines* to drop the bar below a value the server already confirmed.

## Current Solution: Spectator Stale Death Fallback

Two-player testing exposed a spectator-only death presentation failure. Player 2 could receive the server-authoritative lethal event for a monster killed by Player 1, but Player 2's local representation of Player 1 sometimes never consumed that queued event at the expected hit frame. The monster was dead server-side, dealt no real damage, and could not be attacked, but remained visually alive and kept animating on Player 2's client.

The fix keeps hit-frame timing as the primary path and adds a narrow fallback:

1. `DamageEvent` has a client-local `queued_at_ms` timestamp. `CObjCHAR::PushCombatDamageEvent()` stamps it when the event enters `m_CombatDamageQueue`; it is not serialized over the network.
2. Non-avatar lethal defenders are pre-marked with `m_bDead = true` when the lethal event is queued, matching legacy receive-path behavior.
3. `CombatPresentationQueue::pop_stale_lethal()` only pops lethal or dead-HP `MeleeHitFrame` events after the 1500 ms grace window. `CObjCHAR::Proc()` uses that helper for non-avatar defenders that are still visibly alive, then runs normal `ApplyPresentedCombatDamage()` so `Dead()` remains the single visual-death chokepoint and the original final-hit digit is preserved.
4. `ProjectileImpact` deaths are excluded from this generic fallback. They must wait for projectile impact. If projectile creation/impact is explicitly discarded, `DiscardQueuedCombatDamageFromAttacker()` now presents lethal remote defender deaths, while nonlethal projectile discards remain silent and avatar pending-death behavior is unchanged.

This avoids making all lethal packets immediate, which would desynchronize normal hit/projectile presentation, while preventing spectators from being stranded with a visually alive monster that the server has already killed.

## Files Modified

| File | Changes |
|------|---------|
| `src/common/include/rose/combat/combat_presentation.h` | Added client-local `DamageEvent::queued_at_ms`, `DamageEvent::arrival_seq`, and `CombatPresentationQueue::pop_stale_lethal()` for remote stale melee death recovery |
| `src/client/cobjchar.h` | Added `m_dwLastHPSyncTime` field, `SetLastHPSyncTime()` setter, `pHPSynced` param on `PopCurrentAttackerDamage()`, `m_iLastMissedHitAttacker` and `m_dwLastMissedHitTime` fields; heal-in-flight `arrival_seq` skill-payload field, `NextHPAuthoritySeq()`, `m_dwLastAuthoritativeSyncSeq`, and `ConvertDamageOfSkillToDamage(..., arrivalSeq)` |
| `src/client/cobjchar.cpp` | Constructor init, sync detection in `PopCurrentAttackerDamage()`, conditional `Apply_DAMAGE()` in `Hitted()` (both branches) with missed-hit recording, missed-hit recovery in `PushDamageToList()`, sync check in `ProcDamageTimeOut()`, queued lethal timestamping, remote stale melee death fallback, lethal remote projectile-discard death presentation, heal-in-flight arrival-order guard, and the stale-healed visible floor for hits still in flight |
| `src/client/network/cnetwork.cpp` | `recv_update_stats` uses `SetLastHPSyncTime()` instead of `ClearAllDamage()` |
| `src/client/network/recvpacket.cpp` | `Recv_gsv_DAMAGE()` dual-index push for pet/cart attacks |
| `src/sho_gameserver/src/cobjchar.cpp` | `Apply_DAMAGE()` returns `SEND_DAMAGE_TO_TARGET` for misses |
| `src/tests/combat_presenter/combat_presenter_tests.cpp` | Added stale lethal melee, projectile exclusion, lethal/nonlethal projectile discard, heal-in-flight checkpoint, and multi-hit-in-flight stale-healed floor regression coverage |

## Known Limitations

- **Minor timing mismatch:** Approximately 1 in 6 attacks may show the damage number slightly before the hit animation completes. This occurs when the `UpdateStats` packet arrives and the damage entry gets marked as synced before the animation's hit keyframe fires, causing `Hitted()` to display the digit on a slightly earlier frame. This is low-visibility in normal play and does not affect gameplay correctness.
- **Late-packet display position:** When the missed-hit recovery fires in `PushDamageToList()`, the damage digit is placed at the character's current position via `CreateImmediateDigitEffect()`, which may differ slightly from where the hit animation occurred. The difference is negligible in practice.
- **Spectator fallback delay:** A remote monster death whose hit-frame consumer is lost may appear up to ~1.5 s late on spectator clients. This delay is intentional so normal hit-frame and projectile-impact presentation still wins when it arrives correctly.
- **Sub-frame heal/checkpoint race (~0.1 s):** The arrival-order guard and stale-healed floor resolve the bar within the same `ApplyPresentedCombatDamage` call, but if the stale checkpoint presents one or two frames *before* the heal `UpdateStats` actually arrives, the bar can still flash down briefly until the sync lands and a later reconcile raises it. The window is bounded to a couple of frames and the digit is always correct. A purely cosmetic eased/"rolling" HP bar at the draw layer (`cnamebox.cpp` / avatar gauge, smoothing the drawn fill toward `Get_HP()` without touching authoritative HP, digits, or death) would absorb this residual without changing combat authority.
- **`RecoverHP()` / `ReviseHP` mechanism is dead code:** The legacy HP correction system (`SetReviseHP`, `RecoverHP`) is never called from the game loop. HP correction relies entirely on the `UpdateStats` packet path.
