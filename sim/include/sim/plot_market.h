#pragma once

// Plot listings and sealed-bid auctions (tick pipeline step 2).

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/world_state.h"

#include <optional>

namespace sim {

// Opens an auction for the next `count` plots in the listing queue.
void listPlots(const Content& content, WorldState& state, std::uint32_t count, TickReport& report);

// Closes auctions due today: the highest affordable bid wins and pays, the
// plot becomes Sold; an auction without an affordable bid relists the plot
// at a lower ask. Then lists new plots on month boundaries.
void runPlotMarket(const Content& content, WorldState& state, TickReport& report);

// Cash a company has committed to standing bids on open auctions.
[[nodiscard]] Money reservedCash(const WorldState& state, CompanyId company);

// Cash a company may still commit: cash minus reservations. Every spending
// command (bids now; surveys, mines and research later) validates against
// this so the same money is never promised twice.
[[nodiscard]] Money availableCash(const WorldState& state, CompanyId company);

[[nodiscard]] std::optional<RejectReason> validateBid(const WorldState& state,
                                                      const BidOnPlot& bid);
void applyBid(WorldState& state, const BidOnPlot& bid);

}  // namespace sim
