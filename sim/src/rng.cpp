#include "sim/rng.h"

namespace sim {

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t stream) : state_(0), inc_((stream << 1U) | 1U) {
    next();
    state_ += seed;
    next();
}

std::uint32_t Pcg32::next() {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
    const auto rot = static_cast<std::uint32_t>(old >> 59U);
    return (xorshifted >> rot) | (xorshifted << ((32U - rot) & 31U));
}

std::uint32_t Pcg32::below(std::uint32_t bound) {
    // Lemire's nearly divisionless method with rejection, so results are
    // unbiased and identical on every platform.
    std::uint64_t product = static_cast<std::uint64_t>(next()) * bound;
    auto low = static_cast<std::uint32_t>(product);
    if (low < bound) {
        const std::uint32_t threshold = (0U - bound) % bound;
        while (low < threshold) {
            product = static_cast<std::uint64_t>(next()) * bound;
            low = static_cast<std::uint32_t>(product);
        }
    }
    return static_cast<std::uint32_t>(product >> 32U);
}

}  // namespace sim
