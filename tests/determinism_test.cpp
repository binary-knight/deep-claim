#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/program.h"
#include "sim/research.h"
#include "sim/save.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace {

// Plays `days` days with the built-in competitor giving the orders of every
// company, the player's included, so that land is contested.
void playWithAi(sim::Simulation& simulation, sim::HeuristicController& ai, sim::Day days) {
    const std::size_t companies = simulation.state().companies.size();
    for (sim::Day d = 0; d < days; ++d) {
        for (std::uint32_t c = 0; c < companies; ++c) {
            simulation.collectCommands(sim::CompanyId(c), ai);
        }
        simulation.tick();
    }
}

std::int64_t tonnesExtracted(const sim::Mine& mine) {
    return std::accumulate(mine.extractedTonnes.begin(), mine.extractedTonnes.end(),
                           std::int64_t{0});
}

const sim::CompanyId kPlayer(0);
const sim::CompanyId kRival(1);

// A new world in which both companies already hold what the race asks of
// them, put into `simulation` the way a save would: the player the goods and
// cash for all three stages of the fixture's program, the rival the goods for
// two stages and half the iron of the third, with no lithium. Nothing is
// handed over once the game is under way.
void loadRaceWorld(sim::Simulation& simulation, const sim::Content& content) {
    sim::WorldState state = simulation.state();
    state.companies[kPlayer.index()].cash = sim::Money::fromUnits(30'000'000);
    state.companies[kPlayer.index()].inventoryTonnes = {6'000, 200, 50};
    state.companies[kRival.index()].cash = sim::Money::fromUnits(30'000'000);
    state.companies[kRival.index()].inventoryTonnes = {4'500, 200, 0};
    REQUIRE(simulation.load(sim::saveGame(state, {}, sim::hashContent(content))));
}

// One day's orders of a company racing by rote: complete the stage it is on
// when that can be done; until then deliver what the stage still needs from
// stock and start the stage's research once the lab will take it. Every
// order is one the sim would accept today, so a rejection is a fault.
void giveRaceOrders(sim::Simulation& simulation, const sim::Content& content, sim::CompanyId id) {
    const sim::WorldState& state = simulation.state();
    const sim::Company& company = state.companies[id.index()];
    if (company.programStage >= content.program.stages.size()) {
        return;
    }
    if (sim::programStageReady(content, state, id)) {
        simulation.submit(sim::CompleteProgramStage{id});
        return;
    }
    const sim::ProgramStage& stage = content.program.stages[company.programStage];
    for (const sim::ProgramRequirement& need : stage.requirements) {
        const std::size_t r = need.resource.index();
        const std::int64_t tonnes =
            std::min(need.tonnes - company.programDelivered[r], company.inventoryTonnes[r]);
        if (tonnes > 0) {
            simulation.submit(sim::DeliverToProgram{id, need.resource, tonnes});
        }
    }
    const sim::StartResearch research{id, stage.research};
    if (!sim::validateStartResearch(content, state, research)) {
        simulation.submit(research);
    }
}

// What one company was told about the other's race.
struct RaceNotices {
    int researchStarted = 0;
    int stagesComplete = 0;
    int founded = 0;
};

// Adds the day's notices about `about` as `viewer` was shown them, so a
// notice counted here is one the other company could see.
void countRaceNotices(const sim::Simulation& simulation, sim::CompanyId viewer,
                      sim::CompanyId about, RaceNotices& seen) {
    for (const sim::Notice& notice : simulation.reportFor(viewer)) {
        if (notice.company != about) {
            continue;
        }
        seen.researchStarted += notice.kind == sim::NoticeKind::ProgramResearchStarted ? 1 : 0;
        seen.stagesComplete += notice.kind == sim::NoticeKind::ProgramStageComplete ? 1 : 0;
        seen.founded += notice.kind == sim::NoticeKind::ProgramFounded ? 1 : 0;
    }
}

// The orders the last tick refused, from either company.
std::ptrdiff_t ordersRejected(const sim::Simulation& simulation) {
    const std::vector<sim::Notice>& notices = simulation.lastReport().notices;
    return std::count_if(notices.begin(), notices.end(), [](const sim::Notice& notice) {
        return notice.kind == sim::NoticeKind::CommandRejected;
    });
}

}  // namespace

TEST_CASE("Same seed produces identical state", "[determinism]") {
    const sim::Content content = test::makeContent();
    sim::Simulation a(content, 1234, test::setup());
    sim::Simulation b(content, 1234, test::setup());
    a.tick(sim::kDaysPerYear * 10);
    b.tick(sim::kDaysPerYear * 10);
    CHECK(a.state().day == b.state().day);
    CHECK(a.stateHash() == b.stateHash());
}

TEST_CASE("Different seeds produce different state", "[determinism]") {
    const sim::Content content = test::makeContent();
    sim::Simulation a(content, 1, test::setup());
    sim::Simulation b(content, 2, test::setup());
    a.tick(sim::kDaysPerYear);
    b.tick(sim::kDaysPerYear);
    CHECK(a.stateHash() != b.stateHash());
}

TEST_CASE("Ticking changes the state hash", "[determinism]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const std::uint64_t before = simulation.stateHash();
    simulation.tick();
    CHECK(simulation.stateHash() != before);
    CHECK(simulation.state().day == 1);
}

TEST_CASE("Golden state hash is stable across platforms", "[determinism]") {
    // This value was recorded on Linux/GCC and is verified on Windows/MSVC in
    // CI. It uses the in-code content fixture, so tuning data/*.json does not
    // move it. If a deliberate simulation change alters it, update the
    // constant in the same commit and say so in the commit message.
    // Moved once for spec 001 (standing sell policies added company state),
    // spec 004 (plot and mine state) and spec 005 (the listing queue is built
    // as an opening hand plus a region-interleaved remainder) and spec 006
    // (an auction keeps the ask it opened with).
    // Spec 008 added two fields to each mine's state (lossDays and
    // lossAnnounced) and did not move it: nobody gives an order in this run,
    // so no mine is built and the mine list is hashed empty. The new fields
    // are pinned by the save and hash tests in mines_test.cpp instead.
    // Moved once for spec 010 step 1: every company gains its program stage
    // and deliveries, and the world gains an outcome. The mine field added
    // later in that step (lossClearDays) did not move it, as with spec 008.
    // Moved again in the same step, from 0x01906a0707315707: every company
    // gains the list of days it completed its stages on (programStageDays).
    // Nobody completes a stage here, so the list is hashed empty, by its
    // length alone.
    // Moved once for spec 011, from 0x059a7c79213dd307: every company gains
    // the pace check's two flags (programBehind and programPastDeadline),
    // set by the new tick step 9b. Nobody gives an order here, so both
    // companies stay level, far from the deadline, and the flags are hashed
    // as zero.
    constexpr std::uint64_t kGolden = 0xc7cc092323bf84f7ULL;
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 42, test::setup());
    simulation.tick(sim::kDaysPerYear);
    CHECK(simulation.stateHash() == kGolden);
}

TEST_CASE("Golden state hash of a played game is stable across platforms", "[determinism]") {
    // The run above gives no orders, so it hashes empty mine, survey and loan
    // lists and auctions nobody bid in. Here the built-in competitor gives
    // the orders of both companies for three years, which leaves land bought
    // at auction, surveys, mines built, running and idle with ore extracted
    // and sold, loans and research in the state. A change to how any of that
    // is stored, hashed or computed moves this value where the bare run
    // would not notice (spec 008 added two fields to every mine and the bare
    // value stayed put).
    // Recorded on Linux/GCC on 2026-10-10 and verified on Windows/MSVC in CI.
    // It uses the in-code content fixture, so tuning data/*.json does not
    // move it. A deliberate change to sim state or rules moves it once:
    // update the constant in the same commit and record the reason in
    // docs/DESIGN.md.
    // Moved once for spec 010 step 1 (recorded 2026-10-11): company state
    // grew by the program stage and deliveries, and the world by an outcome.
    // No rule changed; nobody delivers or completes a stage yet.
    // Moved again in the same step, from 0xb6d511501247ec2d, for the
    // losing-mine notice's re-arm count (BR-014): every mine gains
    // lossClearDays. With that field left out of the saved form the old
    // value still held, so the fixture's game plays out as before.
    // Moved a third time in the same step, from 0x59fab58dbf9f668d, when the
    // competitor began to race (BR-011): it delivers to its program, sets
    // its standing sell orders to keep what a stage needs, researches toward
    // the program and completes stages, so the game it plays here is a
    // different one. No saved field changed; kGolden above, where nobody
    // gives an order, did not move.
    // Moved a fourth time in the same step, from 0x1d4743bc9b26a84f: every
    // company gains the list of days it completed its stages on
    // (programStageDays). With that field left out of the saved form the old
    // value still held, so the fixture's game plays out as before. The same
    // change gave the view a flag per research project (researchOpen) that
    // the competitor reads in place of its own copy of the rule; that did
    // not move the value.
    // Moved once for spec 011, from 0x41192679894c4afc: every company gains
    // the pace check's two flags, set by the new tick step 9b. The rival
    // ends the run behind, so a flag is hashed as one. The competitor reads
    // only the view, which the step does not change, so the game it plays
    // is the same.
    constexpr std::uint64_t kGoldenPlayed = 0x43349088476870b5ULL;
    constexpr sim::Day kDays = sim::kDaysPerYear * 3;
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 42, test::setup());
    sim::HeuristicController ai(content);
    playWithAi(simulation, ai, kDays);

    // What makes the value worth pinning. If the competitor stops building,
    // this fails here instead of quietly pinning an empty game.
    const sim::WorldState& world = simulation.state();
    REQUIRE(std::any_of(world.mines.begin(), world.mines.end(),
                        [](const sim::Mine& mine) { return tonnesExtracted(mine) > 0; }));
    CHECK(std::any_of(world.surveys.begin(), world.surveys.end(),
                      [](const sim::Survey& survey) { return survey.complete != 0; }));
    CHECK(std::any_of(world.auctions.begin(), world.auctions.end(),
                      [](const sim::Auction& auction) { return auction.winner.valid(); }));
    CHECK(std::any_of(world.loans.begin(), world.loans.end(),
                      [](const sim::Loan& loan) { return loan.active != 0; }));

    CHECK(simulation.stateHash() == kGoldenPlayed);

    // The same state must come back from a save: a field that is hashed but
    // not saved, or saved in another order, shows up here.
    sim::Simulation restored(content, 7, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.stateHash() == kGoldenPlayed);
}

TEST_CASE("Golden state hash of a played race is stable across platforms", "[determinism]") {
    // Neither run above is certain to reach the program's rules: the first
    // gives no orders and the competitor in the second races only as far as
    // its mines carry it in three years. Here both companies start with
    // stock and cash and race by rote on the fixture's small program, with
    // real orders applied by ticks (DeliverToProgram, StartResearch for the
    // stage projects, CompleteProgramStage). The player completes all three
    // stages and founds the program; the rival, starting later, completes
    // two and is part-way through the third when the game ends. That leaves
    // stage days, deliveries toward an unfinished stage, a stage project
    // still running and an outcome with a founder and a lesser partner in
    // the state. A change to how any of that is stored, hashed or computed
    // moves this value.
    // Added for spec 010 step 1. Recorded on Linux/GCC on 2026-10-11; it is
    // confirmed for Windows/MSVC only when CI runs. It uses the in-code
    // content fixture, so tuning data/*.json does not move it. A deliberate
    // change to sim state or rules moves it once: update the constant in the
    // same commit and record the reason in docs/DESIGN.md.
    // Moved once by the review fixes for spec 010 step 1 (was
    // 0x588363dbb9c693f3): the world now freezes on the day it ends. The
    // order that founds the program is the last thing that happens, so the
    // rest of that day's pipeline no longer runs and the day does not
    // advance; the outcome's day equals the state's day.
    // Moved once for spec 011, from 0xae14b05ab3e56760: every company gains
    // the pace check's two flags, set by the new tick step 9b. The rival,
    // starting later, falls behind on the first day and stays behind; the
    // orders and the race are the same.
    constexpr std::uint64_t kGoldenRace = 0xf576ea4384386ae7ULL;
    constexpr sim::Day kRivalStartsDay = 45;
    constexpr sim::Day kLongestRace = sim::kDaysPerYear * 2;
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 42, test::setup());
    loadRaceWorld(simulation, content);

    RaceNotices aboutPlayer;
    RaceNotices aboutRival;
    std::ptrdiff_t rejected = 0;
    while (!sim::gameEnded(simulation.state()) && simulation.state().day < kLongestRace) {
        giveRaceOrders(simulation, content, kPlayer);
        if (simulation.state().day >= kRivalStartsDay) {
            giveRaceOrders(simulation, content, kRival);
        }
        simulation.tick();
        countRaceNotices(simulation, kRival, kPlayer, aboutPlayer);
        countRaceNotices(simulation, kPlayer, kRival, aboutRival);
        rejected += ordersRejected(simulation);
    }

    // What makes the value worth pinning. If the race stops short, this
    // fails here instead of quietly pinning a game nobody finished.
    const sim::WorldState& world = simulation.state();
    REQUIRE(sim::gameEnded(world));
    const sim::Company& player = world.companies[kPlayer.index()];
    const sim::Company& rival = world.companies[kRival.index()];
    REQUIRE(player.programStage == 3);
    REQUIRE(player.programStageDays.size() == 3);
    CHECK(player.programStageDays[0] < player.programStageDays[1]);
    CHECK(player.programStageDays[1] < player.programStageDays[2]);
    CHECK(rival.programStage == 2);
    REQUIRE(rival.programStageDays.size() == 2);
    CHECK(rival.programStageDays[0] < rival.programStageDays[1]);
    // The rival was stopped part-way: iron in for a third stage it cannot
    // complete, and that stage's project still in the lab.
    CHECK(rival.programDelivered[0] == 1'500);
    CHECK(std::any_of(world.research.begin(), world.research.end(),
                      [](const sim::Research& project) { return project.complete == 0; }));
    // The rival, starting later, fell behind and never drew level (spec
    // 011, step 9b).
    CHECK(player.programBehind == 0);
    CHECK(rival.programBehind == 1);
    CHECK(world.outcome.ended != 0);
    CHECK(world.outcome.day == player.programStageDays[2]);
    CHECK(world.outcome.day == world.day);  // the world froze on the day it ended
    CHECK(world.outcome.founder == kPlayer);
    REQUIRE(world.outcome.results.size() == 2);
    CHECK(world.outcome.results[kPlayer.index()] == sim::ProgramResult::Founded);
    CHECK(world.outcome.results[kRival.index()] == sim::ProgramResult::LesserPartner);
    // The public notices of the race, each as the other company saw it.
    CHECK(aboutPlayer.researchStarted == 3);
    CHECK(aboutPlayer.stagesComplete == 3);
    CHECK(aboutPlayer.founded == 1);
    CHECK(aboutRival.researchStarted == 3);
    CHECK(aboutRival.stagesComplete == 2);
    CHECK(aboutRival.founded == 0);
    CHECK(rejected == 0);
    CHECK(simulation.pendingCommands().empty());

    CHECK(simulation.stateHash() == kGoldenRace);

    // The same state must come back from a save of the ended game.
    sim::Simulation restored(content, 7, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.stateHash() == kGoldenRace);

    // A finished world does not change: one more day moves nothing.
    simulation.tick();
    CHECK(simulation.stateHash() == kGoldenRace);
    restored.tick();
    CHECK(restored.stateHash() == kGoldenRace);
}
