#include "sim/program.h"

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

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The program's rules (spec 010, BR-003 and BR-005), called directly; the
// orders going through a session and a tick are covered in trace_test.cpp.

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

// Everything the player's current stage asks for, in place: research done,
// goods delivered, and more cash than the cost.
void makeStageReady(const sim::Content& content, sim::WorldState& state) {
    sim::Company& company = state.companies[kPlayer.index()];
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
    // Ending the game is not decided here.
    CHECK(state.outcome.ended == 0);

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
    sim::WorldState state = freshState(content);
    sim::TickReport report;
    for (std::uint32_t stage = 0; stage < 3; ++stage) {
        makeStageReady(content, state);
        REQUIRE_FALSE(complete(content, state));
        sim::applyCompleteProgramStage(content, state, {kPlayer}, report);
        CHECK(state.companies[0].programStage == stage + 1);
        CHECK(report.notices.back().tonnes == stage + 1);
        CHECK(report.notices.back().amount == content.program.stages[stage].cost);
    }
    CHECK(complete(content, state) == R::ProgramComplete);
    CHECK(sim::programProgressPermille(content, state, kPlayer) == 1'000);
    CHECK(state.outcome.ended == 0);
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
}
