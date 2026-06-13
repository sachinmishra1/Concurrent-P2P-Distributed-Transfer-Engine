#include "logger.hpp"
#include "bencode.hpp"
#include <iostream>
#include <cassert>

void welcome_msg() {
  std::cout << "------------------------------------------------\n";
  std::cout << "| Welcome to P2P distributed transfer engine !! |\n";
  std::cout << "------------------------------------------------\n\n";
}

void print_bencode_value(const BencodeValue& val) {
  if (val.is_int()) {
    std::cout << val.as_int();
  } else if (val.is_string()) {
    std::cout << "\"" << val.as_string() << "\"";
  } else if (val.is_list()) {
    std::cout << "[";
    const auto& list = val.as_list();
    for (size_t i = 0; i < list.size(); ++i) {
      print_bencode_value(list[i]);
      if (i + 1 < list.size()) std::cout << ", ";
    }
    std::cout << "]";
  } else if (val.is_dict()) {
    std::cout << "{";
    const auto& dict = val.as_dict();
    bool first = true;
    for (const auto& [k, v] : dict) {
      if (!first) std::cout << ", ";
      std::cout << "\"" << k << "\": ";
      print_bencode_value(v);
      first = false;
    }
    std::cout << "}";
  }
}

void test_bencode_value() {
  std::cout << "Running BencodeValue tests...\n";

  // 1. Test integer construction and accessor
  std::cout << "  [BencodeValue] Testing integers...\n";
  BencodeValue val_int(42);
  assert(val_int.is_int());
  assert(!val_int.is_string());
  assert(val_int.as_int() == 42);

  // 2. Test string construction and accessor
  std::cout << "  [BencodeValue] Testing strings...\n";
  BencodeValue val_str("hello");
  assert(val_str.is_string());
  assert(!val_int.is_string());
  assert(val_str.as_string() == "hello");

  // 3. Test list construction and accessor
  std::cout << "  [BencodeValue] Testing lists...\n";
  BencodeList list;
  list.push_back(BencodeValue(123));
  list.push_back(BencodeValue("world"));

  BencodeValue val_list(list);
  assert(val_list.is_list());
  assert(val_list.as_list().size() == 2);
  assert(val_list.as_list()[0].as_int() == 123);
  assert(val_list.as_list()[1].as_string() == "world");

  // 4. Test dict construction and accessor
  std::cout << "  [BencodeValue] Testing dicts...\n";
  BencodeDict dict;
  dict["key1"] = BencodeValue(999);
  dict["key2"] = BencodeValue("value2");

  BencodeValue val_dict(dict);
  assert(val_dict.is_dict());
  assert(val_dict.as_dict().size() == 2);
  assert(val_dict.as_dict().at("key1").as_int() == 999);
  assert(val_dict.as_dict().at("key2").as_string() == "value2");

  std::cout << "  [BencodeValue] Testing exceptions...\n";
  try {
    [[maybe_unused]] auto res = val_int.as_string();
    assert(false); // Should not reach here
  } catch (const std::runtime_error& e) {
    std::cout << "  Expected exception caught: " << e.what() << "\n";
  }

  std::cout << "All BencodeValue tests passed successfully!\n\n";
}

void test_bencode_parser() {
  std::cout << "Running BencodeParser tests...\n";

  auto to_span = [](std::string_view sv) {
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
  };

  // Helper to test successful parsing
  auto expect_parse_ok = [&](std::string_view sv) {
    return BencodeParser::parse(to_span(sv));
  };

  // Helper to test parsing that should fail
  auto expect_parse_fail = [&](std::string_view sv, std::string_view expected_err) {
    try {
      BencodeParser::parse(to_span(sv));
      std::cerr << "Expected exception not thrown for input: '" << sv << "'\n";
      assert(false); // Should have thrown an exception
    } catch (const std::runtime_error& e) {
      std::string msg = e.what();
      if (msg.find(expected_err) == std::string::npos) {
        std::cerr << "Test failed for input: '" << sv << "'\n"
                  << "  Expected substring: \"" << expected_err << "\"\n"
                  << "  Actual error message: \"" << msg << "\"\n";
        assert(false);
      }
    }
  };

  // 1. Test integers
  std::cout << "  [BencodeParser] Testing integers...\n";
  {
    auto val1 = expect_parse_ok("i42e");
    std::cout << "    Parsed 'i42e' -> "; print_bencode_value(val1); std::cout << "\n";
    assert(val1.as_int() == 42);

    auto val2 = expect_parse_ok("i-42e");
    std::cout << "    Parsed 'i-42e' -> "; print_bencode_value(val2); std::cout << "\n";
    assert(val2.as_int() == -42);

    auto val3 = expect_parse_ok("i0e");
    std::cout << "    Parsed 'i0e' -> "; print_bencode_value(val3); std::cout << "\n";
    assert(val3.as_int() == 0);
  }

  expect_parse_fail("i03e", "leading zeros");
  expect_parse_fail("i-0e", "-0 is not allowed");
  expect_parse_fail("i00e", "leading zeros");
  expect_parse_fail("ie", "expected digits");
  expect_parse_fail("i42", "expected 'e'");

  // 2. Test strings
  std::cout << "  [BencodeParser] Testing strings...\n";
  {
    auto val1 = expect_parse_ok("5:hello");
    std::cout << "    Parsed '5:hello' -> "; print_bencode_value(val1); std::cout << "\n";
    assert(val1.as_string() == "hello");

    auto val2 = expect_parse_ok("0:");
    std::cout << "    Parsed '0:' -> "; print_bencode_value(val2); std::cout << "\n";
    assert(val2.as_string() == "");
  }

  expect_parse_fail("03:abc", "leading zeros");
  expect_parse_fail("5:abc", "unexpected end");
  expect_parse_fail("abc", "invalid first character");

  // 3. Test lists
  std::cout << "  [BencodeParser] Testing lists...\n";
  {
    auto list_val = expect_parse_ok("li42e4:spame");
    std::cout << "    Parsed 'li42e4:spame' -> "; print_bencode_value(list_val); std::cout << "\n";
    assert(list_val.is_list());
    assert(list_val.as_list().size() == 2);
    assert(list_val.as_list()[0].as_int() == 42);
    assert(list_val.as_list()[1].as_string() == "spam");
  }

  expect_parse_fail("li42e", "expected 'e'");

  // 4. Test dicts
  std::cout << "  [BencodeParser] Testing dicts...\n";
  {
    auto dict_val = expect_parse_ok("d3:cow3:moo4:spam4:eggse");
    std::cout << "    Parsed 'd3:cow3:moo4:spam4:eggse' -> "; print_bencode_value(dict_val); std::cout << "\n";
    assert(dict_val.is_dict());
    assert(dict_val.as_dict().size() == 2);
    assert(dict_val.as_dict().at("cow").as_string() == "moo");
    assert(dict_val.as_dict().at("spam").as_string() == "eggs");
  }

  // Sorting/duplicate checks
  expect_parse_fail("d4:spam4:eggs3:cow3:mooe", "keys not sorted");
  expect_parse_fail("d3:cow3:moo3:cow3:mooe", "duplicate key");

  std::cout << "All BencodeParser tests passed successfully!\n";
}

int main(int argc, char *argv[]) {
  init_logger();
  welcome_msg();

  // Run BencodeValue verification tests
  test_bencode_value();

  // Run BencodeParser verification tests
  test_bencode_parser();

  // Parse command line arguments for --log-level=
  std::string log_level = "info"; // Default string
  std::string_view target_flag = "--log-level=";

  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (arg.starts_with(target_flag)) {
      log_level = arg.substr(target_flag.size());
      break;
    }
  }

  set_log_level(log_level);

  return 0;
}