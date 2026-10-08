#pragma once

#include <cstdint>

namespace sim {

// PCG32 (O'Neill): 64-bit state, 32-bit output, XSH-RR output function.
// Implemented locally rather than via <random> because the standard
// library's distributions differ between libstdc++ and MSVC, which would
// break cross-platform determinism.
class Pcg32 {
public:
    Pcg32() = default;
    Pcg32(std::uint64_t seed, std::uint64_t stream);

    std::uint32_t next();

    // Uniform integer in [0, bound). bound must be greater than zero.
    std::uint32_t below(std::uint32_t bound);

    [[nodiscard]] std::uint64_t state() const { return state_; }
    [[nodiscard]] std::uint64_t increment() const { return inc_; }
    void restore(std::uint64_t state, std::uint64_t increment) {
        state_ = state;
        inc_ = increment;
    }

private:
    std::uint64_t state_ = 0x853c49e6748fea9bULL;
    std::uint64_t inc_ = 0xda3e39cb94b95bdbULL;
};

}  // namespace sim
