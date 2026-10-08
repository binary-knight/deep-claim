#include "sim/worldgen.h"

#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/math.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/world_state.h"

#include <cstdint>
#include <utility>

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
    if (tonnes < 1) {
        tonnes = 1;
    }
    if (tonnes > kMaxDepositTonnes) {
        tonnes = kMaxDepositTonnes;
    }
    return tonnes;
}

}  // namespace

void generateWorld(const Content& content, WorldState& state) {
    Pcg32& rng = state.rng.world;
    state.plots.clear();
    state.hidden.plotDeposits.clear();
    state.listingQueue.clear();
    state.nextListing = 0;

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

    // Listing order: a Fisher-Yates shuffle so regions interleave and the
    // player cannot predict which plot comes up next.
    state.listingQueue.reserve(state.plots.size());
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        state.listingQueue.emplace_back(i);
    }
    for (std::size_t i = state.listingQueue.size(); i > 1; --i) {
        const std::size_t j = rng.below(static_cast<std::uint32_t>(i));
        std::swap(state.listingQueue[i - 1], state.listingQueue[j]);
    }
}

}  // namespace sim
