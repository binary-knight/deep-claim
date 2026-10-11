#pragma once

// Mines (tick pipeline step 4): construction, extraction, depletion, opex.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/world_state.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sim {

[[nodiscard]] Money mineCapex(const Content& content, const Company& company, const Plot& plot);
// What a company with these techs would pay to build on a plot of this area.
[[nodiscard]] Money mineCapexQuote(const Content& content, const std::vector<TechId>& completed,
                                   std::int64_t areaHectares);

// What `mine` costs its owner for one day in state `as` (spec 008). The
// highest mineOpexPerDay among the mine's cost ores, times the owner's
// research cost multiplier and the active event cost multiplier, times the
// mothball share when `as` is Mothballed; zero when Building, Depleted or
// Closed. The cost ores are the ores the mine produced on its most recent
// operating day (lastOutputTonnes); when there are none, every ore the owner
// has unlocked. Never the plot's true deposits: a cost must not reveal an
// ore the owner has not seen. `as` is passed apart from mine.state so a
// caller can ask what a mothballed mine would cost to run. Saturates at
// kMaxCashCents.
[[nodiscard]] Money mineRunningCost(const Content& content, const WorldState& world,
                                    const Company& company, const Mine& mine, MineState as);

// What an operating mine producing `resource` costs `company` today: the
// ore's mineOpexPerDay times the same research and event cost multipliers
// mineRunningCost applies. Quoted whether or not the company can extract the
// ore yet; zero for a resource the content does not have. Saturates at
// kMaxCashCents.
[[nodiscard]] Money mineOpexQuote(const Content& content, const WorldState& world,
                                  const Company& company, ResourceId resource);

// What `mine` earns in a day at `prices` (one per resource, per tonne): its
// most recent operating day's output at those prices, less its running cost
// as an operating mine. For a mothballed mine this is what it would earn if
// reopened; a mine that has produced nothing shows the bare cost as a loss.
// An ore with no entry in `prices`, and a negative price or tonnage, counts
// as nothing. Each term and the result saturate at +/- kMaxCashCents.
[[nodiscard]] Money mineDailyMargin(const Content& content, const WorldState& world,
                                    const Company& company, const Mine& mine,
                                    std::span<const Money> prices);

// The price of the mine's principal ore (the one with the largest output x
// price; the lowest id on a tie) at which mineDailyMargin is zero, the other
// ores held at `prices`. Rounded up to a whole cent, never negative: zero
// when the other ores alone cover the cost, and zero when the mine has no
// output to price.
[[nodiscard]] Money mineBreakEvenPrice(const Content& content, const WorldState& world,
                                       const Company& company, const Mine& mine,
                                       std::span<const Money> prices);

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

// The owner's latest completed survey of the mine's plot, or null.
[[nodiscard]] const Survey* latestOwnSurvey(const WorldState& state, const Mine& mine);

// Tonnes the owner should expect to be left of one surveyed resource: the
// survey's estimate (of what was in the ground when it completed) less what
// the mine has taken since. A survey that completed before the mine opened
// predates all extraction. For a later survey the state keeps no baseline,
// so extraction since is taken as the last daily output times the days
// since, capped at the lifetime total; output declines slowly, so this errs
// a little high.
[[nodiscard]] std::int64_t estimatedRemainingTonnes(const WorldState& state, const Mine& mine,
                                                    const Survey& survey,
                                                    const SurveyEstimate& estimate);

// Month end: a MineRunningLow notice (tonnes = days, floored at zero) for
// an operating mine of a solvent company when the low end of its latest
// survey (estimate x (1 - relative error)) lasts kRunningLowDays or fewer
// at the current output. Given once per survey: the mine remembers which
// survey it has warned about. The low end is used because a survey that
// reads high would otherwise never warn (playtest 2). Unsurveyed mines
// never warn.
constexpr Day kRunningLowDays = 30;
void warnRunningLow(const Content& content, WorldState& state, TickReport& report);

}  // namespace sim
