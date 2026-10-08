#pragma once

#include <cstdint>

namespace sim {

// One tick is one in-game day. The calendar is 12 months of 30 days.
using Day = std::uint32_t;
using Seed = std::uint64_t;

constexpr Day kDaysPerMonth = 30;
constexpr Day kMonthsPerYear = 12;
constexpr Day kDaysPerYear = kDaysPerMonth * kMonthsPerYear;

// Longest supported game: 10,000 in-game years. Saves beyond it are
// rejected and ticking past it aborts, so day arithmetic cannot wrap.
constexpr Day kMaxDay = kDaysPerYear * 10'000;

}  // namespace sim
