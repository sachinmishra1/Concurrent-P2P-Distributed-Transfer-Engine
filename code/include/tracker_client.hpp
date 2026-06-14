#pragma once

#include <string>
#include <vector>
#include <array>
#include <span>
#include <cstdint>
#include <optional>
#include "torrent.hpp"

struct PeerInfo {
    std::string ip;
    uint16_t port;
    std::string id; // optional, empty if compact format was used
};

struct TrackerResponse {
    int64_t interval = 0;
    int64_t complete = 0;
    int64_t incomplete = 0;
    std::vector<PeerInfo> peers;
    std::string failure_reason;
};

class TrackerClient {
public:
    // Construct with torrent metadata and a unique 20-byte peer_id
    TrackerClient(const TorrentMetadata& torrent, std::span<const uint8_t, 20> peer_id);

    // Perform HTTP announce GET request to the tracker URL
    TrackerResponse announce(uint16_t listening_port,
                             uint64_t uploaded,
                             uint64_t downloaded,
                             uint64_t left,
                             std::optional<std::string> event = std::nullopt);

    // Utility helper for URL encoding raw data
    static std::string url_encode(std::span<const uint8_t> data);

    // Parse raw bencoded response body
    static TrackerResponse parse_response(std::span<const uint8_t> response_data);

private:
    TrackerResponse announce_http(const std::string& url,
                                  uint16_t listening_port,
                                  uint64_t uploaded,
                                  uint64_t downloaded,
                                  uint64_t left,
                                  std::optional<std::string> event);

    TrackerResponse announce_udp(const std::string& url,
                                 uint16_t listening_port,
                                 uint64_t uploaded,
                                 uint64_t downloaded,
                                 uint64_t left,
                                 std::optional<std::string> event);

    TorrentMetadata torrent_;
    std::array<uint8_t, 20> peer_id_;
};
