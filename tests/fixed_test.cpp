#include "sim/fixed.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using sim::Fixed;

TEST_CASE("Fixed construction and conversion", "[fixed]") {
    CHECK(Fixed().raw() == 0);
    CHECK(Fixed::fromInt(3).raw() == 3'000'000);
    CHECK(Fixed::fromRatio(1, 4).raw() == 250'000);
    CHECK(Fixed::fromRatio(-1, 3).raw() == -333'333);
    CHECK(Fixed::fromRatio(2, 3).truncToInt() == 0);
    CHECK(Fixed::fromRatio(-5, 3).truncToInt() == -1);
    CHECK(Fixed::fromInt(-7).truncToInt() == -7);
    CHECK(Fixed::one().raw() == Fixed::kScale);
}

TEST_CASE("Fixed multiplication truncates toward zero", "[fixed]") {
    CHECK((Fixed::fromRatio(1, 2) * Fixed::fromRatio(1, 2)).raw() == 250'000);
    CHECK((Fixed::fromRatio(-1, 3) * Fixed::one()).raw() == -333'333);
    // 0.333333 * 0.333333 = 0.111110888889
    CHECK((Fixed::fromRatio(-1, 3) * Fixed::fromRatio(-1, 3)).raw() == 111'110);
    CHECK((Fixed::fromRatio(1, 3) * Fixed::fromRatio(-1, 3)).raw() == -111'110);
    CHECK((Fixed::fromRaw(1) * Fixed::fromRaw(999'999)).raw() == 0);
    CHECK((Fixed::fromRaw(-1) * Fixed::fromRaw(999'999)).raw() == 0);
}

TEST_CASE("Fixed division truncates toward zero", "[fixed]") {
    CHECK((Fixed::one() / Fixed::fromInt(3)).raw() == 333'333);
    CHECK((-Fixed::one() / Fixed::fromInt(3)).raw() == -333'333);
    CHECK((Fixed::one() / Fixed::fromInt(-3)).raw() == -333'333);
    CHECK((-Fixed::one() / Fixed::fromInt(-3)).raw() == 333'333);
    CHECK((Fixed::fromInt(7) / Fixed::fromInt(2)).raw() == 3'500'000);
    CHECK((Fixed::fromInt(7) / 2).raw() == 3'500'000);
    CHECK((Fixed::fromInt(-7) / 2).raw() == -3'500'000);
}

TEST_CASE("Fixed products use a 128-bit intermediate", "[fixed]") {
    // 3e9 * 2.5: the raw product 3e15 * 2.5e6 overflows int64 without it.
    const Fixed big = Fixed::fromInt(3'000'000'000LL);
    CHECK((big * Fixed::fromRatio(5, 2)).raw() == 7'500'000'000'000'000LL);
    CHECK((big / Fixed::fromRatio(1, 4)).raw() == 12'000'000'000'000'000LL);
}

TEST_CASE("checkedMulDiv rejects overflow and zero divisors", "[fixed]") {
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
    CHECK_FALSE(sim::checkedMulDiv(1, 1, 0).has_value());
    CHECK_FALSE(sim::checkedMulDiv(kMax, 2, 1).has_value());
    CHECK_FALSE(sim::checkedMulDiv(kMin, 1, -1).has_value());
    CHECK(sim::checkedMulDiv(kMin, 1, 1) == kMin);
    CHECK(sim::checkedMulDiv(kMax, -1, 1) == -kMax);
    CHECK(sim::checkedMulDiv(kMax, kMax, kMax) == kMax);
    CHECK(sim::checkedMulDiv(-6, 7, 4) == -10);
    CHECK(sim::checkedMulDiv(6, -7, -4) == 10);
    CHECK(sim::checkedMulDiv(0, kMax, 1) == 0);
}

TEST_CASE("Fixed::fromDouble rounds to nearest", "[fixed]") {
    CHECK(Fixed::fromDouble(0.7).raw() == 700'000);
    CHECK(Fixed::fromDouble(-0.7).raw() == -700'000);
    CHECK(Fixed::fromDouble(1.0000004).raw() == 1'000'000);
    CHECK(Fixed::fromDouble(1.0000006).raw() == 1'000'001);
}

TEST_CASE("Fixed comparisons and additive arithmetic", "[fixed]") {
    const Fixed a = Fixed::fromRatio(3, 4);
    const Fixed b = Fixed::fromRatio(1, 4);
    CHECK((a + b) == Fixed::one());
    CHECK((a - b) == Fixed::fromRatio(1, 2));
    CHECK((-a).raw() == -750'000);
    CHECK((a * 4).raw() == 3'000'000);
    CHECK((4 * b) == Fixed::one());
    CHECK(b < a);
    CHECK(a >= b);
    Fixed c = a;
    c += b;
    c -= Fixed::fromRatio(1, 2);
    CHECK(c == Fixed::fromRatio(1, 2));
}

TEST_CASE("Fixed renders as decimal text", "[fixed]") {
    CHECK(sim::toString(Fixed::fromRatio(1, 3)) == "0.333333");
    CHECK(sim::toString(Fixed::fromRatio(-1, 3)) == "-0.333333");
    CHECK(sim::toString(Fixed::fromRatio(25, 2)) == "12.500000");
    CHECK(sim::toString(Fixed::zero()) == "0.000000");
    CHECK(sim::toString(Fixed::fromRaw(-1)) == "-0.000001");
}
