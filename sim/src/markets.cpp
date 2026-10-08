#include "sim/markets.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/math.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace sim {

namespace {

// Demand/supply ratio is clamped so the power law stays tame.
const Fixed kRatioMin = Fixed::fromRatio(1, 5);
const Fixed kRatioMax = Fixed::fromInt(5);
const Fixed kMaxDemandSigma = Fixed::fromInt(3);
// Daily sales above this do not move the supply average any further.
constexpr std::int64_t kMaxSupplyPerDay = 1'000'000'000'000LL;

}  // namespace

void initMarkets(const Content& content, WorldState& state) {
    state.markets.clear();
    for (const ResourceDef& resource : content.resources) {
        Market market;
        market.price = resource.basePrice;
        market.demandMult = Fixed::one();
        market.supplySmoothed = Fixed::zero();
        market.priceHistory.push_back(market.price);
        state.markets.push_back(market);
    }
}

std::optional<RejectReason> validateSell(const WorldState& state, const SellOre& command) {
    if (!command.company.valid() || command.company.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    const Company& company = state.companies[command.company.index()];
    if (company.bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (!command.resource.valid() || command.resource.index() >= state.markets.size()) {
        return RejectReason::UnknownResource;
    }
    if (command.tonnes <= 0) {
        return RejectReason::InvalidQuantity;
    }
    if (command.tonnes > company.inventoryTonnes[command.resource.index()]) {
        return RejectReason::InsufficientInventory;
    }
    const Money price = state.markets[command.resource.index()].price;
    const std::optional<std::int64_t> proceeds = checkedMulDiv(price.cents(), command.tonnes, 1);
    if (!proceeds || *proceeds > kMaxCashCents - company.cash.cents()) {
        return RejectReason::SaleTooLarge;
    }
    return std::nullopt;
}

void applySell(WorldState& state, const SellOre& command, TickReport& report) {
    Company& company = state.companies[command.company.index()];
    Market& market = state.markets[command.resource.index()];
    const Money proceeds = market.price * command.tonnes;
    company.inventoryTonnes[command.resource.index()] -= command.tonnes;
    company.cash += proceeds;
    market.soldToday = checkedAdd(market.soldToday, command.tonnes);
    Notice notice = makeNotice(NoticeKind::OreSold, command.company, PlotId(), proceeds);
    notice.resource = command.resource;
    notice.tonnes = command.tonnes;
    report.notices.push_back(notice);
}

void runMarkets(const Content& content, WorldState& state) {
    const BalanceParams& balance = content.balance;
    for (std::uint32_t i = 0; i < state.markets.size(); ++i) {
        Market& market = state.markets[i];
        const ResourceDef& resource = content.resources[i];

        // Demand: mean-reverting random walk around 1.
        const Fixed z =
            clampFixed(approxNormal(state.rng.market), -kMaxDemandSigma, kMaxDemandSigma);
        market.demandMult = market.demandMult +
                            (Fixed::one() - market.demandMult) * balance.demandReversionRate +
                            resource.volatility * z;
        market.demandMult =
            clampFixed(market.demandMult, balance.demandMultMin, balance.demandMultMax);

        // Supply: exponential moving average of what companies sold.
        const std::int64_t sold = std::min(market.soldToday, kMaxSupplyPerDay);
        market.supplySmoothed = market.supplySmoothed +
                                (Fixed::fromInt(sold) - market.supplySmoothed) /
                                    static_cast<std::int64_t>(balance.marketSupplySmoothingDays);
        market.soldToday = 0;

        // The world outside the game supplies exactly the base demand, so with
        // no company sales and no demand shift the price sits at base.
        const std::int64_t baseDemand = resource.baseDemandTonnesPerDay;
        const std::int64_t demand = mulDiv(baseDemand, market.demandMult.raw(), Fixed::kScale);
        const std::int64_t supply = baseDemand + market.supplySmoothed.truncToInt();
        const Fixed ratio = clampFixed(Fixed::fromRatio(demand, supply), kRatioMin, kRatioMax);
        const Money target = resource.basePrice.scaled(fixedPow(ratio, resource.elasticity));

        // Move part of the way to the target each day, then clamp.
        market.price = market.price + (target - market.price) /
                                          static_cast<std::int64_t>(balance.marketPriceAdjustDays);
        const Money floor = resource.basePrice.scaled(balance.priceFloorMult);
        const Money cap = resource.basePrice.scaled(balance.priceCapMult);
        market.price = std::clamp(market.price, floor, cap);
        market.priceHistory.push_back(market.price);
    }
}

}  // namespace sim
