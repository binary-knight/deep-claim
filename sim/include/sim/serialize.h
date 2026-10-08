#pragma once

// serialize() overloads for every sim type. Include this wherever a type is
// hashed, saved or loaded. See archive.h for the visitor protocol.

#include "sim/archive.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/rng.h"
#include "sim/world_state.h"

#include <cstdint>

namespace sim {

template <class Archive>
void serialize(Archive& ar, Pcg32& rng) {
    std::uint64_t state = rng.state();
    std::uint64_t increment = rng.increment();
    ar(state, increment);
    if constexpr (Archive::kReading) {
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

template <class Archive>
void serialize(Archive& ar, WorldState& state) {
    ar(state.day, state.rng, state.noise);
}

}  // namespace sim
