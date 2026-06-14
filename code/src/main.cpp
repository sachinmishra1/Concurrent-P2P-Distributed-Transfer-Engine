#include "logger.hpp"
#include "bencode.hpp"
#include "torrent.hpp"
#include "config.hpp"
#include "tracker_client.hpp"
#include "disk_io.hpp"
#include "piece_picker.hpp"
#include "piece_manager.hpp"
#include "peer_manager.hpp"
#include "download_coordinator.hpp"
#include "event_loop.hpp"
#include <functional>
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace {
    volatile std::sig_atomic_t g_shutdown_requested = 0;

    void handle_sigint(int) {
        g_shutdown_requested = 1;
    }

    void welcome_msg() {
        std::cout << "------------------------------------------------\n";
        std::cout << "| Welcome to P2P distributed transfer engine !! |\n";
        std::cout << "------------------------------------------------\n\n";
    }

    uint64_t get_downloaded_bytes(const PieceManager& pm, const TorrentMetadata& meta) {
        uint64_t total = 0;
        for (size_t i = 0; i < static_cast<size_t>(meta.num_pieces); ++i) {
            if (pm.has_piece(i)) {
                if (i == static_cast<size_t>(meta.num_pieces - 1)) {
                    total += static_cast<uint64_t>(meta.total_length - (static_cast<int64_t>(meta.num_pieces - 1) * meta.piece_length));
                } else {
                    total += static_cast<uint64_t>(meta.piece_length);
                }
            }
        }
        return total;
    }
}

int main(int argc, char* argv[]) {
    welcome_msg();

    // 1. Parse CLI arguments
    AppConfig cfg;
    try {
        cfg = parse_arguments(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Command line error: " << e.what() << "\n\n";
        print_help(argv[0]);
        return 1;
    }

    if (cfg.show_help) {
        print_help(argv[0]);
        return 0;
    }

    // Initialize logger and severity level
    init_logger();
    set_log_level(cfg.log_level);

    spdlog::info("Starting P2P distributed transfer engine...");
    spdlog::info("Torrent: {}", cfg.torrent_path);
    spdlog::info("Output Dir: {}", cfg.output_dir);
    spdlog::info("Max Peers: {}", cfg.max_peers);

    // 2. Parse torrent file
    TorrentMetadata metadata;
    try {
        std::ifstream ifs(cfg.torrent_path, std::ios::binary);
        if (!ifs.is_open()) {
            spdlog::error("Failed to open torrent file: {}", cfg.torrent_path);
            return 1;
        }
        std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        metadata = TorrentMetadata::from_bencode(buffer);
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse torrent file: {}", e.what());
        return 1;
    }

    spdlog::info("Parsed torrent successfully:");
    spdlog::info("  Name: {}", metadata.name);
    spdlog::info("  Total Length: {} bytes", metadata.total_length);
    spdlog::info("  Piece Length: {} bytes", metadata.piece_length);
    spdlog::info("  Pieces count: {}", metadata.num_pieces);

    // Generate Azureus-style client peer ID
    std::array<uint8_t, 20> our_peer_id{};
    std::string prefix = "-DT0001-";
    std::copy(prefix.begin(), prefix.end(), our_peer_id.begin());
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    for (size_t i = 8; i < 20; ++i) {
        our_peer_id[i] = static_cast<uint8_t>(alphanum[static_cast<size_t>(std::rand()) % (sizeof(alphanum) - 1)]);
    }

    // 3. Instantiate core components
    EventLoop event_loop;
    TrackerClient tracker_client(metadata, our_peer_id);
    DiskIOManager disk_io(cfg.output_dir, metadata);
    PiecePicker piece_picker(static_cast<size_t>(metadata.num_pieces));
    PieceManager piece_manager(metadata);
    PeerManager peer_manager(event_loop, metadata.info_hash, our_peer_id, static_cast<size_t>(cfg.max_peers));
    DownloadCoordinator coordinator(piece_manager, piece_picker, static_cast<size_t>(metadata.num_pieces));

    // Register peer connection callbacks on peer manager
    peer_manager.on_peer_connected([&](std::shared_ptr<PeerConnection> peer) {
        coordinator.register_peer(peer);
    });

    peer_manager.on_peer_disconnected([&](std::shared_ptr<PeerConnection> peer) {
        coordinator.unregister_peer(peer);
    });

    coordinator.on_peer_corrupted([&](std::shared_ptr<PeerConnection> peer) {
        spdlog::warn("Corruption detected from peer {}:{}. Blacklisting.", peer->ip(), peer->port());
        peer_manager.blacklist_peer(peer->ip(), peer->port());
    });

    // 4. Preallocate output files
    try {
        spdlog::info("Preallocating output files on disk...");
        disk_io.preallocate_files();
    } catch (const std::exception& e) {
        spdlog::error("Disk pre-allocation failed: {}", e.what());
        return 1;
    }

    // 5. Query tracker for peer list
    TrackerResponse tracker_res;
    bool tracker_ok = false;
    try {
        spdlog::info("Contacting tracker: {}", metadata.announce_url);
        tracker_res = tracker_client.announce(6881, 0, 0, static_cast<uint64_t>(metadata.total_length), "started");
        if (!tracker_res.failure_reason.empty()) {
            spdlog::error("Tracker announce failed: {}", tracker_res.failure_reason);
        } else {
            spdlog::info("Tracker announce success. Discovered {} peers.", tracker_res.peers.size());
            tracker_ok = true;
        }
    } catch (const std::exception& e) {
        spdlog::error("Tracker announce failed: {}", e.what());
    }

    if (!tracker_ok && cfg.peer.empty()) {
        spdlog::error("No tracker succeeded and no custom peer was specified. Exiting.");
        return 1;
    }

    // Register SIGINT handler for graceful termination
    std::signal(SIGINT, handle_sigint);

    // Register a timer to periodically check for SIGINT shutdown
    std::function<void()> check_shutdown;
    check_shutdown = [&]() {
        if (g_shutdown_requested) {
            spdlog::info("Graceful shutdown requested. Stopping event loop...");
            event_loop.shutdown();
        } else {
            event_loop.register_timer(std::chrono::milliseconds(250), check_shutdown);
        }
    };
    event_loop.register_timer(std::chrono::milliseconds(250), check_shutdown);

    // Hook piece completion to write validated data to disk
    coordinator.on_piece_completed([&](size_t piece_idx, std::span<const uint8_t> data) {
        try {
            disk_io.write_piece(piece_idx, data);
            spdlog::debug("Saved piece {} to disk successfully.", piece_idx);
        } catch (const std::exception& e) {
            spdlog::error("Failed to write piece {} to disk: {}", piece_idx, e.what());
        }
    });

    // 6. Bootstrap connections by adding discovered peers
    if (tracker_ok) {
        peer_manager.add_peers(tracker_res.peers);
    }

    if (!cfg.peer.empty()) {
        auto colon_pos = cfg.peer.find(':');
        if (colon_pos == std::string::npos) {
            spdlog::error("Invalid custom peer format: {}. Must be ip:port", cfg.peer);
            return 1;
        }
        std::string ip = cfg.peer.substr(0, colon_pos);
        std::string port_str = cfg.peer.substr(colon_pos + 1);
        try {
            int port = std::stoi(port_str);
            if (port <= 0 || port > 65535) {
                throw std::out_of_range("port out of range");
            }
            spdlog::info("Adding manual peer connection: {}:{}", ip, port);
            peer_manager.connect_to_peer(ip, static_cast<uint16_t>(port));
        } catch (...) {
            spdlog::error("Invalid port in custom peer: {}", port_str);
            return 1;
        }
    }

    // 7. Initialize download stats tracking
    uint64_t last_bytes = 0;

    // Register 1-second recurring timer for progress rendering
    std::function<void()> render_progress;
    render_progress = [&]() {
        uint64_t current_bytes = get_downloaded_bytes(piece_manager, metadata);
        size_t completed_pieces = piece_manager.bitfield().count();

        double percentage = (static_cast<double>(completed_pieces) / static_cast<double>(metadata.num_pieces)) * 100.0;
        uint64_t bytes_this_sec = (current_bytes > last_bytes) ? (current_bytes - last_bytes) : 0;
        last_bytes = current_bytes;

        double speed_mb = static_cast<double>(bytes_this_sec) / (1024.0 * 1024.0);
        size_t active_peers = peer_manager.established_connection_count();

        // Compute Estimated Time to Completion (ETA)
        uint64_t bytes_left = static_cast<uint64_t>(metadata.total_length) - current_bytes;
        std::string eta_str = "N/A";
        if (bytes_this_sec > 0) {
            uint64_t eta_secs = bytes_left / bytes_this_sec;
            uint64_t mins = eta_secs / 60;
            uint64_t secs = eta_secs % 60;
            eta_str = std::to_string(mins) + "m " + std::to_string(secs) + "s";
        }

        // Render progress bar (width of 20 slots)
        size_t bar_width = 20;
        int filled = static_cast<int>((percentage / 100.0) * static_cast<double>(bar_width));
        std::string bar;
        bar.reserve(bar_width * 3); // Reserve room for UTF-8 block chars
        for (size_t i = 0; i < bar_width; ++i) {
            if (i < static_cast<size_t>(filled)) {
                bar += "█";
            } else {
                bar += "░";
            }
        }

        // Print progress string using carriage return (CR)
        std::printf("\r[%s] %.1f%% | %.2f MB/s | Peers: %zu | ETA: %s", 
                    bar.c_str(), percentage, speed_mb, active_peers, eta_str.c_str());
        std::fflush(stdout);

        // Check for download completion
        if (completed_pieces == static_cast<size_t>(metadata.num_pieces)) {
            std::printf("\n\nDownload complete! Initiating file integrity validation...\n");
            bool all_ok = true;
            for (size_t i = 0; i < static_cast<size_t>(metadata.num_pieces); ++i) {
                if (!disk_io.verify_piece(i)) {
                    spdlog::error("Piece {} validation failed on disk!", i);
                    all_ok = false;
                }
            }
            if (all_ok) {
                std::printf("All pieces successfully validated on disk.\n");
            } else {
                std::printf("Warning: Disk validation reported corruptions.\n");
            }

            // Announce completion to tracker
            try {
                spdlog::info("Announcing completion to tracker...");
                tracker_client.announce(6881, current_bytes, 0, 0, "completed");
            } catch (...) {}

            event_loop.shutdown();
        } else {
            event_loop.register_timer(std::chrono::seconds(1), render_progress);
        }
    };
    event_loop.register_timer(std::chrono::seconds(1), render_progress);

    // 8. Execute non-blocking event loop
    spdlog::info("Starting event multiplexer...");
    event_loop.run();

    // Print event loop scheduling latency statistics
    event_loop.print_latency_stats();

    // Final shutdown report
    size_t completed_pieces = piece_manager.bitfield().count();
    if (completed_pieces == static_cast<size_t>(metadata.num_pieces)) {
        spdlog::info("Engine execution finished successfully.");
        return 0;
    } else {
        spdlog::warn("Engine execution terminated. Progress: {}/{} pieces.", completed_pieces, metadata.num_pieces);
        return 0;
    }
}