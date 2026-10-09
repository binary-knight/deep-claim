#pragma once

// A running game from the client's point of view: the sim, the AI
// competitors, time control, the player's orders and a log of what happened.
// The UI reads views and the log, and sends commands; it never touches the
// sim's world state.

#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"

#include "game/auto_pause.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace game {

class TraceWriter;

enum class Speed : std::uint8_t { Paused, Normal, Fast, Fastest };
[[nodiscard]] int daysPerSecond(Speed speed);  // 0, 1, 5, 20

struct LogEntry {
    sim::Day day = 0;
    sim::Notice notice;
    std::string text;
};

class Session {
public:
    static constexpr std::size_t kMaxLogEntries = 500;
    static constexpr sim::CompanyId kPlayer{0};

    Session(const sim::Content& content, sim::Seed seed, const sim::GameSetup& setup);

    // ---- time ----
    void setSpeed(Speed speed);
    [[nodiscard]] Speed speed() const { return speed_; }
    void togglePause();
    // Advances real time; ticks whole in-game days as they accrue. Returns
    // how many days were ticked. Stops ticking once the player is bankrupt,
    // and pauses the moment a notice in the auto-pause rule set reaches the
    // player (that notice is then available from takePausedBy).
    int advance(double seconds);
    // One day forward regardless of speed (used by a "step" button).
    void stepOneDay();

    // ---- auto-pause ----
    [[nodiscard]] const AutoPause& autoPause() const { return autoPause_; }
    void setAutoPause(const AutoPause& rules) { autoPause_ = rules; }
    // The notice that last stopped the clock, handed over once.
    [[nodiscard]] std::optional<sim::Notice> takePausedBy();

    // ---- player ----
    void submit(sim::Command command);  // always bound to the player
    [[nodiscard]] const sim::CompanyView& view() const { return view_; }
    [[nodiscard]] const std::vector<LogEntry>& log() const { return log_; }
    [[nodiscard]] bool gameOver() const { return view_.bankrupt; }
    [[nodiscard]] sim::Day day() const { return view_.day; }
    // Daily prices since the start of the game, for charts.
    [[nodiscard]] const std::vector<sim::Money>& priceHistory(sim::ResourceId resource) const {
        return simulation_.priceHistory(resource);
    }
    // The player's cash at the end of each day of this session, oldest
    // first, for the header sparkline. Session memory, not sim state: a
    // load starts it again from the loaded day's cash.
    [[nodiscard]] const std::vector<sim::Money>& cashHistory() const { return cashHistory_; }

    // ---- persistence ----
    [[nodiscard]] std::vector<std::uint8_t> save() const { return simulation_.save(); }
    [[nodiscard]] bool load(std::span<const std::uint8_t> bytes);

    [[nodiscard]] const sim::Content& content() const { return *content_; }

    // Optional playtest trace (see trace.h): orders, ticks, notices and a
    // monthly state summary are recorded as they happen. Not owned; null
    // turns tracing off. The host records new games, loads and interface
    // events itself.
    void setTrace(TraceWriter* trace) { trace_ = trace; }
    [[nodiscard]] TraceWriter* trace() const { return trace_; }
    // Tests compare hashes through this; the interface never needs it.
    [[nodiscard]] std::uint64_t stateHash() const { return simulation_.stateHash(); }

private:
    void tickOnce();
    void refreshView();
    void appendLog(sim::Day day, const sim::Notice& notice);

    const sim::Content* content_;
    sim::Simulation simulation_;
    sim::HeuristicController ai_;
    sim::CompanyView view_;
    std::vector<LogEntry> log_;
    Speed speed_ = Speed::Paused;
    double accumulator_ = 0.0;  // real seconds not yet turned into days
    TraceWriter* trace_ = nullptr;
    AutoPause autoPause_ = AutoPause::defaults();
    std::optional<sim::Notice> pausedBy_;
    std::vector<sim::Money> cashHistory_;
};

}  // namespace game
