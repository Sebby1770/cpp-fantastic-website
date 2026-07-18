#pragma once

#include "http.hpp"
#include "log.hpp"
#include "metrics.hpp"
#include "mission.hpp"
#include "thread_pool.hpp"
#include "util.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aster {

namespace fs = std::filesystem;

inline unsigned default_thread_count() {
    return std::clamp(std::max(2u, std::thread::hardware_concurrency()), 2u, 32u);
}

struct CliOptions {
    int port = 8080;
    bool quiet = false;
    unsigned threads = default_thread_count();
    std::size_t max_body = kMaxBodyBytes;
};

class Server {
public:
    Server(const CliOptions& options, fs::path public_dir)
        : port_(options.port),
          public_dir_(std::move(public_dir)),
          quiet_(options.quiet),
          max_body_(options.max_body),
          pool_(options.threads) {}

    int run(std::atomic<bool>& running) {
        running_ = &running;
        const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "Could not create socket\n";
            return 1;
        }

        int opt = 1;
        ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(static_cast<uint16_t>(port_));

        if (::bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            std::cerr << "Could not bind to port " << port_ << "\n";
            ::close(server_fd);
            return 1;
        }

        if (::listen(server_fd, 64) < 0) {
            std::cerr << "Could not listen on port " << port_ << "\n";
            ::close(server_fd);
            return 1;
        }

        std::cout << "AsterForge " << kVersion << " running at http://localhost:" << port_ << "\n";
        std::cout << "Serving " << public_dir_ << "\n";
        if (quiet_) {
            std::cout << "Request logging disabled (--quiet)\n";
        }

        while (running.load()) {
            pollfd listen_poll{};
            listen_poll.fd = server_fd;
            listen_poll.events = POLLIN;
            const int ready = ::poll(&listen_poll, 1, 250);
            if (ready <= 0) {
                // Timeout or EINTR: re-check the running flag.
                continue;
            }

            sockaddr_in client_address{};
            socklen_t client_len = sizeof(client_address);
            const int client_fd =
                ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_address), &client_len);
            if (client_fd < 0) {
                if (running.load()) {
                    std::cerr << "Accept failed\n";
                }
                continue;
            }

            timeval recv_timeout{};
            recv_timeout.tv_sec = 5;
            ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
            timeval send_timeout{};
            send_timeout.tv_sec = 10;
            ::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));

            char ip_buffer[INET_ADDRSTRLEN] = "unknown";
            ::inet_ntop(AF_INET, &client_address.sin_addr, ip_buffer, sizeof(ip_buffer));
            std::string client_ip(ip_buffer);

            pool_.submit([this, client_fd, client_ip = std::move(client_ip)] {
                handle_client(client_fd, client_ip);
            });
        }

        ::close(server_fd);
        pool_.shutdown();
        std::cout << "AsterForge shutting down...\n";
        return 0;
    }

private:
    void handle_client(int client_fd, const std::string& client_ip) {
        std::string leftover;
        for (int served = 0; served < kMaxRequestsPerConn; ++served) {
            Request request;
            const ReadResult result = read_http_request(client_fd, request, leftover, max_body_);
            if (result == ReadResult::Closed || result == ReadResult::Timeout) {
                // Clean close or idle keep-alive timeout: nothing to answer.
                break;
            }

            const auto started = std::chrono::steady_clock::now();
            Response response;
            bool keep_alive = false;
            if (result == ReadResult::Ok) {
                response = route(request);
                const auto connection = request.headers.find("connection");
                keep_alive = wants_keep_alive(
                    request.version,
                    connection == request.headers.end() ? "" : connection->second);
            } else if (result == ReadResult::Malformed) {
                response = bad_request("400 Bad Request: malformed HTTP request.");
            } else if (result == ReadResult::HeadersTooLarge) {
                response = simple_status(431);
            } else {  // ReadResult::BodyTooLarge
                response = simple_status(413);
            }

            if (response.status >= 400 || served + 1 == kMaxRequestsPerConn ||
                !running_->load()) {
                keep_alive = false;
            }
            if (request.method == "HEAD") {
                response.include_body = false;
            }
            const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started);

            metrics_.record(request.path.empty() ? "/" : request.path);

            if (!quiet_) {
                access_log(client_ip, request, response.status, response.body.size(), latency);
            }

            if (!send_response(client_fd, response, keep_alive) || !keep_alive) {
                break;
            }
        }
        ::close(client_fd);
    }

    Response route(const Request& request) {
        Response response = dispatch(request);
        const bool has_cache_control = std::any_of(
            response.extra_headers.begin(), response.extra_headers.end(),
            [](const std::pair<std::string, std::string>& header) {
                return header.first == "Cache-Control";
            });
        if (!has_cache_control) {
            response.extra_headers.emplace_back("Cache-Control", "no-store");
        }
        return response;
    }

    Response dispatch(const Request& request) {
        const std::string& method = request.method;

        if (method == "OPTIONS") {
            Response response{204, "No Content", "text/plain; charset=utf-8", "", true, {}};
            return response;
        }

        if (method != "GET" && method != "HEAD" && method != "POST") {
            return method_not_allowed();
        }

        if (request.path == "/api/health") {
            if (method == "POST") {
                return method_not_allowed();
            }
            std::ostringstream json;
            json << "{";
            json << "\"status\":\"ok\",";
            json << "\"service\":\"AsterForge\",";
            json << "\"language\":\"C++17\",";
            json << "\"version\":\"" << kVersion << "\",";
            json << "\"uptime_seconds\":" << metrics_.uptime_seconds() << ",";
            json << "\"request_count\":" << metrics_.total_requests();
            json << "}";
            return json_response(json.str());
        }

        if (request.path == "/api/mission") {
            if (method == "POST") {
                return method_not_allowed();
            }
            return json_response(build_mission_json(request.query));
        }

        if (request.path == "/api/palettes") {
            if (method == "POST") {
                return method_not_allowed();
            }
            return json_response(build_palettes_json());
        }

        if (request.path == "/api/constellation") {
            if (method == "POST") {
                return method_not_allowed();
            }
            return json_response(build_constellation_json(request.query));
        }

        if (request.path == "/api/metrics") {
            if (method == "POST") {
                return method_not_allowed();
            }
            return json_response(metrics_.to_json());
        }

        if (request.path == "/api/echo") {
            if (method == "GET") {
                return method_not_allowed();
            }
            if (method == "POST" || method == "HEAD") {
                std::ostringstream json;
                json << "{";
                json << "\"echoed\":true,";
                json << "\"bytes\":" << request.body.size() << ",";
                json << "\"contentType\":\""
                     << json_escape(request.headers.count("content-type")
                                        ? request.headers.at("content-type")
                                        : "")
                     << "\",";
                // Echo body as a JSON string (escaped), so clients always get valid JSON.
                json << "\"body\":\"" << json_escape(request.body) << "\"";
                json << "}";
                return json_response(json.str());
            }
        }

        if (method == "POST") {
            return method_not_allowed();
        }

        return serve_static(request.path);
    }

    Response serve_static(const std::string& request_path) const {
        if (request_path.find("..") != std::string::npos) {
            return bad_request("Path traversal is not allowed.");
        }

        std::string relative = request_path == "/" ? "/index.html" : request_path;
        while (!relative.empty() && relative.front() == '/') {
            relative.erase(relative.begin());
        }

        fs::path file_path = public_dir_ / relative;
        if (fs::is_directory(file_path)) {
            file_path /= "index.html";
        }

        if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
            return not_found();
        }

        const std::string body = read_file(file_path);
        return Response{200, "OK", mime_type(file_path), body, true, {}};
    }

    int port_;
    fs::path public_dir_;
    bool quiet_;
    std::size_t max_body_;
    ThreadPool pool_;
    Metrics metrics_;
    std::atomic<bool>* running_ = nullptr;
};

inline fs::path find_public_dir() {
    const std::vector<fs::path> candidates = {
        fs::current_path() / "public",
        fs::current_path() / ".." / "public",
        fs::current_path() / ".." / ".." / "public"
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate / "index.html")) {
            return fs::weakly_canonical(candidate);
        }
    }
    return fs::current_path() / "public";
}

inline CliOptions parse_cli(int argc, char** argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            options.port = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            options.threads = static_cast<unsigned>(std::clamp(std::stoi(argv[++i]), 2, 32));
        } else if (arg == "--max-body" && i + 1 < argc) {
            options.max_body = static_cast<std::size_t>(std::max(1L, std::stol(argv[++i])));
        } else if (arg == "--quiet" || arg == "-q") {
            options.quiet = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "AsterForge " << kVersion << "\n"
                      << "Usage: cpp_fantastic_website [--port N] [--threads N] [--max-body BYTES] [--quiet]\n";
            std::exit(0);
        }
    }
    options.port = std::clamp(options.port, 1024, 65535);
    return options;
}

}  // namespace aster
