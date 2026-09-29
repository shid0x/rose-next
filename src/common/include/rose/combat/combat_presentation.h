#pragma once

#include <cstdint>
#include <vector>

namespace Rose::Combat {

enum class DamagePresentationKind : uint8_t {
    MeleeHitFrame = 0,
    ProjectileImpact = 1,
    Immediate = 2,
    StatusTick = 3,
    MissingAttacker = 4,
};

enum class PresentationResult {
    NoEvent,
    PresentedMiss,
    PresentedDamage,
    PresentedDeath,
};

// Client presentation policy only. Membership follows recent confirmed attacks,
// not queued events: presenting a hit must not make its monster leave the crowd.
class CombatCrowdTracker {
public:
    static constexpr size_t kOpenAttackers = 8;
    static constexpr uint32_t kRecentAttackMs = 3000;
    static constexpr uint32_t kExitDelayMs = 1000;

    void observe(uint32_t attacker, uint32_t now) {
        for (auto& recent: m_attackers) {
            if (recent.id == attacker) {
                recent.at_ms = now;
                return;
            }
        }
        m_attackers.push_back({attacker, now});
    }

    // Called before a departing monster's client object index can be recycled.
    void forget(uint32_t attacker) {
        for (auto it = m_attackers.begin(); it != m_attackers.end(); ++it) {
            if (it->id == attacker) {
                m_attackers.erase(it);
                return;
            }
        }
    }

    template <typename IsStillPresent>
    void update(uint32_t now, IsStillPresent is_still_present) {
        for (auto it = m_attackers.begin(); it != m_attackers.end();) {
            if (uint32_t(now - it->at_ms) >= kRecentAttackMs || !is_still_present(it->id)) {
                it = m_attackers.erase(it);
            } else {
                ++it;
            }
        }

        if (m_attackers.size() >= kOpenAttackers) {
            m_active = true;
            m_exiting = false;
        } else if (m_active) {
            if (!m_exiting) {
                m_exiting = true;
                m_below_since = now;
            }
            if (uint32_t(now - m_below_since) >= kExitDelayMs) {
                m_active = false;
                m_exiting = false;
            }
        }
    }

    bool active() const { return m_active; }
    size_t attacker_count() const { return m_attackers.size(); }

    void clear() {
        m_attackers.clear();
        m_active = false;
        m_exiting = false;
        m_below_since = 0;
    }

private:
    struct RecentAttack {
        uint32_t id;
        uint32_t at_ms;
    };
    std::vector<RecentAttack> m_attackers;
    bool m_active = false;
    bool m_exiting = false;
    uint32_t m_below_since = 0;
};

// Six extra positive digits at most, with a separate work cap so a run of misses
// cannot consume the digit budget or cause an unbounded queue walk. The target is
// an estimate of unpresented melee damage, not an authoritative HP checkpoint.
class CombatCrowdDrainBudget {
public:
    static constexpr int kMaxDamageDigits = 6;
    static constexpr int kMaxEvents = 16;

    static int target_damage(int max_hp) {
        return max_hp >= 20 ? max_hp / 20 : 1;
    }

    bool can_drain(int64_t queued_damage, int max_hp) const {
        return m_events < kMaxEvents && m_damage_digits < kMaxDamageDigits
            && queued_damage > target_damage(max_hp);
    }

    void record(int damage) {
        ++m_events;
        if (damage > 0) {
            ++m_damage_digits;
        }
    }

    int events() const { return m_events; }
    int damage_digits() const { return m_damage_digits; }

private:
    int m_events = 0;
    int m_damage_digits = 0;
};

// Client-synthesized event ids live in the top half of the id space.
//
// The server's g_nextCombatEventId starts at 1 and hands out ids for every swing
// and damage event in the process; the client mints its own ids for legacy
// GSV_DAMAGE / GSV_DAMAGE_OF_SKILL packets, which carry none. Both feed the same
// per-defender queue, and event_id is the key for push() dedupe, discard_event(),
// has_event() and ClearPendingCombatSwingPresentation() -- so an id that exists on
// both sides is not a cosmetic clash. A colliding push is **silently dropped**
// (that hit shows no digit and loses its HP checkpoint), and a discard keyed on the
// id can take the wrong event out from under a live swing.
//
// The client counters used to start at 1 alongside the server's, overlapping for
// the whole low range -- observed live with server ids at 1..330 and legacy skill
// ids at 1..10 in the same five minutes. Offsetting by 1e6 was the earlier
// mitigation and only moved the problem: at a busy zone's event rate the server
// walks into those bands within days of uptime. Setting the high bit puts client
// ids somewhere the server cannot reach without issuing 2^31 events.
//
// Every client-side synthetic id must be built from this base. Keep the per-source
// offsets distinct so the four legacy paths stay separable in a log.
constexpr uint32_t kClientSyntheticEventIdBase = 0x80000000u;

struct DamageEvent {
    uint32_t event_id = 0;
    uint32_t defender_seq = 0;
    uint32_t attacker_id = 0;
    uint32_t defender_id = 0;
    uint32_t queued_at_ms = 0;
    // Client-local arrival-order stamp of the server packet that produced this
    // event's hp_after checkpoint. Captured at packet receive (not at queue time)
    // so a deferred skill hit keeps the order of its source packet. Compared
    // against CObjCHAR::m_dwLastAuthoritativeSyncSeq to detect a heal/sync that
    // superseded the checkpoint. Not wire data; must not be serialized.
    uint32_t arrival_seq = 0;
    // Client-local damage-source attribution for the damage meter. Exact skill
    // index when the source packet carried one (legacy GSV_DAMAGE_OF_SKILL),
    // best-effort caster active-skill guess for FlatBuffer skill projectiles,
    // 0 for normal attacks / unknown. Read only by CDamageMeter; never consulted
    // by combat presentation logic. Not wire data; must not be serialized.
    int32_t skill_id = 0;
    // Client object index of the character to credit when it differs from
    // attacker_id (DoT caster, summon owner) — converted from the wire's
    // server-index field at receive. 0 = same as attacker_id / unknown. Meter
    // display metadata only, same contract as skill_id.
    uint32_t source_attacker_id = 0;
    uint32_t raw_damage = 0;
    int32_t damage_value = 0;
    int32_t hp_after = 0;
    DamagePresentationKind presentation_kind = DamagePresentationKind::MeleeHitFrame;
    bool lethal = false;
};

class CombatPresentationQueue {
public:
    void push(const DamageEvent& event) {
        for (const auto& queued: m_events) {
            if (queued.event_id == event.event_id && event.event_id != 0) {
                return;
            }
        }
        m_events.push_back(event);
    }

    bool pop_for_attacker(uint32_t attacker_id, DamageEvent& out) {
        for (auto it = m_events.begin(); it != m_events.end(); ++it) {
            if (it->attacker_id == attacker_id) {
                out = *it;
                m_events.erase(it);
                return true;
            }
        }
        return false;
    }

    bool discard_for_attacker(uint32_t attacker_id, DamageEvent* out = nullptr) {
        DamageEvent discarded;
        if (!pop_for_attacker(attacker_id, discarded)) {
            return false;
        }
        if (out) {
            *out = discarded;
        }
        return true;
    }

    /// Same, restricted to one presentation kind -- used to release the projectile
    /// events of a cast that will never launch without touching that attacker's
    /// queued melee swings, which keep their own hit-frame consumers.
    bool discard_for_attacker_kind(uint32_t attacker_id,
        DamagePresentationKind kind,
        DamageEvent* out = nullptr) {
        for (auto it = m_events.begin(); it != m_events.end(); ++it) {
            if (it->attacker_id != attacker_id || it->presentation_kind != kind) {
                continue;
            }
            if (out) {
                *out = *it;
            }
            m_events.erase(it);
            return true;
        }
        return false;
    }

    bool discard_event(uint32_t event_id, DamageEvent* out = nullptr) {
        if (event_id == 0) {
            return false;
        }

        for (auto it = m_events.begin(); it != m_events.end(); ++it) {
            if (it->event_id != event_id) {
                continue;
            }

            if (out) {
                *out = *it;
            }
            m_events.erase(it);
            return true;
        }
        return false;
    }

    bool pop_immediate(DamageEvent& out) {
        for (auto it = m_events.begin(); it != m_events.end(); ++it) {
            if (it->presentation_kind != DamagePresentationKind::MeleeHitFrame
                && it->presentation_kind != DamagePresentationKind::ProjectileImpact) {
                out = *it;
                m_events.erase(it);
                return true;
            }
        }
        return false;
    }

    // A lethal MeleeHitFrame event older than grace_ms is popped for fallback
    // presentation -- unless swing_still_pending(event) reports that the killer's
    // confirmed swing animation is still in flight toward its hit frame (slow
    // attack motions, e.g. cart weapons, legitimately exceed the grace window).
    // Deferral is bounded by hard_cap_ms so a swing whose consumer never fires
    // still resolves.
    template <typename SwingStillPending>
    bool pop_stale_lethal(uint32_t now_ms,
        uint32_t grace_ms,
        uint32_t hard_cap_ms,
        int32_t dead_hp,
        SwingStillPending&& swing_still_pending,
        DamageEvent& out) {
        for (auto it = m_events.begin(); it != m_events.end(); ++it) {
            const bool bProjectile =
                it->presentation_kind == DamagePresentationKind::ProjectileImpact;
            if (it->presentation_kind != DamagePresentationKind::MeleeHitFrame
                && !bProjectile) {
                continue;
            }
            if (!it->lethal && it->hp_after > dead_hp) {
                continue;
            }
            const uint32_t age_ms = now_ms - it->queued_at_ms;
            if (bProjectile) {
                // A projectile death waits for its bullet, and only for its
                // bullet: no grace-time pop, no swing test. But a bullet that has
                // not landed by the hard cap is never coming -- the attacker was
                // interrupted before firing, or despawned (Del_Object drains only
                // events queued on the avatar), and no discard path can run
                // without the attacker object. Nothing else resolves a remote
                // defender's lethal ProjectileImpact event (the orphan sweep
                // excludes lethals on purpose), so without this cap the defender
                // stays visually alive forever on a corpse the server deleted
                // without a packet (CObjMOB::Make_gsv_SUB_OBJECT suppresses the
                // removal of a damage-killed mob).
                if (age_ms < hard_cap_ms) {
                    continue;
                }
            } else {
                if (age_ms < grace_ms) {
                    continue;
                }
                if (age_ms < hard_cap_ms && swing_still_pending(*it)) {
                    continue;
                }
            }

            out = *it;
            m_events.erase(it);
            return true;
        }
        return false;
    }

    bool pop_stale_lethal(uint32_t now_ms, uint32_t grace_ms, int32_t dead_hp, DamageEvent& out) {
        return pop_stale_lethal(
            now_ms, grace_ms, grace_ms, dead_hp, [](const DamageEvent&) { return false; }, out);
    }

    // A *non-lethal* deferred event whose presentation vehicle no longer exists.
    //
    // Deferred events are consumed by the attacker's hit frame / projectile impact,
    // so every cancellation path lives on the attacker side. When the attacker
    // object itself goes away -- it left the sector, the zone changed, or two swings
    // were in flight and only the newest one was tracked -- nothing is left to run
    // those paths and the event sits here forever. That is not a cosmetic leak:
    // has_pending_damage() gates both reconciliation paths, so one stranded entry
    // disables HP convergence for this defender permanently.
    //
    // still_live(event) reports that the attacker can still present it (it is alive
    // and swinging, or its projectile is in flight); hard_cap_ms bounds that
    // deferral so an attacker that swings forever at somebody else cannot pin an
    // event here indefinitely.
    //
    // Lethal events are deliberately excluded -- pop_stale_lethal and the
    // pending-authoritative-death backstop resolve those by presenting a death,
    // which is not what a silent HP fold should ever do.
    template <typename StillLive>
    bool pop_stale_orphan(uint32_t now_ms,
        uint32_t grace_ms,
        uint32_t hard_cap_ms,
        int32_t dead_hp,
        StillLive&& still_live,
        DamageEvent& out) {
        for (auto it = m_events.begin(); it != m_events.end(); ++it) {
            if (it->presentation_kind != DamagePresentationKind::MeleeHitFrame
                && it->presentation_kind != DamagePresentationKind::ProjectileImpact) {
                continue;
            }
            if (it->lethal || it->hp_after <= dead_hp) {
                continue;
            }
            const uint32_t age_ms = now_ms - it->queued_at_ms;
            if (age_ms < grace_ms) {
                continue;
            }
            if (age_ms < hard_cap_ms && still_live(*it)) {
                continue;
            }

            out = *it;
            m_events.erase(it);
            return true;
        }
        return false;
    }

    // Diagnostic backlog count, not crowd membership. Multiple swings can be
    // queued per attacker, and presenting them does not end that attacker's fight.
    size_t deferred_attacker_count() const {
        size_t count = 0;
        for (auto it = m_events.begin(); it != m_events.end(); ++it) {
            if (!is_deferred_presentation(*it)) {
                continue;
            }

            bool bAlreadyCounted = false;
            for (auto prev = m_events.begin(); prev != it; ++prev) {
                if (is_deferred_presentation(*prev)
                    && prev->attacker_id == it->attacker_id) {
                    bAlreadyCounted = true;
                    break;
                }
            }
            if (!bAlreadyCounted) {
                ++count;
            }
        }
        return count;
    }

    int64_t deferred_melee_damage(int32_t dead_hp) const {
        int64_t damage = 0;
        for (const auto& event: m_events) {
            if (event.presentation_kind == DamagePresentationKind::MeleeHitFrame
                && !event.lethal && event.hp_after > dead_hp && event.damage_value > 0) {
                damage += event.damage_value;
            }
        }
        return damage;
    }

    // Oldest event awaiting a melee hit frame, for the crowd drain (see
    // CObjCHAR::DrainCrowdedCombatDamage). Oldest-first is deliberate: the stalest
    // entries have waited longest. Some still precede their own visual impact;
    // crowd mode deliberately trades that precision for a more current HP bar.
    //
    // Melee only. A projectile's digit belongs to its visible impact and the bullet
    // is on screen to contradict an early number, and lethal events keep their own
    // death-presenting routes (pop_stale_lethal, the pending-death backstop,
    // drain-on-death) as the only ways a death reaches the screen.
    bool pop_oldest_deferred_melee(int32_t dead_hp, DamageEvent& out) {
        for (auto it = m_events.begin(); it != m_events.end(); ++it) {
            if (it->presentation_kind != DamagePresentationKind::MeleeHitFrame) {
                continue;
            }
            if (it->lethal || it->hp_after <= dead_hp) {
                continue;
            }

            out = *it;
            m_events.erase(it);
            return true;
        }
        return false;
    }

    // Is a committed death sitting in this queue, waiting for its animation frame?
    // The server applies the kill at swing start; presentation is deferred, so for
    // that whole window the character is dead server-side and alive client-side.
    // Read this rather than trusting a derived flag -- it is the same data the
    // m_bDead pre-mark is computed from, but it cannot go stale or be missed.
    bool has_lethal_pending(int32_t dead_hp) const {
        for (const auto& event: m_events) {
            if (event.lethal || event.hp_after <= dead_hp) {
                return true;
            }
        }
        return false;
    }

    // Is a committed death still waiting on a presentation that can actually
    // arrive? Deferred kinds only (MeleeHitFrame / ProjectileImpact -- the two
    // that wait for a hit frame or a bullet impact); still_live(event) is the
    // caller's liveness test for that presentation vehicle; hard_cap_ms bounds
    // the wait from queued_at_ms so a consumer that never fires still resolves.
    //
    // This is what lets the avatar's pending-death backstop wait for the killer's
    // real hit frame / projectile impact instead of presenting the death on a flat
    // timer: a ranged monster's shot lands ~2 s after the swing is received, and a
    // 1.5 s backstop killed the player before the arrow had even been fired.
    template <typename StillLive>
    bool has_live_lethal_pending(uint32_t now_ms,
        uint32_t hard_cap_ms,
        int32_t dead_hp,
        StillLive&& still_live) const {
        // Not is_deferred_presentation(): that helper deliberately answers false
        // for lethal events, and lethal events are the only ones asked about here.
        for (const auto& event: m_events) {
            if (event.presentation_kind != DamagePresentationKind::MeleeHitFrame
                && event.presentation_kind != DamagePresentationKind::ProjectileImpact) {
                continue;
            }
            if (!event.lethal && event.hp_after > dead_hp) {
                continue;
            }
            const uint32_t age_ms = now_ms - event.queued_at_ms;
            if (age_ms >= hard_cap_ms) {
                continue;
            }
            if (still_live(event)) {
                return true;
            }
        }
        return false;
    }

    // Is this exact event still queued? Used by the attacker side to tell "my
    // confirmed swing has not been presented yet" from "the pending-swing id is
    // stale because the hit frame already consumed it".
    bool has_event(uint32_t event_id) const {
        if (event_id == 0) {
            return false;
        }
        for (const auto& event: m_events) {
            if (event.event_id == event_id) {
                return true;
            }
        }
        return false;
    }

    // How many of this attacker's events wait on a consumer an attack motion
    // provides -- a hit frame or a projectile impact. The local avatar's own
    // attack loop pops them oldest-first (pop_for_attacker), so this minus its
    // bullets still in flight is the number of swings it owes on screen.
    size_t count_frame_presented_for_attacker(uint32_t attacker_id) const {
        size_t count = 0;
        for (const auto& event: m_events) {
            if (event.attacker_id == attacker_id
                && (event.presentation_kind == DamagePresentationKind::MeleeHitFrame
                    || event.presentation_kind == DamagePresentationKind::ProjectileImpact)) {
                ++count;
            }
        }
        return count;
    }

    bool has_pending_damage() const {
        for (const auto& event: m_events) {
            if (event.damage_value > 0 || event.lethal) {
                return true;
            }
        }
        return false;
    }

    void clear() { m_events.clear(); }
    size_t size() const { return m_events.size(); }

    static PresentationResult result_for(const DamageEvent& event) {
        if (event.lethal) {
            return PresentationResult::PresentedDeath;
        }
        if (event.damage_value > 0) {
            return PresentationResult::PresentedDamage;
        }
        return PresentationResult::PresentedMiss;
    }

private:
    // Waits on an animation frame (a hit frame or a projectile impact) rather than
    // presenting at receive, and is therefore part of the readable backlog.
    static bool is_deferred_presentation(const DamageEvent& event) {
        if (event.lethal) {
            return false;
        }
        return event.presentation_kind == DamagePresentationKind::MeleeHitFrame
            || event.presentation_kind == DamagePresentationKind::ProjectileImpact;
    }

    std::vector<DamageEvent> m_events;
};

} // namespace Rose::Combat
