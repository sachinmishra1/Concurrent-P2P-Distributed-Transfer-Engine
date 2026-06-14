#pragma once

#include <string>
#include <vector>
#include <array>
#include <span>
#include <cstdint>

struct FileInfo {
    std::string path;        // relative path within the torrent
    int64_t length;          // file size in bytes
    int64_t offset;          // byte offset within the concatenated piece space
};

struct TorrentMetadata {
    std::string name;                  // torrent name
    std::string announce_url;          // tracker URL
    std::vector<std::string> announce_list; // list of fallback tracker URLs
    int64_t piece_length;              // bytes per piece
    int64_t total_length;              // total bytes across all files
    int32_t num_pieces;                // number of pieces
    std::vector<std::array<uint8_t, 20>> piece_hashes;  // SHA-1 per piece
    std::vector<FileInfo> files;       // file layout
    std::array<uint8_t, 20> info_hash; // SHA-1 of info dict

    static TorrentMetadata from_bencode(std::span<const uint8_t> torrent_data);
};
