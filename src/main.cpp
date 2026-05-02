#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_running{true};

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::map<std::string, std::string> query;
};

struct Response {
    int status = 200;
    std::string status_text = "OK";
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
};

void handle_signal(int) {
    g_running = false;
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string url_decode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            char* end = nullptr;
            const long decoded = std::strtol(hex.c_str(), &end, 16);
            if (end != nullptr && *end == '\0') {
                out.push_back(static_cast<char>(decoded));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return out;
}

std::string json_escape(const std::string& value) {
    std::ostringstream escaped;
    for (const char ch : value) {
        switch (ch) {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(static_cast<unsigned char>(ch));
                } else {
                    escaped << ch;
                }
        }
    }
    return escaped.str();
}

std::map<std::string, std::string> parse_query(const std::string& query_string) {
    std::map<std::string, std::string> params;
    std::size_t start = 0;
    while (start <= query_string.size()) {
        const std::size_t end = query_string.find('&', start);
        const std::string pair = query_string.substr(start, end == std::string::npos ? end : end - start);
        if (!pair.empty()) {
            const std::size_t equals = pair.find('=');
            const std::string key = url_decode(pair.substr(0, equals));
            const std::string value = equals == std::string::npos ? "" : url_decode(pair.substr(equals + 1));
            params[key] = value;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return params;
}

Request parse_request(const std::string& raw) {
    std::istringstream stream(raw);
    Request request;
    std::string version;
    stream >> request.method >> request.target >> version;

    const std::size_t query_start = request.target.find('?');
    request.path = url_decode(request.target.substr(0, query_start));
    if (query_start != std::string::npos) {
        request.query = parse_query(request.target.substr(query_start + 1));
    }
    if (request.path.empty()) {
        request.path = "/";
    }
    return request;
}

std::string mime_type(const fs::path& path) {
    const std::string ext = to_lower(path.extension().string());
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool send_all(int socket_fd, const std::string& payload) {
    const char* data = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const ssize_t sent = send(socket_fd, data, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

int int_param(const std::map<std::string, std::string>& query, const std::string& key, int fallback, int min, int max) {
    const auto found = query.find(key);
    if (found == query.end()) {
        return fallback;
    }
    try {
        return std::clamp(std::stoi(found->second), min, max);
    } catch (...) {
        return fallback;
    }
}

std::string string_param(const std::map<std::string, std::string>& query, const std::string& key, const std::string& fallback) {
    const auto found = query.find(key);
    if (found == query.end() || found->second.empty()) {
        return fallback;
    }
    return found->second.substr(0, 64);
}

uint32_t stable_seed(const std::string& value) {
    uint32_t hash = 2166136261u;
    for (const unsigned char ch : value) {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

std::string current_time_iso() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string build_mission_json(const std::map<std::string, std::string>& query) {
    const std::string seed_text = string_param(query, "seed", "sebby");
    const std::string mode = string_param(query, "mode", "pulse");
    const int intensity = int_param(query, "intensity", 68, 1, 100);
    const int tempo = int_param(query, "tempo", 42, 1, 100);

    std::mt19937 rng(stable_seed(seed_text + ":" + mode + ":" + std::to_string(intensity) + ":" + std::to_string(tempo)));
    auto pick = [&](const std::vector<std::string>& values) -> std::string {
        std::uniform_int_distribution<std::size_t> dist(0, values.size() - 1);
        return values[dist(rng)];
    };
    auto metric = [&](int base) {
        std::uniform_int_distribution<int> dist(-9, 14);
        return std::clamp(base + dist(rng), 1, 99);
    };

    const std::vector<std::string> prefixes = {"Aurora", "Vector", "Signal", "Lumen", "Civic", "Keystone", "Nova", "Harbor"};
    const std::vector<std::string> nouns = {"Circuit", "Studio", "Atlas", "Engine", "Desk", "Forge", "Field", "Pulse"};
    const std::vector<std::string> taglines = {
        "Turn rough sparks into a focused launch board.",
        "Shape a crisp interface around messy momentum.",
        "Make the next move visible, measurable, and satisfying.",
        "Blend strategy, rhythm, and craft into one working surface."
    };
    std::vector<std::string> priorities = {
        "Prototype the most useful interaction first",
        "Name the one metric that proves traction",
        "Polish the path from idea to visible result",
        "Keep the dashboard dense, calm, and quick to scan",
        "Ship a tiny loop that feels complete",
        "Use motion only where it clarifies state"
    };
    const std::vector<std::string> stages = {"Map", "Focus", "Build", "Tune", "Launch", "Learn"};
    const std::vector<std::string> palettes = {
        "#171717", "#f7f2e8", "#247c76", "#d85d4c", "#c79a34", "#6e62a6",
        "#222226", "#f4efe3", "#2f6f9f", "#cf5b39", "#8fa34a", "#7b4f87",
        "#161616", "#fbfaf6", "#18706a", "#b94f5f", "#d0a13f", "#476b9b"
    };

    std::ostringstream json;
    json << "{";
    json << "\"app\":\"AsterForge\",";
    json << "\"seed\":\"" << json_escape(seed_text) << "\",";
    json << "\"mode\":\"" << json_escape(mode) << "\",";
    json << "\"intensity\":" << intensity << ",";
    json << "\"tempo\":" << tempo << ",";
    json << "\"updatedAt\":\"" << current_time_iso() << "\",";
    json << "\"missionName\":\"" << pick(prefixes) << " " << pick(nouns) << "\",";
    json << "\"tagline\":\"" << json_escape(pick(taglines)) << "\",";

    json << "\"metrics\":[";
    const std::vector<std::pair<std::string, int>> metrics = {
        {"Momentum", metric(58 + intensity / 3)},
        {"Clarity", metric(54 + tempo / 4)},
        {"Delight", metric(62 + (intensity + tempo) / 8)},
        {"Risk", metric(34 + (100 - tempo) / 5)}
    };
    for (std::size_t i = 0; i < metrics.size(); ++i) {
        if (i) json << ",";
        json << "{\"label\":\"" << metrics[i].first << "\",\"value\":" << metrics[i].second << ",\"unit\":\"%\"}";
    }
    json << "],";

    json << "\"priorities\":[";
    std::shuffle(priorities.begin(), priorities.end(), rng);
    for (int i = 0; i < 4; ++i) {
        if (i) json << ",";
        json << "\"" << json_escape(priorities[static_cast<std::size_t>(i)]) << "\"";
    }
    json << "],";

    json << "\"waypoints\":[";
    for (std::size_t i = 0; i < stages.size(); ++i) {
        if (i) json << ",";
        const int score = metric(48 + static_cast<int>(i) * 6 + intensity / 8);
        const int minutes = 12 + static_cast<int>(i) * 7 + tempo / 9;
        json << "{\"label\":\"" << stages[i] << "\",\"minutes\":" << minutes << ",\"score\":" << score << "}";
    }
    json << "],";

    json << "\"palette\":[";
    const int palette_offset = static_cast<int>(stable_seed(mode) % 3) * 6;
    for (int i = 0; i < 6; ++i) {
        if (i) json << ",";
        json << "\"" << palettes[static_cast<std::size_t>(palette_offset + i)] << "\"";
    }
    json << "],";

    json << "\"nodes\":[";
    std::uniform_real_distribution<double> pos(0.08, 0.92);
    std::uniform_int_distribution<int> energy(28, 99);
    const int node_count = 18 + intensity / 8;
    for (int i = 0; i < node_count; ++i) {
        if (i) json << ",";
        json << std::fixed << std::setprecision(4);
        json << "{\"x\":" << pos(rng)
             << ",\"y\":" << pos(rng)
             << ",\"size\":" << (3 + energy(rng) % 8)
             << ",\"energy\":" << energy(rng) << "}";
    }
    json << "],";

    json << "\"links\":[";
    for (int i = 0; i < node_count - 1; ++i) {
        if (i) json << ",";
        const int jump = 1 + static_cast<int>(rng() % 4);
        json << "[" << i << "," << ((i + jump) % node_count) << "]";
    }
    json << "]";
    json << "}";
    return json.str();
}

Response json_response(const std::string& body) {
    return Response{200, "OK", "application/json; charset=utf-8", body};
}

Response not_found() {
    return Response{404, "Not Found", "text/plain; charset=utf-8", "404 Not Found\n"};
}

Response bad_request(const std::string& message) {
    return Response{400, "Bad Request", "text/plain; charset=utf-8", message + "\n"};
}

class Server {
public:
    Server(int port, fs::path public_dir) : port_(port), public_dir_(std::move(public_dir)) {}

    int run() {
        const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "Could not create socket\n";
            return 1;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(static_cast<uint16_t>(port_));

        if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            std::cerr << "Could not bind to port " << port_ << "\n";
            close(server_fd);
            return 1;
        }

        if (listen(server_fd, 16) < 0) {
            std::cerr << "Could not listen on port " << port_ << "\n";
            close(server_fd);
            return 1;
        }

        std::cout << "AsterForge running at http://localhost:" << port_ << "\n";
        std::cout << "Serving " << public_dir_ << "\n";

        while (g_running) {
            sockaddr_in client_address{};
            socklen_t client_len = sizeof(client_address);
            const int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_address), &client_len);
            if (client_fd < 0) {
                if (g_running) {
                    std::cerr << "Accept failed\n";
                }
                continue;
            }
            std::thread(&Server::handle_client, this, client_fd).detach();
        }

        close(server_fd);
        return 0;
    }

private:
    void handle_client(int client_fd) const {
        std::string raw;
        char buffer[4096];
        while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 32768) {
            const ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                close(client_fd);
                return;
            }
            raw.append(buffer, static_cast<std::size_t>(received));
        }

        const Request request = parse_request(raw);
        const Response response = route(request);

        std::ostringstream payload;
        payload << "HTTP/1.1 " << response.status << " " << response.status_text << "\r\n";
        payload << "Content-Type: " << response.content_type << "\r\n";
        payload << "Content-Length: " << response.body.size() << "\r\n";
        payload << "Cache-Control: no-store\r\n";
        payload << "Connection: close\r\n";
        payload << "\r\n";
        payload << response.body;

        send_all(client_fd, payload.str());
        close(client_fd);
    }

    Response route(const Request& request) const {
        if (request.method != "GET") {
            return bad_request("Only GET requests are supported.");
        }

        if (request.path == "/api/health") {
            return json_response("{\"status\":\"ok\",\"service\":\"AsterForge\",\"language\":\"C++17\"}");
        }

        if (request.path == "/api/mission") {
            return json_response(build_mission_json(request.query));
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

        const std::string body = read_file(file_path);
        if (body.empty() && !fs::exists(file_path)) {
            return not_found();
        }
        return Response{200, "OK", mime_type(file_path), body};
    }

    int port_;
    fs::path public_dir_;
};

fs::path find_public_dir() {
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

int parse_port(int argc, char** argv) {
    int port = 8080;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
    }
    return std::clamp(port, 1024, 65535);
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        Server server(parse_port(argc, argv), find_public_dir());
        return server.run();
    } catch (const std::exception& error) {
        std::cerr << "AsterForge failed: " << error.what() << "\n";
        return 1;
    }
}
