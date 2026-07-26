#ifndef _CDAMAGEMETER_
#define _CDAMAGEMETER_

#include <windows.h>

#include <deque>
#include <string>
#include <vector>

#include "rose/combat/combat_presentation.h"

class CObjCHAR;

/**
 * Damage meter data core (client-only).
 *
 * Fed by a read-only tap in CObjCHAR::PushCombatDamageEvent — every
 * server-authored damage event (FlatBuffer CombatSwing/DamageEvent and all
 * legacy GSV_DAMAGE / GSV_DAMAGE_OF_SKILL conversions) funnels through that
 * one chokepoint exactly once. The meter observes; it NEVER mutates events,
 * the presentation queue, or any combat display state.
 *
 * Samples are recorded at queue time (== packet receive for all immediate
 * paths). This is deliberately upstream of presentation: damage whose
 * animation is later discarded/folded (hard-control interrupts, drain-on-
 * death) was still real server damage and must count.
 *
 * The UI (CDamageMeterPanel) is a separate consumer built entirely on
 * BuildSnapshot(); modders can restyle or replace the panel without touching
 * this class.
 */

/// Attribution category of a sample's attacker, resolved at record time
/// (object indices are recycled, so names/categories cannot be resolved later).
enum t_MeterSource {
    METER_SRC_SELF = 0, ///< the avatar (cart/castle-gear attacks fold to the rider)
    METER_SRC_OWN_PET,  ///< one of the avatar's own summons
    METER_SRC_PARTY,    ///< another party member (visible in-range only)
    METER_SRC_OTHER,    ///< anyone else (recorded only when it hits the avatar)
};

struct MeterSample {
    DWORD dwTime;      ///< g_GameDATA.GetGameTime() at record
    WORD wSegment;     ///< fight-segment counter (new segment after idle gap)
    int iSkillIdx;     ///< LIST_SKILL row, 0 = normal attack / unknown
    int iDamage;       ///< displayed damage (DamageEvent.damage_value)
    BYTE btSource;     ///< t_MeterSource
    BYTE btKind;       ///< Rose::Combat::DamagePresentationKind
    bool bCrit;        ///< DMG_BIT_CRITICAL in raw_damage
    bool bMiss;        ///< no damage dealt (dodge/miss presentation)
    bool bLethal;      ///< killing blow
    bool bIncoming;    ///< defender is the local avatar
    std::string strAttacker; ///< display name resolved at record time
};

class CDamageMeter {
public:
    /// One aggregated display row (per attacker or per skill).
    struct Row {
        std::string strName;
        __int64 iTotal;
        int iHits;   ///< landed hits (damage > 0)
        int iCrits;
        int iMisses;
        int iMaxHit;
        int iMaxHitSkill; ///< skill of the biggest hit (0 = normal attack)

        Row(): iTotal(0), iHits(0), iCrits(0), iMisses(0), iMaxHit(0), iMaxHitSkill(0) {}
    };

    /// Aggregates of the current fight segment (== last fight while idle).
    struct FightSnapshot {
        DWORD dwStart;  ///< first sample time of the segment (0 = no data)
        DWORD dwEnd;    ///< last sample time of the segment
        bool bActive;   ///< a sample arrived within the idle gap

        std::vector<Row> OutgoingByAttacker; ///< self + own pets folded, party members; sorted desc
        std::vector<Row> OutgoingSkillsSelf; ///< self damage by skill (pets under one "Pets" row)
        std::vector<Row> IncomingByAttacker; ///< damage taken by the avatar, by attacker name

        FightSnapshot(): dwStart(0), dwEnd(0), bActive(false) {}

        DWORD DurationMs() const { return (dwEnd > dwStart) ? (dwEnd - dwStart) : 0; }
    };

    static CDamageMeter& GetInstance();

    /// Read-only tap called from CObjCHAR::PushCombatDamageEvent.
    /// pDefender is the queue owner (`this` at the call site).
    void OnCombatDamageEvent(const Rose::Combat::DamageEvent& event, CObjCHAR* pDefender);

    /// Drops all samples and dedup state (e.g. "/dps reset", zone change is NOT
    /// auto-reset — a fight segment simply ends by idle gap).
    void Reset();

    /// Aggregates the current fight segment for display. Cheap enough to call
    /// a few times per second (sample count is capped).
    void BuildSnapshot(FightSnapshot& out) const;

private:
    CDamageMeter();

    bool IsDuplicate(const Rose::Combat::DamageEvent& event);
    bool IsDuplicateLethal(const Rose::Combat::DamageEvent& event, DWORD dwNow);

    std::deque<MeterSample> m_Samples;
    WORD m_wSegment;
    DWORD m_dwLastSampleTime;

    /// Dedup ring: (event_id, attacker, defender) keys of recently recorded
    /// events. The presentation queue dedups FlatBuffer re-pushes by event_id
    /// internally, but the tap sits before that knowledge, and synthetic legacy
    /// ids live in separate counters — so the meter keeps its own guard.
    std::deque<unsigned __int64> m_RecentKeys;

    /// A defender dies once: a second lethal event for the same defender within
    /// the guard window (legacy + FlatBuffer death packet overlap) is dropped.
    struct LethalMark {
        int iDefender;
        DWORD dwTime;
    };
    std::deque<LethalMark> m_RecentLethals;
};

#endif // _CDAMAGEMETER_
