#include "download_coordinator.hpp"
#include <algorithm>

DownloadCoordinator::DownloadCoordinator(PieceManager& piece_manager, PiecePicker& piece_picker, size_t num_pieces)
    : piece_manager_(piece_manager),
      piece_picker_(piece_picker),
      num_pieces_(num_pieces) {}

void DownloadCoordinator::register_peer(std::shared_ptr<PeerConnection> peer) {
    if (!peer) {
        return;
    }

    PeerConnection* raw_ptr = peer.get();
    peers_[raw_ptr] = PeerState{
        .connection = peer,
        .bitfield = Bitfield(num_pieces_),
        .outstanding = {},
        .active_piece = std::nullopt
    };

    std::weak_ptr<PeerConnection> weak_peer = peer;

    peer->on_message([this, weak_peer](const PeerMessage& msg) {
        if (auto p = weak_peer.lock()) {
            handle_peer_message(p, msg);
        }
    });

    peer->on_disconnect([this, weak_peer]() {
        if (auto p = weak_peer.lock()) {
            unregister_peer(p);
        }
    });
}

void DownloadCoordinator::unregister_peer(std::shared_ptr<PeerConnection> peer) {
    if (!peer) {
        return;
    }

    PeerConnection* raw_ptr = peer.get();
    auto it = peers_.find(raw_ptr);
    if (it == peers_.end()) {
        return;
    }

    auto& state = it->second;

    // 1. Remove its bitfield from the PiecePicker
    piece_picker_.remove_peer_bitfield(state.bitfield);

    // 2. Return all outstanding requested blocks back to Pending
    for (const auto& req : state.outstanding) {
        piece_manager_.block_request_failed(req.piece, req.begin);
    }

    // 3. Clear from our map
    peers_.erase(it);
}

void DownloadCoordinator::fill_request_pipeline(std::shared_ptr<PeerConnection> peer) {
    if (!peer || peer->is_peer_choking()) {
        return;
    }

    PeerConnection* raw_ptr = peer.get();
    auto it = peers_.find(raw_ptr);
    if (it == peers_.end()) {
        return;
    }

    auto& state = it->second;

    // Keep requesting blocks until our pipeline is full (depth of 5)
    while (state.outstanding.size() < 5) {
        // If we don't have an active piece, pick one
        if (!state.active_piece.has_value()) {
            Bitfield completed_or_full = piece_manager_.bitfield();
            for (size_t i = 0; i < num_pieces_; ++i) {
                if (!piece_manager_.has_pending_blocks(i)) {
                    completed_or_full.set(i, true);
                }
            }

            auto picked = piece_picker_.pick_piece(state.bitfield, completed_or_full);
            if (!picked.has_value()) {
                // No pieces to download from this peer
                break;
            }
            state.active_piece = picked;
        }

        // Try to get next block of our active piece
        auto block_opt = piece_manager_.get_next_block(*state.active_piece);
        if (!block_opt.has_value()) {
            // All blocks in this piece are already requested or received.
            // Reset active piece so we pick a new one in the next iteration.
            state.active_piece = std::nullopt;
            continue;
        }

        // Add to outstanding requests
        state.outstanding.push_back(RequestedBlock{
            .piece = block_opt->piece,
            .begin = block_opt->begin,
            .length = block_opt->length
        });

        // Send request message
        peer->send_message(PeerMessage::request(block_opt->piece, block_opt->begin, block_opt->length));
    }
}

void DownloadCoordinator::handle_peer_message(std::shared_ptr<PeerConnection> peer, const PeerMessage& msg) {
    PeerConnection* raw_ptr = peer.get();
    auto it = peers_.find(raw_ptr);
    if (it == peers_.end()) {
        return;
    }

    auto& state = it->second;

    std::visit([this, peer, &state](auto&& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, ChokeMsg>) {
            // Peer choked us! Return outstanding requests to Pending
            for (const auto& req : state.outstanding) {
                piece_manager_.block_request_failed(req.piece, req.begin);
            }
            state.outstanding.clear();
            state.active_piece = std::nullopt;
        }
        else if constexpr (std::is_same_v<T, UnchokeMsg>) {
            // Peer unchoked us! Try to fill pipeline
            fill_request_pipeline(peer);
        }
        else if constexpr (std::is_same_v<T, BitfieldMsg>) {
            // Peer sent bitfield. First remove old contribution
            piece_picker_.remove_peer_bitfield(state.bitfield);
            
            // Build the new bitfield
            state.bitfield = Bitfield(m.bitfield, num_pieces_);

            piece_picker_.add_peer_bitfield(state.bitfield);

            // Determine if we are interested
            bool interested = false;
            for (size_t i = 0; i < num_pieces_; ++i) {
                if (state.bitfield.has(i) && !piece_manager_.has_piece(i)) {
                    interested = true;
                    break;
                }
            }
            if (interested && !peer->is_interested()) {
                peer->send_message(PeerMessage::interested());
            }
        }
        else if constexpr (std::is_same_v<T, HaveMsg>) {
            size_t idx = m.piece_index;
            if (idx < num_pieces_) {
                if (!state.bitfield.has(idx)) {
                    state.bitfield.set(idx, true);
                    piece_picker_.add_availability(idx);
                }

                // Check interest
                if (!piece_manager_.has_piece(idx) && !peer->is_interested()) {
                    peer->send_message(PeerMessage::interested());
                }
            }
        }
        else if constexpr (std::is_same_v<T, PieceMsg>) {
            size_t piece_idx = m.piece_index;
            uint32_t begin = m.begin;
            
            // Remove from outstanding requests
            auto req_it = std::find_if(state.outstanding.begin(), state.outstanding.end(),
                [piece_idx, begin](const RequestedBlock& r) {
                    return r.piece == piece_idx && r.begin == begin;
                });
            if (req_it != state.outstanding.end()) {
                state.outstanding.erase(req_it);
            }

            // Feed block to piece manager
            bool completed = piece_manager_.block_received(piece_idx, begin, m.block);
            if (completed) {
                // Validate piece data
                if (piece_manager_.validate_piece(piece_idx)) {
                    if (on_piece_completed_cb_) {
                        on_piece_completed_cb_(piece_idx, piece_manager_.get_piece_data(piece_idx));
                    }
                    piece_manager_.piece_completed(piece_idx);
                    
                    // Cancel outstanding block requests for this piece across all peers
                    cancel_piece_requests(piece_idx);

                    // Broadcast HAVE to all active peers
                    PeerMessage have_msg = PeerMessage::have(static_cast<uint32_t>(piece_idx));
                    for (auto& [p_raw, p_state] : peers_) {
                        p_state.connection->send_message(have_msg);
                    }
                } else {
                    piece_manager_.piece_failed(piece_idx);
                    if (on_peer_corrupted_cb_) {
                        on_peer_corrupted_cb_(peer);
                    }
                }

                if (state.active_piece == piece_idx) {
                    state.active_piece = std::nullopt;
                }
            }

            // Top off the pipeline
            if (!peer->is_peer_choking()) {
                fill_request_pipeline(peer);
            }
        }
    }, msg.payload);
}

void DownloadCoordinator::cancel_piece_requests(size_t piece_idx) {
    for (auto& [p_raw, state] : peers_) {
        auto it = state.outstanding.begin();
        while (it != state.outstanding.end()) {
            if (it->piece == piece_idx) {
                // Send CANCEL message
                state.connection->send_message(PeerMessage::cancel(it->piece, it->begin, it->length));
                it = state.outstanding.erase(it);
            } else {
                ++it;
            }
        }
        if (state.active_piece == piece_idx) {
            state.active_piece = std::nullopt;
        }
    }
}

size_t DownloadCoordinator::outstanding_request_count(std::shared_ptr<PeerConnection> peer) const {
    if (!peer) return 0;
    auto it = peers_.find(peer.get());
    if (it == peers_.end()) return 0;
    return it->second.outstanding.size();
}

std::optional<size_t> DownloadCoordinator::active_piece(std::shared_ptr<PeerConnection> peer) const {
    if (!peer) return std::nullopt;
    auto it = peers_.find(peer.get());
    if (it == peers_.end()) return std::nullopt;
    return it->second.active_piece;
}

const Bitfield* DownloadCoordinator::get_peer_bitfield(std::shared_ptr<PeerConnection> peer) const {
    if (!peer) return nullptr;
    auto it = peers_.find(peer.get());
    if (it == peers_.end()) return nullptr;
    return &it->second.bitfield;
}
