#pragma once

// World events (tick pipeline step 8): random, time-limited multipliers on
// demand, mine opex and land prices. Everything about events is public.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/world_state.h"

namespace sim {

// Product of the matching effects of every active event (in event order),
// clamped to [0.01, 100]. `resource` matters only for DemandMult.
[[nodiscard]] Fixed eventMultiplier(const Content& content, const WorldState& state,
                                    EventEffectType type, ResourceId resource = ResourceId());

// Expires events that end today, then rolls for new ones.
void runEvents(const Content& content, WorldState& state, TickReport& report);

}  // namespace sim
