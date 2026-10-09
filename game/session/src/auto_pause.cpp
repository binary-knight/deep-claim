#include "game/auto_pause.h"

#include "sim/commands.h"
#include "sim/ids.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace game {

namespace {

constexpr std::array<std::string_view, kPauseOnCount> kKeys = {
    "insolvency",      "bankrupt",         "auction_won",        "auction_lost",
    "survey_complete", "mine_operational", "mine_found_nothing", "idle_mine",
};

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

std::string_view pauseOnKey(PauseOn reason) {
    return kKeys.at(static_cast<std::size_t>(reason));
}

AutoPause AutoPause::defaults() {
    AutoPause rules;
    rules.enabled.fill(true);
    rules.set(PauseOn::SurveyComplete, false);
    rules.set(PauseOn::MineOperational, false);
    return rules;
}

std::optional<PauseOn> AutoPause::reasonFor(const sim::Notice& notice, sim::CompanyId player) {
    using K = sim::NoticeKind;
    switch (notice.kind) {
        case K::InsolvencyWarning:
            return PauseOn::Insolvency;
        case K::Bankrupt:
            return notice.company == player ? std::optional<PauseOn>(PauseOn::Bankrupt)
                                            : std::nullopt;
        case K::PlotSold:
            return notice.company == player ? std::optional<PauseOn>(PauseOn::AuctionWon)
                                            : std::nullopt;
        case K::AuctionLost:
            return PauseOn::AuctionLost;
        case K::SurveyComplete:
            return PauseOn::SurveyComplete;
        case K::MineOperational:
            return PauseOn::MineOperational;
        case K::MineFoundNothing:
            return PauseOn::MineFoundNothing;
        case K::IdleMineWarning:
            return PauseOn::IdleMine;
        default:
            return std::nullopt;
    }
}

bool AutoPause::shouldPause(const sim::Notice& notice, sim::CompanyId player) const {
    const std::optional<PauseOn> reason = reasonFor(notice, player);
    return reason.has_value() && isEnabled(*reason);
}

std::string AutoPause::toText() const {
    std::string out;
    for (std::size_t i = 0; i < kPauseOnCount; ++i) {
        out += kKeys.at(i);
        out += enabled.at(i) ? "=1\n" : "=0\n";
    }
    return out;
}

bool AutoPause::fromText(std::string_view text) {
    bool ok = true;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line =
            trim(newline == std::string_view::npos ? text : text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            ok = false;
            continue;
        }
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        if (value != "0" && value != "1") {
            ok = false;
            continue;
        }
        for (std::size_t i = 0; i < kPauseOnCount; ++i) {
            if (kKeys.at(i) == key) {
                enabled.at(i) = value == "1";
            }
        }
    }
    return ok;
}

}  // namespace game
