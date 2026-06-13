#include <gtest/gtest.h>
#include "bencode.hpp"
#include "torrent.hpp"
#include "hasher.hpp"
#include "tcp_connection.hpp"
#include "event_loop.hpp"
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

