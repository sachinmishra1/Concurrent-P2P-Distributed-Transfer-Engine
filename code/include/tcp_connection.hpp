#pragma once

#include <string_view>
#include <span>
#include <cstdint>
#include <cstddef>

enum class SocketStatus {
    Success,
    WouldBlock,
    Closed,
    Error
};

struct IoResult {
    SocketStatus status;
    size_t bytes_transferred;
};

enum class ConnectStatus {
    Connected,
    InProgress,
    Error
};

class TcpConnection {
public:
    // Default constructor (uninitialized)
    TcpConnection();
    
    // Wrap an existing active socket descriptor
    explicit TcpConnection(int fd);
    
    // Destructor: automatically closes the fd if open
    ~TcpConnection();

    // Move-only semantics
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& other) noexcept;
    TcpConnection& operator=(TcpConnection&& other) noexcept;

    // Initiate a non-blocking TCP connection
    ConnectStatus connect_async(std::string_view ip, uint16_t port);

    // Send data (non-blocking)
    IoResult send(std::span<const uint8_t> data);

    // Receive data (non-blocking)
    IoResult recv(std::span<uint8_t> buf);

    // Explicitly close the file descriptor
    void close();

    // Access the underlying file descriptor
    int fd() const { return fd_; }

    // Check if the connection has a valid file descriptor
    bool is_open() const { return fd_ != -1; }

private:
    int fd_ = -1;
};
