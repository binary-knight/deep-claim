#include "sim/finance.h"

#include "sim/commands.h"
#include "sim/content.h"
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
#include <utility>
#include <vector>

namespace {

const sim::CompanyId kPlayer(0);
const sim::CompanyId kRival(1);

bool hasNotice(const std::vector<sim::Notice>& notices, sim::NoticeKind kind,
               sim::RejectReason reason = sim::RejectReason::None) {
    return std::any_of(notices.begin(), notices.end(),
                       [&](const sim::Notice& n) { return n.kind == kind && n.reason == reason; });
}

void setCash(sim::Simulation& simulation, const sim::Content& content, sim::CompanyId company,
             sim::Money cash) {
    sim::WorldState edited = simulation.state();
    edited.companies[company.index()].cash = cash;
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

void tickToNextMonthEnd(sim::Simulation& simulation) {
    // Month-end charges run while processing a day that is a non-zero
    // multiple of 30. After tick() the processed day is state.day - 1.
    sim::Day processed = 0;
    do {
        simulation.tick();
        processed = simulation.state().day - 1;
    } while (processed == 0 || processed % sim::kDaysPerMonth != 0);
}

}  // namespace

TEST_CASE("Taking a loan adds cash and schedules instalments", "[finance]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::Money cash = simulation.state().companies[0].cash;
    const sim::Money principal = sim::Money::fromUnits(6'000'000);

    simulation.submit(sim::TakeLoan{kPlayer, principal});
    const sim::TickReport& report = simulation.tick();
    CHECK(hasNotice(report.notices, sim::NoticeKind::LoanTaken));
    CHECK(simulation.state().companies[0].cash == cash + principal);
    REQUIRE(simulation.state().loans.size() == 1);
    const sim::Loan& loan = simulation.state().loans[0];
    CHECK(loan.remaining == principal);
    CHECK(loan.principalPerMonth == sim::Money::fromUnits(100'000));  // 6M / 60
    CHECK(loan.monthsRemaining == 60);

    sim::CompanyView view = simulation.view(kPlayer);
    REQUIRE(view.loans.size() == 1);
    CHECK(view.outstandingPrincipal == principal);
    CHECK(view.loanCapacity == content.balance.loanMaxPrincipal - principal);
    CHECK(simulation.view(kRival).loans.empty());

    // First month end: 100k principal + (8% / 12) interest on 6M, about 40k
    // (slightly less because the monthly rate truncates to six decimals).
    const sim::Money interest = principal.scaled(content.balance.loanAnnualRate / 12);
    CHECK(interest > sim::Money::fromUnits(39'900));
    CHECK(interest <= sim::Money::fromUnits(40'000));
    const sim::Money before = simulation.state().companies[0].cash;
    tickToNextMonthEnd(simulation);
    CHECK(hasNotice(simulation.lastReport().notices, sim::NoticeKind::LoanPayment));
    CHECK(simulation.state().companies[0].cash ==
          before - sim::Money::fromUnits(100'000) - interest);
    CHECK(simulation.state().loans[0].remaining == sim::Money::fromUnits(5'900'000));
    CHECK(simulation.state().loans[0].monthsRemaining == 59);
}

TEST_CASE("Loan commands are validated against the capacity", "[finance]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.submit(sim::TakeLoan{kPlayer, sim::Money::zero()});
    simulation.submit(sim::TakeLoan{kPlayer, sim::Money::fromUnits(1)});  // below the minimum
    simulation.submit(
        sim::TakeLoan{kPlayer, content.balance.loanMaxPrincipal + sim::Money::fromCents(1)});
    simulation.submit(sim::TakeLoan{kPlayer, content.balance.loanMaxPrincipal});
    simulation.submit(sim::TakeLoan{kPlayer, content.balance.loanMinPrincipal});  // over capacity
    const sim::TickReport& report = simulation.tick();
    using R = sim::RejectReason;
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::InvalidAmount));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::LoanTooSmall));
    CHECK(std::count_if(report.notices.begin(), report.notices.end(), [](const sim::Notice& n) {
              return n.kind == sim::NoticeKind::CommandRejected &&
                     n.reason == sim::RejectReason::LoanLimitExceeded;
          }) == 2);
    CHECK(simulation.state().loans.size() == 1);
    CHECK(simulation.view(kPlayer).loanCapacity == sim::Money::zero());
}

TEST_CASE("The number of active loans is capped", "[finance]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    for (std::uint32_t i = 0; i <= content.balance.maxActiveLoans; ++i) {
        simulation.submit(sim::TakeLoan{kPlayer, content.balance.loanMinPrincipal});
    }
    const sim::TickReport& report = simulation.tick();
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected,
                    sim::RejectReason::TooManyLoans));
    CHECK(simulation.state().loans.size() == content.balance.maxActiveLoans);
}

TEST_CASE("Bankrupt bidders never win and compulsory charges respect the cash bound",
          "[finance][market]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    sim::PlotId plot;
    for (std::uint32_t i = 0; i < simulation.state().plots.size(); ++i) {
        if (simulation.state().plots[i].listing == sim::PlotListing::Listed) {
            plot = sim::PlotId(i);
            break;
        }
    }
    simulation.submit(
        sim::BidOnPlot{kRival, plot, simulation.state().plots[plot.index()].askPrice});
    simulation.tick();
    // Bankrupt the rival before the auction closes: deep in the red with a loan.
    simulation.submit(sim::TakeLoan{kRival, content.balance.loanMaxPrincipal});
    simulation.tick();
    setCash(simulation, content, kRival, sim::Money::fromCents(-sim::kMaxCashCents + 1));
    while (simulation.state().plots[plot.index()].listing == sim::PlotListing::Listed &&
           simulation.state().day < 400) {
        simulation.tick();
        CHECK(simulation.state().companies[1].cash.cents() >= -sim::kMaxCashCents);
    }
    CHECK(simulation.state().companies[1].bankrupt == 1);
    CHECK(simulation.state().plots[plot.index()].owner != kRival);
    CHECK(sim::validateWorld(content, simulation.state()));
}

TEST_CASE("Early repayment reduces principal and can close the loan", "[finance]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.submit(sim::TakeLoan{kPlayer, sim::Money::fromUnits(1'000'000)});
    simulation.tick();
    simulation.submit(sim::RepayLoan{kRival, sim::LoanId(0), sim::Money::fromUnits(1)});
    simulation.submit(sim::RepayLoan{kPlayer, sim::LoanId(5), sim::Money::fromUnits(1)});
    simulation.submit(sim::RepayLoan{kPlayer, sim::LoanId(0), sim::Money::fromUnits(2'000'000)});
    simulation.submit(sim::RepayLoan{kPlayer, sim::LoanId(0), sim::Money::fromUnits(400'000)});
    const sim::TickReport& report = simulation.tick();
    using R = sim::RejectReason;
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::NotLoanHolder));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::UnknownLoan));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::InvalidAmount));
    CHECK(simulation.state().loans[0].remaining == sim::Money::fromUnits(600'000));

    simulation.submit(sim::RepayLoan{kPlayer, sim::LoanId(0), sim::Money::fromUnits(600'000)});
    const sim::TickReport& closed = simulation.tick();
    CHECK(hasNotice(closed.notices, sim::NoticeKind::LoanRepaid));
    CHECK(simulation.state().loans[0].active == 0);
    simulation.submit(sim::RepayLoan{kPlayer, sim::LoanId(0), sim::Money::fromUnits(1)});
    CHECK(hasNotice(simulation.tick().notices, sim::NoticeKind::CommandRejected, R::LoanNotActive));
    CHECK(simulation.view(kPlayer).outstandingPrincipal == sim::Money::zero());
}

TEST_CASE("Storage is charged monthly per tonne held", "[finance]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    sim::WorldState edited = simulation.state();
    edited.companies[0].inventoryTonnes[0] = 1'000;  // iron: 1.50 per tonne-month
    edited.companies[0].inventoryTonnes[2] = 10;     // lithium: 8.00
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    const sim::Money expected = sim::Money::fromUnits(1'500) + sim::Money::fromUnits(80);
    CHECK(simulation.view(kPlayer).monthlyStorageCost == expected);

    const sim::Money before = simulation.state().companies[0].cash;
    simulation.tick(30);                                    // processes days 0..29
    CHECK(simulation.state().companies[0].cash == before);  // nothing mid-month
    simulation.tick();                                      // processes day 30
    CHECK(hasNotice(simulation.lastReport().notices, sim::NoticeKind::StorageCharged));
    CHECK(simulation.state().companies[0].cash == before - expected);
}

TEST_CASE("Insolvent companies get warnings and then go bankrupt", "[finance]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    setCash(simulation, content, kRival, sim::Money::fromUnits(-1));
    const std::uint32_t grace = content.balance.bankruptcyGraceMonths;

    for (std::uint32_t month = 1; month <= grace; ++month) {
        tickToNextMonthEnd(simulation);
        const std::vector<sim::Notice> rival = simulation.reportFor(kRival);
        CHECK(hasNotice(rival, sim::NoticeKind::InsolvencyWarning));
        CHECK(simulation.state().companies[1].monthsInsolvent == month);
        CHECK(simulation.state().companies[1].bankrupt == 0);
        // Warnings are private.
        CHECK_FALSE(hasNotice(simulation.reportFor(kPlayer), sim::NoticeKind::InsolvencyWarning));
    }
    tickToNextMonthEnd(simulation);
    CHECK(hasNotice(simulation.reportFor(kPlayer), sim::NoticeKind::Bankrupt));  // public
    CHECK(simulation.state().companies[1].bankrupt == 1);
    CHECK(simulation.view(kPlayer).companies[1].bankrupt);

    // A bankrupt company can no longer act.
    simulation.submit(sim::TakeLoan{kRival, sim::Money::fromUnits(1)});
    CHECK(hasNotice(simulation.tick().notices, sim::NoticeKind::CommandRejected,
                    sim::RejectReason::CompanyBankrupt));
}

TEST_CASE("Recovering cash before the grace period ends resets the count", "[finance]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    setCash(simulation, content, kPlayer, sim::Money::fromUnits(-1));
    tickToNextMonthEnd(simulation);
    CHECK(simulation.state().companies[0].monthsInsolvent == 1);
    setCash(simulation, content, kPlayer, sim::Money::fromUnits(1));
    tickToNextMonthEnd(simulation);
    CHECK(simulation.state().companies[0].monthsInsolvent == 0);
    CHECK(simulation.state().companies[0].bankrupt == 0);
}

TEST_CASE("Bankruptcy closes mines and writes off loans", "[finance][mines]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    // Give the rival a plot with a building mine and a loan, then drain it.
    sim::PlotId plot;
    for (std::uint32_t i = 0; i < simulation.state().plots.size(); ++i) {
        if (simulation.state().plots[i].listing == sim::PlotListing::Listed) {
            plot = sim::PlotId(i);
            break;
        }
    }
    simulation.submit(
        sim::BidOnPlot{kRival, plot, simulation.state().plots[plot.index()].askPrice});
    simulation.submit(sim::TakeLoan{kRival, sim::Money::fromUnits(1'000'000)});
    while (simulation.state().plots[plot.index()].owner != kRival) {
        simulation.tick();
    }
    simulation.submit(sim::BuildMine{kRival, plot});
    simulation.tick();
    REQUIRE(simulation.state().mines.size() == 1);
    setCash(simulation, content, kRival, sim::Money::fromUnits(-5'000'000));
    for (std::uint32_t month = 0; month <= content.balance.bankruptcyGraceMonths; ++month) {
        tickToNextMonthEnd(simulation);
    }
    REQUIRE(simulation.state().companies[1].bankrupt == 1);
    CHECK(simulation.state().mines[0].state == sim::MineState::Closed);
    CHECK(simulation.state().loans[0].active == 0);
    CHECK(sim::validateWorld(content, simulation.state()));
    // Closed mines cost nothing and the bankrupt company is charged nothing more.
    const sim::Money cash = simulation.state().companies[1].cash;
    simulation.tick(60);
    CHECK(simulation.state().companies[1].cash == cash);
    CHECK(simulation.view(kPlayer).plots[plot.index()].mine->state == sim::MineState::Closed);
}

TEST_CASE("Loan state survives save and load and is validated", "[finance][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 2, test::setup());
    simulation.submit(sim::TakeLoan{kPlayer, sim::Money::fromUnits(3'000'000)});
    simulation.tick(35);
    sim::Simulation restored(content, 9, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.stateHash() == simulation.stateHash());
    simulation.tick(100);
    restored.tick(100);
    CHECK(restored.stateHash() == simulation.stateHash());

    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };
    sim::WorldState broken = simulation.state();
    broken.loans[0].remaining = broken.loans[0].principal + sim::Money::fromCents(1);
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.loans.push_back(broken.loans[0]);
    broken.loans.push_back(broken.loans[0]);
    broken.loans.push_back(broken.loans[0]);  // 12M outstanding > 10M cap
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.companies[0].monthsInsolvent = 99;
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.loans[0].principalPerMonth = sim::Money::fromCents(1);  // not the canonical instalment
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.loans[0].monthsRemaining = 0;  // active loan with no schedule left
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.loans[0].principal = broken.loans[0].remaining =
        sim::Money::fromCents(9'000'000'000'000'000'000LL);
    CHECK_FALSE(simulation.load(saveOf(broken)));  // forged balance must not overflow validation
    CHECK(simulation.load(saveOf(simulation.state())));
}

TEST_CASE("Program state and the outcome survive save and load and are part of the state hash",
          "[finance][spec010][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation fresh(content, 3, test::setup());

    // A new game: nobody has started the program and nothing has ended.
    const sim::WorldState& start = fresh.state();
    REQUIRE(start.companies.size() == 2);
    for (const sim::Company& company : start.companies) {
        CHECK(company.programStage == 0);
        CHECK(company.programDelivered == std::vector<std::int64_t>(content.resources.size(), 0));
    }
    CHECK(start.outcome.ended == 0);
    CHECK(start.outcome.day == 0);
    CHECK_FALSE(start.outcome.founder.valid());
    CHECK(start.outcome.results ==
          std::vector<sim::ProgramResult>(start.companies.size(), sim::ProgramResult::None));

    // The rival has founded the program; the player was one stage short and
    // part-way through its deliveries (stage 3 of the fixture needs 3,000
    // tonnes of resource 0 and 50 of resource 2).
    fresh.tick(10);
    sim::WorldState edited = fresh.state();
    edited.companies[0].programStage = 2;
    edited.companies[0].programStageDays = {4, 7};
    edited.companies[0].programDelivered[0] = 1'234;
    edited.companies[0].programDelivered[2] = 50;
    edited.companies[1].programStage = 3;
    edited.companies[1].programStageDays = {3, 5, 10};
    // A stage complete has its research (the fixture's techs 2, 3 and 4).
    edited.companies[0].completedTechs = {sim::TechId(2), sim::TechId(3)};
    edited.companies[1].completedTechs = {sim::TechId(2), sim::TechId(3), sim::TechId(4)};
    // The world froze on the day the program was founded.
    edited.outcome.ended = 1;
    edited.outcome.day = edited.day;
    edited.outcome.founder = kRival;
    edited.outcome.results = {sim::ProgramResult::LesserPartner, sim::ProgramResult::Founded};
    sim::Simulation original(content, 4, test::setup());
    REQUIRE(original.load(sim::saveGame(edited, {}, sim::hashContent(content))));

    sim::Simulation restored(content, 5, test::setup());
    REQUIRE(restored.load(original.save()));
    const sim::WorldState& loaded = restored.state();
    CHECK(loaded.companies[0].programStage == 2);
    CHECK(loaded.companies[0].programDelivered == edited.companies[0].programDelivered);
    CHECK(loaded.companies[1].programStage == 3);
    CHECK(loaded.companies[1].programDelivered == edited.companies[1].programDelivered);
    CHECK(loaded.companies[0].programStageDays == std::vector<sim::Day>{4, 7});
    CHECK(loaded.companies[1].programStageDays == std::vector<sim::Day>{3, 5, 10});
    CHECK(loaded.outcome.ended == 1);
    CHECK(loaded.outcome.day == 10);
    CHECK(loaded.outcome.founder == kRival);
    CHECK(loaded.outcome.results == edited.outcome.results);
    CHECK(restored.stateHash() == original.stateHash());

    // Each field alone moves the hash.
    const std::uint64_t hash = original.stateHash();
    const auto hashWith = [&](const auto& edit) {
        sim::WorldState changed = original.state();
        edit(changed);
        return sim::hashWorld(changed);
    };
    CHECK(hashWith([](sim::WorldState&) {}) == hash);
    CHECK(hashWith([](sim::WorldState& s) { s.companies[0].programStage = 1; }) != hash);
    CHECK(hashWith([](sim::WorldState& s) { s.companies[0].programDelivered[0] += 1; }) != hash);
    CHECK(hashWith([](sim::WorldState& s) { s.companies[0].programStageDays[0] += 1; }) != hash);
    CHECK(hashWith([](sim::WorldState& s) { s.outcome.ended = 0; }) != hash);
    CHECK(hashWith([](sim::WorldState& s) { s.outcome.day += 1; }) != hash);
    CHECK(hashWith([](sim::WorldState& s) { s.outcome.founder = kPlayer; }) != hash);
    CHECK(hashWith([](sim::WorldState& s) { s.outcome.results[0] = sim::ProgramResult::Lost; }) !=
          hash);
}

TEST_CASE("Saves with impossible program state are rejected", "[finance][spec010][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(3);
    const auto loads = [&](const auto& edit) {
        sim::WorldState edited = simulation.state();
        edit(edited.companies[0]);
        sim::Simulation scratch(content, 2, test::setup());
        return scratch.load(
            sim::saveGame(test::settledPace(content, edited), {}, sim::hashContent(content)));
    };
    const auto stages = static_cast<std::uint32_t>(content.program.stages.size());
    REQUIRE(stages == 3);
    CHECK(loads([](sim::Company&) {}));

    // The stage counts stages complete: none to all of them, never more.
    // Each stage complete has its day (tests/program_test.cpp covers those).
    // and its research (the fixture's techs 2, 3 and 4, one per stage).
    const auto atStage = [&](sim::Company& c, std::uint32_t stage) {
        c.programStage = stage;
        c.programStageDays.assign(stage, 1);
        for (std::uint32_t s = 0; s < stage && s < stages; ++s) {
            c.completedTechs.push_back(content.program.stages[s].research);
        }
    };
    // Every stage but the last: all of them would have founded the program
    // (tests/program_test.cpp covers a save against the ending it claims).
    CHECK(loads([&](sim::Company& c) { atStage(c, stages - 1); }));
    CHECK_FALSE(loads([&](sim::Company& c) { atStage(c, stages + 1); }));

    // One delivery figure per resource.
    CHECK_FALSE(loads([](sim::Company& c) { c.programDelivered.push_back(0); }));
    CHECK_FALSE(loads([](sim::Company& c) { c.programDelivered.pop_back(); }));
    CHECK_FALSE(loads([](sim::Company& c) { c.programDelivered.clear(); }));

    // Stage 1 of the fixture needs 1,000 tonnes of resource 0 and nothing else.
    CHECK(loads([](sim::Company& c) { c.programDelivered[0] = 1'000; }));
    CHECK_FALSE(loads([](sim::Company& c) { c.programDelivered[0] = 1'001; }));
    CHECK_FALSE(loads([](sim::Company& c) { c.programDelivered[0] = -1; }));
    CHECK_FALSE(loads([](sim::Company& c) { c.programDelivered[1] = 1; }));
    CHECK_FALSE(loads([](sim::Company& c) { c.programDelivered[2] = -1; }));

    // The need is the current stage's: stage 2 wants 2,000 and 200 of
    // resource 1, and resource 2 not at all.
    const auto atStageTwo = [&](std::int64_t first, std::int64_t second, std::int64_t third) {
        return [=](sim::Company& c) {
            atStage(c, 1);
            c.programDelivered[0] = first;
            c.programDelivered[1] = second;
            c.programDelivered[2] = third;
        };
    };
    CHECK(loads(atStageTwo(2'000, 200, 0)));
    CHECK_FALSE(loads(atStageTwo(2'001, 0, 0)));
    CHECK_FALSE(loads(atStageTwo(0, 201, 0)));
    CHECK_FALSE(loads(atStageTwo(0, 0, 1)));

    // A finished program holds no deliveries.
    CHECK_FALSE(loads([&](sim::Company& c) {
        atStage(c, stages);
        c.programDelivered[0] = 1;
    }));
}

TEST_CASE("A program with no stages accepts only an untouched company",
          "[finance][spec010][save]") {
    sim::Content content = test::makeContent();
    content.program.stages.clear();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(3);
    const auto loads = [&](const auto& edit) {
        sim::WorldState edited = simulation.state();
        edit(edited.companies[0]);
        sim::Simulation scratch(content, 2, test::setup());
        return scratch.load(sim::saveGame(edited, {}, sim::hashContent(content)));
    };
    CHECK(loads([](sim::Company&) {}));
    CHECK_FALSE(loads([](sim::Company& c) {
        c.programStage = 1;
        c.programStageDays.assign(1, 0);
    }));
    CHECK_FALSE(loads([](sim::Company& c) { c.programDelivered[0] = 1; }));
}

TEST_CASE("Saves with an impossible outcome are rejected", "[finance][spec010][save]") {
    using Result = sim::ProgramResult;
    // The shape of an outcome. The companies are put where the outcome says
    // they are, so that only its shape is in question here: a save checked
    // against the ending it claims is covered in tests/program_test.cpp.
    constexpr sim::Day kToday = 20;
    sim::Content content = test::makeContent();
    content.program.deadlineDay = kToday;  // the day a deadline loss is dated
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(kToday);
    const auto withStages = [&](sim::Company& company, std::uint32_t stages, sim::Day lastDay) {
        company.programStage = stages;
        company.programStageDays.assign(stages, 1);
        for (std::uint32_t s = 0; s < stages; ++s) {
            company.completedTechs.push_back(content.program.stages[s].research);
        }
        if (stages != 0) {
            company.programStageDays.back() = lastDay;
        }
    };
    const auto loads = [&](std::uint8_t ended, sim::Day day, sim::CompanyId founder,
                           std::vector<Result> results) {
        sim::WorldState edited = simulation.state();
        for (std::size_t c = 0; c < edited.companies.size() && c < results.size(); ++c) {
            if (results[c] == Result::Founded) {
                withStages(edited.companies[c], 3, std::min(day, kToday));
            } else if (results[c] == Result::LesserPartner) {
                withStages(edited.companies[c], 2, 1);
            }
        }
        edited.outcome.ended = ended;
        edited.outcome.day = day;
        edited.outcome.founder = founder;
        edited.outcome.results = std::move(results);
        sim::Simulation scratch(content, 2, test::setup());
        return scratch.load(sim::saveGame(edited, {}, sim::hashContent(content)));
    };
    const sim::CompanyId nobody;
    const sim::Day today = simulation.state().day;
    REQUIRE(today == kToday);

    // A game in progress: no day, no founder, no results.
    CHECK(loads(0, 0, nobody, {Result::None, Result::None}));
    CHECK_FALSE(loads(0, 5, nobody, {Result::None, Result::None}));
    CHECK_FALSE(loads(0, 0, kPlayer, {Result::None, Result::None}));
    CHECK_FALSE(loads(0, 0, kPlayer, {Result::Founded, Result::None}));
    CHECK_FALSE(loads(0, 0, nobody, {Result::None, Result::Lost}));
    CHECK_FALSE(loads(0, 0, nobody, {Result::LesserPartner, Result::None}));

    // Every ending the rules allow.
    CHECK(loads(1, today, kPlayer, {Result::Founded, Result::Lost}));
    CHECK(loads(1, today, kRival, {Result::LesserPartner, Result::Founded}));
    CHECK(loads(1, today, kRival, {Result::Lost, Result::Founded}));
    CHECK(loads(1, today, nobody, {Result::Lost, Result::Lost}));

    // The flag is 0 or 1, and an ended game says when: the day the world
    // froze on, never day 0, an earlier day or one still to come.
    CHECK_FALSE(loads(2, today, kPlayer, {Result::Founded, Result::Lost}));
    CHECK_FALSE(loads(1, 0, kPlayer, {Result::Founded, Result::Lost}));
    CHECK_FALSE(loads(1, 5, kPlayer, {Result::Founded, Result::Lost}));
    CHECK_FALSE(loads(1, today - 1, nobody, {Result::Lost, Result::Lost}));
    CHECK_FALSE(loads(1, today + 1, kPlayer, {Result::Founded, Result::Lost}));

    // One result per company, each a result that exists.
    CHECK_FALSE(loads(1, today, nobody, {Result::Lost}));
    CHECK_FALSE(loads(1, today, nobody, {Result::Lost, Result::Lost, Result::Lost}));
    CHECK_FALSE(loads(1, today, nobody, {}));
    CHECK_FALSE(loads(0, 0, nobody, {Result::None}));
    // A result past the last one is what a corrupt save would carry.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    CHECK_FALSE(loads(1, today, nobody, {Result::Lost, static_cast<Result>(4)}));

    // Once the game has ended every company has a result.
    CHECK_FALSE(loads(1, today, kPlayer, {Result::Founded, Result::None}));
    CHECK_FALSE(loads(1, today, nobody, {Result::None, Result::Lost}));

    // The founder is a company, the one whose result is "founded", and the
    // only one.
    CHECK_FALSE(loads(1, today, sim::CompanyId(2), {Result::Lost, Result::Lost}));
    CHECK_FALSE(loads(1, today, kPlayer, {Result::Lost, Result::Founded}));
    CHECK_FALSE(loads(1, today, kPlayer, {Result::Lost, Result::Lost}));
    CHECK_FALSE(loads(1, today, kPlayer, {Result::Founded, Result::Founded}));
    CHECK_FALSE(loads(1, today, nobody, {Result::Founded, Result::Lost}));
}

namespace {

const sim::ResourceId kIron(0);    // the fixture's stage 1 needs 1,000 t
const sim::ResourceId kCopper(1);  // no stage 1 need

// A world at a month end with the player's cover costs on, round prices
// (iron 60, copper 900) and no storage charge, so the shortfall is exactly
// the cash set and every sale can be counted.
struct CoverCostsWorld {
    sim::Content content;
    sim::WorldState state;
    sim::TickReport report;

    CoverCostsWorld() : content(withoutStorage()), state(startState(content)) {
        state.day = sim::kDaysPerMonth;
        state.markets[kIron.index()].price = sim::Money::fromUnits(60);
        state.markets[kCopper.index()].price = sim::Money::fromUnits(900);
        player().coverCosts = 1;
    }

    sim::Company& player() { return state.companies[kPlayer.index()]; }

    void stock(std::int64_t iron, std::int64_t copper, sim::Money cash) {
        player().inventoryTonnes[kIron.index()] = iron;
        player().inventoryTonnes[kCopper.index()] = copper;
        player().cash = cash;
    }

    void monthEnd() { sim::runFinance(content, state, report); }

    // The cover-costs sales in the order they were made.
    std::vector<std::pair<sim::ResourceId, std::int64_t>> sales() const {
        std::vector<std::pair<sim::ResourceId, std::int64_t>> made;
        for (const sim::Notice& notice : report.notices) {
            if (notice.kind == sim::NoticeKind::CoverCostsSale) {
                made.emplace_back(notice.resource, notice.tonnes);
            }
        }
        return made;
    }

    std::int64_t summarized(sim::ResourceId resource) const {
        for (const sim::Notice& notice : report.notices) {
            if (notice.kind == sim::NoticeKind::AutoSaleSummary && notice.resource == resource) {
                return notice.tonnes;
            }
        }
        return 0;
    }

private:
    static sim::Content withoutStorage() {
        sim::Content content = test::makeContent();
        for (sim::ResourceDef& resource : content.resources) {
            resource.storageCostPerTonneMonth = sim::Money::zero();
        }
        return content;
    }

    static sim::WorldState startState(const sim::Content& content) {
        return sim::Simulation(content, 11, test::setup()).state();
    }
};

using Sale = std::pair<sim::ResourceId, std::int64_t>;

}  // namespace

TEST_CASE("Cover costs sells free stock before stock the stage still needs", "[finance][spec011]") {
    CoverCostsWorld world;

    SECTION("a shortfall free stock covers leaves the needed stock alone") {
        // Iron, all of it needed, is worth 60,000; copper, all free, 36,000.
        world.stock(1'000, 40, sim::Money::fromUnits(-20'000));
        world.monthEnd();
        CHECK(world.sales() == std::vector<Sale>{{kCopper, 23}});
        CHECK(world.player().inventoryTonnes[kIron.index()] == 1'000);
        CHECK(world.player().inventoryTonnes[kCopper.index()] == 17);
        CHECK(world.player().cash >= sim::Money::zero());
    }
    SECTION("each tier is ordered by the value of its own tonnes") {
        // Iron's whole stock (120,000) outweighs copper's (90,000), but its
        // free half is worth only 60,000.
        world.stock(2'000, 100, sim::Money::fromUnits(-10'000));
        world.monthEnd();
        CHECK(world.sales() == std::vector<Sale>{{kCopper, 12}});
        CHECK(world.player().inventoryTonnes[kIron.index()] == 2'000);
    }
    SECTION("a larger shortfall takes every good's free stock before any needed stock") {
        // 400 t of iron delivered, so the stage still needs 600: of 1,100 t
        // held, 500 are free (30,000) and 600 needed (36,000). Copper's 200 t
        // are free (180,000).
        world.player().programDelivered[kIron.index()] = 400;
        world.stock(1'100, 200, sim::Money::fromUnits(-225'000));
        world.monthEnd();
        CHECK(world.sales() == std::vector<Sale>{{kCopper, 200}, {kIron, 500}, {kIron, 250}});
        CHECK(world.player().inventoryTonnes[kIron.index()] == 350);
        CHECK(world.player().inventoryTonnes[kCopper.index()] == 0);
        CHECK(world.player().cash == sim::Money::zero());
        // The month's summary counts both of iron's sales.
        CHECK(world.summarized(kIron) == 750);
        CHECK(world.summarized(kCopper) == 200);
    }
}

TEST_CASE("Cover costs keeps the old order when the program needs nothing", "[finance][spec011]") {
    CoverCostsWorld world;
    world.stock(1'000, 40, sim::Money::fromUnits(-20'000));

    SECTION("the stage's iron is delivered in full") {
        world.player().programDelivered[kIron.index()] = 1'000;
    }
    SECTION("the program is complete") {
        world.player().programStage =
            static_cast<std::uint32_t>(world.content.program.stages.size());
    }
    SECTION("the content has no program") {
        world.content.program.stages.clear();
    }
    world.monthEnd();
    // Most valuable stock first, as before spec 011.
    CHECK(world.sales() == std::vector<Sale>{{kIron, 334}});
    CHECK(world.player().inventoryTonnes[kCopper.index()] == 40);
}

// The storage warning (spec 011, BR-014): the sim's view decides it, at the
// content's share of cash.
namespace {

bool storageWarning(const sim::Content& content, sim::WorldState state, sim::Money cash,
                    std::int64_t ironTonnes) {
    state.companies[0].cash = cash;
    state.companies[0].inventoryTonnes[0] = ironTonnes;  // iron: 1.50 per tonne-month
    return sim::makeCompanyView(content, state, kPlayer).storageWarning;
}

}  // namespace

TEST_CASE("Storage costing more than a tenth of cash is a warning", "[finance][spec011]") {
    const sim::Content content = test::makeContent();
    const sim::Simulation simulation(content, 1, test::setup());
    const sim::WorldState state = simulation.state();
    const auto cents = [](std::int64_t c) { return sim::Money::fromCents(c); };

    // $1,500 a month of storage against $15,000 is a tenth, not above it.
    CHECK_FALSE(storageWarning(content, state, cents(1'500'000), 1'000));
    CHECK(storageWarning(content, state, cents(1'499'999), 1'000));
    // With no cash, or less, any storage cost is a warning.
    CHECK(storageWarning(content, state, cents(0), 1'000));
    CHECK(storageWarning(content, state, cents(-100), 1'000));
    // Nothing held, nothing to warn of.
    CHECK_FALSE(storageWarning(content, state, cents(0), 0));
    CHECK_FALSE(storageWarning(content, state, cents(-100), 0));

    // The share is the content's: at a fifth, $7,500 is the line.
    sim::Content fifth = content;
    fifth.balance.storageWarningCashShare = sim::Fixed::fromRatio(1, 5);
    CHECK_FALSE(storageWarning(fifth, state, cents(1'499'999), 1'000));
    CHECK_FALSE(storageWarning(fifth, state, cents(750'000), 1'000));
    CHECK(storageWarning(fifth, state, cents(749'999), 1'000));

    // At a tenth the scaled cash is exactly the integer tenth the client
    // used before (cents / 10, truncated), so the warning is unchanged.
    for (const std::int64_t c : {1LL, 9LL, 10LL, 11LL, 1'234'567LL, 999'999'999'999LL}) {
        CHECK(cents(c).scaled(content.balance.storageWarningCashShare).cents() == c / 10);
    }
}
