#include "sim/program.h"

#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/research.h"
#include "sim/save.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"
#include "sim/world_state.h"

#include "content/loader.h"
#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

// The program's rules (spec 010, BR-003 and BR-005), called directly; the
// orders going through a session and a tick are covered in trace_test.cpp.
// The endings (BR-006) are played through a Simulation, further down.

namespace {

using R = sim::RejectReason;

const sim::CompanyId kPlayer(0);
const sim::CompanyId kRival(1);
const sim::ResourceId kIron(0);
const sim::ResourceId kCopper(1);
const sim::ResourceId kLithium(2);

// A fresh two-company world with the fixture's program: stage 1 needs 1,000 t
// of iron, stage 2 needs 2,000 t of iron and 200 t of copper, stage 3 needs
// 3,000 t of iron and 50 t of lithium.
sim::WorldState freshState(const sim::Content& content) {
    const sim::Simulation simulation(content, 1, test::setup());
    return simulation.state();
}

std::optional<R> deliver(const sim::Content& content, const sim::WorldState& state,
                         sim::ResourceId resource, std::int64_t tonnes,
                         sim::CompanyId company = kPlayer) {
    return sim::validateDeliverToProgram(content, state, {company, resource, tonnes});
}

std::optional<R> complete(const sim::Content& content, const sim::WorldState& state,
                          sim::CompanyId company = kPlayer) {
    return sim::validateCompleteProgramStage(content, state, {company});
}

// The company with `stages` stages complete, as a save holds it: each on
// day 1 (no stage can be completed on day 0) and with its research done.
void atStage(const sim::Content& content, sim::Company& company, std::uint32_t stages) {
    company.programStage = stages;
    company.programStageDays.assign(stages, 1);
    for (std::uint32_t s = 0; s < stages; ++s) {
        const sim::TechId research = content.program.stages[s].research;
        if (!sim::techCompleted(company, research)) {
            company.completedTechs.push_back(research);
        }
    }
}

// Everything the company's current stage asks for, in place: research done,
// goods delivered, and more cash than the cost.
void makeStageReady(const sim::Content& content, sim::WorldState& state,
                    sim::CompanyId id = kPlayer) {
    sim::Company& company = state.companies[id.index()];
    const sim::ProgramStage& stage = content.program.stages[company.programStage];
    company.completedTechs.push_back(stage.research);
    for (const sim::ProgramRequirement& requirement : stage.requirements) {
        company.programDelivered[requirement.resource.index()] = requirement.tonnes;
    }
    company.cash = stage.cost + sim::Money::fromUnits(500'000);
}

bool loadsBack(const sim::Content& content, const sim::WorldState& state) {
    sim::Simulation simulation(content, 1, test::setup());
    return simulation.load(sim::saveGame(state, {}, sim::hashContent(content)));
}

using Result = sim::ProgramResult;
using Kind = sim::NoticeKind;

const sim::Day kRaceDay = 40;

// A two-company world some days into a game, so that an ending has a day to
// record: an outcome never names day 0.
sim::WorldState racingState(const sim::Content& content, sim::Day days = kRaceDay) {
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(days);
    return simulation.state();
}

// The player owning the first plot listed, bought at auction for its asking
// price.
sim::PlotId boughtPlot(sim::Simulation& simulation) {
    const sim::WorldState& state = simulation.state();
    sim::PlotId plot;
    for (std::uint32_t i = 0; i < state.plots.size() && !plot.valid(); ++i) {
        if (state.plots[i].listing == sim::PlotListing::Listed) {
            plot = sim::PlotId(i);
        }
    }
    REQUIRE(plot.valid());
    simulation.submit(sim::BidOnPlot{kPlayer, plot, state.plots[plot.index()].askPrice});
    const sim::Day closes = state.auctions[state.plots[plot.index()].auction.index()].closesOnDay;
    while (state.day <= closes) {
        simulation.tick();
    }
    REQUIRE(state.plots[plot.index()].owner == kPlayer);
    return plot;
}

// The company on its last stage with everything that stage asks for.
void makeFinalStageReady(const sim::Content& content, sim::WorldState& state,
                         sim::CompanyId id = kPlayer) {
    atStage(content, state.companies[id.index()],
            static_cast<std::uint32_t>(content.program.stages.size() - 1));
    makeStageReady(content, state, id);
}

// Bankrupt as the finance step leaves a company: past the grace period, with
// the negative cash that caused it.
void makeBankrupt(const sim::Content& content, sim::WorldState& state, sim::CompanyId id) {
    sim::Company& company = state.companies[id.index()];
    company.bankrupt = 1;
    company.monthsInsolvent = content.balance.bankruptcyGraceMonths + 1;
    company.cash = sim::Money::fromUnits(-1);
}

// Puts an edited world into a simulation the way a save would.
void play(sim::Simulation& simulation, const sim::Content& content, const sim::WorldState& state) {
    REQUIRE(simulation.load(
        sim::saveGame(test::settledPace(content, state), {}, sim::hashContent(content))));
}

std::vector<sim::Notice> noticesOf(const sim::TickReport& report, Kind kind) {
    std::vector<sim::Notice> found;
    for (const sim::Notice& notice : report.notices) {
        if (notice.kind == kind) {
            found.push_back(notice);
        }
    }
    return found;
}

// One of every order, from the player.
std::vector<sim::Command> everyOrder() {
    return {sim::BidOnPlot{kPlayer, sim::PlotId(0), sim::Money::fromUnits(1)},
            sim::CommissionSurvey{kPlayer, sim::PlotId(0)},
            sim::BuildMine{kPlayer, sim::PlotId(0)},
            sim::SetMineState{kPlayer, sim::MineId(0), 0},
            sim::SellOre{kPlayer, kIron, 10},
            sim::TakeLoan{kPlayer, sim::Money::fromUnits(100'000)},
            sim::RepayLoan{kPlayer, sim::LoanId(0), sim::Money::fromUnits(1)},
            sim::StartResearch{kPlayer, sim::TechId(0)},
            sim::SetSellPolicy{kPlayer, kIron, 1, 0, sim::Money::zero()},
            sim::SetCoverCosts{kPlayer, 1},
            sim::DeliverToProgram{kPlayer, kIron, 10},
            sim::CompleteProgramStage{kPlayer}};
}

}  // namespace

TEST_CASE("A delivery moves stock to the program and is not a sale", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = freshState(content);
    state.companies[0].inventoryTonnes[kIron.index()] = 5'000;
    const sim::Money cash = state.companies[0].cash;
    const std::vector<sim::Market> markets = state.markets;

    REQUIRE_FALSE(deliver(content, state, kIron, 400));
    sim::TickReport report;
    sim::applyDeliverToProgram(state, {kPlayer, kIron, 400}, report);

    CHECK(state.companies[0].inventoryTonnes[kIron.index()] == 4'600);
    CHECK(state.companies[0].programDelivered[kIron.index()] == 400);
    CHECK(state.companies[0].programStage == 0);
    // Not a sale: no proceeds and nothing counted as market supply.
    CHECK(state.companies[0].cash == cash);
    for (std::size_t r = 0; r < markets.size(); ++r) {
        CHECK(state.markets[r].soldToday == markets[r].soldToday);
        CHECK(state.markets[r].price == markets[r].price);
    }
    // The rival's program is its own.
    CHECK(state.companies[1].programDelivered[kIron.index()] == 0);

    REQUIRE(report.notices.size() == 1);
    const sim::Notice& notice = report.notices[0];
    CHECK(notice.kind == sim::NoticeKind::ProgramDelivered);
    CHECK(notice.company == kPlayer);
    CHECK(notice.resource == kIron);
    CHECK(notice.tonnes == 400);
    CHECK(notice.amount == sim::Money::zero());
    CHECK(loadsBack(content, state));
}

TEST_CASE("A delivery may fill the stage's remaining need exactly and no further", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = freshState(content);
    state.companies[0].inventoryTonnes[kIron.index()] = 5'000;
    sim::TickReport report;

    CHECK(deliver(content, state, kIron, 1'001) == R::ProgramNotNeeded);
    REQUIRE_FALSE(deliver(content, state, kIron, 1'000));
    sim::applyDeliverToProgram(state, {kPlayer, kIron, 600}, report);
    CHECK(deliver(content, state, kIron, 401) == R::ProgramNotNeeded);
    REQUIRE_FALSE(deliver(content, state, kIron, 400));
    sim::applyDeliverToProgram(state, {kPlayer, kIron, 400}, report);
    CHECK(state.companies[0].programDelivered[kIron.index()] == 1'000);
    // Met in full: one tonne more is not needed.
    CHECK(deliver(content, state, kIron, 1) == R::ProgramNotNeeded);
}

TEST_CASE("A delivery is rejected for each reason the rules give", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = freshState(content);
    state.companies[0].inventoryTonnes[kIron.index()] = 300;
    state.companies[0].inventoryTonnes[kCopper.index()] = 300;

    SECTION("an unknown or bankrupt company") {
        CHECK(deliver(content, state, kIron, 100, sim::CompanyId()) == R::UnknownCompany);
        CHECK(deliver(content, state, kIron, 100, sim::CompanyId(2)) == R::UnknownCompany);
        state.companies[0].bankrupt = 1;
        CHECK(deliver(content, state, kIron, 100) == R::CompanyBankrupt);
    }
    SECTION("an unknown resource") {
        CHECK(deliver(content, state, sim::ResourceId(), 100) == R::UnknownResource);
        CHECK(deliver(content, state, sim::ResourceId(3), 100) == R::UnknownResource);
    }
    SECTION("a good the current stage does not need") {
        CHECK(deliver(content, state, kCopper, 100) == R::ProgramNotNeeded);
        // Not needed is said before the quantity is looked at.
        CHECK(deliver(content, state, kCopper, 0) == R::ProgramNotNeeded);
        CHECK(deliver(content, state, kLithium, 1) == R::ProgramNotNeeded);
    }
    SECTION("tonnes that are not positive") {
        CHECK(deliver(content, state, kIron, 0) == R::InvalidQuantity);
        CHECK(deliver(content, state, kIron, -5) == R::InvalidQuantity);
    }
    SECTION("more than is in stock") {
        CHECK_FALSE(deliver(content, state, kIron, 300));
        CHECK(deliver(content, state, kIron, 301) == R::InsufficientInventory);
        // Stock is checked before the remaining need.
        CHECK(deliver(content, state, kIron, 2'000) == R::InsufficientInventory);
    }
    SECTION("a program that is complete") {
        atStage(content, state.companies[0], 3);
        CHECK(deliver(content, state, kIron, 100) == R::ProgramComplete);
        // Said before anything about the good or the quantity.
        CHECK(deliver(content, state, kCopper, 0) == R::ProgramComplete);
    }
    SECTION("content with no program at all") {
        sim::Content bare = content;
        bare.program.stages.clear();
        CHECK(deliver(bare, state, kIron, 100) == R::ProgramComplete);
    }
}

TEST_CASE("What the program needs changes with the stage", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = freshState(content);
    state.companies[0].inventoryTonnes[kCopper.index()] = 300;
    CHECK(deliver(content, state, kCopper, 200) == R::ProgramNotNeeded);
    atStage(content, state.companies[0], 1);
    CHECK_FALSE(deliver(content, state, kCopper, 200));
    CHECK(deliver(content, state, kCopper, 201) == R::ProgramNotNeeded);
    // The rival is still on stage 1.
    state.companies[1].inventoryTonnes[kCopper.index()] = 300;
    CHECK(deliver(content, state, kCopper, 200, kRival) == R::ProgramNotNeeded);
}

TEST_CASE("Completing a stage charges the cost once, clears deliveries and advances", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    makeStageReady(content, state);
    state.companies[0].inventoryTonnes[kIron.index()] = 700;
    const sim::Money cost = content.program.stages[0].cost;
    const sim::Money cash = state.companies[0].cash;
    const sim::Money rivalCash = state.companies[1].cash;

    REQUIRE_FALSE(complete(content, state));
    CHECK(sim::programStageReady(content, state, kPlayer));
    sim::TickReport report;
    sim::applyCompleteProgramStage(content, state, {kPlayer}, report);

    CHECK(state.companies[0].cash == cash - cost);
    CHECK(state.companies[0].programStage == 1);
    CHECK(state.companies[0].programDelivered ==
          std::vector<std::int64_t>(content.resources.size(), 0));
    CHECK(state.companies[0].inventoryTonnes[kIron.index()] == 700);  // stock is untouched
    CHECK(state.companies[1].cash == rivalCash);
    CHECK(state.companies[1].programStage == 0);
    // A stage short of the last does not end the game.
    CHECK(state.outcome.ended == 0);
    CHECK_FALSE(sim::gameEnded(state));

    REQUIRE(report.notices.size() == 1);
    const sim::Notice& notice = report.notices[0];
    CHECK(notice.kind == sim::NoticeKind::ProgramStageComplete);
    CHECK(notice.company == kPlayer);
    CHECK(notice.amount == cost);
    CHECK(notice.tonnes == 1);  // the stage just completed, counting from one
    CHECK(notice.tech == content.program.stages[0].research);
    CHECK(sim::noticeVisibleTo(notice, kRival));
    CHECK(loadsBack(content, test::settledPace(content, state)));

    // The next stage starts from nothing: a repeat is refused and costs nothing.
    CHECK(complete(content, state) == R::ProgramStageNotReady);
    CHECK_FALSE(sim::programStageReady(content, state, kPlayer));
    CHECK(state.companies[0].cash == cash - cost);
}

TEST_CASE("A stage cannot be completed without research, deliveries and cash", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = freshState(content);
    atStage(content, state.companies[0], 1);  // 2,000 t iron, 200 t copper, 2M, tech 3
    makeStageReady(content, state);
    REQUIRE_FALSE(complete(content, state));
    REQUIRE(sim::programStageReady(content, state, kPlayer));

    SECTION("an unknown or bankrupt company") {
        CHECK(complete(content, state, sim::CompanyId()) == R::UnknownCompany);
        CHECK(complete(content, state, sim::CompanyId(2)) == R::UnknownCompany);
        state.companies[0].bankrupt = 1;
        CHECK(complete(content, state) == R::CompanyBankrupt);
    }
    SECTION("the stage's research is not done") {
        state.companies[0].completedTechs.clear();
        CHECK(complete(content, state) == R::ProgramStageNotReady);
        CHECK_FALSE(sim::programStageReady(content, state, kPlayer));
        // Another stage's project does not count.
        state.companies[0].completedTechs.push_back(content.program.stages[0].research);
        CHECK(complete(content, state) == R::ProgramStageNotReady);
    }
    SECTION("a delivery is short") {
        state.companies[0].programDelivered[kCopper.index()] = 199;
        CHECK(complete(content, state) == R::ProgramStageNotReady);
        CHECK_FALSE(sim::programStageReady(content, state, kPlayer));
        state.companies[0].programDelivered[kCopper.index()] = 200;
        state.companies[0].programDelivered[kIron.index()] = 0;
        CHECK(complete(content, state) == R::ProgramStageNotReady);
    }
    SECTION("the cost is more than the cash on hand") {
        const sim::Money cost = content.program.stages[1].cost;
        state.companies[0].cash = cost;
        CHECK_FALSE(complete(content, state));
        state.companies[0].cash = cost - sim::Money::fromCents(1);
        CHECK(complete(content, state) == R::InsufficientFunds);
        CHECK_FALSE(sim::programStageReady(content, state, kPlayer));
    }
    SECTION("cash promised to a bid is not available") {
        const sim::Money cost = content.program.stages[1].cost;
        state.companies[0].cash = cost;
        sim::Auction* open = nullptr;
        for (sim::Auction& auction : state.auctions) {
            if (auction.open != 0) {
                open = &auction;
                break;
            }
        }
        REQUIRE(open != nullptr);
        open->bids.push_back({kPlayer, sim::Money::fromUnits(1), 0, 0});
        CHECK(complete(content, state) == R::InsufficientFunds);
    }
    SECTION("the rival's readiness is its own") {
        CHECK(complete(content, state, kRival) == R::ProgramStageNotReady);
        CHECK_FALSE(sim::programStageReady(content, state, kRival));
    }
    SECTION("a program that is complete") {
        atStage(content, state.companies[0], 3);
        state.companies[0].programDelivered.assign(content.resources.size(), 0);
        CHECK(complete(content, state) == R::ProgramComplete);
        CHECK_FALSE(sim::programStageReady(content, state, kPlayer));
    }
    SECTION("an unknown company is never ready") {
        CHECK_FALSE(sim::programStageReady(content, state, sim::CompanyId()));
        CHECK_FALSE(sim::programStageReady(content, state, sim::CompanyId(2)));
    }
}

TEST_CASE("All three stages can be completed in turn", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    for (std::uint32_t stage = 0; stage < 3; ++stage) {
        CHECK_FALSE(sim::gameEnded(state));
        makeStageReady(content, state);
        REQUIRE_FALSE(complete(content, state));
        sim::TickReport report;
        sim::applyCompleteProgramStage(content, state, {kPlayer}, report);
        CHECK(state.companies[0].programStage == stage + 1);
        const std::vector<sim::Notice> completed = noticesOf(report, Kind::ProgramStageComplete);
        REQUIRE(completed.size() == 1);
        CHECK(completed[0].tonnes == stage + 1);
        CHECK(completed[0].amount == content.program.stages[stage].cost);
        // Only the last stage founds the program.
        CHECK(noticesOf(report, Kind::ProgramFounded).size() == (stage == 2 ? 1U : 0U));
    }
    CHECK(complete(content, state) == R::ProgramComplete);
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 1'000);
    // The last stage ends the game at once, in the rule itself.
    CHECK(sim::gameEnded(state));
    CHECK(state.outcome.day == kRaceDay);
    CHECK(state.outcome.founder == kPlayer);
    CHECK(state.outcome.results == std::vector<Result>{Result::Founded, Result::Lost});
    CHECK(loadsBack(content, state));
}

TEST_CASE("Progress is the mean share of each requirement delivered", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = freshState(content);
    sim::Company& company = state.companies[0];

    CHECK(sim::programProgressPermille(content, state, kPlayer) == 0);
    company.programDelivered[kIron.index()] = 250;
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 250);
    company.programDelivered[kIron.index()] = 1'000;
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 1'000);
    CHECK(sim::programProgressPermille(content, state, kRival) == 0);

    // Stage 2: each requirement counts by its own need, so 200 t of copper
    // is worth as much as 2,000 t of iron.
    atStage(content, company, 1);
    company.programDelivered.assign(content.resources.size(), 0);
    company.programDelivered[kIron.index()] = 1'000;
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 250);
    company.programDelivered[kCopper.index()] = 200;
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 750);
    company.programDelivered[kIron.index()] = 1'999;
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 999);  // rounds down
    company.programDelivered[kIron.index()] = 2'000;
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 1'000);

    // Progress is about goods only: research and cash do not move it.
    CHECK_FALSE(sim::programStageReady(content, state, kPlayer));

    atStage(content, company, 3);
    company.programDelivered.assign(content.resources.size(), 0);
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 1'000);
    CHECK(sim::programProgressPermille(content, state, sim::CompanyId()) == 0);
}

TEST_CASE("Progress stays exact for the largest need the loader allows", "[program]") {
    sim::Content content = test::makeContent();
    content.program.stages[0].requirements[0].tonnes = sim::kMaxStockTonnes;
    sim::WorldState state = freshState(content);
    state.companies[0].programDelivered[kIron.index()] = sim::kMaxStockTonnes / 2;
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 500);
    state.companies[0].programDelivered[kIron.index()] = sim::kMaxStockTonnes - 1;
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 999);
    state.companies[0].programDelivered[kIron.index()] = sim::kMaxStockTonnes;
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 1'000);
}

TEST_CASE("A stage with no goods to deliver counts as fully delivered", "[program]") {
    sim::Content content = test::makeContent();
    content.program.stages[0].requirements.clear();
    sim::WorldState state = freshState(content);
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 1'000);
    state.companies[0].completedTechs.push_back(content.program.stages[0].research);
    CHECK(sim::programStageReady(content, state, kPlayer));
}

TEST_CASE("The program's notices and reject reasons have names and the right audience",
          "[program]") {
    using K = sim::NoticeKind;
    CHECK(std::string_view(sim::noticeKindName(K::ProgramDelivered)) == "program_delivered");
    CHECK(std::string_view(sim::noticeKindName(K::ProgramStageComplete)) ==
          "program_stage_complete");
    CHECK_FALSE(sim::noticeIsPublic(K::ProgramDelivered));
    CHECK(sim::noticeIsPublic(K::ProgramStageComplete));
    CHECK(std::string_view(sim::rejectReasonName(R::ProgramNotNeeded)) == "program_not_needed");
    CHECK(std::string_view(sim::rejectReasonName(R::ProgramStageNotReady)) ==
          "program_stage_not_ready");
    CHECK(std::string_view(sim::rejectReasonName(R::ProgramComplete)) == "program_complete");
    // The ending: both notices are for everyone.
    CHECK(std::string_view(sim::noticeKindName(K::ProgramFounded)) == "program_founded");
    CHECK(std::string_view(sim::noticeKindName(K::DeadlinePassed)) == "deadline_passed");
    CHECK(sim::noticeIsPublic(K::ProgramFounded));
    CHECK(sim::noticeIsPublic(K::DeadlinePassed));
    CHECK(std::string_view(sim::rejectReasonName(R::GameEnded)) == "game_ended");
}

TEST_CASE("The race notices have names and are for their own company only", "[program][pace]") {
    using K = sim::NoticeKind;
    CHECK(std::string_view(sim::noticeKindName(K::ProgramFellBehind)) == "program_fell_behind");
    CHECK(std::string_view(sim::noticeKindName(K::ProgramPastDeadline)) == "program_past_deadline");
    for (const K kind : {K::ProgramFellBehind, K::ProgramPastDeadline}) {
        INFO(sim::noticeKindName(kind));
        CHECK_FALSE(sim::noticeIsPublic(kind));
        sim::Notice notice;
        notice.kind = kind;
        notice.company = kPlayer;
        notice.rival = kRival;
        CHECK(sim::noticeVisibleTo(notice, kPlayer));
        CHECK_FALSE(sim::noticeVisibleTo(notice, kRival));
    }
}

TEST_CASE("A notice carries no rival or founding days unless it is a race notice",
          "[program][pace]") {
    const sim::Notice plain;
    CHECK_FALSE(plain.rival.valid());
    CHECK_FALSE(plain.foundingDay.has_value());
    CHECK_FALSE(plain.rivalFoundingDay.has_value());
    const sim::Notice made = sim::makeNotice(sim::NoticeKind::ProgramDelivered, kPlayer,
                                             sim::PlotId(), sim::Money::fromUnits(5));
    CHECK_FALSE(made.rival.valid());
    CHECK_FALSE(made.foundingDay.has_value());
    CHECK_FALSE(made.rivalFoundingDay.has_value());
}

TEST_CASE("Completing the last stage founds the program and ends the game", "[program][ending]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    makeFinalStageReady(content, state);
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);
    REQUIRE_FALSE(sim::gameEnded(simulation.state()));

    simulation.submit(sim::CompleteProgramStage{kPlayer});
    const sim::TickReport report = simulation.tick();

    const sim::Outcome& outcome = simulation.state().outcome;
    CHECK(sim::gameEnded(simulation.state()));
    CHECK(outcome.ended == 1);
    CHECK(outcome.day == kRaceDay);  // the day the order was applied
    CHECK(outcome.founder == kPlayer);
    // The rival has completed nothing.
    CHECK(outcome.results == std::vector<Result>{Result::Founded, Result::Lost});
    // The world freezes on the day it ends: the day does not advance.
    CHECK(simulation.state().day == kRaceDay);
    CHECK(outcome.day == simulation.state().day);
    CHECK(simulation.state().companies[0].programStage == 3);

    const std::vector<sim::Notice> founded = noticesOf(report, Kind::ProgramFounded);
    REQUIRE(founded.size() == 1);
    CHECK(founded[0].company == kPlayer);
    CHECK(sim::noticeVisibleTo(founded[0], kRival));
    CHECK(noticesOf(report, Kind::ProgramStageComplete).size() == 1);
    CHECK(noticesOf(report, Kind::DeadlinePassed).empty());
}

TEST_CASE("Nothing runs after the order that founds the program", "[program][ending]") {
    // A month end: without the freeze, finance would close the month on the
    // day the program is founded and declare the rival bankrupt after its
    // result had been fixed.
    const sim::Content content = test::makeContent();
    constexpr sim::Day kMonthEnd = sim::kDaysPerMonth * 2;
    sim::WorldState state = racingState(content, kMonthEnd);
    makeFinalStageReady(content, state);
    atStage(content, state.companies[1], 2);
    state.companies[1].cash = sim::Money::fromUnits(-1);
    state.companies[1].monthsInsolvent = content.balance.bankruptcyGraceMonths;
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);
    const sim::WorldState before = simulation.state();

    simulation.submit(sim::CompleteProgramStage{kPlayer});
    const sim::TickReport report = simulation.tick();

    const sim::WorldState& ended = simulation.state();
    REQUIRE(sim::gameEnded(ended));
    CHECK(ended.day == kMonthEnd);
    CHECK(ended.outcome.day == ended.day);
    CHECK(ended.outcome.results == std::vector<Result>{Result::Founded, Result::LesserPartner});
    CHECK(ended.companies[1].bankrupt == 0);
    CHECK(ended.companies[1].monthsInsolvent == before.companies[1].monthsInsolvent);
    CHECK(noticesOf(report, Kind::Bankrupt).empty());
    for (std::size_t r = 0; r < before.markets.size(); ++r) {
        CHECK(ended.markets[r].priceHistory.size() == before.markets[r].priceHistory.size());
    }
    CHECK(sim::validateWorld(content, ended));
}

TEST_CASE("A game that ends on a day with other orders applied still saves and loads",
          "[program][ending][save]") {
    // The day never closed, so the mine and the research ordered on it carry
    // today's date: a date no world still being played can hold.
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = boughtPlot(simulation);
    sim::WorldState state = simulation.state();
    makeFinalStageReady(content, state);
    state.companies[0].cash = sim::Money::fromUnits(50'000'000);
    state.companies[0].inventoryTonnes[kIron.index()] = 100;
    play(simulation, content, state);

    // Every order that leaves a dated or daily record behind it: a mine, a
    // project, a sale (the market's tonnes sold today), a loan, a survey
    // and a bid.
    sim::PlotId listed;
    for (std::uint32_t i = 0; i < simulation.state().plots.size() && !listed.valid(); ++i) {
        if (simulation.state().plots[i].listing == sim::PlotListing::Listed) {
            listed = sim::PlotId(i);
        }
    }
    REQUIRE(listed.valid());
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(0)});
    simulation.submit(sim::SellOre{kPlayer, kIron, 25});
    simulation.submit(sim::TakeLoan{kPlayer, sim::Money::fromUnits(500'000)});
    simulation.submit(sim::CommissionSurvey{kPlayer, listed});
    simulation.submit(
        sim::BidOnPlot{kPlayer, listed, simulation.state().plots[listed.index()].askPrice});
    simulation.submit(sim::CompleteProgramStage{kPlayer});
    const sim::TickReport report = simulation.tick();
    CHECK(simulation.state().markets[kIron.index()].soldToday == 25);

    const sim::WorldState& ended = simulation.state();
    REQUIRE(sim::gameEnded(ended));
    REQUIRE(noticesOf(report, Kind::CommandRejected).empty());
    REQUIRE(ended.mines.size() == 1);
    CHECK(ended.mines[0].builtDay == ended.day);
    REQUIRE(ended.research.size() == 1);
    CHECK(ended.research[0].startedDay == ended.day);
    CHECK(sim::validateWorld(content, ended));
    sim::Simulation loaded(content, 2, test::setup());
    REQUIRE(loaded.load(simulation.save()));
    CHECK(loaded.stateHash() == simulation.stateHash());

    // The same dates in a world still being played are refused as before.
    sim::WorldState running = ended;
    running.outcome = sim::Outcome{};
    running.outcome.results.assign(running.companies.size(), Result::None);
    atStage(content, running.companies[0], 2);
    CHECK_FALSE(sim::validateWorld(content, running));
    // Each of them on its own: the tonnes sold today, the mine, the project.
    sim::WorldState sold = simulation.state();
    sold.outcome = running.outcome;
    atStage(content, sold.companies[0], 2);
    sold.mines.clear();
    sold.plots[plot.index()].mine = sim::MineId();
    sold.research.clear();
    CHECK_FALSE(sim::validateWorld(content, sold));
    for (auto& market : sold.markets) {
        market.soldToday = 0;
    }
    CHECK(sim::validateWorld(content, sold));
}

TEST_CASE("When the rival founds the program the player is a lesser partner or has lost",
          "[program][ending]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    makeFinalStageReady(content, state, kRival);
    Result expected = Result::Lost;
    SECTION("two stages completed: a lesser partner") {
        atStage(content, state.companies[0], 2);
        expected = Result::LesserPartner;
    }
    SECTION("one stage completed: lost") {
        atStage(content, state.companies[0], 1);
    }
    SECTION("no stage completed: lost") {
        atStage(content, state.companies[0], 0);
    }
    SECTION("two stages completed but bankrupt: lost") {
        atStage(content, state.companies[0], 2);
        makeBankrupt(content, state, kPlayer);
    }
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);

    simulation.submit(sim::CompleteProgramStage{kRival});
    const sim::TickReport report = simulation.tick();

    const sim::Outcome& outcome = simulation.state().outcome;
    REQUIRE(sim::gameEnded(simulation.state()));
    CHECK(outcome.founder == kRival);
    CHECK(outcome.day == kRaceDay);
    CHECK(outcome.results == std::vector<Result>{expected, Result::Founded});
    const std::vector<sim::Notice> founded = noticesOf(report, Kind::ProgramFounded);
    REQUIRE(founded.size() == 1);
    CHECK(founded[0].company == kRival);
    CHECK(sim::noticeVisibleTo(founded[0], kPlayer));
}

TEST_CASE("The deadline ends the game with everyone lost when nobody has founded",
          "[program][ending]") {
    sim::Content content = test::makeContent();
    content.program.deadlineDay = kRaceDay + 5;
    sim::WorldState state = racingState(content);
    // Two stages count for nothing when nobody founds the program.
    atStage(content, state.companies[0], 2);
    atStage(content, state.companies[1], 2);
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);

    // Every day before the deadline day is an ordinary one.
    while (simulation.state().day < content.program.deadlineDay) {
        const sim::TickReport report = simulation.tick();
        REQUIRE_FALSE(sim::gameEnded(simulation.state()));
        REQUIRE(noticesOf(report, Kind::DeadlinePassed).empty());
    }
    // The deadline day takes its orders and then ends the game: nothing
    // else of that day is played and the day does not advance.
    const sim::WorldState before = simulation.state();
    const sim::TickReport report = simulation.tick();

    const sim::Outcome& outcome = simulation.state().outcome;
    REQUIRE(sim::gameEnded(simulation.state()));
    CHECK(outcome.day == content.program.deadlineDay);
    CHECK_FALSE(outcome.founder.valid());
    CHECK(outcome.results == std::vector<Result>{Result::Lost, Result::Lost});
    CHECK(simulation.state().day == content.program.deadlineDay);
    CHECK(outcome.day == simulation.state().day);
    for (std::size_t r = 0; r < before.markets.size(); ++r) {
        CHECK(simulation.state().markets[r].priceHistory.size() ==
              before.markets[r].priceHistory.size());
        CHECK(simulation.state().markets[r].price == before.markets[r].price);
    }
    CHECK(sim::validateWorld(content, simulation.state()));
    const std::vector<sim::Notice> passed = noticesOf(report, Kind::DeadlinePassed);
    REQUIRE(passed.size() == 1);
    CHECK_FALSE(passed[0].company.valid());
    CHECK(sim::noticeVisibleTo(passed[0], kPlayer));
    CHECK(sim::noticeVisibleTo(passed[0], kRival));
    CHECK(noticesOf(report, Kind::ProgramFounded).empty());
}

TEST_CASE("runProgram ends the game only once the deadline day has come", "[program][ending]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = freshState(content);
    sim::TickReport report;

    state.day = content.program.deadlineDay - 1;
    sim::runProgram(content, state, report);
    CHECK_FALSE(sim::gameEnded(state));
    CHECK(report.notices.empty());

    // A day skipped past the deadline still ends it.
    state.day = content.program.deadlineDay + 3;
    sim::runProgram(content, state, report);
    CHECK(sim::gameEnded(state));
    CHECK(state.outcome.day == content.program.deadlineDay + 3);
    CHECK(state.outcome.results == std::vector<Result>{Result::Lost, Result::Lost});
    REQUIRE(report.notices.size() == 1);
    CHECK(report.notices[0].kind == Kind::DeadlinePassed);

    // The outcome is set once: a second call changes nothing.
    const sim::Outcome first = state.outcome;
    ++state.day;
    sim::runProgram(content, state, report);
    CHECK(state.outcome.day == first.day);
    CHECK(state.outcome.results == first.results);
    CHECK(report.notices.size() == 1);
}

TEST_CASE("A bankrupt rival does not end the game for the player", "[program][ending]") {
    sim::Content content = test::makeContent();
    content.program.deadlineDay = kRaceDay + 5;
    sim::WorldState state = racingState(content);
    atStage(content, state.companies[1], 2);
    makeBankrupt(content, state, kRival);

    SECTION("the survivor can still found the program") {
        makeFinalStageReady(content, state);
        sim::Simulation simulation(content, 1, test::setup());
        play(simulation, content, state);
        simulation.tick();
        REQUIRE_FALSE(sim::gameEnded(simulation.state()));

        simulation.submit(sim::CompleteProgramStage{kPlayer});
        simulation.tick();
        const sim::Outcome& outcome = simulation.state().outcome;
        REQUIRE(sim::gameEnded(simulation.state()));
        CHECK(outcome.founder == kPlayer);
        CHECK(outcome.day == kRaceDay + 1);
        // Bankrupt is lost, whatever it had completed.
        CHECK(outcome.results == std::vector<Result>{Result::Founded, Result::Lost});
    }
    SECTION("the survivor still races the deadline") {
        sim::Simulation simulation(content, 1, test::setup());
        play(simulation, content, state);
        while (simulation.state().day < content.program.deadlineDay) {
            simulation.tick();
            REQUIRE_FALSE(sim::gameEnded(simulation.state()));
        }
        simulation.tick();
        const sim::Outcome& outcome = simulation.state().outcome;
        REQUIRE(sim::gameEnded(simulation.state()));
        CHECK_FALSE(outcome.founder.valid());
        CHECK(outcome.results == std::vector<Result>{Result::Lost, Result::Lost});
    }
    SECTION("the bankrupt company cannot found it") {
        makeFinalStageReady(content, state, kRival);
        makeBankrupt(content, state, kRival);
        sim::Simulation simulation(content, 1, test::setup());
        play(simulation, content, state);
        simulation.submit(sim::CompleteProgramStage{kRival});
        const sim::TickReport report = simulation.tick();
        CHECK_FALSE(sim::gameEnded(simulation.state()));
        const std::vector<sim::Notice> rejected = noticesOf(report, Kind::CommandRejected);
        REQUIRE(rejected.size() == 1);
        CHECK(rejected[0].reason == R::CompanyBankrupt);
    }
}

TEST_CASE("A company going bankrupt during play leaves the other racing", "[program][ending]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    state.companies[1].cash = sim::Money::fromUnits(-1);
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);

    bool declared = false;
    for (int day = 0; day < 200 && !declared; ++day) {
        declared = !noticesOf(simulation.tick(), Kind::Bankrupt).empty();
    }
    REQUIRE(declared);
    REQUIRE(simulation.state().companies[1].bankrupt == 1);
    CHECK_FALSE(sim::gameEnded(simulation.state()));
    const sim::Day day = simulation.state().day;
    simulation.tick();
    CHECK(simulation.state().day == day + 1);
}

TEST_CASE("When both finish on the same day the first order applied founds the program",
          "[program][ending]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    makeFinalStageReady(content, state, kPlayer);
    makeFinalStageReady(content, state, kRival);
    const sim::Money rivalCash = state.companies[1].cash;
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);

    // Orders are applied in company order, whichever was submitted first.
    simulation.submit(sim::CompleteProgramStage{kRival});
    simulation.submit(sim::CompleteProgramStage{kPlayer});
    const sim::TickReport report = simulation.tick();

    const sim::Outcome& outcome = simulation.state().outcome;
    REQUIRE(sim::gameEnded(simulation.state()));
    CHECK(outcome.founder == kPlayer);
    // The rival had two stages behind it when the program was founded.
    CHECK(outcome.results == std::vector<Result>{Result::Founded, Result::LesserPartner});
    CHECK(noticesOf(report, Kind::ProgramFounded).size() == 1);
    CHECK(noticesOf(report, Kind::ProgramStageComplete).size() == 1);

    const std::vector<sim::Notice> rejected = noticesOf(report, Kind::CommandRejected);
    REQUIRE(rejected.size() == 1);
    CHECK(rejected[0].company == kRival);
    CHECK(rejected[0].reason == R::GameEnded);
    CHECK_FALSE(sim::noticeVisibleTo(rejected[0], kPlayer));
    // The rival's order did nothing: no stage, no charge.
    CHECK(simulation.state().companies[1].programStage == 2);
    CHECK(simulation.state().companies[1].cash == rivalCash);
}

TEST_CASE("A last stage completed on the deadline day founds the program", "[program][ending]") {
    sim::Content content = test::makeContent();
    content.program.deadlineDay = kRaceDay;
    sim::WorldState state = racingState(content);
    REQUIRE(state.day == content.program.deadlineDay);
    makeFinalStageReady(content, state);
    atStage(content, state.companies[1], 2);
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);

    simulation.submit(sim::CompleteProgramStage{kPlayer});
    const sim::TickReport report = simulation.tick();

    // Orders are applied before the deadline is looked at.
    const sim::Outcome& outcome = simulation.state().outcome;
    REQUIRE(sim::gameEnded(simulation.state()));
    CHECK(outcome.founder == kPlayer);
    CHECK(outcome.day == content.program.deadlineDay);
    CHECK(outcome.results == std::vector<Result>{Result::Founded, Result::LesserPartner});
    CHECK(noticesOf(report, Kind::ProgramFounded).size() == 1);
    CHECK(noticesOf(report, Kind::DeadlinePassed).empty());
}

TEST_CASE("A finished world does not change and refuses every order", "[program][ending]") {
    sim::Content content = test::makeContent();
    content.program.deadlineDay = kRaceDay + 2;
    sim::WorldState state = racingState(content);
    state.companies[0].inventoryTonnes[kIron.index()] = 500;
    bool founded = false;
    SECTION("after a founding") {
        makeFinalStageReady(content, state);
        founded = true;
    }
    SECTION("after the deadline") {}
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);
    if (founded) {
        simulation.submit(sim::CompleteProgramStage{kPlayer});
    }
    while (!sim::gameEnded(simulation.state())) {
        simulation.tick();
    }
    const sim::WorldState ended = simulation.state();
    const std::uint64_t hash = simulation.stateHash();

    // A tick with no orders: no day, no market movement, no notices.
    for (int i = 0; i < 40; ++i) {
        const sim::TickReport report = simulation.tick();
        CHECK(report.notices.empty());
        CHECK(report.day == ended.day);
    }
    CHECK(simulation.stateHash() == hash);
    CHECK(simulation.state().day == ended.day);
    CHECK(simulation.state().outcome.day == ended.outcome.day);
    CHECK(simulation.state().outcome.results == ended.outcome.results);
    for (std::size_t r = 0; r < ended.markets.size(); ++r) {
        CHECK(simulation.state().markets[r].price == ended.markets[r].price);
    }

    // Every order is refused, with the one reason, and changes nothing.
    const std::vector<sim::Command> orders = everyOrder();
    REQUIRE(orders.size() == std::variant_size_v<sim::Command>);
    for (const sim::Command& order : orders) {
        simulation.submit(order);
    }
    const sim::TickReport report = simulation.tick();
    REQUIRE(report.notices.size() == orders.size());
    for (const sim::Notice& notice : report.notices) {
        CHECK(notice.kind == Kind::CommandRejected);
        CHECK(notice.company == kPlayer);
        CHECK(notice.reason == R::GameEnded);
    }
    CHECK(simulation.stateHash() == hash);
    // The refused orders are gone: the next tick is silent again.
    CHECK(simulation.tick().notices.empty());
    CHECK(simulation.stateHash() == hash);
}

TEST_CASE("Hosts that keep ticking a finished world with the AI deciding do no harm",
          "[program][ending]") {
    // What the session and the runner do until they learn to stop: collect
    // the rivals' orders and tick, every day.
    sim::Content content = test::makeContent();
    content.program.deadlineDay = 400;
    sim::Simulation simulation(content, 1, test::setup());
    sim::HeuristicController ai(content);
    const auto playDay = [&] {
        for (std::uint32_t c = 0; c < 2; ++c) {
            simulation.collectCommands(sim::CompanyId(c), ai);
        }
        return simulation.tick();
    };
    while (!sim::gameEnded(simulation.state())) {
        playDay();
    }
    const std::uint64_t hash = simulation.stateHash();
    const sim::Day day = simulation.state().day;
    for (int i = 0; i < 90; ++i) {
        const sim::TickReport report = playDay();
        for (const sim::Notice& notice : report.notices) {
            REQUIRE(notice.kind == Kind::CommandRejected);
            REQUIRE(notice.reason == R::GameEnded);
        }
        REQUIRE(report.day == day);
    }
    CHECK(simulation.stateHash() == hash);
    CHECK(simulation.state().day == day);
    CHECK(simulation.view(kPlayer).day == day);
}

TEST_CASE("A save of an ended game loads and stays ended", "[program][ending]") {
    sim::Content content = test::makeContent();
    content.program.deadlineDay = kRaceDay + 2;
    sim::WorldState state = racingState(content);
    bool founded = false;
    SECTION("a founded program") {
        makeFinalStageReady(content, state, kRival);
        atStage(content, state.companies[0], 2);
        founded = true;
    }
    SECTION("a deadline passed") {}
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);
    if (founded) {
        simulation.submit(sim::CompleteProgramStage{kRival});
    }
    while (!sim::gameEnded(simulation.state())) {
        simulation.tick();
    }
    const std::vector<std::uint8_t> bytes = simulation.save();

    sim::Simulation loaded(content, 2, test::setup());
    REQUIRE(loaded.load(bytes));
    CHECK(loaded.stateHash() == simulation.stateHash());
    CHECK(sim::gameEnded(loaded.state()));
    CHECK(loaded.state().outcome.day == simulation.state().outcome.day);
    CHECK(loaded.state().outcome.founder == simulation.state().outcome.founder);
    CHECK(loaded.state().outcome.results == simulation.state().outcome.results);
    if (founded) {
        CHECK(loaded.state().outcome.results ==
              std::vector<Result>{Result::LesserPartner, Result::Founded});
    }
    // Still finished after the load.
    loaded.submit(sim::CompleteProgramStage{kPlayer});
    const sim::TickReport report = loaded.tick();
    REQUIRE(report.notices.size() == 1);
    CHECK(report.notices[0].reason == R::GameEnded);
    CHECK(loaded.stateHash() == simulation.stateHash());
}

// The race in the view (spec 010, BR-007). Everything about it is public, so
// each figure is checked for both companies against the rules themselves.

namespace {

using StageResearch = sim::ProgramResearchState;

// What the rules say of a company's research into the stage it is on.
StageResearch researchStateByRule(const sim::Content& content, const sim::WorldState& state,
                                  sim::CompanyId id) {
    const sim::Company& company = state.companies[id.index()];
    if (company.programStage >= content.program.stages.size()) {
        return StageResearch::Done;
    }
    const sim::TechId tech = content.program.stages[company.programStage].research;
    for (const sim::TechId done : company.completedTechs) {
        if (done == tech) {
            return StageResearch::Done;
        }
    }
    for (const sim::Research& research : state.research) {
        if (research.company == id && research.tech == tech && research.complete == 0) {
            return StageResearch::InProgress;
        }
    }
    return StageResearch::NotStarted;
}

// The finish day of the company's current stage project while it is under
// way, from the state.
std::optional<sim::Day> stageResearchDueDay(const sim::Content& content,
                                            const sim::WorldState& state, sim::CompanyId id) {
    const sim::Company& company = state.companies[id.index()];
    if (company.programStage >= content.program.stages.size()) {
        return std::nullopt;
    }
    const sim::TechId tech = content.program.stages[company.programStage].research;
    for (const sim::Research& research : state.research) {
        if (research.company == id && research.tech == tech && research.complete == 0) {
            return research.completesDay;
        }
    }
    return std::nullopt;
}

// The viewer's own program, figure by figure, against the rules and the state.
void checkOwnProgram(const sim::Content& content, const sim::WorldState& state,
                     const sim::CompanyView& view) {
    const sim::Company& company = state.companies[view.me.index()];
    const sim::ProgramView& program = view.program;
    CHECK(program.stagesCompleted == company.programStage);
    const bool complete = company.programStage >= content.program.stages.size();
    CHECK(program.complete == complete);
    REQUIRE(program.stage.has_value() == !complete);
    if (complete) {
        return;
    }
    const sim::ProgramStage& rule = content.program.stages[company.programStage];
    const sim::ProgramStageView& stage = *program.stage;
    CHECK(stage.index == company.programStage);
    CHECK(stage.cost == rule.cost);
    CHECK(stage.research == rule.research);
    CHECK(stage.researchState == researchStateByRule(content, state, view.me));
    CHECK(stage.activeResearch.has_value() == (stage.researchState == StageResearch::InProgress));
    CHECK(stage.progressPermille == sim::programProgressPermille(content, state, view.me));
    CHECK(stage.ready == sim::programStageReady(content, state, view.me));
    REQUIRE(stage.requirements.size() == rule.requirements.size());
    for (std::size_t i = 0; i < rule.requirements.size(); ++i) {
        const std::size_t r = rule.requirements[i].resource.index();
        CHECK(stage.requirements[i].resource == rule.requirements[i].resource);
        CHECK(stage.requirements[i].neededTonnes == rule.requirements[i].tonnes);
        CHECK(stage.requirements[i].deliveredTonnes == company.programDelivered[r]);
        CHECK(stage.requirements[i].stockTonnes == company.inventoryTonnes[r]);
    }
}

// Everything one company's view says about the race, against the rules.
void checkRaceView(const sim::Content& content, const sim::Simulation& simulation,
                   sim::CompanyId viewer) {
    const sim::WorldState& state = simulation.state();
    const sim::CompanyView view = simulation.view(viewer);
    CHECK(view.deadlineDay == content.program.deadlineDay);
    CHECK(view.outcome.ended == sim::gameEnded(state));
    CHECK(view.outcome.day == state.outcome.day);
    CHECK(view.outcome.founder == state.outcome.founder);
    CHECK(view.outcome.results == state.outcome.results);
    checkOwnProgram(content, state, view);
    CHECK(view.program.stageDays == state.companies[viewer.index()].programStageDays);
    REQUIRE(view.programs.size() == state.companies.size());
    for (std::uint32_t c = 0; c < state.companies.size(); ++c) {
        const sim::CompanyId id(c);
        const sim::ProgramPublicView& entry = view.programs[c];
        CHECK(entry.company == id);
        CHECK(entry.stagesCompleted == state.companies[c].programStage);
        CHECK(entry.stageDays == state.companies[c].programStageDays);
        CHECK(entry.complete == (state.companies[c].programStage >= content.program.stages.size()));
        CHECK(entry.progressPermille == sim::programProgressPermille(content, state, id));
        CHECK(entry.researchState == researchStateByRule(content, state, id));
        CHECK(entry.earliestFoundingDay == sim::earliestFoundingDay(content, state, id));
        CHECK(entry.researchDueDay == stageResearchDueDay(content, state, id));
        CHECK(entry.pastDeadline == sim::foundsPastDeadline(content, state, id));
    }
}

void checkBothRaceViews(const sim::Content& content, const sim::Simulation& simulation) {
    checkRaceView(content, simulation, kPlayer);
    checkRaceView(content, simulation, kRival);
}

// A research record as StartResearch would have left it the day before.
void startStageResearch(const sim::Content& content, sim::WorldState& state, sim::CompanyId id) {
    const sim::TechId tech =
        content.program.stages[state.companies[id.index()].programStage].research;
    sim::Research research;
    research.company = id;
    research.tech = tech;
    research.startedDay = state.day - 1;
    research.completesDay = research.startedDay + content.techs[tech.index()].durationDays;
    state.research.push_back(research);
}

}  // namespace

TEST_CASE("A new game's view shows the deadline, no outcome and both programs unstarted",
          "[program][view]") {
    const sim::Content content = test::makeContent();
    const sim::Simulation simulation(content, 1, test::setup());
    checkBothRaceViews(content, simulation);

    for (const sim::CompanyId viewer : {kPlayer, kRival}) {
        const sim::CompanyView view = simulation.view(viewer);
        CHECK(view.deadlineDay == 10'800);
        CHECK_FALSE(view.outcome.ended);
        CHECK_FALSE(view.outcome.founder.valid());
        CHECK(view.program.stagesCompleted == 0);
        CHECK_FALSE(view.program.complete);
        REQUIRE(view.program.stage.has_value());
        const sim::ProgramStageView& stage = *view.program.stage;
        CHECK(stage.index == 0);
        CHECK(stage.cost == sim::Money::fromUnits(1'000'000));
        CHECK(stage.research == sim::TechId(2));
        CHECK(stage.researchState == StageResearch::NotStarted);
        CHECK_FALSE(stage.activeResearch.has_value());
        CHECK(stage.progressPermille == 0);
        CHECK_FALSE(stage.ready);
        REQUIRE(stage.requirements.size() == 1);
        CHECK(stage.requirements[0].resource == kIron);
        CHECK(stage.requirements[0].neededTonnes == 1'000);
        CHECK(stage.requirements[0].deliveredTonnes == 0);
        REQUIRE(view.programs.size() == 2);
        CHECK(view.programs[0].company == kPlayer);
        CHECK(view.programs[1].company == kRival);
        CHECK(view.programs[1].stagesCompleted == 0);
        CHECK(view.programs[1].progressPermille == 0);
        CHECK(view.programs[1].researchState == StageResearch::NotStarted);
    }
}

TEST_CASE("The view shows a delivery in the company's stage and in the rival's view of it",
          "[program][view]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    state.companies[0].inventoryTonnes[kIron.index()] = 5'000;
    state.companies[1].inventoryTonnes[kIron.index()] = 700;
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);

    checkBothRaceViews(content, simulation);
    CHECK(simulation.view(kPlayer).program.stage->requirements[0].stockTonnes == 5'000);
    CHECK(simulation.view(kRival).program.stage->requirements[0].stockTonnes == 700);
    CHECK(simulation.view(kPlayer).programs[1].progressPermille == 0);

    simulation.submit(sim::DeliverToProgram{kPlayer, kIron, 400});
    simulation.submit(sim::DeliverToProgram{kRival, kIron, 250});
    simulation.tick();

    checkBothRaceViews(content, simulation);
    const sim::CompanyView player = simulation.view(kPlayer);
    const sim::CompanyView rival = simulation.view(kRival);
    CHECK(player.program.stage->requirements[0].deliveredTonnes == 400);
    CHECK(player.program.stage->requirements[0].stockTonnes == 4'600);
    CHECK(player.program.stage->progressPermille == 400);
    CHECK_FALSE(player.program.stage->ready);
    CHECK(rival.program.stage->requirements[0].deliveredTonnes == 250);
    CHECK(rival.program.stage->requirements[0].stockTonnes == 450);
    CHECK(rival.program.stage->progressPermille == 250);
    // Each sees the other's progress.
    CHECK(player.programs[0].progressPermille == 400);
    CHECK(player.programs[1].progressPermille == 250);
    CHECK(rival.programs[0].progressPermille == 400);
    CHECK(rival.programs[1].progressPermille == 250);
}

TEST_CASE("The view follows a stage's research from started to done, then the stage completing",
          "[program][view]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    state.companies[0].programDelivered[kIron.index()] = 1'000;
    state.companies[0].cash = sim::Money::fromUnits(50'000'000);
    startStageResearch(content, state, kPlayer);
    const sim::Day finishDay = state.research.back().completesDay;
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);

    // In progress: the player sees the finish day, the rival sees the state.
    checkBothRaceViews(content, simulation);
    sim::CompanyView player = simulation.view(kPlayer);
    CHECK(player.program.stage->researchState == StageResearch::InProgress);
    REQUIRE(player.program.stage->activeResearch.has_value());
    CHECK(player.program.stage->activeResearch->tech == sim::TechId(2));
    CHECK(player.program.stage->activeResearch->startedDay == kRaceDay - 1);
    CHECK(player.program.stage->activeResearch->completesDay == finishDay);
    CHECK(player.program.stage->progressPermille == 1'000);
    CHECK_FALSE(player.program.stage->ready);
    CHECK(player.programs[1].researchState == StageResearch::NotStarted);
    sim::CompanyView rival = simulation.view(kRival);
    CHECK(rival.programs[0].researchState == StageResearch::InProgress);
    CHECK(rival.programs[0].progressPermille == 1'000);
    CHECK(rival.program.stage->researchState == StageResearch::NotStarted);

    // Done: the stage is ready, as the rule says.
    while (simulation.state().day <= finishDay) {
        simulation.tick();
    }
    checkBothRaceViews(content, simulation);
    player = simulation.view(kPlayer);
    CHECK(player.program.stage->researchState == StageResearch::Done);
    CHECK_FALSE(player.program.stage->activeResearch.has_value());
    CHECK(player.program.stage->ready);
    CHECK(simulation.view(kRival).programs[0].researchState == StageResearch::Done);
    CHECK_FALSE(simulation.view(kRival).program.stage->ready);

    // Completed: the next stage, with nothing delivered or researched.
    simulation.submit(sim::CompleteProgramStage{kPlayer});
    simulation.tick();
    checkBothRaceViews(content, simulation);
    player = simulation.view(kPlayer);
    CHECK(player.program.stagesCompleted == 1);
    REQUIRE(player.program.stage.has_value());
    CHECK(player.program.stage->index == 1);
    CHECK(player.program.stage->cost == sim::Money::fromUnits(2'000'000));
    CHECK(player.program.stage->research == sim::TechId(3));
    CHECK(player.program.stage->researchState == StageResearch::NotStarted);
    REQUIRE(player.program.stage->requirements.size() == 2);
    CHECK(player.program.stage->requirements[0].neededTonnes == 2'000);
    CHECK(player.program.stage->requirements[0].deliveredTonnes == 0);
    CHECK(player.program.stage->requirements[1].resource == kCopper);
    CHECK(player.program.stage->requirements[1].neededTonnes == 200);
    CHECK(player.program.stage->progressPermille == 0);
    CHECK_FALSE(player.program.stage->ready);
    rival = simulation.view(kRival);
    CHECK(rival.programs[0].stagesCompleted == 1);
    CHECK(rival.programs[0].progressPermille == 0);
    CHECK(rival.programs[0].researchState == StageResearch::NotStarted);
    CHECK(rival.programs[1].stagesCompleted == 0);
}

TEST_CASE("The view shows how the game ended to both companies", "[program][view][ending]") {
    sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    sim::Simulation simulation(content, 1, test::setup());

    SECTION("the rival founds the program") {
        makeFinalStageReady(content, state, kRival);
        atStage(content, state.companies[0], 2);
        play(simulation, content, state);
        simulation.submit(sim::CompleteProgramStage{kRival});
        simulation.tick();

        checkBothRaceViews(content, simulation);
        for (const sim::CompanyId viewer : {kPlayer, kRival}) {
            const sim::CompanyView view = simulation.view(viewer);
            CHECK(view.outcome.ended);
            CHECK(view.outcome.day == kRaceDay);
            CHECK(view.outcome.founder == kRival);
            CHECK(view.outcome.results ==
                  std::vector<Result>{Result::LesserPartner, Result::Founded});
            CHECK(view.programs[0].stagesCompleted == 2);
            CHECK_FALSE(view.programs[0].complete);
            CHECK(view.programs[1].stagesCompleted == 3);
            CHECK(view.programs[1].complete);
            CHECK(view.programs[1].progressPermille == 1'000);
            CHECK(view.programs[1].researchState == StageResearch::Done);
        }
        const sim::CompanyView founder = simulation.view(kRival);
        CHECK(founder.program.complete);
        CHECK(founder.program.stagesCompleted == 3);
        CHECK_FALSE(founder.program.stage.has_value());
        const sim::CompanyView partner = simulation.view(kPlayer);
        CHECK_FALSE(partner.program.complete);
        REQUIRE(partner.program.stage.has_value());
        CHECK(partner.program.stage->index == 2);
        CHECK_FALSE(partner.program.stage->ready);
    }
    SECTION("the deadline passes") {
        content.program.deadlineDay = kRaceDay + 2;
        sim::Simulation shortGame(content, 1, test::setup());
        play(shortGame, content, state);
        while (!sim::gameEnded(shortGame.state())) {
            shortGame.tick();
        }
        checkBothRaceViews(content, shortGame);
        for (const sim::CompanyId viewer : {kPlayer, kRival}) {
            const sim::CompanyView view = shortGame.view(viewer);
            CHECK(view.deadlineDay == kRaceDay + 2);
            CHECK(view.outcome.ended);
            CHECK_FALSE(view.outcome.founder.valid());
            CHECK(view.outcome.results == std::vector<Result>{Result::Lost, Result::Lost});
        }
    }
}

// The day each stage was completed (spec 010): the end screen shows it for
// every company, so the sim keeps it rather than leaving the client to find
// it in a log of notices that is capped and does not survive a load.

namespace {

// Gives the company what its current stage asks for and has it complete the
// stage with an order applied on the simulation's current day, which is
// returned. The simulation is a day further on afterwards.
sim::Day completeStageToday(sim::Simulation& simulation, const sim::Content& content,
                            sim::CompanyId id) {
    sim::WorldState state = simulation.state();
    makeStageReady(content, state, id);
    play(simulation, content, state);
    const sim::Day day = simulation.state().day;
    simulation.submit(sim::CompleteProgramStage{id});
    simulation.tick();
    return day;
}

// A game in which the player has completed two stages and the rival one, on
// days apart from one another.
void playTwoStagesAndOne(sim::Simulation& simulation, const sim::Content& content) {
    simulation.tick(kRaceDay);
    completeStageToday(simulation, content, kPlayer);
    simulation.tick(5);
    completeStageToday(simulation, content, kRival);
    simulation.tick(3);
    completeStageToday(simulation, content, kPlayer);
}

}  // namespace

TEST_CASE("A new company has completed no stage on any day", "[program][stage-days]") {
    const sim::Content content = test::makeContent();
    const sim::Simulation simulation(content, 1, test::setup());
    for (const sim::Company& company : simulation.state().companies) {
        CHECK(company.programStageDays.empty());
    }
    for (const sim::CompanyId viewer : {kPlayer, kRival}) {
        const sim::CompanyView view = simulation.view(viewer);
        CHECK(view.program.stageDays.empty());
        CHECK(view.programs[0].stageDays.empty());
        CHECK(view.programs[1].stageDays.empty());
    }
}

TEST_CASE("Each company keeps the day it completed each stage", "[program][stage-days]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(kRaceDay);

    // The player completes all three stages and the rival two, each on a day
    // of its own; the player's third founds the program.
    std::vector<sim::Day> player;
    std::vector<sim::Day> rival;
    player.push_back(completeStageToday(simulation, content, kPlayer));
    simulation.tick(5);
    rival.push_back(completeStageToday(simulation, content, kRival));
    CHECK(simulation.state().companies[0].programStageDays == player);
    CHECK(simulation.state().companies[1].programStageDays == rival);
    simulation.tick(3);
    player.push_back(completeStageToday(simulation, content, kPlayer));
    rival.push_back(completeStageToday(simulation, content, kRival));
    simulation.tick(7);
    player.push_back(completeStageToday(simulation, content, kPlayer));

    CHECK(player == std::vector<sim::Day>{kRaceDay, kRaceDay + 10, kRaceDay + 19});
    CHECK(rival == std::vector<sim::Day>{kRaceDay + 6, kRaceDay + 11});
    const sim::WorldState& state = simulation.state();
    CHECK(state.companies[0].programStageDays == player);
    CHECK(state.companies[1].programStageDays == rival);
    REQUIRE(sim::gameEnded(state));
    CHECK(state.outcome.day == player.back());

    // Public, like the rest of the race: each company sees its own days and
    // the other's.
    checkBothRaceViews(content, simulation);
    for (const sim::CompanyId viewer : {kPlayer, kRival}) {
        const sim::CompanyView view = simulation.view(viewer);
        CHECK(view.programs[0].stageDays == player);
        CHECK(view.programs[1].stageDays == rival);
    }
    CHECK(simulation.view(kPlayer).program.stageDays == player);
    CHECK(simulation.view(kRival).program.stageDays == rival);
}

TEST_CASE("Two stages completed on one day share that day", "[program][stage-days]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    makeStageReady(content, state);
    sim::TickReport report;
    sim::applyCompleteProgramStage(content, state, {kPlayer}, report);
    makeStageReady(content, state);
    sim::applyCompleteProgramStage(content, state, {kPlayer}, report);
    CHECK(state.companies[0].programStageDays == std::vector<sim::Day>{kRaceDay, kRaceDay});
    CHECK(state.companies[1].programStageDays.empty());
    CHECK(loadsBack(content, test::settledPace(content, state)));
}

TEST_CASE("Stage days come back from a save and are part of the hash", "[program][stage-days]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    playTwoStagesAndOne(simulation, content);
    const std::vector<sim::Day> player{kRaceDay, kRaceDay + 10};
    const std::vector<sim::Day> rival{kRaceDay + 6};
    REQUIRE(simulation.state().companies[0].programStageDays == player);
    REQUIRE(simulation.state().companies[1].programStageDays == rival);

    sim::Simulation loaded(content, 2, test::setup());
    REQUIRE(loaded.load(simulation.save()));
    CHECK(loaded.state().companies[0].programStageDays == player);
    CHECK(loaded.state().companies[1].programStageDays == rival);
    CHECK(loaded.stateHash() == simulation.stateHash());
    CHECK(loaded.view(kRival).programs[0].stageDays == player);
    CHECK(loaded.view(kPlayer).programs[1].stageDays == rival);

    // A stage completed on another day is another state.
    sim::WorldState earlier = simulation.state();
    earlier.companies[0].programStageDays[1] = kRaceDay + 9;
    sim::Simulation other(content, 3, test::setup());
    play(other, content, earlier);
    CHECK(other.stateHash() != simulation.stateHash());
}

TEST_CASE("Saves with impossible stage days are rejected", "[program][stage-days][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    playTwoStagesAndOne(simulation, content);
    const sim::Day today = simulation.state().day;
    const auto loads = [&](const auto& edit) {
        sim::WorldState edited = simulation.state();
        edit(edited.companies[0].programStageDays);
        return loadsBack(content, edited);
    };
    using Days = std::vector<sim::Day>;
    CHECK(loads([](Days&) {}));

    // One day for each stage complete: no fewer and no more.
    CHECK_FALSE(loads([](Days& days) { days.pop_back(); }));
    CHECK_FALSE(loads([](Days& days) { days.clear(); }));
    CHECK_FALSE(loads([&](Days& days) { days.push_back(today); }));

    // In the order the stages were completed; the same day twice is in order.
    CHECK_FALSE(loads([](Days& days) { days = {kRaceDay + 10, kRaceDay}; }));
    CHECK(loads([](Days& days) { days = {kRaceDay, kRaceDay}; }));

    // Never on a day that has not come.
    CHECK(loads([&](Days& days) { days.back() = today; }));
    CHECK_FALSE(loads([&](Days& days) { days.back() = today + 1; }));
    CHECK_FALSE(loads([&](Days& days) { days = {today + 1, today + 1}; }));

    // Nor on day 0: no research is finished the day a game starts.
    CHECK_FALSE(loads([](Days& days) { days = {0, kRaceDay}; }));

    // A company with no stage complete holds no day.
    sim::WorldState fresh = freshState(content);
    CHECK(loadsBack(content, fresh));
    fresh.companies[1].programStageDays.push_back(0);
    CHECK_FALSE(loadsBack(content, fresh));
}

TEST_CASE("A save's completed stages need their research", "[program][save]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    atStage(content, state.companies[0], 2);
    state = test::settledPace(content, state);
    REQUIRE(loadsBack(content, state));

    // Without the project a stage cannot have been completed, and a save
    // that says so would let the company skip it.
    const auto without = [&](sim::TechId research) {
        sim::WorldState edited = state;
        std::erase(edited.companies[0].completedTechs, research);
        return edited;
    };
    CHECK_FALSE(loadsBack(content, without(content.program.stages[0].research)));
    CHECK_FALSE(loadsBack(content, without(content.program.stages[1].research)));
    // The project for the stage being worked on may be done or not.
    CHECK(loadsBack(content, without(content.program.stages[2].research)));
    sim::WorldState ahead = state;
    ahead.companies[0].completedTechs.push_back(content.program.stages[2].research);
    CHECK(loadsBack(content, test::settledPace(content, ahead)));
}

namespace {

// A world in which the player has just founded the program on kRaceDay,
// with the rival two stages in: a lesser partner.
sim::WorldState foundedWorld(const sim::Content& content) {
    sim::WorldState state = racingState(content);
    makeFinalStageReady(content, state);
    atStage(content, state.companies[1], 2);
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);
    simulation.submit(sim::CompleteProgramStage{kPlayer});
    simulation.tick();
    REQUIRE(sim::gameEnded(simulation.state()));
    return simulation.state();
}

// A world the deadline has ended, with nobody having done anything.
sim::WorldState deadlineWorld(const sim::Content& content) {
    sim::Simulation simulation(content, 1, test::setup());
    while (!sim::gameEnded(simulation.state())) {
        simulation.tick();
    }
    return simulation.state();
}

// The world as it would stand `days` later with nothing having happened but
// the prices being recorded: enough for a save's day to be moved on its own.
sim::WorldState daysLater(sim::WorldState state, sim::Day days) {
    state.day += days;
    for (auto& market : state.markets) {
        market.priceHistory.insert(market.priceHistory.end(), days, market.price);
    }
    return state;
}

}  // namespace

TEST_CASE("A save that claims a founding the companies do not show is refused",
          "[program][ending][save]") {
    const sim::Content content = test::makeContent();
    const sim::WorldState founded = foundedWorld(content);
    REQUIRE(founded.outcome.results == std::vector<Result>{Result::Founded, Result::LesserPartner});
    REQUIRE(loadsBack(content, founded));
    const auto loads = [&](const auto& edit) {
        sim::WorldState edited = founded;
        edit(edited);
        return loadsBack(content, edited);
    };

    // The founder completed every stage, the last of them on the day the
    // game ended.
    CHECK_FALSE(loads([&](sim::WorldState& s) { atStage(content, s.companies[0], 2); }));
    CHECK_FALSE(loads([](sim::WorldState& s) { s.companies[0].programStageDays.back() -= 1; }));
    CHECK_FALSE(loads([](sim::WorldState& s) {
        s.outcome.founder = kRival;
        s.outcome.results = {Result::Lost, Result::Founded};
    }));

    // Every other company's result is the one the rule gives: a lesser
    // partner at two stages or more unless bankrupt, lost otherwise.
    CHECK_FALSE(loads([](sim::WorldState& s) { s.outcome.results[1] = Result::Lost; }));
    CHECK_FALSE(loads([&](sim::WorldState& s) { atStage(content, s.companies[1], 1); }));
    CHECK(loads([&](sim::WorldState& s) {
        atStage(content, s.companies[1], 1);
        s.outcome.results[1] = Result::Lost;
    }));
    CHECK_FALSE(loads([&](sim::WorldState& s) { makeBankrupt(content, s, kRival); }));
    CHECK(loads([&](sim::WorldState& s) {
        makeBankrupt(content, s, kRival);
        s.outcome.results[1] = Result::Lost;
    }));
    // Nobody else finished: the second order would have been refused.
    CHECK_FALSE(loads([&](sim::WorldState& s) { atStage(content, s.companies[1], 3); }));
    // The world froze on the day it ended.
    CHECK_FALSE(loads([](sim::WorldState& s) { s = daysLater(s, 1); }));
    CHECK_FALSE(loads([](sim::WorldState& s) {
        s.outcome.day -= 1;
        s.companies[0].programStageDays.back() -= 1;
    }));
}

TEST_CASE("A save that claims a deadline loss the world does not show is refused",
          "[program][ending][save]") {
    sim::Content content = test::makeContent();
    content.program.deadlineDay = kRaceDay;
    const sim::WorldState passed = deadlineWorld(content);
    REQUIRE(passed.day == kRaceDay);
    REQUIRE_FALSE(passed.outcome.founder.valid());
    REQUIRE(loadsBack(content, passed));
    const auto loads = [&](const auto& edit) {
        sim::WorldState edited = passed;
        edit(edited);
        return loadsBack(content, edited);
    };

    // Everyone lost, whatever they had completed.
    CHECK(loads([&](sim::WorldState& s) { atStage(content, s.companies[0], 2); }));
    CHECK_FALSE(loads([](sim::WorldState& s) { s.outcome.results[0] = Result::LesserPartner; }));
    // Nobody had finished, or the program would have been founded.
    CHECK_FALSE(loads([&](sim::WorldState& s) { atStage(content, s.companies[0], 3); }));

    // The deadline ends the game on the deadline day and no other.
    sim::Simulation early(content, 1, test::setup());
    early.tick(kRaceDay - 1);
    sim::WorldState before = early.state();
    REQUIRE(loadsBack(content, before));
    before.outcome = passed.outcome;
    before.outcome.day = before.day;
    CHECK_FALSE(loadsBack(content, before));
}

TEST_CASE("A save of a game still being played shows no ending", "[program][ending][save]") {
    sim::Content content = test::makeContent();
    content.program.deadlineDay = kRaceDay;
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(kRaceDay - 1);
    const sim::WorldState playing = simulation.state();
    REQUIRE(loadsBack(content, playing));

    // The deadline day itself can be saved before its tick: the deadline is
    // looked at after that day's orders.
    CHECK(loadsBack(content, daysLater(playing, 1)));
    // No later day can: the game would have ended.
    CHECK_FALSE(loadsBack(content, daysLater(playing, 2)));

    // A company with every stage complete has founded the program.
    sim::WorldState finished = playing;
    atStage(content, finished.companies[1], 3);
    CHECK_FALSE(loadsBack(content, finished));
    atStage(content, finished.companies[1], 2);
    CHECK(loadsBack(content, test::settledPace(content, finished)));
}

// The earliest founding day (spec 011, BR-001): the race paced by research
// time alone. Orders apply in step 1 of the day the view shows, a project
// completes in step 7 of its finish day, and a stage's completion and the
// next stage's start can both apply on one day. So from a stage's start the
// program takes each remaining project's duration and a day for each
// completion order. The shipped projects take 480, 1,680 and 2,520 days
// (data/techs.json): 4,683 days from the first start to the founding, 4,202
// from the second and 2,521 from the third. The fixture's take 120, 150 and
// 210: 483 days in all.

namespace {

constexpr sim::Day kShippedFromStage1 = 4'683;
constexpr sim::Day kShippedFromStage2 = 4'202;
constexpr sim::Day kShippedFromStage3 = 2'521;
constexpr sim::Day kFixtureFromStage1 = 483;

// The tech every shipped and fixture lab may start on day 0: a survey
// project of 90 days with no prerequisites.
const sim::TechId kSurveyTech(0);

sim::Content shippedContent() {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    return *result.content;
}

std::optional<sim::Day> earliest(const sim::Content& content, const sim::WorldState& state,
                                 sim::CompanyId id) {
    return sim::earliestFoundingDay(content, state, id);
}

// A research record for an ordinary project, as StartResearch would have
// left it the day before.
void startOtherResearch(const sim::Content& content, sim::WorldState& state, sim::CompanyId id,
                        sim::TechId tech) {
    sim::Research research;
    research.company = id;
    research.tech = tech;
    research.startedDay = state.day - 1;
    research.completesDay = research.startedDay + content.techs[tech.index()].durationDays;
    state.research.push_back(research);
}

void completeStageResearch(const sim::Content& content, sim::Company& company) {
    company.completedTechs.push_back(content.program.stages[company.programStage].research);
}

}  // namespace

TEST_CASE("With no stage research started the founding day counts every project from today",
          "[program][founding]") {
    const sim::Content content = shippedContent();
    sim::Simulation simulation(content, 1, test::setup());
    for (const sim::CompanyId id : {kPlayer, kRival}) {
        CHECK(earliest(content, simulation.state(), id) == kShippedFromStage1);
    }
    // A day that could have started a project and did not costs a day
    // (edge case 1).
    simulation.tick();
    for (const sim::CompanyId id : {kPlayer, kRival}) {
        CHECK(earliest(content, simulation.state(), id) == kShippedFromStage1 + 1);
    }
}

TEST_CASE("The founding day if started on a day adds each project and its completion day",
          "[program][founding]") {
    const sim::Content content = shippedContent();
    CHECK(sim::foundingDayIfStartedOn(content, 0, 0) == kShippedFromStage1);
    CHECK(sim::foundingDayIfStartedOn(content, 0, 100) == 100 + kShippedFromStage1);
    CHECK(sim::foundingDayIfStartedOn(content, 1, 100) == 100 + kShippedFromStage2);
    CHECK(sim::foundingDayIfStartedOn(content, 2, 100) == 100 + kShippedFromStage3);
    // No stage left: the founding order itself is the day.
    CHECK(sim::foundingDayIfStartedOn(content, 3, 100) == 100);
}

TEST_CASE("Stage research under way founds from the day after its finish", "[program][founding]") {
    const sim::Content content = shippedContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(kRaceDay);
    sim::WorldState state = simulation.state();

    // Started yesterday, finishing on day 39 + 480; its completion order
    // applies the day after.
    startStageResearch(content, state, kPlayer);
    REQUIRE(state.research.back().completesDay == 519);
    CHECK(earliest(content, state, kPlayer) == 520 + kShippedFromStage2);
    CHECK(earliest(content, state, kRival) == kRaceDay + kShippedFromStage1);

    // The same on a later stage.
    atStage(content, state.companies[1], 1);
    startStageResearch(content, state, kRival);
    REQUIRE(state.research.back().completesDay == 39 + 1'680);
    CHECK(earliest(content, state, kRival) == 1'720 + kShippedFromStage3);
}

TEST_CASE("Stage research done founds from today with the stages after it", "[program][founding]") {
    const sim::Content content = shippedContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(kRaceDay);
    sim::WorldState state = simulation.state();

    completeStageResearch(content, state.companies[0]);
    CHECK(earliest(content, state, kPlayer) == kRaceDay + kShippedFromStage2);

    // The last stage's research done: the completion order founds today.
    atStage(content, state.companies[1], 2);
    completeStageResearch(content, state.companies[1]);
    CHECK(earliest(content, state, kRival) == kRaceDay);
}

TEST_CASE("A lab busy with another project takes the stage's the day after it ends",
          "[program][founding]") {
    const sim::Content content = shippedContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(kRaceDay);
    sim::WorldState state = simulation.state();

    startOtherResearch(content, state, kPlayer, kSurveyTech);
    REQUIRE(state.research.back().completesDay == 39 + 90);
    CHECK(earliest(content, state, kPlayer) == 130 + kShippedFromStage1);
    // The rival's lab is free.
    CHECK(earliest(content, state, kRival) == kRaceDay + kShippedFromStage1);

    // On a later stage, too.
    atStage(content, state.companies[1], 2);
    startOtherResearch(content, state, kRival, kSurveyTech);
    CHECK(earliest(content, state, kRival) == 130 + kShippedFromStage3);
}

TEST_CASE("A founded program's founding day is the day of its last stage", "[program][founding]") {
    const sim::Content content = shippedContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(kRaceDay);
    sim::WorldState state = simulation.state();
    atStage(content, state.companies[0], 3);
    state.companies[0].programStageDays = {600, 2'400, 5'000};
    CHECK(earliest(content, state, kPlayer) == 5'000);
    CHECK_FALSE(sim::foundsPastDeadline(content, state, kPlayer));
}

TEST_CASE("A bankrupt company has no founding day and is no rival", "[program][founding]") {
    const sim::Content content = shippedContent();
    const sim::WorldState fresh = freshState(content);
    REQUIRE(sim::leadingRivalFoundingDay(content, fresh, kPlayer) == kShippedFromStage1);

    sim::WorldState state = fresh;
    makeBankrupt(content, state, kRival);
    CHECK_FALSE(earliest(content, state, kRival).has_value());
    CHECK_FALSE(sim::foundsPastDeadline(content, state, kRival));
    // The player races only the deadline (edge case 4).
    CHECK_FALSE(sim::leadingRivalFoundingDay(content, state, kPlayer).has_value());
    CHECK(sim::leadingRivalFoundingDay(content, state, kRival) == kShippedFromStage1);
}

TEST_CASE("Content with no program gives no founding day", "[program][founding]") {
    sim::Content content = test::makeContent();
    content.program.stages.clear();
    const sim::WorldState state = freshState(content);
    CHECK_FALSE(earliest(content, state, kPlayer).has_value());
    CHECK_FALSE(sim::leadingRivalFoundingDay(content, state, kPlayer).has_value());
    CHECK_FALSE(sim::foundsPastDeadline(content, state, kPlayer));
}

TEST_CASE("The leading rival is the solvent rival that could found soonest",
          "[program][founding]") {
    const sim::Content content = shippedContent();
    sim::GameSetup three = test::setup();
    three.aiNames = {"Rival", "Third"};
    sim::Simulation simulation(content, 1, three);
    simulation.tick(kRaceDay);
    sim::WorldState state = simulation.state();
    const sim::CompanyId third(2);
    startStageResearch(content, state, third);

    CHECK(sim::leadingRivalFoundingDay(content, state, kPlayer) == 520 + kShippedFromStage2);
    CHECK(sim::leadingRivalFoundingDay(content, state, kRival) == 520 + kShippedFromStage2);
    // Not itself.
    CHECK(sim::leadingRivalFoundingDay(content, state, third) == kRaceDay + kShippedFromStage1);

    makeBankrupt(content, state, third);
    CHECK(sim::leadingRivalFoundingDay(content, state, kPlayer) == kRaceDay + kShippedFromStage1);
}

TEST_CASE("The founding day does not change when only cash or deliveries change",
          "[program][founding]") {
    const sim::Content content = shippedContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(kRaceDay);
    sim::WorldState state = simulation.state();
    startStageResearch(content, state, kRival);
    const std::optional<sim::Day> player = earliest(content, state, kPlayer);
    const std::optional<sim::Day> rival = earliest(content, state, kRival);

    for (const sim::CompanyId id : {kPlayer, kRival}) {
        sim::Company& company = state.companies[id.index()];
        company.cash = sim::Money::fromUnits(-5'000'000);
        for (const sim::ProgramRequirement& need : content.program.stages[0].requirements) {
            company.programDelivered[need.resource.index()] = need.tonnes;
            company.inventoryTonnes[need.resource.index()] = 3 * need.tonnes;
        }
    }
    CHECK(earliest(content, state, kPlayer) == player);
    CHECK(earliest(content, state, kRival) == rival);
    state.companies[0].cash = sim::Money::fromUnits(900'000'000);
    CHECK(earliest(content, state, kPlayer) == player);
}

TEST_CASE("Founding past the deadline means founding after the deadline day",
          "[program][founding]") {
    const sim::Content content = shippedContent();
    sim::WorldState state = freshState(content);
    // Founding on the deadline day is in time: that day's orders are applied
    // before the deadline is looked at.
    state.day = content.program.deadlineDay - kShippedFromStage1;
    REQUIRE(earliest(content, state, kPlayer) == content.program.deadlineDay);
    CHECK_FALSE(sim::foundsPastDeadline(content, state, kPlayer));
    state.day += 1;
    CHECK(sim::foundsPastDeadline(content, state, kPlayer));
    CHECK(sim::foundsPastDeadline(content, state, kRival));
}

TEST_CASE("Starting each stage on its earliest day founds on the day foretold",
          "[program][founding]") {
    // The timing the rule counts, played: the lab busy at first, then each
    // project started the day the lab is free, and each stage completed and
    // the next started together the day after its research ends.
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    sim::Company& player = state.companies[0];
    player.cash = sim::Money::fromUnits(1'000'000'000);
    for (const sim::ProgramStage& stage : content.program.stages) {
        for (const sim::ProgramRequirement& need : stage.requirements) {
            player.inventoryTonnes[need.resource.index()] += need.tonnes;
        }
    }
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);
    simulation.submit(sim::StartResearch{kPlayer, kSurveyTech});
    simulation.tick();
    // The survey project ends on day 40 + 90; stage 1's starts the day after.
    const std::optional<sim::Day> foretold = earliest(content, simulation.state(), kPlayer);
    REQUIRE(foretold == 131 + kFixtureFromStage1);

    const auto deliverAndStart = [&](std::uint32_t stage) {
        for (const sim::ProgramRequirement& need : content.program.stages[stage].requirements) {
            simulation.submit(sim::DeliverToProgram{kPlayer, need.resource, need.tonnes});
        }
        simulation.submit(sim::StartResearch{kPlayer, content.program.stages[stage].research});
    };
    std::uint32_t changed = 0;
    std::uint32_t rivalSteps = 0;
    while (!sim::gameEnded(simulation.state())) {
        const sim::WorldState& now = simulation.state();
        const sim::Company& self = now.companies[0];
        const std::uint32_t stage = self.programStage;
        if (sim::techCompleted(self, content.program.stages[stage].research)) {
            simulation.submit(sim::CompleteProgramStage{kPlayer});
            if (stage + 1 < content.program.stages.size()) {
                deliverAndStart(stage + 1);
            }
        } else if (!sim::activeResearch(now, kPlayer).has_value()) {
            deliverAndStart(stage);
        }
        const std::optional<sim::Day> rivalBefore = earliest(content, now, kRival);
        simulation.tick();
        if (earliest(content, simulation.state(), kPlayer) != foretold) {
            ++changed;
        }
        // The rival never starts: its day moves one later each day.
        if (!sim::gameEnded(simulation.state()) &&
            earliest(content, simulation.state(), kRival) == *rivalBefore + 1) {
            ++rivalSteps;
        }
    }
    CHECK(changed == 0);
    CHECK(rivalSteps == *foretold - 41);
    const sim::WorldState& ended = simulation.state();
    CHECK(ended.outcome.founder == kPlayer);
    CHECK(ended.outcome.day == *foretold);
    CHECK(ended.companies[0].programStageDays == std::vector<sim::Day>{252, 403, *foretold});
}

TEST_CASE("No company's founding day moves earlier from one day to the next",
          "[program][founding]") {
    // Both companies played by the AI on the shipped content, which starts
    // and waits on research as it gathers cash and goods.
    const sim::Content content = shippedContent();
    sim::Simulation simulation(content, 3, test::setup());
    sim::HeuristicController ai(content);
    std::vector<std::optional<sim::Day>> before(2);
    std::uint32_t earlier = 0;
    std::uint32_t heldStill = 0;
    for (sim::Day day = 0; day < 3 * sim::kDaysPerYear && !sim::gameEnded(simulation.state());
         ++day) {
        for (std::uint32_t c = 0; c < 2; ++c) {
            before[c] = earliest(content, simulation.state(), sim::CompanyId(c));
            simulation.collectCommands(sim::CompanyId(c), ai);
        }
        simulation.tick();
        for (std::uint32_t c = 0; c < 2; ++c) {
            const std::optional<sim::Day> after =
                earliest(content, simulation.state(), sim::CompanyId(c));
            if (before[c] && after && *after < *before[c]) {
                ++earlier;
            }
            if (before[c] && after && *after == *before[c]) {
                ++heldStill;
            }
        }
    }
    CHECK(earlier == 0);
    // Research was under way on some days, or the test proves little.
    CHECK(heldStill > 0);
}

// The race's pace in the view (spec 011, BR-001 and BR-004), on the shipped
// research durations. The view is built straight from an edited state.

namespace {

// Day 40 of a shipped two-company game, nothing researched.
sim::WorldState shippedRace(const sim::Content& content) {
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(kRaceDay);
    return simulation.state();
}

sim::ProgramView ownPace(const sim::Content& content, const sim::WorldState& state) {
    return sim::makeCompanyView(content, state, kPlayer).program;
}

void checkNoPace(const sim::ProgramView& program) {
    CHECK_FALSE(program.lastStartDay.has_value());
    CHECK_FALSE(program.lastStartPassed);
    CHECK_FALSE(program.paceWarning);
    CHECK_FALSE(program.labFreeDay.has_value());
}

}  // namespace

TEST_CASE("Every view gives each company's founding day and its research's due day",
          "[program][view][founding]") {
    const sim::Content content = shippedContent();
    sim::WorldState state = shippedRace(content);
    startStageResearch(content, state, kPlayer);
    // The rival's lab busy with a project of its own: the day it frees
    // counts (GA-008), but it is no stage research due.
    startOtherResearch(content, state, kRival, kSurveyTech);

    for (const sim::CompanyId viewer : {kPlayer, kRival}) {
        const sim::CompanyView view = sim::makeCompanyView(content, state, viewer);
        REQUIRE(view.programs.size() == 2);
        CHECK(view.programs[0].earliestFoundingDay == 520 + kShippedFromStage2);
        CHECK(view.programs[0].researchDueDay == 519);
        CHECK_FALSE(view.programs[0].pastDeadline);
        CHECK(view.programs[1].earliestFoundingDay == 130 + kShippedFromStage1);
        CHECK_FALSE(view.programs[1].researchDueDay.has_value());
        CHECK_FALSE(view.programs[1].pastDeadline);
    }

    // Too late to found by the deadline: every view says so.
    sim::WorldState late = freshState(content);
    late.day = content.program.deadlineDay - kShippedFromStage1 + 1;
    for (const sim::CompanyId viewer : {kPlayer, kRival}) {
        const sim::CompanyView view = sim::makeCompanyView(content, late, viewer);
        CHECK(view.programs[0].pastDeadline);
        CHECK(view.programs[1].pastDeadline);
    }

    // A bankrupt company has no day.
    makeBankrupt(content, state, kRival);
    CHECK_FALSE(
        sim::makeCompanyView(content, state, kPlayer).programs[1].earliestFoundingDay.has_value());
}

TEST_CASE("The last day to start keeps pace with the leading rival", "[program][view][founding]") {
    const sim::Content content = shippedContent();
    const sim::WorldState base = shippedRace(content);

    // The rival's lab is busy until day 129: it could found on day
    // 130 + 4,683, so the player must start by day 130 to keep pace. That
    // is within the year's warning.
    sim::WorldState busyRival = base;
    startOtherResearch(content, busyRival, kRival, kSurveyTech);
    sim::ProgramView pace = ownPace(content, busyRival);
    CHECK(pace.lastStartDay == 130);
    CHECK_FALSE(pace.lastStartPassed);
    CHECK(pace.paceWarning);
    CHECK_FALSE(pace.labFreeDay.has_value());
    // The warning's reach is the content's.
    sim::Content noWarning = content;
    noWarning.program.paceWarningDays = 0;
    CHECK_FALSE(ownPace(noWarning, busyRival).paceWarning);
    noWarning.program.paceWarningDays = 90;
    CHECK(ownPace(noWarning, busyRival).paceWarning);
    noWarning.program.paceWarningDays = 89;
    CHECK_FALSE(ownPace(noWarning, busyRival).paceWarning);

    // The rival started yesterday: the last day was yesterday.
    sim::WorldState startedRival = base;
    startStageResearch(content, startedRival, kRival);
    pace = ownPace(content, startedRival);
    CHECK_FALSE(pace.lastStartDay.has_value());
    CHECK(pace.lastStartPassed);
    CHECK(pace.paceWarning);

    // A rival that could found today, against a player who has not begun:
    // the last day lies thousands of days before day 0, and is still only
    // "passed", never a day wrapped round.
    sim::WorldState foundingRival = base;
    atStage(content, foundingRival.companies[1], 2);
    completeStageResearch(content, foundingRival.companies[1]);
    REQUIRE(sim::earliestFoundingDay(content, foundingRival, kRival) == kRaceDay);
    pace = ownPace(content, foundingRival);
    CHECK_FALSE(pace.lastStartDay.has_value());
    CHECK(pace.lastStartPassed);
    CHECK(pace.paceWarning);
}

TEST_CASE("With the rival bankrupt the last day to start is measured against the deadline",
          "[program][view][founding]") {
    const sim::Content content = shippedContent();
    sim::WorldState state = shippedRace(content);
    makeBankrupt(content, state, kRival);
    const sim::Day lastDay = content.program.deadlineDay - kShippedFromStage1;
    REQUIRE(lastDay == 6'117);

    sim::ProgramView pace = ownPace(content, state);
    CHECK(pace.lastStartDay == lastDay);
    CHECK_FALSE(pace.lastStartPassed);
    CHECK_FALSE(pace.paceWarning);

    // The warning from paceWarningDays before the last day.
    state.day = lastDay - content.program.paceWarningDays - 1;
    CHECK_FALSE(ownPace(content, state).paceWarning);
    state.day = lastDay - content.program.paceWarningDays;
    CHECK(ownPace(content, state).paceWarning);

    // The last day itself is still in time; the day after is not.
    state.day = lastDay;
    pace = ownPace(content, state);
    CHECK(pace.lastStartDay == lastDay);
    CHECK_FALSE(pace.lastStartPassed);
    state.day = lastDay + 1;
    pace = ownPace(content, state);
    CHECK_FALSE(pace.lastStartDay.has_value());
    CHECK(pace.lastStartPassed);
    CHECK(pace.paceWarning);
}

TEST_CASE("A busy lab is named with the day it is free", "[program][view][founding]") {
    const sim::Content content = shippedContent();
    sim::WorldState state = shippedRace(content);
    CHECK_FALSE(ownPace(content, state).labFreeDay.has_value());

    startOtherResearch(content, state, kPlayer, kSurveyTech);
    const sim::ProgramView pace = ownPace(content, state);
    CHECK(pace.labFreeDay == 130);
    // The last day to start is the rival's pace all the same: the busy lab
    // has already put the player behind it.
    CHECK(pace.lastStartDay == kRaceDay);
    CHECK(pace.paceWarning);

    // The lab busy with the stage's own project is not another project.
    sim::WorldState own = shippedRace(content);
    startStageResearch(content, own, kPlayer);
    CHECK_FALSE(ownPace(content, own).labFreeDay.has_value());
}

TEST_CASE("The pace figures are empty once the stage's research has started or the race is over",
          "[program][view][founding]") {
    const sim::Content content = shippedContent();
    sim::WorldState base = shippedRace(content);
    startStageResearch(content, base, kRival);

    sim::WorldState underWay = base;
    startStageResearch(content, underWay, kPlayer);
    checkNoPace(ownPace(content, underWay));

    sim::WorldState done = base;
    completeStageResearch(content, done.companies[0]);
    checkNoPace(ownPace(content, done));

    sim::WorldState founded = base;
    atStage(content, founded.companies[0], 3);
    checkNoPace(ownPace(content, founded));

    // A busy lab is no news once the race is over.
    sim::WorldState ended = base;
    startOtherResearch(content, ended, kPlayer, kSurveyTech);
    ended.outcome.ended = 1;
    ended.outcome.day = ended.day;
    checkNoPace(ownPace(content, ended));

    sim::WorldState bankrupt = base;
    startOtherResearch(content, bankrupt, kPlayer, kSurveyTech);
    makeBankrupt(content, bankrupt, kPlayer);
    checkNoPace(ownPace(content, bankrupt));
}

// Falling behind is news (spec 011, BR-005): tick step 9b, after the day
// advances, compares each solvent company's earliest founding day with its
// leading solvent rival's and with the deadline, keeps the answers in two
// saved flags and raises a private notice when either turns true.

namespace {

// Day 40 of a fixture race in which both companies have cash for any stage
// project, put into a simulation as a save would. Cash plays no part in the
// earliest founding day, so the pace flags still hold their rule.
void cashedRace(sim::Simulation& simulation, const sim::Content& content) {
    sim::WorldState state = racingState(content);
    for (sim::Company& company : state.companies) {
        company.cash = sim::Money::fromUnits(1'000'000'000);
    }
    play(simulation, content, state);
}

sim::TechId stageTech(const sim::Content& content, std::uint32_t stage) {
    return content.program.stages[stage].research;
}

// The step on its own, on an edited state.
sim::TickReport paceOf(const sim::Content& content, sim::WorldState& state) {
    sim::TickReport report;
    sim::runProgramPace(content, state, report);
    return report;
}

}  // namespace

TEST_CASE("A company is told the day it falls behind, with its rival and both days",
          "[program][pace]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    cashedRace(simulation, content);
    REQUIRE(simulation.state().companies[0].programBehind == 0);

    simulation.submit(sim::StartResearch{kRival, stageTech(content, 0)});
    const sim::TickReport& report = simulation.tick();
    const sim::WorldState& state = simulation.state();
    const std::vector<sim::Notice> fell = noticesOf(report, Kind::ProgramFellBehind);
    REQUIRE(fell.size() == 1);
    CHECK(fell[0].company == kPlayer);
    CHECK(fell[0].rival == kRival);
    // The days the next view shows: the step runs after the day advances.
    CHECK(fell[0].foundingDay == earliest(content, state, kPlayer));
    CHECK(fell[0].rivalFoundingDay == earliest(content, state, kRival));
    CHECK(fell[0].foundingDay == *fell[0].rivalFoundingDay + 1);
    CHECK(state.companies[0].programBehind == 1);
    CHECK(state.companies[1].programBehind == 0);
    // Private: only the company that fell behind reads it.
    CHECK(noticesOf({report.day, simulation.reportFor(kPlayer)}, Kind::ProgramFellBehind).size() ==
          1);
    CHECK(noticesOf({report.day, simulation.reportFor(kRival)}, Kind::ProgramFellBehind).empty());

    // Further behind each day, and told no more.
    for (int day = 0; day < 30; ++day) {
        CHECK(noticesOf(simulation.tick(), Kind::ProgramFellBehind).empty());
    }
    CHECK(simulation.state().companies[0].programBehind == 1);
}

TEST_CASE("Drawing level clears the flag silently and a later fall is news again",
          "[program][pace]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    startStageResearch(content, state, kRival);
    REQUIRE(paceOf(content, state).notices.size() == 1);
    REQUIRE(state.companies[0].programBehind == 1);

    // Level: the player's project started the same day as the rival's.
    startStageResearch(content, state, kPlayer);
    REQUIRE(earliest(content, state, kPlayer) == earliest(content, state, kRival));
    CHECK(paceOf(content, state).notices.empty());
    CHECK(state.companies[0].programBehind == 0);

    // Both projects finish. The day after, the rival completes its stage and
    // starts the next project while the player waits: behind again.
    state.research[state.research.size() - 2].complete = 1;
    state.research.back().complete = 1;
    completeStageResearch(content, state.companies[0]);
    completeStageResearch(content, state.companies[1]);
    state.day = state.research.back().completesDay + 2;
    atStage(content, state.companies[1], 1);
    state.companies[1].programStageDays = {state.day - 1};
    startStageResearch(content, state, kRival);
    REQUIRE(earliest(content, state, kPlayer) == *earliest(content, state, kRival) + 1);
    const sim::TickReport again = paceOf(content, state);
    const std::vector<sim::Notice> fell = noticesOf(again, Kind::ProgramFellBehind);
    REQUIRE(fell.size() == 1);
    CHECK(fell[0].company == kPlayer);
    CHECK(state.companies[0].programBehind == 1);
}

TEST_CASE("Falling behind and drawing level on the same day is no news", "[program][pace]") {
    // Edge case 7: only the days at the end of the tick are compared.
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    cashedRace(simulation, content);
    simulation.submit(sim::StartResearch{kRival, stageTech(content, 0)});
    simulation.submit(sim::StartResearch{kPlayer, stageTech(content, 0)});
    const sim::TickReport& report = simulation.tick();
    CHECK(noticesOf(report, Kind::ProgramFellBehind).empty());
    CHECK(simulation.state().companies[0].programBehind == 0);
    CHECK(simulation.state().companies[1].programBehind == 0);
}

TEST_CASE("The notice names the lowest-indexed of the rivals that lead", "[program][pace]") {
    const sim::Content content = test::makeContent();
    sim::GameSetup three = test::setup();
    three.aiNames = {"Rival", "Third"};
    sim::Simulation simulation(content, 1, three);
    simulation.tick(kRaceDay);
    const sim::CompanyId third(2);

    sim::WorldState onlyThird = simulation.state();
    startStageResearch(content, onlyThird, third);
    std::vector<sim::Notice> fell = noticesOf(paceOf(content, onlyThird), Kind::ProgramFellBehind);
    REQUIRE(fell.size() == 2);
    CHECK(fell[0].company == kPlayer);
    CHECK(fell[0].rival == third);
    CHECK(fell[1].company == kRival);
    CHECK(fell[1].rival == third);

    sim::WorldState tied = simulation.state();
    startStageResearch(content, tied, kRival);
    startStageResearch(content, tied, third);
    fell = noticesOf(paceOf(content, tied), Kind::ProgramFellBehind);
    REQUIRE(fell.size() == 1);
    CHECK(fell[0].company == kPlayer);
    CHECK(fell[0].rival == kRival);
}

TEST_CASE("A bankrupt rival puts nobody behind and a bankrupt company is not checked",
          "[program][pace]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    startStageResearch(content, state, kRival);
    makeBankrupt(content, state, kRival);
    CHECK(paceOf(content, state).notices.empty());
    CHECK(state.companies[0].programBehind == 0);

    // A company behind a rival that then goes bankrupt is no longer behind,
    // and hears nothing of it.
    sim::WorldState fallen = racingState(content);
    startStageResearch(content, fallen, kRival);
    REQUIRE(paceOf(content, fallen).notices.size() == 1);
    makeBankrupt(content, fallen, kRival);
    CHECK(paceOf(content, fallen).notices.empty());
    CHECK(fallen.companies[0].programBehind == 0);

    // The bankrupt company's own flags are left as they stood.
    sim::WorldState broke = racingState(content);
    startStageResearch(content, broke, kRival);
    makeBankrupt(content, broke, kPlayer);
    broke.companies[0].programBehind = 1;
    broke.companies[0].programPastDeadline = 1;
    CHECK(paceOf(content, broke).notices.empty());
    CHECK(broke.companies[0].programBehind == 1);
    CHECK(broke.companies[0].programPastDeadline == 1);
}

TEST_CASE("A company is told once the day its founding falls past the deadline",
          "[program][pace]") {
    sim::Content content = test::makeContent();
    // From day 0 the fixture's program could found on day 483; one week of
    // waiting puts it on the deadline, and the eighth day past it.
    content.program.deadlineDay = 7 + kFixtureFromStage1;
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(6);
    REQUIRE(simulation.state().day == 6);

    // Founding on the deadline day is in time.
    CHECK(noticesOf(simulation.tick(), Kind::ProgramPastDeadline).empty());
    CHECK(earliest(content, simulation.state(), kPlayer) == content.program.deadlineDay);

    const sim::TickReport& report = simulation.tick();
    const std::vector<sim::Notice> past = noticesOf(report, Kind::ProgramPastDeadline);
    REQUIRE(past.size() == 2);
    for (std::uint32_t c = 0; c < 2; ++c) {
        CHECK(past[c].company == sim::CompanyId(c));
        CHECK(past[c].foundingDay == content.program.deadlineDay + 1);
        CHECK(simulation.state().companies[c].programPastDeadline == 1);
    }
    // Private: each reads its own.
    for (const sim::CompanyId id : {kPlayer, kRival}) {
        const std::vector<sim::Notice> own =
            noticesOf({report.day, simulation.reportFor(id)}, Kind::ProgramPastDeadline);
        REQUIRE(own.size() == 1);
        CHECK(own[0].company == id);
    }
    // Level with each other the whole way, so neither fell behind.
    CHECK(noticesOf(report, Kind::ProgramFellBehind).empty());

    std::size_t later = 0;
    while (!sim::gameEnded(simulation.state())) {
        later += noticesOf(simulation.tick(), Kind::ProgramPastDeadline).size();
    }
    CHECK(later == 0);
    CHECK(simulation.state().companies[0].programPastDeadline == 1);
}

TEST_CASE("The pace check does nothing once the game has ended", "[program][pace]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = foundedWorld(content);
    // The rival, two stages in with its last project not started, would read
    // as behind the founder.
    REQUIRE(earliest(content, state, kRival) > earliest(content, state, kPlayer));
    const sim::WorldState before = state;
    CHECK(paceOf(content, state).notices.empty());
    CHECK(sim::hashWorld(state) == sim::hashWorld(before));
}

TEST_CASE("The pace flags are saved and hashed", "[program][pace][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    cashedRace(simulation, content);
    simulation.submit(sim::StartResearch{kRival, stageTech(content, 0)});
    simulation.tick();
    REQUIRE(simulation.state().companies[0].programBehind == 1);

    sim::Simulation restored(content, 7, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.state().companies[0].programBehind == 1);
    CHECK(restored.stateHash() == simulation.stateHash());

    sim::WorldState flipped = simulation.state();
    flipped.companies[0].programBehind = 0;
    CHECK(sim::hashWorld(flipped) != simulation.stateHash());
    flipped = simulation.state();
    flipped.companies[1].programPastDeadline = 1;
    CHECK(sim::hashWorld(flipped) != simulation.stateHash());
}

TEST_CASE("A save whose pace flags differ from their rule is refused", "[program][pace][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    cashedRace(simulation, content);
    simulation.submit(sim::StartResearch{kRival, stageTech(content, 0)});
    simulation.tick();
    const sim::WorldState behind = simulation.state();
    REQUIRE(behind.companies[0].programBehind == 1);
    CHECK(sim::behindLeadingRival(content, behind, kPlayer));
    CHECK_FALSE(sim::behindLeadingRival(content, behind, kRival));
    sim::Simulation restored(content, 7, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.stateHash() == simulation.stateHash());
    const auto loads = [&](const sim::WorldState& from, const auto& edit) {
        sim::WorldState edited = from;
        edit(edited);
        return loadsBack(content, edited);
    };

    // A save may not claim a pace the world does not show, either way.
    CHECK_FALSE(loads(behind, [](sim::WorldState& s) { s.companies[0].programBehind = 0; }));
    CHECK_FALSE(loads(behind, [](sim::WorldState& s) { s.companies[1].programBehind = 1; }));
    CHECK_FALSE(loads(behind, [](sim::WorldState& s) { s.companies[0].programPastDeadline = 1; }));

    sim::Content tight = content;
    tight.program.deadlineDay = 7 + kFixtureFromStage1;
    sim::Simulation late(tight, 1, test::setup());
    late.tick(8);
    const sim::WorldState past = late.state();
    REQUIRE(past.companies[0].programPastDeadline == 1);
    REQUIRE(loadsBack(tight, past));
    sim::WorldState cleared = past;
    cleared.companies[1].programPastDeadline = 0;
    CHECK_FALSE(loadsBack(tight, cleared));

    // A bankrupt company's flags are left as they stood, so are not checked,
    // but are still flags.
    const auto broke = [&](sim::WorldState& s) { makeBankrupt(content, s, kPlayer); };
    REQUIRE(loads(behind, broke));
    CHECK(loads(behind, [&](sim::WorldState& s) {
        broke(s);
        s.companies[0].programBehind = 0;
        s.companies[0].programPastDeadline = 1;
    }));
    CHECK_FALSE(loads(behind, [&](sim::WorldState& s) {
        broke(s);
        s.companies[0].programBehind = 2;
    }));
    CHECK_FALSE(loads(behind, [&](sim::WorldState& s) {
        broke(s);
        s.companies[0].programPastDeadline = 2;
    }));

    // Nor does anything change in an ended game, the pace flags included.
    const sim::WorldState founded = foundedWorld(content);
    REQUIRE(loadsBack(content, founded));
    CHECK(loads(founded, [](sim::WorldState& s) {
        s.companies[1].programBehind ^= 1;
        s.companies[1].programPastDeadline ^= 1;
    }));
}
