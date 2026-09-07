#pragma once

#include "json.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aster {

class Metrics {
public:
    struct Snapshot {
        std::uint64_t requests = 0;
        std::uint64_t s2xx = 0;
        std::uint64_t s3xx = 0;
        std::uint64_t s4xx = 0;
        std::uint64_t s5xx = 0;
        std::size_t latency_count = 0;
        double mean = 0;
        double max = 0;
        double p50 = 0;
        double p99 = 0;
        std::unordered_map<std::string, std::uint64_t> by_path;
    };

    void record(const std::string& path, int status, double ms) {
        requests_.fetch_add(1, std::memory_order_relaxed);
        const int klass = status / 100;
        if (klass == 2) {
            s2xx_.fetch_add(1, std::memory_order_relaxed);
        } else if (klass == 3) {
            s3xx_.fetch_add(1, std::memory_order_relaxed);
        } else if (klass == 4) {
            s4xx_.fetch_add(1, std::memory_order_relaxed);
        } else if (klass == 5) {
            s5xx_.fetch_add(1, std::memory_order_relaxed);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        by_path_[path] += 1;
        const std::size_t index = lat_i_ % kRing;
        if (lat_n_ < kRing) {
            lat_sum_ += ms;
            lat_[index] = ms;
            ++lat_n_;
        } else {
            lat_sum_ -= lat_[index];
            lat_sum_ += ms;
            lat_[index] = ms;
        }
        ++lat_i_;
        if (ms > lat_max_) {
            lat_max_ = ms;
        }
    }

    std::uint64_t request_count() const { return requests_.load(std::memory_order_relaxed); }

    Snapshot snapshot() const {
        Snapshot out;
        out.requests = requests_.load(std::memory_order_relaxed);
        out.s2xx = s2xx_.load(std::memory_order_relaxed);
        out.s3xx = s3xx_.load(std::memory_order_relaxed);
        out.s4xx = s4xx_.load(std::memory_order_relaxed);
        out.s5xx = s5xx_.load(std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(mutex_);
        out.by_path = by_path_;
        out.latency_count = lat_n_;
        out.max = lat_max_;
        if (lat_n_ == 0) {
            return out;
        }
        out.mean = lat_sum_ / static_cast<double>(lat_n_);
        std::vector<double> copy(lat_.begin(), lat_.begin() + static_cast<std::ptrdiff_t>(lat_n_));
        std::sort(copy.begin(), copy.end());
        out.p50 = copy[(copy.size() - 1) / 2];
        const std::size_t idx99 =
            copy.empty() ? 0 : (copy.size() * 99) / 100;
        out.p99 = copy[std::min(copy.size() - 1, idx99)];
        return out;
    }

    std::string to_json() const {
        const Snapshot snap = snapshot();
        Json::Obj latency;
        latency.kv("count", static_cast<unsigned long long>(snap.latency_count));
        latency.kv("mean", snap.mean);
        latency.kv("max", snap.max);
        latency.kv("p50", snap.p50);
        latency.kv("p99", snap.p99);

        Json::Obj status;
        status.kv("2xx", snap.s2xx);
        status.kv("3xx", snap.s3xx);
        status.kv("4xx", snap.s4xx);
        status.kv("5xx", snap.s5xx);

        Json::Obj by_path;
        for (const auto& entry : snap.by_path) {
            by_path.kv(entry.first, entry.second);
        }

        Json::Obj totals;
        totals.kv("requests", snap.requests);

        Json::Obj root;
        root.kv("totals", totals.done());
        root.kv("by_path", by_path.done());
        root.kv("latency_ms", latency.done());
        root.kv("status", status.done());
        return root.done().str();
    }

    std::string telemetry_json(long long uptime_seconds) const {
        const Snapshot snap = snapshot();
        Json::Obj root;
        root.kv("requests", snap.requests);
        root.kv("p99", snap.p99);
        root.kv("2xx", snap.s2xx);
        root.kv("4xx", snap.s4xx);
        root.kv("5xx", snap.s5xx);
        root.kv("uptime_seconds", uptime_seconds);
        return root.done().str();
    }

private:
    static constexpr std::size_t kRing = 1024;
    std::atomic<std::uint64_t> requests_{0};
    std::atomic<std::uint64_t> s2xx_{0};
    std::atomic<std::uint64_t> s3xx_{0};
    std::atomic<std::uint64_t> s4xx_{0};
    std::atomic<std::uint64_t> s5xx_{0};
    mutable std::mutex mutex_;
    std::array<double, kRing> lat_{};
    std::size_t lat_i_ = 0;
    std::size_t lat_n_ = 0;
    double lat_sum_ = 0;
    double lat_max_ = 0;
    std::unordered_map<std::string, std::uint64_t> by_path_;
};

}  // namespace aster
