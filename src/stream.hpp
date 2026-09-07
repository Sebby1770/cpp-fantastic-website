#pragma once

#include "http.hpp"
#include "metrics.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace aster {

class SseHub {
public:
    SseHub(Metrics* metrics, std::atomic<bool>* running, int max_clients)
        : metrics_(metrics), running_(running), max_clients_(max_clients < 1 ? 1 : max_clients) {}

    ~SseHub() { stop(); }

    SseHub(const SseHub&) = delete;
    SseHub& operator=(const SseHub&) = delete;

    void start(std::chrono::steady_clock::time_point started) {
        started_ = started;
        thread_ = std::thread([this] { run(); });
    }

    bool try_reserve() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<int>(clients_.size()) + reserved_ >= max_clients_) {
            return false;
        }
        ++reserved_;
        return true;
    }

    void cancel_reserve() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (reserved_ > 0) {
            --reserved_;
        }
    }

    bool attach(int fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (reserved_ > 0) {
            --reserved_;
        }
        if (static_cast<int>(clients_.size()) >= max_clients_) {
            return false;
        }
        clients_.push_back(fd);
        const std::string first = format_event();
        if (!send_all(fd, first, 5000)) {
            clients_.pop_back();
            return false;
        }
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) {
                return;
            }
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (int fd : clients_) {
            UniqueFd closer(fd);
        }
        clients_.clear();
    }

private:
    std::string format_event() const {
        long long uptime = 0;
        if (started_.time_since_epoch().count() != 0) {
            uptime = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now() - started_)
                         .count();
        }
        const std::string data = metrics_ ? metrics_->telemetry_json(uptime)
                                          : "{\"requests\":0,\"p99\":0,\"2xx\":0,\"4xx\":0,\"5xx\":0,"
                                            "\"uptime_seconds\":0}";
        return "retry: 1000\nevent: telemetry\ndata: " + data + "\n\n";
    }

    void run() {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return stop_ || (running_ != nullptr && !running_->load());
            });
            if (stop_ || (running_ != nullptr && !running_->load())) {
                break;
            }
            const std::string payload = format_event();
            const std::vector<int> snapshot = clients_;
            lock.unlock();

            std::vector<int> dead;
            for (int fd : snapshot) {
                if (!send_all(fd, payload, 4000)) {
                    dead.push_back(fd);
                }
            }
            if (dead.empty()) {
                continue;
            }

            lock.lock();
            std::vector<int> next;
            next.reserve(clients_.size());
            for (int fd : clients_) {
                if (std::find(dead.begin(), dead.end(), fd) != dead.end()) {
                    UniqueFd closer(fd);
                } else {
                    next.push_back(fd);
                }
            }
            clients_.swap(next);
        }
    }

    Metrics* metrics_ = nullptr;
    std::atomic<bool>* running_ = nullptr;
    int max_clients_ = 16;
    std::chrono::steady_clock::time_point started_{};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<int> clients_;
    int reserved_ = 0;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace aster
