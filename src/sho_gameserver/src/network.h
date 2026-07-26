#pragma once

#include "rose/network/packets/combat_generated.h"

// Forward declarations
namespace Rose::Network {
class Packet;
}

class classUSER;

namespace Rose::Network {

bool send_packet(classUSER& user, Packet& packet);
bool send_packet_party(CParty& party, Packet& packet);
bool send_packet_nearby(CGameOBJ& object, Packet& packet);

template<typename T>
Packet
build_packet_from_offset(flatbuffers::FlatBufferBuilder& builder,
    flatbuffers::Offset<T> offset,
    Packets::PacketType type) {

    Packets::PacketDataBuilder pd(builder);
    pd.add_data_type(type);
    pd.add_data(offset.Union());
    builder.Finish(pd.Finish());

    Packet p(builder);
    return p;
}

Packet build_char_move_packet(CObjCHAR& character);
Packet build_char_move_attack_packet(CObjCHAR& character);
// skill_id / source_attacker_id are display metadata for the client damage
// meter (DoT caster, summon owner); presentation logic must not branch on
// them. 0 = unknown / same as attacker.
Packet build_combat_swing_packet(CObjCHAR& attacker,
    CObjCHAR& defender,
    uniDAMAGE damage,
    uint32_t event_id,
    uint32_t defender_seq,
    uint32_t source_attacker_id = 0);
Packet build_damage_event_packet(CObjCHAR& attacker,
    CObjCHAR& defender,
    uniDAMAGE damage,
    uint32_t event_id,
    uint32_t defender_seq,
    Packets::DamagePresentationKind presentation_kind,
    int32_t skill_id = 0,
    uint32_t source_attacker_id = 0);

//Packet build_update_stats_all_packet(classUSER& user);
Packet build_update_hpmp_packet(classUSER& user, uint32_t hp, uint32_t mp);
// Variant that lets the caller pick which of HP / MP to include. Useful so
// natural HP regen does not also clobber a client's pending bonfire heal
// MP delta that is still waiting to be applied at the casting animation's
// action frame. Pass `include_*` = false to omit the field; the client's
// recv_update_stats checks IsFieldPresent and skips absent fields.
Packet build_update_hpmp_packet(classUSER& user,
    uint32_t hp,
    uint32_t mp,
    bool include_hp,
    bool include_mp);
//Packet build_update_move_speed_packet(classUSER& user, uint16_t move_speed);

// Uses legacy packet building methods
//bool send_server_whisper(classUSER& user, const std::string& message);
} // namespace Rose::Network
