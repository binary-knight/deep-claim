#pragma once

#include <cstdint>

namespace sim {

// One tick is one in-game day. The calendar is 12 months of 30 days.
using Day = std::uint32_t;
using Seed = std::uint64_t;

constexpr Day kDaysPerMonth = 30;
constexpr Day kMonthsPerYear = 12;
constexpr Day kDaysPerYear = kDaysPerMonth * kMonthsPerYear;

}  // namespace sim
