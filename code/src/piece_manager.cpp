#include "piece_manager.hpp"
#include "hasher.hpp"
#include <algorithm>

PieceManager::PieceManager(const TorrentMetadata& metadata)
    : metadata_(metadata),
      bitfield_(static_cast<size_t>(metadata.num_pieces)) {}

bool PieceManager::has_piece(size_t piece_idx) const {
    return bitfield_.has(piece_idx);
}

void PieceManager::mark_completed(size_t piece_idx) {
    bitfield_.set(piece_idx, true);
}

void PieceManager::init_piece_state(size_t piece_idx) {
    auto& state = in_progress_[piece_idx];
    if (!state.blocks.empty()) {
        return; // already initialized
    }

    state.index = static_cast<uint32_t>(piece_idx);
    state.length = get_piece_length(piece_idx);
    state.data.resize(state.length, 0);

    uint32_t block_size = 16384; // 16 KiB
    uint32_t offset = 0;
    while (offset < state.length) {
        uint32_t len = std::min(block_size, state.length - offset);
        state.blocks.push_back(Block{
            .piece = static_cast<uint32_t>(piece_idx),
            .begin = offset,
            .length = len,
            .state = Block::State::Pending
        });
        offset += len;
    }
}

std::optional<Block> PieceManager::get_next_block(size_t piece_idx) {
    if (has_piece(piece_idx)) {
        return std::nullopt;
    }

    init_piece_state(piece_idx);

    auto& state = in_progress_[piece_idx];
    for (auto& block : state.blocks) {
        if (block.state == Block::State::Pending) {
            block.state = Block::State::Requested;
            return block;
        }
    }

    return std::nullopt;
}

bool PieceManager::block_received(size_t piece_idx, uint32_t begin, std::span<const uint8_t> block_data) {
    if (has_piece(piece_idx)) {
        return false;
    }

    init_piece_state(piece_idx);

    auto& state = in_progress_[piece_idx];
    auto it = std::find_if(state.blocks.begin(), state.blocks.end(), [begin](const Block& b) {
        return b.begin == begin;
    });

    if (it != state.blocks.end() && it->state != Block::State::Received) {
        it->state = Block::State::Received;
        if (begin + block_data.size() <= state.data.size()) {
            std::copy(block_data.begin(), block_data.end(), state.data.begin() + begin);
            state.blocks_received++;
        }
    }

    return state.blocks_received == state.blocks.size();
}

bool PieceManager::validate_piece(size_t piece_idx) {
    auto it = in_progress_.find(piece_idx);
    if (it == in_progress_.end()) {
        return false;
    }

    auto computed_hash = SHA1Hasher::hash(it->second.data);
    return computed_hash == metadata_.piece_hashes[piece_idx];
}

void PieceManager::piece_completed(size_t piece_idx) {
    bitfield_.set(piece_idx, true);
    in_progress_.erase(piece_idx);
}

void PieceManager::piece_failed(size_t piece_idx) {
    in_progress_.erase(piece_idx);
}

std::span<const uint8_t> PieceManager::get_piece_data(size_t piece_idx) const {
    auto it = in_progress_.find(piece_idx);
    if (it != in_progress_.end()) {
        return std::span<const uint8_t>(it->second.data.data(), it->second.data.size());
    }
    return {};
}

bool PieceManager::is_in_progress(size_t piece_idx) const {
    return in_progress_.find(piece_idx) != in_progress_.end();
}

bool PieceManager::has_pending_blocks(size_t piece_idx) const {
    if (has_piece(piece_idx)) {
        return false;
    }
    auto it = in_progress_.find(piece_idx);
    if (it == in_progress_.end()) {
        return true;
    }
    for (const auto& block : it->second.blocks) {
        if (block.state == Block::State::Pending) {
            return true;
        }
    }
    return false;
}

uint32_t PieceManager::get_piece_length(size_t piece_idx) const {
    if (piece_idx >= static_cast<size_t>(metadata_.num_pieces)) {
        return 0;
    }
    if (piece_idx < static_cast<size_t>(metadata_.num_pieces - 1)) {
        return static_cast<uint32_t>(metadata_.piece_length);
    }
    // Last piece calculation
    int64_t last_piece_len = metadata_.total_length - (static_cast<int64_t>(metadata_.num_pieces - 1) * metadata_.piece_length);
    return static_cast<uint32_t>(last_piece_len);
}

void PieceManager::block_request_failed(size_t piece_idx, uint32_t begin) {
    auto it = in_progress_.find(piece_idx);
    if (it != in_progress_.end()) {
        auto& state = it->second;
        auto block_it = std::find_if(state.blocks.begin(), state.blocks.end(), [begin](const Block& b) {
            return b.begin == begin;
        });
        if (block_it != state.blocks.end() && block_it->state == Block::State::Requested) {
            block_it->state = Block::State::Pending;
        }
    }
}
