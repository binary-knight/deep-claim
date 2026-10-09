// Spec 001: standing sell policies and the month-end cover-costs rule.
#include "sim/commands.h"
#include "sim/content.h"
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
const sim::ResourceId kIron(0);
const sim::ResourceId kCopper(1);
const sim::ResourceId kLithium(2);  // locked until lithium_processing in the fixture

bool hasNotice(const std::vector<sim::Notice>& notices, sim::NoticeKind kind,
               sim::RejectReason reason = sim::RejectReason::None) {
    return std::any_of(notices.begin(), notices.end(),
                       [&](const sim::Notice& n) { return n.kind == kind && n.reason == reason; });
}

const sim::Notice* findNotice(const std::vector<sim::Notice>& notices, sim::NoticeKind kind,
                              sim::ResourceId resource) {
    const auto it = std::find_if(notices.begin(), notices.end(), [&](const sim::Notice& n) {
        return n.kind == kind && n.resource == resource;
    });
    return it == notices.end() ? nullptr : &*it;
}

// Edits the player's warehouse and cash through a save round trip.
void setPlayer(sim::Simulation& simulation, const sim::Content& content, sim::ResourceId resource,
               std::int64_t tonnes, sim::Money cash) {
    sim::WorldState edited = simulation.state();
    edited.companies[kPlayer.index()].inventoryTonnes[resource.index()] = tonnes;
    edited.companies[kPlayer.index()].cash = cash;
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

sim::SetSellPolicy policy(sim::ResourceId resource, bool enabled, std::int64_t reserve = 0,
                          sim::Money floor = sim::Money::zero()) {
    sim::SetSellPolicy command;
    command.company = kPlayer;
    command.resource = resource;
    command.enabled = enabled ? 1 : 0;
    command.reserveTonnes = reserve;
    command.floorPrice = floor;
    return command;
}

const sim::Company& player(const sim::Simulation& simulation) {
    return simulation.state().companies[kPlayer.index()];
}

}  // namespace

TEST_CASE("Companies start with the balance file's default policy", "[sell_policy]") {
    sim::Content content = test::makeContent();
    content.balance.defaultSellPolicy.enabled = 1;
    content.balance.defaultSellPolicy.reserveTonnes = 25;
    content.balance.defaultCoverCosts = true;
    const sim::Simulation simulation(content, 1, test::setup());
    for (const sim::Company& company : simulation.state().companies) {
        REQUIRE(company.sellPolicies.size() == content.resources.size());
        CHECK(company.sellPolicies[0].enabled == 1);
        CHECK(company.sellPolicies[0].reserveTonnes == 25);
        CHECK(company.coverCosts == 1);
        CHECK(company.autoSoldTonnes.size() == content.resources.size());
    }
    const sim::CompanyView view = simulation.view(kPlayer);
    CHECK(view.sellPolicies.size() == content.resources.size());
    CHECK(view.coverCosts);
    CHECK(view.sellPolicies[0].reserveTonnes == 25);
}

TEST_CASE("An enabled policy sells everything above the reserve each day", "[sell_policy]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 2, test::setup());
    setPlayer(simulation, content, kIron, 1'000, sim::Money::fromUnits(1'000'000));

    SECTION("sell everything") {
        simulation.submit(policy(kIron, true));
        const sim::Money before = player(simulation).cash;
        const sim::TickReport& report = simulation.tick();
        const sim::Money price = simulation.state().markets[kIron.index()].price;
        CHECK(player(simulation).inventoryTonnes[kIron.index()] == 0);
        // The sale happened at the day's price before the market update.
        CHECK(player(simulation).cash > before);
        CHECK(player(simulation).autoSoldTonnes[kIron.index()] == 1'000);
        CHECK(player(simulation).autoSoldProceeds[kIron.index()] > sim::Money::zero());
        CHECK_FALSE(hasNotice(report.notices, sim::NoticeKind::OreSold));
        CHECK(simulation.view(kPlayer).autoSoldTonnes[kIron.index()] == 1'000);
        (void)price;
    }
    SECTION("keep a reserve") {
        simulation.submit(policy(kIron, true, 400));
        simulation.tick();
        CHECK(player(simulation).inventoryTonnes[kIron.index()] == 400);
        CHECK(player(simulation).autoSoldTonnes[kIron.index()] == 600);
        simulation.tick();  // nothing above the reserve any more
        CHECK(player(simulation).inventoryTonnes[kIron.index()] == 400);
    }
    SECTION("a reserve above the stock sells nothing") {
        simulation.submit(policy(kIron, true, 5'000));
        simulation.tick();
        CHECK(player(simulation).inventoryTonnes[kIron.index()] == 1'000);
    }
    SECTION("floor price") {
        const sim::Money price = simulation.state().markets[kIron.index()].price;
        simulation.submit(policy(kIron, true, 0, price + sim::Money::fromCents(1)));
        simulation.tick();
        CHECK(player(simulation).inventoryTonnes[kIron.index()] == 1'000);
        simulation.submit(policy(kIron, true, 0, simulation.state().markets[kIron.index()].price));
        simulation.tick();
        CHECK(player(simulation).inventoryTonnes[kIron.index()] == 0);
    }
    SECTION("a disabled policy sells nothing") {
        simulation.submit(policy(kIron, false));
        simulation.tick();
        CHECK(player(simulation).inventoryTonnes[kIron.index()] == 1'000);
    }
    SECTION("manual orders still work alongside") {
        simulation.submit(policy(kIron, true, 900));
        simulation.submit(sim::SellOre{kPlayer, kIron, 500});
        const sim::TickReport& report = simulation.tick();
        CHECK(hasNotice(report.notices, sim::NoticeKind::OreSold));
        // 500 sold by hand, then the policy leaves the 900 reserve: nothing more.
        CHECK(player(simulation).inventoryTonnes[kIron.index()] == 500);
    }
}

TEST_CASE("Policies never sell locked ore", "[sell_policy]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 3, test::setup());
    setPlayer(simulation, content, kLithium, 50, sim::Money::fromUnits(1'000'000));
    simulation.submit(policy(kLithium, true));
    simulation.tick();
    CHECK(player(simulation).inventoryTonnes[kLithium.index()] == 50);
    CHECK(player(simulation).autoSoldTonnes[kLithium.index()] == 0);
}

TEST_CASE("Automatic sales are summarised once a month and the totals reset", "[sell_policy]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 4, test::setup());
    setPlayer(simulation, content, kIron, 300, sim::Money::fromUnits(1'000'000));
    simulation.submit(policy(kIron, true));
    // Month-end processing happens on the tick that starts on day 30.
    simulation.tick(sim::kDaysPerMonth + 1);
    const sim::TickReport& report = simulation.lastReport();
    const sim::Notice* summary =
        findNotice(report.notices, sim::NoticeKind::AutoSaleSummary, kIron);
    REQUIRE(summary != nullptr);
    CHECK(summary->tonnes == 300);
    CHECK(summary->amount > sim::Money::zero());
    CHECK(summary->company == kPlayer);
    CHECK(player(simulation).autoSoldTonnes[kIron.index()] == 0);
    CHECK(player(simulation).autoSoldProceeds[kIron.index()] == sim::Money::zero());
    CHECK_FALSE(sim::noticeVisibleTo(*summary, sim::CompanyId(1)));
}

TEST_CASE("Cover costs sells just enough of the most valuable stock at month end",
          "[sell_policy]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 5, test::setup());
    simulation.tick(sim::kDaysPerMonth);  // day 30: the next tick is the month end
    {
        sim::WorldState edited = simulation.state();
        sim::Company& me = edited.companies[kPlayer.index()];
        me.inventoryTonnes[kIron.index()] = 100'000;  // ~6M of iron
        me.inventoryTonnes[kCopper.index()] = 1'000;  // ~0.9M of copper
        me.cash = sim::Money::fromUnits(-200'000);
        REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    }

    SECTION("with the flag on") {
        simulation.submit(sim::SetCoverCosts{kPlayer, 1});
        const sim::TickReport& report = simulation.tick();
        CHECK(player(simulation).cash >= sim::Money::zero());
        const sim::Notice* sale =
            findNotice(report.notices, sim::NoticeKind::CoverCostsSale, kIron);
        REQUIRE(sale != nullptr);
        CHECK(sale->tonnes > 0);
        CHECK(sale->tonnes < 100'000);  // just enough, not everything
        CHECK(findNotice(report.notices, sim::NoticeKind::CoverCostsSale, kCopper) == nullptr);
        CHECK(player(simulation).inventoryTonnes[kCopper.index()] == 1'000);
        CHECK_FALSE(hasNotice(report.notices, sim::NoticeKind::InsolvencyWarning));
        // The cover-costs sale is part of the month's automatic sales.
        const sim::Notice* summary =
            findNotice(report.notices, sim::NoticeKind::AutoSaleSummary, kIron);
        REQUIRE(summary != nullptr);
        CHECK(summary->tonnes == sale->tonnes);
        // Cash is within one tonne's price of zero.
        CHECK(player(simulation).cash < simulation.state().markets[kIron.index()].price);
    }
    SECTION("with the flag off the company stays insolvent") {
        simulation.submit(sim::SetCoverCosts{kPlayer, 0});
        const sim::TickReport& report = simulation.tick();
        CHECK(player(simulation).cash < sim::Money::zero());
        CHECK(hasNotice(report.notices, sim::NoticeKind::InsolvencyWarning));
        CHECK(player(simulation).inventoryTonnes[kIron.index()] == 100'000);
    }
}

TEST_CASE("Cover costs runs out of stock gracefully", "[sell_policy]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 6, test::setup());
    simulation.tick(sim::kDaysPerMonth);
    {
        sim::WorldState edited = simulation.state();
        sim::Company& me = edited.companies[kPlayer.index()];
        me.inventoryTonnes[kIron.index()] = 10;  // a few hundred dollars
        me.cash = sim::Money::fromUnits(-200'000);
        me.coverCosts = 1;
        REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    }
    const sim::TickReport& report = simulation.tick();
    CHECK(player(simulation).inventoryTonnes[kIron.index()] == 0);
    CHECK(player(simulation).cash < sim::Money::zero());
    CHECK(hasNotice(report.notices, sim::NoticeKind::CoverCostsSale));
    CHECK(hasNotice(report.notices, sim::NoticeKind::InsolvencyWarning));
}

TEST_CASE("Sell policy commands are validated", "[sell_policy]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 7, test::setup());
    simulation.submit(policy(sim::ResourceId(99), true));
    simulation.submit(policy(kIron, true, -1));
    simulation.submit(policy(kIron, true, 0, sim::Money::fromCents(-1)));
    sim::SetSellPolicy wrongCompany = policy(kIron, true);
    wrongCompany.company = sim::CompanyId(42);
    simulation.submit(wrongCompany);
    simulation.submit(sim::SetCoverCosts{sim::CompanyId(42), 1});
    const sim::TickReport& report = simulation.tick();
    using R = sim::RejectReason;
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::UnknownResource));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::InvalidQuantity));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::InvalidAmount));
    CHECK(hasNotice(report.notices, sim::NoticeKind::CommandRejected, R::UnknownCompany));
    CHECK(player(simulation).sellPolicies[kIron.index()].enabled == 0);
}

TEST_CASE("Policies survive a save and bad policy state is refused on load", "[sell_policy]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 8, test::setup());
    simulation.submit(policy(kCopper, true, 10, sim::Money::fromUnits(950)));
    simulation.submit(sim::SetCoverCosts{kPlayer, 1});
    simulation.tick();
    const std::vector<std::uint8_t> bytes = simulation.save();

    sim::Simulation other(content, 9, test::setup());
    REQUIRE(other.load(bytes));
    CHECK(other.stateHash() == simulation.stateHash());
    const sim::CompanyView view = other.view(kPlayer);
    CHECK(view.sellPolicies[kCopper.index()].enabled == 1);
    CHECK(view.sellPolicies[kCopper.index()].reserveTonnes == 10);
    CHECK(view.sellPolicies[kCopper.index()].floorPrice == sim::Money::fromUnits(950));
    CHECK(view.coverCosts);

    sim::WorldState edited = simulation.state();
    edited.companies[kPlayer.index()].sellPolicies.pop_back();
    CHECK_FALSE(other.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    edited = simulation.state();
    edited.companies[kPlayer.index()].coverCosts = 2;
    CHECK_FALSE(other.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    edited = simulation.state();
    edited.companies[kPlayer.index()].sellPolicies[0].reserveTonnes = -5;
    CHECK_FALSE(other.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    edited = simulation.state();
    edited.companies[kPlayer.index()].autoSoldProceeds[0] = sim::Money::fromCents(-1);
    CHECK_FALSE(other.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}
