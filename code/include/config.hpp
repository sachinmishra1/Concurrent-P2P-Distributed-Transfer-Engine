#pragma once

#include <string>

struct AppConfig {
    std::string torrent_path;
    std::string output_dir = ".";
    std::string log_level = "info";
    int max_peers = 50;
    std::string peer;
    bool show_help = false;
};

// Parse command line arguments.
// Throws std::invalid_argument or std::runtime_error on parsing or validation failure.
AppConfig parse_arguments(int argc, char* argv[]);

// Print program usage options to stdout.
void print_help(const char* program_name);
