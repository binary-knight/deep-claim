#include "sim/simulation.h"

#include "sim/save.h"
#include "sim/worldgen.h"

namespace sim {

Simulation::Simulation(const Content& content, Seed seed)
    : content_(&content), contentHash_(hashContent(content)) {
    state_.rng = RngStreams(seed);
    generateWorld(content, state_);
}

void Simulation::tick() {
    // The full tick pipeline is documented in DESIGN.md. Only the final
    // step exists so far; the others are added one system at a time.
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
    return saveWorld(state_, contentHash_);
}

bool Simulation::load(std::span<const std::uint8_t> bytes) {
    WorldState loaded;
    if (!loadWorld(bytes, *content_, loaded)) {
        return false;
    }
    state_ = loaded;
    return true;
}

}  // namespace sim
