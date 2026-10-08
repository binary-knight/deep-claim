#pragma once

#include "sim/content.h"
#include "sim/world_state.h"

namespace sim {

// Populates plots and hidden deposits from the region definitions, drawing
// from state.rng.world. Called once when a game starts. See DESIGN.md for
// the sampling model.
void generateWorld(const Content& content, WorldState& state);

}  // namespace sim
