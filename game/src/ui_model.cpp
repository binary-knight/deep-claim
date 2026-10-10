#include "ui_model.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/view.h"

#include "game/auto_pause.h"
#include "game/format.h"
#include "game/session.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace game {

namespace {

// Display sums saturate here rather than overflow; the sim keeps its own cap.
constexpr std::int64_t kMaxDisplayCents = 1'000'000'000'000'000LL;
constexpr std::size_t kCompanyNameMax = 26;  // fits the 240dp header slot

// Short labels on chips and buttons stay literal (spec 002 decision: only
// sentences live in the catalogue).
const char* speedLabel(Speed speed) {
    switch (speed) {
        case Speed::Paused:
            return "Paused";
        case Speed::Normal:
            return "1x";
        case Speed::Fast:
            return "5x";
        case Speed::Fastest:
            return "20x";
    }
    return "";
}

const char* mineStateLabel(sim::MineState state) {
    switch (state) {
        case sim::MineState::Building:
            return "Building";
        case sim::MineState::Operating:
            return "Operating";
        case sim::MineState::Mothballed:
            return "Mothballed";
        case sim::MineState::Depleted:
            return "Depleted";
        case sim::MineState::Closed:
            return "Closed";
    }
    return "";
}

const char* mineStateClass(sim::MineState state) {
    switch (state) {
        case sim::MineState::Building:
            return "warn";
        case sim::MineState::Operating:
            return "good";
        case sim::MineState::Closed:
            return "danger";
        case sim::MineState::Mothballed:
        case sim::MineState::Depleted:
            break;
    }
    return "muted";
}

std::string plotLabel(sim::PlotId id) {
    return std::format("#{}", id.value() + 1);
}

// A resource's name, or "" for an id the content does not have (a waiting
// order can name anything; the sim rejects it).
std::string resourceName(const sim::Content& content, sim::ResourceId resource) {
    return resource.valid() && resource.index() < content.resources.size()
               ? content.resources[resource.index()].name
               : std::string();
}

std::string dayLabel(sim::Day day) {
    return std::format("day {}", day);
}

sim::Day daysLeft(sim::Day target, sim::Day today) {
    return target > today ? target - today : 0;
}

std::string daysUntil(const sim::Content& content, sim::Day target, sim::Day today) {
    return text(content, "ui.days_in", {{"days", std::to_string(daysLeft(target, today))}});
}

// Sums of what the waiting orders cost and borrow stop at the largest amount
// either way: an order can carry any amount until the sim turns it away, and
// Money's own arithmetic ends the program on overflow. (The session keeps
// the same pair for its sums.)
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

// Cash the player can still spend today: what is not held against bids.
sim::Money spendableCash(const sim::CompanyView& view) {
    return saturatingSub(view.cash, view.reservedCash);
}

std::string moneyTimesTonnes(sim::Money price, std::int64_t tonnes) {
    const std::int64_t cents =
        sim::checkedMulDiv(price.cents(), tonnes, 1).value_or(kMaxDisplayCents);
    return formatMoney(sim::Money::fromCents(std::min(cents, kMaxDisplayCents)));
}

bool knowsTech(const sim::CompanyView& view, sim::TechId id) {
    return std::find(view.completedTechs.begin(), view.completedTechs.end(), id) !=
           view.completedTechs.end();
}

// Presentation of the sim's unlock rule: a resource with an unlocking tech
// the player has not completed cannot be mined or sold yet.
bool resourceLocked(const sim::Content& content, const sim::CompanyView& view,
                    sim::ResourceId resource) {
    const sim::TechId required = content.resources[resource.index()].unlockedBy;
    return required.valid() && !knowsTech(view, required);
}

// How close the player is to a locked resource (spec 004): days left on the
// unlocking research when it is in the lab, else whether it could start now.
struct UnlockOutlook {
    sim::Day days = 0;  // > 0 while the unlocking tech is being researched
    bool ready = false;
};

UnlockOutlook unlockOutlook(const sim::CompanyView& view, sim::ResourceId resource) {
    UnlockOutlook outlook;
    const std::size_t r = resource.index();
    if (r < view.unlockDaysRemaining.size()) {
        outlook.days = view.unlockDaysRemaining[r];
    }
    outlook.ready = outlook.days == 0 && r < view.unlockReady.size() && view.unlockReady[r] != 0;
    return outlook;
}

// "needs <tech>" until the player is close; then an invitation.
std::string lockedNote(const sim::Content& content, const sim::CompanyView& view,
                       sim::ResourceId resource) {
    const sim::TechId required = content.resources[resource.index()].unlockedBy;
    if (!required.valid()) {
        return "";
    }
    const std::string& tech = content.techs[required.index()].name;
    const UnlockOutlook outlook = unlockOutlook(view, resource);
    if (outlook.days > 0) {
        return textPlural(content, "ui.unlock_in_days", outlook.days,
                          {{"days", std::to_string(outlook.days)}});
    }
    if (outlook.ready) {
        return text(content, "ui.unlock_ready", {{"tech", tech}});
    }
    return text(content, "ui.needs_tech", {{"tech", tech}});
}

std::string companyLabel(const sim::CompanyView& view, sim::CompanyId id) {
    if (!id.valid()) {
        return "";
    }
    return id == Session::kPlayer ? "Yours" : view.companies[id.index()].name;
}

std::string effectsText(const sim::Content& content, const sim::TechDef& tech) {
    std::string out;
    for (const sim::TechEffect& effect : tech.effects) {
        if (!out.empty()) {
            out += ", ";
        }
        switch (effect.type) {
            case sim::TechEffectType::SurveyNoiseMult:
                out += "survey error " + formatRatio(effect.value);
                break;
            case sim::TechEffectType::ExtractionCapexMult:
                out += "mine capex " + formatRatio(effect.value);
                break;
            case sim::TechEffectType::OpexMult:
                out += "mine opex " + formatRatio(effect.value);
                break;
            case sim::TechEffectType::OutputMult:
                out += "mine output " + formatRatio(effect.value);
                break;
            case sim::TechEffectType::StorageCostMult:
                out += "storage cost " + formatRatio(effect.value);
                break;
            case sim::TechEffectType::UnlockResource:
                out += "unlocks " + content.resources[effect.resource.index()].name;
                break;
        }
    }
    return out;
}

// The player's latest completed survey of a plot, if any, and whether one is
// pending.
struct SurveyLookup {
    const sim::SurveyView* latest = nullptr;
    bool pending = false;
};

SurveyLookup findSurvey(const sim::CompanyView& view, sim::PlotId plot) {
    SurveyLookup found;
    for (const sim::SurveyView& survey : view.surveys) {
        if (survey.plot != plot) {
            continue;
        }
        if (survey.complete) {
            found.latest = &survey;
        } else {
            found.pending = true;
        }
    }
    return found;
}

const sim::MineView* findMine(const sim::CompanyView& view, sim::MineId id) {
    for (const sim::MineView& mine : view.mines) {
        if (mine.id == id) {
            return &mine;
        }
    }
    return nullptr;
}

// Bids on a listed plot that are not the player's.
std::uint32_t rivalBids(const sim::AuctionView& auction) {
    return auction.bidCount - (auction.myBid > sim::Money::zero() ? 1U : 0U);
}

// An operating mine that produced nothing on its last processed day after
// its opening day has nothing it can extract (the sim says why in the log).
bool mineIdle(const sim::MineView& mine, sim::Day today) {
    if (mine.state != sim::MineState::Operating || mine.operationalDay >= today) {
        return false;
    }
    return std::ranges::all_of(mine.lastOutputTonnes, [](std::int64_t t) { return t == 0; });
}

std::string outputText(const sim::Content& content, const sim::MineView& mine) {
    std::string out;
    for (std::uint32_t r = 0; r < mine.lastOutputTonnes.size(); ++r) {
        if (mine.lastOutputTonnes[r] > 0) {
            std::string tonnes = formatTonnes(mine.lastOutputTonnes[r]);
            tonnes.resize(tonnes.size() - 2);  // drop the " t": the unit is t/day
            out += std::format("{}{} t/day {}", out.empty() ? "" : " + ", tonnes,
                               content.resources[r].name);
        }
    }
    return out.empty() ? "—" : out;
}

// Remaining tonnes and days from the sim's estimate of what the owner's
// latest survey leaves, at the current output. Presentation only.
std::string remainingText(const sim::Content& content, const sim::MineView& mine, sim::Day today) {
    switch (mine.state) {
        case sim::MineState::Building:
            return text(content, "ui.remaining_building",
                        {{"days", std::to_string(daysLeft(mine.operationalDay, today))}});
        case sim::MineState::Depleted:
            return text(content, "ui.remaining_exhausted");
        case sim::MineState::Closed:
            return text(content, "ui.remaining_closed");
        case sim::MineState::Operating:
        case sim::MineState::Mothballed:
            break;
    }
    std::int64_t tonnes = 0;
    std::int64_t days = 0;
    bool estimated = false;
    bool producing = false;
    for (std::size_t r = 0; r < mine.estimatedRemainingTonnes.size(); ++r) {
        const std::int64_t left = mine.estimatedRemainingTonnes[r];
        if (left < 0) {
            continue;  // not in the survey
        }
        estimated = true;
        tonnes += left;
        if (mine.lastOutputTonnes[r] > 0) {
            producing = true;
            days = std::max(days, left / mine.lastOutputTonnes[r]);
        }
    }
    if (mine.state == sim::MineState::Mothballed) {
        return estimated
                   ? text(content, "ui.remaining_mothballed", {{"tonnes", formatTonnes(tonnes)}})
                   : text(content, "ui.remaining_mothballed_unknown");
    }
    if (mineIdle(mine, today)) {
        return text(content, "ui.remaining_idle");
    }
    if (!estimated) {
        return text(content, "ui.remaining_unknown");
    }
    if (!producing) {
        return text(content, "ui.remaining_tonnes", {{"tonnes", formatTonnes(tonnes)}});
    }
    return text(content, "ui.remaining_est",
                {{"tonnes", formatTonnes(tonnes)}, {"days", std::to_string(days)}});
}

// "+$12.3k" or "-$2.1k": a gain says so as plainly as a loss does.
std::string signedMoney(sim::Money amount) {
    return (amount >= sim::Money::zero() ? "+" : "") + formatMoney(amount);
}

}  // namespace

std::string exactPrice(sim::Money amount) {
    // exactMoney is "-1430.00": the sign, the dollars, a point and two cents.
    std::string digits = exactMoney(amount);
    const bool negative = digits.starts_with('-');
    if (negative) {
        digits.erase(0, 1);
    }
    constexpr std::size_t kCentsAndPoint = 3;
    constexpr std::size_t kGroup = 3;
    for (std::size_t at = digits.size() - kCentsAndPoint; at > kGroup; at -= kGroup) {
        digits.insert(at - kGroup, ",");
    }
    return (negative ? "-$" : "$") + digits;
}

namespace {

// What a mine earns a day and the price at which that is nothing (spec 008),
// as the sim worked them out for the view: formatted here, never recomputed.
// A mothballed mine leads with what it costs to keep, written as the loss it
// is, and then says what it would earn if reopened today.
MineFigures mineFigures(const sim::Content& content, const sim::MineView& mine) {
    MineFigures figures;
    if (mine.state == sim::MineState::Depleted || mine.state == sim::MineState::Closed) {
        return figures;  // nothing left to earn: the row already says why
    }
    figures.shown = true;
    const auto perDay = [&](sim::Money amount) {
        return text(content, "ui.mine_margin", {{"amount", signedMoney(amount)}});
    };
    sim::Money shownFirst = mine.dailyMargin;
    if (mine.state == sim::MineState::Mothballed) {
        figures.mothballed = true;
        shownFirst = saturatingSub(sim::Money::zero(), mine.dailyOpex);
        if (mine.hasEarnings) {
            figures.reopen = perDay(mine.dailyMargin);
            figures.reopen_loss = mine.dailyMargin < sim::Money::zero();
        }
    } else if (!mine.hasEarnings) {
        figures.earns = text(content, "ui.mine_no_figure");
        return figures;
    }
    figures.earns = perDay(shownFirst);
    figures.loss = shownFirst < sim::Money::zero();
    if (mine.hasEarnings) {
        figures.break_even =
            text(content, "ui.mine_break_even", {{"amount", exactPrice(mine.breakEvenPrice)}});
        figures.note = text(content, "ui.mine_margin_note");
    }
    return figures;
}

std::string formatPercent(sim::Fixed ratio) {
    return std::to_string(ratio.raw() / (sim::Fixed::kScale / 100)) + "%";
}

std::string oddsText(const sim::Content& content, const sim::PlotView& plot) {
    std::string odds;
    for (const sim::DepositPrior& prior : content.regions[plot.region.index()].deposits) {
        if (!odds.empty()) {
            odds += " · ";
        }
        odds += std::format("{} {}", content.resources[prior.resource.index()].name,
                            formatPercent(prior.presenceProb));
    }
    return odds;
}

std::string monthsBeforeBankruptcy(const sim::Content& content, const sim::CompanyView& view) {
    const std::uint32_t grace = content.balance.bankruptcyGraceMonths + 1;
    return std::to_string(grace > view.monthsInsolvent ? grace - view.monthsInsolvent : 0);
}

// ---- orders waiting for the clock (spec 007) ----

template <typename... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};
template <typename... Fs>
Overloaded(Fs...) -> Overloaded<Fs...>;

// Session ids count up from one, so an int holds them for longer than
// anyone will play; one that did not fit would read as "nothing to cancel".
int orderId(std::uint64_t id) {
    return id <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ? static_cast<int>(id)
                                                                             : 0;
}

// The chip or line for a waiting order, from the catalogue. The two switches
// have a label for each way they can be set: ui.pending_policy_on and _off
// for a resource's auto-sell, ui.pending_cover_on and _off for cover costs.
std::string pendingText(const sim::Content& content, const sim::Command& command) {
    return std::visit(
        Overloaded{
            [&](const sim::CommissionSurvey&) { return text(content, "ui.pending_survey"); },
            [&](const sim::BidOnPlot& bid) {
                return text(content, "ui.pending_bid", {{"amount", formatMoney(bid.amount)}});
            },
            [&](const sim::BuildMine&) { return text(content, "ui.pending_build"); },
            [&](const sim::StartResearch&) { return text(content, "ui.pending_research"); },
            [&](const sim::SetMineState& mine) {
                return text(content,
                            mine.operate != 0 ? "ui.pending_reopen" : "ui.pending_mothball");
            },
            [&](const sim::TakeLoan& loan) {
                return text(content, "ui.pending_loan", {{"amount", formatMoney(loan.principal)}});
            },
            [&](const sim::RepayLoan& repay) {
                return text(content, "ui.pending_repayment",
                            {{"amount", formatMoney(repay.amount)}});
            },
            [&](const sim::SellOre& sale) {
                return text(content, "ui.pending_sale",
                            {{"tonnes", formatTonnes(sale.tonnes)},
                             {"resource", resourceName(content, sale.resource)}});
            },
            [&](const sim::SetSellPolicy& policy) {
                return text(content,
                            policy.enabled != 0 ? "ui.pending_policy_on" : "ui.pending_policy_off",
                            {{"resource", resourceName(content, policy.resource)}});
            },
            [&](const sim::SetCoverCosts& cover) {
                return text(content,
                            cover.enabled != 0 ? "ui.pending_cover_on" : "ui.pending_cover_off");
            },
        },
        command);
}

// The session's queue as the interface shows it: one mark per waiting order,
// and the money left once the queue is counted.
struct Orders {
    const std::vector<PendingOrder>* waiting = nullptr;
    std::vector<OrderMark> marks;  // one per waiting order, same positions
    // Cash less reserved, less what the queue will cost, plus what its loans
    // bring in: the figure every affordability check uses.
    sim::Money available;
};

// Marks an order "not covered" when the money on hand at its turn falls
// short of its cost: cash less reserved, plus the loans given before it,
// less the orders before it that are covered. An order that is not covered
// takes nothing from the orders after it, as a rejected order would not.
// Display only (R-001): the sim decides what it accepts.
Orders readOrders(const Session& session) {
    const sim::CompanyView& view = session.view();
    Orders orders;
    orders.waiting = &session.pendingOrders();
    const sim::Money spendable = spendableCash(view);
    orders.available =
        saturatingAdd(saturatingSub(spendable, session.committed()), session.pendingBorrowed());
    sim::Money running = spendable;
    for (const PendingOrder& order : *orders.waiting) {
        OrderMark mark;
        mark.pending = true;
        mark.id = orderId(order.id);
        mark.text = pendingText(session.content(), order.command);
        if (const auto* loan = std::get_if<sim::TakeLoan>(&order.command)) {
            running = saturatingAdd(running, std::max(loan->principal, sim::Money::zero()));
        } else {
            const sim::Money cost = session.orderCost(order.command);
            mark.not_covered = cost > sim::Money::zero() && cost > running;
            if (!mark.not_covered) {
                running = saturatingSub(running, cost);
            }
        }
        orders.marks.push_back(std::move(mark));
    }
    return orders;
}

// Position of the last waiting order of type T that `matches`.
template <typename T, typename Matches>
std::optional<std::size_t> lastOrder(const Orders& orders, Matches matches) {
    for (std::size_t i = orders.waiting->size(); i-- > 0;) {
        const auto* command = std::get_if<T>(&(*orders.waiting)[i].command);
        if (command != nullptr && matches(*command)) {
            return i;
        }
    }
    return std::nullopt;
}

// The mark for a control: its last waiting order, or nothing pending.
template <typename T, typename Matches>
OrderMark markFor(const Orders& orders, Matches matches) {
    const std::optional<std::size_t> at = lastOrder<T>(orders, matches);
    return at ? orders.marks[*at] : OrderMark{};
}

OrderMark mineOrderMark(const Orders& orders, sim::MineId mine) {
    return markFor<sim::SetMineState>(
        orders, [mine](const sim::SetMineState& order) { return order.mine == mine; });
}

OrderMark buildOrderMark(const Orders& orders, sim::PlotId plot) {
    return markFor<sim::BuildMine>(
        orders, [plot](const sim::BuildMine& order) { return order.plot == plot; });
}

PendingRow pendingRow(const OrderMark& mark) {
    return PendingRow{mark.id, mark.text, mark.not_covered};
}

// The strip's Committed figure, the count line beside the speed controls,
// the list behind it, and the Finance and Market sheets' pending lines.
void fillPending(const Session& session, const Orders& orders, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const sim::Money committed = session.committed();
    model.spendable = formatMoney(spendableCash(view));
    model.available = formatMoney(orders.available);
    model.has_committed = committed > sim::Money::zero();
    model.committed = model.has_committed
                          ? text(content, "ui.committed", {{"amount", formatMoney(committed)}})
                          : "";
    model.pending_orders.clear();
    model.finance_orders.clear();
    model.market_orders.clear();
    model.pending_shortfall = false;
    for (std::size_t i = 0; i < orders.marks.size(); ++i) {
        const sim::Command& command = (*orders.waiting)[i].command;
        const PendingRow row = pendingRow(orders.marks[i]);
        model.pending_orders.push_back(row);
        model.pending_shortfall = model.pending_shortfall || row.not_covered;
        if (std::holds_alternative<sim::TakeLoan>(command) ||
            std::holds_alternative<sim::RepayLoan>(command)) {
            model.finance_orders.push_back(row);
        } else if (std::holds_alternative<sim::SellOre>(command)) {
            model.market_orders.push_back(row);
        }
    }
    model.pending_count = static_cast<int>(model.pending_orders.size());
    model.has_pending = model.pending_count > 0;
    model.pending_line = model.has_pending
                             ? textPlural(content, "ui.pending_count", model.pending_count,
                                          {{"count", std::to_string(model.pending_count)}})
                             : "";
    model.loan_order = markFor<sim::TakeLoan>(orders, [](const sim::TakeLoan&) { return true; });
    model.cover_order =
        markFor<sim::SetCoverCosts>(orders, [](const sim::SetCoverCosts&) { return true; });
    model.ui_cancel = text(content, "ui.cancel");
    model.ui_cancel_all = text(content, "ui.cancel_all");
    model.ui_not_covered = text(content, "ui.pending_not_covered");
    model.ui_mine_loss = text(content, "ui.mine_loss");
    model.ui_mine_if_reopened = text(content, "ui.mine_if_reopened");
    model.ui_mine_earns = text(content, "ui.mine_earns");
    model.ui_mine_loses = text(content, "ui.mine_loses");
    model.ui_mine_mothballed = text(content, "ui.mine_mothballed");
    model.ui_mine_if_reopened_key = text(content, "ui.mine_if_reopened_key");
}

// ---- header and strip ----

void fillStrip(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const std::vector<sim::Money>& cash = session.cashHistory();
    // Cash change since the start of the month, as far back as this session
    // remembers.
    const std::size_t back = std::min<std::size_t>(cash.size() - 1, view.day % sim::kDaysPerMonth);
    const sim::Money net = view.cash - cash[cash.size() - 1 - back];
    model.month_net = (net >= sim::Money::zero() ? "+" : "") + formatMoney(net);
    model.month_good = net >= sim::Money::zero();
    model.storage_month = formatMoney(view.monthlyStorageCost) + "/mo";
    model.storage_month_long = formatMoney(view.monthlyStorageCost) + "/month";
    model.solvent = view.monthsInsolvent == 0;
    model.solvency = model.solvent ? "solvent" : "insolvent";
    model.solvency_sub =
        model.solvent ? text(content, "ui.solvency_grace",
                             {{"months", std::to_string(content.balance.bankruptcyGraceMonths)}})
                      : text(content, "ui.solvency_left",
                             {{"months", monthsBeforeBankruptcy(content, view)}});

    const sim::PlotView* next = nullptr;
    sim::Day nextDay = 0;
    for (const sim::PlotView& plot : view.plots) {
        if (plot.listing != sim::PlotListing::Listed || !plot.auction) {
            continue;
        }
        const sim::Day closes = plot.auction->closesOnDay;
        if (next == nullptr || closes < nextDay) {
            next = &plot;
            nextDay = closes;
        }
    }
    model.next_auction = text(content, "ui.strip_no_auction");
    if (next != nullptr) {
        model.next_auction =
            text(content, "ui.strip_next_auction",
                 {{"plot", plotLabel(next->id)}, {"day", std::to_string(nextDay)}});
    }

    int operating = 0;
    int building = 0;
    int mothballed = 0;
    int depleted = 0;
    for (const sim::MineView& mine : view.mines) {
        switch (mine.state) {
            case sim::MineState::Operating:
                ++operating;
                break;
            case sim::MineState::Building:
                ++building;
                break;
            case sim::MineState::Mothballed:
                ++mothballed;
                break;
            case sim::MineState::Depleted:
            case sim::MineState::Closed:
                ++depleted;
                break;
        }
    }
    model.mines_operating = operating > 0 ? std::format("{} operating", operating) : "";
    model.mines_building = building > 0 ? std::format("{} building", building) : "";
    model.mines_mothballed = mothballed > 0 ? std::format("{} mothballed", mothballed) : "";
    model.mines_depleted = depleted > 0 ? std::format("{} depleted", depleted) : "";

    model.research_strip = text(content, "ui.strip_research_idle");
    if (view.activeResearch) {
        const sim::ResearchView& research = *view.activeResearch;
        model.research_strip = text(content, "ui.strip_research",
                                    {{"tech", content.techs[research.tech.index()].name},
                                     {"day", std::to_string(research.completesDay)}});
    }
    model.has_world = !view.events.empty();
    model.world_event.clear();
    model.world_sub.clear();
    if (model.has_world) {
        const sim::ActiveEventView& event = view.events.front();
        model.world_event = content.events[event.event.index()].name;
        model.world_sub = text(content, "ui.strip_world",
                               {{"days", std::to_string(daysLeft(event.endsDay, view.day))}});
    }
}

// ---- dashboard: needs you ----

struct PendingNeed {
    sim::Day days = 0;
    int priority = 0;  // lower first; warnings before dated items
    NeedRow row;
};

constexpr int kWarningPriority = 0;
constexpr int kDatedPriority = 10;

// Standing problems: insolvency, storage above a tenth of cash, idle mines.
void addWarningNeeds(const sim::Content& content, const sim::CompanyView& view,
                     const Orders& orders, std::vector<PendingNeed>& pending) {
    if (view.monthsInsolvent > 0) {
        NeedRow row;
        row.title = text(content, "ui.need_insolvent_title",
                         {{"months", monthsBeforeBankruptcy(content, view)}});
        row.sub = text(content, "ui.need_insolvent_sub");
        row.action = 4;
        row.act_label = "Finance";
        pending.push_back({0, kWarningPriority, std::move(row)});
    }
    if (view.monthlyStorageCost > sim::Money::zero() &&
        (view.cash <= sim::Money::zero() ||
         view.monthlyStorageCost.cents() > view.cash.cents() / 10)) {
        NeedRow row;
        row.title = text(content, "ui.need_storage_title");
        row.sub = text(
            content, "ui.need_storage_sub",
            {{"amount", formatMoney(view.monthlyStorageCost)}, {"cash", formatMoney(view.cash)}});
        row.action = 3;
        row.act_label = "Sell";
        pending.push_back({0, kWarningPriority + 1, std::move(row)});
    }
    for (const sim::MineView& mine : view.mines) {
        if (!mineIdle(mine, view.day)) {
            continue;
        }
        NeedRow row;
        row.title = text(content, "ui.need_idle_title", {{"plot", plotLabel(mine.plot)}});
        row.sub = text(content, "ui.need_idle_sub");
        row.action = 2;
        row.target = static_cast<int>(mine.id.value());
        row.act_label = "Mothball";
        row.order = mineOrderMark(orders, mine.id);
        pending.push_back({0, kWarningPriority + 2, std::move(row)});
    }
}

// A tech the player could start this minute: not known, prerequisites done,
// affordable with the money available. The Research tab's Start button uses
// the same test.
bool techStartable(const sim::Content& content, const sim::CompanyView& view, sim::Money available,
                   std::uint32_t i) {
    const sim::TechDef& tech = content.techs[i];
    return !view.bankrupt && !view.activeResearch && !knowsTech(view, sim::TechId(i)) &&
           std::ranges::all_of(tech.prerequisites,
                               [&](sim::TechId p) { return knowsTech(view, p); }) &&
           tech.cost <= available;
}

constexpr sim::Day kSurveyNewsDays = 15;

// Things waiting on a decision rather than a date (spec 004): an idle lab
// with a project it could start, and a survey report that has just landed
// on a plot still at auction.
void addDecisionNeeds(const sim::Content& content, const sim::CompanyView& view,
                      sim::Money available, std::vector<PendingNeed>& pending) {
    int startable = 0;
    for (std::uint32_t i = 0; i < content.techs.size(); ++i) {
        startable += techStartable(content, view, available, i) ? 1 : 0;
    }
    if (startable > 0) {
        NeedRow row;
        row.title = text(content, "ui.need_lab_idle_title");
        row.sub = textPlural(content, "ui.need_lab_idle_sub", startable,
                             {{"count", std::to_string(startable)}});
        row.action = 5;
        row.act_label = "Research";
        pending.push_back({0, kWarningPriority + 3, std::move(row)});
    }
    for (const sim::SurveyView& survey : view.surveys) {
        const sim::PlotView& plot = view.plots[survey.plot.index()];
        if (!survey.complete || view.day > survey.completesDay + kSurveyNewsDays ||
            plot.listing != sim::PlotListing::Listed || !plot.auction ||
            findSurvey(view, survey.plot).latest != &survey) {
            continue;
        }
        NeedRow row;
        row.title = text(content, "ui.need_survey_in_title", {{"plot", plotLabel(plot.id)}});
        row.sub = text(content, "ui.need_survey_in_sub",
                       {{"day", std::to_string(plot.auction->closesOnDay)}});
        row.action = 1;
        row.target = static_cast<int>(plot.id.value());
        row.act_label = "Read";
        pending.push_back({0, kWarningPriority + 4, std::move(row)});
    }
}

// A closing auction matters when the player has a bid in or has surveyed
// the plot.
std::optional<PendingNeed> auctionNeed(const sim::Content& content, const sim::CompanyView& view,
                                       const sim::PlotView& plot) {
    if (plot.listing != sim::PlotListing::Listed || !plot.auction) {
        return std::nullopt;
    }
    const sim::AuctionView& auction = *plot.auction;
    const bool mine = auction.myBid > sim::Money::zero();
    const bool surveyed = findSurvey(view, plot.id).latest != nullptr;
    if (!mine && !surveyed) {
        return std::nullopt;
    }
    const std::uint32_t others = rivalBids(auction);
    const std::string bids = std::to_string(auction.bidCount);
    NeedRow row;
    row.title = text(content, "ui.need_auction_title", {{"plot", plotLabel(plot.id)}});
    if (mine && others > 0) {
        row.sub = text(content, "ui.need_auction_sub_contested",
                       {{"bids", bids}, {"amount", formatMoney(auction.myBid)}});
    } else if (mine) {
        row.sub =
            text(content, "ui.need_auction_sub_leading", {{"amount", formatMoney(auction.myBid)}});
    } else if (others > 0) {
        row.sub = text(content, "ui.need_auction_sub_rival", {{"bids", bids}});
    } else {
        row.sub = text(content, "ui.need_auction_sub_open");
    }
    row.days = daysUntil(content, auction.closesOnDay, view.day);
    row.action = 1;
    row.target = static_cast<int>(plot.id.value());
    row.act_label = mine ? "Raise bid" : "Bid";
    return PendingNeed{daysLeft(auction.closesOnDay, view.day), kDatedPriority, std::move(row)};
}

// Things with a date: auctions, mines opening, events ending, research.
void addDatedNeeds(const sim::Content& content, const sim::CompanyView& view,
                   std::vector<PendingNeed>& pending) {
    for (const sim::PlotView& plot : view.plots) {
        if (std::optional<PendingNeed> need = auctionNeed(content, view, plot)) {
            pending.push_back(std::move(*need));
        }
    }
    for (const sim::MineView& mine : view.mines) {
        if (mine.state != sim::MineState::Building) {
            continue;
        }
        NeedRow row;
        row.title = text(content, "ui.need_mine_opens_title", {{"plot", plotLabel(mine.plot)}});
        row.sub = text(content, "ui.need_mine_opens_sub");
        row.days = daysUntil(content, mine.operationalDay, view.day);
        pending.push_back(
            {daysLeft(mine.operationalDay, view.day), kDatedPriority, std::move(row)});
    }
    for (const sim::ActiveEventView& event : view.events) {
        const sim::EventDef& def = content.events[event.event.index()];
        NeedRow row;
        row.title = text(content, "ui.need_event_title", {{"event", def.name}});
        row.sub = def.description;
        row.days = daysUntil(content, event.endsDay, view.day);
        pending.push_back({daysLeft(event.endsDay, view.day), kDatedPriority, std::move(row)});
    }
    if (view.activeResearch) {
        const sim::ResearchView& research = *view.activeResearch;
        NeedRow row;
        row.title = text(content, "ui.need_research_title");
        row.sub = content.techs[research.tech.index()].name;
        row.days = daysUntil(content, research.completesDay, view.day);
        pending.push_back(
            {daysLeft(research.completesDay, view.day), kDatedPriority, std::move(row)});
    }
}

void fillNeeds(const Session& session, const Orders& orders, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    std::vector<PendingNeed> pending;
    addWarningNeeds(content, view, orders, pending);
    addDecisionNeeds(content, view, orders.available, pending);
    addDatedNeeds(content, view, pending);
    std::stable_sort(
        pending.begin(), pending.end(), [](const PendingNeed& a, const PendingNeed& b) {
            return a.priority != b.priority ? a.priority < b.priority : a.days < b.days;
        });
    model.needs.clear();
    for (PendingNeed& item : pending) {
        item.row.number = static_cast<int>(model.needs.size()) + 1;
        model.needs.push_back(std::move(item.row));
    }
    model.needs_none = model.needs.empty() ? text(content, "ui.need_none") : "";
}

// ---- dashboard: what you own ----

OwnedRow makeOwnedRow(const sim::Content& content, const sim::CompanyView& view,
                      const Orders& orders, const sim::PlotView& plot) {
    OwnedRow row;
    row.index = static_cast<int>(plot.id.value());
    row.name = plotLabel(plot.id);
    row.region = content.regions[plot.region.index()].name;
    row.area = std::format("{} ha", plot.areaHectares);
    row.output = "—";
    const sim::MineView* mine = plot.mine ? findMine(view, plot.mine->id) : nullptr;
    if (mine == nullptr) {
        row.mine = "No mine";
        row.mine_class = "muted";
        row.remaining = text(content, "ui.remaining_no_mine");
        row.can_build = !view.bankrupt;
        row.build_order = buildOrderMark(orders, plot.id);
        return row;
    }
    row.mine = mineStateLabel(mine->state);
    row.mine_class = mineStateClass(mine->state);
    row.output = mine->state == sim::MineState::Operating ? outputText(content, *mine) : "—";
    row.remaining = remainingText(content, *mine, view.day);
    row.figures = mineFigures(content, *mine);
    row.mine_operating = mine->state == sim::MineState::Operating;
    row.can_toggle = !view.bankrupt && (mine->state == sim::MineState::Operating ||
                                        mine->state == sim::MineState::Mothballed);
    row.mine_index = static_cast<int>(mine->id.value());
    row.mine_order = mineOrderMark(orders, mine->id);
    return row;
}

// One line per rival: plots, mines, and the plots it is bidding on. Sealed
// bids are anonymous; with a single rival, any bid that is not the player's
// is that rival's.
std::string rivalLine(const sim::Content& content, const sim::CompanyView& view,
                      const sim::CompanyPublicView& rival) {
    if (rival.bankrupt) {
        return text(content, "ui.rival_bankrupt", {{"company", rival.name}});
    }
    int mines = 0;
    std::string bidding;
    for (const sim::PlotView& plot : view.plots) {
        if (plot.mine && plot.mine->owner == rival.id) {
            ++mines;
        }
        if (plot.listing == sim::PlotListing::Listed && plot.auction &&
            rivalBids(*plot.auction) > 0 && view.companies.size() == 2) {
            bidding += (bidding.empty() ? "" : ", ") + plotLabel(plot.id);
        }
    }
    const std::string plots = std::to_string(rival.plotsOwned);
    const std::string mineCount = std::to_string(mines);
    if (bidding.empty()) {
        return text(content, "ui.rival_line_quiet",
                    {{"company", rival.name}, {"plots", plots}, {"mines", mineCount}});
    }
    return text(
        content, "ui.rival_line",
        {{"company", rival.name}, {"plots", plots}, {"mines", mineCount}, {"bidding", bidding}});
}

void fillOwned(const Session& session, const Orders& orders, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.owned.clear();
    for (const sim::PlotView& plot : view.plots) {
        if (plot.listing == sim::PlotListing::Sold && plot.owner == Session::kPlayer) {
            model.owned.push_back(makeOwnedRow(content, view, orders, plot));
        }
    }
    model.rival_line.clear();
    for (std::uint32_t c = 1; c < view.companies.size(); ++c) {
        if (!model.rival_line.empty()) {
            model.rival_line += " ";
        }
        model.rival_line += rivalLine(content, view, view.companies[c]);
    }
}

// ---- dashboard: the money ----

void fillFinanceRow(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.capacity_sub =
        text(content, "ui.fin_can_borrow", {{"amount", formatMoney(view.loanCapacity)}});
    std::int64_t valueCents = 0;
    std::int64_t tonnes = 0;
    int ores = 0;
    for (std::uint32_t r = 0; r < view.markets.size(); ++r) {
        const std::int64_t stock = view.inventoryTonnes[r];
        if (stock <= 0) {
            continue;
        }
        tonnes += stock;
        ++ores;
        valueCents +=
            sim::checkedMulDiv(view.markets[r].price.cents(), stock, 1).value_or(kMaxDisplayCents);
        valueCents = std::min(valueCents, kMaxDisplayCents);
    }
    model.warehouse_value = formatMoney(sim::Money::fromCents(valueCents));
    model.storage_sub =
        text(content, "ui.fin_storage_sub", {{"amount", formatMoney(view.monthlyStorageCost)}});
    sim::Money instalment;
    for (const sim::LoanView& loan : view.loans) {
        if (loan.active) {
            instalment += loan.principalPerMonth;
        }
    }
    model.instalment_sub =
        text(content, "ui.fin_instalment", {{"amount", formatMoney(instalment)}});
    model.storage_tonnes =
        textPlural(content, "ui.storage_tonnes", ores,
                   {{"tonnes", formatTonnes(tonnes)}, {"count", std::to_string(ores)}});
    // The storage rate is per resource in data; all ship at the same share of
    // base price, so the first resource states it.
    std::string percent = "0";
    if (!content.resources.empty() && content.resources[0].basePrice > sim::Money::zero()) {
        percent = std::to_string(content.resources[0].storageCostPerTonneMonth.cents() * 100 /
                                 content.resources[0].basePrice.cents());
    }
    model.storage_rate = text(content, "ui.storage_rate", {{"percent", percent}});
    model.storage_note = text(content, "ui.storage_note");
}

// ---- plots ----

// Among a survey's locked ores, the one whose unlock is nearest in the lab
// and one the lab could start on today.
struct NearestUnlock {
    sim::ResourceId soon;  // being researched; `days` until it is mineable
    sim::Day days = 0;
    sim::ResourceId ready;  // one project away

    [[nodiscard]] sim::ResourceId closest() const { return soon.valid() ? soon : ready; }
};

NearestUnlock nearestUnlock(const sim::Content& content, const sim::CompanyView& view,
                            const sim::SurveyView& survey) {
    NearestUnlock nearest;
    for (const sim::SurveyEstimateView& e : survey.estimates) {
        if (!resourceLocked(content, view, e.resource)) {
            continue;
        }
        const UnlockOutlook outlook = unlockOutlook(view, e.resource);
        if (outlook.days > 0 && (!nearest.soon.valid() || outlook.days < nearest.days)) {
            nearest.soon = e.resource;
            nearest.days = outlook.days;
        } else if (outlook.ready && !nearest.ready.valid()) {
            nearest.ready = e.resource;
        }
    }
    return nearest;
}

// The survey column of a for-sale row: estimate summary, error, and one flag
// chip (bids, not surveyed, empty, locked ore).
void fillSurveyCell(const sim::Content& content, const sim::CompanyView& view,
                    const sim::PlotView& plot, SaleRow& row) {
    const SurveyLookup found = findSurvey(view, plot.id);
    if (found.latest == nullptr) {
        row.survey = found.pending
                         ? text(content, "ui.survey_pending")
                         : text(content, "ui.survey_blind", {{"odds", oddsText(content, plot)}});
        row.flag = found.pending ? "" : "not surveyed";
        row.flag_class = "danger";
        return;
    }
    const std::vector<sim::SurveyEstimateView>& estimates = found.latest->estimates;
    if (estimates.empty()) {
        row.survey = text(content, "ui.survey_none");
        row.flag = "empty";
        row.flag_class = "";
        return;
    }
    bool lockedOnly = true;
    for (const sim::SurveyEstimateView& e : estimates) {
        if (!row.survey.empty()) {
            row.survey += " · ";
        }
        row.survey += std::format("{} ~{}", content.resources[e.resource.index()].name,
                                  formatTonnes(e.tonnesEstimate));
        const bool locked = resourceLocked(content, view, e.resource);
        lockedOnly = lockedOnly && locked;
        row.major = row.major || (e.major && !locked);
    }
    row.error = std::format("({})", formatRatio(estimates[0].relativeError));
    if (lockedOnly) {
        // "mineable in N days" or "one project away" once the player is close.
        const sim::ResourceId close = nearestUnlock(content, view, *found.latest).closest();
        row.flag = close.valid() ? lockedNote(content, view, close) : "locked ore";
        row.flag_class = "warn";
    } else if (plot.auction && rivalBids(*plot.auction) > 0) {
        row.flag =
            text(content, "ui.flag_bids", {{"bids", std::to_string(plot.auction->bidCount)}});
        row.flag_class = "warn";
    } else {
        row.flag = "surveyed";
        row.flag_class = "good";
    }
}

// "16% over the ask", or "at the ask" for a sale within half a percent of
// the minimum bid. Whole percent, to the nearest: the premium is a
// truncated ratio, so a bid of exactly 16% over arrives as 15.9999.
std::string overAskText(const sim::Content& content, const sim::SaleView& sale) {
    const std::int64_t percent = (sale.premium * 100 + sim::Fixed::fromRatio(1, 2)).truncToInt();
    return percent <= 0 ? text(content, "ui.sale_at_ask")
                        : text(content, "ui.sale_over", {{"percent", std::to_string(percent)}});
}

// The record of what land has sold for: the newest five settled auctions.
void fillRecentSales(const sim::Content& content, const sim::CompanyView& view, UiModel& model) {
    constexpr std::size_t kShown = 5;
    model.recent_sales.clear();
    for (const sim::SaleView& sale : view.recentSales) {
        if (model.recent_sales.size() == kShown) {
            break;
        }
        SoldRow row;
        row.index = static_cast<int>(sale.plot.value());
        row.name = plotLabel(sale.plot);
        row.mine = sale.buyer == Session::kPlayer;
        row.buyer =
            row.mine ? text(content, "ui.sale_buyer_you") : view.companies[sale.buyer.index()].name;
        row.paid = formatMoney(sale.paid);
        row.over = overAskText(content, sale);
        row.day = dayLabel(sale.day);
        model.recent_sales.push_back(row);
    }
    model.no_sales = model.recent_sales.empty() ? text(content, "ui.no_sales") : "";
}

void fillSale(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.for_sale.clear();
    for (const sim::PlotView& plot : view.plots) {
        if (plot.listing != sim::PlotListing::Listed || !plot.auction) {
            continue;
        }
        SaleRow row;
        row.index = static_cast<int>(plot.id.value());
        row.name = plotLabel(plot.id);
        row.region = content.regions[plot.region.index()].name;
        row.area = std::format("{} ha", plot.areaHectares);
        row.ask = formatMoney(plot.askPrice);
        row.bids = std::to_string(plot.auction->bidCount);
        row.closes = dayLabel(plot.auction->closesOnDay);
        fillSurveyCell(content, view, plot, row);
        row.has_flag = !row.flag.empty();
        model.for_sale.push_back(row);
    }
    if (view.unlistedPlotsRemaining > 0) {
        model.no_unlisted = textPlural(content, "ui.unlisted_plots", view.unlistedPlotsRemaining,
                                       {{"count", std::to_string(view.unlistedPlotsRemaining)}});
    } else if (view.withdrawnPlots == 0) {
        model.no_unlisted = text(content, "ui.no_unlisted");
    } else {
        model.no_unlisted = textPlural(content, "ui.no_unlisted_withdrawn", view.withdrawnPlots,
                                       {{"count", std::to_string(view.withdrawnPlots)}});
    }
    fillRecentSales(content, view, model);
}

// The survey block of the selected plot. Returns whether anything found
// there can be extracted today.
bool fillPanelSurvey(const sim::Content& content, const sim::CompanyView& view,
                     const SurveyLookup& found, PlotPanel& panel) {
    if (found.latest == nullptr) {
        panel.survey_title =
            found.pending ? text(content, "ui.survey_pending") : text(content, "ui.survey_never");
        return false;
    }
    if (found.latest->estimates.empty()) {
        panel.survey_title = text(content, "ui.survey_none");
        return false;
    }
    panel.survey_title =
        text(content, "ui.survey_title", {{"day", std::to_string(found.latest->completesDay)}});
    std::int64_t valueCents = 0;
    bool anyExtractable = false;
    for (const sim::SurveyEstimateView& e : found.latest->estimates) {
        EstimateRow row;
        row.resource = content.resources[e.resource.index()].name;
        row.tonnes = std::format("~{}", formatTonnes(e.tonnesEstimate));
        row.error = std::format("({})", formatRatio(e.relativeError));
        row.locked = resourceLocked(content, view, e.resource);
        row.major = e.major;
        row.needs = row.locked ? lockedNote(content, view, e.resource) : "";
        anyExtractable = anyExtractable || !row.locked;
        valueCents +=
            sim::checkedMulDiv(view.markets[e.resource.index()].price.cents(), e.tonnesEstimate, 1)
                .value_or(kMaxDisplayCents);
        valueCents = std::min(valueCents, kMaxDisplayCents);
        panel.estimates.push_back(row);
    }
    panel.value = "~" + formatMoney(sim::Money::fromCents(valueCents));
    return anyExtractable;
}

// Two independent warnings: the build risk (blind bet, survey still out,
// empty plot, locked ore only) and competition on the auction. One does not
// hide the other.
// Buy-ahead advice for a plot whose ore is all locked but nearly in reach:
// the nearest unlock in the lab, else one the lab could start today. Empty
// when the player is not close to any of it.
std::string lockedAdvice(const sim::Content& content, const sim::CompanyView& view,
                         const sim::SurveyView& survey) {
    const NearestUnlock nearest = nearestUnlock(content, view, survey);
    const sim::ResourceId soon = nearest.soon;
    const sim::Day soonDays = nearest.days;
    const sim::ResourceId ready = nearest.ready;
    if (soon.valid()) {
        return textPlural(content, "ui.notice_locked_soon", soonDays,
                          {{"resource", content.resources[soon.index()].name},
                           {"days", std::to_string(soonDays)}});
    }
    if (ready.valid()) {
        const sim::TechId tech = content.resources[ready.index()].unlockedBy;
        return text(content, "ui.notice_locked_ready",
                    {{"resource", content.resources[ready.index()].name},
                     {"tech", content.techs[tech.index()].name}});
    }
    return "";
}

void fillPanelNotice(const sim::Content& content, const sim::CompanyView& view,
                     const sim::PlotView& plot, const SurveyLookup& found, bool anyExtractable,
                     PlotPanel& panel) {
    if (panel.listed && plot.auction && rivalBids(*plot.auction) > 0) {
        panel.contest = text(content, "ui.notice_contested");
    }
    if (plot.mine) {
        return;
    }
    if (found.latest == nullptr) {
        // A survey still out is not knowledge yet.
        panel.notice = found.pending ? text(content, "ui.notice_survey_pending")
                                     : text(content, "ui.notice_unsurveyed",
                                            {{"amount", formatMoney(content.balance.surveyCost)}});
        panel.notice_class = found.pending ? "warn" : "danger";
        return;
    }
    if (found.latest->estimates.empty()) {
        panel.notice = text(content, "ui.notice_empty");
        panel.notice_class = "warn";
        return;
    }
    if (!anyExtractable) {
        panel.notice_class = "warn";
        panel.notice = lockedAdvice(content, view, *found.latest);
        if (!panel.notice.empty()) {
            return;
        }
        std::string techs;
        for (const EstimateRow& row : panel.estimates) {
            if (!row.needs.empty() && techs.find(row.needs) == std::string::npos) {
                techs += (techs.empty() ? "" : ", ") + row.needs;
            }
        }
        panel.notice = text(content, "ui.notice_locked_only", {{"needs", techs}});
        panel.notice_class = "warn";
    }
}

// Orders waiting on the selected plot: each stands in for its button.
void fillPanelOrders(const Orders& orders, const sim::PlotView& plot, PlotPanel& panel) {
    const sim::PlotId id = plot.id;
    panel.survey_order = markFor<sim::CommissionSurvey>(
        orders, [id](const sim::CommissionSurvey& order) { return order.plot == id; });
    panel.bid_order = markFor<sim::BidOnPlot>(
        orders, [id](const sim::BidOnPlot& order) { return order.plot == id; });
    panel.build_order = buildOrderMark(orders, id);
    if (plot.mine) {
        panel.mine_order = mineOrderMark(orders, plot.mine->id);
    }
}

void fillPanelActions(const sim::Content& content, const sim::CompanyView& view,
                      sim::Money available, const sim::PlotView& plot, const SurveyLookup& found,
                      PlotPanel& panel) {
    // A worked-out mine has answered the question a survey asks.
    const bool workedOut = panel.owned && plot.mine &&
                           (plot.mine->state == sim::MineState::Depleted ||
                            plot.mine->state == sim::MineState::Closed);
    panel.can_survey =
        !found.pending && (!plot.owner.valid() || panel.owned) && !view.bankrupt && !workedOut;
    if (workedOut) {
        panel.survey_note = text(content, "ui.survey_depleted");
    } else if (panel.can_survey && panel.listed && plot.auction) {
        // An order placed now is processed today and reports after the
        // survey's duration; the auction is settled earlier in its closing
        // day than reports are delivered, so the same day is already late.
        const sim::Day report = view.day + content.balance.surveyDurationDays;
        if (report >= plot.auction->closesOnDay) {
            panel.survey_note = text(content, "ui.survey_late",
                                     {{"report", std::to_string(report)},
                                      {"day", std::to_string(plot.auction->closesOnDay)}});
        }
    }
    panel.survey_label =
        text(content, found.latest != nullptr ? "ui.survey_again" : "ui.survey_first",
             {{"amount", formatMoney(content.balance.surveyCost)}});
    panel.can_afford_survey = content.balance.surveyCost <= available;
    panel.survey_blocked = text(content, "ui.survey_unaffordable",
                                {{"amount", formatMoney(content.balance.surveyCost)}});
    panel.can_build = panel.owned && !plot.mine && !view.bankrupt;
    panel.can_afford_mine = plot.mineCapexQuote <= available;
    panel.build_label = text(content, "ui.build_label", {{"amount", panel.capex}});
    panel.build_blocked = text(content, "ui.build_blocked", {{"amount", panel.capex}});
    if (plot.mine) {
        const sim::MinePublicView& mine = *plot.mine;
        panel.mine_index = static_cast<int>(mine.id.value());
        // Only the player's own mines are in view.mines: a rival's shows nothing.
        if (const sim::MineView* own = panel.owned ? findMine(view, mine.id) : nullptr) {
            panel.figures = mineFigures(content, *own);
        }
        panel.mine_operating = mine.state == sim::MineState::Operating;
        panel.can_toggle_mine =
            panel.owned && !view.bankrupt &&
            (mine.state == sim::MineState::Operating || mine.state == sim::MineState::Mothballed);
    }
}

void fillPanel(const Session& session, const Orders& orders, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    PlotPanel panel;
    panel.any =
        model.selected_plot >= 0 && model.selected_plot < static_cast<int>(view.plots.size());
    if (!panel.any) {
        panel.title = text(content, "ui.select_plot");
        model.panel = panel;
        return;
    }
    const sim::PlotView& plot = view.plots[static_cast<std::size_t>(model.selected_plot)];
    panel.index = static_cast<int>(plot.id.value());
    panel.title =
        std::format("Plot {} · {}", plotLabel(plot.id), content.regions[plot.region.index()].name);
    panel.area = std::format("{} ha", plot.areaHectares);
    panel.listed = plot.listing == sim::PlotListing::Listed;
    panel.owned = plot.owner == Session::kPlayer;
    panel.owner = plot.listing == sim::PlotListing::Sold ? companyLabel(view, plot.owner) : "";
    panel.ask = panel.listed ? formatMoney(plot.askPrice) : "";
    if (plot.sale) {
        panel.sale = text(content, "ui.panel_sale",
                          {{"day", std::to_string(plot.sale->day)},
                           {"paid", formatMoney(plot.sale->paid)},
                           {"over", overAskText(content, *plot.sale)}});
    }
    if (panel.listed && plot.auction) {
        panel.auction = text(content, "ui.auction_closes",
                             {{"day", std::to_string(plot.auction->closesOnDay)},
                              {"bids", std::to_string(plot.auction->bidCount)}});
    }
    panel.capex = formatMoney(plot.mineCapexQuote);
    panel.odds = oddsText(content, plot);
    const SurveyLookup found = findSurvey(view, plot.id);
    const bool anyExtractable = fillPanelSurvey(content, view, found, panel);
    fillPanelNotice(content, view, plot, found, anyExtractable, panel);
    fillPanelActions(content, view, orders.available, plot, found, panel);
    fillPanelOrders(orders, plot, panel);
    model.panel = panel;
}

// ---- market ----

std::string policySummary(const sim::Content& content, const sim::SellPolicy& policy) {
    const bool keep = policy.reserveTonnes > 0;
    const bool floor = policy.floorPrice > sim::Money::zero();
    const char* key = "ui.policy_auto";
    if (policy.enabled == 0) {
        key = "ui.policy_manual";
    } else if (keep && floor) {
        key = "ui.policy_auto_keep_floor";
    } else if (keep) {
        key = "ui.policy_auto_keep";
    } else if (floor) {
        key = "ui.policy_auto_floor";
    }
    return text(content, key,
                {{"reserve", formatTonnes(policy.reserveTonnes)},
                 {"price", formatMoney(policy.floorPrice)}});
}

// A resource's standing order as it will stand once the waiting orders are
// given: the switch shows its new setting at once (spec 007).
sim::SellPolicy policyOrdered(const sim::CompanyView& view, const Orders& orders,
                              sim::ResourceId resource) {
    sim::SellPolicy policy = view.sellPolicies[resource.index()];
    const std::optional<std::size_t> at = lastOrder<sim::SetSellPolicy>(
        orders, [resource](const sim::SetSellPolicy& order) { return order.resource == resource; });
    if (at) {
        const auto& order = std::get<sim::SetSellPolicy>((*orders.waiting)[*at].command);
        policy.enabled = order.enabled;
        policy.reserveTonnes = order.reserveTonnes;
        policy.floorPrice = order.floorPrice;
    }
    return policy;
}

OrderMark policyOrderMark(const Orders& orders, sim::ResourceId resource) {
    return markFor<sim::SetSellPolicy>(
        orders, [resource](const sim::SetSellPolicy& order) { return order.resource == resource; });
}

ResourceRow makeResourceRow(const sim::Content& content, const sim::CompanyView& view,
                            const Orders& orders, std::uint32_t r, int selected) {
    const sim::MarketView& market = view.markets[r];
    ResourceRow row;
    row.index = static_cast<int>(r);
    row.name = content.resources[r].name;
    row.price = formatMoney(market.price);
    row.price_unit = row.price + "/t";
    row.ratio = formatRatio(market.price.ratioTo(market.basePrice));
    row.ratio_warn = market.price.ratioTo(market.basePrice) < sim::Fixed::fromRatio(7, 10);
    row.base_line = text(content, "ui.market_base",
                         {{"price", formatMoney(market.basePrice)}, {"ratio", row.ratio}});
    row.market_sub = text(content, "ui.market_sub",
                          {{"demand", formatTonnes(market.baseDemandTonnesPerDay)},
                           {"supply", formatTonnes(market.recentSupplyTonnesPerDay)}});
    const std::int64_t tonnes = view.inventoryTonnes[r];
    row.stock = formatTonnes(tonnes);
    row.stock_sub = text(content, "ui.stock_sub", {{"tonnes", row.stock}});
    row.stock_value = moneyTimesTonnes(market.price, tonnes);
    row.has_stock = tonnes > 0;
    row.locked = resourceLocked(content, view, sim::ResourceId(r));
    row.locked_note = row.locked ? lockedNote(content, view, sim::ResourceId(r)) : "";
    row.selected = static_cast<int>(r) == selected;
    const sim::ResourceId resource(r);
    const sim::SellPolicy policy = policyOrdered(view, orders, resource);
    row.policy_on = policy.enabled != 0;
    row.policy = policySummary(content, policy);
    row.policy_order = policyOrderMark(orders, resource);
    row.sale_order = markFor<sim::SellOre>(
        orders, [resource](const sim::SellOre& order) { return order.resource == resource; });
    row.auto_sold = view.autoSoldTonnes[r] > 0
                        ? text(content, "ui.auto_sold",
                               {{"tonnes", formatTonnes(view.autoSoldTonnes[r])},
                                {"amount", formatMoney(view.autoSoldProceeds[r])}})
                        : text(content, "ui.auto_sold_none");
    return row;
}

void fillResources(const Session& session, const Orders& orders, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.resources.clear();
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        model.resources.push_back(
            makeResourceRow(content, view, orders, r, model.selected_resource));
    }
    model.policy_order = OrderMark{};
    if (model.selected_resource >= 0 &&
        model.selected_resource < static_cast<int>(content.resources.size())) {
        const auto r = static_cast<std::size_t>(model.selected_resource);
        const sim::ResourceId resource(static_cast<std::uint32_t>(r));
        const sim::SellPolicy policy = policyOrdered(view, orders, resource);
        model.policy_order = policyOrderMark(orders, resource);
        const sim::MarketView& market = view.markets[r];
        model.policy_enabled = policy.enabled != 0;
        model.policy_summary =
            text(content, policy.enabled != 0 ? "ui.policy_summary_on" : "ui.policy_summary_off",
                 {{"resource", content.resources[r].name},
                  {"reserve", formatTonnes(policy.reserveTonnes)},
                  {"price", formatMoney(policy.floorPrice)}});
        model.standing_title = std::format("Standing order · {}", content.resources[r].name);
        model.chart_title = std::format("{} · 360 days", content.resources[r].name);
        model.chart_note = text(content, "ui.chart_note",
                                {{"demand", formatTonnes(market.baseDemandTonnesPerDay)},
                                 {"supply", formatTonnes(market.recentSupplyTonnesPerDay)}});
    }
    model.ui_policy_hint = text(content, "ui.policy_hint");
}

// ---- research ----

TechRow makeTechRow(const sim::Content& content, const sim::CompanyView& view, const Orders& orders,
                    std::uint32_t i, const std::string& busyUntil) {
    const sim::TechDef& tech = content.techs[i];
    TechRow row;
    row.index = static_cast<int>(i);
    row.name = tech.name;
    row.cost = formatMoney(tech.cost);
    row.days = std::to_string(tech.durationDays);
    row.effects = effectsText(content, tech);
    std::string needs;
    for (const sim::TechId p : tech.prerequisites) {
        needs += (needs.empty() ? "" : ", ") + content.techs[p.index()].name;
    }
    row.needs = needs.empty() ? "" : text(content, "ui.needs_tech", {{"tech", needs}});
    const bool ready =
        std::ranges::all_of(tech.prerequisites, [&](sim::TechId p) { return knowsTech(view, p); });
    const bool researching = view.activeResearch && view.activeResearch->tech == sim::TechId(i);
    if (knowsTech(view, sim::TechId(i))) {
        row.chip = "done";
        row.chip_class = "good";
    } else if (researching) {
        row.chip = "researching";
        row.chip_class = "accent";
        row.researching = true;
        row.until = text(content, "ui.tech_until", {{"day", busyUntil}});
    } else if (!ready) {
        row.chip = "locked";
        row.chip_class = "muted";
    } else {
        row.chip = "available";
        row.chip_class = "muted";
        if (view.activeResearch) {
            row.blocked = text(content, "ui.tech_busy", {{"day", busyUntil}});
        } else if (view.bankrupt) {
            row.blocked = text(content, "ui.tech_bankrupt");
        } else if (tech.cost > orders.available) {
            row.blocked = text(content, "ui.tech_unaffordable", {{"amount", row.cost}});
        } else {
            row.can_start = true;
        }
    }
    row.order = markFor<sim::StartResearch>(
        orders, [i](const sim::StartResearch& order) { return order.tech == sim::TechId(i); });
    if (row.order.pending) {
        // The chip stands in for Start; the order's own cost is not a reason.
        row.can_start = false;
        row.blocked.clear();
    }
    return row;
}

void fillTechs(const Session& session, const Orders& orders, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.techs.clear();
    std::string busyUntil;
    std::string busyTech;
    if (view.activeResearch) {
        const sim::ResearchView& research = *view.activeResearch;
        busyUntil = std::to_string(research.completesDay);
        busyTech = content.techs[research.tech.index()].name;
    }
    for (std::uint32_t i = 0; i < content.techs.size(); ++i) {
        model.techs.push_back(makeTechRow(content, view, orders, i, busyUntil));
    }
    model.lab_line = text(content, "ui.lab_one");
    model.lab_sub = busyUntil.empty()
                        ? text(content, "ui.lab_idle")
                        : text(content, "ui.lab_busy", {{"day", busyUntil}, {"tech", busyTech}});
}

// ---- finance ----

void fillFinance(const Session& session, const Orders& orders, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const sim::BalanceParams& b = content.balance;
    model.loans.clear();
    for (const sim::LoanView& loan : view.loans) {
        if (!loan.active) {
            continue;
        }
        LoanRow row;
        row.index = static_cast<int>(loan.id.value());
        row.name = std::format("#{}", loan.id.value() + 1);
        row.principal = formatMoney(loan.principal);
        row.remaining = formatMoney(loan.remaining);
        row.instalment = formatMoney(loan.principalPerMonth) + " + interest";
        row.months = std::to_string(loan.monthsRemaining);
        row.active = loan.active;
        row.can_repay_all = !view.bankrupt && loan.remaining <= orders.available;
        const sim::LoanId id = loan.id;
        row.repay_order = markFor<sim::RepayLoan>(
            orders, [id](const sim::RepayLoan& order) { return order.loan == id; });
        model.loans.push_back(row);
    }
    // The loan form states the remaining credit and refuses in advance what
    // the bank would refuse (spec 004).
    const sim::Money credit = std::max(view.loanCapacity, sim::Money::zero());
    model.loan_credit = text(content, "ui.loan_credit", {{"amount", formatMoney(credit)}});
    model.loan_blocked.clear();
    if (model.loans.size() >= b.maxActiveLoans) {
        model.loan_blocked =
            text(content, "ui.loan_blocked_count", {{"count", std::to_string(b.maxActiveLoans)}});
    } else if (credit < b.loanMinPrincipal) {
        model.loan_blocked =
            text(content, "ui.loan_blocked_credit",
                 {{"amount", formatMoney(credit)}, {"min", formatMoney(b.loanMinPrincipal)}});
    }
    model.can_take_loan = model.loan_blocked.empty() && !view.bankrupt;
    model.loan_rule = text(content, "ui.loan_rule", {{"ratio", formatRatio(b.loanAnnualRate)}});
    model.loan_limits = text(content, "ui.loan_limits",
                             {{"amount", formatMoney(view.loanCapacity)},
                              {"min", formatMoney(b.loanMinPrincipal)},
                              {"months", std::to_string(b.loanTermMonths)},
                              {"count", std::to_string(b.maxActiveLoans)}});
    // The switch shows its new setting at once (spec 007).
    const std::optional<std::size_t> cover =
        lastOrder<sim::SetCoverCosts>(orders, [](const sim::SetCoverCosts&) { return true; });
    model.cover_costs =
        cover ? std::get<sim::SetCoverCosts>((*orders.waiting)[*cover].command).enabled != 0
              : view.coverCosts;
    model.cover_check = text(content, "ui.cover_check");
    model.cover_explain = text(content, "ui.cover_explain");
    // What cover-costs sold at the last month end, from the log.
    sim::Money covered;
    bool found = false;
    for (const LogEntry& entry : session.log()) {
        if (entry.notice.kind == sim::NoticeKind::CoverCostsSale &&
            entry.day + sim::kDaysPerMonth >= view.day) {
            covered += entry.notice.amount;
            found = true;
        }
    }
    model.cover_last = found ? text(content, "ui.cover_last", {{"amount", formatMoney(covered)}})
                             : text(content, "ui.cover_last_none");
    model.solvency_rule =
        text(content, "ui.solvency_rule", {{"count", std::to_string(b.bankruptcyGraceMonths + 1)}});
}

bool noticeIsWarning(sim::NoticeKind kind) {
    switch (kind) {
        case sim::NoticeKind::CommandRejected:
        case sim::NoticeKind::InsolvencyWarning:
        case sim::NoticeKind::Bankrupt:
        case sim::NoticeKind::BidForfeited:
        case sim::NoticeKind::IdleMineWarning:
        case sim::NoticeKind::MineFoundNothing:
        case sim::NoticeKind::MineRunningLow:
        case sim::NoticeKind::MineLosingMoney:
            return true;
        default:
            return false;
    }
}

// The Log tab's filters (spec 004): 1 mine (own orders, mines, surveys,
// research, finance), 2 market (land coming to auction, ore sales, world
// events), 3 rivals (what another company did). Presentation only: decided
// from the notice kind and whose notice it is.
int logCategory(const sim::Notice& notice) {
    using K = sim::NoticeKind;
    const bool rival = notice.company.valid() && notice.company != Session::kPlayer;
    switch (notice.kind) {
        case K::PlotSold:
        case K::MineStarted:
        case K::Bankrupt:
            return rival ? 3 : 1;
        case K::RivalUnlockedResource:
            return 3;
        case K::PlotListed:
        case K::PlotRelisted:
        case K::PlotWithdrawn:
        case K::OreSold:
        case K::AutoSaleSummary:
        case K::CoverCostsSale:
        case K::EventStarted:
            return 2;
        case K::EventEnded:
            // The session's "Game loaded" marker borrows this kind with no event.
            return notice.event.valid() ? 2 : 1;
        default:
            return 1;
    }
}

constexpr std::size_t kLogRows = 200;

void fillLog(const Session& session, UiModel& model) {
    model.log.clear();
    model.recent.clear();
    const std::vector<LogEntry>& entries = session.log();
    // Newest first, capped for the list. The dashboard's "Recent" ignores
    // the filter.
    for (std::size_t i = 0; i < entries.size() && model.log.size() < kLogRows; ++i) {
        const LogEntry& entry = entries[entries.size() - 1 - i];
        LogRow row;
        row.day = formatDay(entry.day);
        row.text = entry.text;
        row.warning = noticeIsWarning(entry.notice.kind);
        if (i < 3) {
            model.recent.push_back(row);
        }
        if (model.log_filter == 0 || logCategory(entry.notice) == model.log_filter) {
            model.log.push_back(row);
        }
    }
    model.log_empty = model.log.empty() ? text(session.content(), "ui.log_empty") : "";
}

void fillPauseRows(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    model.pause_rows.clear();
    for (std::size_t i = 0; i < kPauseOnCount; ++i) {
        const auto reason = static_cast<PauseOn>(i);
        PauseRow row;
        row.index = static_cast<int>(i);
        row.label = text(content, "ui.pause_on." + std::string(pauseOnKey(reason)));
        row.on = session.autoPause().isEnabled(reason);
        model.pause_rows.push_back(row);
    }
}

}  // namespace

void refreshModel(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    // The header truncates the company name in code: RCSS has no ellipsis.
    model.company = view.companies[Session::kPlayer.index()].name;
    if (model.company.size() > kCompanyNameMax) {
        // Cut on a UTF-8 character boundary, never inside a sequence.
        std::size_t cut = kCompanyNameMax - 1;
        while (cut > 0 && (static_cast<unsigned char>(model.company[cut]) & 0xC0U) == 0x80U) {
            --cut;
        }
        model.company = model.company.substr(0, cut) + "…";
    }
    model.day = formatDay(session.day());
    model.cash = formatMoney(view.cash);
    model.debt = formatMoney(view.outstandingPrincipal);
    model.speed_label = speedLabel(session.speed());
    model.speed = static_cast<int>(session.speed());
    model.game_over = session.gameOver();
    model.ui_shortcuts = text(content, "ui.shortcuts");
    model.ui_game_over = text(content, "ui.game_over", {{"company", model.company}});
    model.ui_pause_menu_title = text(content, "ui.pause_menu_title");
    const Orders orders = readOrders(session);
    fillPending(session, orders, model);
    fillStrip(session, model);
    fillNeeds(session, orders, model);
    fillOwned(session, orders, model);
    fillFinanceRow(session, model);
    fillSale(session, model);
    fillPanel(session, orders, model);
    fillResources(session, orders, model);
    fillTechs(session, orders, model);
    fillFinance(session, orders, model);
    fillLog(session, model);
    fillPauseRows(session, model);
}

}  // namespace game
