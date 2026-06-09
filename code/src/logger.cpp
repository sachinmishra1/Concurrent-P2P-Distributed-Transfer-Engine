#include "logger.hpp"

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

void init_logger() {
  // Ensure the logs directory exists before opening the file sink
  std::filesystem::create_directories("logs");

  // Create sinks — console (colored) and file (plain)
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
      LOG_FILE_PATH, true);

  console_sink->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] %v%$");
  file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

  // Combine into the default logger
  std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
  auto logger = std::make_shared<spdlog::logger>("p2p_engine", sinks.begin(),
                                                 sinks.end());

  // Only flush on warn and above
  logger->flush_on(spdlog::level::warn);

  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::info);
}

void set_log_level(const std::string &level) {
  std::string normalised = level;
  std::transform(normalised.begin(), normalised.end(), normalised.begin(),
                 ::tolower);

  spdlog::level::level_enum target = spdlog::level::info;

  if (normalised == "trace") {
    target = spdlog::level::trace;
  } else if (normalised == "debug") {
    target = spdlog::level::debug;
  } else if (normalised == "info") {
    target = spdlog::level::info;
  } else if (normalised == "warn") {
    target = spdlog::level::warn;
  } else if (normalised == "error") {
    target = spdlog::level::err;
  } else if (normalised == "critical") {
    target = spdlog::level::critical;
  } else {
    LOG_WARN("Unknown log level '{}'. Keeping current level.", level);
    return;
  }

  spdlog::set_level(target);
  spdlog::default_logger()
      ->flush(); // flush any buffered messages before switching level
}
