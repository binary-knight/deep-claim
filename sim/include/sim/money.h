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
    [[nodiscard]] static constexpr Money fromUnits(std::int64_t units) {
        return fromCents(units * kCentsPerUnit);
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

    constexpr Money operator-() const { return fromCents(-cents_); }
    friend constexpr Money operator+(Money a, Money b) { return fromCents(a.cents_ + b.cents_); }
    friend constexpr Money operator-(Money a, Money b) { return fromCents(a.cents_ - b.cents_); }
    friend constexpr Money operator*(Money a, std::int64_t n) { return fromCents(a.cents_ * n); }
    friend constexpr Money operator*(std::int64_t n, Money a) { return fromCents(a.cents_ * n); }
    friend constexpr Money operator/(Money a, std::int64_t n) { return fromCents(a.cents_ / n); }

    constexpr Money& operator+=(Money other) {
        cents_ += other.cents_;
        return *this;
    }
    constexpr Money& operator-=(Money other) {
        cents_ -= other.cents_;
        return *this;
    }

private:
    std::int64_t cents_ = 0;
};

// Decimal rendering such as "-3.50". For logs and tests.
[[nodiscard]] std::string toString(Money value);

}  // namespace sim
