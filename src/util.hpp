#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace aster {

inline constexpr const char* kVersion = "4.0.0";
inline constexpr const char* kService = "AsterForge";
inline constexpr const char* kLanguage = "C++17";

namespace fs = std::filesystem;

inline int clamp_int(int value, int lo, int hi) {
    return std::max(lo, std::min(value, hi));
}

template <typename T>
inline T clamp_value(T value, T lo, T hi) {
    return std::max(lo, std::min(value, hi));
}

inline std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string trim(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

inline bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

inline std::string url_decode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            char* end = nullptr;
            const long decoded = std::strtol(hex.c_str(), &end, 16);
            if (end != nullptr && *end == '\0') {
                out.push_back(static_cast<char>(decoded));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return out;
}

inline std::string url_encode(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return out.str();
}

inline std::string json_escape(const std::string& value) {
    std::ostringstream escaped;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (ch < 0x20) {
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(ch);
                } else {
                    escaped << static_cast<char>(ch);
                }
        }
    }
    return escaped.str();
}

inline std::map<std::string, std::string> parse_query(const std::string& query_string) {
    std::map<std::string, std::string> params;
    std::size_t start = 0;
    while (start <= query_string.size()) {
        const std::size_t end = query_string.find('&', start);
        const std::string pair = query_string.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (!pair.empty()) {
            const std::size_t equals = pair.find('=');
            const std::string key = url_decode(pair.substr(0, equals));
            const std::string value =
                equals == std::string::npos ? "" : url_decode(pair.substr(equals + 1));
            params[key] = value;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return params;
}

inline int int_param(const std::map<std::string, std::string>& query, const std::string& key,
                     int fallback, int min_value, int max_value) {
    const auto found = query.find(key);
    if (found == query.end()) {
        return fallback;
    }
    try {
        return clamp_int(std::stoi(found->second), min_value, max_value);
    } catch (...) {
        return fallback;
    }
}

inline std::string string_param(const std::map<std::string, std::string>& query,
                                const std::string& key, const std::string& fallback,
                                std::size_t max_len = 64) {
    const auto found = query.find(key);
    if (found == query.end() || found->second.empty()) {
        return fallback;
    }
    return found->second.substr(0, max_len);
}

inline std::uint32_t fnv1a(const std::string& value) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char ch : value) {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

inline std::uint32_t stable_seed(const std::string& value) {
    return fnv1a(value);
}

inline std::string current_time_iso() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

inline std::string http_date(std::time_t time = std::time(nullptr)) {
    std::tm utc{};
    gmtime_r(&time, &utc);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &utc);
    return buf;
}

inline bool parse_http_date(const std::string& value, std::time_t& out) {
    std::tm utc{};
    std::istringstream in(trim(value));
    in >> std::get_time(&utc, "%a, %d %b %Y %H:%M:%S GMT");
    if (in.fail()) {
        return false;
    }
    const int year = utc.tm_year + 1900;
    const int month = utc.tm_mon;
    if (month < 0 || month > 11) {
        return false;
    }
    static const int kDays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int day_of_year = kDays[month] + utc.tm_mday - 1;
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (month >= 2 && leap) {
        ++day_of_year;
    }
    const long days = static_cast<long>(year - 1970) * 365 + (year - 1969) / 4 -
                      (year - 1901) / 100 + (year - 1601) / 400 + day_of_year;
    out = static_cast<std::time_t>(days * 86400L + utc.tm_hour * 3600 + utc.tm_min * 60 +
                                   utc.tm_sec);
    return true;
}

inline std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << value;
    return out.str();
}

struct ByteRange {
    enum class Status { none, ok, unsatisfiable, invalid };
    Status status = Status::none;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
};

inline ByteRange parse_byte_range(const std::string& header, std::uint64_t file_size) {
    ByteRange range;
    const std::string value = trim(header);
    if (value.empty()) {
        return range;
    }
    if (!starts_with(to_lower(value), "bytes=")) {
        range.status = ByteRange::Status::invalid;
        return range;
    }
    std::string spec = value.substr(6);
    const std::size_t comma = spec.find(',');
    if (comma != std::string::npos) {
        spec = spec.substr(0, comma);
    }
    spec = trim(spec);
    const std::size_t dash = spec.find('-');
    if (dash == std::string::npos) {
        range.status = ByteRange::Status::invalid;
        return range;
    }
    const std::string first = trim(spec.substr(0, dash));
    const std::string last = trim(spec.substr(dash + 1));
    if (file_size == 0) {
        range.status = ByteRange::Status::unsatisfiable;
        return range;
    }
    try {
        if (first.empty()) {
            if (last.empty()) {
                range.status = ByteRange::Status::invalid;
                return range;
            }
            const auto suffix = static_cast<std::uint64_t>(std::stoull(last));
            if (suffix == 0) {
                range.status = ByteRange::Status::unsatisfiable;
                return range;
            }
            const std::uint64_t length = std::min<std::uint64_t>(suffix, file_size);
            range.start = file_size - length;
            range.end = file_size - 1;
            range.status = ByteRange::Status::ok;
            return range;
        }
        range.start = static_cast<std::uint64_t>(std::stoull(first));
        if (range.start >= file_size) {
            range.status = ByteRange::Status::unsatisfiable;
            return range;
        }
        if (last.empty()) {
            range.end = file_size - 1;
        } else {
            range.end = static_cast<std::uint64_t>(std::stoull(last));
            if (range.end < range.start) {
                range.status = ByteRange::Status::invalid;
                return range;
            }
            if (range.end >= file_size) {
                range.end = file_size - 1;
            }
        }
        range.status = ByteRange::Status::ok;
        return range;
    } catch (...) {
        range.status = ByteRange::Status::invalid;
        return range;
    }
}

inline bool path_has_dotdot(const std::string& path) {
    std::string current;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        const char ch = i < path.size() ? path[i] : '/';
        if (ch == '/' || ch == '\\') {
            if (current == "..") {
                return true;
            }
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    return false;
}

inline bool path_is_inside(const fs::path& root, const fs::path& candidate) {
    std::error_code ec;
    const fs::path root_c = fs::weakly_canonical(root, ec);
    if (ec) {
        return false;
    }
    const fs::path cand_c = fs::weakly_canonical(candidate, ec);
    if (ec) {
        return false;
    }
    const std::string root_s = root_c.string();
    const std::string cand_s = cand_c.string();
    if (cand_s == root_s) {
        return true;
    }
    const char sep = static_cast<char>(fs::path::preferred_separator);
    return cand_s.size() > root_s.size() && cand_s.compare(0, root_s.size(), root_s) == 0 &&
           cand_s[root_s.size()] == sep;
}

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }

    int release() {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    void reset() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

}  // namespace aster
