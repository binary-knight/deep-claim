#pragma once

#include "sim/fixed.h"

#include <compare>
#include <cstdint>
#include <string>

namespace sim {

// Currency amount in integer cents. Division and scaling truncate toward zero.
class Money {
public:
    static constexpr std::int64_t kCentsPerUnit = 100;

    constexpr Money() = default;

    [[nodiscard]] static constexpr Money fromCents(std::int64_t cents) {
        Money result;
        result.cents_ = cents;
        return result;
    }
    [[nodiscard]] static Money fromUnits(std::int64_t units) {
        return fromCents(mulDiv(units, kCentsPerUnit, 1));
    }
    [[nodiscard]] static constexpr Money zero() { return fromCents(0); }

    [[nodiscard]] constexpr std::int64_t cents() const { return cents_; }

    // this * factor, truncated to whole cents.
    [[nodiscard]] Money scaled(Fixed factor) const {
        return fromCents(mulDiv(cents_, factor.raw(), Fixed::kScale));
    }
    // this / other as a Fixed ratio. other must be non-zero.
    [[nodiscard]] Fixed ratioTo(Money other) const {
        return Fixed::fromRaw(mulDiv(cents_, Fixed::kScale, other.cents_));
    }

    friend constexpr auto operator<=>(Money, Money) = default;

    Money operator-() const { return fromCents(checkedNegate(cents_)); }
    friend Money operator+(Money a, Money b) { return fromCents(checkedAdd(a.cents_, b.cents_)); }
    friend Money operator-(Money a, Money b) { return fromCents(checkedSub(a.cents_, b.cents_)); }
    friend Money operator*(Money a, std::int64_t n) { return fromCents(mulDiv(a.cents_, n, 1)); }
    friend Money operator*(std::int64_t n, Money a) { return fromCents(mulDiv(a.cents_, n, 1)); }
    friend Money operator/(Money a, std::int64_t n) { return fromCents(mulDiv(a.cents_, 1, n)); }

    Money& operator+=(Money other) {
        cents_ = checkedAdd(cents_, other.cents_);
        return *this;
    }
    Money& operator-=(Money other) {
        cents_ = checkedSub(cents_, other.cents_);
        return *this;
    }

private:
    std::int64_t cents_ = 0;
};

// Decimal rendering such as "-3.50". For logs and tests.
[[nodiscard]] std::string toString(Money value);

}  // namespace sim
