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
#include <string_view>

namespace game {

// "$1.23M", "-$450k", "$12.00"
std::string formatMoney(sim::Money amount);
// "Y2 M3 D14" from a zero-based day.
std::string formatDay(sim::Day day);
// "12,345 t"
std::string formatTonnes(std::int64_t tonnes);
// "x0.85"
std::string formatRatio(sim::Fixed ratio);

// Strict parsers for what the player types. Money accepts "1200000",
// "1,200,000", "1200000.50", "1.2M", "450k", "2.5B", with an optional leading
// "$" and sign; the whole text must be consumed. Tonnes accept digits with
// optional thousands separators and an optional sign. Negative values are
// returned so the sim can reject them with its own reason.
[[nodiscard]] bool parseMoney(std::string_view text, sim::Money& out);
[[nodiscard]] bool parseTonnes(std::string_view text, std::int64_t& out);
// Exact text for an amount the player can submit unchanged: "1234567.89".
[[nodiscard]] std::string exactMoney(sim::Money amount);

// Plain-language reason for a rejected order.
[[nodiscard]] const char* rejectReasonText(sim::RejectReason reason);

// One-line human description of a notice for the player's log.
std::string describeNotice(const sim::Content& content, const sim::CompanyView& view,
                           const sim::Notice& notice);

}  // namespace game
