#pragma once

#include <type_traits>

namespace Rose::Common {

enum class PvpState {
	NoPvp = 0,
	AllExceptClan = 1,
	AllExceptParty = 2,
	All = 3,
	ClanZone = 11,
};

template<typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type>
PvpState
pvp_state_from(T i) {
    switch (i) {
        case 1:
            return PvpState::AllExceptClan;
        case 2:
            return PvpState::AllExceptParty;
        case 3:
            return PvpState::All;
        case 11:
            return PvpState::ClanZone;
        case 0:
        default:
            return PvpState::NoPvp;
    }
}

} // namespace Rose::Common