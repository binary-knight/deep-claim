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
#include <cstdint>
#include <iterator>
#include <optional>
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

TEST_CASE("Unlocking a resource is public news, once", "[research]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::ResourceId lithium = content.findResource("lithium");
    REQUIRE(lithium.valid());
    REQUIRE(content.resources[lithium.index()].unlockedBy == sim::TechId(1));

    // The first tech unlocks nothing: no news.
    simulation.submit(sim::StartResearch{kRival, sim::TechId(0)});
    int unlockNotices = 0;
    for (sim::Day d = 0; d <= content.techs[0].durationDays + 1; ++d) {
        for (const sim::Notice& notice : simulation.tick().notices) {
            unlockNotices += notice.kind == sim::NoticeKind::RivalUnlockedResource ? 1 : 0;
        }
    }
    CHECK(unlockNotices == 0);

    simulation.submit(sim::StartResearch{kRival, sim::TechId(1)});
    for (sim::Day d = 0; d <= content.techs[1].durationDays + 30; ++d) {
        simulation.tick();
        for (const sim::Notice& notice : simulation.reportFor(kPlayer)) {
            if (notice.kind == sim::NoticeKind::RivalUnlockedResource) {
                ++unlockNotices;
                CHECK(notice.company == kRival);
                CHECK(notice.tech == sim::TechId(1));
                CHECK(notice.resource == lithium);
            }
            // The rival's own completion stays private.
            CHECK(notice.kind != sim::NoticeKind::ResearchComplete);
        }
    }
    CHECK(unlockNotices == 1);
    CHECK(sim::noticeIsPublic(sim::NoticeKind::RivalUnlockedResource));
}

TEST_CASE("The view says how close each locked resource is", "[research][view]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const std::size_t lithium = content.findResource("lithium").index();
    const std::size_t iron = content.findResource("iron").index();

    // Nothing researched: the unlocking tech still needs its prerequisite.
    sim::CompanyView view = simulation.view(kPlayer);
    REQUIRE(view.unlockDaysRemaining.size() == content.resources.size());
    REQUIRE(view.unlockReady.size() == content.resources.size());
    CHECK(view.unlockDaysRemaining[lithium] == 0);
    CHECK(view.unlockReady[lithium] == 0);
    CHECK(view.unlockDaysRemaining[iron] == 0);
    CHECK(view.unlockReady[iron] == 0);

    // Prerequisite done: one project away.
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(0)});
    simulation.tick(content.techs[0].durationDays + 2);
    view = simulation.view(kPlayer);
    CHECK(view.unlockReady[lithium] == 1);
    CHECK(view.unlockDaysRemaining[lithium] == 0);
    // The rival has researched nothing; its view is its own.
    CHECK(simulation.view(kRival).unlockReady[lithium] == 0);

    // Researching it: a countdown that ends the day the ore becomes mineable.
    simulation.submit(sim::StartResearch{kPlayer, sim::TechId(1)});
    simulation.tick();
    view = simulation.view(kPlayer);
    REQUIRE(view.activeResearch.has_value());
    CHECK(view.unlockReady[lithium] == 0);
    CHECK(view.unlockDaysRemaining[lithium] == view.activeResearch->completesDay - view.day + 1);
    const sim::Day days = view.unlockDaysRemaining[lithium];
    simulation.tick(days - 1);
    CHECK(simulation.view(kPlayer).unlockDaysRemaining[lithium] == 1);
    simulation.tick();
    view = simulation.view(kPlayer);
    CHECK(view.unlockDaysRemaining[lithium] == 0);
    CHECK(view.unlockReady[lithium] == 0);
    CHECK(std::find(view.completedTechs.begin(), view.completedTechs.end(), sim::TechId(1)) !=
          view.completedTechs.end());
}

// Spec 010, BR-004: a stage's project waits for the stages before it, and
// starting one is news for everyone.

namespace {

// Fixture projects (tests/test_content.h): one per program stage.
const sim::TechId kStageOneProject(2);
const sim::TechId kStageTwoProject(3);
const sim::TechId kStageThreeProject(4);

// A new world whose player can afford any project and has completed
// `stagesDone` stages of the program.
sim::WorldState worldWithStages(const sim::Content& content, std::uint32_t stagesDone) {
    sim::WorldState state = sim::Simulation(content, 1, test::setup()).state();
    state.companies[0].cash = sim::Money::fromUnits(100'000'000);
    state.companies[0].programStage = stagesDone;
    state.companies[0].programStageDays.assign(stagesDone, 0);
    return state;
}

std::vector<sim::Notice> noticesOfKind(const sim::TickReport& report, sim::NoticeKind kind) {
    std::vector<sim::Notice> found;
    std::copy_if(report.notices.begin(), report.notices.end(), std::back_inserter(found),
                 [&](const sim::Notice& n) { return n.kind == kind; });
    return found;
}

}  // namespace

TEST_CASE("A stage's project cannot start before the stages it needs are complete",
          "[research][program]") {
    const sim::Content content = test::makeContent();
    using R = sim::RejectReason;
    const auto start = [&](std::uint32_t stagesDone, sim::TechId tech) {
        return sim::validateStartResearch(content, worldWithStages(content, stagesDone),
                                          sim::StartResearch{kPlayer, tech});
    };
    CHECK(start(0, kStageOneProject) == std::nullopt);
    CHECK(start(0, kStageTwoProject) == R::TechNeedsProgramStage);
    CHECK(start(0, kStageThreeProject) == R::TechNeedsProgramStage);
    CHECK(start(1, kStageTwoProject) == std::nullopt);
    CHECK(start(1, kStageThreeProject) == R::TechNeedsProgramStage);
    CHECK(start(2, kStageThreeProject) == std::nullopt);
    CHECK(std::string_view(sim::rejectReasonName(R::TechNeedsProgramStage)) ==
          "tech_needs_program_stage");

    // Through the tick: the order is refused, nothing starts, nothing is charged.
    sim::Simulation simulation(content, 1, test::setup());
    const sim::Money cash = simulation.state().companies[0].cash;
    simulation.submit(sim::StartResearch{kPlayer, kStageTwoProject});
    const sim::TickReport& report = simulation.tick();
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::TechNeedsProgramStage));
    CHECK(noticesOfKind(report, sim::NoticeKind::ProgramResearchStarted).empty());
    CHECK(simulation.state().research.empty());
    CHECK(simulation.state().companies[0].cash == cash);
}

TEST_CASE("Starting a stage's project is announced to everyone", "[research][program]") {
    const sim::Content content = test::makeContent();
    using K = sim::NoticeKind;
    CHECK(std::string_view(sim::noticeKindName(K::ProgramResearchStarted)) ==
          "program_research_started");
    CHECK(sim::noticeIsPublic(K::ProgramResearchStarted));
    CHECK_FALSE(sim::noticeIsPublic(K::ResearchStarted));

    for (std::uint32_t stage = 0; stage < content.program.stages.size(); ++stage) {
        const sim::TechId project = content.program.stages[stage].research;
        sim::WorldState state = worldWithStages(content, stage);
        sim::TickReport report;
        sim::applyStartResearch(content, state, sim::StartResearch{kPlayer, project}, report);
        // The private line the company always gets, then the public one.
        REQUIRE(report.notices.size() == 2);
        CHECK(report.notices[0].kind == K::ResearchStarted);
        const sim::Notice& announced = report.notices[1];
        CHECK(announced.kind == K::ProgramResearchStarted);
        CHECK(announced.company == kPlayer);
        CHECK(announced.tech == project);
        CHECK(announced.tonnes == stage + 1);
        CHECK(sim::noticeVisibleTo(announced, kRival));
        CHECK_FALSE(sim::noticeVisibleTo(report.notices[0], kRival));
    }
}

TEST_CASE("Ordinary projects ignore the program and are not announced", "[research][program]") {
    const sim::Content content = test::makeContent();
    REQUIRE(content.techs[0].requiresProgramStage == 0);
    for (const std::uint32_t stagesDone : {0U, 1U, 3U}) {
        sim::WorldState state = worldWithStages(content, stagesDone);
        const sim::StartResearch command{kPlayer, sim::TechId(0)};
        CHECK(sim::validateStartResearch(content, state, command) == std::nullopt);
        sim::TickReport report;
        sim::applyStartResearch(content, state, command, report);
        REQUIRE(report.notices.size() == 1);
        CHECK(report.notices[0].kind == sim::NoticeKind::ResearchStarted);
    }
    // A program-less content (in-code content may leave it empty) announces nothing.
    sim::Content bare = test::makeContent();
    bare.program.stages.clear();
    sim::WorldState state = worldWithStages(bare, 0);
    sim::TickReport report;
    sim::applyStartResearch(bare, state, sim::StartResearch{kPlayer, kStageOneProject}, report);
    CHECK(noticesOfKind(report, sim::NoticeKind::ProgramResearchStarted).empty());
}

// Which projects a company may start, money and a busy lab aside. One rule
// in the sim (sim::researchOpenTo): StartResearch is validated by it, the
// view carries its answer per project, and the client and the competitor
// read the view instead of restating it.

namespace {

const sim::TechId kSeismic(0);
const sim::TechId kLithiumProcessing(1);  // needs kSeismic

using Open = std::vector<std::uint8_t>;

// The view's flags for `viewer`, checked on the way against the rule's own
// function and against what a StartResearch order would be told. A project
// marked closed is always refused; one marked open is accepted by a company
// with an idle lab and the money.
Open openProjects(const sim::Content& content, const sim::Simulation& simulation,
                  sim::CompanyId viewer) {
    const sim::WorldState& state = simulation.state();
    const sim::CompanyView view = simulation.view(viewer);
    REQUIRE(view.researchOpen.size() == content.techs.size());
    sim::WorldState rich = state;
    rich.companies[viewer.index()].cash = sim::Money::fromUnits(100'000'000);
    const bool idle = !sim::activeResearch(state, viewer).has_value();
    for (std::uint32_t t = 0; t < content.techs.size(); ++t) {
        const sim::TechId tech(t);
        const bool open = view.researchOpen[t] != 0;
        CHECK(open == sim::researchOpenTo(content, state, viewer, tech));
        const auto refused =
            sim::validateStartResearch(content, rich, sim::StartResearch{viewer, tech});
        if (!open) {
            CHECK(refused.has_value());
            CHECK(refused != sim::RejectReason::InsufficientFunds);
        } else if (idle) {
            CHECK(refused == std::nullopt);
        } else {
            CHECK(refused == sim::RejectReason::ResearchBusy);
        }
    }
    return view.researchOpen;
}

}  // namespace

TEST_CASE("The view marks a project open once its prerequisites are complete",
          "[research][view][open]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());

    // A new game: the project with no prerequisite and the first stage's
    // project are open; the others wait.
    CHECK(openProjects(content, simulation, kPlayer) == Open{1, 0, 1, 0, 0});
    CHECK(openProjects(content, simulation, kRival) == Open{1, 0, 1, 0, 0});
    // Money aside: a company that cannot pay for a project still has it open.
    sim::WorldState broke = simulation.state();
    broke.companies[0].cash = sim::Money::fromUnits(1);
    CHECK(sim::researchOpenTo(content, broke, kPlayer, kStageOneProject));
    CHECK(
        sim::validateStartResearch(content, broke, sim::StartResearch{kPlayer, kStageOneProject}) ==
        sim::RejectReason::InsufficientFunds);

    // In progress: not open to start again. A busy lab aside: the other open
    // project is still marked open. The rival's flags are its own.
    simulation.submit(sim::StartResearch{kPlayer, kSeismic});
    simulation.tick();
    REQUIRE(simulation.view(kPlayer).activeResearch.has_value());
    CHECK(openProjects(content, simulation, kPlayer) == Open{0, 0, 1, 0, 0});
    CHECK(openProjects(content, simulation, kRival) == Open{1, 0, 1, 0, 0});

    // Completed: not open, and the project that needed it now is.
    simulation.tick(content.techs[kSeismic.index()].durationDays + 1);
    REQUIRE_FALSE(simulation.view(kPlayer).activeResearch.has_value());
    CHECK(openProjects(content, simulation, kPlayer) == Open{0, 1, 1, 0, 0});
    CHECK(openProjects(content, simulation, kRival) == Open{1, 0, 1, 0, 0});
}

TEST_CASE("The view marks a stage's project open once the stage before it is complete",
          "[research][view][open]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(2);
    CHECK(openProjects(content, simulation, kPlayer)[kStageTwoProject.index()] == 0);

    // The player completes stage 1 by order: its project done, its iron
    // delivered, the cash at hand.
    sim::WorldState state = simulation.state();
    sim::Company& player = state.companies[0];
    player.completedTechs.push_back(kStageOneProject);
    player.programDelivered[0] = content.program.stages[0].requirements[0].tonnes;
    player.cash = sim::Money::fromUnits(100'000'000);
    REQUIRE(simulation.load(sim::saveGame(state, {}, sim::hashContent(content))));
    CHECK(openProjects(content, simulation, kPlayer) == Open{1, 0, 0, 0, 0});
    simulation.submit(sim::CompleteProgramStage{kPlayer});
    simulation.tick();
    REQUIRE(simulation.state().companies[0].programStage == 1);

    CHECK(openProjects(content, simulation, kPlayer) == Open{1, 0, 0, 1, 0});
    // The rival has completed nothing.
    CHECK(openProjects(content, simulation, kRival) == Open{1, 0, 1, 0, 0});

    // The stage's project in progress, then done: closed both times, and the
    // third stage's project still waits for the second stage itself.
    simulation.submit(sim::StartResearch{kPlayer, kStageTwoProject});
    simulation.tick();
    CHECK(openProjects(content, simulation, kPlayer) == Open{1, 0, 0, 0, 0});
    simulation.tick(content.techs[kStageTwoProject.index()].durationDays + 1);
    CHECK(openProjects(content, simulation, kPlayer) == Open{1, 0, 0, 0, 0});
}

TEST_CASE("No project is open to a bankrupt company or for an unknown id", "[research][open]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(2);
    const sim::WorldState& fresh = simulation.state();
    CHECK(sim::researchOpenTo(content, fresh, kPlayer, kSeismic));
    CHECK_FALSE(sim::researchOpenTo(content, fresh, kPlayer, sim::TechId(99)));
    CHECK_FALSE(sim::researchOpenTo(content, fresh, kPlayer, sim::TechId()));
    CHECK_FALSE(sim::researchOpenTo(content, fresh, sim::CompanyId(9), kSeismic));
    CHECK_FALSE(sim::researchOpenTo(content, fresh, sim::CompanyId(), kSeismic));

    sim::WorldState state = fresh;
    state.companies[0].bankrupt = 1;
    state.companies[0].monthsInsolvent = content.balance.bankruptcyGraceMonths + 1;
    state.companies[0].cash = sim::Money::fromUnits(-1);
    REQUIRE(simulation.load(sim::saveGame(state, {}, sim::hashContent(content))));
    CHECK(openProjects(content, simulation, kPlayer) == Open{0, 0, 0, 0, 0});
    CHECK(openProjects(content, simulation, kRival) == Open{1, 0, 1, 0, 0});
}

TEST_CASE("A busy lab is still the reason given before what a project waits for",
          "[research][open]") {
    const sim::Content content = test::makeContent();
    using R = sim::RejectReason;
    sim::Simulation simulation(content, 1, test::setup());
    simulation.submit(sim::StartResearch{kPlayer, kSeismic});
    simulation.tick();
    const auto start = [&](sim::TechId tech) {
        return sim::validateStartResearch(content, simulation.state(),
                                          sim::StartResearch{kPlayer, tech});
    };
    CHECK(start(kSeismic) == R::ResearchBusy);
    CHECK(start(kLithiumProcessing) == R::ResearchBusy);
    CHECK(start(kStageTwoProject) == R::ResearchBusy);
    CHECK(start(sim::TechId(99)) == R::UnknownTech);
    simulation.tick(content.techs[kSeismic.index()].durationDays + 1);
    CHECK(start(kSeismic) == R::TechAlreadyKnown);
    CHECK(start(kStageTwoProject) == R::TechNeedsProgramStage);
}
