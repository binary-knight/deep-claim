#pragma once

// Effects of a company's completed research. Research itself (starting,
// progressing, completing techs) arrives in step 11; everything that depends
// on tech reads it through these helpers so that step only fills the list.

#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/world_state.h"

namespace sim {

// Product of every completed tech's effects of the given multiplier type;
// one when none apply.
[[nodiscard]] Fixed techMultiplier(const Content& content, const Company& company,
                                   TechEffectType type);

// A resource is unlocked when it has no unlocking tech or the company has
// completed it. Locked ore is neither extracted nor sold.
[[nodiscard]] bool resourceUnlocked(const Content& content, const Company& company,
                                    ResourceId resource);

}  // namespace sim
