#include "stdafx.h"
#include "network.h"

#include "gs_threadzone.h"

#include "rose/network/network_util.h"
#include "rose/network/packets/char_move_generated.h"
#include "rose/network/packets/char_move_attack_generated.h"
#include "rose/network/packets/combat_generated.h"
#include "rose/network/packets/update_stats_generated.h"

namespace Rose::Network {

bool
send_packet(classUSER& user, Packet& packet) {
    classPACKET p;
    p.m_HEADER.m_nSize = 2;
    p.AppendData(packet.raw_packet_data(), packet.raw_packet_data_size());
    return user.SendPacket(&p);
}

bool
send_packet_party(CParty& party, Packet& packet) {
    bool sent_to_all = true;
    for (size_t idx = 0; idx < party.GetMemberCNT(); ++idx) {
        tagPartyUSER* party_user = party.m_pPartyUSERs[idx];
        if (!party_user || !party_user->m_pUSER) {
            continue;
        }

        if (!send_packet(*party_user->m_pUSER, packet)) {
            sent_to_all = false;
        }
    }
    return sent_to_all;
}

bool
send_packet_nearby(CGameOBJ& object, Packet& packet) {
    int center_x = object.m_PosSECTOR.x;
    int center_y = object.m_PosSECTOR.y;

    int x = 0;
    int y = 0;

    CZoneTHREAD* zone = object.GetZONE();
    if (!zone) {
        return false;
    }

    bool sent_to_all = true;
    for (y = center_y - 1; y <= center_y + 1; ++y) {
        if (y < 0 || y >= zone->Get_SectorYCNT()) {
            continue;
        }

        for (x = center_x - 1; x <= center_x + 1; ++x) {
            if (x < 0 || x >= zone->Get_SectorXCNT()) {
                continue;
            }

            CZoneSECTOR* sector = zone->GetSECTOR(x, y);
            if (!sector) {
                continue;
            }

            for (auto obj: sector->objects()) {
                if (!obj || !obj->IsUSER()) {
                    continue;
                }

                classUSER* user = dynamic_cast<classUSER*>(obj);
                if (!user) {
                    continue;
                }

                if (!send_packet(*user, packet)) {
                    sent_to_all = false;
                }
            }
        }
    }

    return sent_to_all;
}

Packet
build_char_move_packet(CObjCHAR& character) {
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);

    const Packets::Vec3 target_pos(character.m_PosGOTO.x, character.m_PosGOTO.y, 0);
    const Packets::CharacterMoveMode move_mode = move_mode_from_int(character.Get_MoveMODE());

    Packets::CharacterMoveBuilder char_move_builder(builder);
    char_move_builder.add_character_id(character.Get_INDEX());
    char_move_builder.add_target_id(character.Get_TargetIDX());
    char_move_builder.add_target_pos(&target_pos);
    char_move_builder.add_target_distance(::distance(character.m_PosCUR, character.m_PosGOTO));
    char_move_builder.add_move_speed(character.total_move_speed());
    char_move_builder.add_move_mode(move_mode);
    const auto pak = char_move_builder.Finish();

    return build_packet_from_offset(builder, pak, Packets::PacketType::CharacterMove);
}

Packet
build_char_move_attack_packet(CObjCHAR& character) {
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);

    const Packets::Vec3 target_pos(character.m_PosGOTO.x, character.m_PosGOTO.y, 0);
    const Packets::CharacterMoveMode move_mode = move_mode_from_int(character.Get_MoveMODE());

    Packets::CharacterMoveBuilder char_move_builder(builder);
    char_move_builder.add_character_id(character.Get_INDEX());
    char_move_builder.add_target_id(character.Get_TargetIDX());
    char_move_builder.add_target_pos(&target_pos);
    char_move_builder.add_target_distance(::distance(character.m_PosCUR, character.m_PosGOTO));
    char_move_builder.add_move_speed(character.total_move_speed());
    char_move_builder.add_move_mode(move_mode);
    const auto pak = char_move_builder.Finish();

    return build_packet_from_offset(builder, pak, Packets::PacketType::CharacterMoveAttack);
}

namespace {
flatbuffers::Offset<Packets::DamageEvent>
build_damage_event(flatbuffers::FlatBufferBuilder& builder,
    CObjCHAR& attacker,
    CObjCHAR& defender,
    uniDAMAGE damage,
    uint32_t event_id,
    uint32_t defender_seq,
    Packets::DamagePresentationKind presentation_kind,
    int32_t skill_id = 0) {
    Packets::DamageEventBuilder damage_builder(builder);
    damage_builder.add_event_id(event_id);
    damage_builder.add_defender_seq(defender_seq);
    damage_builder.add_attacker_id(attacker.Get_INDEX());
    damage_builder.add_defender_id(defender.Get_INDEX());
    damage_builder.add_raw_damage(damage.m_wDamage);
    damage_builder.add_damage_value(damage.m_wVALUE);
    damage_builder.add_hp_after(defender.Get_HP());
    damage_builder.add_presentation_kind(presentation_kind);
    damage_builder.add_lethal((damage.m_wACTION & DMG_ACT_DEAD) != 0);
    damage_builder.add_skill_id(skill_id);
    return damage_builder.Finish();
}

Packets::DamagePresentationKind
presentation_kind_for_normal_attack(CObjCHAR& attacker) {
    if (attacker.UsesProjectileAttackPresentation()) {
        return Packets::DamagePresentationKind::ProjectileImpact;
    }

    return Packets::DamagePresentationKind::MeleeHitFrame;
}
}

Packet
build_combat_swing_packet(CObjCHAR& attacker,
    CObjCHAR& defender,
    uniDAMAGE damage,
    uint32_t event_id,
    uint32_t defender_seq) {
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);

    const Packets::Vec3 target_pos(attacker.m_PosGOTO.x, attacker.m_PosGOTO.y, 0);
    const Packets::CharacterMoveMode move_mode = move_mode_from_int(attacker.Get_MoveMODE());
    const auto damage_event = build_damage_event(builder,
        attacker,
        defender,
        damage,
        event_id,
        defender_seq,
        presentation_kind_for_normal_attack(attacker));

    Packets::CombatSwingBuilder swing_builder(builder);
    swing_builder.add_attacker_id(attacker.Get_INDEX());
    swing_builder.add_defender_id(defender.Get_INDEX());
    swing_builder.add_target_distance((uint16_t)::distance(attacker.m_PosCUR, attacker.m_PosGOTO));
    swing_builder.add_target_pos(&target_pos);
    swing_builder.add_move_mode(move_mode);
    swing_builder.add_damage(damage_event);
    const auto swing = swing_builder.Finish();

    return build_packet_from_offset(builder, swing, Packets::PacketType::CombatSwing);
}

Packet
build_damage_event_packet(CObjCHAR& attacker,
    CObjCHAR& defender,
    uniDAMAGE damage,
    uint32_t event_id,
    uint32_t defender_seq,
    Packets::DamagePresentationKind presentation_kind,
    int32_t skill_id) {
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);

    const auto damage_event = build_damage_event(builder,
        attacker,
        defender,
        damage,
        event_id,
        defender_seq,
        presentation_kind,
        skill_id);
    return build_packet_from_offset(builder, damage_event, Packets::PacketType::DamageEvent);
}
    /*
bool
send_server_whisper(classUSER& user, const std::string& message) {
    return user.Send_gsv_WHISPER("Server", const_cast<char*>(message.c_str()));
}

bool
send_update_stats_all(classUSER& user) {
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);

    Packets::StatsBuilder stats_builder(builder);
    stats_builder.add_move_speed(user.total_move_speed());
    stats_builder.add_attack_speed(user.total_attack_speed());
    stats_builder.add_attack_power(user.total_attack_power());
    stats_builder.add_hit_rate(user.total_hit_rate());
    const auto stats = stats_builder.Finish();

    Packets::UpdateStatsBuilder rep(builder);
    rep.add_target_id(user.Get_INDEX());
    rep.add_stats(stats);
    const auto pak = rep.Finish();

    return user.send_packet_from_offset(builder, pak, Packets::PacketType::UpdateStats);
}
*/
Packet
build_update_hpmp_packet(classUSER& user, uint32_t hp, uint32_t mp) {
    return build_update_hpmp_packet(user, hp, mp, true, true);
}

Packet
build_update_hpmp_packet(classUSER& user,
    uint32_t hp,
    uint32_t mp,
    bool include_hp,
    bool include_mp) {
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);

    Packets::StatsBuilder stats_builder(builder);
    if (include_hp) {
        stats_builder.add_hp(hp);
    }
    if (include_mp) {
        stats_builder.add_mp(mp);
    }
    const auto stats = stats_builder.Finish();

    Packets::UpdateStatsBuilder rep(builder);
    rep.add_character_id(user.Get_INDEX());
    rep.add_stats(stats);
    const auto pak = rep.Finish();

    return build_packet_from_offset(builder, pak, Packets::PacketType::UpdateStats);
}
/*
bool
send_update_move_speed(classUSER& user, uint16_t move_speed) {
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);

    Packets::StatsBuilder stats_builder(builder);
    stats_builder.add_move_speed(move_speed);
    const auto stats = stats_builder.Finish();

    Packets::UpdateStatsBuilder rep(builder);
    rep.add_target_id(user.Get_INDEX());
    rep.add_stats(stats);
    const auto pak = rep.Finish();

    return user.send_packet_from_offset(builder, pak, Packets::PacketType::UpdateStats);
}
*/
} // namespace Rose::Network
