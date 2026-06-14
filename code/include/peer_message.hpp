#pragma once

#include <vector>
#include <cstdint>
#include <array>
#include <variant>
#include <span>
#include <optional>
#include <string>

enum class MessageId : uint8_t {
    Choke = 0,
    Unchoke = 1,
    Interested = 2,
    NotInterested = 3,
    Have = 4,
    Bitfield = 5,
    Request = 6,
    Piece = 7,
    Cancel = 8,
    Port = 9
};

struct KeepAliveMsg {};
struct ChokeMsg {};
struct UnchokeMsg {};
struct InterestedMsg {};
struct NotInterestedMsg {};

struct HaveMsg {
    uint32_t piece_index = 0;
};

struct BitfieldMsg {
    std::vector<uint8_t> bitfield;
};

struct RequestMsg {
    uint32_t piece_index = 0;
    uint32_t begin = 0;
    uint32_t length = 0;
};

struct PieceMsg {
    uint32_t piece_index = 0;
    uint32_t begin = 0;
    std::vector<uint8_t> block;
};

struct CancelMsg {
    uint32_t piece_index = 0;
    uint32_t begin = 0;
    uint32_t length = 0;
};

struct PortMsg {
    uint16_t port = 0;
};

struct HandshakeMsg {
    std::array<uint8_t, 8> reserved{};
    std::array<uint8_t, 20> info_hash{};
    std::array<uint8_t, 20> peer_id{};

    std::vector<uint8_t> serialize() const;
    static std::optional<HandshakeMsg> deserialize(std::span<const uint8_t> data);
};

// Equality operators for GTest comparison
bool operator==(const KeepAliveMsg&, const KeepAliveMsg&);
bool operator==(const ChokeMsg&, const ChokeMsg&);
bool operator==(const UnchokeMsg&, const UnchokeMsg&);
bool operator==(const InterestedMsg&, const InterestedMsg&);
bool operator==(const NotInterestedMsg&, const NotInterestedMsg&);
bool operator==(const HaveMsg& lhs, const HaveMsg& rhs);
bool operator==(const BitfieldMsg& lhs, const BitfieldMsg& rhs);
bool operator==(const RequestMsg& lhs, const RequestMsg& rhs);
bool operator==(const PieceMsg& lhs, const PieceMsg& rhs);
bool operator==(const CancelMsg& lhs, const CancelMsg& rhs);
bool operator==(const PortMsg& lhs, const PortMsg& rhs);
bool operator==(const HandshakeMsg& lhs, const HandshakeMsg& rhs);

struct PeerMessage {
    std::variant<
        KeepAliveMsg,
        ChokeMsg,
        UnchokeMsg,
        InterestedMsg,
        NotInterestedMsg,
        HaveMsg,
        BitfieldMsg,
        RequestMsg,
        PieceMsg,
        CancelMsg,
        PortMsg
    > payload;

    // Helper builders
    static PeerMessage keep_alive();
    static PeerMessage choke();
    static PeerMessage unchoke();
    static PeerMessage interested();
    static PeerMessage not_interested();
    static PeerMessage have(uint32_t piece_index);
    static PeerMessage bitfield(std::vector<uint8_t> bits);
    static PeerMessage request(uint32_t index, uint32_t begin, uint32_t length);
    static PeerMessage piece(uint32_t index, uint32_t begin, std::vector<uint8_t> block);
    static PeerMessage cancel(uint32_t index, uint32_t begin, uint32_t length);
    static PeerMessage port(uint16_t port);

    // Serialization & Deserialization
    std::vector<uint8_t> serialize() const;
    static std::optional<PeerMessage> deserialize(std::span<const uint8_t> data, size_t& consumed);

    bool operator==(const PeerMessage& other) const {
        return payload == other.payload;
    }
};
