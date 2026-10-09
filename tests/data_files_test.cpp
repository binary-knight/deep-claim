// Loads the real data/ files so that a typo in game content fails CI.
#include "sim/content.h"
#include "sim/simulation.h"
#include "sim/world_state.h"
#include "sim/worldgen.h"

#include "content/loader.h"
#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

TEST_CASE("Shipped data files load cleanly", "[content][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;

    CHECK(c.resources.size() == 4);
    CHECK(c.techs.size() == 10);
    CHECK(c.regions.size() == 3);
    CHECK(c.events.size() == 4);

    std::uint32_t plots = 0;
    for (const sim::RegionDef& region : c.regions) {
        plots += region.plotCount;
    }
    CHECK(plots == 120);  // spec 005: 40 per region

    CHECK(c.findResource("lithium").valid());
    CHECK(c.resources[c.findResource("lithium").index()].unlockedBy ==
          c.findTech("lithium_processing"));
}

TEST_CASE("Shipped iron earns four and a half times a mine's opex at base", "[content][data]") {
    // Spec 006: iron was a rounding error at 3.0. The figure is computed
    // from the shipped files, so moving the price, the output or the opex
    // without the others fails here.
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;
    const sim::ResourceDef& iron = c.resources[c.findResource("iron").index()];
    const sim::Money revenue = iron.basePrice * iron.baseOutputTonnesPerDay;
    CHECK(revenue.cents() * 2 == c.balance.mineOpexPerDay.cents() * 9);
    // Every other ore earns more per mine than iron, so iron is the floor
    // and the Copper Belt stays the prize.
    for (const sim::ResourceDef& resource : c.resources) {
        CHECK((resource.basePrice * resource.baseOutputTonnesPerDay) >= revenue);
    }
}

TEST_CASE("Shipped deposits are capped at four medians", "[content][data][worldgen]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;
    for (const sim::RegionDef& region : c.regions) {
        for (const sim::DepositPrior& prior : region.deposits) {
            CHECK(prior.tonnesMaxMult == sim::Fixed::fromInt(4));
        }
    }
    for (sim::Seed seed = 1; seed <= 20; ++seed) {
        const sim::Simulation simulation(c, seed, test::setup());
        const sim::WorldState& state = simulation.state();
        for (std::size_t p = 0; p < state.plots.size(); ++p) {
            const sim::RegionDef& region = c.regions[state.plots[p].region.index()];
            for (const sim::DepositTruth& truth : state.hidden.plotDeposits[p].deposits) {
                for (const sim::DepositPrior& prior : region.deposits) {
                    if (prior.resource == truth.resource) {
                        CHECK(truth.tonnesTotal <= prior.tonnesMedian * 4);
                    }
                }
            }
        }
    }
}

TEST_CASE("The shipped competitor's knobs are the tuned ones", "[content][data][ai]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::AiParams& ai = result.content->ai;
    CHECK(ai.supplyDiscount == sim::Fixed::one());
    CHECK(ai.firstMineRequiredReturn <= ai.requiredReturn);
    CHECK(ai.firstMineRecoverableShare >= ai.recoverableShare);
    CHECK(result.content->balance.openingMineablePresence == sim::Fixed::fromRatio(1, 2));
}

TEST_CASE("A world generates from the shipped data", "[content][data][worldgen]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Simulation simulation(*result.content, 1, test::setup());
    const sim::WorldState& state = simulation.state();
    CHECK(state.plots.size() == 120);
    std::size_t depositsTotal = 0;
    for (const sim::PlotDeposits& deposits : state.hidden.plotDeposits) {
        depositsTotal += deposits.deposits.size();
        for (const sim::DepositTruth& truth : deposits.deposits) {
            CHECK(truth.tonnesTotal >= 1);
        }
    }
    // Priors sum to roughly 1 deposit per plot (1.1, 1.3 and 0.7 by region).
    CHECK(depositsTotal > 85);
    CHECK(depositsTotal < 165);
}

TEST_CASE("Shipped regions give every seed a fair opening hand", "[content][data][worldgen]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;
    // The promise the spec makes to the player: Salt Flats is the locked
    // region, the other two are mineable from day one.
    const auto kindOf = [&c](std::string_view id) {
        for (const sim::RegionDef& region : c.regions) {
            if (region.id == id) {
                return sim::classifyRegionAtStart(c, region);
            }
        }
        return sim::RegionStart::Other;
    };
    CHECK(kindOf("northern_shield") == sim::RegionStart::Mineable);
    CHECK(kindOf("copper_belt") == sim::RegionStart::Mineable);
    CHECK(kindOf("salt_flats") == sim::RegionStart::Locked);
    REQUIRE(c.balance.plotsListedAtStart >= c.balance.openingMinMineable);

    for (sim::Seed seed = 1; seed <= 200; ++seed) {
        const sim::Simulation simulation(c, seed, test::setup());
        const sim::WorldState& state = simulation.state();
        std::uint32_t mineable = 0;
        std::uint32_t locked = 0;
        std::uint32_t listed = 0;
        for (const sim::Plot& plot : state.plots) {
            if (plot.listing != sim::PlotListing::Listed) {
                continue;
            }
            ++listed;
            const sim::RegionStart kind =
                sim::classifyRegionAtStart(c, c.regions[plot.region.index()]);
            mineable += kind == sim::RegionStart::Mineable ? 1U : 0U;
            locked += kind == sim::RegionStart::Locked ? 1U : 0U;
        }
        CHECK(listed == c.balance.plotsListedAtStart);
        CHECK(mineable >= c.balance.openingMinMineable);
        CHECK(locked <= c.balance.openingMaxLockedRegion);
        // After the hand no region supplies three listings in a row, except
        // a forced run when the tail of the queue is one region.
        const auto regionOf = [&state](std::size_t i) {
            return state.plots[state.listingQueue[i].index()].region;
        };
        // The last plots of the hand count as predecessors, so start at the
        // first monthly listing.
        const std::size_t hand = c.balance.plotsListedAtStart;
        for (std::size_t i = std::max<std::size_t>(hand, 2); i < state.listingQueue.size(); ++i) {
            if (regionOf(i) != regionOf(i - 1) || regionOf(i) != regionOf(i - 2)) {
                continue;
            }
            for (std::size_t j = i; j < state.listingQueue.size(); ++j) {
                CHECK(regionOf(j) == regionOf(i));
            }
        }
    }
}
