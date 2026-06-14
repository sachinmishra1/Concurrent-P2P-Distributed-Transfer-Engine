#include "tracker_client.hpp"
#include "bencode.hpp"
#include <curl/curl.h>
#include <stdexcept>
#include <sstream>

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
    static const char hex_chars[] = "0123456789ABCDEF";
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

TrackerResponse TrackerClient::announce(uint16_t listening_port,
                                         uint64_t uploaded,
                                         uint64_t downloaded,
                                         uint64_t left,
                                         std::optional<std::string> event) {
    ensure_curl_initialized();

    // Build query URL
    std::string url = torrent_.announce_url;
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
