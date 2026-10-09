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
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

sim::WorldState generate(const sim::Content& content, sim::Seed seed) {
    sim::WorldState state;
    state.rng = sim::RngStreams(seed);
    sim::generateWorld(content, state);
    return state;
}

bool isPermutation(const std::vector<sim::PlotId>& queue, std::size_t count) {
    std::vector<int> seen(count, 0);
    for (const sim::PlotId id : queue) {
        if (!id.valid() || id.index() >= count || seen[id.index()]++ != 0) {
            return false;
        }
    }
    return queue.size() == count;
}

struct QueueShape {
    std::uint32_t handMineable = 0;
    std::uint32_t handLocked = 0;
    // A third plot in a row from one region, after the hand, while another
    // region still had land in the rest of the queue.
    bool avoidableRunOfThree = false;
};

QueueShape shapeOf(const sim::Content& content, const sim::WorldState& state) {
    QueueShape shape;
    const auto regionOf = [&state](std::size_t position) {
        return state.plots[state.listingQueue[position].index()].region;
    };
    const std::size_t hand =
        std::min<std::size_t>(content.balance.plotsListedAtStart, state.listingQueue.size());
    for (std::size_t i = 0; i < hand; ++i) {
        const sim::RegionStart kind =
            sim::classifyRegionAtStart(content, content.regions[regionOf(i).index()]);
        shape.handMineable += kind == sim::RegionStart::Mineable ? 1U : 0U;
        shape.handLocked += kind == sim::RegionStart::Locked ? 1U : 0U;
    }
    for (std::size_t i = std::max<std::size_t>(hand, 2); i < state.listingQueue.size(); ++i) {
        if (regionOf(i) != regionOf(i - 1) || regionOf(i) != regionOf(i - 2)) {
            continue;
        }
        for (std::size_t j = i; j < state.listingQueue.size(); ++j) {
            shape.avoidableRunOfThree = shape.avoidableRunOfThree || regionOf(j) != regionOf(i);
        }
    }
    return shape;
}

}  // namespace

TEST_CASE("Regions are classified by what a new company can mine", "[worldgen][opening]") {
    sim::Content content = test::makeContent();
    // North: iron on 80% of plots, no research needed.
    CHECK(sim::classifyRegionAtStart(content, content.regions[0]) == sim::RegionStart::Mineable);
    // Flats: lithium only, which needs research.
    CHECK(sim::classifyRegionAtStart(content, content.regions[1]) == sim::RegionStart::Locked);

    // A long-shot unlocked ore does not make a locked region mineable.
    sim::RegionDef mixed = content.regions[1];
    mixed.deposits.push_back(
        {sim::ResourceId(1), sim::Fixed::fromRatio(2, 10), 30'000, sim::Fixed::fromInt(2)});
    CHECK(sim::classifyRegionAtStart(content, mixed) == sim::RegionStart::Locked);
    // Exactly half the plots is enough.
    mixed.deposits.back().presenceProb = sim::Fixed::fromRatio(1, 2);
    CHECK(sim::classifyRegionAtStart(content, mixed) == sim::RegionStart::Mineable);

    // Only long-shot unlocked ore, or nothing at all: neither.
    sim::RegionDef thin = content.regions[0];
    thin.deposits.clear();
    CHECK(sim::classifyRegionAtStart(content, thin) == sim::RegionStart::Other);
    thin.deposits.push_back(
        {sim::ResourceId(0), sim::Fixed::fromRatio(4, 10), 100'000, sim::Fixed::fromInt(2)});
    CHECK(sim::classifyRegionAtStart(content, thin) == sim::RegionStart::Other);
}

TEST_CASE("The opening hand and the queue keep their promises over 200 seeds",
          "[worldgen][opening]") {
    const sim::Content content = test::makeContent();
    bool sawLockedInHand = false;
    for (sim::Seed seed = 1; seed <= 200; ++seed) {
        const sim::WorldState state = generate(content, seed);
        REQUIRE(isPermutation(state.listingQueue, state.plots.size()));
        const QueueShape shape = shapeOf(content, state);
        CHECK(shape.handMineable >= content.balance.openingMinMineable);
        CHECK(shape.handLocked <= content.balance.openingMaxLockedRegion);
        CHECK_FALSE(shape.avoidableRunOfThree);
        sawLockedInHand = sawLockedInHand || shape.handLocked > 0;

        // Same seed, same queue.
        const sim::WorldState again = generate(content, seed);
        CHECK(again.listingQueue == state.listingQueue);
    }
    // The cap is a cap, not a ban: locked land does turn up in some hands.
    CHECK(sawLockedInHand);
}

TEST_CASE("Opening rules give way when the regions cannot meet them", "[worldgen][opening]") {
    // Only locked land: the hand is all locked, and nothing aborts or loops.
    sim::Content lockedOnly = test::makeContent();
    lockedOnly.regions.erase(lockedOnly.regions.begin());
    for (sim::Seed seed = 1; seed <= 20; ++seed) {
        const sim::WorldState state = generate(lockedOnly, seed);
        REQUIRE(isPermutation(state.listingQueue, state.plots.size()));
        const QueueShape shape = shapeOf(lockedOnly, state);
        CHECK(shape.handMineable == 0);
        CHECK(shape.handLocked == 3);
        CHECK_FALSE(shape.avoidableRunOfThree);  // one region: every run is forced
    }

    // One mineable plot in the whole world, two asked for.
    sim::Content scarce = test::makeContent();
    scarce.regions[0].plotCount = 1;
    for (sim::Seed seed = 1; seed <= 20; ++seed) {
        const sim::WorldState state = generate(scarce, seed);
        REQUIRE(isPermutation(state.listingQueue, state.plots.size()));
        CHECK(shapeOf(scarce, state).handMineable == 1);
    }

    // A hand larger than the world, and no hand at all.
    sim::Content tiny = test::makeContent();
    tiny.regions[0].plotCount = 1;
    tiny.regions[1].plotCount = 1;
    CHECK(isPermutation(generate(tiny, 3).listingQueue, 2));
    sim::Content noHand = test::makeContent();
    noHand.balance.plotsListedAtStart = 0;
    noHand.balance.openingMinMineable = 0;
    const sim::WorldState unopened = generate(noHand, 3);
    CHECK(isPermutation(unopened.listingQueue, unopened.plots.size()));
    CHECK_FALSE(shapeOf(noHand, unopened).avoidableRunOfThree);

    // A cap of zero with only locked land: the cap gives way.
    sim::Content lockedStrict = lockedOnly;
    lockedStrict.balance.openingMaxLockedRegion = 0;
    const sim::WorldState capped = generate(lockedStrict, 5);
    CHECK(isPermutation(capped.listingQueue, capped.plots.size()));
    CHECK(shapeOf(lockedStrict, capped).handLocked == 3);

    // No land at all.
    sim::Content empty = test::makeContent();
    for (sim::RegionDef& region : empty.regions) {
        region.plotCount = 0;
    }
    CHECK(generate(empty, 5).listingQueue.empty());

    // A cap of zero keeps locked land out of the hand entirely.
    sim::Content strict = test::makeContent();
    strict.balance.openingMaxLockedRegion = 0;
    for (sim::Seed seed = 1; seed <= 50; ++seed) {
        CHECK(shapeOf(strict, generate(strict, seed)).handLocked == 0);
    }
}

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
        CHECK(plot.askPrice == plot.basePrice);

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

TEST_CASE("Listing queue is a shuffled permutation of all plots", "[worldgen]") {
    const sim::Content content = test::makeContent();
    const sim::WorldState state = generate(content, 5);
    CHECK(isPermutation(state.listingQueue, state.plots.size()));
    CHECK(state.nextListing == 0);
    // Not the identity order (probability of identity is 1/30!).
    bool identity = true;
    for (std::size_t i = 0; i < state.listingQueue.size(); ++i) {
        identity = identity && state.listingQueue[i] == sim::PlotId(static_cast<std::uint32_t>(i));
    }
    CHECK_FALSE(identity);
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
    const sim::Simulation a(content, 77, test::setup());
    const sim::Simulation b(content, 77, test::setup());
    const sim::Simulation c(content, 78, test::setup());
    CHECK(a.stateHash() == b.stateHash());
    CHECK(a.stateHash() != c.stateHash());
    CHECK(a.state().plots.size() == 30);
}
