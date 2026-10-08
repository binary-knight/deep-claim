#pragma once

#include "sim/fixed.h"
#include "sim/rng.h"
#include "sim/types.h"

#include <cstdint>

namespace sim {

// One generator per system. Adding a random draw to one system never
// perturbs the others, which keeps balance tuning and bug hunting sane.
struct RngStreams {
    Pcg32 world;
    Pcg32 market;
    Pcg32 survey;
    Pcg32 event;
    Pcg32 ai;

    RngStreams() = default;
    explicit RngStreams(Seed seed);
};

// Uniform Fixed in [0, 1).
[[nodiscard]] Fixed uniformFixed(Pcg32& rng);

// Uniform integer in [low, high] inclusive. high - low must be below 2^32 - 1.
[[nodiscard]] std::int64_t uniformInt(Pcg32& rng, std::int64_t low, std::int64_t high);

// Approximately standard normal (mean 0, standard deviation 1) using the
// Irwin-Hall sum of twelve uniforms. Bounded to [-6, 6]. Integer-only.
[[nodiscard]] Fixed approxNormal(Pcg32& rng);

}  // namespace sim
