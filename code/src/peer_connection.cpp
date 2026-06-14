#include "peer_connection.hpp"
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <algorithm>
#include <type_traits>

PeerConnection::PeerConnection(EventLoop& loop, 
                               std::string ip, 
                               uint16_t port, 
                               std::array<uint8_t, 20> info_hash, 
                               std::array<uint8_t, 20> our_peer_id)
    : loop_(loop),
      ip_(std::move(ip)),
      port_(port),
      info_hash_(info_hash),
      our_peer_id_(our_peer_id) {}

PeerConnection::~PeerConnection() {
    disconnect();
}

void PeerConnection::connect() {
    if (state_ != ConnectionState::Idle) {
        return;
    }

    state_ = ConnectionState::Connecting;
    ConnectStatus status = conn_.connect_async(ip_, port_);

    if (status == ConnectStatus::Error) {
        state_ = ConnectionState::Disconnected;
        if (on_disconnect_cb_) {
            on_disconnect_cb_();
        }
        return;
    }

    std::weak_ptr<PeerConnection> weak_self = shared_from_this();
    loop_.register_fd(conn_.fd(), 
                      EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP,
                      [weak_self](int /*fd*/, uint32_t events) {
                          if (auto self = weak_self.lock()) {
                              self->handle_events(events);
                          }
                      });

    if (status == ConnectStatus::Connected) {
        state_ = ConnectionState::Handshaking;
        send_handshake();
        update_epoll_interests();
    }
}

void PeerConnection::disconnect() {
    if (state_ == ConnectionState::Disconnected) {
        return;
    }

    state_ = ConnectionState::Disconnected;

    if (conn_.is_open()) {
        loop_.unregister_fd(conn_.fd());
        conn_.close();
    }

    rx_buffer_.clear();
    tx_buffer_.clear();

    if (on_disconnect_cb_) {
        on_disconnect_cb_();
    }
}

void PeerConnection::send_message(const PeerMessage& msg) {
    if (state_ == ConnectionState::Disconnected) {
        return;
    }

    std::visit([this](auto&& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, ChokeMsg>) {
            am_choking_ = true;
        } else if constexpr (std::is_same_v<T, UnchokeMsg>) {
            am_choking_ = false;
        } else if constexpr (std::is_same_v<T, InterestedMsg>) {
            am_interested_ = true;
        } else if constexpr (std::is_same_v<T, NotInterestedMsg>) {
            am_interested_ = false;
        }
    }, msg.payload);

    auto bytes = msg.serialize();
    tx_buffer_.insert(tx_buffer_.end(), bytes.begin(), bytes.end());

    if (state_ == ConnectionState::Handshaking || state_ == ConnectionState::Active) {
        handle_write();
    }
}

void PeerConnection::send_handshake() {
    HandshakeMsg handshake;
    std::fill(handshake.reserved.begin(), handshake.reserved.end(), 0x00);
    handshake.info_hash = info_hash_;
    handshake.peer_id = our_peer_id_;

    auto bytes = handshake.serialize();
    tx_buffer_.insert(tx_buffer_.end(), bytes.begin(), bytes.end());

    handle_write();
}

void PeerConnection::handle_events(uint32_t events) {
    if (events & (EPOLLERR | EPOLLHUP)) {
        handle_error();
        return;
    }

    if (events & EPOLLRDHUP) {
        disconnect();
        return;
    }

    if (state_ == ConnectionState::Connecting) {
        if (events & EPOLLOUT) {
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(conn_.fd(), SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
                handle_error();
                return;
            }
            state_ = ConnectionState::Handshaking;
            send_handshake();
        }
    }

    if (events & EPOLLIN) {
        handle_read();
    }

    if (events & EPOLLOUT) {
        handle_write();
    }
}

void PeerConnection::handle_read() {
    uint8_t buffer[4096];
    bool closed = false;
    bool error = false;

    while (true) {
        ssize_t bytes_read = ::recv(conn_.fd(), buffer, sizeof(buffer), 0);
        if (bytes_read > 0) {
            rx_buffer_.insert(rx_buffer_.end(), buffer, buffer + bytes_read);
        } else if (bytes_read == 0) {
            closed = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else {
                error = true;
                break;
            }
        }
    }

    if (closed || error) {
        disconnect();
        return;
    }

    process_rx_buffer();
}

void PeerConnection::handle_write() {
    if (tx_buffer_.empty()) {
        update_epoll_interests();
        return;
    }

    size_t total_sent = 0;
    bool error = false;

    while (total_sent < tx_buffer_.size()) {
        ssize_t sent = ::send(conn_.fd(), 
                              tx_buffer_.data() + total_sent, 
                              tx_buffer_.size() - total_sent, 
                              MSG_NOSIGNAL);
        if (sent > 0) {
            total_sent += static_cast<size_t>(sent);
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else {
                error = true;
                break;
            }
        }
    }

    if (total_sent > 0) {
        tx_buffer_.erase(tx_buffer_.begin(), tx_buffer_.begin() + static_cast<std::ptrdiff_t>(total_sent));
    }

    if (error) {
        disconnect();
        return;
    }

    update_epoll_interests();
}

void PeerConnection::handle_error() {
    disconnect();
}

void PeerConnection::update_epoll_interests() {
    if (state_ == ConnectionState::Disconnected || state_ == ConnectionState::Idle) {
        return;
    }

    uint32_t events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
    if (!tx_buffer_.empty() || state_ == ConnectionState::Connecting) {
        events |= EPOLLOUT;
    }

    loop_.modify_fd(conn_.fd(), events);
}

void PeerConnection::process_rx_buffer() {
    while (!rx_buffer_.empty()) {
        if (state_ == ConnectionState::Handshaking) {
            if (rx_buffer_.size() < 68) {
                break;
            }

            auto handshake_opt = HandshakeMsg::deserialize(
                std::span<const uint8_t>(rx_buffer_.data(), 68));
            if (!handshake_opt) {
                disconnect();
                return;
            }

            rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + static_cast<std::ptrdiff_t>(68));
            state_ = ConnectionState::Active;

            if (on_handshake_cb_) {
                on_handshake_cb_(*handshake_opt);
            }
        } else if (state_ == ConnectionState::Active) {
            size_t consumed = 0;
            auto msg_opt = PeerMessage::deserialize(rx_buffer_, consumed);
            if (!msg_opt && consumed == 0) {
                break;
            }

            if (consumed > 0) {
                rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
            }

            if (msg_opt) {
                std::visit([this](auto&& msg) {
                    using T = std::decay_t<decltype(msg)>;
                    if constexpr (std::is_same_v<T, ChokeMsg>) {
                        peer_choking_ = true;
                    } else if constexpr (std::is_same_v<T, UnchokeMsg>) {
                        peer_choking_ = false;
                    } else if constexpr (std::is_same_v<T, InterestedMsg>) {
                        peer_interested_ = true;
                    } else if constexpr (std::is_same_v<T, NotInterestedMsg>) {
                        peer_interested_ = false;
                    }
                }, msg_opt->payload);

                if (on_message_cb_) {
                    on_message_cb_(*msg_opt);
                }
            }
        } else {
            rx_buffer_.clear();
            break;
        }
    }
}
