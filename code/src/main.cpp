#include "logger.hpp"
#include "bencode.hpp"
#include <iostream>
#include <cassert>

void welcome_msg() {
  std::cout << "------------------------------------------------\n";
  std::cout << "| Welcome to P2P distributed transfer engine !! |\n";
  std::cout << "------------------------------------------------\n\n";
}

void test_bencode_value() {
  std::cout << "Running BencodeValue tests...\n";

  // 1. Test integer construction and accessor
  BencodeValue val_int(42);
  assert(val_int.is_int());
  assert(!val_int.is_string());
  assert(val_int.as_int() == 42);

  // 2. Test string construction and accessor
  BencodeValue val_str("hello");
  assert(val_str.is_string());
  assert(!val_int.is_string());
  assert(val_str.as_string() == "hello");

  // 3. Test list construction and accessor
  BencodeList list;
  list.push_back(BencodeValue(123));
  list.push_back(BencodeValue("world"));

  BencodeValue val_list(list);
  assert(val_list.is_list());
  assert(val_list.as_list().size() == 2);
  assert(val_list.as_list()[0].as_int() == 123);
  assert(val_list.as_list()[1].as_string() == "world");

  // 4. Test dict construction and accessor
  BencodeDict dict;
  dict["key1"] = BencodeValue(999);
  dict["key2"] = BencodeValue("value2");

  BencodeValue val_dict(dict);
  assert(val_dict.is_dict());
  assert(val_dict.as_dict().size() == 2);
  assert(val_dict.as_dict().at("key1").as_int() == 999);
  assert(val_dict.as_dict().at("key2").as_string() == "value2");

  try {
    [[maybe_unused]] auto res = val_int.as_string();
    assert(false); // Should not reach here
  } catch (const std::runtime_error& e) {
    std::cout << "Expected exception caught: " << e.what() << "\n";
  }

  std::cout << "All BencodeValue tests passed successfully!\n";
}

int main(int argc, char *argv[]) {
  init_logger();
  welcome_msg();

  // Run BencodeValue verification tests
  test_bencode_value();

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