#pragma once

#include "sim/content.h"
#include "sim/world_state.h"

#include <cstdint>

namespace sim {

// Populates plots and hidden deposits from the region definitions, drawing
// from state.rng.world. Called once when a game starts. See DESIGN.md for
// the sampling model.
//
// The listing queue (spec 005) is an opening hand followed by the rest:
//   - the first balance.plotsListedAtStart entries hold at least
//     balance.openingMinMineable plots from regions that are Mineable on day
//     one and at most balance.openingMaxLockedRegion from Locked regions;
//   - after the hand, no three consecutive entries share a region.
// Both are promises about regions, never about hidden deposits, and both
// are relaxed, not enforced, when the regions cannot supply them (a world
// with one region, or a pool that has run down to a single region).
void generateWorld(const Content& content, WorldState& state);

// How a region looks to a company with no research.
enum class RegionStart : std::uint8_t {
    Mineable,  // some ore unlocked from the start is present on at least half the plots
    Locked,    // not Mineable, and it holds an ore that needs research
    Other,     // neither: only long-shot ore, or none
};

[[nodiscard]] RegionStart classifyRegionAtStart(const Content& content, const RegionDef& region);

}  // namespace sim
