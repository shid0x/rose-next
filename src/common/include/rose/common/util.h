#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

std::string get_exe_dir();

namespace Rose::Util {
using DateTime = std::chrono::time_point<std::chrono::system_clock>;

/// Parse a timestamp string (i.e. "%Y-%m-%d %H:%M:%S") into a datetime
std::optional<DateTime> parse_datetime_str(const std::string& s);

/// Create a timestamp string from a datetime
std::string to_datetime_str(const DateTime& dt);

/// Get duration since windows epoch (January 1, 1601)
std::chrono::system_clock::duration time_since_win_epoch(
    const std::chrono::time_point<std::chrono::system_clock>& t);

/// Parse command line arguments into a map of key/value pairs
std::unordered_map<std::string, std::string> parse_args(int argc, char** argv);

/// Tokenize a string by whitespace separators
std::vector<std::string> split_string_whitespace(const std::string& s);

/// Convert a byte buffer to the type `T`
template<typename T>
inline T
from_bytes_le(std::vector<uint8_t>& bytes) {
    const size_t size = sizeof(T);
    if (bytes.size() < size) {
        return 0;
    }

    T res = 0;
    for (size_t i = 0; i < size; ++i) {
        const T byte = static_cast<T>(bytes[i]);
        res |= (byte << (i * 8));
    }
    return res;
}

template<>
inline std::string
from_bytes_le(std::vector<uint8_t>& bytes) {
    return std::string(bytes.data(), bytes.data() + bytes.size());
}

} // namespace Rose::Util