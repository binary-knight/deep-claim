#include "sim/random.h"

#include <cassert>
#include <cstdint>

namespace sim {

namespace {
constexpr std::uint64_t kWorldStream = 1;
constexpr std::uint64_t kMarketStream = 2;
constexpr std::uint64_t kSurveyStream = 3;
constexpr std::uint64_t kEventStream = 4;
constexpr std::uint64_t kAiStream = 5;
}  // namespace

RngStreams::RngStreams(Seed seed)
    : world(seed, kWorldStream),
      market(seed, kMarketStream),
      survey(seed, kSurveyStream),
      event(seed, kEventStream),
      ai(seed, kAiStream) {}

Fixed uniformFixed(Pcg32& rng) {
    return Fixed::fromRaw(rng.below(static_cast<std::uint32_t>(Fixed::kScale)));
}

std::int64_t uniformInt(Pcg32& rng, std::int64_t low, std::int64_t high) {
    assert(low <= high);
    const std::uint64_t span = static_cast<std::uint64_t>(high - low) + 1U;
    assert(span <= 0xFFFFFFFFU);
    return low + static_cast<std::int64_t>(rng.below(static_cast<std::uint32_t>(span)));
}

Fixed approxNormal(Pcg32& rng) {
    Fixed sum = Fixed::zero();
    for (int i = 0; i < 12; ++i) {
        sum += uniformFixed(rng);
    }
    return sum - Fixed::fromInt(6);
}

}  // namespace sim
