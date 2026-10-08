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

void fillPlots(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.plots.clear();
    for (const sim::PlotView& plot : view.plots) {
        PlotRow row;
        row.index = static_cast<int>(plot.id.value());
        row.name = std::format("Plot #{}", plot.id.value() + 1);
        row.region = content.regions[plot.region.index()].name;
        row.area = std::format("{} ha", plot.areaHectares);
        row.capex = formatMoney(plot.mineCapexQuote);
        row.listed = plot.listing == sim::PlotListing::Listed;
        row.owned = plot.owner == Session::kPlayer;
        if (row.listed) {
            row.status = "For sale";
            row.ask = formatMoney(plot.askPrice);
            if (plot.auction) {
                row.auction =
                    std::format("closes {}, {} bid{}", formatDay(plot.auction->closesOnDay),
                                plot.auction->bidCount, plot.auction->bidCount == 1 ? "" : "s");
                if (plot.auction->myBid > sim::Money::zero()) {
                    row.auction += ", yours " + formatMoney(plot.auction->myBid);
                }
            }
        } else if (plot.listing == sim::PlotListing::Sold) {
            row.status = companyLabel(view, plot.owner);
        } else {
            row.status = "Not listed";
        }
        // Latest own survey.
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
        if (latest != nullptr) {
            if (latest->estimates.empty()) {
                row.survey = "nothing found";
            }
            for (const sim::SurveyEstimateView& e : latest->estimates) {
                if (!row.survey.empty()) {
                    row.survey += ", ";
                }
                row.survey += std::format("{} ~{}", content.resources[e.resource.index()].name,
                                          formatTonnes(e.tonnesEstimate));
            }
            row.survey +=
                std::format(" (error {})", formatRatio(latest->estimates.empty()
                                                           ? view.surveyNoiseQuote
                                                           : latest->estimates[0].relativeError));
        } else if (pending) {
            row.survey = "survey in progress";
        }
        if (plot.mine) {
            row.mine_here = true;
            row.mine = mineStateLabel(plot.mine->state);
            row.mine_operating = plot.mine->state == sim::MineState::Operating;
            row.mine_index = static_cast<int>(plot.mine->id.value());
            if (row.owned) {
                for (const sim::MineView& mine : view.mines) {
                    if (mine.id == plot.mine->id) {
                        std::string output;
                        for (std::uint32_t r = 0; r < mine.lastDayTonnes.size(); ++r) {
                            if (mine.lastDayTonnes[r] > 0) {
                                output += std::format(" {} {}", formatTonnes(mine.lastDayTonnes[r]),
                                                      content.resources[r].name);
                            }
                        }
                        if (!output.empty()) {
                            row.mine += ", today:" + output;
                        }
                        row.mine += ", opex " + formatMoney(mine.dailyOpex) + "/day";
                    }
                }
            }
        }
        const bool unowned = !plot.owner.valid();
        row.can_survey = !pending && (unowned || row.owned) && !view.bankrupt;
        row.can_build = row.owned && !plot.mine && !view.bankrupt;
        row.can_toggle_mine = row.owned && plot.mine &&
                              (plot.mine->state == sim::MineState::Operating ||
                               plot.mine->state == sim::MineState::Mothballed) &&
                              !view.bankrupt;
        model.plots.push_back(row);
    }
    if (model.selected_plot >= 0 && model.selected_plot < static_cast<int>(model.plots.size())) {
        const PlotRow& row = model.plots[static_cast<std::size_t>(model.selected_plot)];
        model.selected_plot_title = std::format("{} ({}, {})", row.name, row.region, row.area);
    } else {
        model.selected_plot_title = "Select a plot";
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
        model.resources.push_back(row);
    }
    if (model.selected_resource >= 0 &&
        model.selected_resource < static_cast<int>(content.resources.size())) {
        model.selected_resource_title =
            content.resources[static_cast<std::size_t>(model.selected_resource)].name +
            " price, last 90 days";
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
            row.status = "Known";
        } else if (view.activeResearch && view.activeResearch->tech == sim::TechId(i)) {
            row.status =
                std::format("In progress, done {}", formatDay(view.activeResearch->completesDay));
        } else if (!ready) {
            std::string needs;
            for (const sim::TechId p : tech.prerequisites) {
                if (!known(p)) {
                    needs += (needs.empty() ? "needs " : ", ") + content.techs[p.index()].name;
                }
            }
            row.status = needs;
        } else if (view.activeResearch) {
            row.status = "Available (lab busy)";
        } else {
            row.status = "Available";
            row.can_start = !view.bankrupt && tech.cost <= view.cash - view.reservedCash;
        }
        model.techs.push_back(row);
    }
    if (view.activeResearch) {
        model.research_summary = std::format("Researching {} until {}",
                                             content.techs[view.activeResearch->tech.index()].name,
                                             formatDay(view.activeResearch->completesDay));
    } else {
        model.research_summary = std::format("{} of {} technologies known, lab idle",
                                             view.completedTechs.size(), content.techs.size());
    }
}

void fillFinance(const Session& session, UiModel& model) {
    const sim::CompanyView& view = session.view();
    model.loans.clear();
    for (const sim::LoanView& loan : view.loans) {
        LoanRow row;
        row.index = static_cast<int>(loan.id.value());
        row.remaining = formatMoney(loan.remaining);
        row.instalment = formatMoney(loan.principalPerMonth) + "/month + interest";
        row.months = std::format("{} months left", loan.monthsRemaining);
        row.active = loan.active;
        model.loans.push_back(row);
    }
    model.loan_capacity = formatMoney(view.loanCapacity);
    model.storage_cost = formatMoney(view.monthlyStorageCost) + "/month";
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

}  // namespace

void refreshModel(const Session& session, UiModel& model) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    model.company = view.companies[Session::kPlayer.index()].name;
    model.day = formatDay(session.day());
    model.cash = formatMoney(view.cash);
    model.debt = formatMoney(view.outstandingPrincipal);
    model.speed_label = speedLabel(session.speed());
    model.speed = static_cast<int>(session.speed());
    model.game_over = session.gameOver();

    std::uint32_t operating = 0;
    for (const sim::MineView& mine : view.mines) {
        operating += mine.state == sim::MineState::Operating ? 1 : 0;
    }
    model.mines_summary = std::format("{} mine{} ({} operating)", view.mines.size(),
                                      view.mines.size() == 1 ? "" : "s", operating);
    model.plots_summary = std::format(
        "{} plot{} owned, {} for sale", view.companies[Session::kPlayer.index()].plotsOwned,
        view.companies[Session::kPlayer.index()].plotsOwned == 1 ? "" : "s",
        std::count_if(view.plots.begin(), view.plots.end(), [](const sim::PlotView& p) {
            return p.listing == sim::PlotListing::Listed;
        }));
    model.rivals.clear();
    for (std::uint32_t c = 1; c < view.companies.size(); ++c) {
        const sim::CompanyPublicView& rival = view.companies[c];
        model.rivals.push_back(std::format("{}: {} plot{}{}", rival.name, rival.plotsOwned,
                                           rival.plotsOwned == 1 ? "" : "s",
                                           rival.bankrupt ? " (bankrupt)" : ""));
    }
    model.events.clear();
    for (const sim::ActiveEventView& event : view.events) {
        model.events.push_back(std::format(
            "{} (until {})", content.events[event.event.index()].name, formatDay(event.endsDay)));
    }
    fillPlots(session, model);
    fillResources(session, model);
    fillTechs(session, model);
    fillFinance(session, model);
    fillLog(session, model);
}

}  // namespace game
