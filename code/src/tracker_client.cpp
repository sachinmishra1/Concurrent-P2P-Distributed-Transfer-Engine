#include "tracker_client.hpp"
#include "bencode.hpp"
#include <curl/curl.h>
#include <stdexcept>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/time.h>
#include <cstring>
#include <cstdlib>
#include <spdlog/spdlog.h>

namespace {
    inline uint64_t swap64(uint64_t val) {
        return ((val & 0x00000000000000FFULL) << 56) |
               ((val & 0x000000000000FF00ULL) << 40) |
               ((val & 0x0000000000FF0000ULL) << 24) |
               ((val & 0x00000000FF000000ULL) << 8)  |
               ((val & 0x000000FF00000000ULL) >> 8)  |
               ((val & 0x0000FF0000000000ULL) >> 24) |
               ((val & 0x00FF000000000000ULL) >> 40) |
               ((val & 0xFF00000000000000ULL) >> 56);
    }

    inline uint64_t hton64(uint64_t val) {
        union { uint32_t i; char c[4]; } bint = {0x01020304};
        if (bint.c[0] == 1) {
            return val;
        } else {
            return swap64(val);
        }
    }

    inline uint64_t ntoh64(uint64_t val) {
        return hton64(val);
    }

    inline void write_uint64(uint8_t* buf, uint64_t val) {
        uint64_t net_val = hton64(val);
        std::memcpy(buf, &net_val, 8);
    }

    inline void write_uint32(uint8_t* buf, uint32_t val) {
        uint32_t net_val = htonl(val);
        std::memcpy(buf, &net_val, 4);
    }

    inline void write_uint16(uint8_t* buf, uint16_t val) {
        uint16_t net_val = htons(val);
        std::memcpy(buf, &net_val, 2);
    }

    inline uint64_t read_uint64(const uint8_t* buf) {
        uint64_t net_val;
        std::memcpy(&net_val, buf, 8);
        return ntoh64(net_val);
    }

    inline uint32_t read_uint32(const uint8_t* buf) {
        uint32_t net_val;
        std::memcpy(&net_val, buf, 4);
        return ntohl(net_val);
    }

    inline uint16_t read_uint16(const uint8_t* buf) {
        uint16_t net_val;
        std::memcpy(&net_val, buf, 2);
        return ntohs(net_val);
    }
}

// RAII helper to ensure curl_global_init is called once and cleaned up at shutdown
class CurlGlobal {
public:
    CurlGlobal() {
        ::curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~CurlGlobal() {
        ::curl_global_cleanup();
    }
};

static void ensure_curl_initialized() {
    static CurlGlobal global;
}

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* mem = static_cast<std::string*>(userp);
    mem->append(static_cast<const char*>(contents), realsize);
    return realsize;
}

TrackerClient::TrackerClient(const TorrentMetadata& torrent, std::span<const uint8_t, 20> peer_id)
    : torrent_(torrent) {
    std::copy(peer_id.begin(), peer_id.end(), peer_id_.begin());
}

std::string TrackerClient::url_encode(std::span<const uint8_t> data) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 3);

    for (uint8_t byte : data) {
        if ((byte >= '0' && byte <= '9') ||
            (byte >= 'a' && byte <= 'z') ||
            (byte >= 'A' && byte <= 'Z') ||
            byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(hex_chars[(byte >> 4) & 0x0F]);
            result.push_back(hex_chars[byte & 0x0F]);
        }
    }
    return result;
}

TrackerResponse TrackerClient::announce_http(const std::string& base_url,
                                             uint16_t listening_port,
                                             uint64_t uploaded,
                                             uint64_t downloaded,
                                             uint64_t left,
                                             std::optional<std::string> event) {
    ensure_curl_initialized();

    // Build query URL
    std::string url = base_url;
    url += (url.find('?') == std::string::npos) ? "?" : "&";

    url += "info_hash=" + url_encode(torrent_.info_hash);
    url += "&peer_id=" + url_encode(peer_id_);
    url += "&port=" + std::to_string(listening_port);
    url += "&uploaded=" + std::to_string(uploaded);
    url += "&downloaded=" + std::to_string(downloaded);
    url += "&left=" + std::to_string(left);
    url += "&compact=1";

    if (event.has_value() && !event->empty()) {
        url += "&event=" + *event;
    }

    // Perform HTTP request
    CURL* curl = ::curl_easy_init();
    if (!curl) {
        throw std::runtime_error("TrackerClient: failed to initialize curl handle");
    }

    std::string response_body;
    ::curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    ::curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    ::curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    ::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    // Some trackers require a User-Agent header
    ::curl_easy_setopt(curl, CURLOPT_USERAGENT, "ConcurrentP2PEngine/1.0");
    ::curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    CURLcode res = ::curl_easy_perform(curl);
    
    long response_code = 0;
    ::curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    ::curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("TrackerClient: HTTP request failed: ") + ::curl_easy_strerror(res));
    }

    if (response_code != 200) {
        throw std::runtime_error("TrackerClient: HTTP response code " + std::to_string(response_code) + 
                                 " (Response: " + response_body.substr(0, 200) + ")");
    }

    return parse_response(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(response_body.data()), response_body.size()));
}

TrackerResponse TrackerClient::announce_udp(const std::string& url,
                                            uint16_t listening_port,
                                            uint64_t uploaded,
                                            uint64_t downloaded,
                                            uint64_t left,
                                            std::optional<std::string> event) {
    if (url.size() < 6 || url.substr(0, 6) != "udp://") {
        throw std::runtime_error("TrackerClient: invalid UDP tracker URL scheme: " + url);
    }
    std::string host_port_path = url.substr(6);
    size_t slash_pos = host_port_path.find('/');
    std::string host_port = (slash_pos == std::string::npos) ? host_port_path : host_port_path.substr(0, slash_pos);
    
    size_t colon_pos = host_port.find(':');
    std::string host = (colon_pos == std::string::npos) ? host_port : host_port.substr(0, colon_pos);
    std::string port_str = (colon_pos == std::string::npos) ? "6969" : host_port.substr(colon_pos + 1);

    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* res = nullptr;
    int gai_err = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai_err != 0) {
        throw std::runtime_error("TrackerClient: DNS resolution failed for " + host + ": " + ::gai_strerror(gai_err));
    }

    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        throw std::runtime_error("TrackerClient: failed to create UDP socket: " + std::string(strerror(errno)));
    }

    auto cleanup = [fd, res]() {
        if (res) ::freeaddrinfo(res);
        if (fd >= 0) ::close(fd);
    };

    uint64_t connection_id = 0;
    bool connected = false;
    int retry_attempts = 3;
    int current_timeout = 3;
    uint8_t req_buf[16];
    uint8_t resp_buf[65535];
    
    write_uint64(req_buf, 0x41727101980ULL);
    write_uint32(req_buf + 8, 0);
    
    for (int attempt = 0; attempt < retry_attempts; ++attempt) {
        struct timeval tv;
        tv.tv_sec = current_timeout;
        tv.tv_usec = 0;
        if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            cleanup();
            throw std::runtime_error("TrackerClient: setsockopt failed: " + std::string(strerror(errno)));
        }
        
        uint32_t transaction_id = static_cast<uint32_t>(std::rand());
        write_uint32(req_buf + 12, transaction_id);
        
        spdlog::debug("TrackerClient: sending connect request to {}:{}, attempt {} (timeout {}s)", host, port_str, attempt + 1, current_timeout);
        
        if (::sendto(fd, req_buf, sizeof(req_buf), 0, res->ai_addr, res->ai_addrlen) < 0) {
            spdlog::warn("TrackerClient: sendto failed: {}", strerror(errno));
            current_timeout *= 2;
            continue;
        }
        
        ssize_t recvd = ::recvfrom(fd, resp_buf, sizeof(resp_buf), 0, nullptr, nullptr);
        if (recvd < 0) {
            spdlog::warn("TrackerClient: recvfrom failed or timed out: {}", strerror(errno));
            current_timeout *= 2;
            continue;
        }
        
        if (recvd < 16) {
            spdlog::warn("TrackerClient: connect response too short ({} bytes)", recvd);
            current_timeout *= 2;
            continue;
        }
        
        uint32_t resp_action = read_uint32(resp_buf);
        uint32_t resp_trans_id = read_uint32(resp_buf + 4);
        
        if (resp_trans_id != transaction_id) {
            spdlog::warn("TrackerClient: connect response transaction ID mismatch (got {}, expected {})", resp_trans_id, transaction_id);
            continue;
        }
        
        if (resp_action == 3) {
            std::string err_msg(reinterpret_cast<char*>(resp_buf + 8), static_cast<size_t>(recvd) - 8);
            cleanup();
            throw std::runtime_error("TrackerClient: UDP connect error: " + err_msg);
        }
        
        if (resp_action != 0) {
            spdlog::warn("TrackerClient: unexpected action in connect response: {}", resp_action);
            current_timeout *= 2;
            continue;
        }
        
        connection_id = read_uint64(resp_buf + 8);
        connected = true;
        spdlog::debug("TrackerClient: connected successfully. Connection ID: {}", connection_id);
        break;
    }
    
    if (!connected) {
        cleanup();
        throw std::runtime_error("TrackerClient: failed to connect to UDP tracker after " + std::to_string(retry_attempts) + " attempts");
    }

    uint32_t event_val = 0;
    if (event.has_value()) {
        if (*event == "completed") {
            event_val = 1;
        } else if (*event == "started") {
            event_val = 2;
        } else if (*event == "stopped") {
            event_val = 3;
        }
    }

    uint8_t ann_buf[98];
    write_uint64(ann_buf, connection_id);
    write_uint32(ann_buf + 8, 1);
    std::memcpy(ann_buf + 16, torrent_.info_hash.data(), 20);
    std::memcpy(ann_buf + 36, peer_id_.data(), 20);
    write_uint64(ann_buf + 56, downloaded);
    write_uint64(ann_buf + 64, left);
    write_uint64(ann_buf + 72, uploaded);
    write_uint32(ann_buf + 80, event_val);
    write_uint32(ann_buf + 84, 0);
    uint32_t key_val = static_cast<uint32_t>(std::rand());
    write_uint32(ann_buf + 88, key_val);
    write_uint32(ann_buf + 92, static_cast<uint32_t>(-1));
    write_uint16(ann_buf + 96, listening_port);

    bool announced = false;
    current_timeout = 3;
    TrackerResponse tracker_res;

    for (int attempt = 0; attempt < retry_attempts; ++attempt) {
        struct timeval tv;
        tv.tv_sec = current_timeout;
        tv.tv_usec = 0;
        if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            cleanup();
            throw std::runtime_error("TrackerClient: setsockopt failed: " + std::string(strerror(errno)));
        }
        
        uint32_t transaction_id = static_cast<uint32_t>(std::rand());
        write_uint32(ann_buf + 12, transaction_id);
        
        spdlog::debug("TrackerClient: sending announce request to {}:{}, attempt {} (timeout {}s)", host, port_str, attempt + 1, current_timeout);
        
        if (::sendto(fd, ann_buf, sizeof(ann_buf), 0, res->ai_addr, res->ai_addrlen) < 0) {
            spdlog::warn("TrackerClient: announce sendto failed: {}", strerror(errno));
            current_timeout *= 2;
            continue;
        }
        
        ssize_t recvd = ::recvfrom(fd, resp_buf, sizeof(resp_buf), 0, nullptr, nullptr);
        if (recvd < 0) {
            spdlog::warn("TrackerClient: announce recvfrom failed or timed out: {}", strerror(errno));
            current_timeout *= 2;
            continue;
        }
        
        if (recvd < 20) {
            spdlog::warn("TrackerClient: announce response too short ({} bytes)", recvd);
            current_timeout *= 2;
            continue;
        }
        
        uint32_t resp_action = read_uint32(resp_buf);
        uint32_t resp_trans_id = read_uint32(resp_buf + 4);
        
        if (resp_trans_id != transaction_id) {
            spdlog::warn("TrackerClient: announce response transaction ID mismatch (got {}, expected {})", resp_trans_id, transaction_id);
            continue;
        }
        
        if (resp_action == 3) {
            std::string err_msg(reinterpret_cast<char*>(resp_buf + 8), static_cast<size_t>(recvd) - 8);
            cleanup();
            throw std::runtime_error("TrackerClient: UDP announce error: " + err_msg);
        }
        
        if (resp_action != 1) {
            spdlog::warn("TrackerClient: unexpected action in announce response: {}", resp_action);
            current_timeout *= 2;
            continue;
        }
        
        tracker_res.interval = read_uint32(resp_buf + 8);
        tracker_res.incomplete = read_uint32(resp_buf + 12);
        tracker_res.complete = read_uint32(resp_buf + 16);
        
        size_t peers_offset = 20;
        size_t peers_bytes = static_cast<size_t>(recvd) - peers_offset;
        if (peers_bytes % 6 != 0) {
            spdlog::warn("TrackerClient: peers data size not a multiple of 6 ({} bytes)", peers_bytes);
        }
        
        size_t num_peers = peers_bytes / 6;
        tracker_res.peers.clear();
        tracker_res.peers.reserve(num_peers);
        
        for (size_t i = 0; i < num_peers; ++i) {
            const uint8_t* peer_ptr = resp_buf + peers_offset + i * 6;
            uint8_t ip1 = peer_ptr[0];
            uint8_t ip2 = peer_ptr[1];
            uint8_t ip3 = peer_ptr[2];
            uint8_t ip4 = peer_ptr[3];
            uint16_t p_port = read_uint16(peer_ptr + 4);
            
            PeerInfo peer;
            peer.ip = std::to_string(ip1) + "." + std::to_string(ip2) + "." + std::to_string(ip3) + "." + std::to_string(ip4);
            peer.port = p_port;
            tracker_res.peers.push_back(std::move(peer));
        }
        
        announced = true;
        break;
    }

    cleanup();

    if (!announced) {
        throw std::runtime_error("TrackerClient: failed to announce to UDP tracker after " + std::to_string(retry_attempts) + " attempts");
    }

    return tracker_res;
}

TrackerResponse TrackerClient::announce(uint16_t listening_port,
                                         uint64_t uploaded,
                                         uint64_t downloaded,
                                         uint64_t left,
                                         std::optional<std::string> event) {
    std::vector<std::string> errors;
    
    for (const auto& url : torrent_.announce_list) {
        try {
            if (url.size() >= 6 && url.substr(0, 6) == "udp://") {
                return announce_udp(url, listening_port, uploaded, downloaded, left, event);
            } else if (url.size() >= 7 && (url.substr(0, 7) == "http://" || (url.size() >= 8 && url.substr(0, 8) == "https://"))) {
                return announce_http(url, listening_port, uploaded, downloaded, left, event);
            } else {
                spdlog::warn("TrackerClient: unsupported tracker protocol scheme for URL: {}", url);
                errors.push_back(url + ": unsupported scheme");
            }
        } catch (const std::exception& e) {
            spdlog::warn("TrackerClient: announce to {} failed: {}", url, e.what());
            errors.push_back(url + ": " + e.what());
        }
    }
    
    std::string combined_error = "TrackerClient: announce failed for all trackers. Errors:\n";
    for (const auto& err : errors) {
        combined_error += "  - " + err + "\n";
    }
    throw std::runtime_error(combined_error);
}

TrackerResponse TrackerClient::parse_response(std::span<const uint8_t> response_data) {
    BencodeValue root;
    try {
        root = BencodeParser::parse(response_data);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("TrackerClient: failed to parse bencoded response: ") + e.what());
    }

    if (!root.is_dict()) {
        throw std::runtime_error("TrackerClient: response root is not a bencoded dictionary");
    }

    const auto& dict = root.as_dict();
    TrackerResponse parsed_res;

    // Check for failure reason first
    auto fail_it = dict.find("failure reason");
    if (fail_it != dict.end()) {
        parsed_res.failure_reason = std::string(fail_it->second.as_string());
        return parsed_res;
    }

    // Interval
    auto int_it = dict.find("interval");
    if (int_it != dict.end()) {
        parsed_res.interval = int_it->second.as_int();
    }

    // Complete (seeders)
    auto comp_it = dict.find("complete");
    if (comp_it != dict.end()) {
        parsed_res.complete = comp_it->second.as_int();
    }

    // Incomplete (leechers)
    auto incomp_it = dict.find("incomplete");
    if (incomp_it != dict.end()) {
        parsed_res.incomplete = incomp_it->second.as_int();
    }

    // Peers
    auto peers_it = dict.find("peers");
    if (peers_it != dict.end()) {
        if (peers_it->second.is_string()) {
            std::string_view compact_peers = peers_it->second.as_string();
            if (compact_peers.size() % 6 != 0) {
                throw std::runtime_error("TrackerClient: compact peers string length must be a multiple of 6");
            }
            for (size_t i = 0; i < compact_peers.size(); i += 6) {
                uint8_t ip1 = static_cast<uint8_t>(compact_peers[i]);
                uint8_t ip2 = static_cast<uint8_t>(compact_peers[i+1]);
                uint8_t ip3 = static_cast<uint8_t>(compact_peers[i+2]);
                uint8_t ip4 = static_cast<uint8_t>(compact_peers[i+3]);
                uint16_t port = (static_cast<uint8_t>(compact_peers[i+4]) << 8) | static_cast<uint8_t>(compact_peers[i+5]);

                PeerInfo peer;
                peer.ip = std::to_string(ip1) + "." + std::to_string(ip2) + "." + std::to_string(ip3) + "." + std::to_string(ip4);
                peer.port = port;
                parsed_res.peers.push_back(std::move(peer));
            }
        } else if (peers_it->second.is_list()) {
            const auto& peer_list = peers_it->second.as_list();
            for (const auto& peer_val : peer_list) {
                if (peer_val.is_dict()) {
                    const auto& p_dict = peer_val.as_dict();
                    PeerInfo peer;
                    auto ip_it = p_dict.find("ip");
                    if (ip_it != p_dict.end()) {
                        peer.ip = std::string(ip_it->second.as_string());
                    }
                    auto port_it = p_dict.find("port");
                    if (port_it != p_dict.end()) {
                        peer.port = static_cast<uint16_t>(port_it->second.as_int());
                    }
                    auto id_it = p_dict.find("peer id");
                    if (id_it != p_dict.end()) {
                        peer.id = std::string(id_it->second.as_string());
                    }
                    parsed_res.peers.push_back(std::move(peer));
                }
            }
        }
    }

    return parsed_res;
}
