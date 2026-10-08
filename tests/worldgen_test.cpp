#include "sim/worldgen.h"

#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/simulation.h"
#include "sim/world_state.h"

#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

sim::WorldState generate(const sim::Content& content, sim::Seed seed) {
    sim::WorldState state;
    state.rng = sim::RngStreams(seed);
    sim::generateWorld(content, state);
    return state;
}

}  // namespace

TEST_CASE("World generation creates every plot in region order", "[worldgen]") {
    const sim::Content content = test::makeContent();
    const sim::WorldState state = generate(content, 1);

    REQUIRE(state.plots.size() == 30);
    REQUIRE(state.hidden.plotDeposits.size() == 30);
    for (std::size_t i = 0; i < state.plots.size(); ++i) {
        const sim::Plot& plot = state.plots[i];
        CHECK(plot.region == (i < 20 ? sim::RegionId(0) : sim::RegionId(1)));
        CHECK(plot.listing == sim::PlotListing::Unlisted);
        CHECK_FALSE(plot.owner.valid());

        const sim::RegionDef& region = content.regions[plot.region.index()];
        CHECK(plot.areaHectares >= region.plotAreaMinHectares);
        CHECK(plot.areaHectares <= region.plotAreaMaxHectares);
        // Base price interpolates between min and max, then +/- 20% noise.
        CHECK(plot.askPrice >= region.landPriceMin.scaled(sim::Fixed::fromRatio(8, 10)));
        CHECK(plot.askPrice <= region.landPriceMax.scaled(sim::Fixed::fromRatio(12, 10)));

        for (const sim::DepositTruth& truth : state.hidden.plotDeposits[i].deposits) {
            CHECK(truth.tonnesTotal >= 1);
            CHECK(truth.tonnesRemaining == truth.tonnesTotal);
            const bool allowed = std::any_of(
                region.deposits.begin(), region.deposits.end(),
                [&](const sim::DepositPrior& p) { return p.resource == truth.resource; });
            CHECK(allowed);
        }
    }
}

TEST_CASE("Deposit presence follows the prior probability", "[worldgen]") {
    const sim::Content content = test::makeContent();
    int ironPresent = 0;
    int northPlots = 0;
    for (sim::Seed seed = 1; seed <= 200; ++seed) {
        const sim::WorldState state = generate(content, seed);
        for (std::size_t i = 0; i < 20; ++i) {
            ++northPlots;
            const auto& deposits = state.hidden.plotDeposits[i].deposits;
            ironPresent += std::any_of(deposits.begin(), deposits.end(), [](const auto& d) {
                return d.resource == sim::ResourceId(0);
            });
        }
    }
    const double rate = static_cast<double>(ironPresent) / northPlots;
    CHECK(rate > 0.76);
    CHECK(rate < 0.84);
}

TEST_CASE("Deposit sizes are log-normal around the prior median", "[worldgen]") {
    const sim::Content content = test::makeContent();
    std::vector<std::int64_t> tonnes;
    for (sim::Seed seed = 1; seed <= 200; ++seed) {
        const sim::WorldState state = generate(content, seed);
        for (std::size_t i = 20; i < 30; ++i) {  // flats: fixed 500 ha, lithium only
            for (const sim::DepositTruth& truth : state.hidden.plotDeposits[i].deposits) {
                tonnes.push_back(truth.tonnesTotal);
            }
        }
    }
    REQUIRE(tonnes.size() > 500);
    std::sort(tonnes.begin(), tonnes.end());
    const std::int64_t median = tonnes[tonnes.size() / 2];
    CHECK(median > 4'800);  // prior median 6000, +/- 20%
    CHECK(median < 7'200);
    // 84th percentile should sit near median * spread (3).
    const std::int64_t p84 = tonnes[tonnes.size() * 84 / 100];
    CHECK(p84 > 14'000);
    CHECK(p84 < 22'000);
    // Never absurd: clamped to +/- 3 sigma means at most 6000 * 3^3 = 162000.
    CHECK(tonnes.back() <= 162'000);
    CHECK(tonnes.front() >= 222);
}

TEST_CASE("World generation is deterministic per seed", "[worldgen][determinism]") {
    const sim::Content content = test::makeContent();
    const sim::Simulation a(content, 77);
    const sim::Simulation b(content, 77);
    const sim::Simulation c(content, 78);
    CHECK(a.stateHash() == b.stateHash());
    CHECK(a.stateHash() != c.stateHash());
    CHECK(a.state().plots.size() == 30);
}
