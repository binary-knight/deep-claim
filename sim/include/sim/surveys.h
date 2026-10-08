#pragma once

// Surveys (tick pipeline step 3): noisy, private estimates of a plot's
// deposits, paid up front and delivered after a delay.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/world_state.h"

#include <optional>

namespace sim {

// Log-normal sigma of a company's survey readings after tech effects.
[[nodiscard]] Fixed surveyNoise(const Content& content, const Company& company);

[[nodiscard]] std::optional<RejectReason> validateSurvey(const Content& content,
                                                         const WorldState& state,
                                                         const CommissionSurvey& command);
void applySurvey(const Content& content, WorldState& state, const CommissionSurvey& command,
                 TickReport& report);

// Completes surveys due today, sampling estimates from hidden truth.
void runSurveys(const Content& content, WorldState& state, TickReport& report);

}  // namespace sim
