#pragma once

// A running game from the client's point of view: the sim, the AI
// competitors, time control, the player's orders and a log of what happened.
// The UI reads views and the log, and sends commands; it never touches the
// sim's world state.
//
// Orders wait here, not in the sim, until the clock next moves (spec 007):
// the interface can show each one the moment it is given and withdraw it
// before it counts. The sim still sees them in the order given, on the day
// they were given, so the game plays out exactly as if they had gone
// straight to it.

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
#include <string_view>
#include <vector>

namespace game {

class TraceWriter;

enum class Speed : std::uint8_t { Paused, Normal, Fast, Fastest };
[[nodiscard]] int daysPerSecond(Speed speed);  // 0, 1, 5, 20

// An order the player has given that the sim has not been handed yet.
struct PendingOrder {
    std::uint64_t id = 0;  // unique within a session, never handed out twice
    sim::Command command;  // bound to the player
};

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
    // Once the game is over the clock stays stopped: any other speed is
    // ignored (spec 010, edge case 11).
    void setSpeed(Speed speed);
    [[nodiscard]] Speed speed() const { return speed_; }
    void togglePause();
    // Advances real time; ticks whole in-game days as they accrue. Returns
    // how many days were ticked. Pauses the moment a notice in the
    // auto-pause rule set reaches the player (that notice is then available
    // from takePausedBy). Once the game is over (gameOver) no further day is
    // ticked and nothing more is added to the log, the histories or the
    // trace; the day it ends on stops the clock whatever the rules say, and
    // takePausedBy then hands over the notice that ended it.
    int advance(double seconds);
    // One day forward regardless of speed (used by a "step" button).
    void stepOneDay();

    // ---- auto-pause ----
    [[nodiscard]] const AutoPause& autoPause() const { return autoPause_; }
    void setAutoPause(const AutoPause& rules) { autoPause_ = rules; }
    // The notice that last stopped the clock, handed over once.
    [[nodiscard]] std::optional<sim::Notice> takePausedBy();

    // ---- player ----
    // Queues an order, always bound to the player. It reaches the sim at the
    // start of the next tick; until then it can be cancelled. Once the game
    // is over (gameOver) no order is taken: nothing is queued or traced and
    // the answer is false. True when the order was queued.
    bool submit(sim::Command command);
    // What is waiting for the clock, in the order given.
    [[nodiscard]] const std::vector<PendingOrder>& pendingOrders() const { return pending_; }
    // Withdraws one waiting order; false when no order has that id (it was
    // given to the sim or cancelled already).
    bool cancel(std::uint64_t id);
    void cancelAll();
    // Money the waiting orders will take when they are given, at today's
    // quotes: survey fee, bid (less a standing bid on the same plot, which is
    // reserved already), mine capex, tech cost, loan repayment. For display
    // and button state only (R-001): the sim decides what it accepts.
    [[nodiscard]] sim::Money committed() const;
    // Principal of the waiting loans: money the orders after them can spend.
    [[nodiscard]] sim::Money pendingBorrowed() const;
    // What one order adds to committed(); zero for an order that costs
    // nothing up front or names something the view does not show.
    [[nodiscard]] sim::Money orderCost(const sim::Command& command) const;
    [[nodiscard]] const sim::CompanyView& view() const { return view_; }
    [[nodiscard]] const std::vector<LogEntry>& log() const { return log_; }
    // The player is bankrupt, or the race has ended (spec 010): the program
    // was founded or the deadline passed. The sim decides both; this reads
    // them from the view.
    [[nodiscard]] bool gameOver() const { return view_.bankrupt || view_.outcome.ended; }
    // How the race ended, or that it has not. A bankrupt player's game is
    // over with the outcome still open.
    [[nodiscard]] const sim::OutcomeView& outcome() const { return view_.outcome; }
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
    // The save holds the waiting orders as the current day's orders, so a
    // game loaded from it plays on as this one will. This session is left as
    // it was: the orders still wait here and can still be cancelled.
    [[nodiscard]] std::vector<std::uint8_t> save() const;
    // On success the waiting orders are discarded with the old game.
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
    // Hands the waiting orders to the sim in the order given and records
    // each in the trace as a command.
    void giveOrders();
    void traceOrder(std::string_view kind, const PendingOrder& order);
    // Pauses the clock when the last tick raised a pausing notice or ended
    // the game.
    bool stopIfPaused();
    void refreshView();
    void appendLog(sim::Day day, const sim::Notice& notice);
    void appendLogText(sim::Day day, const sim::Notice& notice, std::string text);
    // Writes the tick's notices to the log: drops what the player has no
    // stake in and folds the month's automatic sales into one line.
    void logNotices(sim::Day day, std::span<const sim::Notice> notices,
                    std::span<const sim::PlotId> bidPlots);
    [[nodiscard]] bool showsInLog(const sim::Notice& notice,
                                  std::span<const sim::PlotId> bidPlots) const;

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
    // Plots the player ordered a bid on since the last tick (the bid and the
    // auction's close can be processed in the same day).
    std::vector<sim::PlotId> submittedBids_;
    std::vector<PendingOrder> pending_;
    std::uint64_t nextOrderId_ = 1;
};

}  // namespace game
