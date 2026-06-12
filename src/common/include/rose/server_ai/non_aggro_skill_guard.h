#pragma once

namespace Rose::ServerAI {

enum SkillTargetFilter {
    SkillTargetFilterEnemyAll = 5,
    SkillTargetFilterEnemyPc = 6,
    SkillTargetFilterAllPc = 7,
    SkillTargetFilterAllChar = 8,
    SkillTargetFilterEnemyMob = 10,
};

enum SkillType {
    SkillTypeAttackMotion = 3,
    SkillTypeWeaponStateAttack = 4,
    SkillTypeBulletAttack = 5,
    SkillTypeProjectileMagic = 6,
    SkillTypeAreaDamage = 7,
    SkillTypeSelfAndDamage = 17,
    SkillTypeSelfAndTarget = 19,
};

inline bool
is_damage_skill_type(int skill_type) {
    return skill_type == SkillTypeAttackMotion
        || skill_type == SkillTypeWeaponStateAttack
        || skill_type == SkillTypeBulletAttack
        || skill_type == SkillTypeProjectileMagic
        || skill_type == SkillTypeAreaDamage
        || skill_type == SkillTypeSelfAndDamage
        || skill_type == SkillTypeSelfAndTarget;
}

inline bool
is_hostile_script_skill(bool target_is_allied,
    int skill_harm,
    int skill_class_filter,
    int skill_type) {
    if (target_is_allied) {
        return false;
    }

    if (skill_harm != 0) {
        return true;
    }

    switch (skill_class_filter) {
        case SkillTargetFilterEnemyAll:
        case SkillTargetFilterEnemyPc:
        case SkillTargetFilterAllPc:
        case SkillTargetFilterAllChar:
        case SkillTargetFilterEnemyMob:
            return true;
        default:
            break;
    }

    return is_damage_skill_type(skill_type);
}

inline bool
should_block_non_aggro_script_skill(bool target_is_allied,
    bool target_is_current_target,
    int skill_harm,
    int skill_class_filter,
    int skill_type) {
    return !target_is_current_target
        && is_hostile_script_skill(target_is_allied, skill_harm, skill_class_filter, skill_type);
}

} // namespace Rose::ServerAI
