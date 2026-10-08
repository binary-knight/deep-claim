#include "sim/money.h"

#include "sim/fixed.h"

#include <catch2/catch_test_macros.hpp>

using sim::Fixed;
using sim::Money;

TEST_CASE("Money construction and arithmetic", "[money]") {
    CHECK(Money().cents() == 0);
    CHECK(Money::fromUnits(12).cents() == 1200);
    CHECK(Money::fromCents(-350).cents() == -350);
    CHECK((Money::fromUnits(5) + Money::fromCents(50)).cents() == 550);
    CHECK((Money::fromUnits(5) - Money::fromCents(50)).cents() == 450);
    CHECK((Money::fromCents(7) * 3).cents() == 21);
    CHECK((3 * Money::fromCents(7)).cents() == 21);
    CHECK((Money::fromCents(-7) / 2).cents() == -3);
    CHECK((-Money::fromCents(7)).cents() == -7);
    Money balance = Money::fromUnits(10);
    balance -= Money::fromUnits(4);
    balance += Money::fromCents(1);
    CHECK(balance.cents() == 601);
    CHECK(Money::fromCents(1) < Money::fromCents(2));
    CHECK(Money::fromUnits(1) == Money::fromCents(100));
}

TEST_CASE("Money scaling by Fixed truncates toward zero", "[money]") {
    const Fixed half = Fixed::fromRatio(1, 2);
    CHECK(Money::fromCents(1).scaled(half).cents() == 0);
    CHECK(Money::fromCents(3).scaled(half).cents() == 1);
    CHECK(Money::fromCents(-3).scaled(half).cents() == -1);
    CHECK(Money::fromUnits(100).scaled(Fixed::fromRatio(7, 100)).cents() == 700);
}

TEST_CASE("Money scaling of large amounts uses a 128-bit intermediate", "[money]") {
    // 1e15 units * 2.5 = 2.5e15 units; the raw intermediate is 2.5e23.
    const Money huge = Money::fromUnits(1'000'000'000'000'000LL);
    CHECK(huge.scaled(Fixed::fromRatio(5, 2)).cents() == 250'000'000'000'000'000LL);
}

TEST_CASE("Money ratios", "[money]") {
    CHECK(Money::fromCents(50).ratioTo(Money::fromCents(200)) == Fixed::fromRatio(1, 4));
    CHECK(Money::fromCents(1).ratioTo(Money::fromCents(3)).raw() == 333'333);
    CHECK(Money::fromCents(-1).ratioTo(Money::fromCents(3)).raw() == -333'333);
}

TEST_CASE("Money renders as decimal text", "[money]") {
    CHECK(sim::toString(Money::fromUnits(12)) == "12.00");
    CHECK(sim::toString(Money::fromCents(-350)) == "-3.50");
    CHECK(sim::toString(Money::fromCents(5)) == "0.05");
    CHECK(sim::toString(Money::fromCents(-5)) == "-0.05");
}
