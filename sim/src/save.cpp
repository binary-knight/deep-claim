#include "sim/save.h"

#include "sim/archive.h"
#include "sim/serialize.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sim {

namespace {
constexpr std::array<std::uint8_t, 4> kMagic = {'D', 'C', 'S', 'V'};
}  // namespace

std::uint64_t hashWorld(const WorldState& state) {
    return hashOf(state);
}

std::vector<std::uint8_t> saveWorld(const WorldState& state) {
    WriteArchive archive;
    for (const std::uint8_t byte : kMagic) {
        archive(byte);
    }
    std::uint32_t version = kSaveVersion;
    archive(version);
    archive(const_cast<WorldState&>(state));  // NOLINT(cppcoreguidelines-pro-type-const-cast)
    return archive.release();
}

bool loadWorld(std::span<const std::uint8_t> bytes, WorldState& out) {
    ReadArchive archive(bytes);
    for (const std::uint8_t expected : kMagic) {
        std::uint8_t byte = 0;
        archive(byte);
        if (byte != expected) {
            return false;
        }
    }
    std::uint32_t version = 0;
    archive(version);
    if (archive.failed() || version != kSaveVersion) {
        return false;
    }
    archive(out);
    return !archive.failed() && archive.atEnd();
}

}  // namespace sim
