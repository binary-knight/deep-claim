#include "sim/mines.h"

#include "sim/commands.h"
#include "sim/fixed.h"
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

const sim::CompanyId kPlayer(0);
const sim::CompanyId kRival(1);

bool hasNotice(const std::vector<sim::Notice>& notices, sim::NoticeKind kind,
               sim::RejectReason reason = sim::RejectReason::None) {
    return std::any_of(notices.begin(), notices.end(),
                       [&](const sim::Notice& n) { return n.kind == kind && n.reason == reason; });
}

sim::PlotId firstListed(const sim::WorldState& state) {
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        if (state.plots[i].listing == sim::PlotListing::Listed) {
            return sim::PlotId(i);
        }
    }
    return sim::PlotId();
}

// Buys the first listed plot for `company` and returns it.
sim::PlotId buyFirstListed(sim::Simulation& simulation, sim::CompanyId company) {
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money ask = simulation.state().plots[plot.index()].askPrice;
    simulation.submit(sim::BidOnPlot{company, plot, ask});
    const sim::Day closes = simulation.state()
                                .auctions[simulation.state().plots[plot.index()].auction.index()]
                                .closesOnDay;
    while (simulation.state().day <= closes) {
        simulation.tick();
    }
    REQUIRE(simulation.state().plots[plot.index()].owner == company);
    return plot;
}

// Sets a company's cash through a save edit.
void grantCash(sim::Simulation& simulation, const sim::Content& content, sim::CompanyId company,
               sim::Money amount) {
    sim::WorldState edited = simulation.state();
    edited.companies[company.index()].cash = amount;
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

// Gives a plot a known deposit through a save edit so extraction is testable.
void setDeposit(sim::Simulation& simulation, const sim::Content& content, sim::PlotId plot,
                sim::ResourceId resource, std::int64_t tonnes) {
    sim::WorldState edited = simulation.state();
    auto& deposits = edited.hidden.plotDeposits[plot.index()].deposits;
    deposits.clear();
    deposits.push_back({resource, tonnes, tonnes});
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

}  // namespace

TEST_CASE("Building a mine charges capex by area and becomes operational later", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    const sim::Plot& plotState = simulation.state().plots[plot.index()];
    const sim::Money cash = simulation.state().companies[0].cash;
    const sim::Money capex = content.balance.mineCapexPerHectare * plotState.areaHectares;

    simulation.submit(sim::BuildMine{kPlayer, plot});
    const sim::TickReport& started = simulation.tick();
    CHECK(hasNotice(started.notices, sim::NoticeKind::MineStarted));
    CHECK(simulation.state().companies[0].cash == cash - capex);
    REQUIRE(simulation.state().mines.size() == 1);
    CHECK(simulation.state().mines[0].state == sim::MineState::Building);
    CHECK(simulation.state().plots[plot.index()].mine == sim::MineId(0));
    CHECK(simulation.view(kRival).plots[plot.index()].mine.has_value());  // mines are public

    // No opex while building. The build tick processed day D; the mine
    // becomes operational while processing day D + mineBuildDays.
    const sim::Money afterBuildStart = simulation.state().companies[0].cash;
    simulation.tick(content.balance.mineBuildDays - 1);
    CHECK(simulation.state().companies[0].cash == afterBuildStart);
    CHECK(simulation.state().mines[0].state == sim::MineState::Building);
    simulation.tick();
    CHECK(simulation.state().mines[0].state == sim::MineState::Operating);
    CHECK(hasNotice(simulation.lastReport().notices, sim::NoticeKind::MineOperational));
}

TEST_CASE("Mine commands are validated", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kRival);
    simulation.submit(sim::BuildMine{kPlayer, plot});  // not the owner
    simulation.submit(sim::BuildMine{kRival, sim::PlotId(999)});
    simulation.submit(sim::SetMineState{kRival, sim::MineId(0), 0});  // no mine yet
    simulation.submit(sim::BuildMine{kRival, plot});
    simulation.submit(sim::BuildMine{kRival, plot});  // second mine on the plot
    const sim::TickReport& report = simulation.tick();
    using R = sim::RejectReason;
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::NotPlotOwner));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::UnknownPlot));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::UnknownMine));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::MineExists));
    CHECK(simulation.state().mines.size() == 1);

    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});  // not the mine owner
    simulation.submit(sim::SetMineState{kRival, sim::MineId(0), 0});   // still building
    const sim::TickReport& later = simulation.tick();
    CHECK(hasNotice(later.notices, sim::NoticeKind::CommandRejected, R::NotMineOwner));
    CHECK(hasNotice(later.notices, sim::NoticeKind::CommandRejected, R::MineNotSwitchable));
}

TEST_CASE("Operating mines extract, pay opex, decline and deplete", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    const sim::ResourceId iron(0);
    setDeposit(simulation, content, plot, iron, 10'000);

    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays + 1);  // operational, first day extracted
    const sim::Mine& mine = simulation.state().mines[0];
    REQUIRE(mine.state == sim::MineState::Operating);
    // Day one: full deposit, output = base 400 * (0.5 + 0.5) = 400.
    CHECK(mine.extractedTonnes[iron.index()] == 400);
    CHECK(simulation.state().companies[0].inventoryTonnes[iron.index()] == 400);
    CHECK(simulation.state().hidden.plotDeposits[plot.index()].deposits[0].tonnesRemaining ==
          9'600);

    const sim::Money before = simulation.state().companies[0].cash;
    simulation.tick();
    CHECK(simulation.state().companies[0].cash == before - content.balance.mineOpexPerDay);
    // Output declines as the deposit empties: 400 * (0.5 + 0.5 * 0.96) = 392.
    CHECK(mine.extractedTonnes[iron.index()] == 792);

    const sim::CompanyView view = simulation.view(kPlayer);
    REQUIRE(view.mines.size() == 1);
    CHECK(view.mines[0].extractedTonnes[iron.index()] == 792);
    CHECK(view.mines[0].state == sim::MineState::Operating);
    CHECK(simulation.view(kRival).mines.empty());

    // Run to depletion: opex stops and the mine reports depleted.
    simulation.tick(60);
    CHECK(mine.state == sim::MineState::Depleted);
    CHECK(simulation.state().hidden.plotDeposits[plot.index()].deposits[0].tonnesRemaining == 0);
    CHECK(mine.extractedTonnes[iron.index()] == 10'000);
    const sim::Money depletedCash = simulation.state().companies[0].cash;
    simulation.tick(5);
    CHECK(simulation.state().companies[0].cash == depletedCash);
}

TEST_CASE("Mothballed mines stop producing and pay reduced opex", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    setDeposit(simulation, content, plot, sim::ResourceId(0), 1'000'000);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays + 1);

    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    const sim::TickReport& switched = simulation.tick();
    CHECK(hasNotice(switched.notices, sim::NoticeKind::MineSwitched));
    CHECK(simulation.state().mines[0].state == sim::MineState::Mothballed);
    const std::int64_t extracted = simulation.state().mines[0].extractedTonnes[0];
    const sim::Money before = simulation.state().companies[0].cash;
    simulation.tick();
    CHECK(simulation.state().mines[0].extractedTonnes[0] == extracted);
    CHECK(simulation.state().companies[0].cash ==
          before - content.balance.mineOpexPerDay.scaled(content.balance.mineMothballOpexMult));

    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 1});
    simulation.tick();
    CHECK(simulation.state().mines[0].state == sim::MineState::Operating);
    CHECK(simulation.state().mines[0].extractedTonnes[0] > extracted);
}

TEST_CASE("Locked resources stay in the ground", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    const sim::ResourceId lithium(2);  // unlocked by tech 1 in the fixture
    setDeposit(simulation, content, plot, lithium, 5'000);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays + 5);
    CHECK(simulation.state().mines[0].state == sim::MineState::Operating);
    CHECK(simulation.state().mines[0].extractedTonnes[lithium.index()] == 0);
    CHECK(simulation.state().hidden.plotDeposits[plot.index()].deposits[0].tonnesRemaining ==
          5'000);

    // Granting the tech through a save edit starts extraction.
    sim::WorldState edited = simulation.state();
    edited.companies[0].completedTechs.push_back(sim::TechId(1));  // prerequisite missing
    CHECK_FALSE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    edited.companies[0].completedTechs.push_back(sim::TechId(0));
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    simulation.tick();
    CHECK(simulation.state().mines[0].extractedTonnes[lithium.index()] == 5);  // base output 5
}

TEST_CASE("Mines and surveys survive save and load mid-way", "[mines][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation original(content, 5, test::setup());
    const sim::PlotId plot = buyFirstListed(original, kPlayer);
    grantCash(original, content, kPlayer, sim::Money::fromUnits(50'000'000));
    original.submit(sim::BuildMine{kPlayer, plot});
    original.submit(sim::CommissionSurvey{kPlayer, plot});
    original.tick(10);
    REQUIRE(original.state().mines.size() == 1);

    sim::Simulation restored(content, 6, test::setup());
    REQUIRE(restored.load(original.save()));
    CHECK(restored.stateHash() == original.stateHash());
    original.tick(300);
    restored.tick(300);
    CHECK(restored.stateHash() == original.stateHash());
    CHECK(restored.state().mines[0].state != sim::MineState::Building);
}

TEST_CASE("Saves with inconsistent mines are rejected", "[mines][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick();
    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };

    sim::WorldState broken = simulation.state();
    broken.mines[0].owner = kRival;  // mine owner differs from plot owner
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.plots[plot.index()].mine = sim::MineId();  // plot forgot its mine
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.mines[0].extractedTonnes.pop_back();  // wrong inventory shape
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.mines[0].extractedTonnes[0] = 5;  // Building mine with extraction
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.mines[0].state = sim::MineState::Operating;  // operational too early
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    for (sim::Plot& p : broken.plots) {
        if (!p.mine.valid() && p.owner != kPlayer) {
            p.mine = sim::MineId(0);  // ghost mine reference from another plot
            break;
        }
    }
    CHECK_FALSE(simulation.load(saveOf(broken)));

    CHECK(simulation.load(saveOf(simulation.state())));
}

TEST_CASE("Mine accounting is conserved across the lifetime of a deposit", "[mines][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    setDeposit(simulation, content, plot, sim::ResourceId(0), 3'000);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays + 5);
    // Every intermediate state must be a valid save (conservation holds daily).
    for (int day = 0; day < 10; ++day) {
        CHECK(sim::validateWorld(content, simulation.state()));
        simulation.tick();
    }
    CHECK(simulation.state().mines[0].state == sim::MineState::Depleted);
    CHECK(sim::validateWorld(content, simulation.state()));

    sim::WorldState broken = simulation.state();
    broken.mines[0].state = sim::MineState::Mothballed;  // nothing left to mothball
    CHECK_FALSE(sim::validateWorld(content, broken));
}
