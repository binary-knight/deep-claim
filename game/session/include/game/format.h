#pragma once

// Text formatting for the UI. Everything here is pure and locale-free so the
// same strings appear on every platform.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/view.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

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

// ---- the prose catalogue (spec 002) ----
// Named substitution into a template from data/strings.json: every
// "{name}" whose name is in `args` is replaced; unknown placeholders stay
// visible as "{name}" so a mismatch shows in play instead of vanishing.
// Locale-free, never throws.
using TextArg = std::pair<std::string_view, std::string>;
using TextArgs = std::initializer_list<TextArg>;
[[nodiscard]] std::string fill(std::string_view text, TextArgs args);
// The template for `key`, or "[key]" when the catalogue lacks it (the
// loader rejects data without the required keys, so this only shows for
// in-code test content).
[[nodiscard]] std::string text(const sim::Content& content, std::string_view key);
[[nodiscard]] std::string text(const sim::Content& content, std::string_view key, TextArgs args);
// `key.one` for a count of one, `key.many` otherwise.
[[nodiscard]] std::string textPlural(const sim::Content& content, std::string_view key,
                                     std::int64_t count, TextArgs args);

// Plain-language reason for a rejected order (reject.<reason>).
[[nodiscard]] std::string rejectReasonText(const sim::Content& content, sim::RejectReason reason);

// One-line human description of a notice for the player's log (notice.<kind>).
std::string describeNotice(const sim::Content& content, const sim::CompanyView& view,
                           const sim::Notice& notice);

}  // namespace game
