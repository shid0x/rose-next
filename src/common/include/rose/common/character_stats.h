#pragma once

namespace Rose::Common {
struct CharacterStats {
    uint8_t level;
    uint32_t hp;
    uint32_t mp;
    uint32_t exp;
    uint16_t stamina;

    uint16_t strength;
    uint16_t dexterity;
    uint16_t intelligence;
    uint16_t concentration;
    uint16_t charm;
    uint16_t sensitivity;

    uint16_t move_speed;
    uint16_t attack_speed;

    uint32_t attack_power;
    uint32_t hit_rate;
    uint16_t crit;

    uint16_t defense;
    uint16_t magic_resistance;
    uint16_t dodge;
};
} // namespace Rose::Common