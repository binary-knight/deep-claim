// Loads the real data/ files so that a typo in game content fails CI.
#include "sim/content.h"
#include "sim/simulation.h"
#include "sim/world_state.h"

#include "content/loader.h"
#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

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
    CHECK(plots == 30);

    CHECK(c.findResource("lithium").valid());
    CHECK(c.resources[c.findResource("lithium").index()].unlockedBy ==
          c.findTech("lithium_processing"));
}

TEST_CASE("A world generates from the shipped data", "[content][data][worldgen]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Simulation simulation(*result.content, 1, test::setup());
    const sim::WorldState& state = simulation.state();
    CHECK(state.plots.size() == 30);
    std::size_t depositsTotal = 0;
    for (const sim::PlotDeposits& deposits : state.hidden.plotDeposits) {
        depositsTotal += deposits.deposits.size();
        for (const sim::DepositTruth& truth : deposits.deposits) {
            CHECK(truth.tonnesTotal >= 1);
        }
    }
    // Priors sum to roughly 2.3 deposits per plot across the three regions.
    CHECK(depositsTotal > 20);
    CHECK(depositsTotal < 90);
}
