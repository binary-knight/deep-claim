#include "sim/finance.h"

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
    simulation.submit(
        sim::TakeLoan{kPlayer, content.balance.loanMaxPrincipal + sim::Money::fromCents(1)});
    simulation.submit(sim::TakeLoan{kPlayer, content.balance.loanMaxPrincipal});
    simulation.submit(sim::TakeLoan{kPlayer, sim::Money::fromUnits(1)});  // over capacity now
    const sim::TickReport& report = simulation.tick();
    using R = sim::RejectReason;
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::InvalidAmount));
    CHECK(std::count_if(report.notices.begin(), report.notices.end(), [](const sim::Notice& n) {
              return n.kind == sim::NoticeKind::CommandRejected &&
                     n.reason == sim::RejectReason::LoanLimitExceeded;
          }) == 2);
    CHECK(simulation.state().loans.size() == 1);
    CHECK(simulation.view(kPlayer).loanCapacity == sim::Money::zero());
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
    CHECK(simulation.load(saveOf(simulation.state())));
}
