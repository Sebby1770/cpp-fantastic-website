#include "server.hpp"

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

namespace {

void handle_signal(int) { aster::request_stop(); }

void print_help() {
    std::cout
        << "AsterForge Observatory 3.0\n"
        << "Usage: cpp_fantastic_website [options]\n"
        << "  --port N          Listen port (1024-65535, default 8080)\n"
        << "  --threads N       Worker threads (2-32, default hardware concurrency)\n"
        << "  --max-body N      Max POST body bytes (default 1048576)\n"
        << "  --rate-limit N    Per-IP requests per second (0 disables, default 50)\n"
        << "  --log-format F    json or text (default json)\n"
        << "  --quiet           Suppress access log\n"
        << "  --help            Show this help\n";
}

const char* require_value(int argc, char** argv, int& i, const std::string& flag) {
    if (i + 1 >= argc) {
        std::cerr << "Missing value for " << flag << "\n";
        std::exit(2);
    }
    return argv[++i];
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    aster::ServerConfig cfg;
    unsigned hardware = std::thread::hardware_concurrency();
    if (hardware == 0) {
        hardware = 4;
    }
    cfg.threads = aster::clamp_int(static_cast<int>(hardware), 2, 32);

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                print_help();
                return 0;
            }
            if (arg == "--quiet") {
                cfg.quiet = true;
                continue;
            }
            if (arg == "--port") {
                cfg.port = aster::clamp_int(std::stoi(require_value(argc, argv, i, arg)), 1024, 65535);
            } else if (arg == "--threads") {
                cfg.threads = aster::clamp_int(std::stoi(require_value(argc, argv, i, arg)), 2, 32);
            } else if (arg == "--max-body") {
                const long long value = std::stoll(require_value(argc, argv, i, arg));
                cfg.max_body = static_cast<std::size_t>(std::max(1LL, value));
            } else if (arg == "--rate-limit") {
                cfg.rate_limit = std::max(0, std::stoi(require_value(argc, argv, i, arg)));
            } else if (arg == "--log-format") {
                cfg.log_format = aster::to_lower(require_value(argc, argv, i, arg));
                if (cfg.log_format != "json" && cfg.log_format != "text") {
                    std::cerr << "log-format must be json or text\n";
                    return 2;
                }
            } else {
                std::cerr << "Unknown flag: " << arg << "\n";
                print_help();
                return 2;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Invalid argument: " << error.what() << "\n";
        return 2;
    }

    cfg.public_dir = aster::find_public_dir(argc > 0 ? argv[0] : nullptr);

    try {
        aster::Server server(std::move(cfg));
        return server.run();
    } catch (const std::exception& error) {
        std::cerr << "AsterForge failed: " << error.what() << "\n";
        return 1;
    }
}
