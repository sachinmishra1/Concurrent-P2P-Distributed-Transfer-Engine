#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <span>

class Bitfield {
public:
    Bitfield() = default;

    // Construct an empty bitfield for a given number of bits (pieces)
    explicit Bitfield(size_t num_bits);

    // Construct a bitfield from raw bytes (received from wire) and total bits count
    Bitfield(std::span<const uint8_t> bytes, size_t num_bits);

    // Get value of a bit at index
    bool has(size_t index) const;

    // Set value of a bit at index
    void set(size_t index, bool val = true);

    // Count number of set bits (pieces we have)
    size_t count() const;

    // Intersection operator (tells us which pieces we want that the peer has)
    Bitfield operator&(const Bitfield& other) const;

    bool operator==(const Bitfield& other) const {
        return num_bits_ == other.num_bits_ && bytes_ == other.bytes_;
    }

    // Get raw bytes for serialization/sending on wire
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    
    // Get total number of bits
    size_t num_bits() const { return num_bits_; }

private:
    size_t num_bits_ = 0;
    std::vector<uint8_t> bytes_;
};
