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
    CHECK(simulation.state().markets[0].priceHistory.size() == 101);
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
    CHECK(simulation.load(saveOf(simulation.state())));
}
