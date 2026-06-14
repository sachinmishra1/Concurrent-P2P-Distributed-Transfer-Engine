#pragma once

#include "torrent.hpp"
#include "bitfield.hpp"
#include <vector>
#include <unordered_map>
#include <optional>
#include <span>
#include <cstdint>
#include <cstddef>

struct Block {
    uint32_t piece;
    uint32_t begin;
    uint32_t length;
    enum class State {
        Pending,
        Requested,
        Received
    } state = State::Pending;
};

struct PieceState {
    uint32_t index = 0;
    uint32_t length = 0;
    std::vector<Block> blocks;
    std::vector<uint8_t> data;
    size_t blocks_received = 0;
};

class PieceManager {
public:
    explicit PieceManager(const TorrentMetadata& metadata);

    ~PieceManager() = default;

    // Check if we already have the piece completed and verified
    bool has_piece(size_t piece_idx) const;

    // Mark a piece as completed and verified directly (e.g. seeding start)
    void mark_completed(size_t piece_idx);

    // Get reference to our client's bitfield
    const Bitfield& bitfield() const { return bitfield_; }

    // Get the next block to request for a given piece index.
    // Transition its state from Pending -> Requested.
    // Returns std::nullopt if all blocks are already requested/received.
    std::optional<Block> get_next_block(size_t piece_idx);

    // Process a received block payload.
    // Returns true if the piece has now been fully received (all blocks received).
    // If true, the caller should then call validate_piece().
    bool block_received(size_t piece_idx, uint32_t begin, std::span<const uint8_t> block_data);

    // Validate the piece data against the expected SHA-1 hash from metadata.
    // Returns true if verification succeeds.
    bool validate_piece(size_t piece_idx);

    // Mark piece as completed, update our bitfield, and clean up temporary buffers
    void piece_completed(size_t piece_idx);

    // Reset piece blocks back to Pending so they can be re-requested, and clean up temporary buffers
    void piece_failed(size_t piece_idx);

    // Get the completed/in-progress piece data buffer
    std::span<const uint8_t> get_piece_data(size_t piece_idx) const;

    // Check if a piece is currently in progress
    bool is_in_progress(size_t piece_idx) const;

    // Check if there are any pending blocks left for this piece
    bool has_pending_blocks(size_t piece_idx) const;

    // Get total length of a specific piece
    uint32_t get_piece_length(size_t piece_idx) const;

    // Reset block state from Requested back to Pending (e.g. timeout or peer disconnect)
    void block_request_failed(size_t piece_idx, uint32_t begin);

private:
    void init_piece_state(size_t piece_idx);

    TorrentMetadata metadata_;
    Bitfield bitfield_;
    std::unordered_map<size_t, PieceState> in_progress_;
};
