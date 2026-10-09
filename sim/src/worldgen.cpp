#include "sim/worldgen.h"

#include "sim/content.h"
#include "sim/fatal.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/math.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace sim {

namespace {

// Deposit sizes are log-normal around the prior median; z is clamped so one
// freak draw cannot produce an absurd plot.
const Fixed kMaxDepositSigma = Fixed::fromInt(3);
// Land price noise around the area-based interpolation, as a fraction.
const Fixed kPriceNoise = Fixed::fromRatio(1, 5);
constexpr std::int64_t kMaxDepositTonnes = 1'000'000'000'000'000LL;

Money interpolatePrice(const RegionDef& region, std::int64_t area, Pcg32& rng) {
    // Linear in area between the region's min and max, then +/- up to 20%
    // noise so price alone does not pin down plot size.
    const std::int64_t areaSpan = region.plotAreaMaxHectares - region.plotAreaMinHectares;
    const Fixed t = areaSpan == 0 ? Fixed::zero()
                                  : Fixed::fromRatio(area - region.plotAreaMinHectares, areaSpan);
    const Money base = region.landPriceMin + (region.landPriceMax - region.landPriceMin).scaled(t);
    const Fixed noise = clampFixed(approxNormal(rng), -Fixed::one(), Fixed::one()) * kPriceNoise;
    return base.scaled(Fixed::one() + noise);
}

std::int64_t sampleTonnes(const DepositPrior& prior, const RegionDef& region, std::int64_t area,
                          Pcg32& rng) {
    const Fixed z = clampFixed(approxNormal(rng), -kMaxDepositSigma, kMaxDepositSigma);
    // median * spread^z, then scaled by area relative to the region's mean
    // plot area so bigger plots hold proportionally more ore. Negative z is
    // applied as a division by the positive power: a factor below one loses
    // relative precision in six decimals, a factor above one does not.
    std::int64_t scaled = 0;
    if (z >= Fixed::zero()) {
        scaled = mulDiv(prior.tonnesMedian, fixedPow(prior.tonnesSpread, z).raw(), Fixed::kScale);
    } else {
        scaled = mulDiv(prior.tonnesMedian, Fixed::kScale, fixedPow(prior.tonnesSpread, -z).raw());
    }
    // mean area = (min + max) / 2, kept exact by doubling the numerator.
    std::int64_t tonnes =
        mulDiv(scaled, 2 * area, region.plotAreaMinHectares + region.plotAreaMaxHectares);
    // The cap applies to what the plot ends up holding, area included.
    const std::int64_t cap =
        checkedMulDiv(prior.tonnesMedian, prior.tonnesMaxMult.raw(), Fixed::kScale)
            .value_or(kMaxDepositTonnes);
    tonnes = std::min({tonnes, cap, kMaxDepositTonnes});
    return std::max<std::int64_t>(tonnes, 1);
}

// The plots not yet placed in the listing queue, grouped by region so a draw
// restricted to some regions costs a walk over the regions, not the plots.
class PlotPool {
public:
    PlotPool(const Content& content, const WorldState& state) : byRegion_(content.regions.size()) {
        for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
            byRegion_[state.plots[i].region.index()].emplace_back(i);
        }
    }

    // Number of plots left in the regions `allowed` accepts.
    template <class Allowed>
    [[nodiscard]] std::uint32_t count(const Allowed& allowed) const {
        std::uint32_t total = 0;
        for (std::size_t r = 0; r < byRegion_.size(); ++r) {
            if (allowed(r)) {
                total += static_cast<std::uint32_t>(byRegion_[r].size());
            }
        }
        return total;
    }

    // Removes and returns a plot drawn uniformly from the allowed regions.
    // Precondition: count(allowed) > 0.
    template <class Allowed>
    PlotId draw(Pcg32& rng, const Allowed& allowed) {
        std::uint32_t pick = rng.below(count(allowed));
        for (std::size_t r = 0; r < byRegion_.size(); ++r) {
            if (!allowed(r)) {
                continue;
            }
            std::vector<PlotId>& plots = byRegion_[r];
            if (pick < plots.size()) {
                const PlotId chosen = plots[pick];
                plots[pick] = plots.back();
                plots.pop_back();
                return chosen;
            }
            pick -= static_cast<std::uint32_t>(plots.size());
        }
        fatal("PlotPool::draw: nothing to draw");
    }

private:
    std::vector<std::vector<PlotId>> byRegion_;
};

// The opening hand: the mineable plots first, then the rest of the hand with
// the cap on locked regions. Each rule is dropped only when no remaining plot
// can meet it, so a one-region world still gets a hand.
void drawOpeningHand(const BalanceParams& balance, const std::vector<RegionStart>& kinds,
                     const WorldState& state, Pcg32& rng, PlotPool& pool,
                     std::vector<PlotId>& queue, std::size_t handSize) {
    const auto mineable = [&kinds](std::size_t r) { return kinds[r] == RegionStart::Mineable; };
    const auto unlocked = [&kinds](std::size_t r) { return kinds[r] != RegionStart::Locked; };
    const auto any = [](std::size_t) { return true; };

    while (queue.size() < handSize && queue.size() < balance.openingMinMineable &&
           pool.count(mineable) > 0) {
        queue.push_back(pool.draw(rng, mineable));
    }
    std::uint32_t lockedInHand = 0;
    while (queue.size() < handSize) {
        // Under the cap any plot will do; at the cap, only unlocked regions,
        // unless locked regions are all that is left.
        const bool atCap = lockedInHand >= balance.openingMaxLockedRegion;
        const PlotId plot =
            atCap && pool.count(unlocked) > 0 ? pool.draw(rng, unlocked) : pool.draw(rng, any);
        if (kinds[state.plots[plot.index()].region.index()] == RegionStart::Locked) {
            ++lockedInHand;
        }
        queue.push_back(plot);
    }
    // The hand is listed together; shuffle so the mineable plots are not
    // always the first ids on the auction board.
    for (std::size_t i = queue.size(); i > 1; --i) {
        const std::size_t j = rng.below(static_cast<std::uint32_t>(i));
        std::swap(queue[i - 1], queue[j]);
    }
}

// After the hand: every remaining plot in random order, except that a third
// plot in a row from one region is redrawn from the others while any other
// region still has land.
void drawRemainder(const WorldState& state, Pcg32& rng, PlotPool& pool,
                   std::vector<PlotId>& queue) {
    const auto any = [](std::size_t) { return true; };
    const auto regionOf = [&state](PlotId plot) { return state.plots[plot.index()].region; };
    while (queue.size() < state.plots.size()) {
        const std::size_t n = queue.size();
        const bool runOfTwo = n >= 2 && regionOf(queue[n - 1]) == regionOf(queue[n - 2]);
        if (runOfTwo) {
            const std::size_t blocked = regionOf(queue[n - 1]).index();
            const auto others = [blocked](std::size_t r) { return r != blocked; };
            if (pool.count(others) > 0) {
                queue.push_back(pool.draw(rng, others));
                continue;
            }
        }
        queue.push_back(pool.draw(rng, any));
    }
}

void buildListingQueue(const Content& content, WorldState& state) {
    std::vector<RegionStart> kinds;
    kinds.reserve(content.regions.size());
    for (const RegionDef& region : content.regions) {
        kinds.push_back(classifyRegionAtStart(content, region));
    }
    PlotPool pool(content, state);
    Pcg32& rng = state.rng.world;
    state.listingQueue.reserve(state.plots.size());
    const std::size_t handSize =
        std::min<std::size_t>(content.balance.plotsListedAtStart, state.plots.size());
    drawOpeningHand(content.balance, kinds, state, rng, pool, state.listingQueue, handSize);
    drawRemainder(state, rng, pool, state.listingQueue);
}

}  // namespace

void generateWorld(const Content& content, WorldState& state) {
    Pcg32& rng = state.rng.world;
    state.plots.clear();
    state.hidden.plotDeposits.clear();
    state.listingQueue.clear();
    state.nextListing = 0;
    state.withdrawnQueue.clear();

    for (std::uint32_t regionIndex = 0; regionIndex < content.regions.size(); ++regionIndex) {
        const RegionDef& region = content.regions[regionIndex];
        for (std::uint32_t i = 0; i < region.plotCount; ++i) {
            Plot plot;
            plot.region = RegionId(regionIndex);
            plot.areaHectares =
                uniformInt(rng, region.plotAreaMinHectares, region.plotAreaMaxHectares);
            plot.basePrice = interpolatePrice(region, plot.areaHectares, rng);
            plot.askPrice = plot.basePrice;

            PlotDeposits deposits;
            for (const DepositPrior& prior : region.deposits) {
                if (uniformFixed(rng) >= prior.presenceProb) {
                    continue;
                }
                DepositTruth truth;
                truth.resource = prior.resource;
                truth.tonnesTotal = sampleTonnes(prior, region, plot.areaHectares, rng);
                truth.tonnesRemaining = truth.tonnesTotal;
                deposits.deposits.push_back(truth);
            }

            state.plots.push_back(plot);
            state.hidden.plotDeposits.push_back(std::move(deposits));
        }
    }

    buildListingQueue(content, state);
}

RegionStart classifyRegionAtStart(const Content& content, const RegionDef& region) {
    bool hasLocked = false;
    for (const DepositPrior& prior : region.deposits) {
        if (!prior.resource.valid() || prior.resource.index() >= content.resources.size()) {
            fatal("classifyRegionAtStart: a deposit names a resource that does not exist");
        }
        const bool lockedOre = content.resources[prior.resource.index()].unlockedBy.valid();
        // An ore that never turns up makes nothing mineable, whatever the
        // threshold: a threshold of zero means "any chance", not "none".
        if (!lockedOre && prior.presenceProb > Fixed::zero() &&
            prior.presenceProb >= content.balance.openingMineablePresence) {
            return RegionStart::Mineable;
        }
        hasLocked = hasLocked || lockedOre;
    }
    return hasLocked ? RegionStart::Locked : RegionStart::Other;
}

}  // namespace sim
