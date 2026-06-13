#include "torrent.hpp"
#include "bencode.hpp"
#include <stdexcept>
#include <algorithm>

TorrentMetadata TorrentMetadata::from_bencode(std::span<const uint8_t> torrent_data) {
    BencodeValue root = BencodeParser::parse(torrent_data);
    if (!root.is_dict()) {
        throw std::runtime_error("TorrentMetadata: root of torrent file must be a dictionary");
    }

    const auto& root_dict = root.as_dict();
    
    // announce_url (required key)
    auto announce_it = root_dict.find("announce");
    if (announce_it == root_dict.end() || !announce_it->second.is_string()) {
        throw std::runtime_error("TorrentMetadata: missing or invalid 'announce' URL");
    }
    std::string announce_url(announce_it->second.as_string());

    // info dictionary (required key)
    auto info_it = root_dict.find("info");
    if (info_it == root_dict.end() || !info_it->second.is_dict()) {
        throw std::runtime_error("TorrentMetadata: missing or invalid 'info' dictionary");
    }
    const auto& info_dict = info_it->second.as_dict();

    // piece length (required key)
    auto piece_length_it = info_dict.find("piece length");
    if (piece_length_it == info_dict.end() || !piece_length_it->second.is_int()) {
        throw std::runtime_error("TorrentMetadata: missing or invalid 'piece length'");
    }
    int64_t piece_length = piece_length_it->second.as_int();

    // name (required key)
    auto name_it = info_dict.find("name");
    if (name_it == info_dict.end() || !name_it->second.is_string()) {
        throw std::runtime_error("TorrentMetadata: missing or invalid 'name'");
    }
    std::string name(name_it->second.as_string());

    // pieces (required key, concatenated 20-byte SHA-1 hashes)
    auto pieces_it = info_dict.find("pieces");
    if (pieces_it == info_dict.end() || !pieces_it->second.is_string()) {
        throw std::runtime_error("TorrentMetadata: missing or invalid 'pieces'");
    }
    std::string_view pieces_str = pieces_it->second.as_string();
    if (pieces_str.size() % 20 != 0) {
        throw std::runtime_error("TorrentMetadata: 'pieces' string size must be a multiple of 20");
    }
    size_t num_pieces = pieces_str.size() / 20;
    std::vector<std::array<uint8_t, 20>> piece_hashes;
    piece_hashes.reserve(num_pieces);
    for (size_t i = 0; i < num_pieces; ++i) {
        std::array<uint8_t, 20> hash;
        std::copy_n(pieces_str.begin() + i * 20, 20, hash.begin());
        piece_hashes.push_back(hash);
    }

    // Single-file vs Multi-file parsing
    std::vector<FileInfo> files;
    int64_t total_length = 0;
    
    auto files_it = info_dict.find("files");
    if (files_it != info_dict.end()) {
        // Multi-file format
        if (!files_it->second.is_list()) {
            throw std::runtime_error("TorrentMetadata: 'files' must be a list in multi-file torrent");
        }
        const auto& files_list = files_it->second.as_list();
        int64_t current_offset = 0;
        for (const auto& file_val : files_list) {
            if (!file_val.is_dict()) {
                throw std::runtime_error("TorrentMetadata: each element in 'files' must be a dictionary");
            }
            const auto& file_dict = file_val.as_dict();
            
            auto length_it = file_dict.find("length");
            if (length_it == file_dict.end() || !length_it->second.is_int()) {
                throw std::runtime_error("TorrentMetadata: missing or invalid file length in multi-file list");
            }
            int64_t length = length_it->second.as_int();

            auto path_it = file_dict.find("path");
            if (path_it == file_dict.end() || !path_it->second.is_list()) {
                throw std::runtime_error("TorrentMetadata: missing or invalid file path in multi-file list");
            }
            const auto& path_list = path_it->second.as_list();
            std::string path_str;
            for (size_t i = 0; i < path_list.size(); ++i) {
                if (!path_list[i].is_string()) {
                    throw std::runtime_error("TorrentMetadata: path segment must be a string");
                }
                if (i > 0) {
                    path_str += "/";
                }
                path_str += path_list[i].as_string();
            }

            files.push_back(FileInfo{
                .path = std::move(path_str),
                .length = length,
                .offset = current_offset
            });
            current_offset += length;
        }
        total_length = current_offset;
    } else {
        // Single-file format
        auto length_it = info_dict.find("length");
        if (length_it == info_dict.end() || !length_it->second.is_int()) {
            throw std::runtime_error("TorrentMetadata: missing or invalid 'length' in single-file torrent");
        }
        int64_t length = length_it->second.as_int();
        files.push_back(FileInfo{
            .path = name,
            .length = length,
            .offset = 0
        });
        total_length = length;
    }

    // Validate size of pieces matches total length
    int64_t calculated_pieces = (total_length + piece_length - 1) / piece_length;
    if (calculated_pieces != static_cast<int64_t>(num_pieces)) {
        throw std::runtime_error("TorrentMetadata: mismatch between piece count and total length");
    }

    return TorrentMetadata{
        .name = std::move(name),
        .announce_url = std::move(announce_url),
        .piece_length = piece_length,
        .total_length = total_length,
        .num_pieces = static_cast<int32_t>(num_pieces),
        .piece_hashes = std::move(piece_hashes),
        .files = std::move(files),
        .info_hash = {}
    };
}
