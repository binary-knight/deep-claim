#include "sim/ai_controller.h"
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

const sim::CompanyId kRival(1);

// Runs the game with the heuristic AI driving company 1.
void playYears(sim::Simulation& simulation, sim::HeuristicController& ai, sim::Day days,
               std::uint32_t& rejections) {
    for (sim::Day d = 0; d < days; ++d) {
        simulation.collectCommands(kRival, ai);
        const sim::TickReport& report = simulation.tick();
        for (const sim::Notice& n : report.notices) {
            if (n.kind == sim::NoticeKind::CommandRejected && n.company == kRival) {
                ++rejections;
            }
        }
    }
}

}  // namespace

TEST_CASE("The AI plays twenty years without invalid commands or crashes", "[ai]") {
    const sim::Content content = test::makeContent();
    for (sim::Seed seed = 1; seed <= 3; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        sim::HeuristicController ai(content);
        std::uint32_t rejections = 0;
        playYears(simulation, ai, sim::kDaysPerYear * 20, rejections);
        CHECK(rejections == 0);
        const sim::CompanyView view = simulation.view(kRival);
        // It did something: owns land, built mines, sold ore or researched.
        const bool active =
            view.companies[1].plotsOwned > 0 || !view.mines.empty() || !view.completedTechs.empty();
        CHECK(active);
    }
}

TEST_CASE("The AI is deterministic and reproducible from a save", "[ai][determinism]") {
    const sim::Content content = test::makeContent();
    sim::Simulation a(content, 11, test::setup());
    sim::Simulation b(content, 11, test::setup());
    sim::HeuristicController ai(content);
    std::uint32_t ignored = 0;
    playYears(a, ai, 400, ignored);
    playYears(b, ai, 400, ignored);
    CHECK(a.stateHash() == b.stateHash());

    sim::Simulation restored(content, 99, test::setup());
    REQUIRE(restored.load(a.save()));
    playYears(a, ai, 400, ignored);
    playYears(restored, ai, 400, ignored);
    CHECK(a.stateHash() == restored.stateHash());
}

TEST_CASE("A changed AI knob changes the content fingerprint", "[ai][content]") {
    // The controller runs inside the sim: a save or trace made with one set
    // of knobs must not open against another.
    const sim::Content content = test::makeContent();
    sim::Content tuned = content;
    tuned.ai.requiredReturn = sim::Fixed::fromRatio(11, 10);
    CHECK(sim::hashContent(tuned) != sim::hashContent(content));
    sim::Content reserve = content;
    reserve.ai.cashReserve = sim::Money::fromUnits(500'001);
    CHECK(sim::hashContent(reserve) != sim::hashContent(content));
    sim::Content supply = content;
    supply.ai.supplyDiscount = sim::Fixed::fromRatio(1, 2);
    CHECK(sim::hashContent(supply) != sim::hashContent(content));
    sim::Content first = content;
    first.ai.firstMineRecoverableShare = sim::Fixed::fromRatio(6, 10);
    CHECK(sim::hashContent(first) != sim::hashContent(content));
    sim::Content firstReturn = content;
    firstReturn.ai.firstMineRequiredReturn = sim::Fixed::fromRatio(11, 10);
    CHECK(sim::hashContent(firstReturn) != sim::hashContent(content));
    sim::Content capped = content;
    capped.regions[0].deposits[0].tonnesMaxMult = sim::Fixed::fromInt(4);
    CHECK(sim::hashContent(capped) != sim::hashContent(content));
    sim::Content threshold = content;
    threshold.balance.openingMineablePresence = sim::Fixed::fromRatio(6, 10);
    CHECK(sim::hashContent(threshold) != sim::hashContent(content));

    const sim::Simulation original(content, 3, test::setup());
    sim::Simulation other(tuned, 3, test::setup());
    CHECK_FALSE(other.load(original.save()));
}

TEST_CASE("The AI values an ore lower the more of it the company already sells", "[ai]") {
    sim::Content content = test::makeContent();
    content.ai.supplyDiscount = sim::Fixed::one();
    const sim::ResourceId copper(1);
    const sim::ResourceDef& def = content.resources[copper.index()];

    // A bare view: copper at base, nobody selling, the company without mines.
    sim::CompanyView view;
    view.me = sim::CompanyId(0);
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        sim::MarketView market;
        market.resource = sim::ResourceId(r);
        market.price = content.resources[r].basePrice;
        market.basePrice = content.resources[r].basePrice;
        market.baseDemandTonnesPerDay = content.resources[r].baseDemandTonnesPerDay;
        view.markets.push_back(market);
    }
    const auto withMines = [&](std::uint32_t count) {
        sim::CompanyView owner = view;
        for (std::uint32_t i = 0; i < count; ++i) {
            sim::MineView mine;
            mine.state = sim::MineState::Operating;
            mine.lastOutputTonnes.assign(content.resources.size(), 0);
            mine.lastOutputTonnes[copper.index()] = def.baseOutputTonnesPerDay;
            owner.mines.push_back(mine);
        }
        // What those mines sell is already in the market's supply.
        owner.markets[copper.index()].recentSupplyTonnesPerDay = def.baseOutputTonnesPerDay * count;
        return owner;
    };

    // A newcomer: base price lowered only by its own mine's effect, which is
    // the market rule itself: base x (100 / 160) ^ 0.8 = 0.687 x base.
    const sim::Money alone = sim::aiValuedPrice(content, view, copper);
    CHECK(alone < def.basePrice);
    CHECK(alone > def.basePrice.scaled(sim::Fixed::fromRatio(68, 100)));
    CHECK(alone < def.basePrice.scaled(sim::Fixed::fromRatio(69, 100)));

    // With two mines of its own the next one is worth less per tonne, and
    // less than the same market looks to a company that owns none of it.
    const sim::CompanyView two = withMines(2);
    sim::CompanyView outsider = two;
    outsider.mines.clear();
    const sim::Money third = sim::aiValuedPrice(content, two, copper);
    const sim::Money entrant = sim::aiValuedPrice(content, outsider, copper);
    CHECK(third < entrant);
    CHECK(sim::aiValuedPrice(content, withMines(4), copper) < third);
    // A mothballed mine sells nothing and costs nothing in the valuation.
    sim::CompanyView idle = two;
    for (sim::MineView& mine : idle.mines) {
        mine.state = sim::MineState::Mothballed;
    }
    CHECK(sim::aiValuedPrice(content, idle, copper) == entrant);

    // A spike is never chased: above base the valuation starts from base.
    sim::CompanyView spike = view;
    spike.markets[copper.index()].price = def.basePrice * 3;
    CHECK(sim::aiValuedPrice(content, spike, copper) == alone);
    // Below base it starts from today's price.
    sim::CompanyView slump = view;
    slump.markets[copper.index()].price = def.basePrice / 2;
    CHECK(sim::aiValuedPrice(content, slump, copper) < alone);

    // With the rule switched off the price is today's, capped at base,
    // whatever the company owns; and the valuation never goes below zero.
    content.ai.supplyDiscount = sim::Fixed::zero();
    CHECK(sim::aiValuedPrice(content, view, copper) == def.basePrice);
    CHECK(sim::aiValuedPrice(content, withMines(4), copper) == def.basePrice);
    content.ai.supplyDiscount = sim::Fixed::one();
    CHECK(sim::aiValuedPrice(content, withMines(500), copper) >= sim::Money::zero());
}

TEST_CASE("Bid jitter favours neither company across plots", "[ai]") {
    // The old formula let company 1 outbid company 0 by six points on every
    // plot. Over the first 120 plots each must come out ahead often.
    std::uint32_t zeroAhead = 0;
    std::uint32_t oneAhead = 0;
    for (std::uint32_t p = 0; p < 120; ++p) {
        const std::int64_t zero = sim::aiBidJitterPercent(sim::PlotId(p), sim::CompanyId(0));
        const std::int64_t one = sim::aiBidJitterPercent(sim::PlotId(p), sim::CompanyId(1));
        CHECK(zero >= 0);
        CHECK(zero <= 6);
        CHECK(one >= 0);
        CHECK(one <= 6);
        zeroAhead += zero > one ? 1U : 0U;
        oneAhead += one > zero ? 1U : 0U;
    }
    CHECK(zeroAhead >= 35);
    CHECK(oneAhead >= 35);
}

namespace {

// Days until the rival first bids, or `limit` when it never does.
sim::Day firstBidDay(const sim::Content& content, sim::Seed seed, sim::Day limit) {
    sim::Simulation simulation(content, seed, test::setup());
    sim::HeuristicController ai(content);
    for (sim::Day d = 0; d < limit; ++d) {
        simulation.collectCommands(kRival, ai);
        simulation.tick();
        for (const sim::Auction& auction : simulation.state().auctions) {
            for (const sim::Bid& bid : auction.bids) {
                if (bid.company == kRival) {
                    return d;
                }
            }
        }
    }
    return limit;
}

}  // namespace

TEST_CASE("A company with no mine buys on the first-mine terms", "[ai]") {
    // General terms nothing can meet; first-mine terms anything worth
    // surveying can. The AI must bid inside the first auction window.
    sim::Content content = test::makeContent();
    content.ai.requiredReturn = sim::Fixed::fromInt(1000);
    content.ai.firstMineRequiredReturn = sim::Fixed::one();
    content.ai.firstMineRecoverableShare = sim::Fixed::one();
    bool bidInWindow = false;
    for (sim::Seed seed = 1; seed <= 4; ++seed) {
        bidInWindow = bidInWindow || firstBidDay(content, seed, content.balance.auctionWindowDays) <
                                         content.balance.auctionWindowDays;
    }
    CHECK(bidInWindow);

    // With the first-mine terms as strict as the general ones it never bids.
    content.ai.firstMineRequiredReturn = sim::Fixed::fromInt(1000);
    for (sim::Seed seed = 1; seed <= 4; ++seed) {
        CHECK(firstBidDay(content, seed, sim::kDaysPerYear) == sim::kDaysPerYear);
    }

    // And once it has a mine the general terms apply: with those unmeetable
    // it builds its first mine and never buys a second plot.
    content.ai.firstMineRequiredReturn = sim::Fixed::one();
    for (sim::Seed seed = 1; seed <= 4; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        sim::HeuristicController ai(content);
        std::uint32_t rejections = 0;
        playYears(simulation, ai, sim::kDaysPerYear * 6, rejections);
        const sim::CompanyView view = simulation.view(kRival);
        CHECK(view.mines.size() <= view.companies[1].plotsOwned);
        // Bids placed before the first mine existed may all win, so a few
        // plots are possible; a company on general terms of 1000x buys none
        // after that, which keeps it far below what six years allow.
        CHECK(view.companies[1].plotsOwned <= 4);
    }
}

TEST_CASE("The AI stops acting once bankrupt", "[ai]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    // Deeper in the red than any loan can cover, so the bootstrap loan rule
    // cannot rescue it and it must go bankrupt after the grace period.
    sim::WorldState edited = simulation.state();
    edited.companies[1].cash = sim::Money::fromUnits(-30'000'000);
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    sim::HeuristicController ai(content);
    std::uint32_t rejections = 0;
    playYears(simulation, ai, sim::kDaysPerYear, rejections);
    CHECK(simulation.state().companies[1].bankrupt == 1);
    CHECK(rejections == 0);
}

TEST_CASE("The AI's daily commands are jointly affordable", "[ai]") {
    // Rich in opportunities, short on cash: several surveyed listings, an owned
    // plot ready to build on, research available. Everything it issues must be
    // accepted by the sim, which only happens if one budget governs all rules.
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 5, test::setup());
    sim::HeuristicController ai(content);
    std::uint32_t rejections = 0;
    // Let it survey and buy for two years, then squeeze its cash.
    playYears(simulation, ai, sim::kDaysPerYear * 2, rejections);
    CHECK(rejections == 0);
    sim::WorldState squeezed = simulation.state();
    squeezed.companies[1].cash =
        sim::Money::fromUnits(2'400'000) + simulation.view(kRival).reservedCash;
    REQUIRE(simulation.load(sim::saveGame(squeezed, {}, sim::hashContent(content))));
    playYears(simulation, ai, sim::kDaysPerYear, rejections);
    CHECK(rejections == 0);
}

TEST_CASE("The AI does not strand itself on land it cannot develop", "[ai]") {
    const sim::Content content = test::makeContent();
    for (sim::Seed seed = 1; seed <= 4; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        sim::HeuristicController ai(content);
        std::uint32_t rejections = 0;
        playYears(simulation, ai, sim::kDaysPerYear * 6, rejections);
        const sim::CompanyView view = simulation.view(kRival);
        // Either it never bought land, or every plot it owns has a mine or it
        // can still fund one from cash plus borrowing capacity.
        for (const sim::PlotView& plot : view.plots) {
            if (plot.owner != kRival || plot.mine.has_value()) {
                continue;
            }
            CHECK((view.cash - view.reservedCash + view.loanCapacity).cents() >=
                  plot.mineCapexQuote.cents());
        }
    }
}
