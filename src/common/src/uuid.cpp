#include "rose/util/uuid.h"

#ifdef WIN32
    #include "Objbase.h"
#endif

#include <iomanip>
#include <sstream>

namespace Rose::Util {

UUID::UUID(): data({0}) {}

bool
UUID::is_nil() {
    for (const uint8_t& b: this->data) {
        if (b > 0) {
            return false;
        }
    }
    return true;
}

#ifdef WIN32
UUID
UUID::generate() {
    GUID guid;
    if (S_OK != CoCreateGuid(&guid)) {
        return UUID();
    }

    UUID uuid;
    uuid.data[0] = static_cast<uint8_t>(guid.Data1 >> 24);
    uuid.data[1] = static_cast<uint8_t>(guid.Data1 >> 16);
    uuid.data[2] = static_cast<uint8_t>(guid.Data1 >> 8);
    uuid.data[3] = static_cast<uint8_t>(guid.Data1 >> 0);

    uuid.data[4] = static_cast<uint8_t>(guid.Data2 >> 8);
    uuid.data[5] = static_cast<uint8_t>(guid.Data2 >> 0);

    uuid.data[6] = static_cast<uint8_t>(guid.Data3 >> 8);
    uuid.data[7] = static_cast<uint8_t>(guid.Data3 >> 0);

    for (size_t i = 0; i < 8; ++i) {
        uuid.data[i + 8] = guid.Data4[i];
    }
    return uuid;
}
#endif

std::string
UUID::to_string() const {
    std::stringstream stream;

    for (const uint8_t& b: this->data) {
        stream << std::hex << ((b >> 4) & 0xF);
        stream << std::hex << ((b & 0xF));
    }

    std::string s(stream.str());
    const std::array<size_t, 4> hyphen_pos = {8, 13, 18, 23};
    for (const size_t pos: hyphen_pos) {
        s.insert(pos, "-");
    }
    return s;
}

UUID
UUID::from_string(const std::string& uuid_str) {
    UUID u;

    if (uuid_str.size() != 36) {
        return u;
    }

    std::string s(uuid_str);
    s.erase(std::remove(s.begin(), s.end(), '-'), s.end());

    for (size_t i = 0; i < 32; i += 2) {
        uint8_t c1 = s[i];
        if (c1 >= '0' && c1 <= '9') {
            c1 = c1 - '0';
        } else if (c1 >= 'a' && c1 <= 'f') {
            c1 = c1 - 'a' + 10;
        } else if (c1 >= 'A' && c1 <= 'A') {
            c1 = c1 - 'A' + 10;
        } else {
            return u;
        }

        uint8_t c2 = s[i + 1];
        if (c2 >= '0' && c2 <= '9') {
            c2 = c2 - '0';
        } else if (c2 >= 'a' && c2 <= 'f') {
            c2 = c2 - 'a' + 10;
        } else if (c2 >= 'A' && c2 <= 'A') {
            c2 = c2 - 'A' + 10;
        } else {
            return u;
        }

        uint8_t byte = 0;
        byte |= (c1 & 0xF) << 4;
        byte |= (c2 & 0xF);
        u.data[i / 2] = byte;
    }
    return u;
}

} // namespace Rose::Util