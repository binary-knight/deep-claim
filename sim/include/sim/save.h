#pragma once

#include "sim/content.h"
#include "sim/world_state.h"

#include <cstdint>
#include <span>
#include <vector>

namespace sim {

// Save stream layout: 4-byte magic "DCSV", uint32 format version, uint64
// content fingerprint, then the serialized WorldState. Until the first
// public build the version stays at 1 and saves are not compatible across
// commits. The fingerprint is hashOf(content) so a save cannot be opened
// against reordered or edited data files, which would silently retarget
// every ResourceId and RegionId in the state.
constexpr std::uint32_t kSaveVersion = 1;

[[nodiscard]] std::uint64_t hashWorld(const WorldState& state);

// Structural invariants a loaded world must satisfy before it is used:
// vector sizes agree, every id is in range for `content`, tonnages are
// non-negative and remaining never exceeds total, enums hold known values.
[[nodiscard]] bool validateWorld(const Content& content, const WorldState& state);
[[nodiscard]] std::uint64_t hashContent(const Content& content);
[[nodiscard]] std::vector<std::uint8_t> saveWorld(const WorldState& state,
                                                  std::uint64_t contentHash);

// Returns false on bad magic, unknown version, mismatched content hash,
// truncation, trailing bytes or a world that fails validateWorld. On failure
// `out` is unspecified.
[[nodiscard]] bool loadWorld(std::span<const std::uint8_t> bytes, const Content& content,
                             WorldState& out);

}  // namespace sim
