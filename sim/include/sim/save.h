#pragma once

#include "sim/world_state.h"

#include <cstdint>
#include <span>
#include <vector>

namespace sim {

// Save stream layout: 4-byte magic "DCSV", uint32 format version, then the
// serialized WorldState. The version is bumped whenever WorldState's
// serialize() changes shape; older saves are rejected rather than migrated
// until the game ships.
constexpr std::uint32_t kSaveVersion = 1;

[[nodiscard]] std::uint64_t hashWorld(const WorldState& state);
[[nodiscard]] std::vector<std::uint8_t> saveWorld(const WorldState& state);

// Returns false on bad magic, unknown version, truncation or trailing bytes.
// On failure `out` is unspecified.
[[nodiscard]] bool loadWorld(std::span<const std::uint8_t> bytes, WorldState& out);

}  // namespace sim
