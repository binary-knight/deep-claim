#pragma once

// Commodity markets (tick pipeline step 5): one price per resource that
// responds to the supply all companies sell into it and to a random demand
// walk. See DESIGN.md for the model.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/world_state.h"

#include <optional>

namespace sim {

// Creates one market per resource at its base price. Called at game start.
void initMarkets(const Content& content, WorldState& state);

[[nodiscard]] std::optional<RejectReason> validateSell(const WorldState& state,
                                                       const SellOre& command);
void applySell(WorldState& state, const SellOre& command, TickReport& report);

// Moves `tonnes` of a resource from the company's warehouse to the market at
// today's price and returns the proceeds. No validation and no notice: the
// caller has already bounded the quantity (see saleTonnesWithinBounds).
Money executeSale(WorldState& state, CompanyId company, ResourceId resource, std::int64_t tonnes);
// The largest quantity up to `tonnes` the company can sell today: within
// its stock and within the cash cap. Zero when nothing can be sold.
[[nodiscard]] std::int64_t saleTonnesWithinBounds(const WorldState& state, CompanyId company,
                                                  ResourceId resource, std::int64_t tonnes);

// Standing sell orders (spec 001).
[[nodiscard]] std::optional<RejectReason> validateSetSellPolicy(const WorldState& state,
                                                                const SetSellPolicy& command);
void applySetSellPolicy(WorldState& state, const SetSellPolicy& command);
[[nodiscard]] std::optional<RejectReason> validateSetCoverCosts(const WorldState& state,
                                                                const SetCoverCosts& command);
void applySetCoverCosts(WorldState& state, const SetCoverCosts& command);
// Daily step between mines and markets: every enabled policy whose floor
// is met sells the stock above its reserve at today's price. No notices;
// the company's month-to-date totals are updated instead.
void runSellPolicies(const Content& content, WorldState& state);

// Daily price update from today's sales and the demand walk; appends to the
// price history and resets soldToday.
void runMarkets(const Content& content, WorldState& state);

}  // namespace sim
