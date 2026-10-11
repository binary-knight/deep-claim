#include "sim/program.h"

#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/world_state.h"

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

// The company on its last stage with everything that stage asks for.
void makeFinalStageReady(const sim::Content& content, sim::WorldState& state,
                         sim::CompanyId id = kPlayer) {
    state.companies[id.index()].programStage =
        static_cast<std::uint32_t>(content.program.stages.size() - 1);
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
    REQUIRE(simulation.load(sim::saveGame(state, {}, sim::hashContent(content))));
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
        state.companies[0].programStage = 3;
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
    state.companies[0].programStage = 1;
    CHECK_FALSE(deliver(content, state, kCopper, 200));
    CHECK(deliver(content, state, kCopper, 201) == R::ProgramNotNeeded);
    // The rival is still on stage 1.
    state.companies[1].inventoryTonnes[kCopper.index()] = 300;
    CHECK(deliver(content, state, kCopper, 200, kRival) == R::ProgramNotNeeded);
}

TEST_CASE("Completing a stage charges the cost once, clears deliveries and advances", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = freshState(content);
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
    CHECK(loadsBack(content, state));

    // The next stage starts from nothing: a repeat is refused and costs nothing.
    CHECK(complete(content, state) == R::ProgramStageNotReady);
    CHECK_FALSE(sim::programStageReady(content, state, kPlayer));
    CHECK(state.companies[0].cash == cash - cost);
}

TEST_CASE("A stage cannot be completed without research, deliveries and cash", "[program]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = freshState(content);
    state.companies[0].programStage = 1;  // 2,000 t iron, 200 t copper, 2M, tech 3
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
        state.companies[0].programStage = 3;
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
    company.programStage = 1;
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

    company.programStage = 3;
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
    // The day the game ends on is played out.
    CHECK(simulation.state().day == kRaceDay + 1);
    CHECK(simulation.state().companies[0].programStage == 3);

    const std::vector<sim::Notice> founded = noticesOf(report, Kind::ProgramFounded);
    REQUIRE(founded.size() == 1);
    CHECK(founded[0].company == kPlayer);
    CHECK(sim::noticeVisibleTo(founded[0], kRival));
    CHECK(noticesOf(report, Kind::ProgramStageComplete).size() == 1);
    CHECK(noticesOf(report, Kind::DeadlinePassed).empty());
}

TEST_CASE("When the rival founds the program the player is a lesser partner or has lost",
          "[program][ending]") {
    const sim::Content content = test::makeContent();
    sim::WorldState state = racingState(content);
    makeFinalStageReady(content, state, kRival);
    Result expected = Result::Lost;
    SECTION("two stages completed: a lesser partner") {
        state.companies[0].programStage = 2;
        expected = Result::LesserPartner;
    }
    SECTION("one stage completed: lost") {
        state.companies[0].programStage = 1;
    }
    SECTION("no stage completed: lost") {
        state.companies[0].programStage = 0;
    }
    SECTION("two stages completed but bankrupt: lost") {
        state.companies[0].programStage = 2;
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
    state.companies[0].programStage = 2;
    state.companies[1].programStage = 2;
    sim::Simulation simulation(content, 1, test::setup());
    play(simulation, content, state);

    // Every day before the deadline day is an ordinary one.
    while (simulation.state().day < content.program.deadlineDay) {
        const sim::TickReport report = simulation.tick();
        REQUIRE_FALSE(sim::gameEnded(simulation.state()));
        REQUIRE(noticesOf(report, Kind::DeadlinePassed).empty());
    }
    // The deadline day itself is played, and ends the game as it closes.
    const sim::TickReport report = simulation.tick();

    const sim::Outcome& outcome = simulation.state().outcome;
    REQUIRE(sim::gameEnded(simulation.state()));
    CHECK(outcome.day == content.program.deadlineDay);
    CHECK_FALSE(outcome.founder.valid());
    CHECK(outcome.results == std::vector<Result>{Result::Lost, Result::Lost});
    CHECK(simulation.state().day == content.program.deadlineDay + 1);
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
    state.companies[1].programStage = 2;
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
    state.companies[1].programStage = 2;
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
        state.companies[0].programStage = 2;
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
    REQUIRE(view.programs.size() == state.companies.size());
    for (std::uint32_t c = 0; c < state.companies.size(); ++c) {
        const sim::CompanyId id(c);
        const sim::ProgramPublicView& entry = view.programs[c];
        CHECK(entry.company == id);
        CHECK(entry.stagesCompleted == state.companies[c].programStage);
        CHECK(entry.complete == (state.companies[c].programStage >= content.program.stages.size()));
        CHECK(entry.progressPermille == sim::programProgressPermille(content, state, id));
        CHECK(entry.researchState == researchStateByRule(content, state, id));
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
        state.companies[0].programStage = 2;
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
