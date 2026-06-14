#pragma once

#include "tcp_connection.hpp"
#include "event_loop.hpp"
#include "peer_message.hpp"
#include <sys/epoll.h>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <memory>
#include <array>

enum class ConnectionState {
    Idle,
    Connecting,
    Handshaking,
    Active,
    Disconnected
};

class PeerConnection : public std::enable_shared_from_this<PeerConnection> {
public:
    // Factory method to enforce shared_ptr lifetime management
    static std::shared_ptr<PeerConnection> create(EventLoop& loop, 
                                                  std::string ip, 
                                                  uint16_t port, 
                                                  std::array<uint8_t, 20> info_hash, 
                                                  std::array<uint8_t, 20> our_peer_id) {
        return std::shared_ptr<PeerConnection>(new PeerConnection(loop, std::move(ip), port, info_hash, our_peer_id));
    }

    // Factory method to create PeerConnection from an existing socket (useful for tests)
    static std::shared_ptr<PeerConnection> create_with_socket(EventLoop& loop, 
                                                              int socket_fd, 
                                                              std::array<uint8_t, 20> info_hash, 
                                                              std::array<uint8_t, 20> our_peer_id) {
        auto conn = std::shared_ptr<PeerConnection>(new PeerConnection(loop, "127.0.0.1", 0, info_hash, our_peer_id));
        conn->conn_ = TcpConnection(socket_fd);
        conn->state_ = ConnectionState::Handshaking; // already connected TCP
        
        std::weak_ptr<PeerConnection> weak_self = conn->shared_from_this();
        loop.register_fd(socket_fd, 
                         EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP,
                         [weak_self](int /*fd*/, uint32_t events) {
                             if (auto self = weak_self.lock()) {
                                 self->handle_events(events);
                             }
                         });
        conn->send_handshake();
        conn->update_epoll_interests();
        return conn;
    }

    ~PeerConnection();

    // Prevent copy/move
    PeerConnection(const PeerConnection&) = delete;
    PeerConnection& operator=(const PeerConnection&) = delete;
    PeerConnection(PeerConnection&&) = delete;
    PeerConnection& operator=(PeerConnection&&) = delete;

    // Asynchronously initiate connection
    void connect();

    // Terminate connection
    void disconnect();

    // Send a peer wire message
    void send_message(const PeerMessage& msg);

    // Getters
    ConnectionState get_state() const { return state_; }
    const std::string& ip() const { return ip_; }
    uint16_t port() const { return port_; }
    bool is_choking() const { return am_choking_; }
    bool is_interested() const { return am_interested_; }
    bool is_peer_choking() const { return peer_choking_; }
    bool is_peer_interested() const { return peer_interested_; }

    // Register callbacks
    void on_handshake(std::function<void(const HandshakeMsg&)> cb) { on_handshake_cb_ = std::move(cb); }
    void on_message(std::function<void(const PeerMessage&)> cb) { on_message_cb_ = std::move(cb); }
    void on_disconnect(std::function<void()> cb) { on_disconnect_cb_ = std::move(cb); }

    // Direct access to socket FD for testing/event loop purposes
    int get_fd() const { return conn_.fd(); }

private:
    PeerConnection(EventLoop& loop, 
                   std::string ip, 
                   uint16_t port, 
                   std::array<uint8_t, 20> info_hash, 
                   std::array<uint8_t, 20> our_peer_id);

    void handle_events(uint32_t events);
    void handle_read();
    void handle_write();
    void handle_error();
    void send_handshake();
    void update_epoll_interests();
    void process_rx_buffer();

    EventLoop& loop_;
    std::string ip_;
    uint16_t port_;
    std::array<uint8_t, 20> info_hash_;
    std::array<uint8_t, 20> our_peer_id_;

    TcpConnection conn_;
    ConnectionState state_ = ConnectionState::Idle;

    // BitTorrent choking and interest state flags
    bool am_choking_ = true;
    bool am_interested_ = false;
    bool peer_choking_ = true;
    bool peer_interested_ = false;

    std::vector<uint8_t> rx_buffer_;
    std::vector<uint8_t> tx_buffer_;

    std::function<void(const HandshakeMsg&)> on_handshake_cb_;
    std::function<void(const PeerMessage&)> on_message_cb_;
    std::function<void()> on_disconnect_cb_;
};
