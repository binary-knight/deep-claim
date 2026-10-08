#include "sim/simulation.h"

#include "sim/hash.h"

namespace sim {

namespace {
constexpr std::uint64_t kWorldStream = 1;
}  // namespace

Simulation::Simulation(Seed seed) {
    state_.rng = Pcg32(seed, kWorldStream);
}

void Simulation::tick() {
    // The full tick pipeline is documented in DESIGN.md. Only the final
    // step exists so far; the others are added one system at a time.
    state_.noise = state_.noise * 31U + state_.rng.next();
    ++state_.day;
}

void Simulation::tick(Day days) {
    for (Day i = 0; i < days; ++i) {
        tick();
    }
}

std::uint64_t Simulation::stateHash() const {
    Fnv1a64 hasher;
    hasher.add(state_.day);
    hasher.add(state_.rng.state());
    hasher.add(state_.rng.increment());
    hasher.add(state_.noise);
    return hasher.value();
}

}  // namespace sim
