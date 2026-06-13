#pragma once

#include <map>
#include <vector>
#include <variant>
#include <string_view>
#include <stdexcept>

struct BencodeValue;

using BencodeInt = int64_t;
using BencodeString = std::string_view;
using BencodeList = std::vector<BencodeValue>;
using BencodeDict = std::map<BencodeString, BencodeValue, std::less<>>;

struct BencodeValue {
    std::variant<BencodeInt, BencodeString, BencodeList, BencodeDict> data;

    // Constructors
    BencodeValue() noexcept : data(BencodeInt(0)) {}
    BencodeValue(BencodeInt val) noexcept : data(val) {}
    BencodeValue(BencodeString val) noexcept : data(val) {}
    BencodeValue(const char* val) noexcept : data(BencodeString(val)) {}
    BencodeValue(BencodeList val) noexcept : data(std::move(val)) {}
    BencodeValue(BencodeDict val) noexcept : data(std::move(val)) {}

    // Type Checks
    bool is_int() const noexcept { return std::holds_alternative<BencodeInt>(data); }
    bool is_string() const noexcept { return std::holds_alternative<BencodeString>(data); }
    bool is_list() const noexcept { return std::holds_alternative<BencodeList>(data); }
    bool is_dict() const noexcept { return std::holds_alternative<BencodeDict>(data); }

    // Accessors
    BencodeInt as_int() const {
        if (!is_int()) throw std::runtime_error("BencodeValue: not an integer");
        return std::get<BencodeInt>(data);
    }

    BencodeString as_string() const {
        if (!is_string()) throw std::runtime_error("BencodeValue: not a string");
        return std::get<BencodeString>(data);
    }

    const BencodeList& as_list() const {
        if (!is_list()) throw std::runtime_error("BencodeValue: not a list");
        return std::get<BencodeList>(data);
    }

    BencodeList& as_list() {
        if (!is_list()) throw std::runtime_error("BencodeValue: not a list");
        return std::get<BencodeList>(data);
    }

    const BencodeDict& as_dict() const {
        if (!is_dict()) throw std::runtime_error("BencodeValue: not a dictionary");
        return std::get<BencodeDict>(data);
    }

    BencodeDict& as_dict() {
        if (!is_dict()) throw std::runtime_error("BencodeValue: not a dictionary");
        return std::get<BencodeDict>(data);
    }
};