#pragma once

// Rebuilds a game from a playtest trace (trace.h) by running the same
// Session the client ran: the recorded seed and setup, each recorded order
// submitted on the day it was submitted, one tick per recorded day. The
// state hash after every day is compared with the recorded one, so the
// first point where the replay stops matching the player's game is found.

#include "sim/content.h"
#include "sim/types.h"

#include "game/session.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace game {

struct ReplayOptions {
    // Stop once the session reaches this day; the rest of the trace is ignored.
    std::optional<sim::Day> stopDay;
    // Resolves a "load" record's file to its save bytes. Returns an empty
    // vector when the file cannot be read.
    std::function<std::vector<std::uint8_t>(const std::string& file)> readSave;
};

struct ReplayResult {
    std::unique_ptr<Session> session;  // null when the trace never started a game
    std::size_t lines = 0;
    std::size_t commands = 0;
    sim::Day daysTicked = 0;
    // First day whose state hash differed from the recorded one, if any.
    std::optional<sim::Day> firstDivergence;
    // Set when the trace is unusable (bad line, content mismatch, unreadable
    // save); the session is left at the last good point.
    std::string error;
    [[nodiscard]] bool ok() const { return error.empty(); }
};

[[nodiscard]] ReplayResult replayTrace(const sim::Content& content, std::istream& trace,
                                       const ReplayOptions& options);

}  // namespace game
