#include "game/session.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"

#include "game/format.h"
#include "game/trace.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace game {

int daysPerSecond(Speed speed) {
    switch (speed) {
        case Speed::Paused:
            return 0;
        case Speed::Normal:
            return 1;
        case Speed::Fast:
            return 5;
        case Speed::Fastest:
            return 20;
    }
    return 0;
}

Session::Session(const sim::Content& content, sim::Seed seed, const sim::GameSetup& setup)
    : content_(&content), simulation_(content, seed, setup), ai_(content) {
    refreshView();
    cashHistory_.push_back(view_.cash);
    for (const sim::Notice& notice : simulation_.reportFor(kPlayer)) {
        appendLog(0, notice);
    }
}

void Session::setSpeed(Speed speed) {
    if (speed != speed_ && trace_ != nullptr) {
        trace_->ui(day(), "speed", std::to_string(daysPerSecond(speed)) + " days/s");
    }
    speed_ = speed;
}

void Session::togglePause() {
    setSpeed(speed_ == Speed::Paused ? Speed::Normal : Speed::Paused);
}

int Session::advance(double seconds) {
    const int rate = daysPerSecond(speed_);
    if (rate == 0 || gameOver()) {
        accumulator_ = 0.0;
        return 0;
    }
    accumulator_ += seconds * rate;
    // Cap the catch-up so a long stall never freezes the UI ticking.
    int ticked = 0;
    while (accumulator_ >= 1.0 && ticked < rate * 2 && !gameOver()) {
        tickOnce();
        accumulator_ -= 1.0;
        ++ticked;
        if (stopIfPaused()) {
            break;
        }
    }
    if (ticked == rate * 2) {
        accumulator_ = 0.0;
    }
    return ticked;
}

void Session::stepOneDay() {
    if (!gameOver()) {
        tickOnce();
        stopIfPaused();
    }
}

bool Session::stopIfPaused() {
    if (!pausedBy_.has_value()) {
        return false;
    }
    // Something the player wants to see: stop here, on this day.
    setSpeed(Speed::Paused);
    accumulator_ = 0.0;
    return true;
}

std::optional<sim::Notice> Session::takePausedBy() {
    std::optional<sim::Notice> notice;
    notice.swap(pausedBy_);
    return notice;
}

void Session::submit(sim::Command command) {
    sim::setCommandCompany(command, kPlayer);
    if (trace_ != nullptr) {
        trace_->command(day(), command);
    }
    simulation_.submit(command);
}

bool Session::load(std::span<const std::uint8_t> bytes) {
    if (!simulation_.load(bytes)) {
        return false;
    }
    refreshView();
    cashHistory_.assign(1, view_.cash);
    pausedBy_.reset();
    log_.clear();
    sim::Notice marker;
    marker.kind = sim::NoticeKind::EventEnded;  // placeholder kind; text is what matters
    LogEntry entry;
    entry.day = day();
    entry.notice = marker;
    entry.text = "Game loaded";
    log_.push_back(std::move(entry));
    accumulator_ = 0.0;
    speed_ = Speed::Paused;
    return true;
}

void Session::tickOnce() {
    // Rivals decide from their own views; the player's orders are already
    // queued. The sim applies everything in company order.
    const std::size_t companies = view_.companies.size();
    for (std::uint32_t c = 1; c < companies; ++c) {
        simulation_.collectCommands(sim::CompanyId(c), ai_);
    }
    const sim::Day processed = view_.day;
    simulation_.tick();
    refreshView();
    cashHistory_.push_back(view_.cash);
    if (trace_ != nullptr) {
        trace_->dayTicked(view_.day, simulation_.stateHash());
    }
    for (const sim::Notice& notice : simulation_.reportFor(kPlayer)) {
        appendLog(processed, notice);
        if (!pausedBy_.has_value() && autoPause_.shouldPause(notice, kPlayer)) {
            pausedBy_ = notice;
        }
    }
    if (trace_ != nullptr && view_.day % sim::kDaysPerMonth == 0) {
        trace_->state(*content_, view_);
    }
}

void Session::refreshView() {
    view_ = simulation_.view(kPlayer);
}

void Session::appendLog(sim::Day day, const sim::Notice& notice) {
    LogEntry entry;
    entry.day = day;
    entry.notice = notice;
    entry.text = describeNotice(*content_, view_, notice);
    if (trace_ != nullptr) {
        trace_->notice(day, notice, entry.text);
    }
    log_.push_back(std::move(entry));
    if (log_.size() > kMaxLogEntries) {
        log_.erase(log_.begin(),
                   log_.begin() + static_cast<std::ptrdiff_t>(log_.size() - kMaxLogEntries));
    }
}

}  // namespace game
