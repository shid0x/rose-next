#pragma once

namespace Rose::Combat {

inline bool
is_projectile_presented_skill(int skill_type, int bullet_no) {
    if (skill_type == 5 || skill_type == 6) {
        return true;
    }

    if (bullet_no <= 0) {
        return false;
    }

    return skill_type == 3 || skill_type == 19;
}

} // namespace Rose::Combat
