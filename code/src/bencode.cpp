#include "bencode.hpp"
#include <cctype>
#include <limits>

BencodeValue BencodeParser::parse(std::span<const uint8_t> input) {
    std::span<const uint8_t> remaining = input;
    BencodeValue val = parse_value(remaining);
    if (!remaining.empty()) {
        throw std::runtime_error("BencodeParser: extra data at the end of input");
    }
    return val;
}

BencodeValue BencodeParser::parse_value(std::span<const uint8_t>& remaining) {
    if (remaining.empty()) {
        throw std::runtime_error("BencodeParser: unexpected end of input");
    }

    uint8_t first_char = remaining[0];
    if (first_char == 'i') {
        return parse_int(remaining);
    } else if (first_char == 'l') {
        return parse_list(remaining);
    } else if (first_char == 'd') {
        return parse_dict(remaining);
    } else if (std::isdigit(first_char)) {
        return parse_string(remaining);
    } else {
        throw std::runtime_error("BencodeParser: invalid first character for Bencode type");
    }
}

BencodeInt BencodeParser::parse_int(std::span<const uint8_t>& remaining) {
    if (remaining.empty() || remaining[0] != 'i') {
        throw std::runtime_error("BencodeParser: expected 'i' for integer");
    }
    remaining = remaining.subspan(1); // consume 'i'

    if (remaining.empty()) {
        throw std::runtime_error("BencodeParser: unexpected end of integer");
    }

    bool negative = false;
    if (remaining[0] == '-') {
        negative = true;
        remaining = remaining.subspan(1); // consume '-'
    }

    if (remaining.empty() || !std::isdigit(remaining[0])) {
        throw std::runtime_error("BencodeParser: expected digits in integer");
    }

    // Check for leading zero rules
    if (remaining[0] == '0') {
        // If there's a leading zero, it must be followed immediately by 'e'
        // and it cannot be negative (no -0).
        if (remaining.size() < 2 || remaining[1] != 'e') {
            throw std::runtime_error("BencodeParser: leading zeros are not allowed in integer");
        }
        if (negative) {
            throw std::runtime_error("BencodeParser: -0 is not allowed");
        }
    }

    BencodeInt value = 0;
    size_t consumed = 0;
    while (consumed < remaining.size() && std::isdigit(remaining[consumed])) {
        uint8_t digit = remaining[consumed];
        // Overflow check
        if (value > (std::numeric_limits<int64_t>::max() - (digit - '0')) / 10) {
             throw std::runtime_error("BencodeParser: integer overflow");
        }
        value = value * 10 + (digit - '0');
        consumed++;
    }

    remaining = remaining.subspan(consumed);

    if (remaining.empty() || remaining[0] != 'e') {
        throw std::runtime_error("BencodeParser: expected 'e' at end of integer");
    }
    remaining = remaining.subspan(1); // consume 'e'

    return negative ? -value : value;
}

BencodeString BencodeParser::parse_string(std::span<const uint8_t>& remaining) {
    if (remaining.empty() || !std::isdigit(remaining[0])) {
        throw std::runtime_error("BencodeParser: expected digit for string length");
    }

    // Check leading zero rule for string length
    if (remaining[0] == '0') {
        if (remaining.size() < 2 || remaining[1] != ':') {
            throw std::runtime_error("BencodeParser: leading zeros not allowed in string length");
        }
    }

    size_t length = 0;
    size_t consumed = 0;
    while (consumed < remaining.size() && std::isdigit(remaining[consumed])) {
        if (length > (std::numeric_limits<size_t>::max() - (remaining[consumed] - '0')) / 10) {
             throw std::runtime_error("BencodeParser: string length overflow");
        }
        length = length * 10 + (remaining[consumed] - '0');
        consumed++;
    }

    remaining = remaining.subspan(consumed);

    if (remaining.empty() || remaining[0] != ':') {
        throw std::runtime_error("BencodeParser: expected ':' after string length");
    }
    remaining = remaining.subspan(1); // consume ':'

    if (remaining.size() < length) {
        throw std::runtime_error("BencodeParser: unexpected end of string data");
    }

    BencodeString str(reinterpret_cast<const char*>(remaining.data()), length);
    remaining = remaining.subspan(length);

    return str;
}

BencodeList BencodeParser::parse_list(std::span<const uint8_t>& remaining) {
    if (remaining.empty() || remaining[0] != 'l') {
        throw std::runtime_error("BencodeParser: expected 'l' for list");
    }
    remaining = remaining.subspan(1); // consume 'l'

    BencodeList list;
    while (!remaining.empty() && remaining[0] != 'e') {
        list.push_back(parse_value(remaining));
    }

    if (remaining.empty() || remaining[0] != 'e') {
        throw std::runtime_error("BencodeParser: expected 'e' at end of list");
    }
    remaining = remaining.subspan(1); // consume 'e'

    return list;
}

BencodeDict BencodeParser::parse_dict(std::span<const uint8_t>& remaining) {
    if (remaining.empty() || remaining[0] != 'd') {
        throw std::runtime_error("BencodeParser: expected 'd' for dictionary");
    }
    remaining = remaining.subspan(1); // consume 'd'

    BencodeDict dict;
    BencodeString last_key;
    bool has_last_key = false;

    while (!remaining.empty() && remaining[0] != 'e') {
        BencodeString key = parse_string(remaining);
        
        // Lexicographical sorting check
        if (has_last_key) {
            if (key <= last_key) {
                if (key == last_key) {
                    throw std::runtime_error("BencodeParser: duplicate key in dictionary");
                }
                throw std::runtime_error("BencodeParser: dictionary keys not sorted lexicographically");
            }
        }
        last_key = key;
        has_last_key = true;

        BencodeValue val = parse_value(remaining);
        dict.emplace(key, std::move(val));
    }

    if (remaining.empty() || remaining[0] != 'e') {
        throw std::runtime_error("BencodeParser: expected 'e' at end of dictionary");
    }
    remaining = remaining.subspan(1); // consume 'e'

    return dict;
}

std::vector<uint8_t> BencodeParser::encode(const BencodeValue& value) {
    std::vector<uint8_t> out;
    encode_value(value, out);
    return out;
}

void BencodeParser::encode_value(const BencodeValue& value, std::vector<uint8_t>& out) {
    std::visit([&out](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, BencodeInt>) {
            out.push_back('i');
            std::string s = std::to_string(arg);
            out.insert(out.end(), s.begin(), s.end());
            out.push_back('e');
        } else if constexpr (std::is_same_v<T, BencodeString>) {
            std::string len_str = std::to_string(arg.size());
            out.insert(out.end(), len_str.begin(), len_str.end());
            out.push_back(':');
            out.insert(out.end(), arg.begin(), arg.end());
        } else if constexpr (std::is_same_v<T, BencodeList>) {
            out.push_back('l');
            for (const auto& item : arg) {
                encode_value(item, out);
            }
            out.push_back('e');
        } else if constexpr (std::is_same_v<T, BencodeDict>) {
            out.push_back('d');
            for (const auto& [k, v] : arg) {
                // Encode key (string)
                std::string len_str = std::to_string(k.size());
                out.insert(out.end(), len_str.begin(), len_str.end());
                out.push_back(':');
                out.insert(out.end(), k.begin(), k.end());
                // Encode value
                encode_value(v, out);
            }
            out.push_back('e');
        }
    }, value.data);
}
