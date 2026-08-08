#include "rose/combat/combat_presentation.h"
#include "rose/combat/skill_presentation.h"
#include "rose/server_ai/non_aggro_skill_guard.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

using Rose::Combat::CombatPresentationQueue;
using Rose::Combat::DamageEvent;
using Rose::Combat::DamagePresentationKind;
using Rose::Combat::PresentationResult;
using Rose::ServerAI::should_block_non_aggro_script_skill;

namespace {
void
expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

// Mirrors CObjCHAR::NextHPAuthoritySeq: a monotonic arrival-order stamp shared by
// queued damage events (captured at receive) and authoritative syncs.
uint32_t g_test_hp_authority_seq = 0;
uint32_t
next_hp_authority_seq() {
    return ++g_test_hp_authority_seq;
}

DamageEvent
event(uint32_t id, uint32_t attacker, int damage, int hp_after, bool lethal = false) {
    DamageEvent e;
    e.event_id = id;
    e.defender_seq = id;
    e.attacker_id = attacker;
    e.defender_id = 100;
    e.raw_damage = static_cast<uint32_t>(damage);
    e.damage_value = damage;
    e.hp_after = hp_after;
    e.presentation_kind = DamagePresentationKind::MeleeHitFrame;
    e.lethal = lethal;
    return e;
}

// Models the object state that CObjCHAR::Proc()'s fnSwingStillPending inspects, so
// the cart / castle-gear tests can exercise *why* a swing counts as live instead of
// just toggling a bool.
//
// Mirrors src/client/cobjchar.cpp: the attacker must resolve, be alive, still be in
// CMD_ATTACK, and still hold this exact event as its pending confirmed swing. A
// mounted swing keys the queued event to the *cart* index while the pending swing is
// tracked on the rider, so a pet attacker additionally checks its parent -- and that
// parent check deliberately does not re-test the rider's alive/attacking state,
// matching the real predicate.
struct SwingWorld {
    struct Attacker {
        uint32_t id = 0;
        bool alive = true; // Get_HP() > DEAD_HP
        bool attacking = true; // Get_COMMAND() == CMD_ATTACK
        bool is_pet = false; // IsPET(): cart / castle gear
        uint32_t parent_id = 0; // CObjCART::GetParent(), 0 == none
        uint32_t pending_event = 0; // HasPendingCombatSwingEvent()
    };

    std::vector<Attacker> attackers;

    void add(const Attacker& a) { attackers.push_back(a); }

    const Attacker* find(uint32_t id) const {
        for (const auto& a: attackers) {
            if (a.id == id) {
                return &a;
            }
        }
        return nullptr;
    }

    Attacker* find_mut(uint32_t id) {
        for (auto& a: attackers) {
            if (a.id == id) {
                return &a;
            }
        }
        return nullptr;
    }

    bool swing_still_pending(const DamageEvent& event) const {
        const Attacker* atk = find(event.attacker_id);
        if (!atk || !atk->alive || !atk->attacking) {
            return false;
        }
        if (event.event_id != 0 && atk->pending_event == event.event_id) {
            return true;
        }
        if (atk->is_pet) {
            const Attacker* rider = find(atk->parent_id);
            if (rider && event.event_id != 0 && rider->pending_event == event.event_id) {
                return true;
            }
        }
        return false;
    }
};

struct HpHarness {
    int visible_hp = 100;
    int authoritative_hp = 100;
    bool has_authoritative_hp = true;
    uint32_t authoritative_seq = 0;
    uint32_t last_sync_seq = 0;
    int corrections = 0;
    int pending_correction = 0;
    int displayed_damage = -1;
    int displayed_digits = 0;
    bool pending_authoritative_death = false;
    int suppressed_outgoing_attacks = 0;
    CombatPresentationQueue queue;

    // Mirrors CObjCHAR::SetAuthoritativeHPFromDamageEvent. Lower-only unless the
    // caller vouches for the checkpoint being fresh (allow_raise), and a checkpoint
    // that actually raises HP also advances the supersession stamp so older queued
    // checkpoints are marked stale -- same contract as reconcile().
    void set_authoritative_from_event(const DamageEvent& e, bool allow_raise = false) {
        const bool raises = has_authoritative_hp && e.hp_after > authoritative_hp;
        if (raises && !allow_raise) {
            return;
        }

        authoritative_hp = e.hp_after;
        authoritative_seq = e.defender_seq;
        has_authoritative_hp = true;

        if (raises && e.arrival_seq > last_sync_seq) {
            last_sync_seq = e.arrival_seq;
        }
    }

    void defer_if_idle() {
        if (has_authoritative_hp && !queue.has_pending_damage()
            && visible_hp > authoritative_hp) {
            pending_correction = visible_hp - authoritative_hp;
            ++corrections;
        }
    }

    void reconcile(int hp) {
        last_sync_seq = next_hp_authority_seq();
        authoritative_hp = hp;
        has_authoritative_hp = true;
        if (hp >= visible_hp) {
            visible_hp = hp;
            pending_correction = 0;
            pending_authoritative_death = false;
        } else if (hp <= 0) {
            pending_correction = 0;
            pending_authoritative_death = true;
        } else {
            defer_if_idle();
        }
    }

    PresentationResult hit(uint32_t attacker) {
        return hit_internal(attacker, false, nullptr);
    }

    PresentationResult outgoing_hit_while_pending_dead(uint32_t attacker, HpHarness* pending_dead_attacker = nullptr) {
        return hit_internal(attacker, true, pending_dead_attacker);
    }

    // Mirrors CObjCHAR::DrainQueuedCombatDamageFromAttacker: an attacker that dies
    // before its hit frame leaves its already-applied damage orphaned in our queue.
    // Discard every such event, lower authoritative HP from it, and stage the lost
    // HP as drift so the next presented hit folds it in silently (no digit).
    void drain_from_attacker(uint32_t attacker, bool is_avatar = true) {
        DamageEvent e;
        int drained = 0;
        bool lethal_orphan = false;
        while (queue.discard_for_attacker(attacker, &e)) {
            ++drained;
            set_authoritative_from_event(e);
            if (e.lethal || e.hp_after <= 0) {
                lethal_orphan = true;
            }
        }
        if (drained == 0) {
            return;
        }
        if (lethal_orphan && is_avatar) {
            if (visible_hp > 0) {
                pending_authoritative_death = true;
                pending_correction = 0;
                // Layer 1: the attacker that killed us is dying this frame and
                // will never reach a hit frame, so present the mutual death now
                // instead of stranding the avatar pending-dead.
                present_pending_authoritative_death();
            }
        } else {
            defer_if_idle();
        }
    }

    // Mirrors CObjCHAR::PresentPendingAuthoritativeDeath: turn a flagged pending
    // authoritative death into an actual death (no digit, no damage). Used both by
    // the Layer 1 mutual-death-on-kill path and the Proc() backstop timeout.
    void present_pending_authoritative_death() {
        if (visible_hp <= 0) {
            pending_authoritative_death = false;
            pending_correction = 0;
            return;
        }
        if (!pending_authoritative_death && (!has_authoritative_hp || authoritative_hp > 0)) {
            return;
        }
        visible_hp = 0;
        pending_authoritative_death = false;
        pending_correction = 0;
    }

    PresentationResult present_stale_lethal(uint32_t now_ms, uint32_t grace_ms) {
        static const int kDeadHp = 0;
        DamageEvent e;
        if (!queue.pop_stale_lethal(now_ms, grace_ms, kDeadHp, e)) {
            return PresentationResult::NoEvent;
        }

        set_authoritative_from_event(e);
        displayed_damage = std::max(0, e.damage_value);
        ++displayed_digits;

        if (e.lethal || e.hp_after <= 0) {
            visible_hp = 0;
            pending_authoritative_death = false;
            pending_correction = 0;
            return PresentationResult::PresentedDeath;
        }

        return displayed_damage > 0
            ? PresentationResult::PresentedDamage
            : PresentationResult::PresentedMiss;
    }

    // Mirrors the CObjCHAR::Proc() call: the live-swing predicate defers the
    // stale pop until the hard cap.
    PresentationResult present_stale_lethal_with_live_swing(uint32_t now_ms,
        uint32_t grace_ms,
        uint32_t hard_cap_ms,
        bool swing_live) {
        static const int kDeadHp = 0;
        DamageEvent e;
        if (!queue.pop_stale_lethal(
                now_ms, grace_ms, hard_cap_ms, kDeadHp,
                [swing_live](const DamageEvent&) { return swing_live; }, e)) {
            return PresentationResult::NoEvent;
        }

        set_authoritative_from_event(e);
        displayed_damage = std::max(0, e.damage_value);
        ++displayed_digits;

        if (e.lethal || e.hp_after <= 0) {
            visible_hp = 0;
            pending_authoritative_death = false;
            pending_correction = 0;
            return PresentationResult::PresentedDeath;
        }

        return displayed_damage > 0
            ? PresentationResult::PresentedDamage
            : PresentationResult::PresentedMiss;
    }

    // Same as present_stale_lethal_with_live_swing, but takes the real predicate
    // shape (event -> bool) so a test can model the attacker state that decides it
    // rather than hard-coding the answer.
    template <typename SwingStillPending>
    PresentationResult present_stale_lethal_with_predicate(uint32_t now_ms,
        uint32_t grace_ms,
        uint32_t hard_cap_ms,
        SwingStillPending&& swing_still_pending) {
        static const int kDeadHp = 0;
        DamageEvent e;
        if (!queue.pop_stale_lethal(now_ms,
                grace_ms,
                hard_cap_ms,
                kDeadHp,
                swing_still_pending,
                e)) {
            return PresentationResult::NoEvent;
        }

        set_authoritative_from_event(e);
        displayed_damage = std::max(0, e.damage_value);
        ++displayed_digits;

        if (e.lethal || e.hp_after <= 0) {
            visible_hp = 0;
            pending_authoritative_death = false;
            pending_correction = 0;
            return PresentationResult::PresentedDeath;
        }

        return displayed_damage > 0
            ? PresentationResult::PresentedDamage
            : PresentationResult::PresentedMiss;
    }

    PresentationResult discard_from_attacker(uint32_t attacker, bool is_avatar) {
        DamageEvent e;
        if (!queue.discard_for_attacker(attacker, &e)) {
            return PresentationResult::NoEvent;
        }

        if (!is_avatar && visible_hp > 0 && (e.lethal || e.hp_after <= 0)) {
            set_authoritative_from_event(e);
            displayed_damage = std::max(0, e.damage_value);
            ++displayed_digits;
            visible_hp = 0;
            pending_authoritative_death = false;
            pending_correction = 0;
            return PresentationResult::PresentedDeath;
        }

        set_authoritative_from_event(e);
        defer_if_idle();
        return CombatPresentationQueue::result_for(e);
    }

    PresentationResult discard_event(uint32_t event_id, bool is_avatar) {
        DamageEvent e;
        if (!queue.discard_event(event_id, &e)) {
            return PresentationResult::NoEvent;
        }

        if (is_avatar && (e.lethal || e.hp_after <= 0)) {
            set_authoritative_from_event(e);
            pending_authoritative_death = true;
            present_pending_authoritative_death();
            return PresentationResult::PresentedDeath;
        }

        if (!is_avatar && visible_hp > 0 && (e.lethal || e.hp_after <= 0)) {
            set_authoritative_from_event(e);
            displayed_damage = std::max(0, e.damage_value);
            ++displayed_digits;
            visible_hp = 0;
            pending_authoritative_death = false;
            pending_correction = 0;
            return PresentationResult::PresentedDeath;
        }

        set_authoritative_from_event(e);
        defer_if_idle();
        return CombatPresentationQueue::result_for(e);
    }

    // Mirrors CNetwork::recv_damage_event's synchronous tail: a non-deferred kind
    // (StatusTick above all) is popped and presented at receive, never at a hit
    // frame.
    PresentationResult present_immediate() {
        DamageEvent e;
        if (!queue.pop_immediate(e)) {
            return PresentationResult::NoEvent;
        }
        return present_event(e, false, nullptr);
    }

private:
    PresentationResult hit_internal(uint32_t attacker, bool suppress_for_pending_dead_attacker, HpHarness* pending_dead_attacker) {
        DamageEvent e;
        if (!queue.pop_for_attacker(attacker, e)) {
            return PresentationResult::NoEvent;
        }
        return present_event(e, suppress_for_pending_dead_attacker, pending_dead_attacker);
    }

    PresentationResult present_event(DamageEvent& e, bool suppress_for_pending_dead_attacker, HpHarness* pending_dead_attacker) {
        // Mirrors ApplyPresentedCombatDamage: a later authoritative sync that raised
        // HP above this event's checkpoint marks the checkpoint stale.
        const bool stale_healed_checkpoint =
            has_authoritative_hp
            && e.arrival_seq != 0
            && last_sync_seq > e.arrival_seq
            && authoritative_hp > e.hp_after;

        // Only a StatusTick may raise authoritative HP: it is presented
        // synchronously at receive, so its checkpoint has no deferral window.
        const bool fresh_checkpoint = e.presentation_kind == DamagePresentationKind::StatusTick;

        if (!stale_healed_checkpoint) {
            set_authoritative_from_event(e, fresh_checkpoint);
        }

        if (suppress_for_pending_dead_attacker && pending_dead_attacker
            && pending_dead_attacker->pending_authoritative_death && !e.lethal && e.hp_after > 0) {
            displayed_damage = 0;
            ++displayed_digits;
            ++suppressed_outgoing_attacks;
            return PresentationResult::PresentedMiss;
        }

        const int display_damage = std::max(0, e.damage_value);
        displayed_damage = display_damage;
        ++displayed_digits;

        bool lethal = e.lethal || e.hp_after <= 0
            || ((pending_authoritative_death || authoritative_hp <= 0) && display_damage > 0);
        int hp_after_delta = visible_hp;
        int hp_delta = display_damage;

        if (lethal) {
            visible_hp = 0;
            pending_correction = 0;
            pending_authoritative_death = false;
            if (suppress_for_pending_dead_attacker && pending_dead_attacker) {
                pending_dead_attacker->visible_hp = 0;
                pending_dead_attacker->pending_authoritative_death = false;
            }
            return PresentationResult::PresentedDeath;
        }

        PresentationResult result = hp_delta > 0
            ? PresentationResult::PresentedDamage
            : PresentationResult::PresentedMiss;
        if (result == PresentationResult::PresentedDamage) {
            hp_after_delta = std::max(1, visible_hp - hp_delta);

            if (!queue.has_pending_damage()) {
                const int checkpoint_hp = stale_healed_checkpoint ? authoritative_hp : e.hp_after;
                const int target_hp = std::min(checkpoint_hp, authoritative_hp);
                if (target_hp <= 0) {
                    visible_hp = 0;
                    pending_correction = 0;
                    return PresentationResult::PresentedDeath;
                }
                if (hp_after_delta > target_hp) {
                    hp_after_delta = target_hp;
                    pending_correction = 0;
                } else if (hp_after_delta < target_hp) {
                    hp_after_delta = target_hp;
                    pending_correction = 0;
                } else {
                    pending_correction = 0;
                }
            }

            // Heal-in-flight visible floor independent of queue state (mirrors
            // ApplyPresentedCombatDamage): a stale-healed checkpoint must not dip the
            // bar below the fresher authoritative HP even while another hit is queued.
            if (stale_healed_checkpoint && has_authoritative_hp) {
                hp_after_delta = std::max(hp_after_delta, std::min(visible_hp, authoritative_hp));
            }

            visible_hp = hp_after_delta;
            defer_if_idle();
        }
        return result;
    }
};

struct ProjectileSkillHarness {
    struct Payload {
        uint32_t caster = 0;
        uint32_t target = 0;
        int skill = 0;
        bool damage_event_already_queued = false;
        bool wait_for_impact = true;
        bool status_applied = false;
    };

    int visible_hp = 100;
    int digits = 0;
    int hit_feedback = 0;
    int status_applied = 0;
    CombatPresentationQueue queue;
    std::vector<Payload> payloads;

    void receive_projectile_damage(uint32_t caster, uint32_t target, int skill, int damage) {
        DamageEvent e = event(static_cast<uint32_t>(queue.size() + 1), caster, damage, visible_hp - damage);
        e.defender_id = target;
        e.presentation_kind = DamagePresentationKind::ProjectileImpact;
        queue.push(e);

        Payload payload;
        payload.caster = caster;
        payload.target = target;
        payload.skill = skill;
        payload.damage_event_already_queued = true;
        payloads.push_back(payload);
    }

    void receive_target_skill(uint32_t, uint32_t, int) {
    }

    PresentationResult generic_immediate_processing() {
        DamageEvent out;
        if (!queue.pop_immediate(out)) {
            return PresentationResult::NoEvent;
        }
        visible_hp -= out.damage_value;
        ++digits;
        ++hit_feedback;
        return CombatPresentationQueue::result_for(out);
    }

    bool proc_effected_skill_without_impact() {
        for (const Payload& payload: payloads) {
            if (payload.wait_for_impact) {
                return false;
            }
        }
        return true;
    }

    PresentationResult impact(uint32_t caster, uint32_t target, int skill) {
        for (std::vector<Payload>::iterator it = payloads.begin(); it != payloads.end(); ++it) {
            if (it->caster == caster && it->target == target && it->skill == skill
                && it->wait_for_impact) {
                status_applied += it->damage_event_already_queued ? 1 : 0;
                payloads.erase(it);
                break;
            }
        }

        DamageEvent out;
        if (!queue.pop_for_attacker(caster, out)) {
            return PresentationResult::NoEvent;
        }
        visible_hp -= out.damage_value;
        ++digits;
        ++hit_feedback;
        return CombatPresentationQueue::result_for(out);
    }

    void timeout_discard(uint32_t caster) {
        DamageEvent discarded;
        queue.discard_for_attacker(caster, &discarded);
        payloads.clear();
    }
};
}

int
main() {
    {
        HpHarness h;
        expect(h.hit(10) == PresentationResult::NoEvent,
            "hit frame without a server damage event must not present damage");
        expect(h.visible_hp == 100, "missing damage event must not change visible HP");
        expect(h.pending_correction == 0, "missing damage event must not create correction");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 25, 75));
        expect(h.visible_hp == 100, "damage event must not move HP before hit frame");
        expect(h.hit(10) == PresentationResult::PresentedDamage, "hit should present queued damage");
        expect(h.visible_hp == 75, "HP must move exactly when hit frame consumes event");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 25, 75));
        h.reconcile(75);
        expect(h.visible_hp == 100, "lower stat sync must not move visible HP before damage event");
        h.hit(10);
        expect(h.visible_hp == 75, "queued damage applies reconciled HP at hit frame");
        expect(h.corrections == 0, "matching queued damage should not need correction");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 10, 90));
        h.queue.push(event(2, 20, 15, 75));
        expect(h.hit(20) == PresentationResult::PresentedDamage, "attacker 20 should consume own event");
        expect(h.visible_hp == 85, "attacker 20 applies only its own delta while attacker 10 is pending");
        expect(h.hit(10) == PresentationResult::PresentedDamage, "attacker 10 event should remain queued");
        expect(h.visible_hp == 75, "late older event must not raise visible HP");
        expect(h.corrections == 0, "out-of-order queued deltas should converge without correction");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 10, 90));
        h.queue.push(event(2, 10, 12, 78));
        expect(h.hit(10) == PresentationResult::PresentedDamage, "first same-attacker swing presents");
        expect(h.visible_hp == 90, "first same-attacker event applies first");
        expect(h.hit(10) == PresentationResult::PresentedDamage, "second same-attacker swing remains queued");
        expect(h.visible_hp == 78, "second same-attacker event applies on second hit");
    }

    {
        HpHarness h;
        h.visible_hp = 1000;
        h.authoritative_hp = 1000;
        h.queue.push(event(1, 10, 250, 800));
        expect(h.hit(10) == PresentationResult::PresentedDamage, "damage delta should present");
        expect(h.visible_hp == 800, "non-lethal presentation must not remain below authoritative HP");
        expect(h.corrections == 0, "higher hp_after should clamp without creating correction");
    }

    {
        // Heal-in-flight: a deferred skill hit (checkpoint 600) is superseded by a
        // heal sync (900) that arrives while the hit waits on its animation frame.
        // The fresher authoritative HP must win so the bar does not dip to 600 and
        // snap back to 900; the digit still shows the full 400.
        HpHarness h;
        h.visible_hp = 1000;
        h.authoritative_hp = 1000;

        DamageEvent skill = event(1, 10, 400, 600);
        skill.arrival_seq = next_hp_authority_seq(); // captured at receive
        h.queue.push(skill);

        h.reconcile(900); // heal lands after the hit was received, before its frame
        expect(h.visible_hp == 1000, "heal below gated visible HP must not move the bar yet");
        expect(h.last_sync_seq > skill.arrival_seq, "heal sync must out-rank the deferred hit");

        expect(h.hit(10) == PresentationResult::PresentedDamage, "deferred skill hit presents");
        expect(h.visible_hp == 900, "stale checkpoint must yield to the fresher healed HP (no snapback)");
        expect(h.displayed_damage == 400, "healed checkpoint must not alter the skill damage digit");
    }

    {
        // Heal-in-flight with another hit still queued. The overshoot-clamp only runs
        // once the queue drains, so before the visible-floor fix the bar dipped by this
        // digit and snapped back up on the next reconcile. The stale-healed floor must
        // hold the bar at the fresher healed HP even while a second hit is pending.
        // Repro: pot back to full while a monster's multi-hit skill is mid-charge.
        HpHarness h;
        h.visible_hp = 1000;
        h.authoritative_hp = 1000;

        DamageEvent skill = event(1, 10, 400, 600);
        skill.arrival_seq = next_hp_authority_seq();
        h.queue.push(skill);

        // Second in-flight hit keeps has_pending_damage() true when the skill presents.
        DamageEvent second = event(2, 10, 150, 450);
        second.arrival_seq = next_hp_authority_seq();
        h.queue.push(second);

        h.reconcile(1000); // pot back to full after both hits were received
        expect(h.last_sync_seq > skill.arrival_seq, "heal must out-rank the deferred skill hit");

        expect(h.hit(10) == PresentationResult::PresentedDamage, "first deferred hit presents");
        expect(h.visible_hp == 1000,
            "stale-healed checkpoint must hold the bar at full even with a second hit queued (no dip-then-snap)");
        expect(h.displayed_damage == 400,
            "healed checkpoint must not alter the skill damage digit");
    }

    {
        // Poison tick raises authoritative HP past an already-queued melee swing.
        // Repro: pot while poisoned. The server applied the melee hit (checkpoint
        // 400) at swing start, the potion healed to 600 with no sync of its own, and
        // the poison tick then reports 550 -- fresher than anything the client has.
        // The tick's raise must also supersede the older queued checkpoint, or the
        // melee hit reaches its frame, sees no later reconcile(), and drags the bar
        // back to 400 for up to a second.
        HpHarness h;
        h.visible_hp = 600;
        h.authoritative_hp = 450;

        DamageEvent swing = event(1, 10, 200, 400);
        swing.arrival_seq = next_hp_authority_seq();
        h.queue.push(swing);

        DamageEvent tick = event(2, 100, 50, 550);
        tick.presentation_kind = DamagePresentationKind::StatusTick;
        tick.arrival_seq = next_hp_authority_seq();
        h.queue.push(tick);

        expect(h.present_immediate() == PresentationResult::PresentedDamage,
            "poison tick presents immediately at receive");
        expect(h.authoritative_hp == 550,
            "a fresh StatusTick checkpoint must be able to raise authoritative HP");
        expect(h.last_sync_seq > swing.arrival_seq,
            "a raising StatusTick must out-rank the older queued swing");
        expect(h.visible_hp == 550, "poison tick lands the bar on server truth");

        expect(h.hit(10) == PresentationResult::PresentedDamage, "queued swing still presents");
        expect(h.displayed_damage == 200, "superseded checkpoint must not alter the swing digit");
        expect(h.visible_hp == 550,
            "swing superseded by the poison tick must not snap the bar back to its own checkpoint");
        expect(h.authoritative_hp == 550, "superseded checkpoint must not lower authoritative HP");
    }

    {
        // Control for the above: with no heal in play the poison tick lowers HP as
        // usual and must NOT stamp supersession, so a later-presented swing still
        // lands on its own checkpoint.
        HpHarness h;
        h.visible_hp = 600;
        h.authoritative_hp = 600;

        DamageEvent swing = event(1, 10, 200, 400);
        swing.arrival_seq = next_hp_authority_seq();
        h.queue.push(swing);

        DamageEvent tick = event(2, 100, 50, 550);
        tick.presentation_kind = DamagePresentationKind::StatusTick;
        tick.arrival_seq = next_hp_authority_seq();
        h.queue.push(tick);

        const uint32_t sync_before = h.last_sync_seq;
        expect(h.present_immediate() == PresentationResult::PresentedDamage,
            "poison tick presents immediately at receive");
        expect(h.authoritative_hp == 550, "lowering tick applies normally");
        expect(h.last_sync_seq == sync_before,
            "a tick that only lowers HP must not supersede older checkpoints");

        expect(h.hit(10) == PresentationResult::PresentedDamage, "queued swing presents");
        expect(h.visible_hp == 400, "without a heal the swing still lands on its own checkpoint");
    }

    {
        // Control: same hit with no intervening heal must still drop to its
        // checkpoint. The guard must not fire when no later sync superseded it.
        HpHarness h;
        h.visible_hp = 1000;
        h.authoritative_hp = 1000;

        DamageEvent skill = event(1, 10, 400, 600);
        skill.arrival_seq = next_hp_authority_seq();
        h.queue.push(skill);

        expect(h.hit(10) == PresentationResult::PresentedDamage, "deferred skill hit presents");
        expect(h.visible_hp == 600, "without a later heal the bar lands on the checkpoint");
    }

    {
        HpHarness h;
        h.visible_hp = 1000;
        h.authoritative_hp = 1000;
        h.reconcile(600);
        expect(h.visible_hp == 1000,
            "lower stat sync waits while legacy skill damage is still in the caster payload");
        expect(h.pending_correction == 400, "lower stat sync stages pending correction");
        h.queue.push(event(1, 10, 400, 600));
        expect(h.hit(10) == PresentationResult::PresentedDamage,
            "skill hit should present after lower stat sync");
        expect(h.visible_hp == 600,
            "skill hit must land on authoritative hp_after without double-subtracting correction");
        expect(h.displayed_damage == 400,
            "authoritative checkpoint correction must not alter the skill damage digit");
        expect(h.pending_correction == 0, "resolved skill checkpoint clears pending correction");
    }

    {
        HpHarness h;
        h.visible_hp = 1000;
        h.authoritative_hp = 1000;
        h.queue.push(event(1, 10, 250, 700));
        expect(h.hit(10) == PresentationResult::PresentedDamage, "damage delta should present");
        expect(h.visible_hp == 700, "lower hp_after should fold into the same drained hit");
        expect(h.displayed_damage == 250,
            "folded checkpoint correction must not change the displayed damage digit");
        expect(h.pending_correction == 0, "same-hit checkpoint correction should not linger");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 20, 70));
        expect(h.hit(10) == PresentationResult::PresentedDamage,
            "server-lower checkpoint should fold on drained queue");
        expect(h.visible_hp == 70, "same hit should land exactly on lower hp_after");
        expect(h.displayed_damage == 20, "same-hit HP correction should keep original damage digit");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 20, 70));
        h.queue.push(event(2, 20, 5, 65));
        expect(h.hit(10) == PresentationResult::PresentedDamage,
            "checkpoint drift should not fold while another hit is pending");
        expect(h.visible_hp == 80, "pending queued damage keeps first hit delta-only");
        expect(h.hit(20) == PresentationResult::PresentedDamage,
            "final pending hit should fold to authoritative checkpoint");
        expect(h.visible_hp == 65, "final drained hit converges to authoritative HP");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 25, 75));
        h.reconcile(60);
        expect(h.visible_hp == 100, "lower stat sync must wait while damage is pending");
        h.hit(10);
        expect(h.visible_hp == 60, "pending lower reconciliation folds into the presented hit");
        expect(h.displayed_damage == 25,
            "pending lower reconciliation should not inflate the displayed damage digit");
        expect(h.pending_correction == 0, "pending lower reconciliation should not linger");
    }

    {
        // Drain-on-death, two attackers: player 100, A queues 10 (server 90),
        // B queues 15 (server 75). Player kills A before its hit frame, so A is
        // drained on death. B's surviving hit must converge the bar to 75 while
        // the digit stays at B's real 15. (The user's 10+15 scenario.)
        HpHarness h;
        h.queue.push(event(1, 10, 10, 90));
        h.queue.push(event(2, 20, 15, 75));
        h.drain_from_attacker(10);
        expect(h.visible_hp == 100, "draining a dead attacker's orphan must not move visible HP yet");
        expect(h.hit(20) == PresentationResult::PresentedDamage,
            "surviving attacker keeps presenting its own hit after the orphan is drained");
        expect(h.visible_hp == 75,
            "drained orphan folds into the next hit so client converges to server HP");
        expect(h.displayed_damage == 15, "drained orphan must stay hidden from the damage digit");
    }

    {
        // Drain-on-death, lone attacker: A queues 10 (server 90) then dies before
        // its hit frame. With no other pending event the orphan stages drift, and
        // a later unrelated hit folds it in.
        HpHarness h;
        h.queue.push(event(1, 10, 10, 90));
        h.drain_from_attacker(10);
        expect(h.visible_hp == 100, "lone drained orphan must not jump the bar on death");
        expect(h.pending_correction == 10, "lone drained orphan stages drift for the next hit");
        h.queue.push(event(2, 30, 5, 85));
        h.hit(30);
        expect(h.visible_hp == 85, "next unrelated hit folds the drained orphan's HP");
        expect(h.displayed_damage == 5, "folded drain must stay hidden from the digit");
    }

    {
        // Mutual death, lone attacker (the user's exact edge case): the avatar kills
        // the only monster before its swing's hit frame, but the server already
        // applied that swing's lethal damage. Draining the lethal orphan must present
        // the death immediately -- there is no future incoming hit to fold it into,
        // so the player must not be left alive-client / dead-server and frozen.
        HpHarness h;
        h.queue.push(event(1, 10, 20, 0, true));
        h.drain_from_attacker(10, /*is_avatar=*/true);
        expect(h.visible_hp == 0,
            "lone lethal orphan presents mutual death immediately on the kill");
        expect(!h.pending_authoritative_death,
            "immediate mutual-death present clears the pending flag");
        expect(h.displayed_damage != 20,
            "mutual-death present must not show a phantom damage digit from the orphan");
    }

    {
        // Backstop timeout: the avatar is flagged pending-dead by a no-attacker
        // server kill (e.g. DoT / fall damage via reconcile to 0) and no incoming
        // hit ever arrives to present it. After the grace elapses, Proc() forces
        // the death. Modeled here by invoking the present helper directly.
        HpHarness h;
        h.reconcile(0);
        expect(h.visible_hp == 100,
            "reconcile-to-dead waits, it must not jump the bar to dead immediately");
        expect(h.pending_authoritative_death,
            "reconcile-to-dead flags pending authoritative death");
        h.present_pending_authoritative_death();
        expect(h.visible_hp == 0, "backstop timeout presents the pending death");
        expect(!h.pending_authoritative_death, "backstop present clears the pending flag");
    }

    {
        HpHarness h;
        h.reconcile(80);
        expect(h.visible_hp == 100, "lower stat sync without pending damage should wait for next hit");
        expect(h.pending_correction == 20, "unexplained lower sync creates pending correction");
        h.queue.push(event(1, 10, 5, 75));
        h.hit(10);
        expect(h.visible_hp == 75, "next hit should include unexplained lower sync correction");
        expect(h.displayed_damage == 5,
            "unexplained lower sync correction should stay hidden from damage digits");
    }

    {
        HpHarness h;
        h.reconcile(0);
        expect(h.visible_hp == 100, "death reconciliation without pending damage waits visually");
        expect(h.pending_correction == 0, "immediate death correction should not linger");
        expect(h.pending_authoritative_death,
            "dead reconciliation should mark pending authoritative death");
        expect(h.displayed_digits == 0, "pending death reconciliation must not create a damage digit");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 5, 0));
        h.reconcile(0);
        expect(h.visible_hp == 100, "death reconciliation waits while damage is pending");
        expect(h.pending_correction == 0, "pending death reconciliation is carried by authoritative HP");
        expect(h.hit(10) == PresentationResult::PresentedDeath,
            "next hit should present deferred death correction");
        expect(h.visible_hp == 0, "deferred death correction should converge to dead HP");
        expect(h.displayed_damage == 5,
            "deferred death correction should show the incoming hit's normal damage digit");
        expect(!h.pending_authoritative_death, "presented death clears the pending death flag");
    }

    {
        HpHarness h;
        h.reconcile(80);
        h.reconcile(125);
        expect(h.visible_hp == 125, "HP increases apply immediately");
        expect(h.pending_correction == 0, "HP increases clear pending corrections");
        expect(h.corrections == 1, "HP increase should not create an additional correction");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 0, 100));
        expect(h.hit(10) == PresentationResult::PresentedMiss, "zero damage event is a miss");
        expect(h.visible_hp == 100, "miss must not change HP");
        expect(h.corrections == 0, "miss must not trigger correction");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 250, 0, true));
        expect(h.hit(10) == PresentationResult::PresentedDeath,
            "lethal event without raw dead bit presents death");
        expect(h.visible_hp == 0, "lethal event applies death HP at presentation");
        expect(h.displayed_damage == 250, "lethal folded HP should display normal final-hit damage");
    }

    {
        HpHarness h;
        h.queue.push(event(1, 10, 5, 0, false));
        expect(h.hit(10) == PresentationResult::PresentedDeath,
            "non-lethal event with dead hp_after still presents death");
        expect(h.visible_hp == 0, "dead hp_after applies death HP at presentation");
        expect(h.displayed_damage == 5, "dead hp_after should not inflate the displayed digit");
    }

    {
        HpHarness player;
        HpHarness monster;
        player.reconcile(0);
        monster.queue.push(event(1, 10, 20, 80));
        expect(monster.outgoing_hit_while_pending_dead(10, &player)
                == PresentationResult::PresentedMiss,
            "server-dead player outgoing hit should present as miss");
        expect(monster.visible_hp == 100, "suppressed outgoing hit must not change monster HP");
        expect(monster.displayed_damage == 0, "suppressed outgoing hit should show miss/no damage");
        expect(monster.suppressed_outgoing_attacks == 1,
            "suppressed outgoing hit should be tracked");
        expect(player.pending_authoritative_death,
            "suppressed outgoing hit should keep player death pending");
    }

    {
        HpHarness player;
        HpHarness monster;
        player.reconcile(0);
        monster.queue.push(event(1, 10, 20, 0, true));
        expect(monster.outgoing_hit_while_pending_dead(10, &player)
                == PresentationResult::PresentedDeath,
            "mutual death should still present the monster's lethal hit");
        expect(monster.visible_hp == 0, "mutual death should kill the monster");
        expect(monster.displayed_damage == 20,
            "mutual death should show the monster hit's normal damage digit");
        expect(player.visible_hp == 0, "mutual death should kill the pending-dead player immediately");
        expect(!player.pending_authoritative_death,
            "mutual death should clear the player's pending death flag");
    }

    {
        HpHarness monster;
        DamageEvent lethal = event(1, 10, 35, 0, true);
        lethal.queued_at_ms = 1000;
        monster.queue.push(lethal);
        expect(monster.present_stale_lethal(2600, 1500) == PresentationResult::PresentedDeath,
            "stale lethal melee event should present death after grace window");
        expect(monster.visible_hp == 0, "stale lethal melee fallback should kill the defender");
        expect(monster.displayed_damage == 35,
            "stale lethal melee fallback should show the normal final-hit digit");
    }

    {
        HpHarness monster;
        DamageEvent lethal = event(1, 10, 35, 0, true);
        lethal.queued_at_ms = 1000;
        monster.queue.push(lethal);
        expect(monster.present_stale_lethal(2499, 1500) == PresentationResult::NoEvent,
            "fresh lethal melee event should not present before grace window");
        expect(monster.visible_hp == 100,
            "fresh lethal melee event should leave visible HP for the normal hit frame");
        expect(monster.hit(10) == PresentationResult::PresentedDeath,
            "fresh lethal melee event should remain queued for hit-frame presentation");
    }

    {
        // Slow swing still in flight (cart weapon): the stale fallback must wait
        // for the hit frame instead of killing the defender mid-animation.
        HpHarness monster;
        DamageEvent lethal = event(1, 10, 35, 0, true);
        lethal.queued_at_ms = 1000;
        monster.queue.push(lethal);
        expect(monster.present_stale_lethal_with_live_swing(2600, 1500, 6000, true)
                == PresentationResult::NoEvent,
            "stale lethal fallback must defer while the killer's swing is still live");
        expect(monster.visible_hp == 100,
            "deferred lethal event should leave visible HP for the late hit frame");
        expect(monster.hit(10) == PresentationResult::PresentedDeath,
            "deferred lethal event should remain queued for its hit-frame consumer");
        expect(monster.displayed_damage == 35,
            "late hit frame should show the normal final-hit digit");
    }

    {
        // Live-swing deferral is bounded: past the hard cap the fallback fires
        // even if the swing state still claims to be pending.
        HpHarness monster;
        DamageEvent lethal = event(1, 10, 35, 0, true);
        lethal.queued_at_ms = 1000;
        monster.queue.push(lethal);
        expect(monster.present_stale_lethal_with_live_swing(6999, 1500, 6000, true)
                == PresentationResult::NoEvent,
            "live swing should keep deferring inside the hard cap");
        expect(monster.present_stale_lethal_with_live_swing(7000, 1500, 6000, true)
                == PresentationResult::PresentedDeath,
            "stale lethal fallback must fire at the hard cap despite a live swing");
        expect(monster.visible_hp == 0, "hard-capped fallback should kill the defender");
    }

    {
        // Once the swing dies (interrupt, command change), the original grace
        // window applies again immediately.
        HpHarness monster;
        DamageEvent lethal = event(1, 10, 35, 0, true);
        lethal.queued_at_ms = 1000;
        monster.queue.push(lethal);
        expect(monster.present_stale_lethal_with_live_swing(2600, 1500, 6000, false)
                == PresentationResult::PresentedDeath,
            "stale lethal fallback must still fire after grace when no swing is live");
        expect(monster.displayed_damage == 35,
            "stale lethal fallback should show the normal final-hit digit");
    }

    // ---------------------------------------------------------------------------
    // Cart / castle-gear stale-lethal deferral.
    //
    // The blocks above cover the *timing* of the live-swing deferral with a
    // hard-coded bool. These cover how the predicate decides, which is where the
    // mounted case actually broke: the queued event is keyed to the cart's client
    // index while the pending confirmed swing is tracked on the rider, so looking
    // the swing up on the cart alone finds nothing, the fallback fires at 1.5 s and
    // the target dies mid-swing (the stone-hammer battle cart repro, 1c97f7ef).
    // ---------------------------------------------------------------------------
    const uint32_t kCart = 55;
    const uint32_t kRider = 10;
    const uint32_t kSwingEvent = 7;

    auto mounted_world = [&]() {
        SwingWorld world;
        SwingWorld::Attacker cart;
        cart.id = kCart;
        cart.is_pet = true;
        cart.parent_id = kRider;
        cart.pending_event = 0; // the cart itself never holds the pending swing
        world.add(cart);

        SwingWorld::Attacker rider;
        rider.id = kRider;
        rider.pending_event = kSwingEvent; // the rider does
        world.add(rider);
        return world;
    };

    auto mounted_lethal = [&]() {
        DamageEvent lethal = event(kSwingEvent, kCart, 35, 0, true);
        lethal.queued_at_ms = 1000;
        return lethal;
    };

    {
        SwingWorld world = mounted_world();
        HpHarness monster;
        monster.queue.push(mounted_lethal());

        expect(monster.present_stale_lethal_with_predicate(2600,
                   1500,
                   6000,
                   [&](const DamageEvent& e) { return world.swing_still_pending(e); })
                == PresentationResult::NoEvent,
            "mounted swing must defer: event keyed to the cart, swing tracked on the rider");
        expect(monster.visible_hp == 100,
            "cart-killed target must stay alive until the late hit frame");
        expect(monster.hit(kCart) == PresentationResult::PresentedDeath,
            "deferred cart event must remain queued for its hit-frame consumer");
        expect(monster.displayed_damage == 35,
            "cart hit frame should show the normal final-hit digit");
    }

    {
        // The rider moved on to a different swing: this orphan is never going to be
        // consumed, so the grace window must apply normally.
        SwingWorld world = mounted_world();
        world.find_mut(kRider)->pending_event = kSwingEvent + 1;

        HpHarness monster;
        monster.queue.push(mounted_lethal());

        expect(monster.present_stale_lethal_with_predicate(2600,
                   1500,
                   6000,
                   [&](const DamageEvent& e) { return world.swing_still_pending(e); })
                == PresentationResult::PresentedDeath,
            "mounted deferral must not apply when the rider holds a different event id");
    }

    {
        // Cart destroyed / dismounted mid-swing.
        SwingWorld world = mounted_world();
        world.find_mut(kCart)->alive = false;

        HpHarness monster;
        monster.queue.push(mounted_lethal());

        expect(monster.present_stale_lethal_with_predicate(2600,
                   1500,
                   6000,
                   [&](const DamageEvent& e) { return world.swing_still_pending(e); })
                == PresentationResult::PresentedDeath,
            "mounted deferral must not apply once the cart is dead");
    }

    {
        // Swing interrupted out of CMD_ATTACK: no hit frame is coming.
        SwingWorld world = mounted_world();
        world.find_mut(kCart)->attacking = false;

        HpHarness monster;
        monster.queue.push(mounted_lethal());

        expect(monster.present_stale_lethal_with_predicate(2600,
                   1500,
                   6000,
                   [&](const DamageEvent& e) { return world.swing_still_pending(e); })
                == PresentationResult::PresentedDeath,
            "mounted deferral must not apply once the cart leaves its attack command");
    }

    {
        // The parent fallback is gated on IsPET(). An ordinary attacker that happens
        // to carry a parent id must not inherit someone else's pending swing.
        SwingWorld world = mounted_world();
        world.find_mut(kCart)->is_pet = false;

        HpHarness monster;
        monster.queue.push(mounted_lethal());

        expect(monster.present_stale_lethal_with_predicate(2600,
                   1500,
                   6000,
                   [&](const DamageEvent& e) { return world.swing_still_pending(e); })
                == PresentationResult::PresentedDeath,
            "rider lookup must stay gated on IsPET so non-pet attackers do not defer");
    }

    {
        // Deferral is re-evaluated every tick: once the swing stops matching, the
        // original grace applies immediately rather than waiting for the hard cap.
        SwingWorld world = mounted_world();
        HpHarness monster;
        monster.queue.push(mounted_lethal());

        expect(monster.present_stale_lethal_with_predicate(2600,
                   1500,
                   6000,
                   [&](const DamageEvent& e) { return world.swing_still_pending(e); })
                == PresentationResult::NoEvent,
            "live mounted swing should defer on the first tick");

        world.find_mut(kRider)->pending_event = 0; // swing ended without consuming

        expect(monster.present_stale_lethal_with_predicate(2700,
                   1500,
                   6000,
                   [&](const DamageEvent& e) { return world.swing_still_pending(e); })
                == PresentationResult::PresentedDeath,
            "fallback must fire as soon as the swing stops matching, not at the hard cap");
        expect(monster.visible_hp == 0,
            "abandoned mounted swing must still kill the defender");
        expect(monster.displayed_damage == 35,
            "abandoned mounted swing should still show the final-hit digit");
    }

    {
        HpHarness monster;
        DamageEvent projectile = event(1, 10, 35, 0, true);
        projectile.presentation_kind = DamagePresentationKind::ProjectileImpact;
        projectile.queued_at_ms = 1000;
        monster.queue.push(projectile);
        expect(monster.present_stale_lethal(3000, 1500) == PresentationResult::NoEvent,
            "stale lethal fallback must not steal projectile-impact deaths");
        expect(monster.visible_hp == 100,
            "projectile lethal event should wait for projectile impact");
        expect(monster.hit(10) == PresentationResult::PresentedDeath,
            "projectile lethal event should remain queued for its impact consumer");
    }

    {
        HpHarness monster;
        DamageEvent projectile = event(1, 10, 35, 0, true);
        projectile.presentation_kind = DamagePresentationKind::ProjectileImpact;
        monster.queue.push(projectile);
        expect(monster.discard_from_attacker(10, false) == PresentationResult::PresentedDeath,
            "lethal projectile discard should kill a remote defender");
        expect(monster.visible_hp == 0,
            "remote defender should not stay visually alive after lethal projectile discard");
        expect(monster.displayed_damage == 35,
            "lethal projectile discard should show the normal final-hit digit");
    }

    {
        HpHarness monster;
        DamageEvent projectile = event(1, 10, 8, 92);
        projectile.presentation_kind = DamagePresentationKind::ProjectileImpact;
        monster.queue.push(projectile);
        expect(monster.discard_from_attacker(10, false) == PresentationResult::PresentedDamage,
            "nonlethal projectile discard should not present feedback");
        expect(monster.visible_hp == 100,
            "nonlethal projectile discard should not move visible HP");
        expect(monster.displayed_digits == 0,
            "nonlethal projectile discard should not create digits");
    }

    {
        HpHarness player;
        DamageEvent interrupted = event(1, 10, 12, 88);
        interrupted.presentation_kind = DamagePresentationKind::ProjectileImpact;
        player.queue.push(interrupted);

        expect(player.discard_event(1, true) == PresentationResult::PresentedDamage,
            "hard-control interruption should discard the exact orphaned projectile swing event");
        expect(player.visible_hp == 100,
            "interrupted projectile discard should not show a phantom HP drop");
        expect(player.pending_correction == 12,
            "interrupted projectile discard should stage the server-applied HP drift");

        DamageEvent next = event(2, 10, 8, 80);
        next.presentation_kind = DamagePresentationKind::ProjectileImpact;
        player.queue.push(next);

        expect(player.hit(10) == PresentationResult::PresentedDamage,
            "next projectile impact should consume its own event, not the interrupted one");
        expect(player.displayed_damage == 8,
            "next projectile impact should show only the new hit digit");
        expect(player.visible_hp == 80,
            "next projectile impact should fold the interrupted server HP drift");
        expect(!player.queue.has_pending_damage(),
            "discarded interrupted swing must not leave the avatar one attack behind");
    }

    {
        HpHarness player;
        DamageEvent lethal = event(1, 10, 100, 0, true);
        lethal.presentation_kind = DamagePresentationKind::ProjectileImpact;
        player.queue.push(lethal);

        expect(player.discard_event(1, true) == PresentationResult::PresentedDeath,
            "lethal hard-control interruption should present avatar death immediately");
        expect(player.visible_hp == 0,
            "lethal interrupted projectile discard should not strand the avatar alive-client");
        expect(!player.pending_authoritative_death,
            "immediate lethal discard presentation should clear pending death");
        expect(player.pending_correction == 0,
            "lethal interrupted projectile discard should not leave HP drift pending");
    }

    {
        CombatPresentationQueue q;
        DamageEvent e = event(1, 0, 8, 92);
        e.presentation_kind = DamagePresentationKind::MissingAttacker;
        q.push(e);

        DamageEvent out;
        expect(q.pop_immediate(out), "missing-attacker event should present immediately");
        expect(CombatPresentationQueue::result_for(out) == PresentationResult::PresentedDamage,
            "missing-attacker event still presents damage feedback");
    }

    {
        CombatPresentationQueue q;
        DamageEvent projectile = event(1, 10, 8, 92);
        projectile.presentation_kind = DamagePresentationKind::ProjectileImpact;
        q.push(projectile);

        DamageEvent immediate = event(2, 20, 4, 88);
        immediate.presentation_kind = DamagePresentationKind::Immediate;
        q.push(immediate);

        DamageEvent out;
        expect(q.pop_immediate(out), "immediate pop should skip projectile events");
        expect(out.event_id == 2, "projectile event must wait for bullet impact");
        expect(q.pop_for_attacker(10, out), "projectile event should remain queued for impact");
        expect(out.event_id == 1, "bullet impact consumes the projectile event later");
    }

    {
        CombatPresentationQueue q;
        DamageEvent projectile = event(1, 10, 8, 92);
        projectile.presentation_kind = DamagePresentationKind::ProjectileImpact;
        q.push(projectile);

        DamageEvent discarded;
        expect(q.discard_for_attacker(10, &discarded),
            "failed projectile creation should be able to discard without presenting");
        expect(discarded.event_id == 1, "discard should return the stranded projectile event");

        DamageEvent out;
        expect(!q.pop_for_attacker(10, out), "discarded projectile must not present later");
        expect(!q.has_pending_damage(), "discarded projectile must not block deferred correction");
    }

    {
        CombatPresentationQueue q;
        DamageEvent old_projectile = event(1, 10, 8, 92);
        old_projectile.presentation_kind = DamagePresentationKind::ProjectileImpact;
        q.push(old_projectile);

        DamageEvent interrupted = event(2, 10, 6, 86);
        interrupted.presentation_kind = DamagePresentationKind::ProjectileImpact;
        q.push(interrupted);

        DamageEvent discarded;
        expect(q.discard_event(2, &discarded),
            "hard-control cancellation should discard the interrupted exact event id");
        expect(discarded.event_id == 2,
            "exact discard should return the interrupted event");

        DamageEvent out;
        expect(q.pop_for_attacker(10, out),
            "older spawned projectile from the same attacker should remain queued");
        expect(out.event_id == 1,
            "exact discard must not steal another projectile from the same attacker");
        expect(!q.has_pending_damage(),
            "all projectile damage should be consumed after exact discard plus real impact");
    }

    {
        ProjectileSkillHarness h;
        h.receive_projectile_damage(10, 100, 2981, 12);
        h.receive_target_skill(10, 100, 2981);
        expect(h.visible_hp == 100, "projectile damage packet before cast must not move HP");
        expect(h.generic_immediate_processing() == PresentationResult::NoEvent,
            "generic immediate processing must skip projectile damage");
        expect(!h.proc_effected_skill_without_impact(),
            "projectile skill payload must wait when processed without impact");
        expect(h.visible_hp == 100 && h.digits == 0 && h.hit_feedback == 0,
            "no HP, digits, or hit feedback before projectile impact");
        expect(h.impact(10, 100, 2981) == PresentationResult::PresentedDamage,
            "projectile impact consumes queued damage");
        expect(h.visible_hp == 88 && h.digits == 1 && h.hit_feedback == 1,
            "projectile damage presents exactly at impact");
        expect(h.status_applied == 1, "status payload applies at impact with damage");
    }

    {
        ProjectileSkillHarness h;
        h.receive_target_skill(10, 100, 2981);
        h.receive_projectile_damage(10, 100, 2981, 9);
        expect(h.visible_hp == 100, "target skill before damage must not move HP before impact");
        expect(h.impact(10, 100, 2981) == PresentationResult::PresentedDamage,
            "target skill before damage still presents on projectile impact");
        expect(h.visible_hp == 91, "target skill before damage applies damage at impact");
        expect(h.status_applied == 1, "target skill before damage applies status at impact");
    }

    {
        ProjectileSkillHarness h;
        h.receive_projectile_damage(10, 100, 2981, 7);
        h.timeout_discard(10);
        expect(h.visible_hp == 100, "projectile timeout must not present HP damage");
        expect(h.digits == 0 && h.hit_feedback == 0,
            "projectile timeout must not create digits or hit feedback");
        expect(!h.queue.has_pending_damage(),
            "projectile timeout discard must unblock deferred correction");
    }

    {
        struct Case {
            int skill_type;
            int bullet_no;
            bool expected;
            const char* message;
        };

        const Case cases[] = {
            {5, 0, true, "enforce-bullet skills present at projectile impact"},
            {6, 0, true, "fire-bullet skills present at projectile impact"},
            {3, 10, true, "immediate ranged skills with bullets present at projectile impact"},
            {3, 0, false, "immediate skills without bullets present immediately"},
            {19, 10, true, "self-and-target skills with bullets present at projectile impact"},
            {9, 10, false, "target-bound-duration bullet ids are effect graphics, not projectiles"},
            {11, 10, false, "target-bound bullet ids are effect graphics, not projectiles"},
            {13, 10, false, "target-state-duration bullet ids are effect graphics, not projectiles"},
        };

        for (const Case& c: cases) {
            expect(Rose::Combat::is_projectile_presented_skill(c.skill_type, c.bullet_no)
                    == c.expected,
                c.message);
        }
    }

    {
        expect(should_block_non_aggro_script_skill(false, false, 1, 5, 6),
            "nearby hostile harmful projectile script skill should be blocked before aggro");
        expect(!should_block_non_aggro_script_skill(false, true, 1, 5, 6),
            "nearby hostile script skill should be allowed once target is current combat target");
        expect(!should_block_non_aggro_script_skill(true, false, 1, 3, 9),
            "allied/friendly script skill should not be blocked");
        expect(!should_block_non_aggro_script_skill(false, false, 0, 3, 9),
            "non-harmful non-damage friendly-style script skill should not be blocked");
        expect(should_block_non_aggro_script_skill(false, false, 0, 7, 6),
            "all-PC projectile script skill against non-allied target should be blocked");
    }

    std::cout << "combat_presenter_tests passed\n";
    return 0;
}
