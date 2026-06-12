#ifndef STATUS_EFFECT_DATA_H
#define STATUS_EFFECT_DATA_H
#pragma once

#include "rose/common/status_effect/status_effect.h"
#include "rose/common/status_effect/status_effect_flag.h"

namespace Rose {
namespace Common {

/// Convet a StatusEffectFlag to a StatusEffectType
constexpr Rose::Common::StatusEffectFlag
set2sef(Rose::Common::StatusEffectType type) {
    switch (type) {
        case Rose::Common::StatusEffectType::Null:
            return Rose::Common::StatusEffectFlag::Null;

        case Rose::Common::StatusEffectType::IncreaseHP:
            return Rose::Common::StatusEffectFlag::IncreaseHP;
        case Rose::Common::StatusEffectType::IncreaseMP:
            return Rose::Common::StatusEffectFlag::IncreaseMP;
        case Rose::Common::StatusEffectType::Poisoned:
            return Rose::Common::StatusEffectFlag::Poisoned;
        case Rose::Common::StatusEffectType::IncreaseMaxHP:
            return Rose::Common::StatusEffectFlag::IncreaseMaxHP;
        case Rose::Common::StatusEffectType::IncreaseMaxMP:
            return Rose::Common::StatusEffectFlag::IncreaseMaxMP;

        case Rose::Common::StatusEffectType::IncreaseMoveSpeed:
            return Rose::Common::StatusEffectFlag::IncreaseMoveSpeed;
        case Rose::Common::StatusEffectType::DecreaseMoveSpeed:
            return Rose::Common::StatusEffectFlag::DecreaseMoveSpeed;
        case Rose::Common::StatusEffectType::IncreaseAttackSpeed:
            return Rose::Common::StatusEffectFlag::IncreaseAttackSpeed;
        case Rose::Common::StatusEffectType::DecreaseAttackSpeed:
            return Rose::Common::StatusEffectFlag::DecreaseAttackSpeed;
        case Rose::Common::StatusEffectType::IncreaseAttackPower:
            return Rose::Common::StatusEffectFlag::IncreaseAttackPower;
        case Rose::Common::StatusEffectType::DecreaseAttackPower:
            return Rose::Common::StatusEffectFlag::DecreaseAttackPower;
        case Rose::Common::StatusEffectType::IncreaseDefense:
            return Rose::Common::StatusEffectFlag::IncreaseDefense;
        case Rose::Common::StatusEffectType::DecreaseDefense:
            return Rose::Common::StatusEffectFlag::DecreaseDefense;
        case Rose::Common::StatusEffectType::IncreaseMagicRes:
            return Rose::Common::StatusEffectFlag::IncreaseMagicRes;
        case Rose::Common::StatusEffectType::DecreaseMagicRes:
            return Rose::Common::StatusEffectFlag::DecreaseMagicRes;
        case Rose::Common::StatusEffectType::IncreaseHit:
            return Rose::Common::StatusEffectFlag::IncreaseHit;
        case Rose::Common::StatusEffectType::DecreaseHit:
            return Rose::Common::StatusEffectFlag::DecreaseHit;
        case Rose::Common::StatusEffectType::IncreaseCrit:
            return Rose::Common::StatusEffectFlag::IncreaseCrit;
        case Rose::Common::StatusEffectType::DecreaseCrit:
            return Rose::Common::StatusEffectFlag::DecreaseCrit;
        case Rose::Common::StatusEffectType::IncreaseDodge:
            return Rose::Common::StatusEffectFlag::IncreaseDodge;
        case Rose::Common::StatusEffectType::DecreaseDodge:
            return Rose::Common::StatusEffectFlag::DecreaseDodge;
        case Rose::Common::StatusEffectType::Dumb:
            return Rose::Common::StatusEffectFlag::Dumb;
        case Rose::Common::StatusEffectType::Sleep:
            return Rose::Common::StatusEffectFlag::Sleep;
        case Rose::Common::StatusEffectType::Faint:
            return Rose::Common::StatusEffectFlag::Faint;
        case Rose::Common::StatusEffectType::Disguise:
            return Rose::Common::StatusEffectFlag::Disguise;
        case Rose::Common::StatusEffectType::Invisible:
            return Rose::Common::StatusEffectFlag::Invisible;
        case Rose::Common::StatusEffectType::ShieldDamage:
            return Rose::Common::StatusEffectFlag::ShieldDamage;
        case Rose::Common::StatusEffectType::AdditionalDamage:
            return Rose::Common::StatusEffectFlag::AdditionalDamage;

        case Rose::Common::StatusEffectType::DecreaseLifetime:
            return Rose::Common::StatusEffectFlag::DecreaseLifetime;

        case Rose::Common::StatusEffectType::ClearGood:
            return Rose::Common::StatusEffectFlag::Null;
        case Rose::Common::StatusEffectType::ClearBad:
            return Rose::Common::StatusEffectFlag::Null;
        case Rose::Common::StatusEffectType::ClearAll:
            return Rose::Common::StatusEffectFlag::Null;
        case Rose::Common::StatusEffectType::ClearInvisible:
            return Rose::Common::StatusEffectFlag::Null;

        case Rose::Common::StatusEffectType::Taunt:
            return Rose::Common::StatusEffectFlag::Taunt;
        case Rose::Common::StatusEffectType::Revive:
            return Rose::Common::StatusEffectFlag::Revive;
        case Rose::Common::StatusEffectType::Goddess:
            return Rose::Common::StatusEffectFlag::Goddess;

        case Rose::Common::StatusEffectType::Hide:
            Rose::Common::StatusEffectFlag::Hide;
        case Rose::Common::StatusEffectType::StoreMode:
            Rose::Common::StatusEffectFlag::StoreMode;
        case Rose::Common::StatusEffectType::IntroChat:
            Rose::Common::StatusEffectFlag::IntroChat;
        case Rose::Common::StatusEffectType::AruaFairy:
            Rose::Common::StatusEffectFlag::AruaFairy;
        case Rose::Common::StatusEffectType::Invincible:
            Rose::Common::StatusEffectFlag::Invincible;
        default:
            return Rose::Common::StatusEffectFlag::Null;
    }

    return Rose::Common::StatusEffectFlag::Null;
}

/// Convet a StatusEffectType to a StatusEffectFlag
constexpr Rose::Common::StatusEffectType
sef2set(Rose::Common::StatusEffectFlag flag) {
    // Handle subflags seperately since they are duplicate values
    switch (flag) {
        case Rose::Common::StatusEffectFlag::Hide:
            return Rose::Common::StatusEffectType::Hide;
        case Rose::Common::StatusEffectFlag::StoreMode:
            return Rose::Common::StatusEffectType::StoreMode;
        case Rose::Common::StatusEffectFlag::IntroChat:
            return Rose::Common::StatusEffectType::IntroChat;
        case Rose::Common::StatusEffectFlag::Goddess:
            return Rose::Common::StatusEffectType::Goddess;
        case Rose::Common::StatusEffectFlag::AruaFairy:
            return Rose::Common::StatusEffectType::AruaFairy;
        case Rose::Common::StatusEffectFlag::Invincible:
            return Rose::Common::StatusEffectType::Invincible;
    }

    switch (flag) {
        case Rose::Common::StatusEffectFlag::Null:
            return Rose::Common::StatusEffectType::Null;

        case Rose::Common::StatusEffectFlag::IncreaseHP:
            return Rose::Common::StatusEffectType::IncreaseHP;
        case Rose::Common::StatusEffectFlag::IncreaseMP:
            return Rose::Common::StatusEffectType::IncreaseMP;
        case Rose::Common::StatusEffectFlag::Poisoned:
            return Rose::Common::StatusEffectType::Poisoned;

        case Rose::Common::StatusEffectFlag::IncreaseMaxHP:
            return Rose::Common::StatusEffectType::IncreaseMaxHP;
        case Rose::Common::StatusEffectFlag::IncreaseMaxMP:
            return Rose::Common::StatusEffectType::IncreaseMaxMP;
        case Rose::Common::StatusEffectFlag::IncreaseMoveSpeed:
            return Rose::Common::StatusEffectType::IncreaseMoveSpeed;
        case Rose::Common::StatusEffectFlag::DecreaseMoveSpeed:
            return Rose::Common::StatusEffectType::DecreaseMoveSpeed;
        case Rose::Common::StatusEffectFlag::IncreaseAttackSpeed:
            return Rose::Common::StatusEffectType::IncreaseAttackSpeed;
        case Rose::Common::StatusEffectFlag::DecreaseAttackSpeed:
            return Rose::Common::StatusEffectType::DecreaseAttackSpeed;
        case Rose::Common::StatusEffectFlag::IncreaseAttackPower:
            return Rose::Common::StatusEffectType::IncreaseAttackPower;
        case Rose::Common::StatusEffectFlag::DecreaseAttackPower:
            return Rose::Common::StatusEffectType::DecreaseAttackPower;
        case Rose::Common::StatusEffectFlag::IncreaseDefense:
            return Rose::Common::StatusEffectType::IncreaseDefense;
        case Rose::Common::StatusEffectFlag::DecreaseDefense:
            return Rose::Common::StatusEffectType::DecreaseDefense;
        case Rose::Common::StatusEffectFlag::IncreaseMagicRes:
            return Rose::Common::StatusEffectType::IncreaseMagicRes;
        case Rose::Common::StatusEffectFlag::DecreaseMagicRes:
            return Rose::Common::StatusEffectType::DecreaseMagicRes;
        case Rose::Common::StatusEffectFlag::IncreaseHit:
            return Rose::Common::StatusEffectType::IncreaseHit;
        case Rose::Common::StatusEffectFlag::DecreaseHit:
            return Rose::Common::StatusEffectType::DecreaseHit;
        case Rose::Common::StatusEffectFlag::IncreaseCrit:
            return Rose::Common::StatusEffectType::IncreaseCrit;
        case Rose::Common::StatusEffectFlag::DecreaseCrit:
            return Rose::Common::StatusEffectType::DecreaseCrit;
        case Rose::Common::StatusEffectFlag::IncreaseDodge:
            return Rose::Common::StatusEffectType::IncreaseDodge;
        case Rose::Common::StatusEffectFlag::DecreaseDodge:
            return Rose::Common::StatusEffectType::DecreaseDodge;
        case Rose::Common::StatusEffectFlag::Dumb:
            return Rose::Common::StatusEffectType::Dumb;
        case Rose::Common::StatusEffectFlag::Sleep:
            return Rose::Common::StatusEffectType::Sleep;
        case Rose::Common::StatusEffectFlag::Faint:
            return Rose::Common::StatusEffectType::Faint;
        case Rose::Common::StatusEffectFlag::Disguise:
            return Rose::Common::StatusEffectType::Disguise;
        case Rose::Common::StatusEffectFlag::Invisible:
            return Rose::Common::StatusEffectType::Invisible;
        case Rose::Common::StatusEffectFlag::ShieldDamage:
            return Rose::Common::StatusEffectType::ShieldDamage;
        case Rose::Common::StatusEffectFlag::AdditionalDamage:
            return Rose::Common::StatusEffectType::AdditionalDamage;
        case Rose::Common::StatusEffectFlag::DecreaseLifetime:
            return Rose::Common::StatusEffectType::DecreaseLifetime;
        case Rose::Common::StatusEffectFlag::Revive:
            return Rose::Common::StatusEffectType::Revive;
        case Rose::Common::StatusEffectFlag::Taunt:
            return Rose::Common::StatusEffectType::Taunt;
        default:
            return Rose::Common::StatusEffectType::Null;
    }
}

} // namespace Common
} // namespace Rose

#endif // STATUS_EFFECT_DATA_H
