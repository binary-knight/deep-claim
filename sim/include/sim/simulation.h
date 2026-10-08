#pragma once

#include "sim/types.h"
#include "sim/world_state.h"

#include <cstdint>

namespace sim {

class Simulation {
public:
    explicit Simulation(Seed seed);

    // Advances the world by one in-game day.
    void tick();
    void tick(Day days);

    [[nodiscard]] const WorldState& state() const { return state_; }
    [[nodiscard]] std::uint64_t stateHash() const;

private:
    WorldState state_;
};

}  // namespace sim
