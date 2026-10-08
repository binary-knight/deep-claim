#pragma once

// serialize() overloads for every sim type. Include this wherever a type is
// hashed, saved or loaded. See archive.h for the visitor protocol.

#include "sim/archive.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/rng.h"
#include "sim/world_state.h"

#include <cstdint>

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
    ar(plot.region, plot.areaHectares, plot.askPrice, plot.listing, plot.owner);
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
    ar(state.day, state.rng, state.plots, state.hidden);
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
       balance.loanMaxPrincipal, balance.surveyCost, balance.surveyDurationDays,
       balance.surveyBaseNoise, balance.mineCapexPerHectare, balance.mineBuildDays,
       balance.mineOpexPerDay, balance.auctionWindowDays, balance.plotsListedPerMonth,
       balance.bankruptcyGraceMonths);
}

template <class Archive>
void serialize(Archive& ar, Content& content) {
    ar(content.resources, content.techs, content.regions, content.events, content.balance);
}

}  // namespace sim
