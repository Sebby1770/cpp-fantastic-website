#include "server.hpp"

#include <atomic>
#include <csignal>
#include <iostream>

namespace {
std::atomic<bool> g_running{true};

void handle_signal(int) {
    g_running = false;
}
}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        const aster::CliOptions options = aster::parse_cli(argc, argv);
        aster::Server server(options.port, aster::find_public_dir(), options.quiet);
        return server.run(g_running);
    } catch (const std::exception& error) {
        std::cerr << "AsterForge failed: " << error.what() << "\n";
        return 1;
    }
}
