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

// One structured access-log line per request, e.g.:
//   2026-07-17T05:00:00Z 127.0.0.1 "GET /api/health HTTP/1.1" 200 142 0.31ms
// Serialized under a mutex so concurrent workers never tear lines.
inline void access_log(const std::string& client_ip, const Request& request, int status,
                       std::size_t bytes, std::chrono::microseconds latency) {
    static std::mutex log_mutex;
    std::ostringstream line;
    line << current_time_iso() << " " << client_ip << " \"" << request.method << " "
         << request.target << " " << request.version << "\" " << status << " " << bytes << " "
         << std::fixed << std::setprecision(2)
         << (static_cast<double>(latency.count()) / 1000.0) << "ms";
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << line.str() << "\n";
}

}  // namespace aster
