#include "sim/math.h"

#include "sim/fixed.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>

using sim::Fixed;

namespace {

bool within(Fixed actual, Fixed expected, std::int64_t toleranceRaw) {
    return std::llabs(actual.raw() - expected.raw()) <= toleranceRaw;
}

bool withinRelative(Fixed actual, Fixed expected, std::int64_t partsPerMillion) {
    // Divide first: expected values reach 3.9e18 raw, so multiplying by the
    // ppm factor would overflow.
    const std::int64_t tolerance = std::llabs(expected.raw()) / Fixed::kScale * partsPerMillion + 1;
    return std::llabs(actual.raw() - expected.raw()) <= tolerance;
}

}  // namespace

TEST_CASE("fixedLn matches known values", "[math]") {
    CHECK(sim::fixedLn(Fixed::one()) == Fixed::zero());
    CHECK(within(sim::fixedLn(Fixed::fromInt(2)), Fixed::fromRaw(693'147), 10));
    CHECK(within(sim::fixedLn(Fixed::fromInt(10)), Fixed::fromRaw(2'302'585), 10));
    CHECK(within(sim::fixedLn(Fixed::fromRatio(1, 2)), Fixed::fromRaw(-693'147), 10));
    CHECK(within(sim::fixedLn(Fixed::fromRaw(1)), Fixed::fromRaw(-13'815'511), 20));
    CHECK(within(sim::fixedLn(Fixed::fromInt(1'000'000)), Fixed::fromRaw(13'815'511), 20));
}

TEST_CASE("fixedExp matches known values", "[math]") {
    CHECK(sim::fixedExp(Fixed::zero()) == Fixed::one());
    CHECK(within(sim::fixedExp(Fixed::one()), Fixed::fromRaw(2'718'282), 10));
    CHECK(within(sim::fixedExp(-Fixed::one()), Fixed::fromRaw(367'879), 10));
    CHECK(within(sim::fixedExp(Fixed::fromRaw(693'147)), Fixed::fromInt(2), 10));
    CHECK(withinRelative(sim::fixedExp(Fixed::fromInt(10)), Fixed::fromRaw(22'026'465'795), 20));
    CHECK(withinRelative(sim::fixedExp(Fixed::fromInt(-10)), Fixed::fromRaw(45), 100'000));
    // Top of the supported range: e^29 = 3.93e12.
    CHECK(withinRelative(sim::fixedExp(Fixed::fromInt(29)),
                         Fixed::fromRaw(3'931'334'297'144'042'074LL), 50));
}

TEST_CASE("exp inverts ln", "[math]") {
    for (const std::int64_t raw : {1'000LL, 500'000LL, 1'000'000LL, 7'250'000LL, 1'000'000'000LL}) {
        const Fixed x = Fixed::fromRaw(raw);
        CHECK(withinRelative(sim::fixedExp(sim::fixedLn(x)), x, 50));
    }
}

TEST_CASE("fixedPow handles fractional and negative exponents", "[math]") {
    CHECK(within(sim::fixedPow(Fixed::fromRatio(5, 2), Fixed::fromInt(2)), Fixed::fromRatio(25, 4),
                 50));
    CHECK(within(sim::fixedPow(Fixed::fromInt(4), Fixed::fromRatio(1, 2)), Fixed::fromInt(2), 20));
    CHECK(within(sim::fixedPow(Fixed::fromRatio(5, 2), -Fixed::one()), Fixed::fromRatio(2, 5), 10));
    CHECK(sim::fixedPow(Fixed::fromInt(7), Fixed::zero()) == Fixed::one());
    CHECK(withinRelative(sim::fixedPow(Fixed::fromInt(3), Fixed::fromInt(3)), Fixed::fromInt(27),
                         50));
}

TEST_CASE("ln and exp are monotonic", "[math]") {
    Fixed previousLn = sim::fixedLn(Fixed::fromRaw(1));
    Fixed previousExp = sim::fixedExp(Fixed::fromInt(-20));
    for (std::int64_t i = 1; i <= 200; ++i) {
        const Fixed lnValue = sim::fixedLn(Fixed::fromRaw(i * 37'000 + 1));
        CHECK(lnValue >= previousLn);
        previousLn = lnValue;
        const Fixed expValue = sim::fixedExp(Fixed::fromRaw(-20'000'000 + i * 200'000));
        CHECK(expValue >= previousExp);
        previousExp = expValue;
    }
}

TEST_CASE("clampFixed clamps", "[math]") {
    CHECK(sim::clampFixed(Fixed::fromInt(5), Fixed::zero(), Fixed::one()) == Fixed::one());
    CHECK(sim::clampFixed(Fixed::fromInt(-5), Fixed::zero(), Fixed::one()) == Fixed::zero());
    CHECK(sim::clampFixed(Fixed::fromRatio(1, 2), Fixed::zero(), Fixed::one()) ==
          Fixed::fromRatio(1, 2));
}
