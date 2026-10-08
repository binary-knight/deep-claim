#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace platform {

// Everything the game asks of the hosting platform. Steamworks will implement
// this later; until then NullServices provides local defaults.
class Services {
public:
    Services() = default;
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;
    Services(Services&&) = delete;
    Services& operator=(Services&&) = delete;
    virtual ~Services() = default;

    // Directory for save files; created if missing.
    [[nodiscard]] virtual std::filesystem::path saveDirectory() = 0;
    virtual void unlockAchievement(std::string_view id) = 0;
    virtual void setStat(std::string_view id, std::int64_t value) = 0;
    [[nodiscard]] virtual std::string_view playerName() = 0;
};

// Local-only services: saves under `baseDirectory`, achievements and stats
// are recorded in memory and otherwise ignored.
std::unique_ptr<Services> makeNullServices(std::filesystem::path baseDirectory,
                                           std::string playerName);

}  // namespace platform
