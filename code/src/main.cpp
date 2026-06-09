#include "logger.hpp"
#include <iostream>

void welcome_msg() {
  std::cout << "------------------------------------------------\n";
  std::cout << "| Welcome to P2P distributed transfer engine !! |\n";
  std::cout << "------------------------------------------------\n\n";
}

int main(int argc, char *argv[]) {
  init_logger();
  welcome_msg();

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