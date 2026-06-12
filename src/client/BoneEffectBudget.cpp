#include "stdafx.h"

#include "BoneEffectBudget.h"
#include "CObjCHAR.h"
#include "OBJECT.h"

namespace {
// Particle texture batching removes most of the draw-call pressure for additive
// bone auras, but particle simulation still has a real cost. Keep this relaxed
// budget bounded so the tier system remains the safety net in dense scenes.
const int kCapacityBudget = 480;
const float kEmitRateBudget = 360.0f;
const int kMaxFullDuplicates = 3;
const DWORD kDemoteDelayMs = 250;
const DWORD kPromoteDelayMs = 1000;

int TierRank(EParticleEffectTier tier)
{
    return static_cast<int>(tier);
}
} // namespace

BoneEffectBudgetStats::BoneEffectBudgetStats() {
    groups = 0;
    effects = 0;
    emitters = 0;
    activeParticles = 0;
    runtimeCapacity = 0;
    configuredEmitRate = 0.0f;
    topNpc = 0;
    for (int i = 0; i < PARTICLE_EFFECT_TIER_COUNT; ++i)
        tierCounts[i] = 0;
}

CBoneEffectBudget&
CBoneEffectBudget::Instance() {
    static CBoneEffectBudget s_Instance;
    return s_Instance;
}

CBoneEffectBudget::CBoneEffectBudget() {}

void
CBoneEffectBudget::Register(CObjCHAR* pOwner, CEffect* pEffect, t_HASHKEY effectHash, int iBoneIndex) {
    if (!pOwner || !pEffect || pEffect->GetParticleEmitterCount() <= 0)
        return;

    for (std::vector<Record>::iterator it = m_Records.begin(); it != m_Records.end(); ++it) {
        if (it->effect == pEffect)
            return;
    }

    Record record;
    record.owner = pOwner;
    record.effect = pEffect;
    record.effectHash = effectHash;
    record.npcId = pOwner->Get_CharNO();
    record.boneIndex = iBoneIndex;
    record.configuredCapacity = pEffect->GetConfiguredParticleCapacity();
    record.configuredEmitRate = pEffect->GetConfiguredParticleEmitRate();
    record.emitterCount = pEffect->GetParticleEmitterCount();
    record.appliedTier = PARTICLE_EFFECT_TIER_FULL;
    record.desiredTier = PARTICLE_EFFECT_TIER_FULL;
    record.tierRequestTime = 0;
    record.distanceSq = 0.0f;
    record.visible = true;
    record.target = false;
    record.player = false;

    m_Records.push_back(record);
}

void
CBoneEffectBudget::Unregister(CEffect* pEffect) {
    if (!pEffect)
        return;

    for (std::vector<Record>::iterator it = m_Records.begin(); it != m_Records.end();) {
        if (it->effect == pEffect)
            it = m_Records.erase(it);
        else
            ++it;
    }
}

unsigned __int64
CBoneEffectBudget::MakeSignature(const Record& record) const {
    return (static_cast<unsigned __int64>(record.npcId & 0xffff) << 48)
        ^ static_cast<unsigned __int64>(record.effectHash);
}

int
CBoneEffectBudget::CapacityForTier(const Record& record, EParticleEffectTier tier) const {
    switch (tier) {
        case PARTICLE_EFFECT_TIER_REDUCED:
            return max(8 * record.emitterCount, (int)(record.configuredCapacity * 0.40f));
        case PARTICLE_EFFECT_TIER_MINIMAL:
            return min(6 * record.emitterCount,
                max(2 * record.emitterCount, (int)(record.configuredCapacity * 0.12f)));
        case PARTICLE_EFFECT_TIER_OFF:
            return 0;
        case PARTICLE_EFFECT_TIER_FULL:
        default:
            return record.configuredCapacity;
    }
}

float
CBoneEffectBudget::EmitRateForTier(const Record& record, EParticleEffectTier tier) const {
    switch (tier) {
        case PARTICLE_EFFECT_TIER_REDUCED:
            return record.configuredEmitRate * 0.35f;
        case PARTICLE_EFFECT_TIER_MINIMAL:
            return record.configuredEmitRate * 0.08f;
        case PARTICLE_EFFECT_TIER_OFF:
            return 0.0f;
        case PARTICLE_EFFECT_TIER_FULL:
        default:
            return record.configuredEmitRate;
    }
}

EParticleEffectTier
CBoneEffectBudget::PickTier(
    const Record& record,
    int& usedCapacity,
    float& usedEmitRate,
    std::map<unsigned __int64, int>& fullDuplicateCounts) {
    if (!record.visible)
        return PARTICLE_EFFECT_TIER_OFF;

    const unsigned __int64 signature = MakeSignature(record);
    if (fullDuplicateCounts[signature] < kMaxFullDuplicates) {
        const int fullCapacity = CapacityForTier(record, PARTICLE_EFFECT_TIER_FULL);
        const float fullEmitRate = EmitRateForTier(record, PARTICLE_EFFECT_TIER_FULL);
        if (usedCapacity + fullCapacity <= kCapacityBudget && usedEmitRate + fullEmitRate <= kEmitRateBudget) {
            usedCapacity += fullCapacity;
            usedEmitRate += fullEmitRate;
            fullDuplicateCounts[signature]++;
            return PARTICLE_EFFECT_TIER_FULL;
        }
    }

    const int reducedCapacity = CapacityForTier(record, PARTICLE_EFFECT_TIER_REDUCED);
    const float reducedEmitRate = EmitRateForTier(record, PARTICLE_EFFECT_TIER_REDUCED);
    if (usedCapacity + reducedCapacity <= kCapacityBudget && usedEmitRate + reducedEmitRate <= kEmitRateBudget) {
        usedCapacity += reducedCapacity;
        usedEmitRate += reducedEmitRate;
        return PARTICLE_EFFECT_TIER_REDUCED;
    }

    const int minimalCapacity = CapacityForTier(record, PARTICLE_EFFECT_TIER_MINIMAL);
    const float minimalEmitRate = EmitRateForTier(record, PARTICLE_EFFECT_TIER_MINIMAL);
    if (usedCapacity + minimalCapacity <= kCapacityBudget && usedEmitRate + minimalEmitRate <= kEmitRateBudget) {
        usedCapacity += minimalCapacity;
        usedEmitRate += minimalEmitRate;
        return PARTICLE_EFFECT_TIER_MINIMAL;
    }

    return PARTICLE_EFFECT_TIER_OFF;
}

void
CBoneEffectBudget::ApplyTierWithHysteresis(Record& record, EParticleEffectTier desiredTier, DWORD now) {
    if (!record.effect)
        return;

    if (record.appliedTier == desiredTier) {
        record.desiredTier = desiredTier;
        record.tierRequestTime = 0;
        return;
    }

    if (!record.visible && desiredTier == PARTICLE_EFFECT_TIER_OFF) {
        record.effect->SetParticleTier(desiredTier);
        record.appliedTier = desiredTier;
        record.desiredTier = desiredTier;
        record.tierRequestTime = 0;
        return;
    }

    if (record.desiredTier != desiredTier) {
        record.desiredTier = desiredTier;
        record.tierRequestTime = now;
        return;
    }

    const bool demoting = TierRank(desiredTier) > TierRank(record.appliedTier);
    const DWORD delay = demoting ? kDemoteDelayMs : kPromoteDelayMs;
    if (now - record.tierRequestTime < delay)
        return;

    record.effect->SetParticleTier(desiredTier);
    record.appliedTier = desiredTier;
    record.tierRequestTime = 0;
}

void
CBoneEffectBudget::Update() {
    m_Stats = BoneEffectBudgetStats();

    if (m_Records.empty() || !g_pAVATAR)
        return;

    const DWORD now = g_GameDATA.GetGameTime();
    const D3DXVECTOR3 avatarPos = g_pAVATAR->Get_CurPOS();
    const int targetClientIndex =
        g_pObjMGR ? g_pObjMGR->Get_ClientObjectIndex(g_pAVATAR->Get_TargetIDX()) : 0;

    std::vector<int> order;
    order.reserve(m_Records.size());
    std::map<int, int> npcCost;

    for (int i = 0; i < (int)m_Records.size(); ++i) {
        Record& record = m_Records[i];
        if (!record.owner || !record.effect)
            continue;

        const D3DXVECTOR3 delta = record.owner->Get_CurPOS() - avatarPos;
        record.distanceSq = D3DXVec3LengthSq(&delta);
        record.visible = record.owner->IsVisible() && record.owner->IsInViewFrustum();
        record.target = (record.owner->Get_INDEX() == targetClientIndex);
        record.player = record.owner->IsA(OBJ_USER);
        record.configuredCapacity = record.effect->GetConfiguredParticleCapacity();
        record.configuredEmitRate = record.effect->GetConfiguredParticleEmitRate();
        record.emitterCount = record.effect->GetParticleEmitterCount();

        m_Stats.groups++;
        m_Stats.effects++;
        m_Stats.emitters += record.emitterCount;
        m_Stats.activeParticles += record.effect->GetActiveParticleCount();
        m_Stats.runtimeCapacity += record.effect->GetRuntimeParticleCapacity();
        m_Stats.configuredEmitRate += record.configuredEmitRate;
        if (record.appliedTier >= 0 && record.appliedTier < PARTICLE_EFFECT_TIER_COUNT)
            m_Stats.tierCounts[record.appliedTier]++;

        npcCost[record.npcId] += record.configuredCapacity;
        order.push_back(i);
    }

    for (std::map<int, int>::iterator it = npcCost.begin(); it != npcCost.end(); ++it) {
        if (!m_Stats.topNpc || it->second > npcCost[m_Stats.topNpc])
            m_Stats.topNpc = it->first;
    }

    std::sort(order.begin(), order.end(), [this](int lhs, int rhs) {
        const Record& a = m_Records[lhs];
        const Record& b = m_Records[rhs];
        if (a.player != b.player)
            return a.player;
        if (a.target != b.target)
            return a.target;
        if (a.visible != b.visible)
            return a.visible;
        return a.distanceSq < b.distanceSq;
    });

    int usedCapacity = 0;
    float usedEmitRate = 0.0f;
    std::map<unsigned __int64, int> fullDuplicateCounts;

    for (std::vector<int>::iterator it = order.begin(); it != order.end(); ++it) {
        Record& record = m_Records[*it];
        const EParticleEffectTier desiredTier =
            PickTier(record, usedCapacity, usedEmitRate, fullDuplicateCounts);
        ApplyTierWithHysteresis(record, desiredTier, now);
    }
}
