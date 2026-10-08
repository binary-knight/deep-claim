#pragma once

#include "sim/random.h"
#include "sim/types.h"

#include <cstdint>

namespace sim {

// The complete dynamic state of a game. Everything that affects the future
// of the simulation lives here so that save, load, hashing and replay all
// operate on one struct. Entities are stored in vectors indexed by StrongId
// and are never erased; dead entities carry a flag instead.
struct WorldState {
    Day day = 0;
    RngStreams rng;
    std::uint64_t noise = 0;  // Placeholder accumulator that exercises the RNG every tick.
};

}  // namespace sim
