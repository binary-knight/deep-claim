#include "sim/fixed.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

// GCC and Clang provide __int128; MSVC x64 provides _umul128/_udiv128.
#if !defined(__SIZEOF_INT128__)
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#else
#error "mulDiv requires 128-bit integer support (GCC/Clang __int128 or MSVC x64 intrinsics)"
#endif
#endif

namespace sim {

namespace {

std::uint64_t magnitude(std::int64_t value) {
    return value < 0 ? 0U - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
}

// 64 x 64 -> 128 unsigned multiply.
void umul128(std::uint64_t a, std::uint64_t b, std::uint64_t& high, std::uint64_t& low) {
#if defined(__SIZEOF_INT128__)
    __extension__ using U128 = unsigned __int128;
    const U128 product = static_cast<U128>(a) * b;
    high = static_cast<std::uint64_t>(product >> 64U);
    low = static_cast<std::uint64_t>(product);
#else
    low = _umul128(a, b, &high);
#endif
}

// 128 / 64 -> 64 unsigned divide. Precondition: high < divisor, so the
// quotient fits in 64 bits (the MSVC intrinsic traps otherwise).
std::uint64_t udiv128(std::uint64_t high, std::uint64_t low, std::uint64_t divisor) {
#if defined(__SIZEOF_INT128__)
    __extension__ using U128 = unsigned __int128;
    const U128 numerator = (static_cast<U128>(high) << 64U) | low;
    return static_cast<std::uint64_t>(numerator / divisor);
#else
    std::uint64_t remainder = 0;
    return _udiv128(high, low, divisor, &remainder);
#endif
}

}  // namespace

std::optional<std::int64_t> checkedMulDiv(std::int64_t a, std::int64_t b, std::int64_t c) {
    if (c == 0) {
        return std::nullopt;
    }
    const bool negative = ((a < 0) != (b < 0)) != (c < 0);

    std::uint64_t high = 0;
    std::uint64_t low = 0;
    umul128(magnitude(a), magnitude(b), high, low);
    const std::uint64_t divisor = magnitude(c);
    if (high >= divisor) {
        return std::nullopt;  // quotient would not fit in 64 bits
    }
    const std::uint64_t quotient = udiv128(high, low, divisor);

    constexpr auto kMaxMagnitude =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (negative) {
        if (quotient > kMaxMagnitude + 1U) {
            return std::nullopt;
        }
        if (quotient == kMaxMagnitude + 1U) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return -static_cast<std::int64_t>(quotient);
    }
    if (quotient > kMaxMagnitude) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(quotient);
}

std::int64_t mulDiv(std::int64_t a, std::int64_t b, std::int64_t c) {
    const std::optional<std::int64_t> result = checkedMulDiv(a, b, c);
    assert(result.has_value() && "mulDiv overflow or division by zero");
    return result.value_or(0);
}

Fixed Fixed::fromDouble(double value) {
    return fromRaw(static_cast<std::int64_t>(std::llround(value * static_cast<double>(kScale))));
}

std::string toString(Fixed value) {
    const std::uint64_t mag = magnitude(value.raw());
    const auto scale = static_cast<std::uint64_t>(Fixed::kScale);
    std::string fraction = std::to_string(mag % scale);
    fraction.insert(0, 6 - fraction.size(), '0');
    std::string result = value.raw() < 0 ? "-" : "";
    result += std::to_string(mag / scale);
    result += '.';
    result += fraction;
    return result;
}

}  // namespace sim
