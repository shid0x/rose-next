#include "rose/io/reader.h"
#include "rose/io/stb.h"

#include <string>

static const std::string DEFAULT_VALUE = "";

using namespace Rose::IO;

STBDATA::STBDATA(): 
    row_count(0),
    col_count(0) {
}

bool
STBDATA::load(const std::filesystem::path& path) {
    BinaryReader reader;
    if (!reader.open(path)) {
        return false;
    }

    return this->load(std::move(reader));
}

bool
STBDATA::load(BinaryReader&& reader) {
    std::string identifier = reader.read_string(4);
    if (identifier != "STB1") {
        return false;
    }

    // Offset to data excluding first row and column
    auto offset = reader.read_uint32();

    // We ignore the first row and column so dont count them
    this->row_count = reader.read_uint32() - 1;
    this->col_count = reader.read_uint32() - 1;

    if (!reader.seek(offset)) {
        return false;
    }

    this->data.reserve(this->row_count * this->col_count);
    for (size_t row_idx = 0; row_idx < this->row_count; ++row_idx) {
        for (size_t col_idx = 0; col_idx < this->col_count; ++col_idx) {
            this->data.push_back(reader.read_string_u16());
        }
    }

    return true;
}

const std::string&
STBDATA::value(size_t row_idx, size_t col_idx) const {
    size_t idx = (row_idx * col_count) + col_idx;
    if (idx >= this->data.size()) {
        return DEFAULT_VALUE;
    }
    return this->data.at(idx);
}

bool
STBDATA::set_value(size_t row_idx, size_t col_idx, std::string_view val) {
    size_t idx = (row_idx * col_count) + col_idx;
    if (idx >= this->data.size()) {
        return false;
    }
    this->data[idx] = val;
    return true;
}

char*
STBDATA::get_cstr(size_t row_idx, size_t col_idx) {
    const std::string& val = this->value(row_idx, col_idx);
    if (val.empty()) {
        return nullptr;
    }

    size_t size = val.size();
    char* cstr = new char[size + 1];
    std::memcpy(cstr, val.c_str(), size);
    cstr[size] = '\0';
    return cstr;
}

int16_t
STBDATA::get_int16(size_t row_idx, size_t col_idx) {
    const std::string& val = this->value(row_idx, col_idx);
    if (val.empty()) {
        return 0;
    }
    return std::atoi(val.c_str());
}

int32_t
STBDATA::get_int32(size_t row_idx, size_t col_idx) {
    const std::string& val = this->value(row_idx, col_idx);
    if (val.empty()) {
        return 0;
    }
    return std::atoi(val.c_str());
}

bool
STBDATA::set_cstr(size_t row_idx, size_t col_idx, const char* val) {
    if (!val) {
        return this->set_value(row_idx, col_idx, "");
    }
    return this->set_value(row_idx, col_idx, val);
}

bool
STBDATA::set_int32(size_t row_idx, size_t col_idx, int32_t val) {
    return this->set_value(row_idx, col_idx, std::to_string(val));
}
