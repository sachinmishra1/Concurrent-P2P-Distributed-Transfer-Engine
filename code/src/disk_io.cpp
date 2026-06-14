#include "disk_io.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>

DiskIOManager::DiskIOManager(std::string base_path, const TorrentMetadata& metadata)
    : base_path_(std::move(base_path)),
      metadata_(metadata) {}

std::string DiskIOManager::get_full_path(const std::string& relative_path) const {
    std::filesystem::path base(base_path_);
    std::filesystem::path rel(relative_path);
    return (base / rel).string();
}

void DiskIOManager::preallocate_files() {
    for (const auto& file : metadata_.files) {
        std::filesystem::path full_path(get_full_path(file.path));
        
        // 1. Create parent directory tree if it doesn't exist
        std::filesystem::path parent = full_path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }

        // 2. Touch the file to ensure it exists
        {
            std::ofstream out(full_path, std::ios::binary | std::ios::out | std::ios::app);
            if (!out.is_open()) {
                throw std::runtime_error("DiskIOManager: failed to open/create file for pre-allocation: " + full_path.string());
            }
        }

        // 3. Resize file to target length
        std::filesystem::resize_file(full_path, static_cast<uint64_t>(file.length));
    }
}

void DiskIOManager::write_piece(size_t piece_idx, std::span<const uint8_t> data) {
    if (piece_idx >= static_cast<size_t>(metadata_.num_pieces)) {
        throw std::out_of_range("DiskIOManager: piece index out of bounds");
    }

    int64_t global_offset = static_cast<int64_t>(piece_idx) * metadata_.piece_length;
    int64_t bytes_to_write = static_cast<int64_t>(data.size());

    for (const auto& file : metadata_.files) {
        int64_t file_start = file.offset;
        int64_t file_end = file.offset + file.length;

        // Check if the file overlaps with [global_offset, global_offset + bytes_to_write)
        if (global_offset + bytes_to_write <= file_start || global_offset >= file_end) {
            continue; // No overlap
        }

        // Determine overlap range
        int64_t overlap_start = std::max(global_offset, file_start);
        int64_t overlap_end = std::min(global_offset + bytes_to_write, file_end);
        int64_t overlap_len = overlap_end - overlap_start;

        // Calculate offset in the file and in the data buffer
        int64_t offset_in_file = overlap_start - file_start;
        int64_t offset_in_data = overlap_start - global_offset;

        // Write to file
        std::string full_path = get_full_path(file.path);
        
        // Open file using std::fstream in read/write binary mode
        std::fstream fs(full_path, std::ios::in | std::ios::out | std::ios::binary);
        if (!fs.is_open()) {
            throw std::runtime_error("DiskIOManager: failed to open file for writing: " + full_path);
        }

        fs.seekp(offset_in_file);
        fs.write(reinterpret_cast<const char*>(data.data() + offset_in_data), overlap_len);
        if (!fs.good()) {
            throw std::runtime_error("DiskIOManager: failed writing to file: " + full_path);
        }
    }
}

std::vector<uint8_t> DiskIOManager::read_piece(size_t piece_idx) {
    if (piece_idx >= static_cast<size_t>(metadata_.num_pieces)) {
        throw std::out_of_range("DiskIOManager: piece index out of bounds");
    }

    int64_t global_offset = static_cast<int64_t>(piece_idx) * metadata_.piece_length;
    int64_t piece_size = metadata_.piece_length;
    if (piece_idx == static_cast<size_t>(metadata_.num_pieces - 1)) {
        piece_size = metadata_.total_length - (static_cast<int64_t>(metadata_.num_pieces - 1) * metadata_.piece_length);
    }

    std::vector<uint8_t> data(static_cast<size_t>(piece_size), 0);
    int64_t bytes_to_read = piece_size;

    for (const auto& file : metadata_.files) {
        int64_t file_start = file.offset;
        int64_t file_end = file.offset + file.length;

        // Check if the file overlaps with [global_offset, global_offset + bytes_to_read)
        if (global_offset + bytes_to_read <= file_start || global_offset >= file_end) {
            continue; // No overlap
        }

        // Determine overlap range
        int64_t overlap_start = std::max(global_offset, file_start);
        int64_t overlap_end = std::min(global_offset + bytes_to_read, file_end);
        int64_t overlap_len = overlap_end - overlap_start;

        // Calculate offset in the file and in the data buffer
        int64_t offset_in_file = overlap_start - file_start;
        int64_t offset_in_data = overlap_start - global_offset;

        // Read from file
        std::string full_path = get_full_path(file.path);
        std::ifstream fs(full_path, std::ios::binary);
        if (!fs.is_open()) {
            throw std::runtime_error("DiskIOManager: failed to open file for reading: " + full_path);
        }

        fs.seekg(offset_in_file);
        fs.read(reinterpret_cast<char*>(data.data() + offset_in_data), overlap_len);
        if (!fs.good() && !fs.eof()) {
            throw std::runtime_error("DiskIOManager: failed reading from file: " + full_path);
        }
    }

    return data;
}

bool DiskIOManager::verify_piece(size_t piece_idx) {
    if (piece_idx >= static_cast<size_t>(metadata_.num_pieces)) {
        return false;
    }
    try {
        auto data = read_piece(piece_idx);
        auto calculated_hash = SHA1Hasher::hash(data);
        return calculated_hash == metadata_.piece_hashes[piece_idx];
    } catch (...) {
        return false;
    }
}
