#pragma once

#include "http.hpp"
#include "log.hpp"
#include "metrics.hpp"
#include "mission.hpp"
#include "rate_limiter.hpp"
#include "stream.hpp"
#include "thread_pool.hpp"
#include "util.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <vector>

namespace aster {

inline std::atomic<bool> g_running{true};

inline void request_stop() { g_running.store(false); }

struct ServerConfig {
    int port = 8080;
    int threads = 4;
    std::size_t max_body = 1024 * 1024;
    int rate_limit = 50;
    std::string log_format = "json";
    bool quiet = false;
    fs::path public_dir;
    int max_sse = 16;
};

inline fs::path find_public_dir(const char* argv0) {
    std::error_code ec;
    std::vector<fs::path> candidates = {
        fs::current_path() / "public",
        fs::current_path() / ".." / "public",
        fs::current_path() / ".." / ".." / "public",
    };
    if (argv0 && argv0[0] != '\0') {
        const fs::path exe = fs::absolute(argv0, ec);
        if (!ec) {
            const fs::path dir = exe.parent_path();
            candidates.push_back(dir / "public");
            candidates.push_back(dir / ".." / "public");
            candidates.push_back(dir / ".." / ".." / "public");
        }
    }
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate / "index.html")) {
            return fs::weakly_canonical(candidate);
        }
    }
    return fs::current_path() / "public";
}

class Server {
public:
    explicit Server(ServerConfig config)
        : cfg_(std::move(config)),
          limiter_(cfg_.rate_limit),
          log_(cfg_.log_format, cfg_.quiet),
          hub_(&metrics_, &g_running, cfg_.max_sse),
          pool_(cfg_.threads, static_cast<std::size_t>(cfg_.threads) * 32),
          started_(std::chrono::steady_clock::now()) {}

    int run() {
        UniqueFd listen_fd(::socket(AF_INET, SOCK_STREAM, 0));
        if (!listen_fd) {
            std::cerr << "Could not create socket\n";
            return 1;
        }

        int opt = 1;
        setsockopt(listen_fd.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(static_cast<std::uint16_t>(cfg_.port));

        if (bind(listen_fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            std::cerr << "Could not bind to port " << cfg_.port << "\n";
            return 1;
        }
        if (listen(listen_fd.get(), 128) < 0) {
            std::cerr << "Could not listen on port " << cfg_.port << "\n";
            return 1;
        }

        if (!cfg_.quiet) {
            std::cout << "AsterForge Observatory " << kVersion << " at http://localhost:"
                      << cfg_.port << "\n";
            std::cout << "Serving " << cfg_.public_dir << " with " << cfg_.threads
                      << " threads\n";
        }

        hub_.start(started_);

        while (g_running.load()) {
            pollfd item{};
            item.fd = listen_fd.get();
            item.events = POLLIN;
            const int ready = ::poll(&item, 1, 250);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (g_running.load()) {
                    std::cerr << "Poll failed\n";
                }
                break;
            }
            if (ready == 0 || !(item.revents & POLLIN)) {
                continue;
            }

            sockaddr_in client_address{};
            socklen_t client_len = sizeof(client_address);
            const int client = accept(listen_fd.get(), reinterpret_cast<sockaddr*>(&client_address),
                                      &client_len);
            if (client < 0) {
                continue;
            }

            char ip[INET_ADDRSTRLEN] = "0.0.0.0";
            inet_ntop(AF_INET, &client_address.sin_addr, ip, sizeof(ip));
            const std::string ip_str = ip;

            if (!pool_.submit([this, client, ip_str] { handle_client(client, ip_str); })) {
                UniqueFd fd(client);
                const Request dummy;
                auto response = service_unavailable();
                send_all(fd.get(), serialize_response(dummy, response, false));
            }
        }

        g_running.store(false);
        hub_.stop();
        pool_.stop();
        return 0;
    }

private:
    long long uptime_seconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - started_)
            .count();
    }

    void configure_client(int fd) const {
        const timeval timeout{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        int nodelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    }

    void handle_client(int raw_fd, std::string ip) {
        UniqueFd fd(raw_fd);
        configure_client(fd.get());

        std::string buffer;
        int served = 0;
        const int max_keepalive = 64;
        const std::size_t max_headers = 64 * 1024;

        while (g_running.load() && served < max_keepalive) {
            const bool idle = buffer.empty();
            while (buffer.find("\r\n\r\n") == std::string::npos && g_running.load()) {
                if (buffer.size() > max_headers) {
                    finish(fd.get(), Request{}, headers_too_large(), ip, false);
                    return;
                }
                char chunk[4096];
                const ssize_t received = ::recv(fd.get(), chunk, sizeof(chunk), 0);
                if (received < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
                        if (idle && buffer.empty()) {
                            return;
                        }
                        finish(fd.get(), Request{}, request_timeout(), ip, false);
                    }
                    return;
                }
                if (received == 0) {
                    return;
                }
                buffer.append(chunk, static_cast<std::size_t>(received));
            }
            if (!g_running.load()) {
                return;
            }

            const ParseOutcome parsed = try_parse_request(buffer, max_headers, cfg_.max_body);
            if (parsed.status == ParseStatus::incomplete) {
                char chunk[4096];
                const ssize_t received = ::recv(fd.get(), chunk, sizeof(chunk), 0);
                if (received < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
                        finish(fd.get(), Request{}, request_timeout(), ip, false);
                    }
                    return;
                }
                if (received == 0) {
                    return;
                }
                buffer.append(chunk, static_cast<std::size_t>(received));
                continue;
            }
            if (parsed.status == ParseStatus::headers_too_large) {
                finish(fd.get(), Request{}, headers_too_large(), ip, false);
                return;
            }
            if (parsed.status == ParseStatus::body_too_large) {
                finish(fd.get(), parsed.request, payload_too_large(), ip, false);
                return;
            }
            if (parsed.status == ParseStatus::bad) {
                finish(fd.get(), Request{}, bad_request("Malformed request."), ip, false);
                return;
            }

            Request request = parsed.request;
            request.ip = ip;
            buffer.erase(0, parsed.consumed);

            const auto t0 = std::chrono::steady_clock::now();
            if (!limiter_.allow(ip)) {
                const Response limited = too_many_requests();
                const bool keep = !wants_close(request) && !limited.close;
                finish(fd.get(), request, limited, ip, keep, t0);
                if (!keep) {
                    return;
                }
                ++served;
                continue;
            }

            Response response;
            try {
                response = route(request, fd.get());
            } catch (...) {
                response = internal_error();
            }

            if (response.take_socket) {
                fd.release();
                const auto t1 = std::chrono::steady_clock::now();
                const double ms =
                    std::chrono::duration<double, std::milli>(t1 - t0).count();
                metrics_.record(request.path, 200, ms);
                log_.write(ip, request.method, request.target, 200, 0, ms);
                return;
            }

            const bool keep = !wants_close(request) && !response.close && served + 1 < max_keepalive;
            finish(fd.get(), request, response, ip, keep, t0);
            if (!keep) {
                return;
            }
            ++served;
        }
    }

    void finish(int fd, const Request& request, Response response, const std::string& ip,
                bool keep_alive,
                std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now()) {
        if (!keep_alive) {
            response.close = true;
        }
        const std::string payload = serialize_response(request, response, keep_alive);
        send_all(fd, payload);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count();
        const std::string path = request.path.empty() ? "-" : request.path;
        metrics_.record(path, response.status, ms);
        log_.write(ip, request.method.empty() ? "-" : request.method,
                   request.target.empty() ? path : request.target, response.status,
                   response.body.size(), ms);
    }

    Response route(Request& request, int fd) {
        if (request.method == "OPTIONS") {
            return cors_preflight();
        }

        if (request.path == "/api/echo") {
            if (request.method != "POST") {
                return method_not_allowed("POST, OPTIONS");
            }
            std::string body = request.body.empty() ? "{}" : request.body;
            return json_ok(body);
        }

        if (request.path == "/api/stream") {
            if (request.method == "HEAD") {
                Response response;
                response.content_type = "text/event-stream";
                response.extra_headers.emplace_back("Cache-Control", "no-store");
                return response;
            }
            if (request.method != "GET") {
                return method_not_allowed("GET, HEAD, OPTIONS");
            }
            return start_stream(fd);
        }

        const bool read = request.method == "GET" || request.method == "HEAD";
        if (starts_with(request.path, "/api/")) {
            if (!read) {
                return method_not_allowed("GET, HEAD, OPTIONS");
            }
            if (request.path == "/api/health") {
                return json_ok(build_health_json(uptime_seconds(), metrics_.request_count()));
            }
            if (request.path == "/api/version") {
                return json_ok(build_version_json());
            }
            if (request.path == "/api/presets") {
                return json_ok(build_presets_json());
            }
            if (request.path == "/api/mission") {
                return json_ok(build_mission_json(request.query));
            }
            if (request.path == "/api/share") {
                return json_ok(build_share_json(request.query));
            }
            if (request.path == "/api/sky") {
                return json_ok(build_sky_json(request.query));
            }
            if (request.path == "/api/orbit") {
                return json_ok(build_orbit_json(request.query));
            }
            if (request.path == "/api/constellation") {
                return json_ok(build_constellation_json(request.query));
            }
            if (request.path == "/api/catalog") {
                return json_ok(build_catalog_json());
            }
            if (request.path == "/api/metrics") {
                return json_ok(metrics_.to_json());
            }
            return not_found();
        }

        if (!read) {
            return method_not_allowed("GET, HEAD, OPTIONS");
        }
        return serve_static(request);
    }

    Response start_stream(int fd) {
        if (!hub_.try_reserve()) {
            return service_unavailable();
        }
        Response headers;
        headers.content_type = "text/event-stream";
        headers.extra_headers.emplace_back("Cache-Control", "no-store");
        headers.extra_headers.emplace_back("X-Accel-Buffering", "no");
        headers.close = true;
        Request dummy;
        dummy.method = "GET";
        if (!send_all(fd, serialize_response(dummy, headers, false))) {
            hub_.cancel_reserve();
            Response failed = internal_error();
            failed.close = true;
            return failed;
        }
        if (!hub_.attach(fd)) {
            UniqueFd closer(fd);
            Response taken;
            taken.take_socket = true;
            taken.status = 200;
            return taken;
        }
        Response taken;
        taken.take_socket = true;
        taken.status = 200;
        return taken;
    }

    Response serve_static(const Request& request) const {
        if (path_has_dotdot(request.path) || request.path.find('\0') != std::string::npos) {
            return bad_request("Path traversal is not allowed.");
        }

        std::string relative = request.path == "/" ? "/index.html" : request.path;
        while (!relative.empty() && relative.front() == '/') {
            relative.erase(relative.begin());
        }
        if (relative.empty()) {
            relative = "index.html";
        }

        fs::path file_path = cfg_.public_dir / relative;
        if (!path_is_inside(cfg_.public_dir, file_path)) {
            return bad_request("Path traversal is not allowed.");
        }

        std::error_code ec;
        if (fs::is_directory(file_path, ec)) {
            file_path /= "index.html";
            if (!path_is_inside(cfg_.public_dir, file_path)) {
                return bad_request("Path traversal is not allowed.");
            }
        }

        struct stat st {};
        if (::stat(file_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            return not_found();
        }

        const auto file_size = static_cast<std::uint64_t>(st.st_size);
        const std::time_t mtime = st.st_mtime;
        const std::string etag = "\"" + hex64(file_size) + "-" +
                                 hex64(static_cast<std::uint64_t>(mtime)) + "\"";
        const std::string last_modified = http_date(mtime);

        const std::string inm = header_get(request, "if-none-match");
        if (!inm.empty()) {
            if (inm == etag || inm == "*" || inm.find(etag) != std::string::npos) {
                Response response;
                response.status = 304;
                response.status_text = "Not Modified";
                response.extra_headers.emplace_back("ETag", etag);
                response.extra_headers.emplace_back("Last-Modified", last_modified);
                response.extra_headers.emplace_back("Cache-Control", "public, max-age=300");
                return response;
            }
        }
        const std::string ims = header_get(request, "if-modified-since");
        if (!ims.empty() && inm.empty()) {
            std::time_t since = 0;
            if (parse_http_date(ims, since) && mtime <= since) {
                Response response;
                response.status = 304;
                response.status_text = "Not Modified";
                response.extra_headers.emplace_back("ETag", etag);
                response.extra_headers.emplace_back("Last-Modified", last_modified);
                response.extra_headers.emplace_back("Cache-Control", "public, max-age=300");
                return response;
            }
        }

        std::ifstream input(file_path, std::ios::binary);
        if (!input) {
            return not_found();
        }
        std::string body;
        body.resize(static_cast<std::size_t>(file_size));
        if (file_size > 0) {
            input.read(&body[0], static_cast<std::streamsize>(file_size));
            body.resize(static_cast<std::size_t>(input.gcount()));
        }

        Response response;
        response.content_type = mime_type(file_path);
        response.extra_headers.emplace_back("ETag", etag);
        response.extra_headers.emplace_back("Last-Modified", last_modified);
        response.extra_headers.emplace_back("Cache-Control", "public, max-age=300");
        response.extra_headers.emplace_back("Accept-Ranges", "bytes");

        const std::string range_header = header_get(request, "range");
        if (!range_header.empty() && header_get(request, "if-range").empty()) {
            const ByteRange range =
                parse_byte_range(range_header, static_cast<std::uint64_t>(body.size()));
            if (range.status == ByteRange::Status::invalid) {
                return bad_request("Invalid Range header.");
            }
            if (range.status == ByteRange::Status::unsatisfiable) {
                Response unsat;
                unsat.status = 416;
                unsat.status_text = "Range Not Satisfiable";
                unsat.content_type = response.content_type;
                unsat.extra_headers = response.extra_headers;
                unsat.extra_headers.emplace_back(
                    "Content-Range", "bytes */" + std::to_string(body.size()));
                return unsat;
            }
            if (range.status == ByteRange::Status::ok) {
                const std::size_t begin = static_cast<std::size_t>(range.start);
                const std::size_t end = static_cast<std::size_t>(range.end);
                response.status = 206;
                response.status_text = "Partial Content";
                response.body = body.substr(begin, end - begin + 1);
                response.extra_headers.emplace_back(
                    "Content-Range", "bytes " + std::to_string(range.start) + "-" +
                                         std::to_string(range.end) + "/" +
                                         std::to_string(body.size()));
                return response;
            }
        }

        response.body = std::move(body);
        return response;
    }

    ServerConfig cfg_;
    Metrics metrics_;
    RateLimiter limiter_;
    AccessLog log_;
    SseHub hub_;
    ThreadPool pool_;
    std::chrono::steady_clock::time_point started_;
};

}  // namespace aster
