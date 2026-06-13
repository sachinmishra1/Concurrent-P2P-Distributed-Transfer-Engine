#pragma once

#include <array>
#include <cstdint>

// Generates an Azureus-style peer ID starting with "-DT0001-" followed by 12 random alphanumeric characters.
std::array<uint8_t, 20> generate_peer_id();
