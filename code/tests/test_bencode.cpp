#include <gtest/gtest.h>
#include "bencode.hpp"
#include "torrent.hpp"
#include <cryptopp/sha.h>

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

// 20. Torrent Metadata Single-File Parsing
TEST(TorrentMetadataTest, SingleFile) {
    // Announce: http://tracker/announce
    // Info:
    //   length: 100
    //   name: test.txt
    //   piece length: 50
    //   pieces: 40 bytes (2 pieces) of SHA-1 hashes (zeros here for placeholder)
    std::string bencode = 
        "d8:announce23:http://tracker/announce"
        "4:infod6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces40:0000000000000000000000000000000000000000ee";

    auto meta = TorrentMetadata::from_bencode(to_span(bencode));
    EXPECT_EQ(meta.announce_url, "http://tracker/announce");
    EXPECT_EQ(meta.name, "test.txt");
    EXPECT_EQ(meta.piece_length, 50);
    EXPECT_EQ(meta.total_length, 100);
    EXPECT_EQ(meta.num_pieces, 2);
    ASSERT_EQ(meta.piece_hashes.size(), 2);
    EXPECT_EQ(meta.files.size(), 1);
    EXPECT_EQ(meta.files[0].path, "test.txt");
    EXPECT_EQ(meta.files[0].length, 100);
    EXPECT_EQ(meta.files[0].offset, 0);
}

// 21. Torrent Metadata Multi-File Parsing
TEST(TorrentMetadataTest, MultiFile) {
    // Announce: http://tracker/announce
    // Info:
    //   name: mydir
    //   piece length: 256
    //   pieces: 20 bytes (1 piece) of SHA-1 hash (all zeros)
    //   files:
    //     - length: 150
    //       path: ["subdir", "file1.txt"]
    //     - length: 100
    //       path: ["file2.txt"]
    std::string bencode = 
        "d8:announce23:http://tracker/announce"
        "4:infod5:filesld6:lengthi150e4:pathl6:subdir9:file1.txteed6:lengthi100e4:pathl9:file2.txteee"
        "4:name5:mydir12:piece lengthi256e6:pieces20:00000000000000000000ee";

    auto meta = TorrentMetadata::from_bencode(to_span(bencode));
    EXPECT_EQ(meta.announce_url, "http://tracker/announce");
    EXPECT_EQ(meta.name, "mydir");
    EXPECT_EQ(meta.piece_length, 256);
    EXPECT_EQ(meta.total_length, 250);
    EXPECT_EQ(meta.num_pieces, 1);
    ASSERT_EQ(meta.piece_hashes.size(), 1);
    
    ASSERT_EQ(meta.files.size(), 2);
    EXPECT_EQ(meta.files[0].path, "subdir/file1.txt");
    EXPECT_EQ(meta.files[0].length, 150);
    EXPECT_EQ(meta.files[0].offset, 0);
    
    EXPECT_EQ(meta.files[1].path, "file2.txt");
    EXPECT_EQ(meta.files[1].length, 100);
    EXPECT_EQ(meta.files[1].offset, 150);
}

// 22. Torrent Metadata Invalid Parsing Checks
TEST(TorrentMetadataTest, Invalid) {
    // Missing announce
    std::string no_announce = 
        "d4:infod6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces20:00000000000000000000ee";
    EXPECT_THROW(TorrentMetadata::from_bencode(to_span(no_announce)), std::runtime_error);

    // Mismatched pieces count vs length
    std::string mismatched_pieces = 
        "d8:announce23:http://tracker/announce"
        "4:infod6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces20:00000000000000000000ee"; // length is 100, piece_length is 50, so needs 2 pieces (40 bytes), but pieces has only 20 bytes (1 piece)
    EXPECT_THROW(TorrentMetadata::from_bencode(to_span(mismatched_pieces)), std::runtime_error);
}

// 23. Torrent Metadata Info Hash Calculation
TEST(TorrentMetadataTest, InfoHashCalculation) {
    std::string bencode = 
        "d8:announce23:http://tracker/announce"
        "4:infod6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces40:0000000000000000000000000000000000000000ee";

    auto meta = TorrentMetadata::from_bencode(to_span(bencode));

    std::string expected_info_bytes = 
        "d6:lengthi100e4:name8:test.txt12:piece lengthi50e"
        "6:pieces40:0000000000000000000000000000000000000000e";

    std::array<uint8_t, 20> expected_hash{};
    CryptoPP::SHA1 sha1;
    sha1.CalculateDigest(expected_hash.data(), 
                         reinterpret_cast<const uint8_t*>(expected_info_bytes.data()), 
                         expected_info_bytes.size());

    EXPECT_EQ(meta.info_hash, expected_hash);
}

