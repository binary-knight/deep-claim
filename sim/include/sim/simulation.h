#pragma once

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/types.h"
#include "sim/view.h"
#include "sim/world_state.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sim {

struct GameSetup {
    std::string playerName = "Player";
    std::vector<std::string> aiNames;  // one AI company per name
};

class Simulation {
public:
    // Generates a new world from `content`, which must outlive the Simulation
    // and never change while it is in use. Company 0 is the player.
    Simulation(const Content& content, Seed seed, const GameSetup& setup);

    // Queues a command for the next tick.
    void submit(const Command& command);

    // Advances the world by one in-game day and reports what happened.
    const TickReport& tick();
    void tick(Day days);
    [[nodiscard]] const TickReport& lastReport() const { return report_; }

    [[nodiscard]] CompanyView view(CompanyId company) const;

    [[nodiscard]] const Content& content() const { return *content_; }
    [[nodiscard]] const WorldState& state() const { return state_; }
    [[nodiscard]] std::uint64_t stateHash() const;

    [[nodiscard]] std::vector<std::uint8_t> save() const;
    // Replaces the current state and drops queued commands. Returns false and
    // leaves everything untouched if the bytes are not a valid save for this
    // content.
    [[nodiscard]] bool load(std::span<const std::uint8_t> bytes);

private:
    void applyCommands();

    const Content* content_;
    std::uint64_t contentHash_;
    WorldState state_;
    std::vector<Command> pending_;
    TickReport report_;
};

}  // namespace sim
