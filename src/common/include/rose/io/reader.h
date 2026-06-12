#pragma once

#include <filesystem>

namespace Rose::IO {

class BinaryReader {
public:
    std::vector<std::byte> buffer;
    size_t pos;

public:
    BinaryReader();
    ~BinaryReader() = default;

    bool open(const std::filesystem::path& path);
    bool open(std::vector<std::byte>&& buffer);

    size_t tell() const;
    bool seek(size_t to);

    std::vector<std::byte> read_bytes(size_t count);

    std::string read_string(size_t count);
    std::string read_string_u16();

    uint8_t read_uint8();
    uint16_t read_uint16();
    uint32_t read_uint32();
    uint64_t read_uint64();

    int8_t read_int8();
    int16_t read_int16();
    int32_t read_int32();
    int64_t read_int64();

    float read_f32();
    double read_f64();

    bool read_bool();
    bool read_bool16();

    template<typename T>
    T read() {
        auto begin = this->buffer.begin() + this->pos;
        auto end = begin + sizeof(T);

        if (std::distance(begin, end) != sizeof(T)) {
            this->pos = this->buffer.size();
            return T {};
        }

        T* ptr = reinterpret_cast<T*>(this->buffer.data() + this->pos);
        this->pos += sizeof(T);
        return *ptr;
    };
};

} // namespace Rose::IO