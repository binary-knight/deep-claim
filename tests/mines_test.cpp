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
    // Whatever the first listing cost and held, the build is affordable and
    // there is ore to open on.
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    setDeposit(simulation, content, plot, sim::ResourceId(0), 1'000'000);
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
    grantCash(simulation, content, kRival, sim::Money::fromUnits(50'000'000));
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

namespace {

// Removes every deposit from a plot through a save edit.
void clearDeposits(sim::Simulation& simulation, const sim::Content& content, sim::PlotId plot) {
    sim::WorldState edited = simulation.state();
    edited.hidden.plotDeposits[plot.index()].deposits.clear();
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

// Ticks until the next month boundary has been processed and returns that
// tick's report.
const sim::TickReport& tickToMonthEnd(sim::Simulation& simulation) {
    while (simulation.state().day % sim::kDaysPerMonth != 0) {
        simulation.tick();
    }
    return simulation.tick();  // processes the month-end day itself
}

}  // namespace

TEST_CASE("A mine opened on an empty plot says it found nothing and is depleted", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    clearDeposits(simulation, content, plot);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays);
    const sim::TickReport& opened = simulation.tick();
    CHECK(hasNotice(opened.notices, sim::NoticeKind::MineFoundNothing));
    CHECK_FALSE(hasNotice(opened.notices, sim::NoticeKind::MineOperational));
    CHECK_FALSE(hasNotice(opened.notices, sim::NoticeKind::MineDepleted));
    CHECK(simulation.state().mines[0].state == sim::MineState::Depleted);
}

TEST_CASE("A mine with only locked ore found nothing and warns at every month end", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    const sim::ResourceId lithium(2);  // unlocked by tech 1 in the fixture
    setDeposit(simulation, content, plot, lithium, 5'000);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays);
    const sim::TickReport& opened = simulation.tick();
    CHECK(hasNotice(opened.notices, sim::NoticeKind::MineFoundNothing));
    CHECK_FALSE(hasNotice(opened.notices, sim::NoticeKind::MineOperational));
    CHECK(simulation.state().mines[0].state == sim::MineState::Operating);  // locked ore remains

    const sim::TickReport& monthEnd = tickToMonthEnd(simulation);
    CHECK(hasNotice(monthEnd.notices, sim::NoticeKind::IdleMineWarning));
    // A day that is not a month end does not warn.
    CHECK_FALSE(hasNotice(simulation.tick().notices, sim::NoticeKind::IdleMineWarning));

    // Mothballing stops the warning; unlocking the ore makes it extractable.
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    simulation.tick();
    CHECK_FALSE(hasNotice(tickToMonthEnd(simulation).notices, sim::NoticeKind::IdleMineWarning));
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 1});
    simulation.tick();
    sim::WorldState edited = simulation.state();
    edited.companies[0].completedTechs.push_back(sim::TechId(0));
    edited.companies[0].completedTechs.push_back(sim::TechId(1));
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    CHECK_FALSE(hasNotice(tickToMonthEnd(simulation).notices, sim::NoticeKind::IdleMineWarning));
    CHECK(simulation.state().mines[0].extractedTonnes[lithium.index()] > 0);
}

TEST_CASE("The mine view carries the owner's latest survey estimate", "[mines][view]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    setDeposit(simulation, content, plot, sim::ResourceId(0), 100'000);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick();
    REQUIRE(simulation.view(kPlayer).mines.size() == 1);
    CHECK(simulation.view(kPlayer).mines[0].bestEstimateTonnes ==
          std::vector<std::int64_t>(content.resources.size(), 0));  // never surveyed

    simulation.submit(sim::CommissionSurvey{kPlayer, plot});
    simulation.tick(content.balance.surveyDurationDays + 1);
    const sim::CompanyView view = simulation.view(kPlayer);
    REQUIRE(view.surveys.size() == 1);
    REQUIRE(view.surveys[0].complete);
    REQUIRE(view.surveys[0].estimates.size() == 1);
    CHECK(view.mines[0].bestEstimateTonnes[0] == view.surveys[0].estimates[0].tonnesEstimate);
    CHECK(view.mines[0].bestEstimateTonnes[0] > 0);
    // The rival's view of the same plot carries no estimate.
    CHECK(simulation.view(kRival).mines.empty());
}

// Injects a completed survey of `plot` for the player estimating `tonnes`
// of resource 0 with the given relative error, as the latest survey on record.
void injectSurvey(sim::Simulation& simulation, const sim::Content& content, sim::PlotId plot,
                  std::int64_t tonnes, sim::Fixed error = sim::Fixed::zero()) {
    sim::WorldState edited = simulation.state();
    sim::Survey survey;
    survey.company = kPlayer;
    survey.plot = plot;
    // Saves are validated: a complete survey finished before today and ran
    // the full survey duration.
    REQUIRE(edited.day > content.balance.surveyDurationDays + 1);
    survey.completesDay = edited.day - 1;
    survey.commissionedDay = survey.completesDay - content.balance.surveyDurationDays;
    survey.complete = 1;
    survey.estimates.push_back({sim::ResourceId(0), tonnes, error});
    edited.surveys.push_back(survey);
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

TEST_CASE("A mine warns at month end when the survey estimate is nearly spent", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    setDeposit(simulation, content, plot, sim::ResourceId(0), 10'000'000'000);  // never depletes
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays + 1);
    const sim::CompanyView opened = simulation.view(kPlayer);
    const sim::MineView& mine = opened.mines.at(0);
    REQUIRE(mine.state == sim::MineState::Operating);
    const std::int64_t output = mine.lastOutputTonnes.at(0);
    REQUIRE(output > 0);
    const std::int64_t extracted = mine.extractedTonnes.at(0);

    // Without a survey there is no estimate and no warning.
    CHECK_FALSE(hasNotice(tickToMonthEnd(simulation).notices, sim::NoticeKind::MineRunningLow));

    // A generous estimate: hundreds of days left, no warning.
    injectSurvey(simulation, content, plot, extracted + output * 1000);
    CHECK_FALSE(hasNotice(tickToMonthEnd(simulation).notices, sim::NoticeKind::MineRunningLow));

    // The latest survey says about twenty days: warn, with the days in the notice.
    // The survey is fresh (it measures what is in the ground now), so only
    // extraction after it counts.
    injectSurvey(simulation, content, plot, output * (sim::kRunningLowDays - 10));
    const sim::TickReport& monthEnd = tickToMonthEnd(simulation);
    REQUIRE(hasNotice(monthEnd.notices, sim::NoticeKind::MineRunningLow));
    for (const sim::Notice& notice : monthEnd.notices) {
        if (notice.kind == sim::NoticeKind::MineRunningLow) {
            CHECK(notice.plot == plot);
            CHECK(notice.tonnes <= sim::kRunningLowDays);
            CHECK(notice.tonnes >= 0);
            CHECK(notice.company == kPlayer);
        }
    }
    // Said once: the mine keeps working past the estimate without repeating it.
    CHECK_FALSE(hasNotice(tickToMonthEnd(simulation).notices, sim::NoticeKind::MineRunningLow));
    CHECK_FALSE(hasNotice(tickToMonthEnd(simulation).notices, sim::NoticeKind::MineRunningLow));
    // The rival never sees it.
    CHECK(simulation.view(kRival).mines.empty());
}

TEST_CASE("The running-low warning uses the low end of the survey", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    setDeposit(simulation, content, plot, sim::ResourceId(0), 10'000'000'000);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays + 1);
    const std::int64_t output = simulation.view(kPlayer).mines.at(0).lastOutputTonnes.at(0);
    REQUIRE(output > 0);
    const auto half = sim::Fixed::fromRatio(1, 2);

    // Central estimate: 400 days of ore. Low end at x0.5: 200 days. Neither
    // is close, so the first months are quiet.
    injectSurvey(simulation, content, plot, output * 400, half);
    int quietMonths = 0;
    int warnings = 0;
    std::int64_t daysAtWarning = -1;
    for (int month = 0; month < 12; ++month) {
        const sim::TickReport& report = tickToMonthEnd(simulation);
        const std::int64_t centralDaysLeft =
            simulation.view(kPlayer).mines.at(0).estimatedRemainingTonnes.at(0) / output;
        for (const sim::Notice& notice : report.notices) {
            if (notice.kind == sim::NoticeKind::MineRunningLow) {
                ++warnings;
                daysAtWarning = centralDaysLeft;
                CHECK(notice.tonnes > 0);
                CHECK(notice.tonnes <= sim::kRunningLowDays);
            }
        }
        if (warnings == 0) {
            ++quietMonths;
        }
    }
    // One warning, about when the low end (200 days) ran out: the central
    // estimate still showed roughly 200 days of ore, far above the 30-day
    // threshold the old rule waited for.
    CHECK(warnings == 1);
    CHECK(quietMonths >= 4);
    CHECK(daysAtWarning > 150);
    CHECK(daysAtWarning < 260);
}

TEST_CASE("A fresh survey of a working mine is not charged for earlier extraction",
          "[mines][view]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(50'000'000));
    setDeposit(simulation, content, plot, sim::ResourceId(0), 10'000'000'000);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays + 60);
    const sim::CompanyView before = simulation.view(kPlayer);
    const std::int64_t output = before.mines.at(0).lastOutputTonnes.at(0);
    const std::int64_t lifetime = before.mines.at(0).extractedTonnes.at(0);
    REQUIRE(output > 0);
    CHECK(before.mines.at(0).estimatedRemainingTonnes.at(0) == -1);  // never surveyed

    // A survey completed yesterday that found less than the mine has
    // already taken: nearly all of it is still there.
    const std::int64_t estimate = lifetime / 2;
    REQUIRE(estimate > output * 5);
    injectSurvey(simulation, content, plot, estimate);
    const sim::CompanyView after = simulation.view(kPlayer);
    const std::int64_t remaining = after.mines.at(0).estimatedRemainingTonnes.at(0);
    CHECK(remaining <= estimate);
    CHECK(remaining >= estimate - 2 * output);
    CHECK(after.mines.at(0).bestEstimateTonnes.at(0) == estimate);

    // A survey from before the mine opened predates every tonne extracted.
    sim::WorldState edited = simulation.state();
    edited.surveys.back().completesDay = edited.mines[0].operationalDay - 1;
    edited.surveys.back().commissionedDay =
        edited.surveys.back().completesDay - content.balance.surveyDurationDays;
    edited.surveys.back().estimates[0].tonnesEstimate = lifetime * 3;
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    const sim::CompanyView old = simulation.view(kPlayer);
    CHECK(old.mines.at(0).estimatedRemainingTonnes.at(0) ==
          lifetime * 3 - old.mines.at(0).extractedTonnes.at(0));
}

namespace {

int countRunningLow(const sim::TickReport& report) {
    return static_cast<int>(std::count_if(
        report.notices.begin(), report.notices.end(),
        [](const sim::Notice& n) { return n.kind == sim::NoticeKind::MineRunningLow; }));
}

}  // namespace

TEST_CASE("Running low is said once per survey, whatever the mine does next", "[mines][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(80'000'000));
    setDeposit(simulation, content, plot, sim::ResourceId(0), 10'000'000'000);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    simulation.tick(content.balance.mineBuildDays + 1);
    const std::int64_t output = simulation.view(kPlayer).mines.at(0).lastOutputTonnes.at(0);
    REQUIRE(output > 0);
    CHECK_FALSE(simulation.state().mines[0].lowWarnedSurvey.valid());

    // Eighty days of ore: quiet, then one warning as it comes within thirty.
    injectSurvey(simulation, content, plot, output * 80);
    int warnings = 0;
    for (int month = 0; month < 4 && warnings == 0; ++month) {
        warnings += countRunningLow(tickToMonthEnd(simulation));
    }
    REQUIRE(warnings == 1);
    const sim::SurveyId warned = simulation.state().mines[0].lowWarnedSurvey;
    REQUIRE(warned.valid());
    CHECK(warned.index() == simulation.state().surveys.size() - 1);

    // Mothball for most of a month and reopen: the estimate has days left
    // again at the next check, but the warning has been given.
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    simulation.tick(20);
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 1});
    simulation.tick(2);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Operating);
    for (int month = 0; month < 6; ++month) {
        warnings += countRunningLow(tickToMonthEnd(simulation));
    }
    CHECK(warnings == 1);

    // The marker survives a save and is checked on load.
    const std::vector<std::uint8_t> bytes = simulation.save();
    sim::Simulation other(content, 7, test::setup());
    REQUIRE(other.load(bytes));
    CHECK(other.state().mines[0].lowWarnedSurvey == warned);
    CHECK(other.stateHash() == simulation.stateHash());
    sim::WorldState broken = simulation.state();
    broken.mines[0].lowWarnedSurvey = sim::SurveyId(999);  // no such survey
    CHECK_FALSE(other.load(sim::saveGame(broken, {}, sim::hashContent(content))));
    broken = simulation.state();
    broken.surveys[warned.index()].company = kRival;  // not the owner's survey
    CHECK_FALSE(other.load(sim::saveGame(broken, {}, sim::hashContent(content))));

    // A newer survey is a new question: it warns once more.
    injectSurvey(simulation, content, plot, output * 10);
    CHECK(countRunningLow(tickToMonthEnd(simulation)) == 1);
    CHECK(countRunningLow(tickToMonthEnd(simulation)) == 0);
    CHECK(simulation.state().mines[0].lowWarnedSurvey != warned);
}

TEST_CASE("A survey already past its low end when the mine opens still warns", "[mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(80'000'000));
    setDeposit(simulation, content, plot, sim::ResourceId(0), 10'000'000'000);
    // Surveyed before building: a handful of tonnes, long since dug by the
    // first month end of operation, and the survey is months old by then.
    injectSurvey(simulation, content, plot, 5);
    simulation.submit(sim::BuildMine{kPlayer, plot});
    int warnings = 0;
    for (sim::Day d = 0; d <= content.balance.mineBuildDays; ++d) {
        warnings += countRunningLow(simulation.tick());
    }
    CHECK(warnings == 0);  // not while building
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Operating);
    for (int month = 0; month < 6; ++month) {
        const sim::TickReport& report = tickToMonthEnd(simulation);
        for (const sim::Notice& notice : report.notices) {
            if (notice.kind == sim::NoticeKind::MineRunningLow) {
                ++warnings;
                CHECK(notice.tonnes == 0);  // floored: the low end is behind us
            }
        }
    }
    CHECK(warnings == 1);
}
