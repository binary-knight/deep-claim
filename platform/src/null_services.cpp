#include "platform/services.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace platform {

namespace {

class NullServices final : public Services {
public:
    NullServices(std::filesystem::path baseDirectory, std::string playerName)
        : base_(std::move(baseDirectory)), playerName_(std::move(playerName)) {}

    std::filesystem::path saveDirectory() override {
        const std::filesystem::path dir = base_ / "saves";
        std::error_code ignored;
        std::filesystem::create_directories(dir, ignored);
        return dir;
    }
    void unlockAchievement(std::string_view id) override { achievements_.emplace(id); }
    void setStat(std::string_view id, std::int64_t value) override {
        stats_[std::string(id)] = value;
    }
    std::string_view playerName() override { return playerName_; }

private:
    std::filesystem::path base_;
    std::string playerName_;
    std::set<std::string> achievements_;
    std::map<std::string, std::int64_t> stats_;
};

}  // namespace

std::unique_ptr<Services> makeNullServices(std::filesystem::path baseDirectory,
                                           std::string playerName) {
    return std::make_unique<NullServices>(std::move(baseDirectory), std::move(playerName));
}

}  // namespace platform
