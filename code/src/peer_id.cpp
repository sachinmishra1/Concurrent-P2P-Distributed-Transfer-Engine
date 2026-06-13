#include "peer_id.hpp"
#include <random>
#include <algorithm>

std::array<uint8_t, 20> generate_peer_id() {
    std::array<uint8_t, 20> peer_id;
    
    // Prefix: "-DT0001-" is exactly 8 bytes
    const char prefix[] = "-DT0001-";
    std::copy(std::begin(prefix), std::begin(prefix) + 8, peer_id.begin());

    // Alphanumeric charset (62 characters)
    static const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2); // excluding the null terminator

    for (size_t i = 8; i < 20; ++i) {
        peer_id[i] = static_cast<uint8_t>(charset[dist(gen)]);
    }

    return peer_id;
}
