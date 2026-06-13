#include "tcp_connection.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utility>
#include <string>

TcpConnection::TcpConnection() : fd_(-1) {}

TcpConnection::TcpConnection(int fd) : fd_(fd) {}

TcpConnection::~TcpConnection() {
    close();
}

TcpConnection::TcpConnection(TcpConnection&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

ConnectStatus TcpConnection::connect_async(std::string_view ip, uint16_t port) {
    close();

    // Create IPv4 TCP socket
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        return ConnectStatus::Error;
    }

    // Set non-blocking flag
    int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        close();
        return ConnectStatus::Error;
    }

    // Prepare address
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    // Parse IP string
    std::string ip_str(ip);
    if (::inet_pton(AF_INET, ip_str.c_str(), &addr.sin_addr) <= 0) {
        close();
        return ConnectStatus::Error;
    }

    // Attempt non-blocking connection
    int ret = ::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret == 0) {
        return ConnectStatus::Connected;
    }

    if (errno == EINPROGRESS) {
        return ConnectStatus::InProgress;
    }

    close();
    return ConnectStatus::Error;
}

IoResult TcpConnection::send(std::span<const uint8_t> data) {
    if (fd_ == -1) {
        return IoResult{SocketStatus::Error, 0};
    }

    if (data.empty()) {
        return IoResult{SocketStatus::Success, 0};
    }

    ssize_t ret = ::send(fd_, data.data(), data.size(), MSG_NOSIGNAL);
    if (ret >= 0) {
        return IoResult{SocketStatus::Success, static_cast<size_t>(ret)};
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return IoResult{SocketStatus::WouldBlock, 0};
    }

    if (errno == EPIPE || errno == ECONNRESET) {
        return IoResult{SocketStatus::Closed, 0};
    }

    return IoResult{SocketStatus::Error, 0};
}

IoResult TcpConnection::recv(std::span<uint8_t> buf) {
    if (fd_ == -1) {
        return IoResult{SocketStatus::Error, 0};
    }

    if (buf.empty()) {
        return IoResult{SocketStatus::Success, 0};
    }

    ssize_t ret = ::recv(fd_, buf.data(), buf.size(), 0);
    if (ret > 0) {
        return IoResult{SocketStatus::Success, static_cast<size_t>(ret)};
    }

    if (ret == 0) {
        return IoResult{SocketStatus::Closed, 0};
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return IoResult{SocketStatus::WouldBlock, 0};
    }

    if (errno == ECONNRESET) {
        return IoResult{SocketStatus::Closed, 0};
    }

    return IoResult{SocketStatus::Error, 0};
}

void TcpConnection::close() {
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}
