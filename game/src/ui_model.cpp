#include "ui_model.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/view.h"

#include "game/format.h"
#include "game/session.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace game {

namespace {

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

std::string plotStatus(const sim::CompanyView& view, const sim::PlotView& plot) {
    if (plot.listing == sim::PlotListing::Listed) {
        return "For sale";
    }
    if (plot.listing == sim::PlotListing::Sold) {
        return companyLabel(view, plot.owner);
    }
    return "Not listed";
}

std::string auctionText(const sim::PlotView& plot) {
    if (!plot.auction) {
        return "";
    }
    std::string line = textPlural(content, "ui.auction", plot.auction->bidCount,
                                  {{"day", formatDay(plot.auction->closesOnDay)},
                                   {"bids", std::to_string(plot.auction->bidCount)}});
    if (plot.auction->myBid > sim::Money::zero()) {
        line += ", " +
                text(content, "ui.auction_yours", {{"amount", formatMoney(plot.auction->myBid)}});
    }
    return line;
}

// The player's latest completed survey of a plot plus any pending one.
std::string surveyText(const sim::Content& content, const sim::CompanyView& view,
                       const sim::PlotView& plot) {
    const sim::SurveyView* latest = nullptr;
    bool pending = false;
    for (const sim::SurveyView& survey : view.surveys) {
        if (survey.plot != plot.id) {
            continue;
        }
        if (survey.complete) {
            latest = &survey;
        } else {
            pending = true;
        }
    }
    std::string text;
    if (latest != nullptr) {
        for (const sim::SurveyEstimateView& e : latest->estimates) {
            if (!text.empty()) {
                text += ", ";
            }
            text += std::format("{} ~{}", content.resources[e.resource.index()].name,
                                formatTonnes(e.tonnesEstimate));
        }
        if (text.empty()) {
            text = game::text(content, "ui.survey_none");  // presence is certain; no error band
        } else {
            text += std::format(" (error {})", formatRatio(latest->estimates[0].relativeError));
        }
    }
    if (pending) {
        text += text.empty() ? game::text(content, "ui.survey_pending")
                             : "; " + game::text(content, "ui.survey_pending_again");
    }
    return text;
}

// Mine state, and for the player's own mines today's output and opex.
std::string mineText(const sim::Content& content, const sim::CompanyView& view,
                     const sim::PlotView& plot, bool owned) {
    if (!plot.mine) {
        return "";
    }
    std::string text = mineStateLabel(plot.mine->state);
    if (!owned) {
        return text;
    }
    for (const sim::MineView& mine : view.mines) {
        if (mine.id != plot.mine->id) {
            continue;
        }
        std::string output;
        for (std::uint32_t r = 0; r < mine.lastDayTonnes.size(); ++r) {
            if (mine.lastDayTonnes[r] > 0) {
                output +=
                    std::format("{}{} {}", output.empty() ? "" : ", ",
                                formatTonnes(mine.lastDayTonnes[r]), content.resources[r].name);
            }
        }
        if (!output.empty()) {
            text += ", " + game::text(content, "ui.mine_today", {{"output", output}});
        }
        text +=
            ", " + game::text(content, "ui.mine_opex", {{"amount", formatMoney(mine.dailyOpex)}});
    }
    return text;
}

PlotRow makePlotRow(const sim::Content& content, const sim::CompanyView& view,
                    const sim::PlotView& plot) {
    PlotRow row;
    row.index = static_cast<int>(plot.id.value());
    row.name = std::format("Plot #{}", plot.id.value() + 1);
    row.region = content.regions[plot.region.index()].name;
    row.area = std::format("{} ha", plot.areaHectares);
    row.capex = formatMoney(plot.mineCapexQuote);
    row.can_afford_mine = plot.mineCapexQuote <= view.cash - view.reservedCash;
    for (const sim::DepositPrior& prior : content.regions[plot.region.index()].deposits) {
        if (!row.odds.empty()) {
            row.odds += ", ";
        }
        const std::int64_t percent = prior.presenceProb.raw() / (sim::Fixed::kScale / 100);
        row.odds += std::format("{} {}%", content.resources[prior.resource.index()].name, percent);
    }
    row.odds_line =
        game::text(content, "ui.plot_odds", {{"region", row.region}, {"odds", row.odds}});
    row.costs_line =
        game::text(content, "ui.plot_costs",
                   {{"amount", row.capex},
                    {"days", std::to_string(content.balance.mineBuildDays)},
                    {"survey_cost", formatMoney(content.balance.surveyCost)},
                    {"survey_days", std::to_string(content.balance.surveyDurationDays)}});
    row.build_blocked = game::text(content, "ui.build_blocked", {{"amount", row.capex}});
    row.listed = plot.listing == sim::PlotListing::Listed;
    row.owned = plot.owner == Session::kPlayer;
    row.status = plotStatus(view, plot);
    row.ask = row.listed ? formatMoney(plot.askPrice) : "";
    row.auction = row.listed ? auctionText(plot) : "";
    row.survey = surveyText(content, view, plot);
    row.mine = mineText(content, view, plot, row.owned);
    row.mine_here = plot.mine.has_value();
    row.mine_operating = plot.mine && plot.mine->state == sim::MineState::Operating;
    row.mine_index = plot.mine ? static_cast<int>(plot.mine->id.value()) : -1;
    const bool pending = std::ranges::any_of(
        view.surveys, [&](const sim::SurveyView& s) { return s.plot == plot.id && !s.complete; });
    const bool switchable = plot.mine && (plot.mine->state == sim::MineState::Operating ||
                                          plot.mine->state == sim::MineState::Mothballed);
    row.can_survey = !pending && (!plot.owner.valid() || row.owned) && !view.bankrupt;
    row.can_build = row.owned && !plot.mine && !view.bankrupt;
    row.can_toggle_mine = row.owned && switchable && !view.bankrupt;
    return row;
}

void fillPlots(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.plots.clear();
    for (const sim::PlotView& plot : view.plots) {
        model.plots.push_back(makePlotRow(content, view, plot));
    }
    if (model.selected_plot >= 0 && model.selected_plot < static_cast<int>(model.plots.size())) {
        const PlotRow& row = model.plots[static_cast<std::size_t>(model.selected_plot)];
        model.selected_plot_title =
            game::text(content, "ui.selected_plot_title",
                       {{"plot", row.name}, {"region", row.region}, {"area", row.area}});
    } else {
        model.selected_plot_title = game::text(content, "ui.select_plot");
    }
}

void fillResources(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.resources.clear();
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        const sim::MarketView& market = view.markets[r];
        ResourceRow row;
        row.index = static_cast<int>(r);
        row.name = content.resources[r].name;
        row.price = formatMoney(market.price) + "/t";
        row.base = formatMoney(market.basePrice) + "/t";
        row.ratio = formatRatio(market.price.ratioTo(market.basePrice));
        row.supply = formatTonnes(market.recentSupplyTonnesPerDay) + "/day";
        const std::int64_t tonnes = view.inventoryTonnes[r];
        row.stock = formatTonnes(tonnes);
        const std::int64_t valueCents =
            sim::checkedMulDiv(market.price.cents(), tonnes, 1).value_or(sim::kMaxCashCents);
        row.stock_value =
            formatMoney(sim::Money::fromCents(std::min(valueCents, sim::kMaxCashCents)));
        row.has_stock = tonnes > 0;
        row.selected = static_cast<int>(r) == model.selected_resource;
        const sim::SellPolicy& policy = view.sellPolicies[r];
        const bool keep = policy.reserveTonnes > 0;
        const bool floor = policy.floorPrice > sim::Money::zero();
        const char* policyKey = policy.enabled == 0 ? "ui.policy_manual"
                                : keep && floor     ? "ui.policy_auto_keep_floor"
                                : keep              ? "ui.policy_auto_keep"
                                : floor             ? "ui.policy_auto_floor"
                                                    : "ui.policy_auto";
        row.policy = game::text(content, policyKey,
                                {{"reserve", formatTonnes(policy.reserveTonnes)},
                                 {"price", formatMoney(policy.floorPrice)}});
        if (view.autoSoldTonnes[r] > 0) {
            row.auto_sold = game::text(content, "ui.auto_sold",
                                       {{"tonnes", formatTonnes(view.autoSoldTonnes[r])},
                                        {"amount", formatMoney(view.autoSoldProceeds[r])}});
        }
        model.resources.push_back(row);
    }
    if (model.selected_resource >= 0 &&
        model.selected_resource < static_cast<int>(content.resources.size())) {
        const auto r = static_cast<std::size_t>(model.selected_resource);
        const sim::SellPolicy& policy = view.sellPolicies[r];
        model.policy_enabled = policy.enabled != 0;
        model.policy_summary = game::text(
            content, policy.enabled != 0 ? "ui.policy_summary_on" : "ui.policy_summary_off",
            {{"resource", content.resources[r].name},
             {"reserve", formatTonnes(policy.reserveTonnes)},
             {"price", formatMoney(policy.floorPrice)}});
    }
    model.cover_costs = view.coverCosts;
    model.cover_costs_label = game::text(content, view.coverCosts ? "ui.cover_on" : "ui.cover_off");
    if (model.selected_resource >= 0 &&
        model.selected_resource < static_cast<int>(content.resources.size())) {
        model.selected_resource_title = game::text(
            content, "ui.price_title",
            {{"resource",
              content.resources[static_cast<std::size_t>(model.selected_resource)].name}});
    }
}

void fillTechs(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.techs.clear();
    const auto known = [&](sim::TechId id) {
        return std::find(view.completedTechs.begin(), view.completedTechs.end(), id) !=
               view.completedTechs.end();
    };
    for (std::uint32_t i = 0; i < content.techs.size(); ++i) {
        const sim::TechDef& tech = content.techs[i];
        TechRow row;
        row.index = static_cast<int>(i);
        row.name = tech.name;
        row.cost = formatMoney(tech.cost);
        row.duration = std::format("{} days", tech.durationDays);
        row.effects = effectsText(content, tech);
        const bool ready = std::ranges::all_of(tech.prerequisites, known);
        if (known(sim::TechId(i))) {
            row.status = game::text(content, "ui.tech_known");
        } else if (view.activeResearch && view.activeResearch->tech == sim::TechId(i)) {
            row.status = game::text(content, "ui.tech_in_progress",
                                    {{"day", formatDay(view.activeResearch->completesDay)}});
        } else if (!ready) {
            std::string needs;
            for (const sim::TechId p : tech.prerequisites) {
                if (!known(p)) {
                    needs += (needs.empty() ? "" : ", ") + content.techs[p.index()].name;
                }
            }
            row.status = game::text(content, "ui.tech_needs", {{"tech", needs}});
        } else if (view.activeResearch) {
            row.status = game::text(content, "ui.tech_available_busy");
        } else {
            row.status = game::text(content, "ui.tech_available");
            row.can_start = !view.bankrupt && tech.cost <= view.cash - view.reservedCash;
        }
        model.techs.push_back(row);
    }
    if (view.activeResearch) {
        model.research_summary =
            game::text(content, "ui.research_active",
                       {{"tech", content.techs[view.activeResearch->tech.index()].name},
                        {"day", formatDay(view.activeResearch->completesDay)}});
    } else {
        model.research_summary = game::text(content, "ui.research_idle",
                                            {{"known", std::to_string(view.completedTechs.size())},
                                             {"total", std::to_string(content.techs.size())}});
    }
}

void fillFinance(const Session& session, UiModel& model) {
    const sim::CompanyView& view = session.view();
    model.loans.clear();
    for (const sim::LoanView& loan : view.loans) {
        LoanRow row;
        row.index = static_cast<int>(loan.id.value());
        row.remaining = formatMoney(loan.remaining);
        row.instalment = game::text(session.content(), "ui.loan_instalment",
                                    {{"amount", formatMoney(loan.principalPerMonth)}});
        row.months = game::text(session.content(), "ui.loan_months",
                                {{"months", std::to_string(loan.monthsRemaining)}});
        row.active = loan.active;
        model.loans.push_back(row);
    }
    model.loan_capacity = formatMoney(view.loanCapacity);
    model.storage_cost = formatMoney(view.monthlyStorageCost) + "/month";
    // Presentation only: the sim does not care, but the first playtest lost
    // games to storage charges nobody noticed.
    model.storage_warning.clear();
    if (view.monthlyStorageCost > sim::Money::zero() &&
        (view.cash <= sim::Money::zero() ||
         view.monthlyStorageCost.cents() > view.cash.cents() / 10)) {
        model.storage_warning = game::text(session.content(), "ui.storage_warning",
                                           {{"amount", formatMoney(view.monthlyStorageCost)}});
    }
    model.mine_opex = formatMoney(view.mineOpexQuote) + "/day";
    model.survey_noise = formatRatio(view.surveyNoiseQuote);
}

void fillLog(const Session& session, UiModel& model) {
    model.log.clear();
    const std::vector<LogEntry>& entries = session.log();
    // Newest first, capped for the list.
    const std::size_t count = std::min<std::size_t>(entries.size(), 200);
    for (std::size_t i = 0; i < count; ++i) {
        const LogEntry& entry = entries[entries.size() - 1 - i];
        LogRow row;
        row.day = formatDay(entry.day);
        row.text = entry.text;
        row.warning = entry.notice.kind == sim::NoticeKind::CommandRejected ||
                      entry.notice.kind == sim::NoticeKind::InsolvencyWarning ||
                      entry.notice.kind == sim::NoticeKind::Bankrupt ||
                      entry.notice.kind == sim::NoticeKind::BidForfeited;
        model.log.push_back(row);
    }
}

// Sentences the document shows verbatim, with the balance figures filled in.
void fillCatalogueProse(const sim::Content& content, UiModel& model) {
    const sim::BalanceParams& b = content.balance;
    model.ui_shortcuts = text(content, "ui.shortcuts");
    model.ui_how_to_1 = text(content, "ui.how_to_1");
    model.ui_how_to_2 = text(content, "ui.how_to_2",
                             {{"survey_cost", formatMoney(b.surveyCost)},
                              {"survey_days", std::to_string(b.surveyDurationDays)},
                              {"days", std::to_string(b.mineBuildDays)},
                              {"months", std::to_string(b.bankruptcyGraceMonths)}});
    model.ui_how_to_3 = text(content, "ui.how_to_3");
    model.ui_no_events = text(content, "ui.no_events");
    model.ui_plots_intro = text(content, "ui.plots_intro");
    model.ui_policy_form_keep = text(content, "ui.policy_form_keep");
    model.ui_policy_form_mid = text(content, "ui.policy_form_mid");
    model.ui_policy_form_end = text(content, "ui.policy_form_end");
    model.ui_policy_hint = text(content, "ui.policy_hint");
    model.ui_chart_hint = text(content, "ui.chart_hint");
}

}  // namespace

void refreshModel(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.company = view.companies[Session::kPlayer.index()].name;
    model.day = formatDay(session.day());
    model.cash = formatMoney(view.cash);
    model.spendable = formatMoney(view.cash - view.reservedCash);
    model.debt = formatMoney(view.outstandingPrincipal);
    model.insolvency =
        view.monthsInsolvent == 0
            ? ""
            : textPlural(content, "ui.insolvency", view.monthsInsolvent,
                         {{"months", std::to_string(view.monthsInsolvent)},
                          {"count", std::to_string(content.balance.bankruptcyGraceMonths + 1)}});
    model.survey_cost = formatMoney(content.balance.surveyCost);
    model.survey_days = std::to_string(content.balance.surveyDurationDays);
    model.mine_build_days = std::to_string(content.balance.mineBuildDays);
    model.grace_months = std::to_string(content.balance.bankruptcyGraceMonths);
    model.loan_terms = text(content, "ui.loan_terms",
                            {{"months", std::to_string(content.balance.loanTermMonths)},
                             {"ratio", formatRatio(content.balance.loanAnnualRate)},
                             {"amount", formatMoney(content.balance.loanMinPrincipal)},
                             {"count", std::to_string(content.balance.maxActiveLoans)}});
    fillCatalogueProse(content, model);
    model.speed_label = speedLabel(session.speed());
    model.speed = static_cast<int>(session.speed());
    model.game_over = session.gameOver();

    std::uint32_t operating = 0;
    for (const sim::MineView& mine : view.mines) {
        operating += mine.state == sim::MineState::Operating ? 1 : 0;
    }
    model.mines_summary = textPlural(
        content, "ui.mines_summary", static_cast<std::int64_t>(view.mines.size()),
        {{"count", std::to_string(view.mines.size())}, {"operating", std::to_string(operating)}});
    const std::uint32_t owned = view.companies[Session::kPlayer.index()].plotsOwned;
    const auto listed =
        std::count_if(view.plots.begin(), view.plots.end(),
                      [](const sim::PlotView& p) { return p.listing == sim::PlotListing::Listed; });
    model.plots_summary =
        textPlural(content, "ui.plots_summary", owned,
                   {{"count", std::to_string(owned)}, {"listed", std::to_string(listed)}});
    model.rivals.clear();
    for (std::uint32_t c = 1; c < view.companies.size(); ++c) {
        const sim::CompanyPublicView& rival = view.companies[c];
        model.rivals.push_back(rival.bankrupt
                                   ? text(content, "ui.rival_bankrupt", {{"company", rival.name}})
                                   : textPlural(content, "ui.rival", rival.plotsOwned,
                                                {{"company", rival.name},
                                                 {"count", std::to_string(rival.plotsOwned)}}));
    }
    model.events.clear();
    for (const sim::ActiveEventView& event : view.events) {
        model.events.push_back(text(content, "ui.event_active",
                                    {{"event", content.events[event.event.index()].name},
                                     {"day", formatDay(event.endsDay)}}));
    }
    fillPlots(session, model);
    fillResources(session, model);
    fillTechs(session, model);
    fillFinance(session, model);
    fillLog(session, model);
    // Sentences that quote figures computed above.
    model.ui_company_terms = text(content, "ui.company_terms",
                                  {{"amount", model.mine_opex}, {"ratio", model.survey_noise}});
    model.ui_finance_cash =
        text(content, "ui.finance_cash", {{"cash", model.cash}, {"spendable", model.spendable}});
    model.ui_finance_debt =
        text(content, "ui.finance_debt", {{"debt", model.debt}, {"amount", model.loan_capacity}});
    model.ui_finance_storage =
        text(content, "ui.finance_storage", {{"amount", model.storage_cost}});
    model.ui_borrow_line = text(content, "ui.borrow_capacity", {{"amount", model.loan_capacity}}) +
                           " " + model.loan_terms;
    model.ui_each_mine_opex = text(content, "ui.each_mine_opex", {{"amount", model.mine_opex}});
    model.ui_game_over = text(content, "ui.game_over", {{"company", model.company}});
}

}  // namespace game
