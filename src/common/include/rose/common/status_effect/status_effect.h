#ifndef STATUS_EFFECT_H
#define STATUS_EFFECT_H
#pragma once

#include <cstdint>

namespace Rose {
namespace Common {

enum class StatusEffectType : unsigned int {
    Null,

    // HP/MP status effects
    HPMPStart,
    IncreaseHP = HPMPStart,
    IncreaseMP,
    Poisoned,
    IncreaseMaxHP,
    IncreaseMaxMP,
    HPMPEnd = IncreaseMaxMP,

    // General status effects
    GeneralStart,
    IncreaseMoveSpeed = GeneralStart,
    DecreaseMoveSpeed,
    IncreaseAttackSpeed,
    DecreaseAttackSpeed,
    IncreaseAttackPower,
    DecreaseAttackPower,
    IncreaseDefense,
    DecreaseDefense,
    IncreaseMagicRes,
    DecreaseMagicRes,
    IncreaseHit,
    DecreaseHit,
    IncreaseCrit,
    DecreaseCrit,
    IncreaseDodge,
    DecreaseDodge,
    Dumb,
    Sleep,
    Faint,
    Disguise,
    Invisible,
    ShieldDamage,
    AdditionalDamage,
    GeneralEnd = AdditionalDamage,

    DecreaseLifetime,

    ClearGood,
    ClearBad,
    ClearAll,
    ClearInvisible,

    Taunt,
    Revive,
    Goddess,

    Max,

    // "Sub" types
    Hide,
    StoreMode,
    IntroChat,
    AruaFairy,
    Invincible,
};

} // namespace Common
} // namespace Rose

#endif // STATUS_EFFECT_H
