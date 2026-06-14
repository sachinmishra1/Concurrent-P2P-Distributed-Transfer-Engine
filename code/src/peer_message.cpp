#include "peer_message.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <type_traits>

// Endianness converters utilizing standard POSIX socket helper functions
static inline uint32_t read_uint32_be(const uint8_t* ptr) {
    uint32_t val;
    std::memcpy(&val, ptr, sizeof(val));
    return ::ntohl(val);
}

static inline void write_uint32_be(uint8_t* ptr, uint32_t val) {
    uint32_t net_val = ::htonl(val);
    std::memcpy(ptr, &net_val, sizeof(net_val));
}

static inline uint16_t read_uint16_be(const uint8_t* ptr) {
    uint16_t val;
    std::memcpy(&val, ptr, sizeof(val));
    return ::ntohs(val);
}

static inline void write_uint16_be(uint8_t* ptr, uint16_t val) {
    uint16_t net_val = ::htons(val);
    std::memcpy(ptr, &net_val, sizeof(net_val));
}

// Equality comparison operator implementations
bool operator==(const KeepAliveMsg&, const KeepAliveMsg&) { return true; }
bool operator==(const ChokeMsg&, const ChokeMsg&) { return true; }
bool operator==(const UnchokeMsg&, const UnchokeMsg&) { return true; }
bool operator==(const InterestedMsg&, const InterestedMsg&) { return true; }
bool operator==(const NotInterestedMsg&, const NotInterestedMsg&) { return true; }
bool operator==(const HaveMsg& lhs, const HaveMsg& rhs) { return lhs.piece_index == rhs.piece_index; }
bool operator==(const BitfieldMsg& lhs, const BitfieldMsg& rhs) { return lhs.bitfield == rhs.bitfield; }
bool operator==(const RequestMsg& lhs, const RequestMsg& rhs) {
    return lhs.piece_index == rhs.piece_index && lhs.begin == rhs.begin && lhs.length == rhs.length;
}
bool operator==(const PieceMsg& lhs, const PieceMsg& rhs) {
    return lhs.piece_index == rhs.piece_index && lhs.begin == rhs.begin && lhs.block == rhs.block;
}
bool operator==(const CancelMsg& lhs, const CancelMsg& rhs) {
    return lhs.piece_index == rhs.piece_index && lhs.begin == rhs.begin && lhs.length == rhs.length;
}
bool operator==(const PortMsg& lhs, const PortMsg& rhs) { return lhs.port == rhs.port; }

bool operator==(const HandshakeMsg& lhs, const HandshakeMsg& rhs) {
    return lhs.reserved == rhs.reserved &&
           lhs.info_hash == rhs.info_hash &&
           lhs.peer_id == rhs.peer_id;
}

// Handshake Serialization & Deserialization
std::vector<uint8_t> HandshakeMsg::serialize() const {
    std::vector<uint8_t> out(68);
    out[0] = 19;
    std::memcpy(out.data() + 1, "BitTorrent protocol", 19);
    std::memcpy(out.data() + 20, reserved.data(), 8);
    std::memcpy(out.data() + 28, info_hash.data(), 20);
    std::memcpy(out.data() + 48, peer_id.data(), 20);
    return out;
}

std::optional<HandshakeMsg> HandshakeMsg::deserialize(std::span<const uint8_t> data) {
    if (data.size() < 68) {
        return std::nullopt;
    }
    if (data[0] != 19) {
        return std::nullopt;
    }
    if (std::memcmp(data.data() + 1, "BitTorrent protocol", 19) != 0) {
        return std::nullopt;
    }
    HandshakeMsg msg;
    std::memcpy(msg.reserved.data(), data.data() + 20, 8);
    std::memcpy(msg.info_hash.data(), data.data() + 28, 20);
    std::memcpy(msg.peer_id.data(), data.data() + 48, 20);
    return msg;
}

// PeerMessage Builders
PeerMessage PeerMessage::keep_alive() { return PeerMessage{KeepAliveMsg{}}; }
PeerMessage PeerMessage::choke() { return PeerMessage{ChokeMsg{}}; }
PeerMessage PeerMessage::unchoke() { return PeerMessage{UnchokeMsg{}}; }
PeerMessage PeerMessage::interested() { return PeerMessage{InterestedMsg{}}; }
PeerMessage PeerMessage::not_interested() { return PeerMessage{NotInterestedMsg{}}; }
PeerMessage PeerMessage::have(uint32_t piece_index) { return PeerMessage{HaveMsg{piece_index}}; }
PeerMessage PeerMessage::bitfield(std::vector<uint8_t> bits) { return PeerMessage{BitfieldMsg{std::move(bits)}}; }
PeerMessage PeerMessage::request(uint32_t index, uint32_t begin, uint32_t length) {
    return PeerMessage{RequestMsg{index, begin, length}};
}
PeerMessage PeerMessage::piece(uint32_t index, uint32_t begin, std::vector<uint8_t> block) {
    return PeerMessage{PieceMsg{index, begin, std::move(block)}};
}
PeerMessage PeerMessage::cancel(uint32_t index, uint32_t begin, uint32_t length) {
    return PeerMessage{CancelMsg{index, begin, length}};
}
PeerMessage PeerMessage::port(uint16_t port) { return PeerMessage{PortMsg{port}}; }

// PeerMessage Serialization
std::vector<uint8_t> PeerMessage::serialize() const {
    return std::visit([](const auto& msg) -> std::vector<uint8_t> {
        using T = std::decay_t<decltype(msg)>;
        if constexpr (std::is_same_v<T, KeepAliveMsg>) {
            return std::vector<uint8_t>(4, 0);
        } else if constexpr (std::is_same_v<T, ChokeMsg>) {
            std::vector<uint8_t> out(5);
            write_uint32_be(out.data(), 1);
            out[4] = static_cast<uint8_t>(MessageId::Choke);
            return out;
        } else if constexpr (std::is_same_v<T, UnchokeMsg>) {
            std::vector<uint8_t> out(5);
            write_uint32_be(out.data(), 1);
            out[4] = static_cast<uint8_t>(MessageId::Unchoke);
            return out;
        } else if constexpr (std::is_same_v<T, InterestedMsg>) {
            std::vector<uint8_t> out(5);
            write_uint32_be(out.data(), 1);
            out[4] = static_cast<uint8_t>(MessageId::Interested);
            return out;
        } else if constexpr (std::is_same_v<T, NotInterestedMsg>) {
            std::vector<uint8_t> out(5);
            write_uint32_be(out.data(), 1);
            out[4] = static_cast<uint8_t>(MessageId::NotInterested);
            return out;
        } else if constexpr (std::is_same_v<T, HaveMsg>) {
            std::vector<uint8_t> out(9);
            write_uint32_be(out.data(), 5);
            out[4] = static_cast<uint8_t>(MessageId::Have);
            write_uint32_be(out.data() + 5, msg.piece_index);
            return out;
        } else if constexpr (std::is_same_v<T, BitfieldMsg>) {
            uint32_t len = 1 + static_cast<uint32_t>(msg.bitfield.size());
            std::vector<uint8_t> out(4 + len);
            write_uint32_be(out.data(), len);
            out[4] = static_cast<uint8_t>(MessageId::Bitfield);
            std::memcpy(out.data() + 5, msg.bitfield.data(), msg.bitfield.size());
            return out;
        } else if constexpr (std::is_same_v<T, RequestMsg>) {
            std::vector<uint8_t> out(17);
            write_uint32_be(out.data(), 13);
            out[4] = static_cast<uint8_t>(MessageId::Request);
            write_uint32_be(out.data() + 5, msg.piece_index);
            write_uint32_be(out.data() + 9, msg.begin);
            write_uint32_be(out.data() + 13, msg.length);
            return out;
        } else if constexpr (std::is_same_v<T, PieceMsg>) {
            uint32_t len = 9 + static_cast<uint32_t>(msg.block.size());
            std::vector<uint8_t> out(4 + len);
            write_uint32_be(out.data(), len);
            out[4] = static_cast<uint8_t>(MessageId::Piece);
            write_uint32_be(out.data() + 5, msg.piece_index);
            write_uint32_be(out.data() + 9, msg.begin);
            std::memcpy(out.data() + 13, msg.block.data(), msg.block.size());
            return out;
        } else if constexpr (std::is_same_v<T, CancelMsg>) {
            std::vector<uint8_t> out(17);
            write_uint32_be(out.data(), 13);
            out[4] = static_cast<uint8_t>(MessageId::Cancel);
            write_uint32_be(out.data() + 5, msg.piece_index);
            write_uint32_be(out.data() + 9, msg.begin);
            write_uint32_be(out.data() + 13, msg.length);
            return out;
        } else if constexpr (std::is_same_v<T, PortMsg>) {
            std::vector<uint8_t> out(7);
            write_uint32_be(out.data(), 3);
            out[4] = static_cast<uint8_t>(MessageId::Port);
            write_uint16_be(out.data() + 5, msg.port);
            return out;
        }
        return std::vector<uint8_t>{};
    }, payload);
}

// PeerMessage Deserialization
std::optional<PeerMessage> PeerMessage::deserialize(std::span<const uint8_t> data, size_t& consumed) {
    consumed = 0;
    if (data.size() < 4) {
        return std::nullopt;
    }

    uint32_t length = read_uint32_be(data.data());

    if (length == 0) {
        consumed = 4;
        return PeerMessage::keep_alive();
    }

    // Make sure we have the full message payload
    if (data.size() < 4 + length) {
        return std::nullopt;
    }

    consumed = 4 + length;

    uint8_t id_byte = data[4];
    MessageId id = static_cast<MessageId>(id_byte);

    switch (id) {
        case MessageId::Choke: {
            if (length != 1) return std::nullopt;
            return PeerMessage::choke();
        }
        case MessageId::Unchoke: {
            if (length != 1) return std::nullopt;
            return PeerMessage::unchoke();
        }
        case MessageId::Interested: {
            if (length != 1) return std::nullopt;
            return PeerMessage::interested();
        }
        case MessageId::NotInterested: {
            if (length != 1) return std::nullopt;
            return PeerMessage::not_interested();
        }
        case MessageId::Have: {
            if (length != 5) return std::nullopt;
            uint32_t idx = read_uint32_be(data.data() + 5);
            return PeerMessage::have(idx);
        }
        case MessageId::Bitfield: {
            uint32_t field_len = length - 1;
            std::vector<uint8_t> field(field_len);
            std::memcpy(field.data(), data.data() + 5, field_len);
            return PeerMessage::bitfield(std::move(field));
        }
        case MessageId::Request: {
            if (length != 13) return std::nullopt;
            uint32_t index = read_uint32_be(data.data() + 5);
            uint32_t begin = read_uint32_be(data.data() + 9);
            uint32_t len = read_uint32_be(data.data() + 13);
            return PeerMessage::request(index, begin, len);
        }
        case MessageId::Piece: {
            if (length < 9) return std::nullopt;
            uint32_t index = read_uint32_be(data.data() + 5);
            uint32_t begin = read_uint32_be(data.data() + 9);
            uint32_t block_len = length - 9;
            std::vector<uint8_t> block(block_len);
            std::memcpy(block.data(), data.data() + 13, block_len);
            return PeerMessage::piece(index, begin, std::move(block));
        }
        case MessageId::Cancel: {
            if (length != 13) return std::nullopt;
            uint32_t index = read_uint32_be(data.data() + 5);
            uint32_t begin = read_uint32_be(data.data() + 9);
            uint32_t len = read_uint32_be(data.data() + 13);
            return PeerMessage::cancel(index, begin, len);
        }
        case MessageId::Port: {
            if (length != 3) return std::nullopt;
            uint16_t port_val = read_uint16_be(data.data() + 5);
            return PeerMessage::port(port_val);
        }
        default: {
            // Unrecognized message ID - return std::nullopt but keep consumed updated to skip the message bytes
            return std::nullopt;
        }
    }
}
