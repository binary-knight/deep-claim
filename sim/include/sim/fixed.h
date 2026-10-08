#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>

namespace sim {

// (a * b) / c with a 128-bit intermediate, truncating toward zero.
// Returns nullopt when c is zero or the quotient does not fit in int64.
[[nodiscard]] std::optional<std::int64_t> checkedMulDiv(std::int64_t a, std::int64_t b,
                                                        std::int64_t c);

// Same as checkedMulDiv but asserts the preconditions (release builds return 0
// on violation). Every Fixed and Money multiply or divide funnels through here,
// so it is the only platform-specific arithmetic in the sim.
[[nodiscard]] std::int64_t mulDiv(std::int64_t a, std::int64_t b, std::int64_t c);

// Decimal fixed-point number with six fractional digits (one unit = 1,000,000
// raw). Used for ratios, probabilities and multipliers. All arithmetic
// truncates toward zero. The sim never uses floating point for state.
class Fixed {
public:
    static constexpr std::int64_t kScale = 1'000'000;

    constexpr Fixed() = default;

    [[nodiscard]] static constexpr Fixed fromRaw(std::int64_t raw) {
        Fixed result;
        result.raw_ = raw;
        return result;
    }
    [[nodiscard]] static constexpr Fixed fromInt(std::int64_t whole) {
        return fromRaw(whole * kScale);
    }
    // numerator / denominator, truncated. |numerator| must be below 9.2e12.
    [[nodiscard]] static constexpr Fixed fromRatio(std::int64_t numerator,
                                                   std::int64_t denominator) {
        return fromRaw(numerator * kScale / denominator);
    }
    // Content-loading boundary only. Rounds to nearest so that a JSON value
    // such as 0.7 becomes exactly 700000 raw. Never call this from sim logic.
    [[nodiscard]] static Fixed fromDouble(double value);

    [[nodiscard]] static constexpr Fixed zero() { return fromRaw(0); }
    [[nodiscard]] static constexpr Fixed one() { return fromRaw(kScale); }

    [[nodiscard]] constexpr std::int64_t raw() const { return raw_; }
    [[nodiscard]] constexpr std::int64_t truncToInt() const { return raw_ / kScale; }

    friend constexpr auto operator<=>(Fixed, Fixed) = default;

    constexpr Fixed operator-() const { return fromRaw(-raw_); }
    friend constexpr Fixed operator+(Fixed a, Fixed b) { return fromRaw(a.raw_ + b.raw_); }
    friend constexpr Fixed operator-(Fixed a, Fixed b) { return fromRaw(a.raw_ - b.raw_); }
    friend Fixed operator*(Fixed a, Fixed b) { return fromRaw(mulDiv(a.raw_, b.raw_, kScale)); }
    friend Fixed operator/(Fixed a, Fixed b) { return fromRaw(mulDiv(a.raw_, kScale, b.raw_)); }
    friend constexpr Fixed operator*(Fixed a, std::int64_t n) { return fromRaw(a.raw_ * n); }
    friend constexpr Fixed operator*(std::int64_t n, Fixed a) { return fromRaw(a.raw_ * n); }
    friend constexpr Fixed operator/(Fixed a, std::int64_t n) { return fromRaw(a.raw_ / n); }

    constexpr Fixed& operator+=(Fixed other) {
        raw_ += other.raw_;
        return *this;
    }
    constexpr Fixed& operator-=(Fixed other) {
        raw_ -= other.raw_;
        return *this;
    }

private:
    std::int64_t raw_ = 0;
};

// Decimal rendering such as "-0.333333". For logs and tests.
[[nodiscard]] std::string toString(Fixed value);

}  // namespace sim
