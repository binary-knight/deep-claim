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
    ResearchComplete,  // ResearchComplete (the player's own lab)
    MineLosing,        // MineLosingMoney (the player's own mine)
    RivalProgram,      // ProgramResearchStarted, ProgramStageComplete (another company's)
    kCount,
};

constexpr std::size_t kPauseOnCount = static_cast<std::size_t>(PauseOn::kCount);

// Stable key for the settings file and the string catalogue ("ui.pause_on.<key>").
[[nodiscard]] std::string_view pauseOnKey(PauseOn reason);

struct AutoPause {
    std::array<bool, kPauseOnCount> enabled{};

    // The defaults: everything on except mine operational. Survey and
    // research complete joined the "on" set with spec 004: a report that
    // lands unseen or a lab left idle costs the player more than a stop.
    // A mine losing money (spec 008) is on too: every day unseen is paid for.
    // So is a rival's progress in the race (spec 010): the player has to be
    // able to react in time.
    [[nodiscard]] static AutoPause defaults();

    [[nodiscard]] bool isEnabled(PauseOn reason) const {
        return enabled.at(static_cast<std::size_t>(reason));
    }
    void set(PauseOn reason, bool on) { enabled.at(static_cast<std::size_t>(reason)) = on; }

    // The reason a notice to `player` would pause for, if it is one of the
    // pausing kinds (regardless of whether that reason is enabled).
    [[nodiscard]] static std::optional<PauseOn> reasonFor(const sim::Notice& notice,
                                                          sim::CompanyId player);
    // True when `notice` should stop the clock under this rule set.
    [[nodiscard]] bool shouldPause(const sim::Notice& notice, sim::CompanyId player) const;

    // Text form: one "key=0" or "key=1" line per reason, in enum order.
    [[nodiscard]] std::string toText() const;
    // Reads the text form. Unknown keys are ignored and missing keys keep
    // their current value, so an old or edited file never breaks loading:
    // read over defaults(), a file from before a reason existed gets that
    // reason's default and keeps every toggle it does record.
    // Returns false only when a line is malformed.
    bool fromText(std::string_view text);
};

}  // namespace game
