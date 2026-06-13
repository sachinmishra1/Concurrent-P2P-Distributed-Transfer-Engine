#pragma once

#include <array>
#include <span>
#include <string_view>
#include <cstdint>
#include <memory>

namespace CryptoPP {
    class SHA1;
}

class SHA1Hasher {
public:
    SHA1Hasher();
    ~SHA1Hasher();

    // Prevent copying to avoid multiple objects sharing/mutating the same hash state
    SHA1Hasher(const SHA1Hasher&) = delete;
    SHA1Hasher& operator=(const SHA1Hasher&) = delete;

    // Support moving
    SHA1Hasher(SHA1Hasher&&) noexcept;
    SHA1Hasher& operator=(SHA1Hasher&&) noexcept;

    // One-shot hashing helpers
    static std::array<uint8_t, 20> hash(std::span<const uint8_t> data);
    static std::array<uint8_t, 20> hash(std::string_view data);

    // Incremental hashing API
    void update(std::span<const uint8_t> data);
    void update(std::string_view data);
    std::array<uint8_t, 20> finalize();
    void reset();

private:
    std::unique_ptr<CryptoPP::SHA1> impl_;
};
