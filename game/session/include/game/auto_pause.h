#pragma once

// Which notices stop the clock. The playtest showed warnings scrolling past
// at 20x; with auto-pause the session halts the moment one of these reaches
// the player and the interface shows it. The rule set is the player's
// setting, kept as a small text file by the host (key=0/1 lines), and never
// part of the sim.

#include "sim/commands.h"
#include "sim/ids.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace game {

enum class PauseOn : std::uint8_t {
    Insolvency,        // InsolvencyWarning
    Bankrupt,          // Bankrupt (the player's own)
    AuctionWon,        // PlotSold to the player
    AuctionLost,       // AuctionLost
    SurveyComplete,    // SurveyComplete
    MineOperational,   // MineOperational
    MineFoundNothing,  // MineFoundNothing
    IdleMine,          // IdleMineWarning
    kCount,
};

constexpr std::size_t kPauseOnCount = static_cast<std::size_t>(PauseOn::kCount);

// Stable key for the settings file and the string catalogue ("ui.pause_on.<key>").
[[nodiscard]] std::string_view pauseOnKey(PauseOn reason);

struct AutoPause {
    std::array<bool, kPauseOnCount> enabled{};

    // The spec's defaults: everything on except survey complete and mine
    // operational, which happen often and rarely need a decision.
    [[nodiscard]] static AutoPause defaults();

    [[nodiscard]] bool isEnabled(PauseOn reason) const {
        return enabled[static_cast<std::size_t>(reason)];
    }
    void set(PauseOn reason, bool on) { enabled[static_cast<std::size_t>(reason)] = on; }

    // The reason a notice to `player` would pause for, if it is one of the
    // pausing kinds (regardless of whether that reason is enabled).
    [[nodiscard]] static std::optional<PauseOn> reasonFor(const sim::Notice& notice,
                                                          sim::CompanyId player);
    // True when `notice` should stop the clock under this rule set.
    [[nodiscard]] bool shouldPause(const sim::Notice& notice, sim::CompanyId player) const;

    // Text form: one "key=0" or "key=1" line per reason, in enum order.
    [[nodiscard]] std::string toText() const;
    // Reads the text form. Unknown keys are ignored and missing keys keep
    // their current value, so an old or edited file never breaks loading.
    // Returns false only when a line is malformed.
    bool fromText(std::string_view text);
};

}  // namespace game
