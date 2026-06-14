#include "piece_picker.hpp"
#include <limits>
#include <algorithm>

PiecePicker::PiecePicker(size_t num_pieces)
    : num_pieces_(num_pieces),
      availability_histogram_(num_pieces),
      rng_(std::random_device{}()) {
    for (size_t i = 0; i < num_pieces_; ++i) {
        availability_histogram_[i].store(0, std::memory_order_relaxed);
    }
}

void PiecePicker::add_availability(size_t piece_idx) {
    if (piece_idx >= num_pieces_) {
        return;
    }
    availability_histogram_[piece_idx].fetch_add(1, std::memory_order_relaxed);
}

void PiecePicker::remove_availability(size_t piece_idx) {
    if (piece_idx >= num_pieces_) {
        return;
    }
    uint32_t val = availability_histogram_[piece_idx].load(std::memory_order_relaxed);
    while (val > 0) {
        if (availability_histogram_[piece_idx].compare_exchange_weak(val, val - 1, std::memory_order_relaxed)) {
            break;
        }
    }
}

void PiecePicker::add_peer_bitfield(const Bitfield& bitfield) {
    for (size_t i = 0; i < num_pieces_ && i < bitfield.num_bits(); ++i) {
        if (bitfield.has(i)) {
            availability_histogram_[i].fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void PiecePicker::remove_peer_bitfield(const Bitfield& bitfield) {
    for (size_t i = 0; i < num_pieces_ && i < bitfield.num_bits(); ++i) {
        if (bitfield.has(i)) {
            uint32_t val = availability_histogram_[i].load(std::memory_order_relaxed);
            while (val > 0) {
                if (availability_histogram_[i].compare_exchange_weak(val, val - 1, std::memory_order_relaxed)) {
                    break;
                }
            }
        }
    }
}

std::optional<size_t> PiecePicker::pick_piece(const Bitfield& peer_bitfield, const Bitfield& our_bitfield) {
    std::vector<size_t> candidates;
    candidates.reserve(num_pieces_);
    for (size_t i = 0; i < num_pieces_; ++i) {
        if (peer_bitfield.has(i) && !our_bitfield.has(i)) {
            candidates.push_back(i);
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(rng_mutex_);

    // Bootstrapping: pick random if we have < 4 pieces
    if (our_bitfield.count() < 4) {
        std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
        return candidates[dist(rng_)];
    }

    // Rarest-first: find minimum availability among candidates
    uint32_t min_avail = std::numeric_limits<uint32_t>::max();
    std::vector<size_t> rarest_candidates;
    rarest_candidates.reserve(candidates.size());

    for (size_t idx : candidates) {
        uint32_t avail = availability_histogram_[idx].load(std::memory_order_relaxed);
        if (avail < min_avail) {
            min_avail = avail;
            rarest_candidates.clear();
            rarest_candidates.push_back(idx);
        } else if (avail == min_avail) {
            rarest_candidates.push_back(idx);
        }
    }

    if (rarest_candidates.empty()) {
        return std::nullopt;
    }

    // Random choice among rarest candidates to distribute load
    std::uniform_int_distribution<size_t> dist(0, rarest_candidates.size() - 1);
    return rarest_candidates[dist(rng_)];
}

uint32_t PiecePicker::get_availability(size_t piece_idx) const {
    if (piece_idx >= num_pieces_) {
        return 0;
    }
    return availability_histogram_[piece_idx].load(std::memory_order_relaxed);
}
