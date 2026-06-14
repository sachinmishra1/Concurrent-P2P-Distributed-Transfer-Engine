#pragma once

#include "peer_connection.hpp"
#include "piece_manager.hpp"
#include "piece_picker.hpp"
#include "bitfield.hpp"
#include <unordered_map>
#include <memory>
#include <vector>
#include <optional>
#include <cstdint>
#include <cstddef>

class DownloadCoordinator {
public:
    DownloadCoordinator(PieceManager& piece_manager, PiecePicker& piece_picker, size_t num_pieces);
    ~DownloadCoordinator() = default;

    // Prevent copy/move
    DownloadCoordinator(const DownloadCoordinator&) = delete;
    DownloadCoordinator& operator=(const DownloadCoordinator&) = delete;
    DownloadCoordinator(DownloadCoordinator&&) = delete;
    DownloadCoordinator& operator=(DownloadCoordinator&&) = delete;

    // Register a new peer connection. Registers callbacks on the connection for messages and disconnections.
    void register_peer(std::shared_ptr<PeerConnection> peer);

    // Unregister a peer connection. Cleans up any outstanding requests and updates availability picker.
    void unregister_peer(std::shared_ptr<PeerConnection> peer);

    // Send cancel messages to all peers for a specific piece (e.g. once verified)
    void cancel_piece_requests(size_t piece_idx);

    // Test/verification helpers
    size_t outstanding_request_count(std::shared_ptr<PeerConnection> peer) const;
    std::optional<size_t> active_piece(std::shared_ptr<PeerConnection> peer) const;
    const Bitfield* get_peer_bitfield(std::shared_ptr<PeerConnection> peer) const;

    // Callback registered to listen for validated piece downloads.
    // Triggers when a piece's hash has been successfully validated.
    void on_piece_completed(std::function<void(size_t, std::span<const uint8_t>)> cb) {
        on_piece_completed_cb_ = std::move(cb);
    }

    // Callback registered to listen for peer corruption detection.
    void on_peer_corrupted(std::function<void(std::shared_ptr<PeerConnection>)> cb) {
        on_peer_corrupted_cb_ = std::move(cb);
    }

private:
    // Helper to send up to 5 requests (pipeline depth) to the peer if it's unchoked
    void fill_request_pipeline(std::shared_ptr<PeerConnection> peer);

    // Handles incoming messages for a peer
    void handle_peer_message(std::shared_ptr<PeerConnection> peer, const PeerMessage& msg);

    PieceManager& piece_manager_;
    PiecePicker& piece_picker_;
    size_t num_pieces_;

    struct RequestedBlock {
        uint32_t piece;
        uint32_t begin;
        uint32_t length;
    };

    struct PeerState {
        std::shared_ptr<PeerConnection> connection;
        Bitfield bitfield;
        std::vector<RequestedBlock> outstanding;
        std::optional<size_t> active_piece;
    };

    std::unordered_map<PeerConnection*, PeerState> peers_;
    std::function<void(size_t, std::span<const uint8_t>)> on_piece_completed_cb_;
    std::function<void(std::shared_ptr<PeerConnection>)> on_peer_corrupted_cb_;
};
