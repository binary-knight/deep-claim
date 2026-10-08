#pragma once

// serialize() overloads for every sim type. Include this wherever a type is
// hashed, saved or loaded. See archive.h for the visitor protocol.

#include "sim/archive.h"
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/rng.h"
#include "sim/world_state.h"

#include <cstdint>
#include <variant>

namespace sim {

// ---- core types ----

template <class Archive>
void serialize(Archive& ar, Pcg32& rng) {
    std::uint64_t state = rng.state();
    std::uint64_t increment = rng.increment();
    ar(state, increment);
    if constexpr (Archive::kReading) {
        // Pcg32 increments are always odd by construction; an even one means
        // the stream is corrupt (and zero would make the generator absorbing).
        if ((increment & 1U) == 0U) {
            ar.fail();
        }
        rng.restore(state, increment);
    }
}

template <class Archive>
void serialize(Archive& ar, RngStreams& streams) {
    ar(streams.world, streams.market, streams.survey, streams.event, streams.ai);
}

template <class Archive>
void serialize(Archive& ar, Fixed& value) {
    std::int64_t raw = value.raw();
    ar(raw);
    if constexpr (Archive::kReading) {
        value = Fixed::fromRaw(raw);
    }
}

template <class Archive>
void serialize(Archive& ar, Money& value) {
    std::int64_t cents = value.cents();
    ar(cents);
    if constexpr (Archive::kReading) {
        value = Money::fromCents(cents);
    }
}

template <class Archive, class Tag>
void serialize(Archive& ar, StrongId<Tag>& id) {
    typename StrongId<Tag>::Value raw = id.value();
    ar(raw);
    if constexpr (Archive::kReading) {
        id = StrongId<Tag>(raw);
    }
}

// ---- world state ----

template <class Archive>
void serialize(Archive& ar, Plot& plot) {
    ar(plot.region, plot.areaHectares, plot.basePrice, plot.askPrice, plot.listing, plot.owner,
       plot.auction, plot.mine);
}

template <class Archive>
void serialize(Archive& ar, Company& company) {
    ar(company.name, company.kind, company.cash, company.inventoryTonnes, company.completedTechs,
       company.monthsInsolvent, company.bankrupt);
}

template <class Archive>
void serialize(Archive& ar, Market& market) {
    ar(market.price, market.demandMult, market.supplySmoothed, market.soldToday,
       market.priceHistory);
}

template <class Archive>
void serialize(Archive& ar, Research& research) {
    ar(research.company, research.tech, research.startedDay, research.completesDay,
       research.complete);
}

template <class Archive>
void serialize(Archive& ar, ActiveEvent& event) {
    ar(event.event, event.startedDay, event.endsDay, event.active);
}

template <class Archive>
void serialize(Archive& ar, Loan& loan) {
    ar(loan.company, loan.principal, loan.remaining, loan.principalPerMonth, loan.takenDay,
       loan.monthsRemaining, loan.active);
}

template <class Archive>
void serialize(Archive& ar, SurveyEstimate& estimate) {
    ar(estimate.resource, estimate.tonnesEstimate, estimate.relativeError);
}

template <class Archive>
void serialize(Archive& ar, Survey& survey) {
    ar(survey.company, survey.plot, survey.commissionedDay, survey.completesDay, survey.complete,
       survey.estimates);
}

template <class Archive>
void serialize(Archive& ar, Mine& mine) {
    ar(mine.plot, mine.owner, mine.builtDay, mine.operationalDay, mine.state, mine.extractedTonnes);
}

template <class Archive>
void serialize(Archive& ar, Bid& bid) {
    ar(bid.company, bid.amount, bid.day, bid.order);
}

template <class Archive>
void serialize(Archive& ar, Auction& auction) {
    ar(auction.plot, auction.opensOnDay, auction.closesOnDay, auction.bids, auction.open,
       auction.winner, auction.winningAmount);
}

template <class Archive>
void serialize(Archive& ar, DepositTruth& truth) {
    ar(truth.resource, truth.tonnesTotal, truth.tonnesRemaining);
}

template <class Archive>
void serialize(Archive& ar, PlotDeposits& deposits) {
    ar(deposits.deposits);
}

template <class Archive>
void serialize(Archive& ar, HiddenState& hidden) {
    ar(hidden.plotDeposits);
}

template <class Archive>
void serialize(Archive& ar, WorldState& state) {
    ar(state.day, state.rng, state.companies, state.plots, state.auctions, state.surveys,
       state.mines, state.markets, state.loans, state.research, state.events, state.listingQueue,
       state.nextListing, state.bidCounter, state.hidden);
}

// ---- commands (queued commands travel with saves) ----

template <class Archive>
void serialize(Archive& ar, BidOnPlot& bid) {
    ar(bid.company, bid.plot, bid.amount);
}

template <class Archive>
void serialize(Archive& ar, CommissionSurvey& command) {
    ar(command.company, command.plot);
}

template <class Archive>
void serialize(Archive& ar, BuildMine& command) {
    ar(command.company, command.plot);
}

template <class Archive>
void serialize(Archive& ar, SetMineState& command) {
    ar(command.company, command.mine, command.operate);
}

template <class Archive>
void serialize(Archive& ar, SellOre& command) {
    ar(command.company, command.resource, command.tonnes);
}

template <class Archive>
void serialize(Archive& ar, TakeLoan& command) {
    ar(command.company, command.principal);
}

template <class Archive>
void serialize(Archive& ar, RepayLoan& command) {
    ar(command.company, command.loan, command.amount);
}

template <class Archive>
void serialize(Archive& ar, StartResearch& command) {
    ar(command.company, command.tech);
}

template <class Archive>
void serialize(Archive& ar, Command& command) {
    auto index = static_cast<std::uint8_t>(command.index());
    ar(index);
    if constexpr (Archive::kReading) {
        switch (index) {
            case 0:
                command = BidOnPlot{};
                break;
            case 1:
                command = CommissionSurvey{};
                break;
            case 2:
                command = BuildMine{};
                break;
            case 3:
                command = SetMineState{};
                break;
            case 4:
                command = SellOre{};
                break;
            case 5:
                command = TakeLoan{};
                break;
            case 6:
                command = RepayLoan{};
                break;
            case 7:
                command = StartResearch{};
                break;
            default:
                ar.fail();
                return;
        }
    }
    std::visit([&](auto& concrete) { serialize(ar, concrete); }, command);
}

// ---- content (hashed into saves so a save cannot be opened against
// different data; never loaded from a save) ----

template <class Archive>
void serialize(Archive& ar, ResourceDef& def) {
    ar(def.id, def.name, def.basePrice, def.baseDemandTonnesPerDay, def.baseOutputTonnesPerDay,
       def.elasticity, def.volatility, def.storageCostPerTonneMonth, def.unlockedBy);
}

template <class Archive>
void serialize(Archive& ar, TechEffect& effect) {
    ar(effect.type, effect.value, effect.resource);
}

template <class Archive>
void serialize(Archive& ar, TechDef& def) {
    ar(def.id, def.name, def.cost, def.durationDays, def.prerequisites, def.effects);
}

template <class Archive>
void serialize(Archive& ar, DepositPrior& prior) {
    ar(prior.resource, prior.presenceProb, prior.tonnesMedian, prior.tonnesSpread);
}

template <class Archive>
void serialize(Archive& ar, RegionDef& def) {
    ar(def.id, def.name, def.plotCount, def.landPriceMin, def.landPriceMax, def.plotAreaMinHectares,
       def.plotAreaMaxHectares, def.deposits);
}

template <class Archive>
void serialize(Archive& ar, EventEffect& effect) {
    ar(effect.type, effect.resource, effect.value);
}

template <class Archive>
void serialize(Archive& ar, EventDef& def) {
    ar(def.id, def.name, def.dailyWeight, def.minYear, def.durationDays, def.effects);
}

template <class Archive>
void serialize(Archive& ar, BalanceParams& balance) {
    ar(balance.startingCapital, balance.loanAnnualRate, balance.loanTermMonths,
       balance.loanMaxPrincipal, balance.loanMinPrincipal, balance.maxActiveLoans,
       balance.surveyCost, balance.surveyDurationDays, balance.surveyBaseNoise,
       balance.mineCapexPerHectare, balance.mineBuildDays, balance.mineOpexPerDay,
       balance.mineMothballOpexMult, balance.auctionWindowDays, balance.plotsListedAtStart,
       balance.plotsListedPerMonth, balance.bankruptcyGraceMonths,
       balance.marketSupplySmoothingDays, balance.marketPriceAdjustDays,
       balance.demandReversionRate, balance.demandMultMin, balance.demandMultMax,
       balance.priceFloorMult, balance.priceCapMult);
}

template <class Archive>
void serialize(Archive& ar, Content& content) {
    ar(content.resources, content.techs, content.regions, content.events, content.balance);
}

}  // namespace sim
