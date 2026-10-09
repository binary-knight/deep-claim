#pragma once

// A standing order to sell ore every day (spec 001). Shared by the balance
// defaults in Content and the per-company state in WorldState.

#include "sim/money.h"

#include <cstdint>

namespace sim {

struct SellPolicy {
    std::uint8_t enabled = 0;
    std::int64_t reserveTonnes = 0;  // keep this much in the warehouse
    Money floorPrice;                // sell only at or above this price per tonne

    friend bool operator==(const SellPolicy&, const SellPolicy&) = default;
};

}  // namespace sim
