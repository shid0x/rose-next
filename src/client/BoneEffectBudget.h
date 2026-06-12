#ifndef __BONE_EFFECT_BUDGET_H
#define __BONE_EFFECT_BUDGET_H

#include "IO_Effect.h"

#include <map>
#include <vector>

class CObjCHAR;

struct BoneEffectBudgetStats {
    int groups;
    int effects;
    int emitters;
    int activeParticles;
    int runtimeCapacity;
    float configuredEmitRate;
    int tierCounts[PARTICLE_EFFECT_TIER_COUNT];
    int topNpc;

    BoneEffectBudgetStats();
};

class CBoneEffectBudget {
public:
    static CBoneEffectBudget& Instance();

    void Register(CObjCHAR* pOwner, CEffect* pEffect, t_HASHKEY effectHash, int iBoneIndex);
    void Unregister(CEffect* pEffect);
    void Update();

    const BoneEffectBudgetStats& GetStats() const { return m_Stats; }

private:
    struct Record {
        CObjCHAR* owner;
        CEffect* effect;
        t_HASHKEY effectHash;
        int npcId;
        int boneIndex;
        int configuredCapacity;
        float configuredEmitRate;
        int emitterCount;
        EParticleEffectTier appliedTier;
        EParticleEffectTier desiredTier;
        DWORD tierRequestTime;
        float distanceSq;
        bool visible;
        bool target;
        bool player;
    };

    CBoneEffectBudget();

    void ApplyTierWithHysteresis(Record& record, EParticleEffectTier desiredTier, DWORD now);
    EParticleEffectTier PickTier(
        const Record& record,
        int& usedCapacity,
        float& usedEmitRate,
        std::map<unsigned __int64, int>& fullDuplicateCounts);
    unsigned __int64 MakeSignature(const Record& record) const;
    int CapacityForTier(const Record& record, EParticleEffectTier tier) const;
    float EmitRateForTier(const Record& record, EParticleEffectTier tier) const;

    std::vector<Record> m_Records;
    BoneEffectBudgetStats m_Stats;
};

#endif
