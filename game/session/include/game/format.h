#pragma once

// Text formatting for the UI. Everything here is pure and locale-free so the
// same strings appear on every platform.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/view.h"

#include <cstdint>
#include <string>

namespace game {

// "$1.23M", "-$450k", "$12.00"
std::string formatMoney(sim::Money amount);
// "Y2 M3 D14" from a zero-based day.
std::string formatDay(sim::Day day);
// "12,345 t"
std::string formatTonnes(std::int64_t tonnes);
// "x0.85"
std::string formatRatio(sim::Fixed ratio);

// One-line human description of a notice for the player's log.
std::string describeNotice(const sim::Content& content, const sim::CompanyView& view,
                           const sim::Notice& notice);

}  // namespace game
