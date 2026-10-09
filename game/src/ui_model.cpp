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
#include <optional>
#include <string>
#include <utility>
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

std::string dayLabel(sim::Day day) {
    return std::format("day {}", day);
}

sim::Day daysLeft(sim::Day target, sim::Day today) {
    return target > today ? target - today : 0;
}

std::string daysUntil(const sim::Content& content, sim::Day target, sim::Day today) {
    return text(content, "ui.days_in", {{"days", std::to_string(daysLeft(target, today))}});
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

std::string lockedNote(const sim::Content& content, sim::ResourceId resource) {
    const sim::TechId required = content.resources[resource.index()].unlockedBy;
    return required.valid()
               ? text(content, "ui.needs_tech", {{"tech", content.techs[required.index()].name}})
               : "";
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
                     std::vector<PendingNeed>& pending) {
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
        pending.push_back({0, kWarningPriority + 2, std::move(row)});
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

void fillNeeds(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    std::vector<PendingNeed> pending;
    addWarningNeeds(content, view, pending);
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
                      const sim::PlotView& plot) {
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
        return row;
    }
    row.mine = mineStateLabel(mine->state);
    row.mine_class = mineStateClass(mine->state);
    row.output = mine->state == sim::MineState::Operating ? outputText(content, *mine) : "—";
    row.remaining = remainingText(content, *mine, view.day);
    row.mine_operating = mine->state == sim::MineState::Operating;
    row.can_toggle = !view.bankrupt && (mine->state == sim::MineState::Operating ||
                                        mine->state == sim::MineState::Mothballed);
    row.mine_index = static_cast<int>(mine->id.value());
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

void fillOwned(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.owned.clear();
    for (const sim::PlotView& plot : view.plots) {
        if (plot.listing == sim::PlotListing::Sold && plot.owner == Session::kPlayer) {
            model.owned.push_back(makeOwnedRow(content, view, plot));
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
        lockedOnly = lockedOnly && resourceLocked(content, view, e.resource);
    }
    row.error = std::format("({})", formatRatio(estimates[0].relativeError));
    if (lockedOnly) {
        row.flag = "locked ore";
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
        row.needs = row.locked ? lockedNote(content, e.resource) : "";
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
void fillPanelNotice(const sim::Content& content, const sim::PlotView& plot,
                     const SurveyLookup& found, bool anyExtractable, PlotPanel& panel) {
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

void fillPanelActions(const sim::Content& content, const sim::CompanyView& view,
                      const sim::PlotView& plot, const SurveyLookup& found, PlotPanel& panel) {
    panel.can_survey = !found.pending && (!plot.owner.valid() || panel.owned) && !view.bankrupt;
    panel.survey_label =
        text(content, found.latest != nullptr ? "ui.survey_again" : "ui.survey_first",
             {{"amount", formatMoney(content.balance.surveyCost)}});
    panel.can_build = panel.owned && !plot.mine && !view.bankrupt;
    panel.can_afford_mine = plot.mineCapexQuote <= view.cash - view.reservedCash;
    panel.build_label = text(content, "ui.build_label", {{"amount", panel.capex}});
    panel.build_blocked = text(content, "ui.build_blocked", {{"amount", panel.capex}});
    if (plot.mine) {
        const sim::MinePublicView& mine = *plot.mine;
        panel.mine_index = static_cast<int>(mine.id.value());
        panel.mine_operating = mine.state == sim::MineState::Operating;
        panel.can_toggle_mine =
            panel.owned && !view.bankrupt &&
            (mine.state == sim::MineState::Operating || mine.state == sim::MineState::Mothballed);
    }
}

void fillPanel(const Session& session, UiModel& model) {
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
    if (panel.listed && plot.auction) {
        panel.auction = text(content, "ui.auction_closes",
                             {{"day", std::to_string(plot.auction->closesOnDay)},
                              {"bids", std::to_string(plot.auction->bidCount)}});
    }
    panel.capex = formatMoney(plot.mineCapexQuote);
    panel.odds = oddsText(content, plot);
    const SurveyLookup found = findSurvey(view, plot.id);
    const bool anyExtractable = fillPanelSurvey(content, view, found, panel);
    fillPanelNotice(content, plot, found, anyExtractable, panel);
    fillPanelActions(content, view, plot, found, panel);
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

ResourceRow makeResourceRow(const sim::Content& content, const sim::CompanyView& view,
                            std::uint32_t r, int selected) {
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
    row.locked_note = row.locked ? lockedNote(content, sim::ResourceId(r)) : "";
    row.selected = static_cast<int>(r) == selected;
    const sim::SellPolicy& policy = view.sellPolicies[r];
    row.policy_on = policy.enabled != 0;
    row.policy = policySummary(content, policy);
    row.auto_sold = view.autoSoldTonnes[r] > 0
                        ? text(content, "ui.auto_sold",
                               {{"tonnes", formatTonnes(view.autoSoldTonnes[r])},
                                {"amount", formatMoney(view.autoSoldProceeds[r])}})
                        : text(content, "ui.auto_sold_none");
    return row;
}

void fillResources(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.resources.clear();
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        model.resources.push_back(makeResourceRow(content, view, r, model.selected_resource));
    }
    if (model.selected_resource >= 0 &&
        model.selected_resource < static_cast<int>(content.resources.size())) {
        const auto r = static_cast<std::size_t>(model.selected_resource);
        const sim::SellPolicy& policy = view.sellPolicies[r];
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

TechRow makeTechRow(const sim::Content& content, const sim::CompanyView& view, std::uint32_t i,
                    const std::string& busyUntil) {
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
        } else if (tech.cost > view.cash - view.reservedCash) {
            row.blocked = text(content, "ui.tech_unaffordable", {{"amount", row.cost}});
        } else {
            row.can_start = true;
        }
    }
    return row;
}

void fillTechs(const Session& session, UiModel& model) {
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
        model.techs.push_back(makeTechRow(content, view, i, busyUntil));
    }
    model.lab_line = text(content, "ui.lab_one");
    model.lab_sub = busyUntil.empty()
                        ? text(content, "ui.lab_idle")
                        : text(content, "ui.lab_busy", {{"day", busyUntil}, {"tech", busyTech}});
}

// ---- finance ----

void fillFinance(const Session& session, UiModel& model) {
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
        model.loans.push_back(row);
    }
    model.loan_rule = text(content, "ui.loan_rule", {{"ratio", formatRatio(b.loanAnnualRate)}});
    model.loan_limits = text(content, "ui.loan_limits",
                             {{"amount", formatMoney(view.loanCapacity)},
                              {"min", formatMoney(b.loanMinPrincipal)},
                              {"months", std::to_string(b.loanTermMonths)},
                              {"count", std::to_string(b.maxActiveLoans)}});
    model.cover_costs = view.coverCosts;
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
            return true;
        default:
            return false;
    }
}

void fillLog(const Session& session, UiModel& model) {
    model.log.clear();
    model.recent.clear();
    const std::vector<LogEntry>& entries = session.log();
    // Newest first, capped for the list.
    const std::size_t count = std::min<std::size_t>(entries.size(), 200);
    for (std::size_t i = 0; i < count; ++i) {
        const LogEntry& entry = entries[entries.size() - 1 - i];
        LogRow row;
        row.day = formatDay(entry.day);
        row.text = entry.text;
        row.warning = noticeIsWarning(entry.notice.kind);
        if (model.recent.size() < 3) {
            model.recent.push_back(row);
        }
        model.log.push_back(row);
    }
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
    model.spendable = formatMoney(view.cash - view.reservedCash);
    model.debt = formatMoney(view.outstandingPrincipal);
    model.speed_label = speedLabel(session.speed());
    model.speed = static_cast<int>(session.speed());
    model.game_over = session.gameOver();
    model.ui_shortcuts = text(content, "ui.shortcuts");
    model.ui_game_over = text(content, "ui.game_over", {{"company", model.company}});
    model.ui_pause_menu_title = text(content, "ui.pause_menu_title");
    fillStrip(session, model);
    fillNeeds(session, model);
    fillOwned(session, model);
    fillFinanceRow(session, model);
    fillSale(session, model);
    fillPanel(session, model);
    fillResources(session, model);
    fillTechs(session, model);
    fillFinance(session, model);
    fillLog(session, model);
    fillPauseRows(session, model);
}

}  // namespace game
