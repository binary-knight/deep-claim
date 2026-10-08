#include "sim/events.h"

#include "sim/commands.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/mines.h"
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
#include <string>
#include <vector>

namespace {

const sim::CompanyId kPlayer(0);

// Content whose single event fires every eligible day with certainty.
sim::Content certainEventContent() {
    sim::Content c = test::makeContent();
    c.events[0].dailyWeight = sim::Fixed::one();
    c.events[0].minYear = 1;
    c.events[0].durationDays = 10;
    c.events[0].effects.push_back(
        {sim::EventEffectType::OpexMult, sim::ResourceId(), sim::Fixed::fromRatio(3, 2)});
    c.events[0].effects.push_back(
        {sim::EventEffectType::LandPriceMult, sim::ResourceId(), sim::Fixed::fromInt(2)});
    return c;
}

}  // namespace

TEST_CASE("Events start when rolled, last their duration and end", "[events]") {
    const sim::Content content = certainEventContent();
    sim::Simulation simulation(content, 1, test::setup());
    const sim::TickReport& day0 = simulation.tick();
    CHECK(std::any_of(day0.notices.begin(), day0.notices.end(), [](const sim::Notice& n) {
        return n.kind == sim::NoticeKind::EventStarted && n.event == sim::EventId(0);
    }));
    REQUIRE(simulation.state().events.size() == 1);
    CHECK(simulation.state().events[0].active == 1);
    CHECK(simulation.state().events[0].endsDay == 10);
    REQUIRE(simulation.view(kPlayer).events.size() == 1);
    CHECK(simulation.view(kPlayer).events[0].endsDay == 10);

    simulation.tick(9);  // days 1..9: still active, no second instance
    CHECK(simulation.state().events.size() == 1);
    const sim::TickReport& day10 = simulation.tick();
    CHECK(std::any_of(day10.notices.begin(), day10.notices.end(),
                      [](const sim::Notice& n) { return n.kind == sim::NoticeKind::EventEnded; }));
    // Ended and immediately re-rolled on the same day (weight 1).
    CHECK(simulation.state().events[0].active == 0);
    CHECK(simulation.state().events.size() == 2);
    CHECK(simulation.view(kPlayer).events.size() == 1);
}

TEST_CASE("Event multipliers reach demand, opex and land prices", "[events]") {
    const sim::Content content = certainEventContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick();  // event active from day 0
    const sim::WorldState& state = simulation.state();

    CHECK(sim::eventMultiplier(content, state, sim::EventEffectType::DemandMult,
                               sim::ResourceId(2)) == sim::Fixed::fromRatio(16, 10));
    CHECK(sim::eventMultiplier(content, state, sim::EventEffectType::DemandMult,
                               sim::ResourceId(0)) == sim::Fixed::one());
    CHECK(sim::eventMultiplier(content, state, sim::EventEffectType::OpexMult) ==
          sim::Fixed::fromRatio(3, 2));
    CHECK(sim::eventMultiplier(content, state, sim::EventEffectType::LandPriceMult) ==
          sim::Fixed::fromInt(2));

    // Opex: a notional operating mine costs 1.5x during the event.
    CHECK(sim::mineDailyOpex(content, state, state.companies[0], sim::MineState::Operating) ==
          content.balance.mineOpexPerDay.scaled(sim::Fixed::fromRatio(3, 2)));

    // Land: plots listed while the event runs ask twice their land value.
    while (simulation.state().day < sim::kDaysPerMonth) {
        simulation.tick();
    }
    const sim::TickReport& listing = simulation.tick();  // monthly listing on day 30
    const auto listed =
        std::find_if(listing.notices.begin(), listing.notices.end(),
                     [](const sim::Notice& n) { return n.kind == sim::NoticeKind::PlotListed; });
    REQUIRE(listed != listing.notices.end());
    const sim::Plot& plot = simulation.state().plots[listed->plot.index()];
    CHECK(plot.askPrice == plot.basePrice.scaled(sim::Fixed::fromInt(2)));
}

TEST_CASE("Demand events move the price", "[events][markets]") {
    sim::Content content = certainEventContent();
    for (sim::ResourceDef& r : content.resources) {
        r.volatility = sim::Fixed::zero();
    }
    content.events[0].durationDays = 300;
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(200);
    // Lithium demand x1.6 with no extra supply: ratio 1.6, elasticity 1.1 => 1.677x base.
    const sim::Money base = content.resources[2].basePrice;
    const sim::Money price = simulation.state().markets[2].price;
    CHECK(price > base.scaled(sim::Fixed::fromRatio(165, 100)));
    CHECK(price < base.scaled(sim::Fixed::fromRatio(170, 100)));
    CHECK(simulation.state().markets[0].price == content.resources[0].basePrice);
}

TEST_CASE("Events respect the minimum year and fire at a sane rate", "[events]") {
    const sim::Content content = test::makeContent();  // weight 0.0006, minYear 2
    int starts = 0;
    for (sim::Seed seed = 1; seed <= 20; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        simulation.tick(sim::kDaysPerYear);
        CHECK(simulation.state().events.empty());  // year 1: not eligible
        simulation.tick(sim::kDaysPerYear * 4);
        starts += static_cast<int>(simulation.state().events.size());
    }
    // 4 eligible years x 360 days x 0.0006 = 0.86 expected starts per run, minus
    // days spent active; 20 runs should land roughly between 5 and 30.
    CHECK(starts >= 5);
    CHECK(starts <= 30);
}

TEST_CASE("Event state survives save and load and is validated", "[events][save]") {
    const sim::Content content = certainEventContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(5);
    sim::Simulation restored(content, 2, test::setup());
    REQUIRE(restored.load(simulation.save()));
    CHECK(restored.stateHash() == simulation.stateHash());
    simulation.tick(50);
    restored.tick(50);
    CHECK(restored.stateHash() == simulation.stateHash());

    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };
    sim::WorldState broken = simulation.state();
    broken.events.push_back(broken.events.back());  // two active copies of one event
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.events[0].active = 1;  // long over
    CHECK_FALSE(simulation.load(saveOf(broken)));
    CHECK(simulation.load(saveOf(simulation.state())));
}

TEST_CASE("Event multipliers do not depend on activation order", "[events]") {
    sim::Content content = test::makeContent();
    content.events.clear();
    for (int i = 0; i < 3; ++i) {
        sim::EventDef def;
        def.id = "e" + std::to_string(i);
        def.name = def.id;
        def.dailyWeight = sim::Fixed::zero();
        def.durationDays = 100;
        content.events.push_back(def);
    }
    content.events[0].effects.push_back(
        {sim::EventEffectType::OpexMult, sim::ResourceId(), sim::Fixed::fromInt(100)});
    content.events[1].effects.push_back(
        {sim::EventEffectType::OpexMult, sim::ResourceId(), sim::Fixed::fromInt(100)});
    content.events[2].effects.push_back(
        {sim::EventEffectType::OpexMult, sim::ResourceId(), sim::Fixed::fromRatio(1, 100)});

    sim::WorldState a;
    a.day = 5;
    for (const std::uint32_t order : {0U, 1U, 2U}) {
        a.events.push_back({sim::EventId(order), 0, 100, 1});
    }
    sim::WorldState b;
    b.day = 5;
    for (const std::uint32_t order : {2U, 0U, 1U}) {
        b.events.push_back({sim::EventId(order), 0, 100, 1});
    }
    const sim::Fixed ma = sim::eventMultiplier(content, a, sim::EventEffectType::OpexMult);
    const sim::Fixed mb = sim::eventMultiplier(content, b, sim::EventEffectType::OpexMult);
    CHECK(ma == mb);
    CHECK(ma == sim::Fixed::fromInt(100));  // 100 * 100 saturates, then x 0.01
}

TEST_CASE("Event records are validated for eligibility, dates and overlap", "[events][save]") {
    const sim::Content content = certainEventContent();
    sim::Simulation simulation(content, 1, test::setup());
    simulation.tick(25);  // instances at 0-10, 10-20, 20-30
    const auto saveOf = [&](const sim::WorldState& s) {
        return sim::saveGame(s, {}, sim::hashContent(content));
    };
    sim::WorldState broken = simulation.state();
    broken.events[0].startedDay = 5;  // overlaps the next instance
    broken.events[0].endsDay = 15;
    CHECK_FALSE(simulation.load(saveOf(broken)));
    broken = simulation.state();
    broken.events.back().startedDay = broken.day;  // started in the future
    broken.events.back().endsDay = broken.day + 10;
    CHECK_FALSE(simulation.load(saveOf(broken)));
    CHECK(simulation.load(saveOf(simulation.state())));
}
