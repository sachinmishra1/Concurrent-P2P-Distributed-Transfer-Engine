#include "config.hpp"
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <sstream>

AppConfig parse_arguments(int argc, char* argv[]) {
    AppConfig config;
    bool has_torrent_path = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            config.show_help = true;
        } 
        else if (arg.rfind("--output-dir=", 0) == 0) {
            config.output_dir = arg.substr(13);
            if (config.output_dir.empty()) {
                throw std::invalid_argument("Empty value provided for --output-dir");
            }
        } 
        else if (arg.rfind("--log-level=", 0) == 0) {
            std::string val = arg.substr(12);
            if (val != "debug" && val != "info" && val != "warn" && val != "error") {
                throw std::invalid_argument("Invalid log level: " + val + " (must be debug, info, warn, or error)");
            }
            config.log_level = val;
        } 
        else if (arg.rfind("--max-peers=", 0) == 0) {
            std::string val = arg.substr(12);
            if (val.empty()) {
                throw std::invalid_argument("Empty value provided for --max-peers");
            }
            try {
                size_t processed = 0;
                int peers = std::stoi(val, &processed);
                if (processed != val.size() || peers <= 0) {
                    throw std::invalid_argument("Invalid integer value: " + val);
                }
                config.max_peers = peers;
            } catch (...) {
                throw std::invalid_argument("Invalid integer for --max-peers: " + val);
            }
        } 
        else if (arg.rfind("--peer=", 0) == 0) {
            config.peer = arg.substr(7);
            if (config.peer.empty()) {
                throw std::invalid_argument("Empty value provided for --peer");
            }
        }
        else if (arg.rfind("-", 0) == 0) {
            throw std::invalid_argument("Unknown command line option: " + arg);
        } 
        else {
            // Positional argument: torrent file path
            if (has_torrent_path) {
                throw std::invalid_argument("Multiple torrent files specified: " + config.torrent_path + " and " + arg);
            }
            config.torrent_path = arg;
            has_torrent_path = true;
        }
    }

    // Validation
    if (!config.show_help) {
        if (!has_torrent_path) {
            throw std::runtime_error("Missing required argument: <torrent_file>");
        }
        if (!std::filesystem::exists(config.torrent_path)) {
            throw std::runtime_error("Torrent file does not exist: " + config.torrent_path);
        }
    }

    return config;
}

void print_help(const char* program_name) {
    std::cout << "Usage: " << program_name << " <torrent_file> [options]\n\n"
              << "Options:\n"
              << "  --help, -h             Show this help message\n"
              << "  --output-dir=<dir>     Directory to save downloaded files (default: .)\n"
              << "  --log-level=<level>    Logging level: debug, info, warn, error (default: info)\n"
              << "  --max-peers=<n>        Maximum concurrent peer connections (default: 50)\n"
              << "  --peer=<ip>:<port>     Manually specify a peer to connect to directly\n";
}
