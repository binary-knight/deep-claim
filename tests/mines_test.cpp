#include "sim/mines.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/events.h"
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
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string_view>
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
    CHECK(simulation.state().companies[0].cash ==
          before - content.resources[iron.index()].mineOpexPerDay);
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
    // The notice says which way the mine went, on which plot (playtest 4).
    const auto mothballed = std::find_if(
        switched.notices.begin(), switched.notices.end(),
        [](const sim::Notice& n) { return n.kind == sim::NoticeKind::MineMothballed; });
    REQUIRE(mothballed != switched.notices.end());
    CHECK(mothballed->company == kPlayer);
    CHECK(mothballed->plot == plot);
    CHECK(mothballed->mine == sim::MineId(0));
    CHECK_FALSE(hasNotice(switched.notices, sim::NoticeKind::MineReopened));
    CHECK(std::string_view(sim::noticeKindName(sim::NoticeKind::MineMothballed)) ==
          "mine_mothballed");
    CHECK(std::string_view(sim::noticeKindName(sim::NoticeKind::MineReopened)) == "mine_reopened");
    CHECK_FALSE(sim::noticeIsPublic(sim::NoticeKind::MineMothballed));
    CHECK_FALSE(sim::noticeIsPublic(sim::NoticeKind::MineReopened));
    CHECK(simulation.state().mines[0].state == sim::MineState::Mothballed);
    // Ordering what is already so is not news.
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    CHECK_FALSE(hasNotice(simulation.tick().notices, sim::NoticeKind::MineMothballed));
    const std::int64_t extracted = simulation.state().mines[0].extractedTonnes[0];
    const sim::Money before = simulation.state().companies[0].cash;
    simulation.tick();
    CHECK(simulation.state().mines[0].extractedTonnes[0] == extracted);
    CHECK(simulation.state().companies[0].cash ==
          before -
              content.resources[0].mineOpexPerDay.scaled(content.balance.mineMothballOpexMult));

    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 1});
    const sim::TickReport& reopenedReport = simulation.tick();
    const auto reopened =
        std::find_if(reopenedReport.notices.begin(), reopenedReport.notices.end(),
                     [](const sim::Notice& n) { return n.kind == sim::NoticeKind::MineReopened; });
    REQUIRE(reopened != reopenedReport.notices.end());
    CHECK(reopened->plot == plot);
    CHECK(reopened->mine == sim::MineId(0));
    CHECK_FALSE(hasNotice(reopenedReport.notices, sim::NoticeKind::MineMothballed));
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

// Spec 008: running cost by ore, daily margin and break-even price.

namespace {

const sim::ResourceId kIron(0);
const sim::ResourceId kCopper(1);
const sim::ResourceId kLithium(2);  // locked until tech 1 in the fixture

// The fixture with a different running cost per ore, so a test can tell
// which ore a mine was charged for: iron 8,000, copper 12,000 and (locked)
// lithium 20,000 a day.
sim::Content costedContent() {
    sim::Content c = test::makeContent();
    c.resources[kIron.index()].mineOpexPerDay = sim::Money::fromUnits(8'000);
    c.resources[kCopper.index()].mineOpexPerDay = sim::Money::fromUnits(12'000);
    c.resources[kLithium.index()].mineOpexPerDay = sim::Money::fromUnits(20'000);
    return c;
}

// Iron $60, copper $900, lithium $14,000: the fixture's base prices.
std::vector<sim::Money> basePrices() {
    return {sim::Money::fromUnits(60), sim::Money::fromUnits(900), sim::Money::fromUnits(14'000)};
}

sim::DepositTruth deposit(sim::ResourceId resource, std::int64_t tonnes) {
    return {resource, tonnes, tonnes};
}

// Buys the first listed plot for the player, gives it exactly `deposits`
// and orders a mine on it. Returns the plot; the mine is mine 0.
sim::PlotId orderMine(sim::Simulation& simulation, const sim::Content& content,
                      const std::vector<sim::DepositTruth>& deposits) {
    const sim::PlotId plot = buyFirstListed(simulation, kPlayer);
    sim::WorldState edited = simulation.state();
    edited.companies[kPlayer.index()].cash = sim::Money::fromUnits(50'000'000);
    edited.hidden.plotDeposits[plot.index()].deposits = deposits;
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    simulation.submit(sim::BuildMine{kPlayer, plot});
    return plot;
}

// As orderMine, then runs until the mine has had its first operating day.
sim::PlotId openMine(sim::Simulation& simulation, const sim::Content& content,
                     const std::vector<sim::DepositTruth>& deposits) {
    const sim::PlotId plot = orderMine(simulation, content, deposits);
    simulation.tick(content.balance.mineBuildDays + 1);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Operating);
    return plot;
}

// What one more day takes out of the player's cash. Nothing is sold in
// these tests, so on a day that is not a month end this is the mine's cost.
sim::Money dayCharge(sim::Simulation& simulation) {
    const sim::Money before = simulation.state().companies[kPlayer.index()].cash;
    simulation.tick();
    return before - simulation.state().companies[kPlayer.index()].cash;
}

sim::Money runningCost(const sim::Content& content, const sim::Simulation& simulation,
                       sim::MineState as) {
    const sim::WorldState& state = simulation.state();
    return sim::mineRunningCost(content, state, state.companies[kPlayer.index()], state.mines[0],
                                as);
}

sim::Money margin(const sim::Content& content, const sim::WorldState& state,
                  const std::vector<sim::Money>& prices) {
    return sim::mineDailyMargin(content, state, state.companies[kPlayer.index()], state.mines[0],
                                prices);
}

sim::Money breakEven(const sim::Content& content, const sim::WorldState& state,
                     const std::vector<sim::Money>& prices) {
    return sim::mineBreakEvenPrice(content, state, state.companies[kPlayer.index()], state.mines[0],
                                   prices);
}

}  // namespace

TEST_CASE("A single-ore mine pays that ore's running cost", "[mines][spec008]") {
    const sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    REQUIRE(simulation.state().mines[0].lastOutputTonnes[kIron.index()] == 400);

    // Iron's 8,000, though copper (12,000) is unlocked and dearer: the mine
    // does not dig copper.
    CHECK(runningCost(content, simulation, sim::MineState::Operating) ==
          sim::Money::fromUnits(8'000));
    CHECK(dayCharge(simulation) == sim::Money::fromUnits(8'000));
}

TEST_CASE("An ore's running cost quote is what a mine producing it pays", "[mines][spec008]") {
    const sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    const sim::WorldState& state = simulation.state();
    const sim::Company& player = state.companies[kPlayer.index()];

    // The ore's own cost, whether or not the company can dig it today.
    CHECK(sim::mineOpexQuote(content, state, player, kCopper) == sim::Money::fromUnits(12'000));
    CHECK(sim::mineOpexQuote(content, state, player, kLithium) == sim::Money::fromUnits(20'000));
    // The same figure an operating mine of that ore is charged.
    CHECK(sim::mineOpexQuote(content, state, player, kIron) ==
          runningCost(content, simulation, sim::MineState::Operating));
    // An ore the content does not have costs nothing.
    CHECK(sim::mineOpexQuote(content, state, player, sim::ResourceId(99)) == sim::Money::zero());
    CHECK(sim::mineOpexQuote(content, state, player, sim::ResourceId()) == sim::Money::zero());
}

TEST_CASE("An ore's running cost quote carries the event cost multiplier", "[mines][spec008]") {
    sim::Content content = costedContent();
    content.events[0].dailyWeight = sim::Fixed::one();
    content.events[0].minYear = 1;
    content.events[0].effects.push_back(
        {sim::EventEffectType::OpexMult, sim::ResourceId(), sim::Fixed::fromRatio(3, 2)});
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick();  // the event is active from day 0
    const sim::WorldState& state = simulation.state();

    CHECK(sim::mineOpexQuote(content, state, state.companies[kPlayer.index()], kCopper) ==
          sim::Money::fromUnits(18'000));
}

TEST_CASE("A single-ore mine's margin and break-even follow its output", "[mines][spec008]") {
    sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    const sim::WorldState& state = simulation.state();
    REQUIRE(state.mines[0].lastOutputTonnes[kIron.index()] == 400);

    // 400 t at $60 less 8,000 a day; zero margin at 8,000 / 400 = $20.
    CHECK(margin(content, state, basePrices()) == sim::Money::fromUnits(16'000));
    CHECK(breakEven(content, state, basePrices()) == sim::Money::fromUnits(20));
    std::vector<sim::Money> atBreakEven = basePrices();
    atBreakEven[kIron.index()] = sim::Money::fromUnits(20);
    CHECK(margin(content, state, atBreakEven) == sim::Money::zero());
    // Below break-even the margin is a loss.
    atBreakEven[kIron.index()] = sim::Money::fromUnits(15);
    CHECK(margin(content, state, atBreakEven) == sim::Money::fromUnits(-2'000));

    // A cost that does not divide evenly rounds up to the next whole cent:
    // 800,001 cents / 400 t = 2000.0025 cents.
    content.resources[kIron.index()].mineOpexPerDay = sim::Money::fromCents(800'001);
    CHECK(breakEven(content, state, basePrices()) == sim::Money::fromCents(2'001));
}

TEST_CASE("A two-ore mine pays the dearer cost once and earns from both", "[mines][spec008]") {
    const sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000), deposit(kCopper, 1'000'000)});
    const sim::WorldState& state = simulation.state();
    REQUIRE(state.mines[0].lastOutputTonnes[kIron.index()] == 400);
    REQUIRE(state.mines[0].lastOutputTonnes[kCopper.index()] == 60);

    // The higher of 8,000 and 12,000, not their sum.
    CHECK(runningCost(content, simulation, sim::MineState::Operating) ==
          sim::Money::fromUnits(12'000));
    // 400 x $60 + 60 x $900 - 12,000.
    CHECK(margin(content, state, basePrices()) == sim::Money::fromUnits(66'000));
    // Copper earns most (54,000 against 24,000) and iron alone covers the
    // cost, so copper could fall to nothing: break-even zero.
    CHECK(breakEven(content, state, basePrices()) == sim::Money::zero());

    // Iron at $10 brings in 4,000; copper must find the other 8,000 from
    // 60 t: $133.33 and a third, rounded up.
    std::vector<sim::Money> cheapIron = basePrices();
    cheapIron[kIron.index()] = sim::Money::fromUnits(10);
    CHECK(breakEven(content, state, cheapIron) == sim::Money::fromCents(13'334));

    // With copper worthless iron is the principal ore: 12,000 / 400 t.
    std::vector<sim::Money> noCopper = basePrices();
    noCopper[kCopper.index()] = sim::Money::zero();
    CHECK(breakEven(content, state, noCopper) == sim::Money::fromUnits(30));

    CHECK(dayCharge(simulation) == sim::Money::fromUnits(12'000));
}

TEST_CASE("A mine's running cost drops when its dearer ore runs out", "[mines][spec008]") {
    const sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    // 100 t of copper: 60 on the first day, the last 40 on the second.
    openMine(simulation, content, {deposit(kIron, 1'000'000), deposit(kCopper, 100)});

    CHECK(dayCharge(simulation) == sim::Money::fromUnits(12'000));  // the day copper ends
    REQUIRE(simulation.state().mines[0].lastOutputTonnes[kCopper.index()] == 40);
    CHECK(dayCharge(simulation) == sim::Money::fromUnits(8'000));  // iron only
    const sim::WorldState& state = simulation.state();
    REQUIRE(state.mines[0].lastOutputTonnes[kCopper.index()] == 0);
    CHECK(runningCost(content, simulation, sim::MineState::Operating) ==
          sim::Money::fromUnits(8'000));
    CHECK(margin(content, state, basePrices()) ==
          sim::Money::fromUnits(60) * state.mines[0].lastOutputTonnes[kIron.index()] -
              sim::Money::fromUnits(8'000));
}

TEST_CASE("A locked ore in the ground does not set the running cost", "[mines][spec008]") {
    const sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000), deposit(kLithium, 5'000)});

    // Lithium (20,000) is under the mine but locked: iron's cost applies.
    CHECK(runningCost(content, simulation, sim::MineState::Operating) ==
          sim::Money::fromUnits(8'000));
    CHECK(dayCharge(simulation) == sim::Money::fromUnits(8'000));

    // Unlocking it starts extraction, and the cost follows the output.
    sim::WorldState edited = simulation.state();
    edited.companies[kPlayer.index()].completedTechs = {sim::TechId(0), sim::TechId(1)};
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    CHECK(dayCharge(simulation) == sim::Money::fromUnits(20'000));
    REQUIRE(simulation.state().mines[0].lastOutputTonnes[kLithium.index()] == 5);
    CHECK(runningCost(content, simulation, sim::MineState::Operating) ==
          sim::Money::fromUnits(20'000));
}

TEST_CASE("A mine with no output yet is costed on what its owner can extract", "[mines][spec008]") {
    const sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    // Only iron in the ground. The cost must not say so.
    orderMine(simulation, content, {deposit(kIron, 1'000'000)});
    simulation.tick();
    const sim::WorldState& state = simulation.state();
    REQUIRE(state.mines[0].state == sim::MineState::Building);

    // Nothing is charged while it is being built.
    CHECK(runningCost(content, simulation, sim::MineState::Building) == sim::Money::zero());
    CHECK(dayCharge(simulation) == sim::Money::zero());
    // Asked what it would cost to run: the dearest ore the owner has
    // unlocked (copper), not locked lithium and not the plot's true ore.
    CHECK(runningCost(content, simulation, sim::MineState::Operating) ==
          sim::Money::fromUnits(12'000));
    CHECK(margin(content, state, basePrices()) == sim::Money::fromUnits(-12'000));
    CHECK(breakEven(content, state, basePrices()) == sim::Money::zero());
}

TEST_CASE("An operating mine that digs nothing pays the dearest unlocked cost",
          "[mines][spec008]") {
    const sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    // Only locked ore: the mine opens, extracts nothing and stays open.
    openMine(simulation, content, {deposit(kLithium, 5'000)});
    CHECK(dayCharge(simulation) == sim::Money::fromUnits(12'000));
    CHECK(breakEven(content, simulation.state(), basePrices()) == sim::Money::zero());
}

TEST_CASE("Depleted and closed mines cost nothing", "[mines][spec008]") {
    const sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    CHECK(runningCost(content, simulation, sim::MineState::Depleted) == sim::Money::zero());
    CHECK(runningCost(content, simulation, sim::MineState::Closed) == sim::Money::zero());
}

TEST_CASE("A mothballed mine pays the mothball share and is valued as if reopened",
          "[mines][spec008]") {
    const sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000), deposit(kCopper, 1'000'000)});
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    simulation.tick();
    const sim::WorldState& state = simulation.state();
    REQUIRE(state.mines[0].state == sim::MineState::Mothballed);
    // Its last operating day's output is kept, so its cost ores are too.
    REQUIRE(state.mines[0].lastOutputTonnes[kIron.index()] == 400);
    REQUIRE(state.mines[0].lastOutputTonnes[kCopper.index()] == 60);

    // 3/20 of the two-ore cost of 12,000.
    CHECK(runningCost(content, simulation, sim::MineState::Mothballed) ==
          sim::Money::fromUnits(1'800));
    CHECK(dayCharge(simulation) == sim::Money::fromUnits(1'800));
    // Margin and break-even use the operating cost: what reopening earns.
    CHECK(runningCost(content, simulation, sim::MineState::Operating) ==
          sim::Money::fromUnits(12'000));
    CHECK(margin(content, state, basePrices()) == sim::Money::fromUnits(66'000));
    std::vector<sim::Money> cheapIron = basePrices();
    cheapIron[kIron.index()] = sim::Money::fromUnits(10);
    CHECK(breakEven(content, state, cheapIron) == sim::Money::fromCents(13'334));
}

TEST_CASE("Research and event multipliers scale a mine's running cost", "[mines][spec008]") {
    sim::Content content = costedContent();
    // Tech 0 cuts running costs by a tenth; the event, certain from day 0
    // and outlasting the test, raises them by half.
    content.techs[0].effects.push_back(
        {sim::TechEffectType::OpexMult, sim::Fixed::fromRatio(9, 10), sim::ResourceId()});
    content.events[0].dailyWeight = sim::Fixed::one();
    content.events[0].minYear = 1;
    content.events[0].durationDays = 3'600;
    content.events[0].effects.push_back(
        {sim::EventEffectType::OpexMult, sim::ResourceId(), sim::Fixed::fromRatio(3, 2)});
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    REQUIRE(sim::eventMultiplier(content, simulation.state(), sim::EventEffectType::OpexMult) ==
            sim::Fixed::fromRatio(3, 2));

    // Event only: 8,000 x 1.5.
    CHECK(runningCost(content, simulation, sim::MineState::Operating) ==
          sim::Money::fromUnits(12'000));
    CHECK(dayCharge(simulation) == sim::Money::fromUnits(12'000));

    // With the research as well: 8,000 x 0.9 x 1.5.
    sim::WorldState edited = simulation.state();
    edited.companies[kPlayer.index()].completedTechs = {sim::TechId(0)};
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    CHECK(runningCost(content, simulation, sim::MineState::Operating) ==
          sim::Money::fromUnits(10'800));
    CHECK(dayCharge(simulation) == sim::Money::fromUnits(10'800));
    // The margin pays the multiplied cost: 24,000 - 10,800; break-even $27.
    const sim::WorldState& state = simulation.state();
    CHECK(margin(content, state, basePrices()) ==
          sim::Money::fromUnits(60) * state.mines[0].lastOutputTonnes[kIron.index()] -
              sim::Money::fromUnits(10'800));
    // And the mothball share applies after both: 10,800 x 3/20.
    CHECK(runningCost(content, simulation, sim::MineState::Mothballed) ==
          sim::Money::fromUnits(1'620));
}

TEST_CASE("Mine margin and break-even saturate instead of overflowing", "[mines][spec008]") {
    sim::Content content = costedContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000), deposit(kCopper, 1'000'000)});
    sim::WorldState state = simulation.state();
    const std::int64_t huge = std::numeric_limits<std::int64_t>::max();
    const sim::Money cap = sim::Money::fromCents(sim::kMaxCashCents);

    // Revenue past the cash bound stops at it.
    state.mines[0].lastOutputTonnes = {huge, huge, 0};
    const std::vector<sim::Money> hugePrices(3, sim::Money::fromCents(huge));
    CHECK(margin(content, state, hugePrices) == cap - sim::Money::fromUnits(12'000));
    CHECK(breakEven(content, state, hugePrices) == sim::Money::zero());

    // So does a cost: the margin stops at the bound below zero.
    content.resources[kCopper.index()].mineOpexPerDay = sim::Money::fromCents(huge);
    state.mines[0].lastOutputTonnes = {1, 1, 0};
    CHECK(runningCost(content, simulation, sim::MineState::Operating) == cap);
    const std::vector<sim::Money> none(3, sim::Money::zero());
    CHECK(margin(content, state, none) == -cap);
    CHECK(breakEven(content, state, none) == cap);

    // A price list shorter than the ore list prices the missing ores at
    // nothing; a negative price or tonnage counts as nothing.
    state.mines[0].lastOutputTonnes = {400, -60, 0};
    content.resources[kCopper.index()].mineOpexPerDay = sim::Money::fromUnits(12'000);
    const std::vector<sim::Money> ironOnly = {sim::Money::fromUnits(60)};
    CHECK(margin(content, state, ironOnly) == sim::Money::fromUnits(16'000));
    CHECK(breakEven(content, state, ironOnly) == sim::Money::fromUnits(20));
    const std::vector<sim::Money> negative = {sim::Money::fromUnits(-60)};
    CHECK(margin(content, state, negative) == sim::Money::fromUnits(-8'000));
    CHECK(breakEven(content, state, negative) == sim::Money::fromUnits(20));
}

// Spec 008: consecutive losing days per mine (the only new saved state).

namespace {

// Iron at 100,000 a day: 400 t breaks even at exactly $250, far above the
// fixture's $60, so an iron mine loses money every day at market prices.
sim::Content losingContent() {
    sim::Content c = costedContent();
    c.resources[kIron.index()].mineOpexPerDay = sim::Money::fromUnits(100'000);
    return c;
}

// Sets today's iron price through a save edit. The mines step runs before
// the market moves, so the next tick's counter sees exactly this price.
void setIronPrice(sim::Simulation& simulation, const sim::Content& content, sim::Money price) {
    sim::WorldState edited = simulation.state();
    sim::Market& market = edited.markets[kIron.index()];
    market.price = price;
    market.priceHistory.back() = price;
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

// Marks mine 0's loss as already announced, as the losing-mine notice does.
void markLossAnnounced(sim::Simulation& simulation, const sim::Content& content) {
    sim::WorldState edited = simulation.state();
    edited.mines[0].lossAnnounced = 1;
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

}  // namespace

TEST_CASE("A mine running at a loss counts its consecutive losing days", "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    const sim::Mine& mine = simulation.state().mines[0];
    REQUIRE(mine.lastOutputTonnes[kIron.index()] == 400);

    // Its first operating day already lost money.
    CHECK(mine.lossDays == 1);
    simulation.tick(3);
    CHECK(simulation.state().mines[0].lossDays == 4);
    // The count runs on past the notice threshold, which marks it announced.
    simulation.tick(content.balance.mineLossNoticeDays);
    CHECK(simulation.state().mines[0].lossDays == 4 + content.balance.mineLossNoticeDays);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);
}

TEST_CASE("A mine that has not opened or is making money has no losing days", "[mines][spec008]") {
    const sim::Content content = costedContent();  // iron breaks even at $20
    sim::Simulation simulation(content, 1, test::setup());
    orderMine(simulation, content, {deposit(kIron, 1'000'000)});
    simulation.tick(content.balance.mineBuildDays);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Building);
    CHECK(simulation.state().mines[0].lossDays == 0);

    simulation.tick(5);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Operating);
    CHECK(simulation.state().mines[0].lossDays == 0);
}

TEST_CASE("A profitable day clears the losing days and the announcement", "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    simulation.tick(2);
    REQUIRE(simulation.state().mines[0].lossDays == 3);
    markLossAnnounced(simulation, content);

    // Still losing: the announcement stands while the run of days goes on.
    simulation.tick();
    CHECK(simulation.state().mines[0].lossDays == 4);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);

    setIronPrice(simulation, content, sim::Money::fromUnits(300));
    simulation.tick();
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);
    // The next losing day starts a new count.
    setIronPrice(simulation, content, sim::Money::fromUnits(60));
    simulation.tick();
    CHECK(simulation.state().mines[0].lossDays == 1);
}

TEST_CASE("A day exactly at break-even is not a losing day", "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    orderMine(simulation, content, {deposit(kIron, 1'000'000)});
    simulation.tick(content.balance.mineBuildDays);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Building);

    // The mine opens next tick with a full 400 t: 100,000 a day is $250 a tonne.
    SECTION("at break-even") {
        setIronPrice(simulation, content, sim::Money::fromUnits(250));
        simulation.tick();
        REQUIRE(simulation.state().mines[0].lastOutputTonnes[kIron.index()] == 400);
        CHECK(simulation.state().mines[0].lossDays == 0);
    }
    SECTION("a cent under") {
        setIronPrice(simulation, content, sim::Money::fromCents(24'999));
        simulation.tick();
        REQUIRE(simulation.state().mines[0].lastOutputTonnes[kIron.index()] == 400);
        CHECK(simulation.state().mines[0].lossDays == 1);
    }
}

TEST_CASE("The losing days use the price as the mines step sees it", "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    REQUIRE(simulation.state().mines[0].lossDays == 1);

    // Profitable at the price the day opens with; the market then pulls the
    // price back under break-even in the same tick, which does not count.
    setIronPrice(simulation, content, sim::Money::fromUnits(251));
    simulation.tick();
    REQUIRE(simulation.state().markets[kIron.index()].price < sim::Money::fromUnits(250));
    CHECK(simulation.state().mines[0].lossDays == 0);
    simulation.tick();
    CHECK(simulation.state().mines[0].lossDays == 1);
}

TEST_CASE("Mothballing a losing mine clears its losing days", "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    simulation.tick(2);
    REQUIRE(simulation.state().mines[0].lossDays == 3);
    markLossAnnounced(simulation, content);

    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    simulation.tick();
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Mothballed);
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);
    // It would still lose money if reopened, but an idle mine is not counted.
    simulation.tick(3);
    CHECK(simulation.state().mines[0].lossDays == 0);

    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 1});
    simulation.tick();
    CHECK(simulation.state().mines[0].lossDays == 1);
}

TEST_CASE("A losing mine that runs out has no losing days", "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000)});
    REQUIRE(simulation.state().mines[0].lossDays == 1);
    markLossAnnounced(simulation, content);

    simulation.tick(10);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Depleted);
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);
}

TEST_CASE("A mine with no output to price has no losing days", "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    // Only locked ore: the mine operates and pays its cost but digs nothing.
    openMine(simulation, content, {deposit(kLithium, 5'000)});
    simulation.tick(4);
    const sim::WorldState& state = simulation.state();
    REQUIRE(state.mines[0].state == sim::MineState::Operating);
    REQUIRE(std::ranges::all_of(state.mines[0].lastOutputTonnes,
                                [](std::int64_t tonnes) { return tonnes == 0; }));
    // The bare cost would read as a loss; there is no price to blame for it.
    std::vector<sim::Money> prices;
    prices.reserve(state.markets.size());
    for (const sim::Market& market : state.markets) {
        prices.push_back(market.price);
    }
    REQUIRE(margin(content, state, prices) < sim::Money::zero());
    CHECK(state.mines[0].lossDays == 0);
}

TEST_CASE("A mine's losing days survive save and load and are part of the state hash",
          "[mines][spec008][save]") {
    const sim::Content content = losingContent();
    sim::Simulation original(content, 5, test::setup());
    openMine(original, content, {deposit(kIron, 1'000'000)});
    original.tick(2);
    markLossAnnounced(original, content);
    REQUIRE(original.state().mines[0].lossDays == 3);

    sim::Simulation restored(content, 6, test::setup());
    REQUIRE(restored.load(original.save()));
    CHECK(restored.state().mines[0].lossDays == 3);
    CHECK(restored.state().mines[0].lossAnnounced == 1);
    CHECK(restored.stateHash() == original.stateHash());
    original.tick(4);
    restored.tick(4);
    CHECK(restored.state().mines[0].lossDays == 7);
    CHECK(restored.stateHash() == original.stateHash());

    // Either field alone moves the hash.
    sim::WorldState edited = original.state();
    edited.mines[0].lossDays += 1;
    CHECK(sim::hashWorld(edited) != original.stateHash());
    edited = original.state();
    edited.mines[0].lossAnnounced = 0;
    CHECK(sim::hashWorld(edited) != original.stateHash());
}

TEST_CASE("Saves with impossible losing-day fields are rejected", "[mines][spec008][save]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    orderMine(simulation, content, {deposit(kIron, 1'000'000)});
    simulation.tick();
    const auto loads = [&](std::uint32_t lossDays, std::uint8_t lossAnnounced) {
        sim::WorldState edited = simulation.state();
        edited.mines[0].lossDays = lossDays;
        edited.mines[0].lossAnnounced = lossAnnounced;
        sim::Simulation scratch(content, 2, test::setup());
        return scratch.load(sim::saveGame(edited, {}, sim::hashContent(content)));
    };

    // A mine still being built has never had a losing day.
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Building);
    CHECK(loads(0, 0));
    CHECK_FALSE(loads(1, 0));
    CHECK_FALSE(loads(0, 2));

    // Working, any count is possible; the announcement is a flag.
    simulation.tick(content.balance.mineBuildDays + 2);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Operating);
    REQUIRE(simulation.state().mines[0].lossDays == 3);
    CHECK(loads(3, 0));
    CHECK(loads(3, 1));
    CHECK(loads(0, 1));  // the flag alone is not refused
    CHECK(loads(std::numeric_limits<std::uint32_t>::max(), 1));
    CHECK_FALSE(loads(3, 2));
    CHECK_FALSE(loads(3, 255));
    CHECK_FALSE(loads(0, 2));

    // Mothballed: the mines step cleared the count the day it stopped.
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    simulation.tick();
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Mothballed);
    CHECK(loads(0, 0));
    CHECK_FALSE(loads(1, 0));
    CHECK_FALSE(loads(3, 1));
    CHECK_FALSE(loads(0, 2));
}

TEST_CASE("A save with losing days on a depleted mine is rejected", "[mines][spec008][save]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000)});
    simulation.tick(10);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Depleted);
    CHECK(simulation.load(simulation.save()));

    sim::WorldState edited = simulation.state();
    edited.mines[0].lossDays = 1;
    CHECK_FALSE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

TEST_CASE("A save made the day a losing mine's owner goes bankrupt still loads",
          "[mines][spec008][save]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000'000)});
    grantCash(simulation, content, kPlayer, sim::Money::fromUnits(-1));

    // Bankruptcy is declared in the finance step, after the mines step has
    // counted the day: the mine ends that tick closed with its count intact.
    const sim::Day limit = simulation.state().day + 400;
    while (simulation.state().companies[kPlayer.index()].bankrupt == 0) {
        REQUIRE(simulation.state().day < limit);
        simulation.tick();
        CHECK(simulation.load(simulation.save()));
    }
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Closed);
    CHECK(simulation.state().mines[0].lossDays > 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);

    sim::Simulation restored(content, 2, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.stateHash() == simulation.stateHash());
    // The next mines step clears it, as for any mine that has stopped.
    restored.tick();
    CHECK(restored.state().mines[0].lossDays == 0);
    CHECK(restored.state().mines[0].lossAnnounced == 0);
    CHECK(restored.load(restored.save()));
}

// Spec 008: the losing-mine notice, raised once per run of losing days.

namespace {

std::vector<sim::Notice> losingNotices(const sim::TickReport& report) {
    std::vector<sim::Notice> found;
    std::copy_if(report.notices.begin(), report.notices.end(), std::back_inserter(found),
                 [](const sim::Notice& n) { return n.kind == sim::NoticeKind::MineLosingMoney; });
    return found;
}

// Ticks `days` days and returns how many losing-mine notices they raised.
std::size_t losingNoticesOver(sim::Simulation& simulation, sim::Day days) {
    std::size_t count = 0;
    for (sim::Day d = 0; d < days; ++d) {
        count += losingNotices(simulation.tick()).size();
    }
    return count;
}

std::vector<sim::Money> marketPrices(const sim::WorldState& state) {
    std::vector<sim::Money> prices;
    prices.reserve(state.markets.size());
    for (const sim::Market& market : state.markets) {
        prices.push_back(market.price);
    }
    return prices;
}

// A losing iron mine one day short of its notice: opened on day one of its
// loss and run until lossDays is the threshold less one, nothing said yet.
// With `ironTonnes` the deposit can be made kSteadyIronTonnes (below).
void runToEveOfNotice(sim::Simulation& simulation, const sim::Content& content,
                      std::int64_t ironTonnes = 1'000'000) {
    openMine(simulation, content, {deposit(kIron, ironTonnes)});
    REQUIRE(content.balance.mineLossNoticeDays >= 2);
    REQUIRE(losingNoticesOver(simulation, content.balance.mineLossNoticeDays - 2) == 0);
    REQUIRE(simulation.state().mines[0].lossDays == content.balance.mineLossNoticeDays - 1);
    REQUIRE(simulation.state().mines[0].lossAnnounced == 0);
}

// A deposit too large to decline over a test: after its first day the mine
// digs a steady 399 t (the depletion factor rounds down from one), which
// steadyLosingContent's iron cost of 99,750 a day prices at exactly $250.
constexpr std::int64_t kSteadyIronTonnes = 1'000'000'000'000;
constexpr std::int64_t kSteadyIronOutput = 399;

sim::Content steadyLosingContent() {
    sim::Content c = costedContent();
    c.resources[kIron.index()].mineOpexPerDay = sim::Money::fromUnits(250 * kSteadyIronOutput);
    return c;
}

// One day with iron at exactly the steady mine's break-even price. Returns
// the losing-mine notices it raised.
std::size_t breakEvenDay(sim::Simulation& simulation, const sim::Content& content) {
    setIronPrice(simulation, content, sim::Money::fromUnits(250));
    const std::vector<sim::Money> seen = marketPrices(simulation.state());
    const std::size_t raised = losingNoticesOver(simulation, 1);
    const sim::WorldState& state = simulation.state();
    REQUIRE(state.mines[0].lastOutputTonnes[kIron.index()] == kSteadyIronOutput);
    REQUIRE(margin(content, state, seen) == sim::Money::zero());
    return raised;
}

}  // namespace

TEST_CASE("The losing-mine notice has a stable name and is private", "[mines][spec008]") {
    CHECK(std::string_view(sim::noticeKindName(sim::NoticeKind::MineLosingMoney)) ==
          "mine_losing_money");
    CHECK_FALSE(sim::noticeIsPublic(sim::NoticeKind::MineLosingMoney));
}

TEST_CASE("A mine is announced on the day its losing days reach the threshold",
          "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    // Never on fewer days: runToEveOfNotice requires that none was raised.
    runToEveOfNotice(simulation, content);
    const sim::PlotId plot = simulation.state().mines[0].plot;
    const std::vector<sim::Money> seen = marketPrices(simulation.state());

    const std::vector<sim::Notice> raised = losingNotices(simulation.tick());
    const sim::WorldState& state = simulation.state();
    REQUIRE(raised.size() == 1);
    const sim::Notice& notice = raised[0];
    CHECK(notice.company == kPlayer);
    CHECK(notice.plot == plot);
    CHECK(notice.mine == sim::MineId(0));
    CHECK(notice.tonnes == content.balance.mineLossNoticeDays);
    // The day's loss, as a positive figure, at the prices the mines step saw.
    CHECK(notice.amount > sim::Money::zero());
    CHECK(notice.amount == -margin(content, state, seen));
    CHECK(state.mines[0].lossDays == content.balance.mineLossNoticeDays);
    CHECK(state.mines[0].lossAnnounced == 1);

    // Private to the owner.
    CHECK(sim::noticeVisibleTo(notice, kPlayer));
    CHECK_FALSE(sim::noticeVisibleTo(notice, kRival));
    CHECK(losingNotices({state.day, simulation.reportFor(kPlayer)}).size() == 1);
    CHECK(losingNotices({state.day, simulation.reportFor(kRival)}).empty());
}

TEST_CASE("A losing mine is announced once until it has recovered and lost again",
          "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    runToEveOfNotice(simulation, content);
    const sim::Day threshold = content.balance.mineLossNoticeDays;
    REQUIRE(losingNoticesOver(simulation, 1) == 1);

    // The loss goes on: nothing more is said, however long it lasts.
    CHECK(losingNoticesOver(simulation, 3 * threshold) == 0);
    CHECK(simulation.state().mines[0].lossDays == 4 * threshold);

    // One day at a profit ends the run; the next run is announced afresh,
    // and again only when it has lasted the full count.
    setIronPrice(simulation, content, sim::Money::fromUnits(300));
    CHECK(losingNoticesOver(simulation, 1) == 0);
    REQUIRE(simulation.state().mines[0].lossAnnounced == 0);
    setIronPrice(simulation, content, sim::Money::fromUnits(60));
    CHECK(losingNoticesOver(simulation, threshold - 1) == 0);
    REQUIRE(simulation.state().mines[0].lossDays == threshold - 1);
    CHECK(losingNoticesOver(simulation, 1) == 1);
    CHECK(losingNoticesOver(simulation, threshold) == 0);
}

TEST_CASE("A break-even day between two runs of losing days does not repeat the notice",
          "[mines][spec008]") {
    // Spec 010 (BR-014): a break-even day is a day that is not a losing day,
    // so it counts toward the re-arm run like a profitable one. With a
    // re-arm run of two days, one of them is not enough.
    sim::Content content = steadyLosingContent();
    content.balance.mineLossRearmDays = 2;
    sim::Simulation simulation(content, 1, test::setup());
    runToEveOfNotice(simulation, content, kSteadyIronTonnes);
    const sim::Day threshold = content.balance.mineLossNoticeDays;
    REQUIRE(losingNoticesOver(simulation, 1) == 1);

    // One day at exactly nothing: not a losing day, and not yet a recovery.
    CHECK(breakEvenDay(simulation, content) == 0);
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);
    CHECK(simulation.state().mines[0].lossClearDays == 1);

    // The loss resumes and lasts the full count again: the mine never
    // completed the re-arm run, so nothing more is said.
    setIronPrice(simulation, content, sim::Money::fromUnits(60));
    CHECK(losingNoticesOver(simulation, threshold) == 0);
    CHECK(simulation.state().mines[0].lossDays == threshold);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);
    CHECK(simulation.state().mines[0].lossClearDays == 0);
    CHECK(losingNoticesOver(simulation, threshold) == 0);

    // A break-even day and a profitable day in a row are the recovery; the
    // run after them is announced.
    CHECK(breakEvenDay(simulation, content) == 0);
    setIronPrice(simulation, content, sim::Money::fromUnits(300));
    CHECK(losingNoticesOver(simulation, 1) == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);
    CHECK(simulation.state().mines[0].lossClearDays == 0);
    setIronPrice(simulation, content, sim::Money::fromUnits(60));
    CHECK(losingNoticesOver(simulation, threshold - 1) == 0);
    CHECK(losingNoticesOver(simulation, 1) == 1);
}

TEST_CASE("A break-even day before any notice does not hold one back", "[mines][spec008]") {
    const sim::Content content = steadyLosingContent();
    sim::Simulation simulation(content, 1, test::setup());
    runToEveOfNotice(simulation, content, kSteadyIronTonnes);
    const sim::Day threshold = content.balance.mineLossNoticeDays;

    // The count starts again, and the first full run after it is announced.
    CHECK(breakEvenDay(simulation, content) == 0);
    REQUIRE(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);
    setIronPrice(simulation, content, sim::Money::fromUnits(60));
    CHECK(losingNoticesOver(simulation, threshold - 1) == 0);
    CHECK(losingNoticesOver(simulation, 1) == 1);
}

// Spec 010 (BR-014, playtest 4 finding 2): a day is a losing day for the
// notice only when the loss is at least a set share of the mine's running
// cost, and after a notice the mine must have a set run of days that are not
// losing days before it can be told again.

namespace {

// The steady mine (399 t a day at a cost of $99,750) with the two numbers
// set. At a share of 5% the smallest loss that counts is $4,987.50: iron at
// $237.50 loses exactly that, and at $237.51 loses $3.99 less.
sim::Content noticeContent(sim::Fixed minShare, std::uint32_t rearmDays) {
    sim::Content c = steadyLosingContent();
    c.balance.mineLossNoticeMinShare = minShare;
    c.balance.mineLossRearmDays = rearmDays;
    return c;
}

const sim::Fixed kFivePercent = sim::Fixed::fromRatio(5, 100);
const sim::Money kIronAtTheShare = sim::Money::fromCents(23'750);
const sim::Money kIronUnderTheShare = sim::Money::fromCents(23'751);
const sim::Money kLossAtTheShare = sim::Money::fromCents(498'750);

// `days` days with iron at `price` each morning (the market moves it again
// every tick). Returns the losing-mine notices they raised.
std::size_t noticesAtIronPrice(sim::Simulation& simulation, const sim::Content& content,
                               sim::Money price, sim::Day days) {
    std::size_t raised = 0;
    for (sim::Day d = 0; d < days; ++d) {
        setIronPrice(simulation, content, price);
        raised += losingNoticesOver(simulation, 1);
        REQUIRE(simulation.state().mines[0].lastOutputTonnes[kIron.index()] == kSteadyIronOutput);
    }
    return raised;
}

// The steady mine on the day its notice is raised, at the market's price.
void runToNotice(sim::Simulation& simulation, const sim::Content& content) {
    runToEveOfNotice(simulation, content, kSteadyIronTonnes);
    REQUIRE(losingNoticesOver(simulation, 1) == 1);
    REQUIRE(simulation.state().mines[0].lossAnnounced == 1);
    REQUIRE(simulation.state().mines[0].lossClearDays == 0);
}

}  // namespace

TEST_CASE("A mine losing less than the set share of its running cost is never announced",
          "[mines][spec010]") {
    const sim::Content content = noticeContent(kFivePercent, 30);
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, kSteadyIronTonnes)});
    const sim::Day threshold = content.balance.mineLossNoticeDays;

    // It does lose money at this price, a little under the share.
    setIronPrice(simulation, content, kIronUnderTheShare);
    const std::vector<sim::Money> seen = marketPrices(simulation.state());
    CHECK(losingNoticesOver(simulation, 1) == 0);
    const sim::Money loss = -margin(content, simulation.state(), seen);
    CHECK(loss > sim::Money::zero());
    CHECK(loss == kLossAtTheShare - sim::Money::fromCents(399));
    CHECK(simulation.state().mines[0].lossDays == 0);

    // However long it goes on.
    CHECK(noticesAtIronPrice(simulation, content, kIronUnderTheShare, 20 * threshold) == 0);
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);
    CHECK(simulation.state().mines[0].lossClearDays == 0);
}

TEST_CASE("A mine losing the set share of its running cost is announced once after the usual days",
          "[mines][spec010]") {
    const sim::Content content = noticeContent(kFivePercent, 30);
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, kSteadyIronTonnes)});
    const sim::Day threshold = content.balance.mineLossNoticeDays;
    REQUIRE(threshold >= 2);
    REQUIRE(simulation.state().mines[0].lossDays == 1);  // its first day, at the market's price

    // A loss of exactly the share counts.
    CHECK(noticesAtIronPrice(simulation, content, kIronAtTheShare, threshold - 2) == 0);
    CHECK(simulation.state().mines[0].lossDays == threshold - 1);
    setIronPrice(simulation, content, kIronAtTheShare);
    const std::vector<sim::Notice> raised = losingNotices(simulation.tick());
    REQUIRE(raised.size() == 1);
    CHECK(raised[0].amount == kLossAtTheShare);
    CHECK(raised[0].tonnes == threshold);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);

    CHECK(noticesAtIronPrice(simulation, content, kIronAtTheShare, 4 * threshold) == 0);
    CHECK(simulation.state().mines[0].lossDays == 5 * threshold);
}

TEST_CASE("A share of nothing counts every loss as a losing day", "[mines][spec010]") {
    const sim::Content content = noticeContent(sim::Fixed::zero(), 30);
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, kSteadyIronTonnes)});
    const sim::Day threshold = content.balance.mineLossNoticeDays;

    // A cent a tonne under break-even, and exactly at it.
    CHECK(noticesAtIronPrice(simulation, content, sim::Money::fromCents(24'999), 1) == 0);
    CHECK(simulation.state().mines[0].lossDays == 2);
    CHECK(noticesAtIronPrice(simulation, content, sim::Money::fromUnits(250), 1) == 0);
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(noticesAtIronPrice(simulation, content, sim::Money::fromCents(24'999), threshold) == 1);
}

TEST_CASE("One profitable day after a notice does not re-arm it", "[mines][spec010]") {
    const sim::Content content = noticeContent(sim::Fixed::zero(), 30);
    sim::Simulation simulation(content, 1, test::setup());
    runToNotice(simulation, content);
    const sim::Day threshold = content.balance.mineLossNoticeDays;

    CHECK(noticesAtIronPrice(simulation, content, sim::Money::fromUnits(300), 1) == 0);
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);
    CHECK(simulation.state().mines[0].lossClearDays == 1);

    // The loss resumes: the run of days starts again and nothing is said.
    setIronPrice(simulation, content, sim::Money::fromUnits(60));
    CHECK(losingNoticesOver(simulation, 1) == 0);
    CHECK(simulation.state().mines[0].lossDays == 1);
    CHECK(simulation.state().mines[0].lossClearDays == 0);
    CHECK(losingNoticesOver(simulation, 4 * threshold) == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);
}

TEST_CASE("A re-arm run broken by a losing day starts again", "[mines][spec010]") {
    const sim::Content content = noticeContent(sim::Fixed::zero(), 30);
    sim::Simulation simulation(content, 1, test::setup());
    runToNotice(simulation, content);
    const sim::Day threshold = content.balance.mineLossNoticeDays;
    const sim::Money profit = sim::Money::fromUnits(300);
    const sim::Money losing = sim::Money::fromUnits(60);

    // One day short of the run, a losing day, and one day short again.
    CHECK(noticesAtIronPrice(simulation, content, profit, 29) == 0);
    CHECK(simulation.state().mines[0].lossClearDays == 29);
    CHECK(noticesAtIronPrice(simulation, content, losing, 1) == 0);
    CHECK(simulation.state().mines[0].lossClearDays == 0);
    CHECK(noticesAtIronPrice(simulation, content, profit, 29) == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);
    CHECK(noticesAtIronPrice(simulation, content, losing, 2 * threshold) == 0);
}

TEST_CASE("After the full re-arm run a new run of losing days is announced", "[mines][spec010]") {
    const sim::Content content = noticeContent(sim::Fixed::zero(), 30);
    sim::Simulation simulation(content, 1, test::setup());
    runToNotice(simulation, content);
    const sim::Day threshold = content.balance.mineLossNoticeDays;
    const sim::Money losing = sim::Money::fromUnits(60);

    CHECK(noticesAtIronPrice(simulation, content, sim::Money::fromUnits(300), 29) == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);
    CHECK(simulation.state().mines[0].lossClearDays == 29);
    CHECK(noticesAtIronPrice(simulation, content, sim::Money::fromUnits(300), 1) == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);
    CHECK(simulation.state().mines[0].lossClearDays == 0);

    // Told again, and again only after the usual days.
    CHECK(noticesAtIronPrice(simulation, content, losing, threshold - 1) == 0);
    CHECK(noticesAtIronPrice(simulation, content, losing, 1) == 1);
    CHECK(noticesAtIronPrice(simulation, content, losing, 2 * threshold) == 0);
}

TEST_CASE("Days of loss under the share count toward the re-arm run", "[mines][spec010]") {
    const sim::Content content = noticeContent(kFivePercent, 3);
    sim::Simulation simulation(content, 1, test::setup());
    runToNotice(simulation, content);
    const sim::Day threshold = content.balance.mineLossNoticeDays;

    // A small loss, a break-even day and another small loss: none is a
    // losing day, so together they are the run.
    CHECK(noticesAtIronPrice(simulation, content, kIronUnderTheShare, 1) == 0);
    CHECK(noticesAtIronPrice(simulation, content, sim::Money::fromUnits(250), 1) == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 1);
    CHECK(simulation.state().mines[0].lossClearDays == 2);
    CHECK(noticesAtIronPrice(simulation, content, kIronUnderTheShare, 1) == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);

    CHECK(noticesAtIronPrice(simulation, content, kIronAtTheShare, threshold - 1) == 0);
    CHECK(noticesAtIronPrice(simulation, content, kIronAtTheShare, 1) == 1);
}

TEST_CASE("A mine mothballed during its re-arm run starts afresh when reopened",
          "[mines][spec010]") {
    const sim::Content content = noticeContent(sim::Fixed::zero(), 30);
    sim::Simulation simulation(content, 1, test::setup());
    runToNotice(simulation, content);
    const sim::Day threshold = content.balance.mineLossNoticeDays;
    CHECK(noticesAtIronPrice(simulation, content, sim::Money::fromUnits(300), 1) == 0);
    REQUIRE(simulation.state().mines[0].lossAnnounced == 1);
    REQUIRE(simulation.state().mines[0].lossClearDays == 1);

    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    CHECK(losingNoticesOver(simulation, 1) == 0);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Mothballed);
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);
    CHECK(simulation.state().mines[0].lossClearDays == 0);

    // No re-arm run was completed: stopping the mine ended the episode.
    // (The price is set first: loading a save drops an order not yet run.)
    setIronPrice(simulation, content, sim::Money::fromUnits(60));
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 1});
    CHECK(losingNoticesOver(simulation, threshold - 1) == 0);
    CHECK(losingNoticesOver(simulation, 1) == 1);
}

TEST_CASE("The re-arm count survives save and load and is part of the state hash",
          "[mines][spec010][save]") {
    const sim::Content content = noticeContent(sim::Fixed::zero(), 30);
    sim::Simulation original(content, 5, test::setup());
    runToNotice(original, content);
    CHECK(noticesAtIronPrice(original, content, sim::Money::fromUnits(300), 3) == 0);
    REQUIRE(original.state().mines[0].lossClearDays == 3);

    sim::Simulation restored(content, 6, test::setup());
    REQUIRE(restored.load(original.save()));
    CHECK(restored.state().mines[0].lossClearDays == 3);
    CHECK(restored.state().mines[0].lossAnnounced == 1);
    CHECK(restored.stateHash() == original.stateHash());

    sim::WorldState edited = original.state();
    edited.mines[0].lossClearDays += 1;
    CHECK(sim::hashWorld(edited) != original.stateHash());
}

TEST_CASE("Saves with an impossible re-arm count are rejected", "[mines][spec010][save]") {
    const sim::Content content = noticeContent(sim::Fixed::zero(), 30);
    sim::Simulation simulation(content, 1, test::setup());
    runToNotice(simulation, content);
    const auto loads = [&](std::uint32_t lossDays, std::uint8_t lossAnnounced,
                           std::uint32_t lossClearDays) {
        sim::WorldState edited = simulation.state();
        edited.mines[0].lossDays = lossDays;
        edited.mines[0].lossAnnounced = lossAnnounced;
        edited.mines[0].lossClearDays = lossClearDays;
        sim::Simulation scratch(content, 2, test::setup());
        return scratch.load(sim::saveGame(edited, {}, sim::hashContent(content)));
    };

    CHECK(loads(0, 1, 1));
    CHECK(loads(0, 1, 30));
    // Never above the re-arm number.
    CHECK_FALSE(loads(0, 1, 31));
    CHECK_FALSE(loads(0, 1, std::numeric_limits<std::uint32_t>::max()));
    // Counted only after a notice.
    CHECK_FALSE(loads(0, 0, 1));
    // A losing day ends the run, so the two counts are never both set.
    CHECK_FALSE(loads(1, 1, 1));
    CHECK(loads(1, 1, 0));
}

TEST_CASE("The losing-mine notice's share and re-arm days are part of the content fingerprint",
          "[mines][spec010][content]") {
    const sim::Content content = test::makeContent();
    sim::Content share = content;
    share.balance.mineLossNoticeMinShare = sim::Fixed::fromRatio(1, 100);
    sim::Content rearm = content;
    rearm.balance.mineLossRearmDays = 2;
    CHECK(sim::hashContent(share) != sim::hashContent(content));
    CHECK(sim::hashContent(rearm) != sim::hashContent(content));
    CHECK(sim::hashContent(share) != sim::hashContent(rearm));
}

TEST_CASE("A mothballed mine is never announced as losing money", "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    runToEveOfNotice(simulation, content);

    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    CHECK(losingNoticesOver(simulation, 3 * content.balance.mineLossNoticeDays) == 0);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Mothballed);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);

    // Reopened, it must lose for the full count again before it is said.
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 1});
    CHECK(losingNoticesOver(simulation, content.balance.mineLossNoticeDays - 1) == 0);
    CHECK(losingNoticesOver(simulation, 1) == 1);
}

TEST_CASE("A mine mothballed after its notice is announced afresh when reopened",
          "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    runToEveOfNotice(simulation, content);
    const sim::Day threshold = content.balance.mineLossNoticeDays;
    REQUIRE(losingNoticesOver(simulation, 1) == 1);

    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 0});
    CHECK(losingNoticesOver(simulation, 1) == 0);
    REQUIRE(simulation.state().mines[0].state == sim::MineState::Mothballed);
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);

    // Never profitable in between: stopping the mine ended the episode.
    simulation.submit(sim::SetMineState{kPlayer, sim::MineId(0), 1});
    CHECK(losingNoticesOver(simulation, threshold - 1) == 0);
    CHECK(losingNoticesOver(simulation, 1) == 1);
}

TEST_CASE("A bankrupt owner's mine is never announced as losing money", "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    runToEveOfNotice(simulation, content);

    // The owner as bankruptcy leaves it, the day before the notice was due.
    // A save holds a bankrupt company only with its mines closed, so that
    // is the one state of a bankrupt owner's mine the sim can be in.
    sim::WorldState edited = simulation.state();
    sim::Company& owner = edited.companies[kPlayer.index()];
    owner.bankrupt = 1;
    owner.monthsInsolvent = content.balance.bankruptcyGraceMonths + 1;
    owner.cash = sim::Money::fromUnits(-1);
    edited.mines[0].state = sim::MineState::Closed;
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    REQUIRE(simulation.state().mines[0].lossDays == content.balance.mineLossNoticeDays - 1);

    CHECK(losingNoticesOver(simulation, 3 * content.balance.mineLossNoticeDays) == 0);
    CHECK(simulation.state().mines[0].lossDays == 0);
    CHECK(simulation.state().mines[0].lossAnnounced == 0);
}

TEST_CASE("Several mines can each be announced as losing money on the same day",
          "[mines][spec008]") {
    const sim::Content content = losingContent();
    sim::Simulation simulation(content, 1, test::setup());
    openMine(simulation, content, {deposit(kIron, 1'000'000)});
    const sim::PlotId second = buyFirstListed(simulation, kPlayer);
    setDeposit(simulation, content, second, kIron, 1'000'000);
    simulation.submit(sim::BuildMine{kPlayer, second});
    simulation.tick(content.balance.mineBuildDays + 1);
    REQUIRE(simulation.state().mines.size() == 2);
    REQUIRE(simulation.state().mines[1].state == sim::MineState::Operating);

    // Both one day short of the notice, neither announced.
    sim::WorldState edited = simulation.state();
    for (sim::Mine& mine : edited.mines) {
        mine.lossDays = content.balance.mineLossNoticeDays - 1;
        mine.lossAnnounced = 0;
    }
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));

    const std::vector<sim::Notice> raised = losingNotices(simulation.tick());
    REQUIRE(raised.size() == 2);
    CHECK(raised[0].mine == sim::MineId(0));
    CHECK(raised[1].mine == sim::MineId(1));
    CHECK(raised[0].plot != raised[1].plot);
    CHECK(raised[1].plot == second);
    CHECK(losingNoticesOver(simulation, content.balance.mineLossNoticeDays) == 0);
}
