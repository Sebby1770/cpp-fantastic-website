#pragma once

#include "util.hpp"

#include <iostream>
#include <mutex>
#include <string>

namespace aster {

class AccessLog {
public:
    AccessLog(std::string format, bool quiet)
        : format_(format == "text" ? "text" : "json"), quiet_(quiet) {}

    void write(const std::string& ip, const std::string& method, const std::string& target,
               int status, std::size_t bytes, double ms) {
        if (quiet_) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (format_ == "text") {
            std::cout << ip << " - " << method << " " << target << " " << status << " " << bytes
                      << " " << ms << "ms\n";
        } else {
            std::cout << "{"
                      << "\"ts\":\"" << json_escape(current_time_iso()) << "\","
                      << "\"ip\":\"" << json_escape(ip) << "\","
                      << "\"method\":\"" << json_escape(method) << "\","
                      << "\"path\":\"" << json_escape(target) << "\","
                      << "\"status\":" << status << ","
                      << "\"bytes\":" << bytes << ","
                      << "\"ms\":" << ms << "}\n";
        }
        std::cout.flush();
    }

private:
    std::string format_;
    bool quiet_;
    std::mutex mutex_;
};

}  // namespace aster
