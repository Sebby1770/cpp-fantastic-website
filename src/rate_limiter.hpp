#pragma once

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace aster {

class RateLimiter {
public:
    explicit RateLimiter(int per_second) : rate_(per_second) {}

    bool allow(const std::string& ip) {
        if (rate_ <= 0) {
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        auto& bucket = buckets_[ip];
        if (bucket.last.time_since_epoch().count() == 0) {
            bucket.tokens = static_cast<double>(rate_);
            bucket.last = now;
        } else {
            const double elapsed = std::chrono::duration<double>(now - bucket.last).count();
            bucket.tokens = std::min(static_cast<double>(rate_), bucket.tokens + elapsed * rate_);
            bucket.last = now;
        }
        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            maybe_gc(now);
            return true;
        }
        maybe_gc(now);
        return false;
    }

private:
    struct Bucket {
        double tokens = 0;
        std::chrono::steady_clock::time_point last{};
    };

    void maybe_gc(std::chrono::steady_clock::time_point now) {
        if (buckets_.size() < 4096) {
            return;
        }
        for (auto it = buckets_.begin(); it != buckets_.end();) {
            if (now - it->second.last > std::chrono::minutes(1)) {
                it = buckets_.erase(it);
            } else {
                ++it;
            }
        }
    }

    int rate_;
    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};

}  // namespace aster
