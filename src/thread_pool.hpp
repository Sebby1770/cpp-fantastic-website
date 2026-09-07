#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace aster {

class ThreadPool {
public:
    ThreadPool(int workers, std::size_t max_queue)
        : max_queue_(max_queue == 0 ? 32 : max_queue) {
        const int count = workers < 1 ? 1 : workers;
        workers_.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            workers_.emplace_back([this] { run(); });
        }
    }

    ~ThreadPool() { stop(); }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool submit(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_ || queue_.size() >= max_queue_) {
                return false;
            }
            queue_.push(std::move(job));
        }
        cv_.notify_one();
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
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }

private:
    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) {
                    return;
                }
                job = std::move(queue_.front());
                queue_.pop();
            }
            try {
                job();
            } catch (...) {
            }
        }
    }

    std::size_t max_queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
};

}  // namespace aster
