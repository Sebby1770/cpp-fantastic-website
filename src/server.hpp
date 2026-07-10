#pragma once

#include "http.hpp"
#include "metrics.hpp"
#include "mission.hpp"
#include "util.hpp"

#include <atomic>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aster {

namespace fs = std::filesystem;

class Server {
public:
    Server(int port, fs::path public_dir, bool quiet)
        : port_(port), public_dir_(std::move(public_dir)), quiet_(quiet) {}

    int run(std::atomic<bool>& running) {
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

        if (::listen(server_fd, 32) < 0) {
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
            std::thread(&Server::handle_client, this, client_fd).detach();
        }

        ::close(server_fd);
        return 0;
    }

private:
    void handle_client(int client_fd) {
        Request request;
        if (!read_http_request(client_fd, request)) {
            ::close(client_fd);
            return;
        }

        Response response = route(request);
        if (request.method == "HEAD") {
            response.include_body = false;
        }

        metrics_.record(request.path);

        if (!quiet_) {
            std::cout << request.method << " " << request.path << " " << response.status << " "
                      << response.body.size() << "\n";
        }

        std::string allow;
        if (response.status == 405) {
            allow = "GET, HEAD, POST, OPTIONS";
        }
        send_response(client_fd, response, allow);
        ::close(client_fd);
    }

    Response route(const Request& request) {
        const std::string& method = request.method;

        if (method == "OPTIONS") {
            Response response{204, "No Content", "text/plain; charset=utf-8", "", true};
            return response;
        }

        if (method != "GET" && method != "HEAD" && method != "POST") {
            return Response{405, "Method Not Allowed", "text/plain; charset=utf-8",
                            "405 Method Not Allowed\n", true};
        }

        if (request.path == "/api/health") {
            if (method == "POST") {
                return Response{405, "Method Not Allowed", "text/plain; charset=utf-8",
                                "405 Method Not Allowed\n", true};
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
                return Response{405, "Method Not Allowed", "text/plain; charset=utf-8",
                                "405 Method Not Allowed\n", true};
            }
            return json_response(build_mission_json(request.query));
        }

        if (request.path == "/api/palettes") {
            if (method == "POST") {
                return Response{405, "Method Not Allowed", "text/plain; charset=utf-8",
                                "405 Method Not Allowed\n", true};
            }
            return json_response(build_palettes_json());
        }

        if (request.path == "/api/constellation") {
            if (method == "POST") {
                return Response{405, "Method Not Allowed", "text/plain; charset=utf-8",
                                "405 Method Not Allowed\n", true};
            }
            return json_response(build_constellation_json(request.query));
        }

        if (request.path == "/api/metrics") {
            if (method == "POST") {
                return Response{405, "Method Not Allowed", "text/plain; charset=utf-8",
                                "405 Method Not Allowed\n", true};
            }
            return json_response(metrics_.to_json());
        }

        if (request.path == "/api/echo") {
            if (method != "POST" && method != "HEAD") {
                // Allow HEAD for probing; GET is not supported for echo body demos.
                if (method == "GET") {
                    return Response{405, "Method Not Allowed", "text/plain; charset=utf-8",
                                    "405 Method Not Allowed\n", true};
                }
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
            return Response{405, "Method Not Allowed", "text/plain; charset=utf-8",
                            "405 Method Not Allowed\n", true};
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
        return Response{200, "OK", mime_type(file_path), body, true};
    }

    int port_;
    fs::path public_dir_;
    bool quiet_;
    Metrics metrics_;
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

struct CliOptions {
    int port = 8080;
    bool quiet = false;
};

inline CliOptions parse_cli(int argc, char** argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            options.port = std::stoi(argv[++i]);
        } else if (arg == "--quiet" || arg == "-q") {
            options.quiet = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "AsterForge " << kVersion << "\n"
                      << "Usage: cpp_fantastic_website [--port N] [--quiet]\n";
            std::exit(0);
        }
    }
    options.port = std::clamp(options.port, 1024, 65535);
    return options;
}

}  // namespace aster
