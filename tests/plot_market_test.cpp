#include "sim/plot_market.h"

#include "sim/commands.h"
#include "sim/ids.h"
#include "sim/money.h"
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

sim::PlotId firstListed(const sim::WorldState& state) {
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        if (state.plots[i].listing == sim::PlotListing::Listed) {
            return sim::PlotId(i);
        }
    }
    return sim::PlotId();
}

std::size_t countListed(const sim::WorldState& state) {
    return static_cast<std::size_t>(
        std::count_if(state.plots.begin(), state.plots.end(),
                      [](const sim::Plot& p) { return p.listing == sim::PlotListing::Listed; }));
}

bool hasNotice(const sim::TickReport& report, sim::NoticeKind kind, sim::CompanyId company,
               sim::RejectReason reason = sim::RejectReason::None) {
    return std::any_of(report.notices.begin(), report.notices.end(), [&](const sim::Notice& n) {
        return n.kind == kind && n.company == company && n.reason == reason;
    });
}

}  // namespace

TEST_CASE("A new game has companies with starting capital and initial listings", "[market]") {
    const sim::Content content = test::makeContent();
    const sim::Simulation simulation(content, 1, test::setup());
    const sim::WorldState& state = simulation.state();

    REQUIRE(state.companies.size() == 2);
    CHECK(state.companies[0].kind == sim::CompanyKind::Player);
    CHECK(state.companies[0].name == "Player");
    CHECK(state.companies[1].kind == sim::CompanyKind::Ai);
    CHECK(state.companies[0].cash == content.balance.startingCapital);
    CHECK(state.companies[0].inventoryTonnes.size() == content.resources.size());

    CHECK(countListed(state) == content.balance.plotsListedAtStart);
    CHECK(state.auctions.size() == content.balance.plotsListedAtStart);
    CHECK(state.nextListing == content.balance.plotsListedAtStart);
    for (const sim::Auction& auction : state.auctions) {
        CHECK(auction.closesOnDay == content.balance.auctionWindowDays);
        CHECK(auction.open == 1);
    }
}

TEST_CASE("Plots are listed monthly and auctions close after the window", "[market]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(30);  // processes days 0..29
    CHECK(simulation.state().auctions.size() == 3);
    const sim::TickReport& day30 = simulation.tick();  // processes day 30: close + list
    CHECK(day30.day == 30);
    // Three unsold auctions relist, plus one new monthly listing.
    CHECK(std::count_if(day30.notices.begin(), day30.notices.end(), [](const sim::Notice& n) {
              return n.kind == sim::NoticeKind::PlotRelisted;
          }) == 3);
    CHECK(std::count_if(day30.notices.begin(), day30.notices.end(), [](const sim::Notice& n) {
              return n.kind == sim::NoticeKind::PlotListed;
          }) == 1);
    CHECK(countListed(simulation.state()) == 4);
    CHECK(simulation.state().nextListing == 4);
}

TEST_CASE("Unsold plots relist at a lower ask down to half the land value", "[market]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money base = simulation.state().plots[plot.index()].basePrice;
    simulation.tick(31);  // the day-0 auction closes while processing day 30
    CHECK(simulation.state().plots[plot.index()].askPrice ==
          base.scaled(sim::Fixed::fromRatio(9, 10)));
    simulation.tick(sim::kDaysPerYear * 3);
    CHECK(simulation.state().plots[plot.index()].askPrice ==
          base.scaled(sim::Fixed::fromRatio(1, 2)));
    CHECK(simulation.state().plots[plot.index()].listing == sim::PlotListing::Listed);
}

TEST_CASE("Bids are validated and rejections are reported", "[market]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId listed = firstListed(simulation.state());
    const sim::Money ask = simulation.state().plots[listed.index()].askPrice;
    sim::PlotId unlisted;
    for (std::uint32_t i = 0; i < simulation.state().plots.size(); ++i) {
        if (simulation.state().plots[i].listing == sim::PlotListing::Unlisted) {
            unlisted = sim::PlotId(i);
            break;
        }
    }

    simulation.submit(sim::BidOnPlot{sim::CompanyId(7), listed, ask});
    simulation.submit(sim::BidOnPlot{kPlayer, sim::PlotId(999), ask});
    simulation.submit(sim::BidOnPlot{kPlayer, unlisted, ask});
    simulation.submit(sim::BidOnPlot{kPlayer, listed, ask - sim::Money::fromCents(1)});
    simulation.submit(sim::BidOnPlot{kPlayer, listed, sim::Money::fromUnits(100'000'000)});
    simulation.submit(sim::BidOnPlot{kPlayer, listed, ask});  // valid
    const sim::TickReport& report = simulation.tick();

    using R = sim::RejectReason;
    CHECK(
        hasNotice(report, sim::NoticeKind::CommandRejected, sim::CompanyId(7), R::UnknownCompany));
    CHECK(hasNotice(report, sim::NoticeKind::CommandRejected, kPlayer, R::UnknownPlot));
    CHECK(hasNotice(report, sim::NoticeKind::CommandRejected, kPlayer, R::PlotNotForSale));
    CHECK(hasNotice(report, sim::NoticeKind::CommandRejected, kPlayer, R::BidBelowAsk));
    CHECK(hasNotice(report, sim::NoticeKind::CommandRejected, kPlayer, R::InsufficientFunds));
    CHECK(report.notices.size() == 5);

    const sim::CompanyView view = simulation.view(kPlayer);
    CHECK(view.reservedCash == ask);
    CHECK(view.plots[listed.index()].auction->myBid == ask);
    CHECK(view.plots[listed.index()].auction->bidCount == 1);
    CHECK(simulation.view(kRival).plots[listed.index()].auction->myBid == sim::Money::zero());
    CHECK(simulation.view(kRival).plots[listed.index()].auction->bidCount == 1);
}

TEST_CASE("Reserved cash limits bids across auctions; rebids replace", "[market]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::WorldState& state = simulation.state();
    std::vector<sim::PlotId> listed;
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        if (state.plots[i].listing == sim::PlotListing::Listed) {
            listed.push_back(sim::PlotId(i));
        }
    }
    REQUIRE(listed.size() == 3);
    const sim::Money cash = state.companies[0].cash;

    // Bid most of the cash on plot A, then the remainder would not cover B.
    const sim::Money bidA = cash - sim::Money::fromUnits(100'000);
    simulation.submit(sim::BidOnPlot{kPlayer, listed[0], bidA});
    simulation.submit(sim::BidOnPlot{kPlayer, listed[1], state.plots[listed[1].index()].askPrice});
    const sim::TickReport& report = simulation.tick();
    CHECK(hasNotice(report, sim::NoticeKind::CommandRejected, kPlayer,
                    sim::RejectReason::InsufficientFunds));
    CHECK(simulation.view(kPlayer).reservedCash == bidA);

    // Rebidding lower on A frees cash: only the new amount counts.
    const sim::Money lower = state.plots[listed[0].index()].askPrice;
    simulation.submit(sim::BidOnPlot{kPlayer, listed[0], lower});
    simulation.tick();
    CHECK(simulation.view(kPlayer).reservedCash == lower);
    CHECK(state.auctions[0].bids.size() + state.auctions[1].bids.size() +
              state.auctions[2].bids.size() ==
          1);
}

TEST_CASE("Highest bid wins, ties go to the earlier bid, winner pays", "[market]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money ask = simulation.state().plots[plot.index()].askPrice;
    const sim::Money startCash = simulation.state().companies[0].cash;

    // Rival bids first at ask + 1; player submits the same amount later.
    simulation.submit(sim::BidOnPlot{kRival, plot, ask + sim::Money::fromUnits(1)});
    simulation.tick();
    simulation.submit(sim::BidOnPlot{kPlayer, plot, ask + sim::Money::fromUnits(1)});
    simulation.tick();
    simulation.tick(28);  // state.day is now 30; the auction is still open
    CHECK(simulation.state().plots[plot.index()].listing == sim::PlotListing::Listed);
    const sim::TickReport& closing = simulation.tick();  // processes day 30
    CHECK(hasNotice(closing, sim::NoticeKind::AuctionWon, kRival));
    CHECK(hasNotice(closing, sim::NoticeKind::AuctionLost, kPlayer));
    const sim::Plot& sold = simulation.state().plots[plot.index()];
    CHECK(sold.listing == sim::PlotListing::Sold);
    CHECK(sold.owner == kRival);
    CHECK_FALSE(sold.auction.valid());
    CHECK(simulation.state().companies[1].cash == startCash - ask - sim::Money::fromUnits(1));
    CHECK(simulation.state().companies[0].cash == startCash);
    CHECK(simulation.view(kPlayer).companies[1].plotsOwned == 1);

    // A higher later bid beats an earlier lower one on another plot.
    sim::PlotId other;
    for (std::uint32_t i = 0; i < simulation.state().plots.size(); ++i) {
        if (simulation.state().plots[i].listing == sim::PlotListing::Listed) {
            other = sim::PlotId(i);
            break;
        }
    }
    const sim::Money otherAsk = simulation.state().plots[other.index()].askPrice;
    simulation.submit(sim::BidOnPlot{kRival, other, otherAsk});
    simulation.tick();
    simulation.submit(sim::BidOnPlot{kPlayer, other, otherAsk + sim::Money::fromUnits(5)});
    simulation.tick(sim::kDaysPerMonth);
    CHECK(simulation.state().plots[other.index()].owner == kPlayer);
}

TEST_CASE("Commands change the hash and replays reproduce it", "[market][determinism]") {
    const sim::Content content = test::makeContent();
    sim::Simulation quiet(content, 3, test::setup());
    sim::Simulation active(content, 3, test::setup());
    sim::Simulation replay(content, 3, test::setup());
    const sim::PlotId plot = firstListed(active.state());
    const sim::Money ask = active.state().plots[plot.index()].askPrice;

    for (sim::Day d = 0; d < 60; ++d) {
        if (d == 2) {
            active.submit(sim::BidOnPlot{kPlayer, plot, ask});
            replay.submit(sim::BidOnPlot{kPlayer, plot, ask});
        }
        quiet.tick();
        active.tick();
        replay.tick();
    }
    CHECK(active.stateHash() != quiet.stateHash());
    CHECK(active.stateHash() == replay.stateHash());
    CHECK(active.state().plots[plot.index()].owner == kPlayer);
}

TEST_CASE("Saving mid-auction preserves bids and the eventual outcome", "[market][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation original(content, 4, test::setup());
    const sim::PlotId plot = firstListed(original.state());
    const sim::Money ask = original.state().plots[plot.index()].askPrice;
    original.submit(sim::BidOnPlot{kRival, plot, ask});
    original.tick(10);
    const std::vector<std::uint8_t> bytes = original.save();

    sim::Simulation restored(content, 99, test::setup());
    REQUIRE(restored.load(bytes));
    CHECK(restored.stateHash() == original.stateHash());
    original.tick(30);
    restored.tick(30);
    CHECK(restored.stateHash() == original.stateHash());
    CHECK(restored.state().plots[plot.index()].owner == kRival);
}

TEST_CASE("Views expose no hidden deposits and only the viewer's own bids", "[market][view]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money ask = simulation.state().plots[plot.index()].askPrice;
    simulation.submit(sim::BidOnPlot{kRival, plot, ask + sim::Money::fromUnits(1)});
    simulation.tick();

    const sim::CompanyView player = simulation.view(kPlayer);
    CHECK(player.me == kPlayer);
    CHECK(player.day == 1);
    CHECK(player.plots.size() == simulation.state().plots.size());
    CHECK(player.plots[plot.index()].auction.has_value());
    CHECK(player.plots[plot.index()].auction->bidCount == 1);
    CHECK(player.plots[plot.index()].auction->myBid == sim::Money::zero());
    CHECK(player.reservedCash == sim::Money::zero());
    CHECK(player.companies.size() == 2);
    CHECK(player.companies[1].name == "Rival");
    for (const sim::PlotView& p : player.plots) {
        CHECK(p.auction.has_value() == (p.listing == sim::PlotListing::Listed));
    }
}
