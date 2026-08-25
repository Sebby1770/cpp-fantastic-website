#pragma once

#include "http.hpp"
#include "metrics.hpp"

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>

#include <unistd.h>

namespace aster {

inline constexpr int kMaxStreamClients = 32;

// Tracks how many Server-Sent Events clients are connected. Streams run on
// detached threads (they must not occupy pool workers), so an atomic counter
// is the only shared state: try_acquire() caps concurrency, release() must be
// called exactly once per successful acquire.
class StreamHub {
public:
    bool try_acquire() {
        int current = active_.load(std::memory_order_relaxed);
        while (current < kMaxStreamClients) {
            if (active_.compare_exchange_weak(current, current + 1,
                                              std::memory_order_acq_rel)) {
                return true;
            }
        }
        return false;
    }

    void release() {
        active_.fetch_sub(1, std::memory_order_acq_rel);
    }

    int active() const {
        return active_.load(std::memory_order_acquire);
    }

private:
    std::atomic<int> active_{0};
};

// Serve one SSE connection: write the handshake, then push a telemetry event
// every second until the client disconnects (send_all fails; SO_SNDTIMEO
// bounds blocking) or the server begins shutting down. The 1 s wait is sliced
// into 250 ms checks of `running` so graceful shutdown never stalls behind an
// open stream. Owns `client_fd` and closes it; releases `hub` on exit.
inline void run_sse(int client_fd, Metrics& metrics, std::atomic<bool>& running,
                    StreamHub& hub) {
    const std::string handshake =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Accel-Buffering: no\r\n"
        "\r\n";
    if (send_all(client_fd, handshake)) {
        std::uint64_t tick = 0;
        while (running.load()) {
            ++tick;
            std::ostringstream event;
            event << "event: telemetry\ndata: " << metrics.snapshot_json(tick) << "\n\n";
            if (tick % 15 == 0) {
                event << ": keep-alive\n\n";
            }
            if (!send_all(client_fd, event.str())) {
                break;
            }
            for (int slice = 0; slice < 4 && running.load(); ++slice) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }
    }
    ::close(client_fd);
    hub.release();
}

}  // namespace aster
