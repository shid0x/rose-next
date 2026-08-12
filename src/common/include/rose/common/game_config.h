#pragma once

namespace Rose {

namespace GameStaticConfig {
const char* const NAME = "ROSE Next";

constexpr uint8_t MAX_LEVEL = 240;
constexpr uint16_t MAX_STAT = 300;
constexpr uint16_t MAX_STAMINA = 5000;
constexpr uint32_t MAX_DAMAGE = 9999;

constexpr int MAX_CHARACTER_NAME = 20;
constexpr int MAX_CHARACTERS = 5;

constexpr int MAX_MAIL_MESSAGES = 50;

constexpr int DELETE_TIME_SEC = 3600; // 1-hour

constexpr int DEFAULT_WALK_SPEED = 200;

constexpr int MAX_DROP_MULTIPLIER = 10;
constexpr int MAX_CONSUMABLE_DROP_MULTIPLIER = 1;

constexpr float CAMERA_MIN_ZOOM = 300.0f;
constexpr float CAMERA_MAX_ZOOM = 2700.0f;

constexpr int RECOVERY_RATE_TIME = 2000;
constexpr int RECOVERY_RATE_SITTING = 100;

constexpr int FUEL_DECREASE_TIME = 10'000;

}; // namespace GameStaticConfig

namespace Common {

struct GameConfig {
    uint32_t base_attack_power;
    uint32_t base_attack_speed;
    uint32_t base_move_speed;
    uint32_t base_hit_rate;
    uint32_t base_crit;

    GameConfig():
        base_attack_power(0),
        base_attack_speed(0),
        base_move_speed(0),
        base_hit_rate(0),
        base_crit(0) {}
};

} // namespace Common
}; // namespace Rose
