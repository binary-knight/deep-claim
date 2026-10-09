#pragma once

// Playtest trace: one JSON object per line describing everything that
// happened in a client session, written as it happens and flushed per line
// so a crash leaves the record intact. A trace plus the data files is a
// complete replay of the game (see replay.h), because the sim is
// deterministic and only commands and loads change its state.
//
// Record types ("t"):
//   new_game   seed, player, ais, content (hash), build
//   load       file (a copy of the save bytes next to the trace)
//   save       file
//   command    the player's order, tagged with the day it was submitted
//   day        day and state hash after each tick
//   notice     a line of the player's log, with its text (what the log
//              shows: filtered and collapsed as the session does it)
//   state      compact monthly summary of the player's finances and output
//   ui         interface events: tab, select, speed, status
//   marker     the player pressed the "look at this" key
//   screenshot file of a captured frame
//   end        the session closed normally

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace game {

constexpr int kTraceVersion = 1;

class TraceWriter {
public:
    // `out` must outlive the writer. Every record is flushed.
    explicit TraceWriter(std::ostream& out) : out_(&out) {}

    void newGame(sim::Seed seed, const sim::GameSetup& setup, std::uint64_t contentHash,
                 std::string_view build);
    void loaded(sim::Day day, std::string_view file, std::uint64_t stateHash);
    void saved(sim::Day day, std::string_view file);
    // With `content`, a StartResearch record also carries "tech_id" (the
    // content id string) so a trace reads without the data files open. The
    // parser still keys on the index.
    void command(sim::Day day, const sim::Command& command, const sim::Content* content = nullptr);
    void dayTicked(sim::Day day, std::uint64_t stateHash);
    void notice(sim::Day day, const sim::Notice& notice, std::string_view text);
    void state(const sim::Content& content, const sim::CompanyView& view);
    void ui(sim::Day day, std::string_view kind, std::string_view detail);
    void marker(sim::Day day, std::string_view note);
    void screenshot(sim::Day day, std::string_view file);
    void end(sim::Day day);

private:
    void line(const std::string& json);
    std::ostream* out_;
};

// A parsed trace line. Fields not relevant to `type` are left default.
struct TraceRecord {
    std::string type;
    sim::Day day = 0;
    sim::Seed seed = 0;
    std::uint64_t hash = 0;         // day: state hash; load: hash after loading
    std::uint64_t contentHash = 0;  // new_game
    std::string player;             // new_game
    std::vector<std::string> ais;   // new_game
    std::string file;               // load, save, screenshot
    std::string text;               // notice text, ui detail, marker note
    std::optional<sim::Command> command;
};

// Command <-> JSON text, used by the writer and the parser and by tests.
[[nodiscard]] std::string encodeCommand(const sim::Command& command);
[[nodiscard]] bool decodeCommand(std::string_view json, sim::Command& out);

// Returns false with `error` set for malformed lines. Blank lines parse as
// a record with an empty type.
[[nodiscard]] bool parseTraceLine(std::string_view line, TraceRecord& out, std::string& error);

}  // namespace game
