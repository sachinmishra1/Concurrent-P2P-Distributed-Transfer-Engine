#include "peer_manager.hpp"
#include <spdlog/spdlog.h>

PeerManager::PeerManager(EventLoop& loop, 
                         std::array<uint8_t, 20> info_hash, 
                         std::array<uint8_t, 20> our_peer_id,
                         size_t max_connections)
    : loop_(loop),
      info_hash_(info_hash),
      our_peer_id_(our_peer_id),
      max_connections_(max_connections) {}

PeerManager::~PeerManager() {
    spdlog::debug("PeerManager: destroying and disconnecting all active peers");
    auto peers = std::move(active_peers_);
    for (auto& [key, peer] : peers) {
        peer->on_handshake(nullptr);
        peer->on_disconnect(nullptr);
        peer->disconnect();
    }
}

void PeerManager::add_peers(const std::vector<PeerInfo>& peers) {
    spdlog::info("PeerManager: adding {} peers to potential connection pool", peers.size());
    for (const auto& peer_info : peers) {
        if (active_peers_.size() >= max_connections_) {
            spdlog::warn("PeerManager: active connections count ({}) reached max_connections ({})", active_peers_.size(), max_connections_);
            break;
        }

        if (is_blacklisted(peer_info.ip, peer_info.port)) {
            spdlog::debug("PeerManager: skipping blacklisted peer {}:{}", peer_info.ip, peer_info.port);
            continue;
        }

        std::string key = make_peer_key(peer_info.ip, peer_info.port);
        if (active_peers_.find(key) != active_peers_.end()) {
            spdlog::trace("PeerManager: peer {}:{} is already active or connecting", peer_info.ip, peer_info.port);
            continue;
        }

        connect_to_peer(peer_info.ip, peer_info.port);
    }
}

void PeerManager::blacklist_peer(const std::string& ip, uint16_t port) {
    std::string key = make_peer_key(ip, port);
    spdlog::info("PeerManager: blacklisting peer {}:{}", ip, port);
    blacklist_.insert(key);

    auto it = active_peers_.find(key);
    if (it != active_peers_.end()) {
        auto peer = it->second;
        peer->disconnect();
    }
}

bool PeerManager::is_blacklisted(const std::string& ip, uint16_t port) const {
    return blacklist_.find(make_peer_key(ip, port)) != blacklist_.end();
}

void PeerManager::connect_to_peer(const std::string& ip, uint16_t port) {
    if (active_peers_.size() >= max_connections_) {
        spdlog::warn("PeerManager: cannot connect to {}:{}, max connections reached", ip, port);
        return;
    }
    if (is_blacklisted(ip, port)) {
        spdlog::debug("PeerManager: connect to {}:{} refused (blacklisted)", ip, port);
        return;
    }
    std::string key = make_peer_key(ip, port);
    if (active_peers_.find(key) != active_peers_.end()) {
        return;
    }

    spdlog::info("PeerManager: attempting connection to {}:{}", ip, port);
    auto peer = PeerConnection::create(loop_, ip, port, info_hash_, our_peer_id_);
    active_peers_[key] = peer;

    std::weak_ptr<PeerConnection> weak_peer = peer;

    peer->on_handshake([this, weak_peer](const HandshakeMsg& /*msg*/) {
        if (auto p = weak_peer.lock()) {
            spdlog::info("PeerManager: peer connected successfully (handshake exchanged) from {}:{}", p->ip(), p->port());
            if (on_peer_connected_cb_) {
                on_peer_connected_cb_(p);
            }
            p->send_message(PeerMessage::interested());
        }
    });

    peer->on_disconnect([this, key, weak_peer]() {
        std::shared_ptr<PeerConnection> p = weak_peer.lock();
        if (p) {
            spdlog::info("PeerManager: peer disconnected or connection failed from {}:{}", p->ip(), p->port());
            if (on_peer_disconnected_cb_) {
                on_peer_disconnected_cb_(p);
            }
        }
        active_peers_.erase(key);
    });

    peer->connect();
}

std::vector<std::pair<std::string, uint16_t>> PeerManager::get_active_peers() const {
    std::vector<std::pair<std::string, uint16_t>> result;
    result.reserve(active_peers_.size());
    for (const auto& [key, peer] : active_peers_) {
        result.emplace_back(peer->ip(), peer->port());
    }
    return result;
}

size_t PeerManager::established_connection_count() const {
    size_t count = 0;
    for (const auto& [key, peer] : active_peers_) {
        if (peer->get_state() == ConnectionState::Active) {
            count++;
        }
    }
    return count;
}
