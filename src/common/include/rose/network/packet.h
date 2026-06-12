#pragma once

#include "flatbuffers/flatbuffers.h"
#include "rose/network/packets/packet_data_generated.h"

namespace Rose {
namespace Network {

class Packet {
public:
    static const uint32_t SIZE_OFFSET = 0;
    static const uint32_t DATA_OFFSET = 2;

    std::vector<uint8_t> buffer;

public:
    Packet(uint8_t* buf, uint16_t size): buffer(std::vector<uint8_t>(buf, buf + size)) {}

    Packet(const flatbuffers::FlatBufferBuilder& builder):
        buffer(std::vector<uint8_t>(builder.GetSize() + DATA_OFFSET)) {
        this->set_raw_packet_data(builder.GetBufferPointer(), builder.GetSize());
    }

    void set_raw_packet_data(uint8_t* data, uint16_t size) {
        uint16_t s = size + DATA_OFFSET;
        this->buffer[0] = uint8_t(s);
        this->buffer[1] = uint8_t(s >> 8);

        size_t i = DATA_OFFSET;
        for (uint8_t* d = data; d < (data + size); ++d) {
            this->buffer[i] = *d;
            ++i;
        }
    }

    uint8_t* raw_packet_data() { return this->buffer.data() + DATA_OFFSET; }

    uint32_t raw_packet_data_size() const { return this->buffer.size() - DATA_OFFSET; }

    const Packets::PacketData* packet_data() {
        return Packets::GetPacketData(this->raw_packet_data());
    }
};

} // namespace Network
} // namespace Rose