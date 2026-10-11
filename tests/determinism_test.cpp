#include "sim/ai_controller.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>

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
    constexpr std::uint64_t kGolden = 0x01906a0707315707ULL;
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
    constexpr std::uint64_t kGoldenPlayed = 0x1d4743bc9b26a84fULL;
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
