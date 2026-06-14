cmake_minimum_required(VERSION 3.24)
project(torrentClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(warnings)
include(sanitisers)


include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0 # Use an appropriate stable version
)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.13.0
)
FetchContent_MakeAvailable(spdlog)

find_package(CURL REQUIRED)

FetchContent_Declare(
    cryptopp_wrapper
    GIT_REPOSITORY https://github.com/abdes/cryptopp-cmake.git
    GIT_TAG        CRYPTOPP_8_9_0 
)
FetchContent_MakeAvailable(cryptopp_wrapper)


add_executable(engine 
              src/main.cpp
              src/logger.cpp
              src/bencode.cpp
              src/torrent.cpp
              src/hasher.cpp
              src/tcp_connection.cpp
              src/event_loop.cpp
              src/tracker_client.cpp
              src/peer_id.cpp
              src/peer_message.cpp
              src/peer_connection.cpp
              src/bitfield.cpp
              src/peer_manager.cpp
)

target_include_directories(engine PUBLIC include)
set_target_warnings(engine)
enable_sanitisers(engine)
target_link_libraries(engine PRIVATE
    spdlog::spdlog
    CURL::libcurl
    cryptopp::cryptopp
)

add_executable(tests 
              tests/test_bencode.cpp
              src/bencode.cpp
              src/torrent.cpp
              src/hasher.cpp
              src/tcp_connection.cpp
              src/event_loop.cpp
              src/tracker_client.cpp
              src/peer_id.cpp
              src/peer_message.cpp
              src/peer_connection.cpp
              src/bitfield.cpp
              src/peer_manager.cpp
)
target_include_directories(tests PUBLIC include)
set_target_warnings(tests)
enable_sanitisers(tests)
target_link_libraries(tests PRIVATE
    gtest_main
    spdlog::spdlog
    cryptopp::cryptopp
    CURL::libcurl
)


