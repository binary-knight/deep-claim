#pragma once

#include "sim/rng.h"
#include "sim/types.h"

#include <cstdint>

namespace sim {

// The complete dynamic state of a game. Everything that affects the future
// of the simulation lives here so that save, load, hashing and replay all
// operate on one struct.
struct WorldState {
    Day day = 0;
    Pcg32 rng;                // World stream. Per-system streams arrive in step 2.
    std::uint64_t noise = 0;  // Placeholder accumulator that exercises the RNG every tick.
};

}  // namespace sim
