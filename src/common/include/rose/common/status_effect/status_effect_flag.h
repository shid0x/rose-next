#ifndef STATUS_EFFECT_FLAG_H
#define STATUS_EFFECT_FLAG_H
#pragma once

#include <cstdint>

namespace Rose {
namespace Common {

enum class StatusEffectFlag : uint32_t {
    Null = 0,

    IncreaseHP = 1 << 0,
    IncreaseMP = 1 << 1,
    Poisoned = 1 << 2,

    IncreaseMaxHP = 1 << 4,
    IncreaseMaxMP = 1 << 5,
    IncreaseMoveSpeed = 1 << 6,
    DecreaseMoveSpeed = 1 << 7,
    IncreaseAttackSpeed = 1 << 8,
    DecreaseAttackSpeed = 1 << 9,
    IncreaseAttackPower = 1 << 10,
    DecreaseAttackPower = 1 << 11,
    IncreaseDefense = 1 << 12,
    DecreaseDefense = 1 << 13,
    IncreaseMagicRes = 1 << 14,
    DecreaseMagicRes = 1 << 15,
    IncreaseHit = 1 << 16,
    DecreaseHit = 1 << 17,
    IncreaseCrit = 1 << 18,
    DecreaseCrit = 1 << 19,
    IncreaseDodge = 1 << 20,
    DecreaseDodge = 1 << 21,
    Dumb = 1 << 22,
    Sleep = 1 << 23,
    Faint = 1 << 24,
    Disguise = 1 << 25,
    Invisible = 1 << 26,
    ShieldDamage = 1 << 27,
    AdditionalDamage = 1 << 28,
    DecreaseLifetime = 1 << 29,
    Revive = 1 << 30,
    Taunt = 1U << 31,

    IgnoreAll = Sleep | Faint,

    ProcPerTick = DecreaseLifetime | Poisoned,
    CheckPerTick = 0xfffffff0,

    Bad = Poisoned | DecreaseMoveSpeed | DecreaseAttackSpeed | DecreaseAttackPower | DecreaseDefense
        | DecreaseMagicRes | DecreaseHit | DecreaseCrit | DecreaseDodge | Dumb | Sleep | Faint,

    Good = IncreaseMaxHP | IncreaseMaxMP | IncreaseMoveSpeed | IncreaseAttackSpeed
        | IncreaseAttackPower | IncreaseDefense | IncreaseMagicRes | IncreaseHit | IncreaseCrit
        | IncreaseDodge | ShieldDamage | AdditionalDamage | Disguise | Invisible,

    // "Sub Flags"
    Hide = 1 << 0,
    StoreMode = 1 << 1,
    IntroChat = 1 << 2,
    Goddess = 1 << 3,
    AruaFairy = 1 << 30,
    Invincible = 1 << 31,

    Clear = DecreaseLifetime | StoreMode | IntroChat,
};

inline constexpr StatusEffectFlag
operator&(StatusEffectFlag x, StatusEffectFlag y) {
    return static_cast<StatusEffectFlag>(static_cast<uint64_t>(x) & static_cast<uint64_t>(y));
}

inline constexpr StatusEffectFlag
operator|(StatusEffectFlag x, StatusEffectFlag y) {
    return static_cast<StatusEffectFlag>(static_cast<uint64_t>(x) | static_cast<uint64_t>(y));
}

inline constexpr StatusEffectFlag
operator^(StatusEffectFlag x, StatusEffectFlag y) {
    return static_cast<StatusEffectFlag>(static_cast<uint64_t>(x) ^ static_cast<uint64_t>(y));
}

inline constexpr StatusEffectFlag
operator~(StatusEffectFlag x) {
    return static_cast<StatusEffectFlag>(~static_cast<uint64_t>(x));
}

inline StatusEffectFlag&
operator&=(StatusEffectFlag& x, StatusEffectFlag y) {
    x = x & y;
    return x;
}

inline StatusEffectFlag&
operator|=(StatusEffectFlag& x, StatusEffectFlag y) {
    x = x | y;
    return x;
}

inline StatusEffectFlag&
operator^=(StatusEffectFlag& x, StatusEffectFlag y) {
    x = x ^ y;
    return x;
}

} // namespace Common
} // namespace Rose

#endif // STATUS_EFFECT_FLAG_H