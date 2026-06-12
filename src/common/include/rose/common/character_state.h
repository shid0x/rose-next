#pragma once

namespace Rose::Common {
enum class CharacterMoveMode {
    Walk,
    Run,
    Drive,
    Ride,
};

struct CharacterState {
    CharacterMoveMode move_mode;
};

} // namespace Rose::Common