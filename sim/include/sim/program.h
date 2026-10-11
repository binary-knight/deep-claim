#pragma once

// The race for the program (spec 010): delivering goods toward the stage a
// company is working on, and completing that stage. Company::programStage
// counts the stages completed; the program is complete when it equals the
// number of stages in Content::program.
//
// The ending (tick pipeline step 8b for the deadline): the first company to
// complete the last stage founds the program, in the commands step, and the
// game ends there; if the deadline day closes with no founder, everyone has
// lost. WorldState::outcome is set once and a finished world never changes.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/world_state.h"

#include <cstdint>
#include <optional>

namespace sim {

// A delivery moves tonnes from the company's stock to its program, up to
// what the current stage still needs of that good. It is not a sale: no
// proceeds, and the market does not see it.
[[nodiscard]] std::optional<RejectReason> validateDeliverToProgram(const Content& content,
                                                                   const WorldState& state,
                                                                   const DeliverToProgram& command);
void applyDeliverToProgram(WorldState& state, const DeliverToProgram& command, TickReport& report);

// Completing a stage needs its research done, every requirement delivered in
// full and the cost within the company's available cash. It charges the
// cost, clears the deliveries and moves the company to the next stage.
// Completing the last stage founds the program and sets the outcome at once:
// the founder Founded; any other company LesserPartner if it has completed
// at least two stages and is not bankrupt, Lost otherwise. It raises a
// public ProgramFounded notice after the stage's own.
[[nodiscard]] std::optional<RejectReason> validateCompleteProgramStage(
    const Content& content, const WorldState& state, const CompleteProgramStage& command);
void applyCompleteProgramStage(const Content& content, WorldState& state,
                               const CompleteProgramStage& command, TickReport& report);

// True exactly when a CompleteProgramStage order from the company would be
// accepted today. False for an unknown company.
[[nodiscard]] bool programStageReady(const Content& content, const WorldState& state,
                                     CompanyId company);

// How far the company's deliveries have got toward its current stage, in
// thousandths: the mean over the stage's requirements of the share of each
// that has been delivered, rounded down. Each good counts by its own need,
// so a small requirement weighs as much as a large one. 1000 when the stage
// asks for no goods or the program is complete; 0 for an unknown company.
// Research and cash are not part of it.
[[nodiscard]] std::int64_t programProgressPermille(const Content& content, const WorldState& state,
                                                   CompanyId company);

// True once the outcome is set. From then on a tick changes nothing and
// every order is rejected as GameEnded.
[[nodiscard]] bool gameEnded(const WorldState& state);

// The deadline, at the end of the day after events: when the game has not
// ended and the day has reached Content::program.deadlineDay, every company
// has lost and a public DeadlinePassed notice is raised. Orders are applied
// earlier in the day, so a last stage completed on the deadline day founds
// the program.
void runProgram(const Content& content, WorldState& state, TickReport& report);

}  // namespace sim
