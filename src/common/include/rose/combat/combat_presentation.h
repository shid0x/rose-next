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
            if (it->presentation_kind != DamagePresentationKind::MeleeHitFrame) {
                continue;
            }
            if (!it->lethal && it->hp_after > dead_hp) {
                continue;
            }
            const uint32_t age_ms = now_ms - it->queued_at_ms;
            if (age_ms < grace_ms) {
                continue;
            }
            if (age_ms < hard_cap_ms && swing_still_pending(*it)) {
                continue;
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
    std::vector<DamageEvent> m_events;
};

} // namespace Rose::Combat
