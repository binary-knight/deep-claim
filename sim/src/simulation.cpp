#include "sim/simulation.h"

#include "sim/save.h"

namespace sim {

Simulation::Simulation(Seed seed) {
    state_.rng = RngStreams(seed);
}

void Simulation::tick() {
    // The full tick pipeline is documented in DESIGN.md. Only the final
    // step exists so far; the others are added one system at a time.
    state_.noise = state_.noise * 31U + state_.rng.world.next();
    ++state_.day;
}

void Simulation::tick(Day days) {
    for (Day i = 0; i < days; ++i) {
        tick();
    }
}

std::uint64_t Simulation::stateHash() const {
    return hashWorld(state_);
}

std::vector<std::uint8_t> Simulation::save() const {
    return saveWorld(state_);
}

bool Simulation::load(std::span<const std::uint8_t> bytes) {
    WorldState loaded;
    if (!loadWorld(bytes, loaded)) {
        return false;
    }
    state_ = loaded;
    return true;
}

}  // namespace sim
