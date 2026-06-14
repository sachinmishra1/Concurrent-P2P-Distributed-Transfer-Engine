#include "torrent.hpp"
#include "bencode.hpp"
#include <stdexcept>
#include <algorithm>
#include <cryptopp/sha.h>

TorrentMetadata TorrentMetadata::from_bencode(std::span<const uint8_t> torrent_data) {
    std::span<const uint8_t> remaining = torrent_data;
    if (remaining.empty() || remaining[0] != 'd') {
        throw std::runtime_error("TorrentMetadata: root of torrent file must be a dictionary");
    }
    remaining = remaining.subspan(1); // consume 'd'

    std::string announce_url;
    std::vector<std::string> announce_list;
    BencodeValue info_val;
    std::span<const uint8_t> info_span;

    BencodeString last_key;
    bool has_last_key = false;

    while (!remaining.empty() && remaining[0] != 'e') {
        BencodeString key = BencodeParser::parse_string(remaining);
        
        // Lexicographical sorting check for root dict keys
        if (has_last_key) {
            if (key <= last_key) {
                if (key == last_key) {
                    throw std::runtime_error("TorrentMetadata: duplicate key in root dictionary");
                }
                throw std::runtime_error("TorrentMetadata: root dictionary keys not sorted lexicographically");
            }
        }
        last_key = key;
        has_last_key = true;

        if (key == "announce") {
            BencodeValue val = BencodeParser::parse_value(remaining);
            if (!val.is_string()) {
                throw std::runtime_error("TorrentMetadata: 'announce' must be a string");
            }
            announce_url = std::string(val.as_string());
        } else if (key == "announce-list") {
            BencodeValue val = BencodeParser::parse_value(remaining);
            if (val.is_list()) {
                const auto& tier_list = val.as_list();
                for (const auto& tier : tier_list) {
                    if (tier.is_list()) {
                        const auto& tracker_list = tier.as_list();
                        for (const auto& tracker : tracker_list) {
                            if (tracker.is_string()) {
                                std::string tracker_str(tracker.as_string());
                                if (!tracker_str.empty()) {
                                    announce_list.push_back(std::move(tracker_str));
                                }
                            }
                        }
                    }
                }
            }
        } else if (key == "info") {
            const uint8_t* info_start = remaining.data();
            info_val = BencodeParser::parse_value(remaining);
            const uint8_t* info_end = remaining.data();
            if (info_end < info_start) {
                throw std::runtime_error("TorrentMetadata: invalid parsing of 'info' dictionary");
            }
            info_span = std::span<const uint8_t>(info_start, static_cast<size_t>(info_end - info_start));
        } else {
            // parse and ignore other keys
            BencodeParser::parse_value(remaining);
        }
    }

    if (remaining.empty() || remaining[0] != 'e') {
        throw std::runtime_error("TorrentMetadata: expected 'e' at end of root dictionary");
    }
    remaining = remaining.subspan(1); // consume 'e'

    if (!remaining.empty()) {
        throw std::runtime_error("TorrentMetadata: extra data at end of input");
    }

    if (announce_url.empty()) {
        throw std::runtime_error("TorrentMetadata: missing 'announce' URL");
    }
    if (!info_val.is_dict()) {
        throw std::runtime_error("TorrentMetadata: missing or invalid 'info' dictionary");
    }
    const auto& info_dict = info_val.as_dict();

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

    // Compute info_hash (SHA-1 over raw info dictionary value bytes)
    std::array<uint8_t, 20> info_hash{};
    if (!info_span.empty()) {
        CryptoPP::SHA1 sha1;
        sha1.CalculateDigest(info_hash.data(), info_span.data(), info_span.size());
    }

    if (announce_list.empty() && !announce_url.empty()) {
        announce_list.push_back(announce_url);
    }

    return TorrentMetadata{
        .name = std::move(name),
        .announce_url = std::move(announce_url),
        .announce_list = std::move(announce_list),
        .piece_length = piece_length,
        .total_length = total_length,
        .num_pieces = static_cast<int32_t>(num_pieces),
        .piece_hashes = std::move(piece_hashes),
        .files = std::move(files),
        .info_hash = info_hash
    };
}
