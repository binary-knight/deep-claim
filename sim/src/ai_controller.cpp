#include "sim/ai_controller.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/view.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace sim {

namespace {

// Tuning knobs for the heuristic competitor. These shape one opponent's
// personality rather than the economy, so they live in code for now.
const Fixed kSellAbove = Fixed::fromRatio(85, 100);      // sell when price >= 85% of base
const Fixed kDumpAbove = Fixed::fromRatio(2, 100);       // ...or storage > 2% of stock value
const Fixed kRecoverableShare = Fixed::fromRatio(1, 2);  // haircut on surveyed tonnage
const Fixed kBidPremium = Fixed::fromRatio(11, 10);
constexpr std::int64_t kOpexReserveDays = 180;
const Money kCashReserve = Money::fromUnits(500'000);
const Money kResearchFloor = Money::fromUnits(2'500'000);
const Money kLoanFloor = Money::fromUnits(750'000);
const Money kLoanChunk = Money::fromUnits(2'000'000);

Money available(const CompanyView& view) {
    return view.cash - view.reservedCash;
}

// Latest complete survey this company holds for a plot.
const SurveyView* latestSurvey(const CompanyView& view, PlotId plot) {
    const SurveyView* best = nullptr;
    for (const SurveyView& survey : view.surveys) {
        if (survey.plot == plot && survey.complete) {
            best = &survey;
        }
    }
    return best;
}

bool surveyPending(const CompanyView& view, PlotId plot) {
    return std::ranges::any_of(view.surveys,
                               [&](const SurveyView& s) { return s.plot == plot && !s.complete; });
}

// Gross value of a surveyed plot at today's prices after the haircut.
Money surveyedValue(const CompanyView& view, const SurveyView& survey) {
    Money total = Money::zero();
    for (const SurveyEstimateView& estimate : survey.estimates) {
        const Money price = view.markets[estimate.resource.index()].price;
        const std::int64_t tonnes =
            checkedMulDiv(estimate.tonnesEstimate, kRecoverableShare.raw(), Fixed::kScale)
                .value_or(kMaxDepositTonnes);
        const std::int64_t cents = checkedMulDiv(price.cents(), tonnes, 1).value_or(kMaxCashCents);
        total += Money::fromCents(std::min(cents, kMaxCashCents - total.cents()));
    }
    return total;
}

Money estimatedCapex(const Content& content, const PlotView& plot) {
    return content.balance.mineCapexPerHectare * plot.areaHectares;
}

void decideSales(const CompanyView& view, std::vector<Command>& out) {
    for (std::uint32_t r = 0; r < view.inventoryTonnes.size(); ++r) {
        const std::int64_t tonnes = view.inventoryTonnes[r];
        if (tonnes <= 0) {
            continue;
        }
        const MarketView& market = view.markets[r];
        const bool goodPrice = market.price >= market.basePrice.scaled(kSellAbove);
        const std::int64_t valueCents =
            checkedMulDiv(market.price.cents(), tonnes, 1).value_or(kMaxCashCents);
        const bool costlyToHold =
            view.monthlyStorageCost > Money::fromCents(valueCents).scaled(kDumpAbove);
        if (goodPrice || costlyToHold) {
            out.emplace_back(SellOre{view.me, ResourceId(r), tonnes});
        }
    }
}

// Mothball mines whose products all trade below half base; reopen above 70%.
void decideMothballing(const CompanyView& view, std::vector<Command>& out) {
    for (const MineView& mine : view.mines) {
        if (mine.state != MineState::Operating && mine.state != MineState::Mothballed) {
            continue;
        }
        const Fixed threshold =
            mine.state == MineState::Operating ? Fixed::fromRatio(1, 2) : Fixed::fromRatio(7, 10);
        bool anyGood = false;
        bool anyProduct = false;
        for (std::uint32_t r = 0; r < mine.extractedTonnes.size(); ++r) {
            if (mine.extractedTonnes[r] == 0) {
                continue;
            }
            anyProduct = true;
            const MarketView& market = view.markets[r];
            anyGood = anyGood || market.price >= market.basePrice.scaled(threshold);
        }
        if (!anyProduct) {
            continue;
        }
        if (mine.state == MineState::Operating && !anyGood) {
            out.emplace_back(SetMineState{view.me, mine.id, 0});
        } else if (mine.state == MineState::Mothballed && anyGood) {
            out.emplace_back(SetMineState{view.me, mine.id, 1});
        }
    }
}

// Survey owned, mine-less plots, and build where the survey justifies the capex.
void decideBuilding(const Content& content, const CompanyView& view, std::vector<Command>& out) {
    Money budget = available(view) - kCashReserve;
    for (const PlotView& plot : view.plots) {
        if (plot.owner != view.me || plot.mine.has_value()) {
            continue;
        }
        const SurveyView* survey = latestSurvey(view, plot.id);
        if (survey == nullptr) {
            if (!surveyPending(view, plot.id) && budget >= content.balance.surveyCost) {
                out.emplace_back(CommissionSurvey{view.me, plot.id});
                budget -= content.balance.surveyCost;
            }
            continue;
        }
        const Money capex = estimatedCapex(content, plot);
        const Money reserve = content.balance.mineOpexPerDay * kOpexReserveDays;
        if (surveyedValue(view, *survey) >= capex * 2 && budget >= capex + reserve) {
            out.emplace_back(BuildMine{view.me, plot.id});
            budget -= capex;
        }
    }
}

void decidePlots(const Content& content, const CompanyView& view, std::vector<Command>& out) {
    Money budget = available(view) - kCashReserve;
    bool surveyedToday = false;
    for (const PlotView& plot : view.plots) {
        if (plot.listing != PlotListing::Listed || !plot.auction.has_value() ||
            plot.auction->myBid > Money::zero()) {
            continue;
        }
        const SurveyView* survey = latestSurvey(view, plot.id);
        if (survey == nullptr) {
            // Survey before bidding, one new survey a day, if the auction
            // leaves time for the result to arrive.
            const bool timeLeft =
                plot.auction->closesOnDay > view.day + content.balance.surveyDurationDays;
            if (!surveyedToday && timeLeft && !surveyPending(view, plot.id) &&
                budget >= content.balance.surveyCost) {
                out.emplace_back(CommissionSurvey{view.me, plot.id});
                budget -= content.balance.surveyCost;
                surveyedToday = true;
            }
            continue;
        }
        const Money value = surveyedValue(view, *survey);
        const Money capex = estimatedCapex(content, plot);
        if (value < plot.askPrice * 2 + capex) {
            continue;  // not worth the land plus the mine
        }
        Money bid = plot.askPrice;
        if (value >= plot.askPrice * 4 + capex) {
            bid = plot.askPrice.scaled(kBidPremium);
        }
        if (bid + capex <= budget) {
            out.emplace_back(BidOnPlot{view.me, plot.id, bid});
            budget -= bid;
        }
    }
}

void decideResearch(const Content& content, const CompanyView& view, std::vector<Command>& out) {
    if (view.activeResearch.has_value() || available(view) < kResearchFloor) {
        return;
    }
    // Cheapest tech whose prerequisites are done.
    std::optional<TechId> best;
    for (std::uint32_t i = 0; i < content.techs.size(); ++i) {
        const TechId id(i);
        const bool done = std::find(view.completedTechs.begin(), view.completedTechs.end(), id) !=
                          view.completedTechs.end();
        if (done) {
            continue;
        }
        const bool ready = std::ranges::all_of(content.techs[i].prerequisites, [&](TechId p) {
            return std::find(view.completedTechs.begin(), view.completedTechs.end(), p) !=
                   view.completedTechs.end();
        });
        if (!ready || content.techs[i].cost > available(view) - kResearchFloor / 2) {
            continue;
        }
        if (!best || content.techs[i].cost < content.techs[best->index()].cost) {
            best = id;
        }
    }
    if (best) {
        out.emplace_back(StartResearch{view.me, *best});
    }
}

void decideLoans(const Content& content, const CompanyView& view, std::vector<Command>& out) {
    // Borrow in chunks only while producing and short of cash; never just to
    // sit on money.
    const bool producing = std::ranges::any_of(
        view.mines, [](const MineView& m) { return m.state == MineState::Operating; });
    if (!producing || view.cash >= kLoanFloor ||
        view.loans.size() >= content.balance.maxActiveLoans) {
        return;
    }
    const Money chunk = std::min(kLoanChunk, view.loanCapacity);
    if (chunk >= content.balance.loanMinPrincipal) {
        out.emplace_back(TakeLoan{view.me, chunk});
    }
}

}  // namespace

void HeuristicController::decide(const CompanyView& view, std::vector<Command>& out) {
    if (view.bankrupt) {
        return;
    }
    decideSales(view, out);
    decideMothballing(view, out);
    decideBuilding(*content_, view, out);
    decidePlots(*content_, view, out);
    decideResearch(*content_, view, out);
    decideLoans(*content_, view, out);
}

}  // namespace sim
