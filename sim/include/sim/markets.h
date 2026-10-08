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

// Daily price update from today's sales and the demand walk; appends to the
// price history and resets soldToday.
void runMarkets(const Content& content, WorldState& state);

}  // namespace sim
