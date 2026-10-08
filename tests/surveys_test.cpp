#include "sim/surveys.h"

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
#include <cmath>
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

// Buys `plot` for the player by bidding the ask and running the auction.
void buyPlot(sim::Simulation& simulation, sim::CompanyId company, sim::PlotId plot) {
    const sim::Money ask = simulation.state().plots[plot.index()].askPrice;
    simulation.submit(sim::BidOnPlot{company, plot, ask});
    const sim::Day closes = simulation.state()
                                .auctions[simulation.state().plots[plot.index()].auction.index()]
                                .closesOnDay;
    while (simulation.state().day <= closes) {
        simulation.tick();
    }
    REQUIRE(simulation.state().plots[plot.index()].owner == company);
}

sim::PlotId firstListed(const sim::WorldState& state) {
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        if (state.plots[i].listing == sim::PlotListing::Listed) {
            return sim::PlotId(i);
        }
    }
    return sim::PlotId();
}

}  // namespace

TEST_CASE("Commissioning a survey charges the cost and completes after the delay", "[surveys]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money cash = simulation.state().companies[0].cash;

    simulation.submit(sim::CommissionSurvey{kPlayer, plot});
    const sim::TickReport& started = simulation.tick();
    CHECK(hasNotice(started.notices, sim::NoticeKind::SurveyStarted));
    CHECK(simulation.state().companies[0].cash == cash - content.balance.surveyCost);
    REQUIRE(simulation.state().surveys.size() == 1);
    CHECK(simulation.state().surveys[0].complete == 0);

    sim::CompanyView view = simulation.view(kPlayer);
    REQUIRE(view.surveys.size() == 1);
    CHECK(view.surveys[0].plot == plot);
    CHECK_FALSE(view.surveys[0].complete);
    CHECK(view.surveys[0].completesDay == content.balance.surveyDurationDays);
    CHECK(simulation.view(kRival).surveys.empty());

    while (simulation.state().day < content.balance.surveyDurationDays) {
        simulation.tick();
    }
    const sim::TickReport& done = simulation.tick();
    CHECK(hasNotice(done.notices, sim::NoticeKind::SurveyComplete));
    view = simulation.view(kPlayer);
    REQUIRE(view.surveys.size() == 1);
    CHECK(view.surveys[0].complete);
    CHECK(view.surveys[0].estimates.size() ==
          simulation.state().hidden.plotDeposits[plot.index()].deposits.size());
    for (const sim::SurveyEstimateView& e : view.surveys[0].estimates) {
        CHECK(e.tonnesEstimate > 0);
        CHECK(e.relativeError == content.balance.surveyBaseNoise);
    }
}

TEST_CASE("Survey commands are validated", "[surveys]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    buyPlot(simulation, kRival, plot);

    simulation.submit(sim::CommissionSurvey{kPlayer, plot});  // owned by rival
    simulation.submit(sim::CommissionSurvey{kPlayer, sim::PlotId(999)});
    simulation.submit(sim::CommissionSurvey{kRival, plot});  // fine: own plot
    simulation.submit(sim::CommissionSurvey{kRival, plot});  // duplicate while pending
    const sim::TickReport& report = simulation.tick();
    using R = sim::RejectReason;
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::PlotOwnedByOther));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::UnknownPlot));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::SurveyInProgress));
    CHECK(simulation.state().surveys.size() == 1);

    // Unaffordable: drain the rival through a save edit.
    sim::WorldState drained = simulation.state();
    drained.companies[1].cash = sim::Money::fromUnits(1);
    REQUIRE(simulation.load(sim::saveGame(drained, {}, sim::hashContent(content))));
    sim::PlotId unowned;
    for (std::uint32_t i = 0; i < simulation.state().plots.size(); ++i) {
        if (!simulation.state().plots[i].owner.valid()) {
            unowned = sim::PlotId(i);
            break;
        }
    }
    REQUIRE(unowned.valid());
    simulation.submit(sim::CommissionSurvey{kRival, unowned});
    const sim::TickReport& broke = simulation.tick();
    CHECK(hasNotice(broke.notices, sim::NoticeKind::CommandRejected, R::InsufficientFunds));
}

TEST_CASE("Survey estimates are noisy but unbiased in log space", "[surveys]") {
    const sim::Content content = test::makeContent();
    double sumLogRatio = 0.0;
    double sumSquares = 0.0;
    int samples = 0;
    for (sim::Seed seed = 1; seed <= 40; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        // Survey every plot the player may: all unowned at day 0.
        for (std::uint32_t i = 0; i < simulation.state().plots.size() && i < 8; ++i) {
            simulation.submit(sim::CommissionSurvey{kPlayer, sim::PlotId(i)});
        }
        simulation.tick(content.balance.surveyDurationDays + 1);
        for (const sim::Survey& survey : simulation.state().surveys) {
            REQUIRE(survey.complete == 1);
            const auto& truths =
                simulation.state().hidden.plotDeposits[survey.plot.index()].deposits;
            REQUIRE(survey.estimates.size() == truths.size());
            for (std::size_t k = 0; k < truths.size(); ++k) {
                CHECK(survey.estimates[k].resource == truths[k].resource);
                const double ratio = static_cast<double>(survey.estimates[k].tonnesEstimate) /
                                     static_cast<double>(truths[k].tonnesTotal);
                const double logRatio = std::log(ratio);
                sumLogRatio += logRatio;
                sumSquares += logRatio * logRatio;
                ++samples;
            }
        }
    }
    REQUIRE(samples > 200);
    const double mean = sumLogRatio / samples;
    const double sd = std::sqrt(sumSquares / samples - mean * mean);
    CHECK(std::abs(mean) < 0.08);
    CHECK(sd > 0.4);  // base noise 0.5
    CHECK(sd < 0.6);
}

TEST_CASE("Saves with inconsistent surveys are rejected", "[surveys][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 2, test::setup());
    simulation.submit(sim::CommissionSurvey{kRival, sim::PlotId(3)});
    simulation.tick(5);
    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };

    sim::WorldState broken = simulation.state();
    broken.surveys.push_back(broken.surveys[0]);  // two pending for one company/plot
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.surveys[0].complete = 1;  // complete before its day
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.surveys[0].completesDay += 1;  // inconsistent with the duration
    CHECK_FALSE(simulation.load(saveOf(broken)));

    simulation.tick(content.balance.surveyDurationDays);
    REQUIRE(simulation.state().surveys[0].complete == 1);
    broken = simulation.state();
    if (!broken.surveys[0].estimates.empty()) {
        broken.surveys[0].estimates.push_back(broken.surveys[0].estimates[0]);  // duplicate
        CHECK_FALSE(simulation.load(saveOf(broken)));
    }
    CHECK(simulation.load(saveOf(simulation.state())));
}

TEST_CASE("Survey results survive save and load and never leak to others", "[surveys][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 2, test::setup());
    simulation.submit(sim::CommissionSurvey{kRival, sim::PlotId(3)});
    simulation.tick(content.balance.surveyDurationDays + 1);
    REQUIRE(simulation.state().surveys[0].complete == 1);

    sim::Simulation restored(content, 7, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.stateHash() == simulation.stateHash());
    CHECK(restored.view(kRival).surveys.size() == 1);
    CHECK(restored.view(kPlayer).surveys.empty());
}
