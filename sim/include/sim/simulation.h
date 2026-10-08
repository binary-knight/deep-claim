#pragma once

#include "sim/types.h"
#include "sim/world_state.h"

#include <cstdint>
#include <span>
#include <vector>

namespace sim {

class Simulation {
public:
    explicit Simulation(Seed seed);

    // Advances the world by one in-game day.
    void tick();
    void tick(Day days);

    [[nodiscard]] const WorldState& state() const { return state_; }
    [[nodiscard]] std::uint64_t stateHash() const;

    [[nodiscard]] std::vector<std::uint8_t> save() const;
    // Replaces the current state. Returns false and leaves the state
    // untouched if the bytes are not a valid save.
    [[nodiscard]] bool load(std::span<const std::uint8_t> bytes);

private:
    WorldState state_;
};

}  // namespace sim
