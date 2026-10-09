#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>

namespace sim {

// (a * b) / c with a 128-bit intermediate, truncating toward zero.
// Returns nullopt when c is zero or the quotient does not fit in int64.
// Compares a1*b1 with a2*b2 exactly (128-bit intermediates, magnitudes
// only: all four inputs must be non-negative). Returns -1, 0 or 1.
[[nodiscard]] int compareProducts(std::int64_t a1, std::int64_t b1, std::int64_t a2,
                                  std::int64_t b2);

[[nodiscard]] std::optional<std::int64_t> checkedMulDiv(std::int64_t a, std::int64_t b,
                                                        std::int64_t c);

// Same as checkedMulDiv but aborts (in every build type) on violation.
// Every Fixed and Money multiply or divide funnels through here, so it is the
// only platform-specific arithmetic in the sim.
[[nodiscard]] std::int64_t mulDiv(std::int64_t a, std::int64_t b, std::int64_t c);

// Signed add, subtract and negate that abort on overflow instead of invoking
// undefined behaviour. All Fixed and Money arithmetic goes through these.
[[nodiscard]] std::int64_t checkedAdd(std::int64_t a, std::int64_t b);
[[nodiscard]] std::int64_t checkedSub(std::int64_t a, std::int64_t b);
[[nodiscard]] std::int64_t checkedNegate(std::int64_t a);

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
    [[nodiscard]] static Fixed fromInt(std::int64_t whole) {
        return fromRaw(mulDiv(whole, kScale, 1));
    }
    // numerator / denominator, truncated toward zero.
    [[nodiscard]] static Fixed fromRatio(std::int64_t numerator, std::int64_t denominator) {
        return fromRaw(mulDiv(numerator, kScale, denominator));
    }
    // Content-loading boundary only. Rounds to nearest so that a JSON value
    // such as 0.7 becomes exactly 700000 raw. Never call this from sim logic.
    [[nodiscard]] static Fixed fromDouble(double value);

    [[nodiscard]] static constexpr Fixed zero() { return fromRaw(0); }
    [[nodiscard]] static constexpr Fixed one() { return fromRaw(kScale); }

    [[nodiscard]] constexpr std::int64_t raw() const { return raw_; }
    [[nodiscard]] constexpr std::int64_t truncToInt() const { return raw_ / kScale; }

    friend constexpr auto operator<=>(Fixed, Fixed) = default;

    Fixed operator-() const { return fromRaw(checkedNegate(raw_)); }
    friend Fixed operator+(Fixed a, Fixed b) { return fromRaw(checkedAdd(a.raw_, b.raw_)); }
    friend Fixed operator-(Fixed a, Fixed b) { return fromRaw(checkedSub(a.raw_, b.raw_)); }
    friend Fixed operator*(Fixed a, Fixed b) { return fromRaw(mulDiv(a.raw_, b.raw_, kScale)); }
    friend Fixed operator/(Fixed a, Fixed b) { return fromRaw(mulDiv(a.raw_, kScale, b.raw_)); }
    friend Fixed operator*(Fixed a, std::int64_t n) { return fromRaw(mulDiv(a.raw_, n, 1)); }
    friend Fixed operator*(std::int64_t n, Fixed a) { return fromRaw(mulDiv(a.raw_, n, 1)); }
    friend Fixed operator/(Fixed a, std::int64_t n) { return fromRaw(mulDiv(a.raw_, 1, n)); }

    Fixed& operator+=(Fixed other) {
        raw_ = checkedAdd(raw_, other.raw_);
        return *this;
    }
    Fixed& operator-=(Fixed other) {
        raw_ = checkedSub(raw_, other.raw_);
        return *this;
    }

private:
    std::int64_t raw_ = 0;
};

// Decimal rendering such as "-0.333333". For logs and tests.
[[nodiscard]] std::string toString(Fixed value);

}  // namespace sim
