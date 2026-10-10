#include "sim/markets.h"

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
const sim::ResourceId kIron(0);

bool hasNotice(const std::vector<sim::Notice>& notices, sim::NoticeKind kind,
               sim::RejectReason reason = sim::RejectReason::None) {
    return std::any_of(notices.begin(), notices.end(),
                       [&](const sim::Notice& n) { return n.kind == kind && n.reason == reason; });
}

// Puts ore in a company's warehouse through a save edit.
void grantOre(sim::Simulation& simulation, const sim::Content& content, sim::CompanyId company,
              sim::ResourceId resource, std::int64_t tonnes) {
    sim::WorldState edited = simulation.state();
    edited.companies[company.index()].inventoryTonnes[resource.index()] = tonnes;
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

// Removes demand noise so price tests are exact.
sim::Content calmContent() {
    sim::Content c = test::makeContent();
    for (sim::ResourceDef& r : c.resources) {
        r.volatility = sim::Fixed::zero();
    }
    c.events.clear();  // the fixture's lithium demand boom would move prices
    c.balance.demandGrowthPerYear = sim::Fixed::zero();  // a growing world lifts prices
    return c;
}

}  // namespace

TEST_CASE("Markets start at base price with a one-entry history", "[markets]") {
    const sim::Content content = test::makeContent();
    const sim::Simulation simulation(content, 1, test::setup());
    REQUIRE(simulation.state().markets.size() == content.resources.size());
    for (std::size_t i = 0; i < content.resources.size(); ++i) {
        const sim::Market& market = simulation.state().markets[i];
        CHECK(market.price == content.resources[i].basePrice);
        CHECK(market.demandMult == sim::Fixed::one());
        REQUIRE(market.priceHistory.size() == 1);
        CHECK(market.priceHistory[0] == content.resources[i].basePrice);
    }
    const sim::CompanyView view = simulation.view(kPlayer);
    REQUIRE(view.markets.size() == content.resources.size());
    CHECK(view.markets[0].basePrice == content.resources[0].basePrice);
    CHECK(view.markets[0].baseDemandTonnesPerDay == content.resources[0].baseDemandTonnesPerDay);
}

TEST_CASE("Without sales or volatility the price stays at base", "[markets]") {
    const sim::Content content = calmContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(100);
    CHECK(simulation.state().markets[0].price == content.resources[0].basePrice);
    CHECK(simulation.priceHistory(kIron).size() == 101);
    CHECK(simulation.view(kPlayer).markets[0].recentPrices.size() == sim::kRecentPriceDays);
}

TEST_CASE("Small markets settle at the exact power-law equilibrium", "[markets]") {
    const sim::Content content = calmContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::ResourceId lithium(2);
    // A stockpile large enough for the run but cheap enough to store: 10M tonnes
    // would cost 80M a month and bankrupt the seller mid-test.
    grantOre(simulation, content, kPlayer, lithium, 5'000);
    for (int day = 0; day < 600; ++day) {
        simulation.submit(sim::SellOre{kPlayer, lithium, 5});
        simulation.tick();
    }
    // supply = 8 + 5 = 13, ratio 8/13, elasticity 1.1 => 0.5862 of base.
    const std::int64_t base = content.resources[2].basePrice.cents();
    const std::int64_t price = simulation.state().markets[2].price.cents();
    CHECK(price > base * 580 / 1000);
    CHECK(price < base * 592 / 1000);
    CHECK(simulation.state().markets[2].supplySmoothed.raw() > 4'990'000);
    CHECK(simulation.state().companies[0].inventoryTonnes[2] == 5'000 - 600 * 5);
    CHECK(simulation.state().companies[0].bankrupt == 0);
}

TEST_CASE("Selling ore pays today's price and depresses tomorrow's", "[markets]") {
    const sim::Content content = calmContent();
    sim::Simulation simulation(content, 1, test::setup());
    grantOre(simulation, content, kPlayer, kIron, 100'000);
    const sim::Money price = simulation.state().markets[0].price;
    const sim::Money cash = simulation.state().companies[0].cash;

    simulation.submit(sim::SellOre{kPlayer, kIron, 800});
    const sim::TickReport& report = simulation.tick();
    CHECK(hasNotice(report.notices, sim::NoticeKind::OreSold));
    CHECK(simulation.state().companies[0].cash == cash + price * 800);
    CHECK(simulation.state().companies[0].inventoryTonnes[0] == 99'200);
    CHECK(simulation.state().markets[0].soldToday == 0);  // consumed by the update
    CHECK(simulation.state().markets[0].price < price);

    // Keep selling a mine's worth every day: price converges below base.
    for (int day = 0; day < 120; ++day) {
        simulation.submit(sim::SellOre{kPlayer, kIron, 800});
        simulation.tick();
    }
    const sim::Money settled = simulation.state().markets[0].price;
    // supply = 800 + 800, ratio 0.5, elasticity 0.6 => 0.5^0.6 = 0.66 of base.
    CHECK(settled > content.resources[0].basePrice.scaled(sim::Fixed::fromRatio(60, 100)));
    CHECK(settled < content.resources[0].basePrice.scaled(sim::Fixed::fromRatio(72, 100)));
    CHECK(simulation.view(kPlayer).markets[0].recentSupplyTonnesPerDay > 700);

    // Stop selling: price recovers toward base.
    simulation.tick(200);
    CHECK(simulation.state().markets[0].price > settled);
    CHECK(simulation.state().markets[0].price <= content.resources[0].basePrice);
}

TEST_CASE("Sell commands are validated", "[markets]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    grantOre(simulation, content, kPlayer, kIron, 10);
    simulation.submit(sim::SellOre{kPlayer, sim::ResourceId(99), 1});
    simulation.submit(sim::SellOre{kPlayer, kIron, 0});
    simulation.submit(sim::SellOre{kPlayer, kIron, 11});
    simulation.submit(sim::SellOre{kRival, kIron, 1});
    const sim::TickReport& report = simulation.tick();
    using R = sim::RejectReason;
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::UnknownResource));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::InvalidQuantity));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::InsufficientInventory));
    CHECK(std::count_if(report.notices.begin(), report.notices.end(), [](const sim::Notice& n) {
              return n.kind == sim::NoticeKind::CommandRejected;
          }) == 4);
    CHECK(simulation.state().companies[0].inventoryTonnes[0] == 10);
}

TEST_CASE("Demand noise moves prices but keeps them inside the bounds", "[markets]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 7, test::setup());
    simulation.tick(sim::kDaysPerYear * 5);
    for (std::size_t i = 0; i < content.resources.size(); ++i) {
        const sim::Market& market = simulation.state().markets[i];
        const sim::Money base = content.resources[i].basePrice;
        CHECK(market.price >= base.scaled(content.balance.priceFloorMult));
        CHECK(market.price <= base.scaled(content.balance.priceCapMult));
        CHECK(market.priceHistory.size() == sim::kDaysPerYear * 5 + 1);
        const bool moved = std::any_of(market.priceHistory.begin(), market.priceHistory.end(),
                                       [&](sim::Money p) { return p != base; });
        CHECK(moved);
        // Mean reversion keeps the long-run average near base.
        std::int64_t sum = 0;
        for (const sim::Money p : market.priceHistory) {
            sum += p.cents();
        }
        const std::int64_t mean = sum / static_cast<std::int64_t>(market.priceHistory.size());
        CHECK(mean > base.cents() * 7 / 10);
        CHECK(mean < base.cents() * 14 / 10);
    }
}

TEST_CASE("Market state survives save and load and is validated", "[markets][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 3, test::setup());
    simulation.tick(40);
    sim::Simulation restored(content, 4, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.stateHash() == simulation.stateHash());
    simulation.tick(40);
    restored.tick(40);
    CHECK(restored.stateHash() == simulation.stateHash());

    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };
    sim::WorldState broken = simulation.state();
    broken.markets[0].priceHistory.pop_back();  // history out of step with the day
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.markets[0].soldToday = 5;  // sales not consumed
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.markets[0].price = sim::Money::zero();  // below the floor
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.markets.pop_back();
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.markets[0].priceHistory.back() += sim::Money::fromCents(1);  // disagrees with price
    CHECK_FALSE(simulation.load(saveOf(broken)));
    CHECK(simulation.load(saveOf(simulation.state())));
}

TEST_CASE("Demand growth lifts prices slowly in an otherwise calm world", "[markets]") {
    sim::Content content = calmContent();
    content.balance.demandGrowthPerYear = sim::Fixed::fromRatio(4, 100);
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(sim::kDaysPerYear * 5);
    // Demand 1.04^5 = 1.217x; ratio^0.6 => about 1.125x base for iron.
    const std::int64_t base = content.resources[0].basePrice.cents();
    const std::int64_t price = simulation.state().markets[0].price.cents();
    CHECK(price > base * 110 / 100);
    CHECK(price < base * 115 / 100);
}

namespace {

// One number for every price a run produced, in order.
std::uint64_t foldPriceHistories(const sim::Simulation& simulation) {
    std::uint64_t folded = 14695981039346656037ULL;
    for (const sim::Market& market : simulation.state().markets) {
        for (const sim::Money price : market.priceHistory) {
            folded = (folded ^ static_cast<std::uint64_t>(price.cents())) * 1099511628211ULL;
        }
    }
    return folded;
}

// Iron's price after a company has sold a mine's worth every day for long
// enough to settle: demand over supply is 800 / 1600.
sim::Money settledIronPrice(const sim::Content& content) {
    sim::Simulation simulation(content, 1, test::setup());
    grantOre(simulation, content, kPlayer, kIron, 100'000);
    for (int day = 0; day < 120; ++day) {
        simulation.submit(sim::SellOre{kPlayer, kIron, 800});
        simulation.tick();
    }
    return simulation.state().markets[0].price;
}

sim::Money ironPriceAfter(const sim::Content& content, sim::Day days) {
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(days);
    return simulation.state().markets[0].price;
}

}  // namespace

TEST_CASE("Prices over five years are unchanged at the shipped clamp values", "[markets]") {
    // Recorded before spec 008 moved the two clamps from sim/src/markets.cpp
    // into content.balance: reading them from data must not move one price.
    constexpr std::uint64_t kRecorded = 0x4a415465b0d90deaULL;
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 7, test::setup());
    simulation.tick(sim::kDaysPerYear * 5);
    CHECK(foldPriceHistories(simulation) == kRecorded);
}

TEST_CASE("The demand-over-supply clamp comes from content", "[markets]") {
    const sim::Content content = calmContent();
    const sim::Money base = content.resources[0].basePrice;

    // A ratio of 0.5 is inside the shipped clamp; a floor of 0.75 holds it up.
    sim::Content floored = content;
    floored.balance.marketRatioMin = sim::Fixed::fromRatio(3, 4);
    const sim::Money settled = settledIronPrice(content);
    const sim::Money held = settledIronPrice(floored);
    CHECK(held > settled);
    // 0.75^0.6 = 0.84 of base.
    CHECK(held > base.scaled(sim::Fixed::fromRatio(82, 100)));
    CHECK(held < base.scaled(sim::Fixed::fromRatio(86, 100)));

    // Five years of 4% growth lift the ratio to 1.217; a ceiling of 1 holds
    // the price at base.
    sim::Content growing = content;
    growing.balance.demandGrowthPerYear = sim::Fixed::fromRatio(4, 100);
    sim::Content capped = growing;
    capped.balance.marketRatioMax = sim::Fixed::one();
    CHECK(ironPriceAfter(growing, sim::kDaysPerYear * 5) > base);
    CHECK(ironPriceAfter(capped, sim::kDaysPerYear * 5) == base);
}

TEST_CASE("The demand-factor clamp comes from content", "[markets]") {
    const sim::Content content = calmContent();
    const sim::Money base = content.resources[0].basePrice;
    CHECK(ironPriceAfter(content, 200) == base);

    // A calm world's factor is 1: a floor of 2 doubles demand (2^0.6 = 1.52
    // of base), a ceiling of 0.5 halves it (0.5^0.6 = 0.66 of base).
    sim::Content lifted = content;
    lifted.balance.demandFactorMin = sim::Fixed::fromInt(2);
    const sim::Money high = ironPriceAfter(lifted, 200);
    CHECK(high > base.scaled(sim::Fixed::fromRatio(149, 100)));
    CHECK(high < base.scaled(sim::Fixed::fromRatio(154, 100)));

    sim::Content pressed = content;
    pressed.balance.demandFactorMax = sim::Fixed::fromRatio(1, 2);
    const sim::Money low = ironPriceAfter(pressed, 200);
    CHECK(low > base.scaled(sim::Fixed::fromRatio(64, 100)));
    CHECK(low < base.scaled(sim::Fixed::fromRatio(68, 100)));
}

namespace {

// The fixture with one spec 008 value changed, once per value.
std::vector<sim::Content> spec008Variants(const sim::Content& content) {
    std::vector<sim::Content> variants(6, content);
    variants[0].resources[1].mineOpexPerDay = sim::Money::fromUnits(8'001);
    variants[1].balance.marketRatioMin = sim::Fixed::fromRatio(1, 4);
    variants[2].balance.marketRatioMax = sim::Fixed::fromInt(4);
    variants[3].balance.demandFactorMin = sim::Fixed::fromRatio(1, 50);
    variants[4].balance.demandFactorMax = sim::Fixed::fromInt(500);
    variants[5].balance.mineLossNoticeDays = 6;
    return variants;
}

}  // namespace

TEST_CASE("Spec 008 content values are part of the content fingerprint", "[markets][content]") {
    // A save or trace made with one set of running costs, clamps or loss
    // threshold must not open against another.
    const sim::Content content = test::makeContent();
    const std::uint64_t original = sim::hashContent(content);
    const sim::Simulation saved(content, 3, test::setup());

    for (const sim::Content& changed : spec008Variants(content)) {
        sim::Simulation other(changed, 3, test::setup());
        const bool refused = !other.load(saved.save());
        CHECK(sim::hashContent(changed) != original);
        CHECK(refused);
    }

    // The single running-cost figure is being retired: it no longer counts.
    sim::Content retired = content;
    retired.balance.mineOpexPerDay = sim::Money::fromUnits(8'001);
    CHECK(sim::hashContent(retired) == original);
    sim::Simulation same(retired, 4, test::setup());
    CHECK(same.load(saved.save()));
}
