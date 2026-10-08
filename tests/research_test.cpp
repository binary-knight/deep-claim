#include "sim/research.h"

#include "sim/commands.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/simulation.h"
#include "sim/view.h"
#include "sim/world_state.h"

#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

namespace {

const sim::CompanyId kPlayer(0);
const sim::CompanyId kRival(1);

bool hasNotice(const std::vector<sim::Notice>& notices, sim::NoticeKind kind,
               sim::RejectReason reason = sim::RejectReason::None) {
    return std::any_of(notices.begin(), notices.end(),
                       [&](const sim::Notice& n) { return n.kind == kind && n.reason == reason; });
}

}  // namespace

TEST_CASE("Research costs money up front and grants the tech when done", "[research]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::Money cash = simulation.state().companies[0].cash;
    const sim::TechDef& tech = content.techs[0];

    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(0)});
    const sim::TickReport& started = simulation.tick();
    CHECK(hasNotice(started.notices, sim::NoticeKind::ResearchStarted));
    CHECK(simulation.state().companies[0].cash == cash - tech.cost);
    REQUIRE(simulation.view(kPlayer).activeResearch.has_value());
    CHECK(simulation.view(kPlayer).activeResearch->tech == sim::TechId(0));
    CHECK(simulation.view(kPlayer).activeResearch->completesDay == tech.durationDays);
    CHECK_FALSE(simulation.view(kRival).activeResearch.has_value());

    while (simulation.state().day < tech.durationDays) {
        simulation.tick();
    }
    CHECK(simulation.state().companies[0].completedTechs.empty());
    const sim::TickReport& done = simulation.tick();
    CHECK(hasNotice(done.notices, sim::NoticeKind::ResearchComplete));
    CHECK(simulation.state().companies[0].completedTechs ==
          std::vector<sim::TechId>{sim::TechId(0)});
    CHECK_FALSE(simulation.view(kPlayer).activeResearch.has_value());
    CHECK(simulation.view(kPlayer).completedTechs.size() == 1);
}

TEST_CASE("Research commands are validated", "[research]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(99)});
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(1)});  // needs tech 0 first
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(0)});
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(0)});  // busy
    const sim::TickReport& report = simulation.tick();
    using R = sim::RejectReason;
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::UnknownTech));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::PrerequisitesMissing));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::ResearchBusy));
    CHECK(simulation.state().research.size() == 1);

    simulation.tick(content.techs[0].durationDays + 1);
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(0)});  // already known
    CHECK(hasNotice(simulation.tick().notices, sim::NoticeKind::CommandRejected,
                    R::TechAlreadyKnown));
    sim::WorldState poor = simulation.state();
    poor.companies[0].cash = sim::Money::fromUnits(1'000'000);
    REQUIRE(simulation.load(sim::saveGame(poor, {}, sim::hashContent(content))));
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(1)});  // 4M > 1M cash
    CHECK(hasNotice(simulation.tick().notices, sim::NoticeKind::CommandRejected,
                    R::InsufficientFunds));
}

TEST_CASE("Completed research changes survey noise and unlocks resources", "[research][surveys]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(0)});  // survey noise x0.8
    simulation.tick(content.techs[0].durationDays + 1);
    simulation.submit(sim::CommissionSurvey{kPlayer, sim::PlotId(0)});
    simulation.tick(content.balance.surveyDurationDays + 1);
    const sim::CompanyView view = simulation.view(kPlayer);
    REQUIRE_FALSE(view.surveys.empty());
    for (const sim::SurveyEstimateView& e : view.surveys[0].estimates) {
        CHECK(e.relativeError == content.balance.surveyBaseNoise * sim::Fixed::fromRatio(8, 10));
    }
}

TEST_CASE("Research state survives save and load and is validated", "[research][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 2, test::setup());
    simulation.submit(sim::StartResearch{kRival, sim::TechId(0)});
    simulation.tick(10);
    sim::Simulation restored(content, 3, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.stateHash() == simulation.stateHash());
    simulation.tick(200);
    restored.tick(200);
    CHECK(restored.stateHash() == simulation.stateHash());
    CHECK(restored.state().companies[1].completedTechs.size() == 1);

    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };
    sim::WorldState broken = simulation.state();
    broken.research[0].complete = 0;  // should be complete by now
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.research.push_back(broken.research[0]);
    broken.research.back().complete = 0;
    broken.research.back().completesDay = broken.day + 50;
    broken.research.back().startedDay =
        broken.research.back().completesDay - content.techs[0].durationDays;
    broken.research.push_back(broken.research.back());  // two active projects
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.companies[1].completedTechs.clear();  // completed research without the tech
    CHECK_FALSE(simulation.load(saveOf(broken)));
    CHECK(simulation.load(saveOf(simulation.state())));
}

TEST_CASE("Pending research must have been startable", "[research][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(0)});
    simulation.tick(5);
    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };
    sim::WorldState broken = simulation.state();
    broken.research[0].tech = sim::TechId(1);  // prerequisite (tech 0) not complete
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.companies[0].completedTechs.push_back(sim::TechId(0));  // researching a known tech
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.research[0].startedDay = broken.day;  // started in the future
    broken.research[0].completesDay = broken.day + content.techs[0].durationDays;
    CHECK_FALSE(simulation.load(saveOf(broken)));
    CHECK(simulation.load(saveOf(simulation.state())));
}
