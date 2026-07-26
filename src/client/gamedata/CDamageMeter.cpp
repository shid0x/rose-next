#include "stdafx.h"

#include "CDamageMeter.h"

#include "Game.h"
#include "Object.h"
#include "CObjUSER.h"
#include "CObjCART.h"
#include "CParty.h"
#include "Common/IO_Skill.h"
#include "net_prototype.h"

#include <algorithm>
#include <map>

namespace {

/// Tuning constants (grouped here for easy modding).
const DWORD kIdleGapMs = 6000;      ///< silence that ends a fight segment
const size_t kMaxSamples = 8192;    ///< sample ring capacity
const size_t kMaxRecentKeys = 128;  ///< event dedup ring capacity
const size_t kMaxRecentLethals = 32;
const DWORD kLethalDupWindowMs = 2000;

unsigned __int64
MakeEventKey(const Rose::Combat::DamageEvent& event) {
    return ((unsigned __int64)event.event_id << 32)
        | ((unsigned __int64)(event.attacker_id & 0xFFFF) << 16)
        | (unsigned __int64)(event.defender_id & 0xFFFF);
}

bool
IsOwnSummon(WORD wAtkSvrIdx) {
    if (g_pAVATAR == NULL)
        return false;

    const std::list<SummonMobInfo>& list = g_pAVATAR->GetSummonedMobList();
    for (std::list<SummonMobInfo>::const_iterator it = list.begin(); it != list.end(); ++it) {
        if (it->iIndex == (int)wAtkSvrIdx)
            return true;
    }
    return false;
}

/// Label for the self-skill breakdown view. skill_id wins; otherwise the
/// presentation kind separates normal attacks from DoT/other damage.
std::string
SkillLabel(const MeterSample& sample) {
    if (sample.btSource == METER_SRC_OWN_PET)
        return "Pets";

    if (sample.iSkillIdx > 0) {
        const char* pName = SKILL_NAME(sample.iSkillIdx);
        if (pName != NULL && pName[0] != '\0')
            return pName;
    }

    switch ((Rose::Combat::DamagePresentationKind)sample.btKind) {
        case Rose::Combat::DamagePresentationKind::StatusTick:
            return "DoT";
        case Rose::Combat::DamagePresentationKind::Immediate:
            return "Other";
        default:
            return "Attack";
    }
}

void
AccumulateRow(CDamageMeter::Row& row, const MeterSample& sample) {
    if (sample.bMiss) {
        ++row.iMisses;
    } else {
        ++row.iHits;
        row.iTotal += sample.iDamage;
        if (sample.bCrit)
            ++row.iCrits;
        if (sample.iDamage > row.iMaxHit) {
            row.iMaxHit = sample.iDamage;
            row.iMaxHitSkill = sample.iSkillIdx;
        }
    }
}

void
MapToSortedRows(std::map<std::string, CDamageMeter::Row>& rows,
    std::vector<CDamageMeter::Row>& out) {
    out.reserve(rows.size());
    for (std::map<std::string, CDamageMeter::Row>::iterator it = rows.begin(); it != rows.end();
         ++it) {
        it->second.strName = it->first;
        out.push_back(it->second);
    }
    std::sort(out.begin(), out.end(), [](const CDamageMeter::Row& a, const CDamageMeter::Row& b) {
        return a.iTotal > b.iTotal;
    });
}

} // namespace

CDamageMeter::CDamageMeter()
    : m_wSegment(0)
    , m_dwLastSampleTime(0) {}

CDamageMeter&
CDamageMeter::GetInstance() {
    static CDamageMeter s_Instance;
    return s_Instance;
}

void
CDamageMeter::Reset() {
    m_Samples.clear();
    m_RecentKeys.clear();
    m_RecentLethals.clear();
    m_wSegment = 0;
    m_dwLastSampleTime = 0;
}

bool
CDamageMeter::IsDuplicate(const Rose::Combat::DamageEvent& event) {
    const unsigned __int64 key = MakeEventKey(event);
    for (std::deque<unsigned __int64>::const_iterator it = m_RecentKeys.begin();
         it != m_RecentKeys.end();
         ++it) {
        if (*it == key)
            return true;
    }

    m_RecentKeys.push_back(key);
    while (m_RecentKeys.size() > kMaxRecentKeys)
        m_RecentKeys.pop_front();
    return false;
}

bool
CDamageMeter::IsDuplicateLethal(const Rose::Combat::DamageEvent& event, DWORD dwNow) {
    if (!event.lethal)
        return false;

    for (std::deque<LethalMark>::const_iterator it = m_RecentLethals.begin();
         it != m_RecentLethals.end();
         ++it) {
        if (it->iDefender == (int)event.defender_id && (dwNow - it->dwTime) < kLethalDupWindowMs)
            return true;
    }

    LethalMark mark;
    mark.iDefender = (int)event.defender_id;
    mark.dwTime = dwNow;
    m_RecentLethals.push_back(mark);
    while (m_RecentLethals.size() > kMaxRecentLethals)
        m_RecentLethals.pop_front();
    return false;
}

void
CDamageMeter::OnCombatDamageEvent(const Rose::Combat::DamageEvent& event, CObjCHAR* pDefender) {
    if (g_pAVATAR == NULL || g_pObjMGR == NULL || pDefender == NULL)
        return;

    const DWORD dwNow = g_GameDATA.GetGameTime();

    // Legacy + FlatBuffer paths can each announce the same death; a defender
    // only dies once. Event-key dedup additionally guards re-pushed events.
    if (IsDuplicate(event) || IsDuplicateLethal(event, dwNow))
        return;

    // Resolve the attacker now — object slots are recycled, names and party
    // membership cannot be resolved later. Cart / castle gear damage is keyed
    // to the mount object; credit the rider. IsPET() covers both OBJ_CART and
    // OBJ_CGEAR (castle gear is its own type — checking OBJ_CART alone made
    // castle gear damage vanish from the meter), and CObjCastleGear derives
    // from CObjCART so GetParent() is valid for both.
    CObjCHAR* pAtkOBJ =
        (event.attacker_id > 0) ? g_pObjMGR->Get_CharOBJ((int)event.attacker_id, false) : NULL;
    if (pAtkOBJ != NULL && pAtkOBJ->IsPET()) {
        CObjCHAR* pRider = ((CObjCART*)pAtkOBJ)->GetParent();
        if (pRider != NULL)
            pAtkOBJ = pRider;
    }

    // Credit target: the wire's source_attacker_id (DoT caster, summon owner)
    // wins over the raw attacker. For a status tick the attacker is the victim
    // and the source is the caster — that's attribution, not a pet. For a
    // summon swing the attacker is the pet and the source is the owner — fold
    // the damage into the owner and remember it was pet damage.
    CObjCHAR* pCreditOBJ = pAtkOBJ;
    bool bPetSample = false;
    if (event.source_attacker_id != 0) {
        CObjCHAR* pSrcOBJ = g_pObjMGR->Get_CharOBJ((int)event.source_attacker_id, false);
        if (pSrcOBJ != NULL && pSrcOBJ != pAtkOBJ) {
            bPetSample =
                (event.presentation_kind != Rose::Combat::DamagePresentationKind::StatusTick);
            pCreditOBJ = pSrcOBJ;
        }
    }

    BYTE btSource = METER_SRC_OTHER;
    if (pCreditOBJ == g_pAVATAR) {
        btSource = bPetSample ? METER_SRC_OWN_PET : METER_SRC_SELF;
    } else if (pCreditOBJ != NULL) {
        const WORD wCreditSvrIdx = g_pObjMGR->Get_ServerObjectIndex(pCreditOBJ->Get_INDEX());
        // Own-summon fallback for events without a wire source (legacy paths).
        if (IsOwnSummon(wCreditSvrIdx))
            btSource = METER_SRC_OWN_PET;
        else if (pCreditOBJ->IsUSER() && CParty::GetInstance().IsPartyMember(wCreditSvrIdx))
            btSource = METER_SRC_PARTY;
    }

    // Same mount fold on the defender side: a hit keyed to the avatar's
    // cart/castle gear is damage taken by the avatar.
    CObjCHAR* pDefOBJ = pDefender;
    if (pDefOBJ->IsPET()) {
        CObjCHAR* pRider = ((CObjCART*)pDefOBJ)->GetParent();
        if (pRider != NULL)
            pDefOBJ = pRider;
    }
    const bool bIncoming = (pDefOBJ == g_pAVATAR);

    // Keep only what the views consume: our side's outgoing damage, and
    // anything that hits the avatar. Stranger-vs-mob spectator data is dropped.
    if (btSource == METER_SRC_OTHER && !bIncoming)
        return;

    // Fight segmentation by idle gap.
    if (!m_Samples.empty() && (dwNow - m_dwLastSampleTime) > kIdleGapMs)
        ++m_wSegment;
    m_dwLastSampleTime = dwNow;

    MeterSample sample;
    sample.dwTime = dwNow;
    sample.wSegment = m_wSegment;
    sample.iSkillIdx = (event.skill_id > 0) ? event.skill_id : 0;
    sample.iDamage = (event.damage_value > 0) ? event.damage_value : 0;
    sample.btSource = btSource;
    sample.btKind = (BYTE)event.presentation_kind;
    sample.bCrit = (event.raw_damage & DMG_BIT_CRITICAL) != 0;
    sample.bMiss = (event.damage_value <= 0);
    sample.bLethal = event.lethal;
    sample.bIncoming = bIncoming;

    // Row name = credit character (owner for pets, caster for DoTs). An
    // incoming tick whose credit is still the avatar itself is an
    // unattributed self-tick (old server / caster unknown).
    if (bIncoming && pCreditOBJ == g_pAVATAR) {
        sample.strAttacker = "DoT / Status";
    } else if (pCreditOBJ != NULL && pCreditOBJ->Get_NAME() != NULL
        && pCreditOBJ->Get_NAME()[0] != '\0') {
        sample.strAttacker = pCreditOBJ->Get_NAME();
    } else {
        sample.strAttacker = "Unknown";
    }

    m_Samples.push_back(sample);
    while (m_Samples.size() > kMaxSamples)
        m_Samples.pop_front();
}

void
CDamageMeter::BuildSnapshot(FightSnapshot& out) const {
    out = FightSnapshot();
    if (m_Samples.empty() || g_pAVATAR == NULL)
        return;

    const char* pSelfName = g_pAVATAR->Get_NAME();
    const std::string strSelf =
        (pSelfName != NULL && pSelfName[0] != '\0') ? pSelfName : "You";

    std::map<std::string, Row> outgoing;
    std::map<std::string, Row> skills;
    std::map<std::string, Row> incoming;

    for (std::deque<MeterSample>::const_iterator it = m_Samples.begin(); it != m_Samples.end();
         ++it) {
        const MeterSample& sample = *it;
        if (sample.wSegment != m_wSegment)
            continue;

        if (out.dwStart == 0)
            out.dwStart = sample.dwTime;
        out.dwEnd = sample.dwTime;

        if (sample.bIncoming) {
            AccumulateRow(incoming[sample.strAttacker], sample);
            continue;
        }

        // Outgoing: self + own pets fold into the avatar's row; party members
        // keep their own names. (bIncoming self-damage never reaches here.)
        switch (sample.btSource) {
            case METER_SRC_SELF:
            case METER_SRC_OWN_PET:
                AccumulateRow(outgoing[strSelf], sample);
                AccumulateRow(skills[SkillLabel(sample)], sample);
                break;
            case METER_SRC_PARTY:
                AccumulateRow(outgoing[sample.strAttacker], sample);
                break;
            default:
                break;
        }
    }

    out.bActive = (g_GameDATA.GetGameTime() - m_dwLastSampleTime) <= kIdleGapMs;

    MapToSortedRows(outgoing, out.OutgoingByAttacker);
    MapToSortedRows(skills, out.OutgoingSkillsSelf);
    MapToSortedRows(incoming, out.IncomingByAttacker);
}
