#include "sim/plot_market.h"

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
    const sim::TickReport& day30 = simulation.tick();  // processes day 30: monthly listing
    CHECK(day30.day == 30);
    CHECK(std::count_if(day30.notices.begin(), day30.notices.end(), [](const sim::Notice& n) {
              return n.kind == sim::NoticeKind::PlotListed;
          }) == 1);
    CHECK(countListed(simulation.state()) == 4);
    CHECK(simulation.state().nextListing == 4);
    // The three day-0 auctions close unsold on day 45 and relist.
    while (simulation.state().day < content.balance.auctionWindowDays) {
        simulation.tick();
    }
    const sim::TickReport& day45 = simulation.tick();
    CHECK(std::count_if(day45.notices.begin(), day45.notices.end(), [](const sim::Notice& n) {
              return n.kind == sim::NoticeKind::PlotRelisted;
          }) == 3);
}

TEST_CASE("Unsold plots relist at a lower ask down to half the land value", "[market]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money base = simulation.state().plots[plot.index()].basePrice;
    simulation.tick(content.balance.auctionWindowDays + 1);  // first auction closes unsold
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
    while (simulation.state().day < content.balance.auctionWindowDays) {
        simulation.tick();
    }
    CHECK(simulation.state().plots[plot.index()].listing == sim::PlotListing::Listed);
    const sim::TickReport& closing = simulation.tick();  // processes the closing day
    CHECK(hasNotice(closing, sim::NoticeKind::PlotSold, kRival));
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
    simulation.tick(content.balance.auctionWindowDays + 1);
    CHECK(simulation.state().plots[other.index()].owner == kPlayer);
}

TEST_CASE("Commands change the hash and replays reproduce it", "[market][determinism]") {
    const sim::Content content = test::makeContent();
    sim::Simulation quiet(content, 3, test::setup());
    sim::Simulation active(content, 3, test::setup());
    sim::Simulation replay(content, 3, test::setup());
    const sim::PlotId plot = firstListed(active.state());
    const sim::Money ask = active.state().plots[plot.index()].askPrice;

    for (sim::Day d = 0; d < 90; ++d) {
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
    original.tick(45);
    restored.tick(45);
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

TEST_CASE("Reports are filtered per viewer; sale prices are public", "[market][view]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money ask = simulation.state().plots[plot.index()].askPrice;

    // Rival's rejected bid must not reach the player.
    simulation.submit(sim::BidOnPlot{kRival, plot, sim::Money::fromUnits(100'000'000)});
    simulation.submit(sim::BidOnPlot{kRival, plot, ask + sim::Money::fromUnits(7)});
    simulation.submit(sim::BidOnPlot{kPlayer, plot, ask});
    simulation.tick();
    const std::vector<sim::Notice> playerDay0 = simulation.reportFor(kPlayer);
    CHECK(std::none_of(playerDay0.begin(), playerDay0.end(), [](const sim::Notice& n) {
        return n.kind == sim::NoticeKind::CommandRejected;
    }));
    const std::vector<sim::Notice> rivalDay0 = simulation.reportFor(kRival);
    CHECK(std::count_if(rivalDay0.begin(), rivalDay0.end(), [](const sim::Notice& n) {
              return n.kind == sim::NoticeKind::CommandRejected;
          }) == 1);

    while (simulation.state().day < content.balance.auctionWindowDays) {
        simulation.tick();
    }
    simulation.tick();  // closes on the window's last day
    const std::vector<sim::Notice> playerClose = simulation.reportFor(kPlayer);
    const std::vector<sim::Notice> rivalClose = simulation.reportFor(kRival);
    // Loser sees its loss; winner does not see the loss notice.
    CHECK(std::any_of(playerClose.begin(), playerClose.end(), [&](const sim::Notice& n) {
        return n.kind == sim::NoticeKind::AuctionLost && n.company == kPlayer;
    }));
    CHECK(std::none_of(rivalClose.begin(), rivalClose.end(), [](const sim::Notice& n) {
        return n.kind == sim::NoticeKind::AuctionLost;
    }));
    // Sale is public with the price paid and the auction id.
    for (const auto* notices : {&playerClose, &rivalClose}) {
        const auto sold = std::find_if(notices->begin(), notices->end(), [](const sim::Notice& n) {
            return n.kind == sim::NoticeKind::PlotSold;
        });
        REQUIRE(sold != notices->end());
        CHECK(sold->company == kRival);
        CHECK(sold->amount == ask + sim::Money::fromUnits(7));
        CHECK(sold->auction.valid());
    }
}

TEST_CASE("A bidder that can no longer pay forfeits and the plot relists", "[market][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money ask = simulation.state().plots[plot.index()].askPrice;
    simulation.submit(sim::BidOnPlot{kRival, plot, ask});
    simulation.tick();

    // Drain the rival's cash through a save edit (later steps add real costs).
    sim::WorldState drained = simulation.state();
    drained.companies[1].cash = sim::Money::fromUnits(1);
    REQUIRE(simulation.load(sim::saveGame(drained, {}, sim::hashContent(content))));

    while (simulation.state().day < content.balance.auctionWindowDays) {
        simulation.tick();
    }
    const sim::TickReport& closing = simulation.tick();
    CHECK(hasNotice(closing, sim::NoticeKind::BidForfeited, kRival));
    CHECK(std::any_of(closing.notices.begin(), closing.notices.end(), [&](const sim::Notice& n) {
        return n.kind == sim::NoticeKind::PlotRelisted && n.plot == plot;
    }));
    CHECK(simulation.state().plots[plot.index()].listing == sim::PlotListing::Listed);
    CHECK(simulation.state().companies[1].cash == sim::Money::fromUnits(1));
}

TEST_CASE("Queued commands survive save and load", "[market][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation original(content, 8, test::setup());
    sim::Simulation restored(content, 9, test::setup());
    const sim::PlotId plot = firstListed(original.state());
    const sim::Money ask = original.state().plots[plot.index()].askPrice;

    original.submit(sim::BidOnPlot{kPlayer, plot, ask});
    REQUIRE(restored.load(original.save()));
    CHECK(restored.pendingCommands().size() == 1);
    original.tick(content.balance.auctionWindowDays + 1);
    restored.tick(content.balance.auctionWindowDays + 1);
    CHECK(restored.stateHash() == original.stateHash());
    CHECK(restored.state().plots[plot.index()].owner == kPlayer);
}

TEST_CASE("Saves with inconsistent market state are rejected", "[market][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money ask = simulation.state().plots[plot.index()].askPrice;
    simulation.submit(sim::BidOnPlot{kRival, plot, ask});
    simulation.tick();
    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };
    const std::size_t a = simulation.state().plots[plot.index()].auction.index();
    REQUIRE(simulation.state().auctions[a].bids.size() == 1);

    sim::WorldState broken = simulation.state();
    broken.auctions.push_back(broken.auctions[a]);  // second open auction for the plot
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.nextListing = 0;  // listed plots in the unconsumed part of the queue
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.auctions[a].bids.push_back(broken.auctions[a].bids[0]);  // duplicate bidder
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.auctions[a].bids[0].order = broken.bidCounter;  // stamp beyond the counter
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.day = sim::kMaxDay + 1;
    CHECK_FALSE(simulation.load(saveOf(broken)));

    CHECK(simulation.load(saveOf(simulation.state())));
}

namespace {

class BidEverything final : public sim::Controller {
public:
    void decide(const sim::CompanyView& view, std::vector<sim::Command>& out) override {
        for (const sim::PlotView& plot : view.plots) {
            if (plot.auction.has_value() && plot.auction->myBid == sim::Money::zero() &&
                plot.askPrice <= view.cash - view.reservedCash) {
                // Deliberately wrong company id: collectCommands must rebind it.
                out.push_back(sim::BidOnPlot{sim::CompanyId(0), plot.id, plot.askPrice});
            }
        }
    }
};

}  // namespace

TEST_CASE("collectCommands binds commands to the controller's company", "[market]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    BidEverything rivalBrain;
    simulation.collectCommands(kRival, rivalBrain);
    REQUIRE_FALSE(simulation.pendingCommands().empty());
    for (const sim::Command& command : simulation.pendingCommands()) {
        CHECK(sim::commandCompany(command) == kRival);
    }
    simulation.tick();
    CHECK(simulation.view(kRival).reservedCash > sim::Money::zero());
    CHECK(simulation.view(kPlayer).reservedCash == sim::Money::zero());
}

TEST_CASE("Views carry the viewer's own research list", "[view]") {
    const sim::Content content = test::makeContent();
    const sim::Simulation simulation(content, 1, test::setup());
    CHECK(simulation.view(kPlayer).completedTechs.empty());
}

namespace {

// The fixture relists forever; these tests switch withdrawal on.
sim::Content withdrawingContent() {
    sim::Content content = test::makeContent();
    content.balance.maxUnsoldRounds = 3;
    content.balance.relistCooldownDays = 360;
    return content;
}

bool inQueue(const std::vector<sim::PlotId>& queue, sim::PlotId plot) {
    return std::find(queue.begin(), queue.end(), plot) != queue.end();
}

}  // namespace

TEST_CASE("A plot unsold three times is withdrawn and returns after its cooldown", "[market]") {
    const sim::Content content = withdrawingContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    const sim::Money base = simulation.state().plots[plot.index()].basePrice;
    const sim::Day window = content.balance.auctionWindowDays;

    // Two unsold rounds: still listed, counting.
    simulation.tick(window * 2 + 1);
    CHECK(simulation.state().plots[plot.index()].listing == sim::PlotListing::Listed);
    CHECK(simulation.state().plots[plot.index()].unsoldCount == 2);
    CHECK(simulation.state().withdrawnQueue.empty());

    // The third closes on day 3 * window: withdrawn, public notice, no auction.
    while (simulation.state().day < window * 3) {
        simulation.tick();
    }
    const sim::TickReport& closing = simulation.tick();
    const sim::Plot& withdrawn = simulation.state().plots[plot.index()];
    CHECK(withdrawn.listing == sim::PlotListing::Unlisted);
    CHECK_FALSE(withdrawn.auction.valid());
    CHECK(withdrawn.unsoldCount == 0);
    CHECK(withdrawn.relistNotBefore == window * 3 + content.balance.relistCooldownDays);
    CHECK(inQueue(simulation.state().withdrawnQueue, plot));
    const sim::Money reduced = withdrawn.askPrice;
    CHECK(reduced < base);
    bool noticed = false;
    for (const sim::Notice& notice : closing.notices) {
        if (notice.kind == sim::NoticeKind::PlotWithdrawn && notice.plot == plot) {
            noticed = true;
            CHECK(notice.amount == reduced);
            CHECK(sim::noticeIsPublic(notice.kind));
        }
        // Withdrawn instead of relisted, not as well as.
        CHECK_FALSE((notice.kind == sim::NoticeKind::PlotRelisted && notice.plot == plot));
    }
    CHECK(noticed);
    CHECK(simulation.view(kPlayer).withdrawnPlots >= 1);

    // A save with withdrawn land loads and replays identically.
    const std::vector<std::uint8_t> bytes = simulation.save();
    sim::Simulation other(content, 99, test::setup());
    REQUIRE(other.load(bytes));
    CHECK(other.stateHash() == simulation.stateHash());

    // It comes back only once every never-listed plot has been offered and
    // its cooldown has passed, at the ask it left with.
    const sim::Day notBefore = withdrawn.relistNotBefore;
    sim::Day relistedOn = 0;
    for (int i = 0; i < 4000 && relistedOn == 0; ++i) {
        const sim::Day processed = simulation.state().day;
        const sim::TickReport& report = simulation.tick();
        for (const sim::Notice& notice : report.notices) {
            if (notice.kind == sim::NoticeKind::PlotListed && notice.plot == plot) {
                relistedOn = processed;
                CHECK(notice.amount == reduced);
            }
        }
    }
    REQUIRE(relistedOn != 0);
    CHECK(relistedOn >= notBefore);
    CHECK(relistedOn % sim::kDaysPerMonth == 0);
    CHECK(simulation.state().nextListing == simulation.state().listingQueue.size());
    const sim::Plot& back = simulation.state().plots[plot.index()];
    CHECK(back.listing == sim::PlotListing::Listed);
    CHECK(back.relistNotBefore == 0);
    CHECK(back.unsoldCount == 0);
    CHECK(back.askPrice == reduced);
    CHECK_FALSE(inQueue(simulation.state().withdrawnQueue, plot));
    CHECK(simulation.view(kPlayer).unlistedPlotsRemaining == 0);
    other.tick(simulation.state().day - other.state().day);
    CHECK(other.stateHash() == simulation.stateHash());
}

TEST_CASE("With zero unsold rounds configured plots relist forever", "[market]") {
    const sim::Content content = test::makeContent();
    REQUIRE(content.balance.maxUnsoldRounds == 0);
    sim::Simulation simulation(content, 1, test::setup());
    const sim::PlotId plot = firstListed(simulation.state());
    simulation.tick(sim::kDaysPerYear * 2);
    CHECK(simulation.state().plots[plot.index()].listing == sim::PlotListing::Listed);
    CHECK(simulation.state().withdrawnQueue.empty());
    CHECK(simulation.state().plots[plot.index()].unsoldCount > 3);
}

TEST_CASE("The view counts land not yet offered", "[market][view]") {
    const sim::Content content = withdrawingContent();
    sim::Simulation simulation(content, 1, test::setup());
    const std::uint32_t total = static_cast<std::uint32_t>(simulation.state().plots.size());
    CHECK(simulation.view(kPlayer).unlistedPlotsRemaining ==
          total - content.balance.plotsListedAtStart);
    CHECK(simulation.view(kPlayer).withdrawnPlots == 0);
    simulation.tick(sim::kDaysPerMonth + 1);
    CHECK(simulation.view(kRival).unlistedPlotsRemaining ==
          total - content.balance.plotsListedAtStart - content.balance.plotsListedPerMonth);
}

TEST_CASE("Saves with inconsistent withdrawal state are rejected", "[market][save]") {
    const sim::Content content = withdrawingContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(content.balance.auctionWindowDays * 3 + 2);
    REQUIRE_FALSE(simulation.state().withdrawnQueue.empty());
    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };
    const sim::PlotId gone = simulation.state().withdrawnQueue.front();
    const sim::PlotId listed = firstListed(simulation.state());
    REQUIRE(listed.valid());
    CHECK(simulation.load(saveOf(simulation.state())));

    sim::WorldState broken = simulation.state();
    broken.withdrawnQueue.push_back(gone);  // the same plot twice
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.withdrawnQueue.push_back(listed);  // a listed plot in the withdrawn queue
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.withdrawnQueue.clear();  // unlisted, already offered, in no queue
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.plots[listed.index()].relistNotBefore = 500;  // a cooldown on a listed plot
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.plots[listed.index()].unsoldCount = content.balance.maxUnsoldRounds;  // past the limit
    CHECK_FALSE(simulation.load(saveOf(broken)));

    broken = simulation.state();
    broken.plots[gone.index()].unsoldCount = 1;  // unsold rounds on an unlisted plot
    CHECK_FALSE(simulation.load(saveOf(broken)));

    if (simulation.state().withdrawnQueue.size() >= 2) {
        broken = simulation.state();
        std::swap(broken.plots[broken.withdrawnQueue[0].index()].relistNotBefore,
                  broken.plots[broken.withdrawnQueue[1].index()].relistNotBefore);
        const bool sameDay =
            simulation.state()
                .plots[simulation.state().withdrawnQueue[0].index()]
                .relistNotBefore ==
            simulation.state().plots[simulation.state().withdrawnQueue[1].index()].relistNotBefore;
        CHECK(simulation.load(saveOf(broken)) == sameDay);  // cooldowns must not decrease
    }
}

TEST_CASE("Monthly batches offer never-listed land before returning withdrawn land", "[market]") {
    sim::Content content = withdrawingContent();
    content.balance.plotsListedPerMonth = 4;  // batches straddle the end of the queue
    sim::Simulation simulation(content, 1, test::setup());
    const std::size_t total = simulation.state().plots.size();
    std::vector<sim::Day> returnsNotBefore(total, 0);  // set when a plot is withdrawn
    std::vector<int> timesListed(total, 0);
    int returned = 0;
    for (int i = 0; i < 2500; ++i) {
        const sim::Day processed = simulation.state().day;
        const std::size_t neverListed =
            simulation.state().listingQueue.size() - simulation.state().nextListing;
        const sim::TickReport& report = simulation.tick();
        std::size_t listedNow = 0;
        for (const sim::Notice& notice : report.notices) {
            const std::size_t p = notice.plot.valid() ? notice.plot.index() : 0;
            if (notice.kind == sim::NoticeKind::PlotWithdrawn) {
                returnsNotBefore[p] = processed + content.balance.relistCooldownDays;
            } else if (notice.kind == sim::NoticeKind::PlotListed) {
                ++listedNow;
                ++timesListed[p];
                if (returnsNotBefore[p] != 0) {
                    ++returned;
                    // Only once the never-listed plots ran out within this
                    // batch, and only after its year away.
                    CHECK(neverListed < content.balance.plotsListedPerMonth);
                    CHECK(processed >= returnsNotBefore[p]);
                    returnsNotBefore[p] = 0;
                }
            }
        }
        CHECK(listedNow <= content.balance.plotsListedPerMonth);
        if (listedNow > 0) {
            CHECK(processed % sim::kDaysPerMonth == 0);
        }
    }
    CHECK(returned > 0);
    CHECK(simulation.state().nextListing == simulation.state().listingQueue.size());
    // The day-0 listings predate the first report; every other plot was
    // announced at least once.
    const auto announced = static_cast<std::size_t>(
        std::count_if(timesListed.begin(), timesListed.end(), [](int n) { return n > 0; }));
    CHECK(announced >= total - content.balance.plotsListedAtStart);
    CHECK(simulation.load(simulation.save()));  // the long-run state passes validation
}
