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

struct HpHarness {
    int visible_hp = 100;
    int authoritative_hp = 100;
    bool has_authoritative_hp = true;
    uint32_t authoritative_seq = 0;
    int corrections = 0;
    int pending_correction = 0;
    int displayed_damage = -1;
    int displayed_digits = 0;
    bool pending_authoritative_death = false;
    int suppressed_outgoing_attacks = 0;
    CombatPresentationQueue queue;

    void set_authoritative_from_event(const DamageEvent& e) {
        if (!has_authoritative_hp || e.hp_after <= authoritative_hp) {
            authoritative_hp = e.hp_after;
            authoritative_seq = e.defender_seq;
            has_authoritative_hp = true;
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
        DamageEvent e;
        if (!queue.pop_stale_lethal(now_ms, grace_ms, e)) {
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

private:
    PresentationResult hit_internal(uint32_t attacker, bool suppress_for_pending_dead_attacker, HpHarness* pending_dead_attacker) {
        DamageEvent e;
        if (!queue.pop_for_attacker(attacker, e)) {
            return PresentationResult::NoEvent;
        }
        set_authoritative_from_event(e);

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
                const int target_hp = std::min(e.hp_after, authoritative_hp);
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
