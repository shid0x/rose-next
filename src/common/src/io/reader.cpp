#include "rose/io/reader.h"

#include <algorithm>
#include <fstream>

namespace Rose::IO {

BinaryReader::BinaryReader(): pos(0) {}

bool
BinaryReader::open(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return false;
    }

    const size_t file_size = std::filesystem::file_size(path);
    this->buffer.resize(file_size);
    this->pos = 0;

    std::ifstream is(path, std::ios::binary);
    is.read(reinterpret_cast<char*>(this->buffer.data()), file_size);
    return is.good() && is.gcount() == file_size;
}

bool
BinaryReader::open(std::vector<std::byte>&& buffer) {
    this->buffer = std::move(buffer);
    this->pos = 0;
    return true;
}

size_t
BinaryReader::tell() const {
    return this->pos;
}

bool
BinaryReader::seek(size_t to) {
    if (to >= this->buffer.size()) {
        this->pos = this->buffer.size();
        return false;
    }
    this->pos = to;
    return true;
}

std::vector<std::byte>
BinaryReader::read_bytes(size_t count) {
    auto begin = this->buffer.begin() + this->pos;
    auto end = begin + count;
    this->pos += std::distance(begin, end);
    return std::vector<std::byte>(begin, end);
}

std::string
BinaryReader::read_string(size_t length) {
    auto begin = this->buffer.begin() + this->pos;
    auto end = begin + length;

    if (std::distance(begin, end) != length) {
        return "";
    }

    char* ptr = reinterpret_cast<char*>(this->buffer.data() + this->pos);
    this->pos += length;
    return std::string(ptr, length);
}

std::string
BinaryReader::read_string_u16() {
    uint16_t length = this->read_uint16();
    return this->read_string(length);
}

uint8_t
BinaryReader::read_uint8() {
    return this->read<uint8_t>();
}

uint16_t
BinaryReader::read_uint16() {
    return this->read<uint16_t>();
}

uint32_t
BinaryReader::read_uint32() {
    return this->read<uint32_t>();
}

uint64_t
BinaryReader::read_uint64() {
    return this->read<uint64_t>();
}

int8_t
BinaryReader::read_int8() {
    return this->read<int8_t>();
}

int16_t
BinaryReader::read_int16() {
    return this->read<int16_t>();
}

int32_t
BinaryReader::read_int32() {
    return this->read<int32_t>();
}

int64_t
BinaryReader::read_int64() {
    return this->read<int64_t>();
}

float
BinaryReader::read_f32() {
    return this->read<float>();
}

double
BinaryReader::read_f64() {
    return this->read<double>();
}

bool
BinaryReader::read_bool() {
    return this->read<uint8_t>() != 0;
}

bool
BinaryReader::read_bool16() {
    return this->read<uint16_t>() != 0;
}

} // namespace Rose::IO