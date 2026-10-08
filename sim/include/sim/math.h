#pragma once

// Deterministic fixed-point transcendental functions. Integer-only so results
// are bit-identical on every compiler. ln is accurate to a few raw units;
// exp and pow carry a relative error of roughly 1e-5 after reconstruction.

#include "sim/fixed.h"

namespace sim {

[[nodiscard]] Fixed clampFixed(Fixed value, Fixed low, Fixed high);

// Natural logarithm. value must be positive.
[[nodiscard]] Fixed fixedLn(Fixed value);

// e^value. value must lie within [-29, 29]; e^29 is about 3.9e12, near the
// largest magnitude Fixed can hold.
[[nodiscard]] Fixed fixedExp(Fixed value);

// base^exponent for positive base, computed as exp(exponent * ln(base)).
[[nodiscard]] Fixed fixedPow(Fixed base, Fixed exponent);

}  // namespace sim
