#include "rose/common/status_effect/goddess_effect.h"

#include <algorithm>

using namespace Rose::Common;

const short GODDESS_MOVE_VALS[10] = {100, 110, 120, 130, 140, 150, 160, 170, 180, 200};
const short GODDESS_ATTACK_VALS[10] = {25, 30, 35, 40, 45, 50, 55, 60, 65, 70};
const short GODDESS_HIT_VALS[10] = {30, 35, 40, 45, 50, 55, 60, 65, 70, 80};
const short GODDESS_ASPEED_VALS[10] = {18, 20, 22, 24, 26, 28, 30, 32, 34, 36};
const short GODDESS_CRIT_VALS[10] = {40, 45, 50, 55, 60, 65, 70, 75, 80, 90};

GoddessEffect::GoddessEffect():
    move_speed(0),
    attack_power(0),
    hit(0),
    crit(0),
    attack_speed(0),
    additional_damage(0) {}

void
GoddessEffect::update(int level) {
    // The GoddessEffect works similarly to player buffs in that it scales off
    // some factor. Instead of scaling off the players int, we use their level
    // to generate a factor in the similar range of int (i.e. 15 -> 300 or
    // min_int -> max_int).
    //
    // This formula was created by calculating the line that intersects (1, 15)
    // and (100, 300) aka. (min_level, min_int) and (max_level, max_int)
    const float simulated_int = ((2.87f * level) + 12.12f);

    // Convert our factor into the range (1,2) where maximum factor
    // doubles the scale of the effect. I.e. At max level the buffs are
    // twice as strong.
    const float scalar = (simulated_int + 300.0f) / 315.0f;

    auto get_index = [](int level, int upper) {
        const int bounded = std::min(level, upper) - 1;
        return bounded % 10;
    };

    if (level >= 1) {
        const int idx = std::min(level, 10) - 1;
        const float val = GODDESS_MOVE_VALS[idx] * scalar;
        this->move_speed = static_cast<short>(val);
    }
    if (level >= 11) {
        const int idx = get_index(level, 20);
        float val = GODDESS_ATTACK_VALS[idx] * scalar;
        this->attack_power = static_cast<short>(val);
    }
    if (level >= 21) {
        const int idx = get_index(level, 30);
        const float val = GODDESS_HIT_VALS[idx] * scalar;
        this->hit = static_cast<short>(val);
    }
    if (level >= 31) {
        const int idx = get_index(level, 40);
        const float val = GODDESS_ASPEED_VALS[idx] * scalar;
        this->attack_speed = static_cast<short>(val);
    }
    if (level >= 41) {
        const int idx = get_index(level, 50);
        const float val = GODDESS_CRIT_VALS[idx] * scalar;
        this->crit = static_cast<short>(val);
    }
}