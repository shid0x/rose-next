#pragma once

#include "rose/network/packets/char_move_generated.h"

namespace Rose::Network {

constexpr Packets::CharacterMoveMode
move_mode_from_int(int i, Packets::CharacterMoveMode default = Packets::CharacterMoveMode::Run) {
    switch (i) {
        case 0:
            return Packets::CharacterMoveMode::Walk;
        case 1:
            return Packets::CharacterMoveMode::Run;
        case 2:
            return Packets::CharacterMoveMode::Ride;
        case 4:
            return Packets::CharacterMoveMode::Drive;
        default:
            return default;
    }
    return default;
}
} // namespace Rose::Network