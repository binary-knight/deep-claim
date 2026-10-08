#pragma once

#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/types.h"

#include <cstdint>
#include <vector>

namespace sim {

enum class PlotListing : std::uint8_t {
    Unlisted,  // held back; listed on the schedule in balance.json
    Listed,    // open for bids
    Sold,      // owned by a company
};

// Public plot data. Everything here may be shown to every company.
struct Plot {
    RegionId region;
    std::int64_t areaHectares = 0;
    Money askPrice;  // base land price; the auction (step 6) starts here
    PlotListing listing = PlotListing::Unlisted;
    CompanyId owner;  // invalid while unowned
};

// True deposit of one resource on one plot. Hidden from every company;
// surveys produce noisy estimates of tonnesTotal and extraction reveals it.
struct DepositTruth {
    ResourceId resource;
    std::int64_t tonnesTotal = 0;
    std::int64_t tonnesRemaining = 0;
};

struct PlotDeposits {
    std::vector<DepositTruth> deposits;  // only resources actually present
};

// State that no company view may read. Kept in one struct so the view code
// (step 5) can be audited for never touching it.
struct HiddenState {
    std::vector<PlotDeposits> plotDeposits;  // indexed by PlotId
};

// The complete dynamic state of a game. Everything that affects the future
// of the simulation lives here so that save, load, hashing and replay all
// operate on one struct. Entities are stored in vectors indexed by StrongId
// and are never erased; dead entities carry a flag instead.
struct WorldState {
    Day day = 0;
    RngStreams rng;
    std::vector<Plot> plots;
    HiddenState hidden;
};

}  // namespace sim
