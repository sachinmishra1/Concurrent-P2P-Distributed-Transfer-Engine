#include <gtest/gtest.h>
#include "bencode.hpp"

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
