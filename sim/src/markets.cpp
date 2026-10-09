#include "sim/markets.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/events.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/math.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/tech.h"
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

Money executeSale(WorldState& state, CompanyId companyId, ResourceId resource,
                  std::int64_t tonnes) {
    Company& company = state.companies[companyId.index()];
    Market& market = state.markets[resource.index()];
    const Money proceeds = market.price * tonnes;
    company.inventoryTonnes[resource.index()] -= tonnes;
    company.cash += proceeds;
    // Saturate: beyond this the day's supply no longer moves the average.
    const std::int64_t counted = std::min(tonnes, kMaxSupplyPerDay);
    market.soldToday = std::min(kMaxSupplyPerDay - counted, market.soldToday) + counted;
    return proceeds;
}

void applySell(WorldState& state, const SellOre& command, TickReport& report) {
    const Money proceeds = executeSale(state, command.company, command.resource, command.tonnes);
    Notice notice = makeNotice(NoticeKind::OreSold, command.company, PlotId(), proceeds);
    notice.resource = command.resource;
    notice.tonnes = command.tonnes;
    report.notices.push_back(notice);
}

std::int64_t saleTonnesWithinBounds(const WorldState& state, CompanyId companyId,
                                    ResourceId resource, std::int64_t tonnes) {
    const Company& company = state.companies[companyId.index()];
    const std::int64_t stock = company.inventoryTonnes[resource.index()];
    const std::int64_t quantity = std::clamp<std::int64_t>(tonnes, 0, stock);
    const std::int64_t price = state.markets[resource.index()].price.cents();
    if (quantity == 0 || price <= 0) {
        return quantity;
    }
    // Proceeds may not push cash past the persisted bound.
    const std::int64_t room = kMaxCashCents - company.cash.cents();
    if (room <= 0) {
        return 0;
    }
    return std::min(quantity, room / price);
}

namespace {

std::optional<RejectReason> validatePolicyCompany(const WorldState& state, CompanyId company) {
    if (!company.valid() || company.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    if (state.companies[company.index()].bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    return std::nullopt;
}

void recordAutoSale(Company& company, std::uint32_t resource, std::int64_t tonnes,
                    Money proceeds) {
    company.autoSoldTonnes[resource] =
        std::min(kMaxStockTonnes, company.autoSoldTonnes[resource] + tonnes);
    company.autoSoldProceeds[resource] = Money::fromCents(
        std::min(kMaxCashCents, company.autoSoldProceeds[resource].cents() + proceeds.cents()));
}

}  // namespace

std::optional<RejectReason> validateSetSellPolicy(const WorldState& state,
                                                  const SetSellPolicy& command) {
    if (const auto reason = validatePolicyCompany(state, command.company)) {
        return reason;
    }
    if (!command.resource.valid() || command.resource.index() >= state.markets.size()) {
        return RejectReason::UnknownResource;
    }
    if (command.reserveTonnes < 0 || command.reserveTonnes > kMaxStockTonnes) {
        return RejectReason::InvalidQuantity;
    }
    if (command.floorPrice < Money::zero() || command.floorPrice.cents() > kMaxCashCents) {
        return RejectReason::InvalidAmount;
    }
    return std::nullopt;
}

void applySetSellPolicy(WorldState& state, const SetSellPolicy& command) {
    SellPolicy& policy =
        state.companies[command.company.index()].sellPolicies[command.resource.index()];
    policy.enabled = command.enabled != 0 ? 1 : 0;
    policy.reserveTonnes = command.reserveTonnes;
    policy.floorPrice = command.floorPrice;
}

std::optional<RejectReason> validateSetCoverCosts(const WorldState& state,
                                                  const SetCoverCosts& command) {
    return validatePolicyCompany(state, command.company);
}

void applySetCoverCosts(WorldState& state, const SetCoverCosts& command) {
    state.companies[command.company.index()].coverCosts = command.enabled != 0 ? 1 : 0;
}

void runSellPolicies(const Content& content, WorldState& state) {
    for (std::uint32_t c = 0; c < state.companies.size(); ++c) {
        Company& company = state.companies[c];
        if (company.bankrupt != 0) {
            continue;
        }
        for (std::uint32_t r = 0; r < state.markets.size(); ++r) {
            const SellPolicy& policy = company.sellPolicies[r];
            if (policy.enabled == 0 || state.markets[r].price < policy.floorPrice ||
                !resourceUnlocked(content, company, ResourceId(r))) {
                continue;
            }
            const std::int64_t above = company.inventoryTonnes[r] - policy.reserveTonnes;
            const std::int64_t tonnes =
                saleTonnesWithinBounds(state, CompanyId(c), ResourceId(r), above);
            if (tonnes <= 0) {
                continue;
            }
            const Money proceeds = executeSale(state, CompanyId(c), ResourceId(r), tonnes);
            recordAutoSale(company, r, tonnes, proceeds);
        }
    }
}

// The world economy grows: base demand compounds by demand_growth_per_year,
// evaluated continuously so there is no step at year boundaries.
Fixed demandGrowth(const BalanceParams& balance, Day day) {
    if (balance.demandGrowthPerYear == Fixed::zero()) {
        return Fixed::one();
    }
    const Fixed years = Fixed::fromRatio(day, kDaysPerYear);
    const Fixed exponent = clampFixed(years * fixedLn(Fixed::one() + balance.demandGrowthPerYear),
                                      Fixed::zero(), Fixed::fromInt(20));
    return fixedExp(exponent);
}

void runMarkets(const Content& content, WorldState& state) {
    const BalanceParams& balance = content.balance;
    const Fixed growth = demandGrowth(balance, state.day);
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

        // Supply: exponential moving average of what companies sold, kept in
        // fractional tonnes so small markets (a few tonnes a day) stay exact.
        const std::int64_t sold = std::min(market.soldToday, kMaxSupplyPerDay);
        market.supplySmoothed = market.supplySmoothed +
                                (Fixed::fromInt(sold) - market.supplySmoothed) /
                                    static_cast<std::int64_t>(balance.marketSupplySmoothingDays);
        market.supplySmoothed =
            clampFixed(market.supplySmoothed, Fixed::zero(), Fixed::fromInt(kMaxSupplyPerDay));
        market.soldToday = 0;

        // The world outside the game supplies exactly the base demand, so with
        // no company sales and no demand shift the price sits at base.
        const Fixed baseDemand = Fixed::fromInt(resource.baseDemandTonnesPerDay);
        // Combined demand factor is bounded before it meets base demand so the
        // product fits Fixed for any valid content (1e9 t x 1000 = 1e12 t).
        const Fixed demandFactor = clampFixed(
            market.demandMult * growth *
                eventMultiplier(content, state, EventEffectType::DemandMult, ResourceId(i)),
            Fixed::fromRatio(1, 100), Fixed::fromInt(1000));
        const Fixed demand = baseDemand * demandFactor;
        const Fixed supply = baseDemand + market.supplySmoothed;
        const Fixed ratio = clampFixed(demand / supply, kRatioMin, kRatioMax);
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
