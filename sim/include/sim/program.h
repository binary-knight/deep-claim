#pragma once

// The race for the program (spec 010): delivering goods toward the stage a
// company is working on, and completing that stage. Company::programStage
// counts the stages completed; the program is complete when it equals the
// number of stages in Content::program.
//
// The ending: the first company to complete the last stage founds the
// program, in the commands step (tick pipeline step 1), and the game ends
// with that order; if the deadline day comes and its orders found nothing,
// everyone has lost (step 1b, straight after the orders). The world freezes
// on the day it ends: no later step of that tick runs and the day does not
// advance, so Outcome::day equals WorldState::day from then on.
// WorldState::outcome is set once and a finished world never changes.

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

// The earliest day the company could found the program (spec 011, BR-001),
// were every remaining stage project started as soon as the rules allow and
// goods and cash never held it up. Counted from WorldState::day, the day an
// order given now applies: a project started on day D finishes on D plus its
// duration, its stage's completion order applies the day after, and the next
// stage's project can start on that same day; a lab busy with another
// project takes the stage's the day after that project ends. A founded
// program's day is the day of its last stage. No research can be sped up, so
// the day never moves earlier. Pure: it reads research records, the stages
// completed and their days, and bankruptcy, never cash or deliveries. Empty
// for a bankrupt or unknown company, or content with no program.
[[nodiscard]] std::optional<Day> earliestFoundingDay(const Content& content,
                                                     const WorldState& state, CompanyId company);
// The founding day for a company whose project for `stage` (from zero)
// starts on startDay: startDay plus, over that stage and every later one,
// the project's duration and a day for the completion order.
[[nodiscard]] Day foundingDayIfStartedOn(const Content& content, std::uint32_t stage, Day startDay);
// The earliest founding day among the company's solvent rivals; empty when
// it has none.
[[nodiscard]] std::optional<Day> leadingRivalFoundingDay(const Content& content,
                                                         const WorldState& state,
                                                         CompanyId company);
// True when the company's earliest founding day is after the deadline day; a
// founding on the deadline day is in time. False when it has no day. The one
// statement of that test: the view and the pace check both answer from it.
[[nodiscard]] bool foundsPastDeadline(const Content& content, const WorldState& state,
                                      CompanyId company);

// How the race ends for a company that did not found the program: a lesser
// partner when another company founded it and this one has completed at
// least two stages and is not bankrupt; lost otherwise, and always when
// nobody founded it. The one statement of the rule: the ending applies it
// and a save is checked against it.
[[nodiscard]] ProgramResult programResultBesideFounder(const Company& company, bool founded);

// True once the outcome is set. From then on a tick changes nothing, the
// day included, and every order is rejected as GameEnded.
[[nodiscard]] bool gameEnded(const WorldState& state);

// The deadline, straight after the day's orders: when the game has not ended
// and the day has reached Content::program.deadlineDay, every company has
// lost and a public DeadlinePassed notice is raised. Orders are applied
// first, so a last stage completed on the deadline day founds the program.
// Nothing else of the deadline day is played.
void runProgram(const Content& content, WorldState& state, TickReport& report);

}  // namespace sim
