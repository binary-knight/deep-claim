#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"
#include "sim/world_state.h"

#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

const sim::CompanyId kRival(1);

// Runs the game with the heuristic AI driving company 1.
void playYears(sim::Simulation& simulation, sim::HeuristicController& ai, sim::Day days,
               std::uint32_t& rejections) {
    for (sim::Day d = 0; d < days; ++d) {
        simulation.collectCommands(kRival, ai);
        const sim::TickReport& report = simulation.tick();
        for (const sim::Notice& n : report.notices) {
            if (n.kind == sim::NoticeKind::CommandRejected && n.company == kRival) {
                ++rejections;
            }
        }
    }
}

}  // namespace

TEST_CASE("The AI plays twenty years without invalid commands or crashes", "[ai]") {
    const sim::Content content = test::makeContent();
    for (sim::Seed seed = 1; seed <= 3; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        sim::HeuristicController ai(content);
        std::uint32_t rejections = 0;
        playYears(simulation, ai, sim::kDaysPerYear * 20, rejections);
        CHECK(rejections == 0);
        const sim::CompanyView view = simulation.view(kRival);
        // It did something: owns land, built mines, sold ore or researched.
        const bool active =
            view.companies[1].plotsOwned > 0 || !view.mines.empty() || !view.completedTechs.empty();
        CHECK(active);
    }
}

TEST_CASE("The AI is deterministic and reproducible from a save", "[ai][determinism]") {
    const sim::Content content = test::makeContent();
    sim::Simulation a(content, 11, test::setup());
    sim::Simulation b(content, 11, test::setup());
    sim::HeuristicController ai(content);
    std::uint32_t ignored = 0;
    playYears(a, ai, 400, ignored);
    playYears(b, ai, 400, ignored);
    CHECK(a.stateHash() == b.stateHash());

    sim::Simulation restored(content, 99, test::setup());
    REQUIRE(restored.load(a.save()));
    playYears(a, ai, 400, ignored);
    playYears(restored, ai, 400, ignored);
    CHECK(a.stateHash() == restored.stateHash());
}

TEST_CASE("The AI stops acting once bankrupt", "[ai]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    sim::WorldState edited = simulation.state();
    edited.companies[1].cash = sim::Money::fromUnits(-1);
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    sim::HeuristicController ai(content);
    std::uint32_t rejections = 0;
    playYears(simulation, ai, sim::kDaysPerYear, rejections);
    CHECK(simulation.state().companies[1].bankrupt == 1);
    CHECK(rejections == 0);
}
