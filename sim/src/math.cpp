#include "sim/math.h"

#include "sim/fatal.h"
#include "sim/fixed.h"

#include <cstdint>

namespace sim {

namespace {

// ln(2) and 1/ln(2) to six decimals.
const Fixed kLn2 = Fixed::fromRaw(693'147);
constexpr std::int64_t kMaxExpArgument = 29;

}  // namespace

Fixed clampFixed(Fixed value, Fixed low, Fixed high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

Fixed fixedLn(Fixed value) {
    if (value <= Fixed::zero()) {
        fatal("fixedLn: argument must be positive");
    }
    // Range-reduce to m in [1, 2): value = m * 2^k.
    std::int64_t k = 0;
    Fixed m = value;
    while (m >= Fixed::fromInt(2)) {
        m = m / 2;
        ++k;
    }
    while (m < Fixed::one()) {
        m = m * 2;
        --k;
    }
    // ln(m) = 2 * atanh(u), u = (m - 1) / (m + 1), |u| < 1/3, so the series
    // u + u^3/3 + u^5/5 + ... converges to 1e-6 within about eight terms.
    const Fixed u = (m - Fixed::one()) / (m + Fixed::one());
    const Fixed uSquared = u * u;
    Fixed term = u;
    Fixed sum = Fixed::zero();
    for (std::int64_t n = 1; n <= 19; n += 2) {
        sum += term / n;
        term = term * uSquared;
        if (term == Fixed::zero()) {
            break;
        }
    }
    return sum * 2 + kLn2 * k;
}

Fixed fixedExp(Fixed value) {
    if (value > Fixed::fromInt(kMaxExpArgument) || value < Fixed::fromInt(-kMaxExpArgument)) {
        fatal("fixedExp: argument out of range");
    }
    // Range-reduce: value = n * ln2 + r with r in [0, ln2), then exp(value)
    // = 2^n * exp(r). Floor division keeps r non-negative.
    std::int64_t n = (value / kLn2).truncToInt();
    if (value < Fixed::zero() && kLn2 * n != value) {
        --n;
    }
    const Fixed r = value - kLn2 * n;
    // Taylor series for exp(r), r < 0.7: twelve terms give full precision.
    Fixed term = Fixed::one();
    Fixed sum = Fixed::one();
    for (std::int64_t i = 1; i <= 12; ++i) {
        term = term * r / i;
        sum += term;
        if (term == Fixed::zero()) {
            break;
        }
    }
    if (n >= 0) {
        return Fixed::fromRaw(mulDiv(sum.raw(), std::int64_t{1} << n, 1));
    }
    return Fixed::fromRaw(mulDiv(sum.raw(), 1, std::int64_t{1} << (-n)));
}

Fixed fixedPow(Fixed base, Fixed exponent) {
    return fixedExp(exponent * fixedLn(base));
}

}  // namespace sim
