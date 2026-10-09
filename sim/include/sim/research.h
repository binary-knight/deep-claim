#pragma once

// Research (tick pipeline step 7): one project at a time per company.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/world_state.h"

#include <optional>

namespace sim {

[[nodiscard]] bool techCompleted(const Company& company, TechId tech);
[[nodiscard]] bool prerequisitesMet(const Content& content, const Company& company, TechId tech);
// The company's in-progress project, if any.
[[nodiscard]] std::optional<ResearchId> activeResearch(const WorldState& state, CompanyId company);

[[nodiscard]] std::optional<RejectReason> validateStartResearch(const Content& content,
                                                                const WorldState& state,
                                                                const StartResearch& command);
void applyStartResearch(const Content& content, WorldState& state, const StartResearch& command,
                        TickReport& report);

// Completes projects due today and grants their techs.
void runResearch(const Content& content, WorldState& state, TickReport& report);

}  // namespace sim
