#include "sim/money.h"

#include <cstdint>
#include <string>

namespace sim {

std::string toString(Money value) {
    const std::int64_t cents = value.cents();
    const std::uint64_t mag =
        cents < 0 ? 0U - static_cast<std::uint64_t>(cents) : static_cast<std::uint64_t>(cents);
    const auto perUnit = static_cast<std::uint64_t>(Money::kCentsPerUnit);
    std::string result = cents < 0 ? "-" : "";
    result += std::to_string(mag / perUnit);
    result += '.';
    const std::uint64_t fraction = mag % perUnit;
    if (fraction < 10U) {
        result += '0';
    }
    result += std::to_string(fraction);
    return result;
}

}  // namespace sim
