#include "bitfield.hpp"
#include <algorithm>

Bitfield::Bitfield(size_t num_bits) 
    : num_bits_(num_bits),
      bytes_((num_bits + 7) / 8, 0) {}

Bitfield::Bitfield(std::span<const uint8_t> bytes, size_t num_bits)
    : num_bits_(num_bits) {
    size_t expected_size = (num_bits + 7) / 8;
    bytes_.resize(expected_size, 0);
    size_t copy_size = std::min(bytes.size(), expected_size);
    std::copy(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(copy_size), bytes_.begin());
    
    // Clear any trailing padding bits to keep them clean
    if (num_bits_ % 8 != 0 && !bytes_.empty()) {
        uint8_t mask = static_cast<uint8_t>(0xFF << (8 - (num_bits_ % 8)));
        bytes_.back() &= mask;
    }
}

bool Bitfield::has(size_t index) const {
    if (index >= num_bits_) {
        return false;
    }
    size_t byte_idx = index / 8;
    size_t bit_offset = 7 - (index % 8);
    return (bytes_[byte_idx] & (1 << bit_offset)) != 0;
}

void Bitfield::set(size_t index, bool val) {
    if (index >= num_bits_) {
        return;
    }
    size_t byte_idx = index / 8;
    size_t bit_offset = 7 - (index % 8);
    if (val) {
        bytes_[byte_idx] |= static_cast<uint8_t>(1 << bit_offset);
    } else {
        bytes_[byte_idx] &= static_cast<uint8_t>(~(1 << bit_offset));
    }
}

size_t Bitfield::count() const {
    size_t total = 0;
    for (uint8_t byte : bytes_) {
        unsigned int b = byte;
        while (b) {
            total += (b & 1);
            b >>= 1;
        }
    }
    return total;
}

Bitfield Bitfield::operator&(const Bitfield& other) const {
    size_t min_bits = std::min(num_bits_, other.num_bits_);
    Bitfield result(min_bits);
    size_t num_bytes = result.bytes_.size();
    for (size_t i = 0; i < num_bytes; ++i) {
        result.bytes_[i] = static_cast<uint8_t>(bytes_[i] & other.bytes_[i]);
    }
    return result;
}
