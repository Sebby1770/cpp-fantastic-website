#pragma once

#include "util.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace aster {

class Metrics {
public:
    Metrics() : started_at_(std::chrono::steady_clock::now()) {}

    void record(const std::string& path, int status, std::chrono::microseconds latency) {
        total_requests_.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex_);
        ++by_path_[path];
        const int status_class = status / 100;
        if (status_class >= 2 && status_class <= 5) {
            ++status_classes_[static_cast<std::size_t>(status_class - 2)];
        }
        latencies_us_[latency_count_ % kLatencyWindow] = latency.count();
        ++latency_count_;
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
        std::array<std::uint64_t, 4> status_classes{};
        std::vector<std::int64_t> latency_sample;
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
            status_classes = status_classes_;
            const std::size_t sample_size = std::min(latency_count_, kLatencyWindow);
            latency_sample.assign(latencies_us_.begin(),
                                  latencies_us_.begin() + static_cast<std::ptrdiff_t>(sample_size));
        }
        json << "},";
        json << "\"status\":{";
        json << "\"2xx\":" << status_classes[0] << ",";
        json << "\"3xx\":" << status_classes[1] << ",";
        json << "\"4xx\":" << status_classes[2] << ",";
        json << "\"5xx\":" << status_classes[3];
        json << "},";
        json << "\"latency_ms\":" << latency_json(latency_sample);
        json << "}";
        return json.str();
    }

private:
    static constexpr std::size_t kLatencyWindow = 1024;

    static std::string latency_json(std::vector<std::int64_t> sample) {
        std::ostringstream json;
        json << std::fixed << std::setprecision(3);
        json << "{\"count\":" << sample.size();
        if (sample.empty()) {
            json << ",\"mean\":0,\"max\":0,\"p50\":0,\"p99\":0}";
            return json.str();
        }
        double sum_us = 0.0;
        std::int64_t max_us = 0;
        for (const std::int64_t value : sample) {
            sum_us += static_cast<double>(value);
            max_us = std::max(max_us, value);
        }
        const auto percentile_us = [&sample](double fraction) {
            const auto index = static_cast<std::ptrdiff_t>(
                fraction * static_cast<double>(sample.size() - 1));
            std::nth_element(sample.begin(), sample.begin() + index, sample.end());
            return static_cast<double>(sample[static_cast<std::size_t>(index)]);
        };
        json << ",\"mean\":" << (sum_us / static_cast<double>(sample.size()) / 1000.0);
        json << ",\"max\":" << (static_cast<double>(max_us) / 1000.0);
        json << ",\"p50\":" << (percentile_us(0.50) / 1000.0);
        json << ",\"p99\":" << (percentile_us(0.99) / 1000.0);
        json << "}";
        return json.str();
    }

    std::chrono::steady_clock::time_point started_at_;
    std::atomic<std::uint64_t> total_requests_{0};
    mutable std::mutex mutex_;
    std::map<std::string, std::uint64_t> by_path_;
    std::array<std::uint64_t, 4> status_classes_{};
    std::array<std::int64_t, kLatencyWindow> latencies_us_{};
    std::size_t latency_count_ = 0;
};

}  // namespace aster
