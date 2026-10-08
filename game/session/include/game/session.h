#pragma once

// A running game from the client's point of view: the sim, the AI
// competitors, time control, the player's orders and a log of what happened.
// The UI reads views and the log, and sends commands; it never touches the
// sim's world state.

#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace game {

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
    void setSpeed(Speed speed) { speed_ = speed; }
    [[nodiscard]] Speed speed() const { return speed_; }
    void togglePause();
    // Advances real time; ticks whole in-game days as they accrue. Returns
    // how many days were ticked. Stops ticking once the player is bankrupt.
    int advance(double seconds);
    // One day forward regardless of speed (used by a "step" button).
    void stepOneDay();

    // ---- player ----
    void submit(sim::Command command);  // always bound to the player
    [[nodiscard]] const sim::CompanyView& view() const { return view_; }
    [[nodiscard]] const std::vector<LogEntry>& log() const { return log_; }
    [[nodiscard]] bool gameOver() const { return view_.bankrupt; }
    [[nodiscard]] sim::Day day() const { return simulation_.state().day; }

    // ---- persistence ----
    [[nodiscard]] std::vector<std::uint8_t> save() const { return simulation_.save(); }
    [[nodiscard]] bool load(std::span<const std::uint8_t> bytes);

    [[nodiscard]] const sim::Content& content() const { return *content_; }
    [[nodiscard]] const sim::Simulation& simulation() const { return simulation_; }

private:
    void tickOnce();
    void refreshView();
    void appendLog(const sim::Notice& notice);

    const sim::Content* content_;
    sim::Simulation simulation_;
    sim::HeuristicController ai_;
    sim::CompanyView view_;
    std::vector<LogEntry> log_;
    Speed speed_ = Speed::Paused;
    double accumulator_ = 0.0;  // real seconds not yet turned into days
};

}  // namespace game
