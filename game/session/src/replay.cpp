#include "game/replay.h"

#include "sim/content.h"
#include "sim/save.h"
#include "sim/simulation.h"
#include "sim/types.h"

#include "game/session.h"
#include "game/trace.h"

#include <cstdint>
#include <format>
#include <istream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace game {

namespace {

class Replayer {
public:
    Replayer(const sim::Content& content, const ReplayOptions& options)
        : content_(&content), options_(&options), contentHash_(sim::hashContent(content)) {}

    ReplayResult run(std::istream& trace) {
        std::string line;
        while (std::getline(trace, line)) {
            ++result_.lines;
            if (!step(line)) {
                break;
            }
        }
        return std::move(result_);
    }

private:
    // Returns false when the replay is finished, by error or by stop day.
    bool step(const std::string& line) {
        TraceRecord record;
        std::string error;
        if (!parseTraceLine(line, record, error)) {
            return fail(error);
        }
        if (record.type.empty()) {
            return true;
        }
        if (record.type == "new_game") {
            return newGame(record);
        }
        if (result_.session == nullptr) {
            return fail("record before any new_game");
        }
        if (record.type == "load") {
            return load(record);
        }
        if (record.type == "command") {
            return command(record);
        }
        if (record.type == "day") {
            return day(record);
        }
        // notice, state, ui, marker, screenshot, save, end: informational.
        return true;
    }

    bool newGame(const TraceRecord& record) {
        if (record.contentHash != contentHash_) {
            return fail("trace was recorded with different game data");
        }
        sim::GameSetup setup;
        setup.playerName = record.player;
        setup.aiNames = record.ais;
        result_.session = std::make_unique<Session>(*content_, record.seed, setup);
        return !reachedStop();
    }

    bool load(const TraceRecord& record) {
        if (!options_->readSave) {
            return fail("trace loads a save but no save reader was given");
        }
        const std::vector<std::uint8_t> bytes = options_->readSave(record.file);
        if (bytes.empty()) {
            return fail("could not read save " + record.file);
        }
        if (!result_.session->load(bytes)) {
            return fail("save " + record.file + " does not load");
        }
        if (result_.session->stateHash() != record.hash) {
            return fail("save " + record.file + " does not match the recorded state");
        }
        return !reachedStop();
    }

    bool command(const TraceRecord& record) {
        if (!record.command.has_value()) {
            return fail("command record without an order");
        }
        if (record.day != result_.session->day()) {
            return fail(std::format("order recorded on day {} but the replay is on day {}",
                                    record.day, result_.session->day()));
        }
        result_.session->submit(*record.command);
        ++result_.commands;
        return true;
    }

    bool day(const TraceRecord& record) {
        if (reachedStop()) {
            return false;
        }
        result_.session->stepOneDay();
        ++result_.daysTicked;
        if (result_.session->day() != record.day) {
            return fail(std::format("trace is on day {} but the replay is on day {}", record.day,
                                    result_.session->day()));
        }
        if (!result_.firstDivergence.has_value() && result_.session->stateHash() != record.hash) {
            result_.firstDivergence = record.day;
        }
        return true;
    }

    bool fail(const std::string& message) {
        result_.error = std::format("line {}: {}", result_.lines, message);
        return false;
    }

    [[nodiscard]] bool reachedStop() const {
        return options_->stopDay.has_value() && result_.session->day() >= *options_->stopDay;
    }

    const sim::Content* content_;
    const ReplayOptions* options_;
    std::uint64_t contentHash_;
    ReplayResult result_;
};

}  // namespace

ReplayResult replayTrace(const sim::Content& content, std::istream& trace,
                         const ReplayOptions& options) {
    Replayer replayer(content, options);
    return replayer.run(trace);
}

}  // namespace game
