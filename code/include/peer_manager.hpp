#pragma once

#include "event_loop.hpp"
#include "peer_connection.hpp"
#include "tracker_client.hpp" // For PeerInfo
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <functional>

class PeerManager {
public:
    PeerManager(EventLoop& loop, 
                std::array<uint8_t, 20> info_hash, 
                std::array<uint8_t, 20> our_peer_id,
                size_t max_connections = 128);

    ~PeerManager();

    // Prevent copy/move
    PeerManager(const PeerManager&) = delete;
    PeerManager& operator=(const PeerManager&) = delete;
    PeerManager(PeerManager&&) = delete;
    PeerManager& operator=(PeerManager&&) = delete;

    // Discovers and schedules connection to a list of peers.
    // Skips blacklisted peers, already active/connecting peers, or if max_connections is reached.
    void add_peers(const std::vector<PeerInfo>& peers);

    // Blacklist a peer so we never connect to them. If currently connected, disconnects them.
    void blacklist_peer(const std::string& ip, uint16_t port);

    // Check if a peer is blacklisted
    bool is_blacklisted(const std::string& ip, uint16_t port) const;

    // Direct connection to a single peer (useful for tests)
    void connect_to_peer(const std::string& ip, uint16_t port);

    // Get current connection count
    size_t active_connection_count() const { return active_peers_.size(); }

    // Get count of connections where handshake is complete
    size_t established_connection_count() const;

    // Get lists of connected / connecting peer IPs/ports
    std::vector<std::pair<std::string, uint16_t>> get_active_peers() const;

    // Callback registering
    void on_peer_connected(std::function<void(std::shared_ptr<PeerConnection>)> cb) {
        on_peer_connected_cb_ = std::move(cb);
    }
    void on_peer_disconnected(std::function<void(std::shared_ptr<PeerConnection>)> cb) {
        on_peer_disconnected_cb_ = std::move(cb);
    }

private:
    std::string make_peer_key(const std::string& ip, uint16_t port) const {
        return ip + ":" + std::to_string(port);
    }

    EventLoop& loop_;
    std::array<uint8_t, 20> info_hash_;
    std::array<uint8_t, 20> our_peer_id_;
    size_t max_connections_;

    // Maps "ip:port" to PeerConnection
    std::unordered_map<std::string, std::shared_ptr<PeerConnection>> active_peers_;
    std::unordered_set<std::string> blacklist_;

    std::function<void(std::shared_ptr<PeerConnection>)> on_peer_connected_cb_;
    std::function<void(std::shared_ptr<PeerConnection>)> on_peer_disconnected_cb_;
};
