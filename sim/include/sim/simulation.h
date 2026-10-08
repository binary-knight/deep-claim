#pragma once

#include "sim/content.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <cstdint>
#include <span>
#include <vector>

namespace sim {

class Simulation {
public:
    // Generates a new world from `content`, which must outlive the Simulation
    // and never change while it is in use.
    Simulation(const Content& content, Seed seed);

    // Advances the world by one in-game day.
    void tick();
    void tick(Day days);

    [[nodiscard]] const Content& content() const { return *content_; }
    [[nodiscard]] const WorldState& state() const { return state_; }
    [[nodiscard]] std::uint64_t stateHash() const;

    [[nodiscard]] std::vector<std::uint8_t> save() const;
    // Replaces the current state. Returns false and leaves the state
    // untouched if the bytes are not a valid save for this content.
    [[nodiscard]] bool load(std::span<const std::uint8_t> bytes);

private:
    const Content* content_;
    std::uint64_t contentHash_;
    WorldState state_;
};

}  // namespace sim
