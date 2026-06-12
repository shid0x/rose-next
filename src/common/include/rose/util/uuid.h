#pragma once

#include <array>
#include <string>

namespace Rose::Util {

// Universally Unique Identifier
class UUID {
public:
    // UUID bytes stored as variation 1 UUID
    std::array<uint8_t, 16> data;

public:
    UUID();

    bool is_nil();

    static UUID generate();

    std::string to_string() const;
    static UUID from_string(const std::string& s);
};

}

