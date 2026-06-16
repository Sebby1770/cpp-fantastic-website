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

std::string url_encode(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return out.str();
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

struct Preset {
    std::string id;
    std::string label;
    std::string description;
    std::string seed;
    int intensity;
    int tempo;
    int density;
};

const std::vector<Preset>& presets() {
    static const std::vector<Preset> values = {
        {"orbit", "Orbit", "Balanced routes with readable movement.", "sebby-orbit", 68, 54, 72},
        {"bloom", "Bloom", "Dense color, high energy, and wider clusters.", "bloom-signal", 82, 48, 88},
        {"forge", "Forge", "Sharper tension with faster route pressure.", "forge-line", 76, 74, 66},
        {"night", "Night", "Slower motion and sparse late-session focus.", "night-map", 46, 34, 42},
    };
    return values;
}

bool is_supported_mode(const std::string& mode) {
    const auto& values = presets();
    return std::any_of(values.begin(), values.end(), [&](const Preset& preset) {
        return preset.id == mode;
    });
}

std::string normalize_mode(const std::string& value) {
    const std::string mode = to_lower(value);
    return is_supported_mode(mode) ? mode : "orbit";
}

std::string short_id(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << stable_seed(value);
    return out.str();
}

std::string build_presets_json() {
    std::ostringstream json;
    json << "{\"presets\":[";
    const auto& values = presets();
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto& preset = values[i];
        if (i) json << ",";
        json << "{";
        json << "\"id\":\"" << json_escape(preset.id) << "\",";
        json << "\"label\":\"" << json_escape(preset.label) << "\",";
        json << "\"description\":\"" << json_escape(preset.description) << "\",";
        json << "\"seed\":\"" << json_escape(preset.seed) << "\",";
        json << "\"intensity\":" << preset.intensity << ",";
        json << "\"tempo\":" << preset.tempo << ",";
        json << "\"density\":" << preset.density;
        json << "}";
    }
    json << "]}";
    return json.str();
}

std::string build_share_json(const std::map<std::string, std::string>& query) {
    const std::string seed_text = string_param(query, "seed", "sebby");
    const std::string mode = normalize_mode(string_param(query, "mode", "orbit"));
    const int intensity = int_param(query, "intensity", 68, 1, 100);
    const int tempo = int_param(query, "tempo", 54, 1, 100);
    const int density = int_param(query, "density", 72, 1, 100);
    const std::string id = short_id(seed_text + ":" + mode + ":" + std::to_string(intensity) + ":" +
                                   std::to_string(tempo) + ":" + std::to_string(density));

    std::ostringstream path;
    path << "/?seed=" << url_encode(seed_text)
         << "&mode=" << url_encode(mode)
         << "&intensity=" << intensity
         << "&tempo=" << tempo
         << "&density=" << density;

    std::ostringstream json;
    json << "{";
    json << "\"shortId\":\"" << id << "\",";
    json << "\"path\":\"" << json_escape(path.str()) << "\",";
    json << "\"config\":{";
    json << "\"seed\":\"" << json_escape(seed_text) << "\",";
    json << "\"mode\":\"" << json_escape(mode) << "\",";
    json << "\"intensity\":" << intensity << ",";
    json << "\"tempo\":" << tempo << ",";
    json << "\"density\":" << density;
    json << "}}";
    return json.str();
}

std::string build_mission_json(const std::map<std::string, std::string>& query) {
    const std::string seed_text = string_param(query, "seed", "sebby");
    const std::string mode = normalize_mode(string_param(query, "mode", "orbit"));
    const int intensity = int_param(query, "intensity", 68, 1, 100);
    const int tempo = int_param(query, "tempo", 54, 1, 100);
    const int density = int_param(query, "density", 72, 1, 100);
    const std::string mission_id = short_id(seed_text + ":" + mode + ":" + std::to_string(intensity) + ":" +
                                            std::to_string(tempo) + ":" + std::to_string(density));

    std::mt19937 rng(stable_seed(seed_text + ":" + mode + ":" + std::to_string(intensity) + ":" +
                                 std::to_string(tempo) + ":" + std::to_string(density)));
    auto pick = [&](const std::vector<std::string>& values) -> std::string {
        std::uniform_int_distribution<std::size_t> dist(0, values.size() - 1);
        return values[dist(rng)];
    };
    auto metric = [&](int base) {
        std::uniform_int_distribution<int> dist(-9, 14);
        return std::clamp(base + dist(rng), 1, 99);
    };

    const std::vector<std::string> prefixes = {
        "Velvet", "Copper", "Solar", "Nocturne", "Meridian", "Echo", "Glass", "Kinetic", "Civic", "Prism"
    };
    const std::vector<std::string> nouns = {
        "Observatory", "Relay", "Garden", "Cartograph", "Engine", "Harbor", "Foundry", "Signal", "Atelier", "Beacon"
    };
    const std::vector<std::string> taglines = {
        "A living map for turning scattered sparks into a launchable shape.",
        "A quiet command surface for seeing momentum before it becomes obvious.",
        "A cinematic forge for tracing the route from instinct to shipped work.",
        "A signal garden where ideas, risks, and next moves move in one rhythm.",
        "A native C++ atlas that makes the invisible structure of a project glow."
    };
    std::vector<std::string> priorities = {
        "Prototype the interaction that makes the whole idea feel inevitable",
        "Name the one metric that proves the signal is real",
        "Polish the path from first touch to visible result",
        "Keep the surface dense, calm, and quick to scan",
        "Ship a tiny loop that feels complete in the hand",
        "Use motion only where it clarifies state",
        "Turn the riskiest assumption into a small live test",
        "Make the strongest visual moment carry useful information"
    };
    std::vector<std::string> notes = {
        "The center cluster is strong enough to become the first demo moment.",
        "The route map favors one decisive release over a wide feature spread.",
        "Momentum rises when the interface gives immediate visual feedback.",
        "The quietest risk is discoverability; make the first action unmistakable.",
        "There is enough visual identity here to support a memorable product name.",
        "The densest nodes should become the first three build tickets."
    };
    const std::vector<std::string> stages = {"Spark", "Shape", "Wire", "Stress", "Reveal", "Launch", "Echo"};
    const std::vector<std::string> palette_names = {"emberglass", "tidewire", "citrus-noir", "violet-oxide"};
    const std::vector<std::vector<std::string>> palettes = {
        {"#080908", "#f7f0df", "#10b8a6", "#ef5e4d", "#f2b544", "#8c6cf5", "#77c66e"},
        {"#071013", "#edf7f2", "#1f8fb3", "#ff6b57", "#d9b847", "#4bbf83", "#d46fb0"},
        {"#0b0b10", "#fff4ce", "#95d839", "#ff5d35", "#49a7ff", "#b877ff", "#f1c232"},
        {"#100c13", "#f3efe7", "#b888ff", "#d85f7d", "#43c6a8", "#f0aa3b", "#6ea8fe"}
    };
    const std::vector<std::string> weather = {
        "clear signal", "charged air", "useful tension", "clean pressure", "late-night focus", "bright friction"
    };

    std::ostringstream json;
    json << "{";
    json << "\"app\":\"AsterForge\",";
    json << "\"shortId\":\"" << mission_id << "\",";
    json << "\"seed\":\"" << json_escape(seed_text) << "\",";
    json << "\"mode\":\"" << json_escape(mode) << "\",";
    json << "\"intensity\":" << intensity << ",";
    json << "\"tempo\":" << tempo << ",";
    json << "\"density\":" << density << ",";
    json << "\"updatedAt\":\"" << current_time_iso() << "\",";
    json << "\"missionName\":\"" << pick(prefixes) << " " << pick(nouns) << "\",";
    json << "\"tagline\":\"" << json_escape(pick(taglines)) << "\",";
    json << "\"weather\":\"" << json_escape(pick(weather)) << "\",";

    json << "\"metrics\":[";
    const std::vector<std::pair<std::string, int>> metrics = {
        {"Velocity", metric(44 + intensity / 2)},
        {"Clarity", metric(50 + tempo / 3)},
        {"Wonder", metric(48 + (intensity + density) / 5)},
        {"Tension", metric(30 + (100 - tempo) / 4)},
        {"Finish", metric(38 + (tempo + density) / 5)}
    };
    for (std::size_t i = 0; i < metrics.size(); ++i) {
        if (i) json << ",";
        json << "{\"label\":\"" << metrics[i].first << "\",\"value\":" << metrics[i].second << ",\"unit\":\"%\"}";
    }
    json << "],";

    json << "\"priorities\":[";
    std::shuffle(priorities.begin(), priorities.end(), rng);
    for (int i = 0; i < 5; ++i) {
        if (i) json << ",";
        json << "\"" << json_escape(priorities[static_cast<std::size_t>(i)]) << "\"";
    }
    json << "],";

    json << "\"waypoints\":[";
    for (std::size_t i = 0; i < stages.size(); ++i) {
        if (i) json << ",";
        const int score = metric(42 + static_cast<int>(i) * 7 + intensity / 9);
        const int minutes = 9 + static_cast<int>(i) * 6 + tempo / 10;
        json << "{\"label\":\"" << stages[i] << "\",\"minutes\":" << minutes
             << ",\"score\":" << score << "}";
    }
    json << "],";

    json << "\"notes\":[";
    std::shuffle(notes.begin(), notes.end(), rng);
    for (int i = 0; i < 3; ++i) {
        if (i) json << ",";
        json << "\"" << json_escape(notes[static_cast<std::size_t>(i)]) << "\"";
    }
    json << "],";

    json << "\"palette\":[";
    const int palette_index = static_cast<int>(stable_seed(mode + seed_text) % palettes.size());
    const auto& palette = palettes[static_cast<std::size_t>(palette_index)];
    for (std::size_t i = 0; i < palette.size(); ++i) {
        if (i) json << ",";
        json << "\"" << palette[i] << "\"";
    }
    json << "],";
    json << "\"paletteName\":\"" << palette_names[static_cast<std::size_t>(palette_index)] << "\",";

    json << "\"nodes\":[";
    std::uniform_real_distribution<double> pos(0.08, 0.92);
    std::uniform_real_distribution<double> phase(0.0, 6.2832);
    std::uniform_int_distribution<int> energy(28, 99);
    std::uniform_int_distribution<int> node_type(0, 3);
    const int node_count = 20 + density / 5 + intensity / 12;
    for (int i = 0; i < node_count; ++i) {
        if (i) json << ",";
        json << std::fixed << std::setprecision(4);
        json << "{\"x\":" << pos(rng)
             << ",\"y\":" << pos(rng)
             << ",\"size\":" << (3 + energy(rng) % 8)
             << ",\"energy\":" << energy(rng)
             << ",\"phase\":" << phase(rng)
             << ",\"kind\":" << node_type(rng) << "}";
    }
    json << "],";

    json << "\"links\":[";
    const int link_count = node_count + density / 3;
    for (int i = 0; i < link_count; ++i) {
        if (i) json << ",";
        const int start = static_cast<int>(rng() % node_count);
        const int jump = 1 + static_cast<int>(rng() % 7);
        const int strength = 24 + static_cast<int>(rng() % 76);
        json << "[" << start << "," << ((start + jump) % node_count) << "," << strength << "]";
    }
    json << "],";

    json << "\"rings\":[";
    for (int i = 0; i < 5; ++i) {
        if (i) json << ",";
        json << std::fixed << std::setprecision(4);
        json << "{\"x\":" << pos(rng)
             << ",\"y\":" << pos(rng)
             << ",\"radius\":" << (0.16 + 0.05 * i + (density % 8) / 100.0)
             << ",\"speed\":" << (0.18 + (rng() % 60) / 100.0)
             << ",\"color\":" << (2 + i % 5) << "}";
    }
    json << "],";

    json << "\"signature\":\"" << json_escape(seed_text + " / " + mode + " / " + pick(weather)) << "\"";
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
        const bool send_body = request.method != "HEAD";

        std::ostringstream payload;
        payload << "HTTP/1.1 " << response.status << " " << response.status_text << "\r\n";
        payload << "Content-Type: " << response.content_type << "\r\n";
        payload << "Content-Length: " << response.body.size() << "\r\n";
        payload << "Cache-Control: no-store\r\n";
        payload << "X-Content-Type-Options: nosniff\r\n";
        payload << "Referrer-Policy: no-referrer\r\n";
        payload << "Connection: close\r\n";
        payload << "\r\n";
        if (send_body) {
            payload << response.body;
        }

        send_all(client_fd, payload.str());
        close(client_fd);
    }

    Response route(const Request& request) const {
        if (request.method != "GET" && request.method != "HEAD") {
            return bad_request("Only GET and HEAD requests are supported.");
        }

        if (request.path == "/api/health") {
            return json_response("{\"status\":\"ok\",\"service\":\"AsterForge\",\"language\":\"C++17\",\"endpoints\":[\"/api/health\",\"/api/mission\",\"/api/presets\",\"/api/share\"]}");
        }

        if (request.path == "/api/mission") {
            return json_response(build_mission_json(request.query));
        }

        if (request.path == "/api/presets") {
            return json_response(build_presets_json());
        }

        if (request.path == "/api/share") {
            return json_response(build_share_json(request.query));
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

        fs::path file_path = fs::weakly_canonical(public_dir_ / relative);
        const fs::path root_path = fs::weakly_canonical(public_dir_);
        const std::string root = root_path.string();
        const std::string candidate = file_path.string();
        const bool inside_root = candidate == root ||
                                 (candidate.rfind(root, 0) == 0 &&
                                  candidate.size() > root.size() &&
                                  candidate[root.size()] == fs::path::preferred_separator);
        if (!inside_root) {
            return bad_request("Path traversal is not allowed.");
        }

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
