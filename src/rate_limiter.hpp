#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace aster {

// Per-key token bucket: each key holds up to `capacity` tokens, refilled at
// `refill_per_sec`; every allowed request consumes one token. `now` is passed
// in so tests can inject a fake clock. Idle entries are pruned once the map
// grows past kMaxEntries.
class RateLimiter {
public:
    RateLimiter(double capacity, double refill_per_sec)
        : capacity_(capacity), refill_per_sec_(refill_per_sec) {}

    bool allow(const std::string& ip, std::chrono::steady_clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto emplaced = buckets_.try_emplace(ip, Bucket{capacity_, now});
        Bucket& bucket = emplaced.first->second;
        if (!emplaced.second) {
            const double elapsed_sec =
                std::chrono::duration<double>(now - bucket.last).count();
            if (elapsed_sec > 0.0) {
                bucket.tokens = std::min(capacity_, bucket.tokens + elapsed_sec * refill_per_sec_);
            }
            bucket.last = now;
        }
        if (buckets_.size() > kMaxEntries) {
            prune(now);
        }
        if (bucket.tokens < 1.0) {
            return false;
        }
        bucket.tokens -= 1.0;
        return true;
    }

private:
    struct Bucket {
        double tokens;
        std::chrono::steady_clock::time_point last;
    };

    static constexpr std::size_t kMaxEntries = 1024;
    static constexpr std::chrono::seconds kIdleTtl{60};

    void prune(std::chrono::steady_clock::time_point now) {
        for (auto it = buckets_.begin(); it != buckets_.end();) {
            if (now - it->second.last > kIdleTtl) {
                it = buckets_.erase(it);
            } else {
                ++it;
            }
        }
    }

    double capacity_;
    double refill_per_sec_;
    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};

}  // namespace aster
