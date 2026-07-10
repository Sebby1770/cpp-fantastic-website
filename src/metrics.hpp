#pragma once

#include "util.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace aster {

class Metrics {
public:
    Metrics() : started_at_(std::chrono::steady_clock::now()) {}

    void record(const std::string& path) {
        total_requests_.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex_);
        ++by_path_[path];
    }

    std::uint64_t total_requests() const {
        return total_requests_.load(std::memory_order_relaxed);
    }

    std::int64_t uptime_seconds() const {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - started_at_).count();
    }

    std::string to_json() const {
        std::ostringstream json;
        json << "{";
        json << "\"total_requests\":" << total_requests() << ",";
        json << "\"uptime_seconds\":" << uptime_seconds() << ",";
        json << "\"by_path\":{";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool first = true;
            for (const auto& entry : by_path_) {
                if (!first) {
                    json << ",";
                }
                first = false;
                json << "\"" << json_escape(entry.first) << "\":" << entry.second;
            }
        }
        json << "}}";
        return json.str();
    }

private:
    std::chrono::steady_clock::time_point started_at_;
    std::atomic<std::uint64_t> total_requests_{0};
    mutable std::mutex mutex_;
    std::map<std::string, std::uint64_t> by_path_;
};

}  // namespace aster
