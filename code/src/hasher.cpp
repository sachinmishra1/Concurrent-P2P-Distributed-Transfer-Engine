#include "hasher.hpp"
#include <cryptopp/sha.h>

SHA1Hasher::SHA1Hasher() : impl_(std::make_unique<CryptoPP::SHA1>()) {}

SHA1Hasher::~SHA1Hasher() = default;

SHA1Hasher::SHA1Hasher(SHA1Hasher&&) noexcept = default;
SHA1Hasher& SHA1Hasher::operator=(SHA1Hasher&&) noexcept = default;

std::array<uint8_t, 20> SHA1Hasher::hash(std::span<const uint8_t> data) {
    std::array<uint8_t, 20> digest{};
    CryptoPP::SHA1 sha1;
    sha1.CalculateDigest(digest.data(), data.data(), data.size());
    return digest;
}

std::array<uint8_t, 20> SHA1Hasher::hash(std::string_view data) {
    return hash(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
}

void SHA1Hasher::update(std::span<const uint8_t> data) {
    impl_->Update(data.data(), data.size());
}

void SHA1Hasher::update(std::string_view data) {
    update(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
}

std::array<uint8_t, 20> SHA1Hasher::finalize() {
    std::array<uint8_t, 20> digest{};
    impl_->Final(digest.data());
    return digest;
}

void SHA1Hasher::reset() {
    impl_->Restart();
}
