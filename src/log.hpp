#pragma once

#include "http.hpp"
#include "util.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace aster {

// Access-log output format.
enum class LogFormat { Json, Text };

inline LogFormat log_format_from_string(const std::string& value) {
    return to_lower(value) == "text" ? LogFormat::Text : LogFormat::Json;
}

// Render one access-log line. Exposed (rather than only printed) so tests can
// assert the exact shape of both formats without capturing stdout.
//
// Json (default) — machine-parseable, one object per line:
//   {"time":"…Z","ip":"127.0.0.1","method":"GET","path":"/api/health",
//    "status":200,"bytes":142,"ms":0.31}
// Text — closer to Combined Log Format, easier to skim:
//   2026-07-17T05:00:00Z 127.0.0.1 "GET /api/health HTTP/1.1" 200 142 0.31ms
inline std::string format_access_log(LogFormat format, const std::string& client_ip,
                                     const Request& request, int status, std::size_t bytes,
                                     std::chrono::microseconds latency) {
    // Sub-millisecond resolution matters here: most handlers finish well under
    // 1 ms, and truncating to integers reported every one of them as "0".
    const double ms = static_cast<double>(latency.count()) / 1000.0;
    std::ostringstream line;
    line << std::fixed << std::setprecision(2);
    if (format == LogFormat::Text) {
        line << current_time_iso() << " " << client_ip << " \"" << request.method << " "
             << request.target << " " << request.version << "\" " << status << " " << bytes << " "
             << ms << "ms";
    } else {
        line << "{\"time\":\"" << current_time_iso() << "\","
             << "\"ip\":\"" << json_escape(client_ip) << "\","
             << "\"method\":\"" << json_escape(request.method) << "\","
             << "\"path\":\"" << json_escape(request.path) << "\","
             << "\"status\":" << status << ","
             << "\"bytes\":" << bytes << ","
             << "\"ms\":" << ms << "}";
    }
    return line.str();
}

// One structured access-log line per request, serialized under a mutex so
// concurrent pool workers never tear each other's lines.
inline void access_log(LogFormat format, const std::string& client_ip, const Request& request,
                       int status, std::size_t bytes, std::chrono::microseconds latency) {
    static std::mutex log_mutex;
    const std::string line = format_access_log(format, client_ip, request, status, bytes, latency);
    std::lock_guard<std::mutex> lock(log_mutex);
    // Flush every line. When stdout is redirected to a file it is block
    // buffered, so without this the access log of a long-running server stays
    // stuck in a 4 KiB buffer — `tail -f` shows nothing until the buffer fills
    // or the process exits. One write per request is what any access log does.
    std::cout << line << std::endl;
}

}  // namespace aster
