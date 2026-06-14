#pragma once

#include "torrent.hpp"
#include "hasher.hpp"
#include <span>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

class DiskIOManager {
public:
    DiskIOManager(std::string base_path, const TorrentMetadata& metadata);
    ~DiskIOManager() = default;

    // Prevent copy/move
    DiskIOManager(const DiskIOManager&) = delete;
    DiskIOManager& operator=(const DiskIOManager&) = delete;
    DiskIOManager(DiskIOManager&&) = delete;
    DiskIOManager& operator=(DiskIOManager&&) = delete;

    // Pre-allocate all output files with their correct sizes.
    // Creates parent directories if they don't exist.
    void preallocate_files();

    // Write a validated piece to its correct offset(s) in file(s).
    // Safely handles cross-boundary pieces spanning multiple files.
    void write_piece(size_t piece_idx, std::span<const uint8_t> data);

    // Read a piece from disk.
    std::vector<uint8_t> read_piece(size_t piece_idx);

    // Verify a piece stored on disk matches its expected SHA-1 hash.
    bool verify_piece(size_t piece_idx);

private:
    std::string get_full_path(const std::string& relative_path) const;

    std::string base_path_;
    TorrentMetadata metadata_;
};
