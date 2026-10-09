#pragma once

// Mines (tick pipeline step 4): construction, extraction, depletion, opex.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/world_state.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace sim {

[[nodiscard]] Money mineCapex(const Content& content, const Company& company, const Plot& plot);
// What a company with these techs would pay to build on a plot of this area.
[[nodiscard]] Money mineCapexQuote(const Content& content, const std::vector<TechId>& completed,
                                   std::int64_t areaHectares);
[[nodiscard]] Money mineDailyOpex(const Content& content, const WorldState& world,
                                  const Company& company, MineState state);

// Tonnes an operating mine would extract today from a deposit of the given
// size: base output scaled by tech and by remaining share (output falls to
// half as the deposit nears exhaustion), capped by what is left.
[[nodiscard]] std::int64_t mineDailyOutput(const Content& content, const Company& company,
                                           ResourceId resource, std::int64_t tonnesTotal,
                                           std::int64_t tonnesRemaining);

[[nodiscard]] std::optional<RejectReason> validateBuildMine(const Content& content,
                                                            const WorldState& state,
                                                            const BuildMine& command);
void applyBuildMine(const Content& content, WorldState& state, const BuildMine& command,
                    TickReport& report);

[[nodiscard]] std::optional<RejectReason> validateSetMineState(const WorldState& state,
                                                               const SetMineState& command);
void applySetMineState(WorldState& state, const SetMineState& command, TickReport& report);

// Finishes construction, extracts, charges opex, marks depletion.
void runMines(const Content& content, WorldState& state, TickReport& report);

// True when the plot under `mine` still holds ore its owner can extract
// today: a remaining deposit of an unlocked resource.
[[nodiscard]] bool mineHasExtractable(const Content& content, const WorldState& state,
                                      const Mine& mine);

// Month end: an IdleMineWarning for every operating mine of a solvent
// company that has nothing extractable (empty, or only locked ore). The
// mine keeps paying full opex; the warning is the player's cue to mothball.
void warnIdleMines(const Content& content, const WorldState& state, TickReport& report);

}  // namespace sim
