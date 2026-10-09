#include "sim/fixed.h"

#include "sim/fatal.h"

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

int compareProducts(std::int64_t a1, std::int64_t b1, std::int64_t a2, std::int64_t b2) {
    std::uint64_t high1 = 0;
    std::uint64_t low1 = 0;
    std::uint64_t high2 = 0;
    std::uint64_t low2 = 0;
    umul128(magnitude(a1), magnitude(b1), high1, low1);
    umul128(magnitude(a2), magnitude(b2), high2, low2);
    if (high1 != high2) {
        return high1 < high2 ? -1 : 1;
    }
    if (low1 != low2) {
        return low1 < low2 ? -1 : 1;
    }
    return 0;
}

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
    if (!result.has_value()) {
        fatal("mulDiv: overflow or division by zero");
    }
    return *result;
}

std::int64_t checkedAdd(std::int64_t a, std::int64_t b) {
    // Unsigned wrap is defined; the sign test detects signed overflow.
    const auto sum =
        static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
    if ((a >= 0) == (b >= 0) && (sum >= 0) != (a >= 0)) {
        fatal("checkedAdd: overflow");
    }
    return sum;
}

std::int64_t checkedSub(std::int64_t a, std::int64_t b) {
    const auto difference =
        static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
    if ((a >= 0) != (b >= 0) && (difference >= 0) != (a >= 0)) {
        fatal("checkedSub: overflow");
    }
    return difference;
}

std::int64_t checkedNegate(std::int64_t a) {
    if (a == std::numeric_limits<std::int64_t>::min()) {
        fatal("checkedNegate: overflow");
    }
    return -a;
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
