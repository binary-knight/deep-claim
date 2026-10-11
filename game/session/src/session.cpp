#include "game/session.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"
#include "sim/world_state.h"

#include "game/format.h"
#include "game/trace.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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
    logNotices(0, simulation_.reportFor(kPlayer), {});
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
    PendingOrder order;
    order.id = nextOrderId_++;
    order.command = command;
    traceOrder("order_queued", order);
    pending_.push_back(order);
}

bool Session::cancel(std::uint64_t id) {
    const auto it = std::find_if(pending_.begin(), pending_.end(),
                                 [id](const PendingOrder& order) { return order.id == id; });
    if (it == pending_.end()) {
        return false;
    }
    traceOrder("order_cancelled", *it);
    pending_.erase(it);
    return true;
}

void Session::cancelAll() {
    for (const PendingOrder& order : pending_) {
        traceOrder("order_cancelled", order);
    }
    pending_.clear();
}

void Session::traceOrder(std::string_view kind, const PendingOrder& order) {
    if (trace_ != nullptr) {
        // The id ties a cancellation to the order it withdrew.
        const std::string detail =
            "#" + std::to_string(order.id) + " " + encodeCommand(order.command);
        trace_->ui(day(), kind, detail);
    }
}

void Session::giveOrders() {
    // Moved out first: nothing given here can queue another order, but the
    // queue must be empty whatever happens below.
    const std::vector<PendingOrder> orders = std::move(pending_);
    pending_.clear();
    for (const PendingOrder& order : orders) {
        if (trace_ != nullptr) {
            trace_->command(day(), order.command, content_);
        }
        if (const auto* bid = std::get_if<sim::BidOnPlot>(&order.command)) {
            submittedBids_.push_back(bid->plot);
        }
        simulation_.submit(order.command);
    }
}

namespace {

template <typename... Handlers>
struct Overloaded : Handlers... {
    using Handlers::operator()...;
};
template <typename... Handlers>
Overloaded(Handlers...) -> Overloaded<Handlers...>;

// A cost for display is never negative: an order with a nonsense amount is
// the sim's to reject, and must not make money look available.
sim::Money atLeastZero(sim::Money amount) {
    return std::max(amount, sim::Money::zero());
}

// The pending sums are for display and button state only, and an order can
// carry any amount until the sim turns it away, so they stop at the largest
// amount either way rather than use Money's checked arithmetic, which ends
// the program on overflow.
constexpr std::int64_t kMaxSumCents = std::numeric_limits<std::int64_t>::max();

sim::Money saturatingAdd(sim::Money a, sim::Money b) {
    const std::int64_t x = a.cents();
    const std::int64_t y = b.cents();
    if (y > 0 && x > kMaxSumCents - y) {
        return sim::Money::fromCents(kMaxSumCents);
    }
    if (y < 0 && x < -kMaxSumCents - y) {
        return sim::Money::fromCents(-kMaxSumCents);
    }
    return sim::Money::fromCents(x + y);
}

sim::Money saturatingSub(sim::Money a, sim::Money b) {
    const std::int64_t x = a.cents();
    const std::int64_t y = b.cents();
    if (y > 0 && x < -kMaxSumCents + y) {
        return sim::Money::fromCents(-kMaxSumCents);
    }
    if (y < 0 && x > kMaxSumCents + y) {
        return sim::Money::fromCents(kMaxSumCents);
    }
    return sim::Money::fromCents(x - y);
}

}  // namespace

sim::Money Session::orderCost(const sim::Command& command) const {
    const auto plotView = [this](sim::PlotId plot) -> const sim::PlotView* {
        return plot.valid() && plot.index() < view_.plots.size() ? &view_.plots[plot.index()]
                                                                 : nullptr;
    };
    return std::visit(
        Overloaded{
            [&](const sim::CommissionSurvey& survey) {
                return plotView(survey.plot) != nullptr ? content_->balance.surveyCost
                                                        : sim::Money::zero();
            },
            [&](const sim::BidOnPlot& bid) {
                const sim::PlotView* plot = plotView(bid.plot);
                if (plot == nullptr) {
                    return sim::Money::zero();
                }
                // A raise needs only the difference from the standing bid,
                // which the view already counts as reserved.
                const sim::Money standing =
                    plot->auction.has_value() ? plot->auction->myBid : sim::Money::zero();
                return atLeastZero(saturatingSub(bid.amount, standing));
            },
            [&](const sim::BuildMine& build) {
                const sim::PlotView* plot = plotView(build.plot);
                return plot != nullptr ? plot->mineCapexQuote : sim::Money::zero();
            },
            [this](const sim::StartResearch& research) {
                return research.tech.valid() && research.tech.index() < content_->techs.size()
                           ? content_->techs[research.tech.index()].cost
                           : sim::Money::zero();
            },
            [](const sim::RepayLoan& repay) { return atLeastZero(repay.amount); },
            [](const sim::TakeLoan&) { return sim::Money::zero(); },
            [](const sim::SellOre&) { return sim::Money::zero(); },
            [](const sim::SetMineState&) { return sim::Money::zero(); },
            [](const sim::SetSellPolicy&) { return sim::Money::zero(); },
            [](const sim::SetCoverCosts&) { return sim::Money::zero(); },
            [](const sim::DeliverToProgram&) { return sim::Money::zero(); },
            // The stage the player is working on; nothing once the program
            // is complete.
            [this](const sim::CompleteProgramStage&) {
                return view_.program.stage.has_value() ? view_.program.stage->cost
                                                       : sim::Money::zero();
            },
        },
        command);
}

sim::Money Session::committed() const {
    sim::Money total = sim::Money::zero();
    for (const PendingOrder& order : pending_) {
        total = saturatingAdd(total, orderCost(order.command));
    }
    return total;
}

sim::Money Session::pendingBorrowed() const {
    sim::Money total = sim::Money::zero();
    for (const PendingOrder& order : pending_) {
        if (const auto* loan = std::get_if<sim::TakeLoan>(&order.command)) {
            total = saturatingAdd(total, atLeastZero(loan->principal));
        }
    }
    return total;
}

std::vector<std::uint8_t> Session::save() const {
    // The save is taken from a copy of the sim that has been given the
    // waiting orders, so this game keeps them: still listed, still counted
    // and still open to Cancel. A save is a rare act; the copy is its cost.
    sim::Simulation saved = simulation_;
    for (const PendingOrder& order : pending_) {
        saved.submit(order.command);
    }
    return saved.save();
}

bool Session::load(std::span<const std::uint8_t> bytes) {
    if (!simulation_.load(bytes)) {
        return false;
    }
    refreshView();
    cashHistory_.assign(1, view_.cash);
    pausedBy_.reset();
    submittedBids_.clear();
    pending_.clear();
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
    // The player's orders go to the sim first, then the rivals decide from
    // their own views. The sim applies everything in company order.
    giveOrders();
    const std::size_t companies = view_.companies.size();
    for (std::uint32_t c = 1; c < companies; ++c) {
        simulation_.collectCommands(sim::CompanyId(c), ai_);
    }
    const sim::Day processed = view_.day;
    // The player's stake in each auction as the day starts: an auction that
    // closes today is gone from the view by the time its notice arrives.
    std::vector<sim::PlotId> bidPlots = std::move(submittedBids_);
    submittedBids_.clear();
    for (const sim::PlotView& plot : view_.plots) {
        if (plot.auction.has_value() && plot.auction->myBid != sim::Money::zero()) {
            bidPlots.push_back(plot.id);
        }
    }
    simulation_.tick();
    refreshView();
    cashHistory_.push_back(view_.cash);
    if (trace_ != nullptr) {
        trace_->dayTicked(view_.day, simulation_.stateHash());
    }
    logNotices(processed, simulation_.reportFor(kPlayer), bidPlots);
    if (trace_ != nullptr && view_.day % sim::kDaysPerMonth == 0) {
        trace_->state(*content_, view_);
    }
}

void Session::refreshView() {
    view_ = simulation_.view(kPlayer);
}

namespace {

// The log entry for a month's automatic sales: the kind and company of the
// summaries, with totals when there are several. Totals saturate; they are
// for display and filtering only.
sim::Notice mergedAutoSale(std::span<const sim::Notice> sales) {
    sim::Notice merged = sales.front();
    if (sales.size() == 1) {
        return merged;
    }
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    merged.resource = sim::ResourceId();
    std::int64_t tonnes = 0;
    std::int64_t cents = 0;
    for (const sim::Notice& sale : sales) {
        tonnes =
            sale.tonnes > kMax - tonnes ? kMax : tonnes + std::max<std::int64_t>(sale.tonnes, 0);
        const std::int64_t add = std::max<std::int64_t>(sale.amount.cents(), 0);
        cents = add > kMax - cents ? kMax : cents + add;
    }
    merged.tonnes = tonnes;
    merged.amount = sim::Money::fromCents(cents);
    return merged;
}

}  // namespace

bool Session::showsInLog(const sim::Notice& notice, std::span<const sim::PlotId> bidPlots) const {
    using K = sim::NoticeKind;
    switch (notice.kind) {
        case K::PlotRelisted:
        case K::PlotWithdrawn: {
            // Land going round the market again is news only to someone who
            // paid to look at it or tried to buy it.
            const bool surveyed =
                std::any_of(view_.surveys.begin(), view_.surveys.end(),
                            [&](const sim::SurveyView& s) { return s.plot == notice.plot; });
            return surveyed ||
                   std::find(bidPlots.begin(), bidPlots.end(), notice.plot) != bidPlots.end();
        }
        case K::RivalUnlockedResource:
            // The player's own unlock is already told by ResearchComplete.
            return notice.company != kPlayer;
        default:
            return true;
    }
}

void Session::logNotices(sim::Day day, std::span<const sim::Notice> notices,
                         std::span<const sim::PlotId> bidPlots) {
    // The sim reports one AutoSaleSummary per resource at month end; the log
    // gets one line for the lot, where the first of them stood, whether or
    // not the sim reported them next to each other.
    std::vector<sim::Notice> autoSales;
    for (const sim::Notice& notice : notices) {
        if (notice.kind == sim::NoticeKind::AutoSaleSummary) {
            autoSales.push_back(notice);
        }
    }
    bool salesLogged = false;
    for (const sim::Notice& notice : notices) {
        if (!showsInLog(notice, bidPlots)) {
            continue;
        }
        if (notice.kind == sim::NoticeKind::AutoSaleSummary) {
            if (!salesLogged) {
                appendLogText(day, mergedAutoSale(autoSales),
                              describeAutoSales(*content_, autoSales));
                salesLogged = true;
            }
            continue;
        }
        appendLog(day, notice);
        if (!pausedBy_.has_value() && autoPause_.shouldPause(notice, kPlayer)) {
            pausedBy_ = notice;
        }
    }
}

void Session::appendLog(sim::Day day, const sim::Notice& notice) {
    appendLogText(day, notice, describeNotice(*content_, view_, notice));
}

void Session::appendLogText(sim::Day day, const sim::Notice& notice, std::string text) {
    LogEntry entry;
    entry.day = day;
    entry.notice = notice;
    entry.text = std::move(text);
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
