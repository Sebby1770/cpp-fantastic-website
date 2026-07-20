#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace aster {

inline constexpr const char* kVersion = "2.0.0";

inline std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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

inline std::string json_escape(const std::string& value) {
    std::ostringstream escaped;
    for (const char ch : value) {
        switch (ch) {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(static_cast<unsigned char>(ch));
                } else {
                    escaped << ch;
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
        const std::string pair = query_string.substr(start, end == std::string::npos ? end : end - start);
        if (!pair.empty()) {
            const std::size_t equals = pair.find('=');
            const std::string key = url_decode(pair.substr(0, equals));
            const std::string value = equals == std::string::npos ? "" : url_decode(pair.substr(equals + 1));
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
                     int fallback, int min, int max) {
    const auto found = query.find(key);
    if (found == query.end()) {
        return fallback;
    }
    try {
        return std::clamp(std::stoi(found->second), min, max);
    } catch (...) {
        return fallback;
    }
}

inline std::string string_param(const std::map<std::string, std::string>& query, const std::string& key,
                                const std::string& fallback, std::size_t max_len = 64) {
    const auto found = query.find(key);
    if (found == query.end() || found->second.empty()) {
        return fallback;
    }
    return found->second.substr(0, max_len);
}

inline uint32_t stable_seed(const std::string& value) {
    uint32_t hash = 2166136261u;
    for (const unsigned char ch : value) {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
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

inline std::time_t file_time_to_time_t(std::filesystem::file_time_type file_time) {
    // C++17 has no clock_cast; bridge through the current time of both clocks.
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(system_time);
}

// IMF-fixdate per RFC 9110, e.g. "Thu, 17 Jul 2026 05:00:00 GMT".
inline std::string http_date(std::time_t time) {
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%a, %d %b %Y %H:%M:%S GMT");
    return out.str();
}

inline std::string http_date(std::filesystem::file_time_type file_time) {
    return http_date(file_time_to_time_t(file_time));
}

inline bool parse_http_date(const std::string& value, std::time_t& out) {
    std::tm parsed{};
    if (strptime(value.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &parsed) == nullptr) {
        return false;
    }
    out = timegm(&parsed);
    return true;
}

// FNV-1a 64-bit over arbitrary bytes, rendered as 16 hex chars (for weak ETags).
inline std::string weak_hash_hex(const std::string& data) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char ch : data) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

inline std::string mime_type(const std::filesystem::path& path) {
    const std::string ext = to_lower(path.extension().string());
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace aster
