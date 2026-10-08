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
// Deadlines (survey completion, mine operational day, auction close) may sit
// a little past kMaxDay because they are created while day < kMaxDay.
constexpr Day kMaxDeadline = kMaxDay + kDaysPerYear * 200;

// Quantity bounds enforced on load so that one more day of accounting can
// never overflow int64. A deposit holds at most 1e15 tonnes; stocks and
// lifetime totals at most 1e18; cash magnitude at most 1e17 cents.
constexpr std::int64_t kMaxDepositTonnes = 1'000'000'000'000'000LL;
constexpr std::int64_t kMaxStockTonnes = 1'000'000'000'000'000'000LL;
constexpr std::int64_t kMaxCashCents = 100'000'000'000'000'000LL;
// Daily market sales above this no longer move the supply average.
constexpr std::int64_t kMaxSupplyPerDay = 1'000'000'000'000LL;

}  // namespace sim
