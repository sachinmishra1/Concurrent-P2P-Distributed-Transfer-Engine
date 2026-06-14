#include <gtest/gtest.h>
#include "bencode.hpp"
#include "torrent.hpp"
#include "hasher.hpp"
#include "tcp_connection.hpp"
#include "event_loop.hpp"
#include "tracker_client.hpp"
#include "peer_id.hpp"
#include "peer_message.hpp"
#include "peer_connection.hpp"
#include "bitfield.hpp"
#include "peer_manager.hpp"
#include <cryptopp/sha.h>
#include <filesystem>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Helper to convert string_view to span
std::span<const uint8_t> to_span(std::string_view sv) {
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
}

// 1. Positive Integer Test
TEST(BencodeIntTest, Positive) {
    auto val = BencodeParser::parse(to_span("i42e"));
    EXPECT_TRUE(val.is_int());
    EXPECT_EQ(val.as_int(), 42);
}

// 2. Negative Integer Test
TEST(BencodeIntTest, Negative) {
    auto val = BencodeParser::parse(to_span("i-999e"));
    EXPECT_TRUE(val.is_int());
    EXPECT_EQ(val.as_int(), -999);
}

// 3. Zero Integer Test
TEST(BencodeIntTest, Zero) {
    auto val = BencodeParser::parse(to_span("i0e"));
    EXPECT_TRUE(val.is_int());
    EXPECT_EQ(val.as_int(), 0);
}

// 4. Leading Zeros Integer (Invalid)
TEST(BencodeIntTest, LeadingZerosInvalid) {
    EXPECT_THROW(BencodeParser::parse(to_span("i03e")), std::runtime_error);
    EXPECT_THROW(BencodeParser::parse(to_span("i00e")), std::runtime_error);
}

// 5. Negative Zero Integer (Invalid)
TEST(BencodeIntTest, NegativeZeroInvalid) {
    EXPECT_THROW(BencodeParser::parse(to_span("i-0e")), std::runtime_error);
}

// 6. Integer Overflow
TEST(BencodeIntTest, Overflow) {
    EXPECT_THROW(BencodeParser::parse(to_span("i9223372036854775808e")), std::runtime_error); // Max int64_t + 1
}

// 7. Simple String Test
TEST(BencodeStringTest, Simple) {
    auto val = BencodeParser::parse(to_span("5:hello"));
    EXPECT_TRUE(val.is_string());
    EXPECT_EQ(val.as_string(), "hello");
}

// 8. Empty String Test
TEST(BencodeStringTest, Empty) {
    auto val = BencodeParser::parse(to_span("0:"));
    EXPECT_TRUE(val.is_string());
    EXPECT_EQ(val.as_string(), "");
}

// 9. Invalid String (Leading Zeros / Unexpected End)
TEST(BencodeStringTest, Invalid) {
    EXPECT_THROW(BencodeParser::parse(to_span("03:abc")), std::runtime_error);
    EXPECT_THROW(BencodeParser::parse(to_span("5:abc")), std::runtime_error);
    EXPECT_THROW(BencodeParser::parse(to_span("abc")), std::runtime_error);
}

// 10. Simple List Test
TEST(BencodeListTest, Simple) {
    auto val = BencodeParser::parse(to_span("li42e4:spame"));
    EXPECT_TRUE(val.is_list());
    const auto& list = val.as_list();
    ASSERT_EQ(list.size(), 2);
    EXPECT_EQ(list[0].as_int(), 42);
    EXPECT_EQ(list[1].as_string(), "spam");
}

// 11. Nested List Test
TEST(BencodeListTest, Nested) {
    auto val = BencodeParser::parse(to_span("llli1eeee"));
    EXPECT_TRUE(val.is_list());
    EXPECT_EQ(val.as_list().size(), 1);
    EXPECT_TRUE(val.as_list()[0].is_list());
}

// 12. Invalid List (Missing Closing 'e')
TEST(BencodeListTest, MissingClosingE) {
    EXPECT_THROW(BencodeParser::parse(to_span("li42e")), std::runtime_error);
}

// 13. Simple Dictionary Test
TEST(BencodeDictTest, Simple) {
    auto val = BencodeParser::parse(to_span("d3:cow3:moo4:spam4:eggse"));
    EXPECT_TRUE(val.is_dict());
    const auto& dict = val.as_dict();
    ASSERT_EQ(dict.size(), 2);
    EXPECT_EQ(dict.at("cow").as_string(), "moo");
    EXPECT_EQ(dict.at("spam").as_string(), "eggs");
}

// 14. Dictionary Sorting Checks (Unsorted is Invalid)
TEST(BencodeDictTest, UnsortedKeysInvalid) {
    EXPECT_THROW(BencodeParser::parse(to_span("d4:spam4:eggs3:cow3:mooe")), std::runtime_error);
}

// 15. Dictionary Duplicates Check (Duplicate is Invalid)
TEST(BencodeDictTest, DuplicateKeysInvalid) {
    EXPECT_THROW(BencodeParser::parse(to_span("d3:cow3:moo3:cow3:mooe")), std::runtime_error);
}

// 16. Roundtrip Integers
TEST(BencodeRoundtripTest, Integers) {
    std::string_view inputs[] = {"i42e", "i-42e", "i0e"};
    for (auto input : inputs) {
        auto parsed = BencodeParser::parse(to_span(input));
        auto encoded = BencodeParser::encode(parsed);
        std::string_view output(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        EXPECT_EQ(input, output);
    }
}

// 17. Roundtrip Strings
TEST(BencodeRoundtripTest, Strings) {
    std::string_view inputs[] = {"5:hello", "0:"};
    for (auto input : inputs) {
        auto parsed = BencodeParser::parse(to_span(input));
        auto encoded = BencodeParser::encode(parsed);
        std::string_view output(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        EXPECT_EQ(input, output);
    }
}

// 18. Roundtrip Lists
TEST(BencodeRoundtripTest, Lists) {
    std::string_view inputs[] = {
        "li42e4:spame",
        "l5:helloi-123ed3:cow3:mooee"
    };
    for (auto input : inputs) {
        auto parsed = BencodeParser::parse(to_span(input));
        auto encoded = BencodeParser::encode(parsed);
        std::string_view output(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        EXPECT_EQ(input, output);
    }
}

// 19. Roundtrip Dictionaries
TEST(BencodeRoundtripTest, Dictionaries) {
    std::string_view inputs[] = {
        "d3:cow3:moo4:spam4:eggse",
        "d1:a1:b1:c1:de"
    };
    for (auto input : inputs) {
        auto parsed = BencodeParser::parse(to_span(input));
        auto encoded = BencodeParser::encode(parsed);
        std::string_view output(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        EXPECT_EQ(input, output);
    }
}

// 20. Torrent Metadata Single-File Parsing
TEST(TorrentMetadataTest, SingleFile) {
    // Announce: http://tracker/announce
    // Info:
    //   length: 100
    //   name: test.txt
    //   piece length: 50
    //   pieces: 40 bytes (2 pieces) of SHA-1 hashes (zeros here for placeholder)
    std::string bencode = 
        "d8:announce23:http://tracker/announce"
        "4:infod6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces40:0000000000000000000000000000000000000000ee";

    auto meta = TorrentMetadata::from_bencode(to_span(bencode));
    EXPECT_EQ(meta.announce_url, "http://tracker/announce");
    EXPECT_EQ(meta.name, "test.txt");
    EXPECT_EQ(meta.piece_length, 50);
    EXPECT_EQ(meta.total_length, 100);
    EXPECT_EQ(meta.num_pieces, 2);
    ASSERT_EQ(meta.piece_hashes.size(), 2);
    EXPECT_EQ(meta.files.size(), 1);
    EXPECT_EQ(meta.files[0].path, "test.txt");
    EXPECT_EQ(meta.files[0].length, 100);
    EXPECT_EQ(meta.files[0].offset, 0);
}

// 21. Torrent Metadata Multi-File Parsing
TEST(TorrentMetadataTest, MultiFile) {
    // Announce: http://tracker/announce
    // Info:
    //   name: mydir
    //   piece length: 256
    //   pieces: 20 bytes (1 piece) of SHA-1 hash (all zeros)
    //   files:
    //     - length: 150
    //       path: ["subdir", "file1.txt"]
    //     - length: 100
    //       path: ["file2.txt"]
    std::string bencode = 
        "d8:announce23:http://tracker/announce"
        "4:infod5:filesld6:lengthi150e4:pathl6:subdir9:file1.txteed6:lengthi100e4:pathl9:file2.txteee"
        "4:name5:mydir12:piece lengthi256e6:pieces20:00000000000000000000ee";

    auto meta = TorrentMetadata::from_bencode(to_span(bencode));
    EXPECT_EQ(meta.announce_url, "http://tracker/announce");
    EXPECT_EQ(meta.name, "mydir");
    EXPECT_EQ(meta.piece_length, 256);
    EXPECT_EQ(meta.total_length, 250);
    EXPECT_EQ(meta.num_pieces, 1);
    ASSERT_EQ(meta.piece_hashes.size(), 1);
    
    ASSERT_EQ(meta.files.size(), 2);
    EXPECT_EQ(meta.files[0].path, "subdir/file1.txt");
    EXPECT_EQ(meta.files[0].length, 150);
    EXPECT_EQ(meta.files[0].offset, 0);
    
    EXPECT_EQ(meta.files[1].path, "file2.txt");
    EXPECT_EQ(meta.files[1].length, 100);
    EXPECT_EQ(meta.files[1].offset, 150);
}

// 22. Torrent Metadata Invalid Parsing Checks
TEST(TorrentMetadataTest, Invalid) {
    // Missing announce
    std::string no_announce = 
        "d4:infod6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces20:00000000000000000000ee";
    EXPECT_THROW(TorrentMetadata::from_bencode(to_span(no_announce)), std::runtime_error);

    // Mismatched pieces count vs length
    std::string mismatched_pieces = 
        "d8:announce23:http://tracker/announce"
        "4:infod6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces20:00000000000000000000ee"; // length is 100, piece_length is 50, so needs 2 pieces (40 bytes), but pieces has only 20 bytes (1 piece)
    EXPECT_THROW(TorrentMetadata::from_bencode(to_span(mismatched_pieces)), std::runtime_error);
}

// 23. Torrent Metadata Info Hash Calculation
TEST(TorrentMetadataTest, InfoHashCalculation) {
    std::string bencode = 
        "d8:announce23:http://tracker/announce"
        "4:infod6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces40:0000000000000000000000000000000000000000ee";

    auto meta = TorrentMetadata::from_bencode(to_span(bencode));

    std::string expected_info_bytes = 
        "d6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces40:0000000000000000000000000000000000000000e";

    std::array<uint8_t, 20> expected_hash{};
    CryptoPP::SHA1 sha1;
    sha1.CalculateDigest(expected_hash.data(), 
                         reinterpret_cast<const uint8_t*>(expected_info_bytes.data()), 
                         expected_info_bytes.size());

    EXPECT_EQ(meta.info_hash, expected_hash);
}

// 24. SHA1Hasher One-Shot Hashing
TEST(SHA1HasherTest, OneShot) {
    std::string data = "abc";
    auto digest = SHA1Hasher::hash(data);
    
    // Known SHA-1 of "abc" is a9993e364706816aba3e25717850c26c9cd0d89d
    std::array<uint8_t, 20> expected_digest = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d
    };
    
    EXPECT_EQ(digest, expected_digest);
}

// 25. SHA1Hasher Incremental Hashing
TEST(SHA1HasherTest, Incremental) {
    SHA1Hasher hasher;
    hasher.update("a");
    hasher.update("b");
    hasher.update("c");
    auto digest = hasher.finalize();

    std::array<uint8_t, 20> expected_digest = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d
    };
    
    EXPECT_EQ(digest, expected_digest);

    // Verify reset/restart functionality
    hasher.reset();
    hasher.update("abc");
    EXPECT_EQ(hasher.finalize(), expected_digest);
}

// 26. TcpConnection Move and RAII Semantics
TEST(TcpConnectionTest, MoveAndRAII) {
    TcpConnection conn;
    EXPECT_FALSE(conn.is_open());
    EXPECT_EQ(conn.fd(), -1);

    // Initiating connection to invalid IP should fail
    ConnectStatus status = conn.connect_async("999.999.999.999", 80);
    EXPECT_EQ(status, ConnectStatus::Error);
    EXPECT_FALSE(conn.is_open());

    // Connect to invalid format
    status = conn.connect_async("invalid-ip", 80);
    EXPECT_EQ(status, ConnectStatus::Error);
    EXPECT_FALSE(conn.is_open());

    // Connect to local loopback address to allocate a descriptor
    status = conn.connect_async("127.0.0.1", 12345);
    // Connection could be Refused immediately (Error), InProgress, or Connected.
    // If it allocated an open socket, test move semantics:
    if (conn.is_open()) {
        int original_fd = conn.fd();
        EXPECT_NE(original_fd, -1);

        // Move construct
        TcpConnection conn2(std::move(conn));
        EXPECT_FALSE(conn.is_open());
        EXPECT_EQ(conn.fd(), -1);
        EXPECT_TRUE(conn2.is_open());
        EXPECT_EQ(conn2.fd(), original_fd);

        // Move assign
        TcpConnection conn3;
        conn3 = std::move(conn2);
        EXPECT_FALSE(conn2.is_open());
        EXPECT_TRUE(conn3.is_open());
        EXPECT_EQ(conn3.fd(), original_fd);

        conn3.close();
        EXPECT_FALSE(conn3.is_open());
    }
}

// 27. EventLoop Timer test
TEST(EventLoopTest, Timers) {
    EventLoop loop;
    bool timer1_fired = false;
    bool timer2_fired = false;
    bool timer3_fired = false;

    auto start = std::chrono::steady_clock::now();

    loop.register_timer(std::chrono::milliseconds(10), [&]() {
        timer1_fired = true;
    });

    uint64_t id2 = loop.register_timer(std::chrono::milliseconds(20), [&]() {
        timer2_fired = true;
    });

    loop.register_timer(std::chrono::milliseconds(35), [&]() {
        timer3_fired = true;
        loop.shutdown();
    });

    loop.cancel_timer(id2);

    loop.run();

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    EXPECT_TRUE(timer1_fired);
    EXPECT_FALSE(timer2_fired); // cancelled
    EXPECT_TRUE(timer3_fired);
    EXPECT_GE(duration, 30);
}

// Helper to get count of open file descriptors
static size_t get_open_fd_count() {
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        (void)entry;
        count++;
    }
    return count;
}

// 28. EventLoop Echo Server integration test
TEST(EventLoopTest, EchoServer) {
    EventLoop loop;

    // Create listening socket on ephemeral port
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);

    // Set reuseaddr
    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 10), 0);

    socklen_t addr_len = sizeof(addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len), 0);
    uint16_t port = ::ntohs(addr.sin_port);

    std::vector<int> server_client_fds;
    
    // Register listening socket
    loop.register_fd(listen_fd, EPOLLIN, [&](int fd, uint32_t events) {
        (void)events;
        struct sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = ::accept(fd, reinterpret_cast<struct sockaddr*>(&client_addr), &len);
        if (client_fd >= 0) {
            // Set non-blocking
            int flags = ::fcntl(client_fd, F_GETFL, 0);
            ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

            server_client_fds.push_back(client_fd);

            // Register read callback
            loop.register_fd(client_fd, EPOLLIN, [&](int cfd, uint32_t cevents) {
                (void)cevents;
                uint8_t buf[256];
                ssize_t n = ::recv(cfd, buf, sizeof(buf), 0);
                if (n > 0) {
                    ::send(cfd, buf, static_cast<size_t>(n), MSG_NOSIGNAL);
                } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    loop.unregister_fd(cfd);
                    ::close(cfd);
                }
            });
        }
    });

    // Client connection
    TcpConnection client;
    ConnectStatus c_status = client.connect_async("127.0.0.1", port);
    ASSERT_NE(c_status, ConnectStatus::Error);

    std::string received_data;
    bool client_writable_fired = false;

    loop.register_fd(client.fd(), EPOLLIN | EPOLLOUT, [&](int fd, uint32_t events) {
        if (events & EPOLLOUT) {
            if (!client_writable_fired) {
                client_writable_fired = true;
                std::string msg = "hello echo";
                ::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL);
                loop.modify_fd(fd, EPOLLIN);
            }
        }
        if (events & EPOLLIN) {
            char buf[256];
            ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                received_data = buf;
            }
            loop.shutdown();
        }
    });

    loop.run();

    // Cleanup
    loop.unregister_fd(listen_fd);
    ::close(listen_fd);
    for (int fd : server_client_fds) {
        loop.unregister_fd(fd);
        ::close(fd);
    }
    loop.unregister_fd(client.fd());
    client.close();

    EXPECT_TRUE(client_writable_fired);
    EXPECT_EQ(received_data, "hello echo");
}

// 29. EventLoop Timer Accuracy test
TEST(EventLoopTest, TimerAccuracy) {
    EventLoop loop;
    auto start = std::chrono::steady_clock::now();
    std::vector<std::chrono::steady_clock::time_point> fire_times;

    loop.register_timer(std::chrono::milliseconds(5), [&]() {
        fire_times.push_back(std::chrono::steady_clock::now());
    });

    loop.register_timer(std::chrono::milliseconds(15), [&]() {
        fire_times.push_back(std::chrono::steady_clock::now());
    });

    loop.register_timer(std::chrono::milliseconds(25), [&]() {
        fire_times.push_back(std::chrono::steady_clock::now());
        loop.shutdown();
    });

    loop.run();

    ASSERT_EQ(fire_times.size(), 3);
    auto diff1 = std::chrono::duration_cast<std::chrono::milliseconds>(fire_times[0] - start).count();
    auto diff2 = std::chrono::duration_cast<std::chrono::milliseconds>(fire_times[1] - start).count();
    auto diff3 = std::chrono::duration_cast<std::chrono::milliseconds>(fire_times[2] - start).count();

    EXPECT_GE(diff1, 4); // allow 1ms float/resolution rounding
    EXPECT_GE(diff2, 14);
    EXPECT_GE(diff3, 24);
}

// 30. EventLoop FD Churn rapid register/unregister test
TEST(EventLoopTest, FdChurn) {
    EventLoop loop;
    
    constexpr int NUM_PAIRS = 50;
    std::vector<int> fds_to_close;
    fds_to_close.reserve(NUM_PAIRS * 2);

    for (int i = 0; i < NUM_PAIRS; ++i) {
        int sv[2];
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
        fds_to_close.push_back(sv[0]);
        fds_to_close.push_back(sv[1]);

        bool reg1 = loop.register_fd(sv[0], EPOLLIN, [](int, uint32_t) {});
        bool reg2 = loop.register_fd(sv[1], EPOLLIN, [](int, uint32_t) {});
        EXPECT_TRUE(reg1);
        EXPECT_TRUE(reg2);

        EXPECT_TRUE(loop.modify_fd(sv[0], EPOLLIN | EPOLLOUT));

        EXPECT_TRUE(loop.unregister_fd(sv[0]));
        EXPECT_TRUE(loop.unregister_fd(sv[1]));
    }

    for (int fd : fds_to_close) {
        ::close(fd);
    }
}

// 31. EventLoop 128 Concurrent Connections Stress test and FD Leak verification
TEST(EventLoopTest, Concurrent128Connections) {
    EventLoop loop;

    size_t start_fd_count = get_open_fd_count();

    // Create listening socket on ephemeral port
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);

    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 200), 0);

    socklen_t addr_len = sizeof(addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len), 0);
    uint16_t port = ::ntohs(addr.sin_port);

    constexpr int NUM_CONNS = 128;
    std::vector<int> accepted_fds;
    accepted_fds.reserve(NUM_CONNS);

    int accepted_count = 0;

    loop.register_fd(listen_fd, EPOLLIN, [&](int fd, uint32_t events) {
        (void)events;
        while (true) {
            struct sockaddr_in client_addr{};
            socklen_t len = sizeof(client_addr);
            int client_fd = ::accept(fd, reinterpret_cast<struct sockaddr*>(&client_addr), &len);
            if (client_fd >= 0) {
                // Set non-blocking
                int flags = ::fcntl(client_fd, F_GETFL, 0);
                ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

                accepted_fds.push_back(client_fd);
                accepted_count++;

                loop.register_fd(client_fd, EPOLLIN, [&](int cfd, uint32_t cevents) {
                    (void)cevents;
                    uint8_t buf[64];
                    ssize_t n = ::recv(cfd, buf, sizeof(buf), 0);
                    if (n > 0) {
                        ::send(cfd, buf, static_cast<size_t>(n), MSG_NOSIGNAL);
                    }
                });

                if (accepted_count == NUM_CONNS) {
                    loop.unregister_fd(listen_fd);
                }
            } else {
                break;
            }
        }
    });

    int listen_flags = ::fcntl(listen_fd, F_GETFL, 0);
    ::fcntl(listen_fd, F_SETFL, listen_flags | O_NONBLOCK);

    std::vector<TcpConnection> clients;
    clients.reserve(NUM_CONNS);

    int client_echoes_received = 0;

    for (int i = 0; i < NUM_CONNS; ++i) {
        TcpConnection client;
        ConnectStatus c_status = client.connect_async("127.0.0.1", port);
        ASSERT_NE(c_status, ConnectStatus::Error);

        int c_fd = client.fd();
        loop.register_fd(c_fd, EPOLLIN | EPOLLOUT, [c_fd, &loop, &client_echoes_received](int fd, uint32_t events) {
            if (events & EPOLLOUT) {
                std::string msg = "ping";
                ::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL);
                loop.modify_fd(fd, EPOLLIN);
            }
            if (events & EPOLLIN) {
                char buf[64];
                ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                if (n > 0) {
                    client_echoes_received++;
                    if (client_echoes_received == NUM_CONNS) {
                        loop.shutdown();
                    }
                }
            }
        });

        clients.push_back(std::move(client));
    }

    loop.run();

    // Close all descriptors
    loop.unregister_fd(listen_fd);
    ::close(listen_fd);
    for (int fd : accepted_fds) {
        loop.unregister_fd(fd);
        ::close(fd);
    }
    for (auto& client : clients) {
        loop.unregister_fd(client.fd());
        client.close();
    }

    EXPECT_EQ(client_echoes_received, NUM_CONNS);
    EXPECT_EQ(accepted_count, NUM_CONNS);

    size_t end_fd_count = get_open_fd_count();
    EXPECT_EQ(end_fd_count, start_fd_count);
}

// 32. TrackerClient URL encoding tests
TEST(TrackerClientTest, UrlEncoding) {
    // Standard unreserved chars
    std::string unreserved = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~";
    std::vector<uint8_t> data1(unreserved.begin(), unreserved.end());
    EXPECT_EQ(TrackerClient::url_encode(data1), unreserved);

    // Reserved/special chars
    std::vector<uint8_t> data2 = {0x00, 0x1F, 0x20, 0x7F, 0x80, 0xFF, 'A', '%'};
    // Expected: %00%1F%20%7F%80%FFA%25
    EXPECT_EQ(TrackerClient::url_encode(data2), "%00%1F%20%7F%80%FFA%25");
}

// 33. TrackerClient Compact response parsing
TEST(TrackerClientTest, CompactResponseParsing) {
    // Construct a mock tracker response dictionary with:
    // interval = 1800, complete = 5, incomplete = 2
    // peers = 12 bytes string representing two peers:
    //   Peer 1: 127.0.0.1:6881 (7F 00 00 01 1A E1)
    //   Peer 2: 192.168.1.10:8080 (C0 A8 01 0A 1F 90)
    std::vector<uint8_t> response_bytes = {
        'd', '8', ':', 'c', 'o', 'm', 'p', 'l', 'e', 't', 'e', 'i', '5', 'e',
        '1', '0', ':', 'i', 'n', 'c', 'o', 'm', 'p', 'l', 'e', 't', 'e', 'i', '2', 'e',
        '8', ':', 'i', 'n', 't', 'e', 'r', 'v', 'a', 'l', 'i', '1', '8', '0', '0', 'e',
        '5', ':', 'p', 'e', 'e', 'r', 's', '1', '2', ':',
        0x7F, 0x00, 0x00, 0x01, 0x1A, 0xE1, 0xC0, 0xA8, 0x01, 0x0A, 0x1F, 0x90,
        'e'
    };

    TrackerResponse res = TrackerClient::parse_response(response_bytes);

    EXPECT_TRUE(res.failure_reason.empty());
    EXPECT_EQ(res.interval, 1800);
    EXPECT_EQ(res.complete, 5);
    EXPECT_EQ(res.incomplete, 2);
    ASSERT_EQ(res.peers.size(), 2);

    EXPECT_EQ(res.peers[0].ip, "127.0.0.1");
    EXPECT_EQ(res.peers[0].port, 6881);
    EXPECT_TRUE(res.peers[0].id.empty());

    EXPECT_EQ(res.peers[1].ip, "192.168.1.10");
    EXPECT_EQ(res.peers[1].port, 8080);
    EXPECT_TRUE(res.peers[1].id.empty());
}

// 34. TrackerClient List response parsing
TEST(TrackerClientTest, ListResponseParsing) {
    // Construct a mock tracker response with:
    // interval = 900
    // peers = list of dictionary format:
    //   [{ip: "10.0.0.5", port: 5000, peer id: "12345678901234567890"}]
    std::string response = "d8:intervali900e5:peersld2:ip8:10.0.0.57:peer id20:123456789012345678904:porti5000eeee";
    std::vector<uint8_t> response_bytes(response.begin(), response.end());
    TrackerResponse res = TrackerClient::parse_response(response_bytes);

    EXPECT_TRUE(res.failure_reason.empty());
    EXPECT_EQ(res.interval, 900);
    ASSERT_EQ(res.peers.size(), 1);
    EXPECT_EQ(res.peers[0].ip, "10.0.0.5");
    EXPECT_EQ(res.peers[0].port, 5000);
    EXPECT_EQ(res.peers[0].id, "12345678901234567890");
}

// 35. TrackerClient Failure response parsing
TEST(TrackerClientTest, FailureResponseParsing) {
    std::string response = "d14:failure reason27:info_hash is not registerede";
    std::vector<uint8_t> response_bytes(response.begin(), response.end());
    TrackerResponse res = TrackerClient::parse_response(response_bytes);

    EXPECT_EQ(res.failure_reason, "info_hash is not registered");
    EXPECT_TRUE(res.peers.empty());
}

// 36. Peer ID Generation tests
TEST(PeerIdTest, StructureAndEntropy) {
    auto id1 = generate_peer_id();
    auto id2 = generate_peer_id();

    // Check prefix
    std::string prefix(reinterpret_cast<const char*>(id1.data()), 8);
    EXPECT_EQ(prefix, "-DT0001-");

    // Check that we get different results across runs (entropy check)
    EXPECT_NE(id1, id2);

    // Verify all remaining characters are alphanumeric
    for (size_t i = 8; i < 20; ++i) {
        char c = static_cast<char>(id1[i]);
        EXPECT_TRUE(std::isalnum(c)) << "Character at index " << i << " is not alphanumeric: " << static_cast<int>(c);
    }
}

// 37. Handshake serialization and deserialization
TEST(PeerMessageTest, HandshakeRoundtrip) {
    HandshakeMsg handshake;
    std::fill(handshake.reserved.begin(), handshake.reserved.end(), 0x00);
    std::fill(handshake.info_hash.begin(), handshake.info_hash.end(), 0xAA);
    std::fill(handshake.peer_id.begin(), handshake.peer_id.end(), 0xBB);

    auto bytes = handshake.serialize();
    ASSERT_EQ(bytes.size(), 68);
    EXPECT_EQ(bytes[0], 19);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes.data() + 1), 19), "BitTorrent protocol");

    auto deserialized = HandshakeMsg::deserialize(bytes);
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(deserialized->reserved, handshake.reserved);
    EXPECT_EQ(deserialized->info_hash, handshake.info_hash);
    EXPECT_EQ(deserialized->peer_id, handshake.peer_id);

    // Rejection tests
    std::vector<uint8_t> short_bytes(bytes.begin(), bytes.begin() + 67);
    EXPECT_FALSE(HandshakeMsg::deserialize(short_bytes).has_value());

    std::vector<uint8_t> bad_len_bytes = bytes;
    bad_len_bytes[0] = 18;
    EXPECT_FALSE(HandshakeMsg::deserialize(bad_len_bytes).has_value());

    std::vector<uint8_t> bad_protocol_bytes = bytes;
    bad_protocol_bytes[1] = 'b';
    EXPECT_FALSE(HandshakeMsg::deserialize(bad_protocol_bytes).has_value());
}

// 38. PeerMessage serialization and deserialization roundtrips
TEST(PeerMessageTest, MessagesRoundtrip) {
    std::vector<PeerMessage> test_messages = {
        PeerMessage::keep_alive(),
        PeerMessage::choke(),
        PeerMessage::unchoke(),
        PeerMessage::interested(),
        PeerMessage::not_interested(),
        PeerMessage::have(1234),
        PeerMessage::bitfield({0x00, 0xFF, 0x55, 0xAA}),
        PeerMessage::request(1, 2, 3),
        PeerMessage::piece(5, 10, {0xDE, 0xAD, 0xBE, 0xEF}),
        PeerMessage::cancel(10, 20, 30),
        PeerMessage::port(8888)
    };

    for (const auto& msg : test_messages) {
        auto bytes = msg.serialize();
        size_t consumed = 0;
        auto deserialized = PeerMessage::deserialize(bytes, consumed);
        ASSERT_TRUE(deserialized.has_value());
        EXPECT_EQ(*deserialized, msg);
        EXPECT_EQ(consumed, bytes.size());
    }
}

// 39. PeerMessage parsing TCP stream scenarios (partial delivery, concatenation)
TEST(PeerMessageTest, StreamParsingScenarios) {
    // 1. Partial length prefix
    std::vector<uint8_t> data1 = {0x00, 0x00};
    size_t consumed = 0;
    auto res1 = PeerMessage::deserialize(data1, consumed);
    EXPECT_FALSE(res1.has_value());
    EXPECT_EQ(consumed, 0);

    // 2. Full length but partial payload
    std::vector<uint8_t> data2 = {0x00, 0x00, 0x00, 0x05, 0x04}; // Have message, wants 5 bytes but only got 1
    auto res2 = PeerMessage::deserialize(data2, consumed);
    EXPECT_FALSE(res2.has_value());
    EXPECT_EQ(consumed, 0);

    // 3. Concatenated messages
    auto msg1 = PeerMessage::choke().serialize();
    auto msg2 = PeerMessage::unchoke().serialize();
    std::vector<uint8_t> stream;
    stream.insert(stream.end(), msg1.begin(), msg1.end());
    stream.insert(stream.end(), msg2.begin(), msg2.end());

    size_t c1 = 0;
    auto r1 = PeerMessage::deserialize(stream, c1);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, PeerMessage::choke());
    EXPECT_EQ(c1, 5);

    std::span<const uint8_t> remaining(stream.data() + c1, stream.size() - c1);
    size_t c2 = 0;
    auto r2 = PeerMessage::deserialize(remaining, c2);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, PeerMessage::unchoke());
    EXPECT_EQ(c2, 5);

    // 4. Unrecognized message ID (graceful skip)
    std::vector<uint8_t> unknown_msg = {0x00, 0x00, 0x00, 0x03, 0x63, 0x01, 0x02}; // ID 99 (0x63), length 3
    size_t c3 = 0;
    auto r3 = PeerMessage::deserialize(unknown_msg, c3);
    EXPECT_FALSE(r3.has_value());
    EXPECT_EQ(c3, 7); // skip 4 + 3 = 7 bytes
}

// 40. PeerConnection state machine, handshaking, and messages using socketpair
TEST(PeerConnectionTest, FullStateMachineAndFlow) {
    int sv[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    // Make both sockets non-blocking
    fcntl(sv[0], F_SETFL, O_NONBLOCK);
    fcntl(sv[1], F_SETFL, O_NONBLOCK);

    EventLoop loop;
    std::array<uint8_t, 20> info_hash;
    std::fill(info_hash.begin(), info_hash.end(), 0x11);
    std::array<uint8_t, 20> our_peer_id;
    std::fill(our_peer_id.begin(), our_peer_id.end(), 0x22);

    // Create PeerConnection with sv[0] (acting as client)
    auto peer_conn = PeerConnection::create_with_socket(loop, sv[0], info_hash, our_peer_id);
    
    EXPECT_EQ(peer_conn->get_state(), ConnectionState::Handshaking);

    // Verify client immediately sends its handshake on sv[0], which we can read from sv[1]
    uint8_t read_buf[200];
    ssize_t read_bytes = ::read(sv[1], read_buf, sizeof(read_buf));
    ASSERT_EQ(read_bytes, 68);
    auto client_handshake = HandshakeMsg::deserialize(std::span<const uint8_t>(read_buf, 68));
    ASSERT_TRUE(client_handshake.has_value());
    EXPECT_EQ(client_handshake->info_hash, info_hash);
    EXPECT_EQ(client_handshake->peer_id, our_peer_id);

    // Set up callbacks
    bool handshake_called = false;
    HandshakeMsg received_handshake;
    peer_conn->on_handshake([&](const HandshakeMsg& h) {
        handshake_called = true;
        received_handshake = h;
    });

    std::vector<PeerMessage> received_msgs;
    peer_conn->on_message([&](const PeerMessage& m) {
        received_msgs.push_back(m);
    });

    bool disconnect_called = false;
    peer_conn->on_disconnect([&]() {
        disconnect_called = true;
    });

    // Send mock handshake from peer (sv[1] to sv[0])
    HandshakeMsg peer_handshake;
    std::fill(peer_handshake.reserved.begin(), peer_handshake.reserved.end(), 0x00);
    std::fill(peer_handshake.info_hash.begin(), peer_handshake.info_hash.end(), 0x11);
    std::fill(peer_handshake.peer_id.begin(), peer_handshake.peer_id.end(), 0x33);
    auto hs_bytes = peer_handshake.serialize();
    ssize_t written = ::write(sv[1], hs_bytes.data(), hs_bytes.size());
    ASSERT_EQ(written, 68);

    // Step event loop once to process readable handshake
    loop.register_timer(std::chrono::milliseconds(1), [&]() {
        loop.shutdown();
    });
    loop.run();

    EXPECT_EQ(peer_conn->get_state(), ConnectionState::Active);
    EXPECT_TRUE(handshake_called);
    EXPECT_EQ(received_handshake.peer_id, peer_handshake.peer_id);

    // Send mock messages from peer (sv[1] to sv[0])
    // Unchoke + Interested
    auto unchoke_msg = PeerMessage::unchoke();
    auto interested_msg = PeerMessage::interested();
    auto u_bytes = unchoke_msg.serialize();
    auto i_bytes = interested_msg.serialize();
    
    // Write partially (TCP fragmentation simulation)
    ssize_t w1 = ::write(sv[1], u_bytes.data(), u_bytes.size() - 1);
    ASSERT_EQ(w1, static_cast<ssize_t>(u_bytes.size() - 1));
    
    // Run loop - should not trigger message callback yet (partial unchoke)
    loop.register_timer(std::chrono::milliseconds(1), [&]() {
        loop.shutdown();
    });
    loop.run();
    EXPECT_TRUE(received_msgs.empty());

    // Write the remaining byte of unchoke + full interested message
    ssize_t w2 = ::write(sv[1], u_bytes.data() + u_bytes.size() - 1, 1);
    ASSERT_EQ(w2, 1);
    ssize_t w3 = ::write(sv[1], i_bytes.data(), i_bytes.size());
    ASSERT_EQ(w3, static_cast<ssize_t>(i_bytes.size()));

    // Run loop - should process both messages
    loop.register_timer(std::chrono::milliseconds(1), [&]() {
        loop.shutdown();
    });
    loop.run();

    ASSERT_EQ(received_msgs.size(), 2);
    EXPECT_EQ(received_msgs[0], unchoke_msg);
    EXPECT_EQ(received_msgs[1], interested_msg);

    EXPECT_FALSE(peer_conn->is_peer_choking());
    EXPECT_TRUE(peer_conn->is_peer_interested());

    // Send message from client to peer (sv[0] to sv[1]) and verify output draining
    auto client_msg = PeerMessage::have(42);
    peer_conn->send_message(client_msg);

    // Run loop to drain write queue
    loop.register_timer(std::chrono::milliseconds(1), [&]() {
        loop.shutdown();
    });
    loop.run();

    uint8_t out_msg_buf[100];
    ssize_t out_bytes = ::read(sv[1], out_msg_buf, sizeof(out_msg_buf));
    ASSERT_EQ(out_bytes, 9);
    size_t consumed = 0;
    auto read_msg = PeerMessage::deserialize(std::span<const uint8_t>(out_msg_buf, 9), consumed);
    ASSERT_TRUE(read_msg.has_value());
    EXPECT_EQ(*read_msg, client_msg);
    EXPECT_EQ(consumed, 9);

    // Disconnect test
    ::close(sv[1]); // peer closes socket
    
    // Run loop - detects peer closure (EPOLLRDHUP/EPOLLIN with 0 read)
    loop.register_timer(std::chrono::milliseconds(1), [&]() {
        loop.shutdown();
    });
    loop.run();

    EXPECT_EQ(peer_conn->get_state(), ConnectionState::Disconnected);
    EXPECT_TRUE(disconnect_called);
}

// 41. Bitfield tests
TEST(BitfieldTest, BasicOperationsAndEdgeCases) {
    // 1. Construct empty
    Bitfield bf1(10);
    EXPECT_EQ(bf1.num_bits(), 10);
    EXPECT_EQ(bf1.bytes().size(), 2);
    EXPECT_EQ(bf1.count(), 0);

    for (size_t i = 0; i < 10; ++i) {
        EXPECT_FALSE(bf1.has(i));
    }

    // 2. Set bits and verify MSB-first indexing
    bf1.set(0, true);
    bf1.set(7, true);
    bf1.set(8, true);

    EXPECT_TRUE(bf1.has(0));
    EXPECT_TRUE(bf1.has(7));
    EXPECT_TRUE(bf1.has(8));
    EXPECT_FALSE(bf1.has(1));
    EXPECT_FALSE(bf1.has(9));
    EXPECT_EQ(bf1.count(), 3);

    // Verify raw bytes
    // Piece 0 is 0x80, Piece 7 is 0x01 -> first byte should be 0x81 (129)
    // Piece 8 is 0x80 -> second byte should be 0x80 (128)
    ASSERT_EQ(bf1.bytes().size(), 2);
    EXPECT_EQ(bf1.bytes()[0], 0x81);
    EXPECT_EQ(bf1.bytes()[1], 0x80);

    // 3. Construct from span and test padding bit clearing
    // 10 bits, last 6 bits of 2nd byte are padding and must be cleared
    std::vector<uint8_t> raw_bytes = {0xAA, 0xFF}; // 0xAA = 10101010, 0xFF = 11111111
    Bitfield bf2(raw_bytes, 10);
    EXPECT_EQ(bf2.num_bits(), 10);
    ASSERT_EQ(bf2.bytes().size(), 2);
    EXPECT_EQ(bf2.bytes()[0], 0xAA);
    // last byte has 2 significant bits (indices 8, 9), rest 6 are padding (should be cleared to 0)
    // 11000000 = 0xC0
    EXPECT_EQ(bf2.bytes()[1], 0xC0);

    // 4. Test Intersection operator &
    // bf1 has bits: 0, 7, 8
    // bf2 (0xAA, 0xC0) has bits: 0, 2, 4, 6, 8, 9
    // Intersection should have bits: 0, 8
    Bitfield intersection = bf1 & bf2;
    EXPECT_EQ(intersection.num_bits(), 10);
    EXPECT_EQ(intersection.count(), 2);
    EXPECT_TRUE(intersection.has(0));
    EXPECT_TRUE(intersection.has(8));
    EXPECT_FALSE(intersection.has(7));
    EXPECT_FALSE(intersection.has(9));

    // Out of bounds safety
    EXPECT_FALSE(bf1.has(99));
    bf1.set(99, true); // should not crash, just ignore
    EXPECT_FALSE(bf1.has(99));
}

// 42. PeerManager tests
TEST(PeerManagerTest, ConnectionManagementAndBlacklisting) {
    EventLoop loop;
    std::array<uint8_t, 20> info_hash;
    std::array<uint8_t, 20> our_peer_id;
    info_hash.fill(0x11);
    our_peer_id.fill(0x22);

    // Create a PeerManager with max_connections = 2
    PeerManager manager(loop, info_hash, our_peer_id, 2);

    EXPECT_EQ(manager.active_connection_count(), 0);

    // 1. Add peers from tracker
    std::vector<PeerInfo> peers = {
        {"127.0.0.1", 65001, ""},
        {"127.0.0.1", 65002, ""},
        {"127.0.0.1", 65003, ""} // This should exceed the limit of 2
    };

    manager.add_peers(peers);

    // Active connection count should be exactly 2 (respecting max_connections)
    EXPECT_EQ(manager.active_connection_count(), 2);
    
    auto active_list = manager.get_active_peers();
    ASSERT_EQ(active_list.size(), 2);
    EXPECT_EQ(active_list[0].first, "127.0.0.1");
    EXPECT_EQ(active_list[1].first, "127.0.0.1");

    // 2. Blacklisting peer {"127.0.0.1", 65001}
    manager.blacklist_peer("127.0.0.1", 65001);
    EXPECT_TRUE(manager.is_blacklisted("127.0.0.1", 65001));

    // After blacklisting, it should have been disconnected and removed from active list
    EXPECT_EQ(manager.active_connection_count(), 1);

    // Try to add it again, it should be skipped because it is blacklisted
    manager.add_peers({{"127.0.0.1", 65001, ""}});
    EXPECT_EQ(manager.active_connection_count(), 1);

    // Try to add the third peer now that there is room (active connections = 1, limit = 2)
    manager.add_peers({{"127.0.0.1", 65003, ""}});
    EXPECT_EQ(manager.active_connection_count(), 2);
}







