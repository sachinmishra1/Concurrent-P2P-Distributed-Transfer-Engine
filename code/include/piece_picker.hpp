#pragma once

#include "bitfield.hpp"
#include <vector>
#include <atomic>
#include <cstdint>
#include <random>
#include <mutex>
#include <optional>

class PiecePicker {
public:
    explicit PiecePicker(size_t num_pieces);

    ~PiecePicker() = default;

    // Prevent copy
    PiecePicker(const PiecePicker&) = delete;
    PiecePicker& operator=(const PiecePicker&) = delete;

    // Increments availability of a specific piece
    void add_availability(size_t piece_idx);

    // Decrements availability of a specific piece (avoids underflow)
    void remove_availability(size_t piece_idx);

    // Updates availability for all pieces in a peer's bitfield
    void add_peer_bitfield(const Bitfield& bitfield);

    // Decrements availability for all pieces in a peer's bitfield
    void remove_peer_bitfield(const Bitfield& bitfield);

    // Picks a piece for a given peer that we want to download.
    // Selection rules:
    // 1. Must be a piece that the peer has (peer_bitfield.has(i) == true)
    // 2. Must be a piece we do NOT have (our_bitfield.has(i) == false)
    // 3. Bootstrapping: If our_bitfield.count() < 4, select a random piece matching rules 1 & 2.
    // 4. Rarest-First: Otherwise, select the piece with the lowest positive availability count.
    //    If there are ties, select randomly.
    // Returns std::nullopt if no suitable piece is found.
    std::optional<size_t> pick_piece(const Bitfield& peer_bitfield, const Bitfield& our_bitfield);

    // Returns the availability count of a specific piece
    uint32_t get_availability(size_t piece_idx) const;

private:
    size_t num_pieces_;
    // Histogram of piece availability using std::atomic
    std::vector<std::atomic<uint32_t>> availability_histogram_;

    // Random engine for bootstrap phase and ties
    std::mt19937 rng_;
    mutable std::mutex rng_mutex_;
};
