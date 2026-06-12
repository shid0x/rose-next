#include "util.h"

#include "rose/common/common_interface.h"

#include <ctime>
#include <iomanip>
#include <iterator>
#include <sstream>

std::string
get_exe_dir() {
    char buffer[256] = {0};
    Rose::Common::get_bin_dir(buffer, 256);

    return std::string(buffer).append("/");
}

namespace Rose::Util {

// Number of seconds between windows epoch  and unix epoch
const uint64_t WIN_TO_UNIX_EPOCH_SECS = 116444736000000000;

std::optional<DateTime>
parse_datetime_str(const std::string& s) {
    // Visual studio bug if format string is longer than stream
    // https://developercommunity.visualstudio.com/content/problem/18311/stdget-time-asserts-with-istreambuf-iterator-is-no.html
    if (s.size() < 19) {
        return std::nullopt;
    }
    std::tm t = {};
    std::istringstream ss(s);
    ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        return std::nullopt;
    }
    const std::time_t tt = std::mktime(&t);
    return std::chrono::system_clock::from_time_t(tt);
}

std::string
to_datetime_str(const DateTime& dt) {
    std::time_t tt = std::chrono::system_clock::to_time_t(dt);
    std::tm* t = std::gmtime(&tt);

    std::stringstream ss;
    ss << std::put_time(t, "%Y-%m-%d %H:%M:%S") << "+00";
    if (ss.fail()) {
        return "";
    }
    return ss.str();
}

std::chrono::system_clock::duration
time_since_win_epoch(const std::chrono::time_point<std::chrono::system_clock>& t) {
    return t.time_since_epoch() + std::chrono::seconds(116444736000000000);
}

std::unordered_map<std::string, std::string>
parse_args(int argc, char** argv) {
    std::unordered_map<std::string, std::string> res;

    for (int idx = 1; idx < argc; ++idx) {
        const int next_idx = idx + 1;
        if (next_idx < argc) {
            res.insert({(argv[idx]), argv[next_idx]});
        }
    }

    return res;
}

std::vector<std::string>
split_string_whitespace(const std::string& s) {
    std::istringstream iss(s);
    return std::vector<std::string>{std::istream_iterator<std::string>{iss},
        std::istream_iterator<std::string>{}};
}

}; // namespace Rose::Util
