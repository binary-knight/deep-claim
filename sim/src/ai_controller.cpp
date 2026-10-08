#include "sim/ai_controller.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/tech.h"
#include "sim/types.h"
#include "sim/view.h"

#include <algorithm>
#include <cstddef>
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
const Fixed kBidPremiumHigh = Fixed::fromRatio(5, 4);
const Fixed kRequiredReturn = Fixed::fromRatio(6, 5);  // net value must cover outlay x1.2
const Fixed kReopenMargin = Fixed::fromRatio(5, 4);    // reopen a mine at 125% of opex
constexpr std::int64_t kOpexReserveDays = 60;          // two months of opex in hand when building
constexpr std::int64_t kPaybackHorizonDays = 1800;     // value at most five years of output
constexpr Day kIdleMineGraceDays = 30;
constexpr std::size_t kMaxOpenProspects = 2;  // surveyed-but-unbought listed plots
const Money kCashReserve = Money::fromUnits(500'000);
const Money kResearchFloor = Money::fromUnits(2'500'000);
const Money kLoanFloor = Money::fromUnits(750'000);
const Money kLoanChunk = Money::fromUnits(2'000'000);
const Money kBootstrapLoan = Money::fromUnits(4'000'000);

// One projected cash position shared by every rule, so the commands issued
// on a day are jointly affordable and no rule can starve another.
struct Budget {
    Money cash;    // projected spendable cash: actual minus reservations, commitments, reserve
    Money actual;  // projected bank balance, for the cash-bound checks the sim applies

    [[nodiscard]] bool canSpend(Money amount) const { return amount <= cash; }
    void spend(Money amount) {
        cash -= amount;
        actual -= amount;
    }
    void commit(Money amount) { cash -= amount; }  // earmarked, not paid yet
    void receive(Money amount) {
        // Stay inside the persisted cash bound.
        cash = Money::fromCents(std::min(cash.cents() + amount.cents(), kMaxCashCents));
        actual = Money::fromCents(std::min(actual.cents() + amount.cents(), kMaxCashCents));
    }
    [[nodiscard]] std::int64_t headroomCents() const { return kMaxCashCents - actual.cents(); }
};

std::uint32_t activeLoanCount(const CompanyView& view) {
    return static_cast<std::uint32_t>(std::count_if(view.loans.begin(), view.loans.end(),
                                                    [](const LoanView& l) { return l.active; }));
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

// A region is worth prospecting only if some resource it may hold is unlocked.
bool regionWorthProspecting(const Content& content, const CompanyView& view, RegionId region) {
    return std::ranges::any_of(
        content.regions[region.index()].deposits, [&](const DepositPrior& prior) {
            return prior.presenceProb > Fixed::zero() &&
                   resourceUnlocked(content, view.completedTechs, prior.resource);
        });
}

// Forward declaration: net value of a surveyed plot (defined below).
Money surveyedNetValue(const Content& content, const CompanyView& view, const SurveyView& survey);

// Prospects still in play: pending surveys on listed plots, plus completed
// surveys on listed plots that are worth bidding on. Dead prospects (empty
// or uneconomic plots that keep relisting) do not count, so they can never
// block further exploration.
std::size_t openProspects(const Content& content, const CompanyView& view) {
    std::size_t count = 0;
    for (const SurveyView& survey : view.surveys) {
        const PlotView& plot = view.plots[survey.plot.index()];
        if (plot.listing != PlotListing::Listed || plot.owner == view.me) {
            continue;
        }
        if (!survey.complete) {
            ++count;
            continue;
        }
        const Money outlay = plot.askPrice + plot.mineCapexQuote;
        if (surveyedNetValue(content, view, survey) >= outlay.scaled(kRequiredReturn)) {
            ++count;
        }
    }
    return count;
}

// Net value of developing a surveyed plot: unlocked resources only, half the
// estimated tonnage (survey error haircut), at most five years of output,
// at today's prices, minus the opex of running the mine for as long as the
// slowest deposit takes to extract (output declines, so 1.3x the nominal
// time). A buy must cover land plus capex out of this.
Money surveyedNetValue(const Content& content, const CompanyView& view, const SurveyView& survey) {
    Money revenue = Money::zero();
    std::int64_t extractionDays = 0;
    for (const SurveyEstimateView& estimate : survey.estimates) {
        if (!resourceUnlocked(content, view.completedTechs, estimate.resource)) {
            continue;
        }
        const ResourceDef& def = content.resources[estimate.resource.index()];
        const std::int64_t output = std::max<std::int64_t>(def.baseOutputTonnesPerDay, 1);
        const std::int64_t horizonTonnes =
            checkedMulDiv(output, kPaybackHorizonDays, 1).value_or(kMaxDepositTonnes);
        const std::int64_t tonnes =
            std::min(checkedMulDiv(estimate.tonnesEstimate, kRecoverableShare.raw(), Fixed::kScale)
                         .value_or(kMaxDepositTonnes),
                     horizonTonnes);
        const Money price = view.markets[estimate.resource.index()].price;
        const std::int64_t cents = checkedMulDiv(price.cents(), tonnes, 1).value_or(kMaxCashCents);
        revenue += Money::fromCents(std::min(cents, kMaxCashCents - revenue.cents()));
        const std::int64_t days = (tonnes + output - 1) / output;
        extractionDays = std::max(extractionDays, days * 13 / 10);
    }
    const std::int64_t opexCents =
        checkedMulDiv(view.mineOpexQuote.cents(), extractionDays, 1).value_or(kMaxCashCents);
    return revenue - Money::fromCents(std::min(opexCents, kMaxCashCents));
}

Money constructionReserve(const CompanyView& view) {
    return view.mineOpexQuote * kOpexReserveDays;
}

// Cash the company could raise with one more loan right now.
Money borrowingRoom(const Content& content, const CompanyView& view) {
    if (activeLoanCount(view) >= content.balance.maxActiveLoans ||
        view.loanCapacity < content.balance.loanMinPrincipal) {
        return Money::zero();
    }
    return std::min(view.loanCapacity, kBootstrapLoan);
}

// Sell stock when the price is good or holding it costs too much.
void decideSales(const Content& content, const CompanyView& view, Budget& budget,
                 std::vector<Command>& out) {
    const Fixed storageMult =
        techMultiplier(content, view.completedTechs, TechEffectType::StorageCostMult);
    for (std::uint32_t r = 0; r < view.inventoryTonnes.size(); ++r) {
        std::int64_t tonnes = view.inventoryTonnes[r];
        if (tonnes <= 0) {
            continue;
        }
        const MarketView& market = view.markets[r];
        const Money price = market.price;
        const std::int64_t valueCents =
            checkedMulDiv(price.cents(), tonnes, 1).value_or(kMaxCashCents);
        const std::int64_t storageCents =
            checkedMulDiv(content.resources[r].storageCostPerTonneMonth.cents(), tonnes, 1)
                .value_or(kMaxCashCents);
        const Money storage =
            Money::fromCents(std::min(storageCents, kMaxCashCents)).scaled(storageMult);
        const bool goodPrice = price >= market.basePrice.scaled(kSellAbove);
        const bool costlyToHold = storage > Money::fromCents(valueCents).scaled(kDumpAbove);
        if (!goodPrice && !costlyToHold) {
            continue;
        }
        // Never sell more than the cash bound can absorb.
        if (price > Money::zero()) {
            tonnes = std::min(tonnes, budget.headroomCents() / price.cents());
        }
        if (tonnes <= 0) {
            continue;
        }
        out.emplace_back(SellOre{view.me, ResourceId(r), tonnes});
        budget.receive(price * tonnes);
    }
}

// Value of what a mine produced on its most recent operating day, at today's
// prices: the AI's estimate of tomorrow's revenue.
Money dailyOutputValue(const CompanyView& view, const MineView& mine) {
    Money total = Money::zero();
    for (std::uint32_t r = 0; r < mine.lastOutputTonnes.size(); ++r) {
        const std::int64_t cents =
            checkedMulDiv(view.markets[r].price.cents(), mine.lastOutputTonnes[r], 1)
                .value_or(kMaxCashCents);
        total += Money::fromCents(std::min(cents, kMaxCashCents - total.cents()));
    }
    return total;
}

// Mothball mines that lose money on a day's output (after a grace period so a
// new mine gets to produce); reopen when the output would clear opex with a
// margin, so prices must recover before the mine restarts.
void decideMothballing(const CompanyView& view, std::vector<Command>& out) {
    for (const MineView& mine : view.mines) {
        if (mine.state != MineState::Operating && mine.state != MineState::Mothballed) {
            continue;
        }
        if (view.day <= mine.operationalDay + kIdleMineGraceDays) {
            continue;
        }
        const Money revenue = dailyOutputValue(view, mine);
        const Money opex = view.mineOpexQuote;
        if (mine.state == MineState::Operating && revenue < opex) {
            out.emplace_back(SetMineState{view.me, mine.id, 0});
        } else if (mine.state == MineState::Mothballed && revenue >= opex.scaled(kReopenMargin)) {
            out.emplace_back(SetMineState{view.me, mine.id, 1});
        }
    }
}

// Survey owned, mine-less plots, and build where the survey justifies the capex.
void decideBuilding(const Content& content, const CompanyView& view, Budget& budget,
                    std::vector<Command>& out) {
    for (const PlotView& plot : view.plots) {
        if (plot.owner != view.me || plot.mine.has_value()) {
            continue;
        }
        const SurveyView* survey = latestSurvey(view, plot.id);
        if (survey == nullptr) {
            if (!surveyPending(view, plot.id) && budget.canSpend(content.balance.surveyCost)) {
                out.emplace_back(CommissionSurvey{view.me, plot.id});
                budget.spend(content.balance.surveyCost);
            }
            continue;
        }
        const Money capex = plot.mineCapexQuote;
        if (surveyedNetValue(content, view, *survey) >= capex.scaled(kRequiredReturn) &&
            budget.canSpend(capex + constructionReserve(view))) {
            out.emplace_back(BuildMine{view.me, plot.id});
            budget.spend(capex);
            budget.commit(constructionReserve(view));
        }
    }
}

// Survey listed plots there is time to evaluate, then bid where the land plus
// a mine would still be affordable and worthwhile. Returns true when a
// worthwhile plot was found that only a loan would make affordable.
bool decidePlots(const Content& content, const CompanyView& view, Budget& budget,
                 std::vector<Command>& out) {
    bool wantsLoan = false;
    const Money room = borrowingRoom(content, view);
    bool surveyedToday = openProspects(content, view) >= kMaxOpenProspects;
    for (const PlotView& plot : view.plots) {
        if (plot.listing != PlotListing::Listed || !plot.auction.has_value() ||
            plot.auction->myBid > Money::zero() ||
            !regionWorthProspecting(content, view, plot.region)) {
            continue;
        }
        const Money capex = plot.mineCapexQuote;
        const Money fullCost = plot.askPrice + capex + constructionReserve(view);
        const SurveyView* survey = latestSurvey(view, plot.id);
        if (survey == nullptr) {
            // One new survey a day, only for plots the company could develop
            // (with a loan if need be), and only if the result arrives before
            // the auction closes.
            const bool timeLeft =
                plot.auction->closesOnDay > view.day + content.balance.surveyDurationDays;
            if (!surveyedToday && timeLeft && !surveyPending(view, plot.id) &&
                budget.canSpend(content.balance.surveyCost) &&
                content.balance.surveyCost + fullCost <= budget.cash + room) {
                out.emplace_back(CommissionSurvey{view.me, plot.id});
                budget.spend(content.balance.surveyCost);
                surveyedToday = true;
            }
            continue;
        }
        const Money value = surveyedNetValue(content, view, *survey);
        if (value < (plot.askPrice + capex).scaled(kRequiredReturn)) {
            continue;  // not worth the land plus the mine
        }
        // Bids are sealed and ties go to the earlier bidder, so pay a premium
        // for clearly valuable plots, plus a small company-specific offset so
        // identical competitors do not tie on every auction.
        Fixed premium = Fixed::one();
        if (value >= (plot.askPrice + capex) * 4) {
            premium = kBidPremiumHigh;
        } else if (plot.auction->bidCount > 0 || value >= (plot.askPrice + capex) * 2) {
            premium = kBidPremium;
        }
        const auto jitterPercent =
            static_cast<std::int64_t>((plot.id.value() * 7U + view.me.value() * 13U) % 7U);
        premium = premium + Fixed::fromRatio(jitterPercent, 100);
        const Money bid = plot.askPrice.scaled(premium);
        if (value < (bid + capex).scaled(kRequiredReturn)) {
            continue;  // the premium would eat the return
        }
        const Money development = capex + constructionReserve(view);
        if (budget.canSpend(bid + development)) {
            out.emplace_back(BidOnPlot{view.me, plot.id, bid});
            // The bid is reserved by the sim; the mine it implies is earmarked
            // here so later rules cannot spend that money.
            budget.commit(bid + development);
        } else if (bid + development <= budget.cash + room) {
            wantsLoan = true;  // worth it, but only with a loan
        }
    }
    return wantsLoan;
}

// Research the cheapest available tech, but only once the company is
// producing, and only out of net worth (cash after debt), so a loan never
// funds a lab.
void decideResearch(const Content& content, const CompanyView& view, bool producing, Budget& budget,
                    std::vector<Command>& out) {
    const Money netCash = budget.cash - view.outstandingPrincipal;
    if (!producing || view.activeResearch.has_value() || netCash < kResearchFloor) {
        return;
    }
    const auto known = [&](TechId id) {
        return std::find(view.completedTechs.begin(), view.completedTechs.end(), id) !=
               view.completedTechs.end();
    };
    std::optional<TechId> best;
    for (std::uint32_t i = 0; i < content.techs.size(); ++i) {
        const TechId id(i);
        const bool ready = !known(id) && std::ranges::all_of(content.techs[i].prerequisites, known);
        if (!ready || content.techs[i].cost + kResearchFloor / 2 > netCash) {
            continue;
        }
        if (!best || content.techs[i].cost < content.techs[best->index()].cost) {
            best = id;
        }
    }
    if (best) {
        out.emplace_back(StartResearch{view.me, *best});
        budget.spend(content.techs[best->index()].cost);
    }
}

// Borrow when producing and short of cash, when owning land whose mine the
// company cannot yet pay for, or (once, before the first mine) when a
// surveyed listing is worth more than the company can pay unaided.
void decideLoans(const Content& content, const CompanyView& view, bool producing, bool wantsLoan,
                 const Budget& budget, std::vector<Command>& out) {
    const std::uint32_t activeLoans = activeLoanCount(view);
    if (activeLoans >= content.balance.maxActiveLoans) {
        return;
    }
    const bool needsCapex = std::ranges::any_of(view.plots, [&](const PlotView& plot) {
        return plot.owner == view.me && !plot.mine.has_value() &&
               !budget.canSpend(plot.mineCapexQuote + constructionReserve(view));
    });
    const bool shortWhileProducing = producing && view.cash < kLoanFloor;
    const bool bootstrap = !producing && view.mines.empty() && activeLoans == 0 && wantsLoan;
    if (!needsCapex && !shortWhileProducing && !bootstrap) {
        return;
    }
    const Money chunk = std::min(bootstrap ? kBootstrapLoan : kLoanChunk, view.loanCapacity);
    if (chunk >= content.balance.loanMinPrincipal && chunk.cents() <= budget.headroomCents()) {
        out.emplace_back(TakeLoan{view.me, chunk});
    }
}

// Repay the largest loan outright when cash comfortably exceeds it, or pay
// down as much as possible when the company has nothing to spend on, so idle
// money never sits next to 8% debt.
void decideRepayment(const CompanyView& view, Budget& budget, std::vector<Command>& out) {
    const LoanView* largest = nullptr;
    for (const LoanView& loan : view.loans) {
        if (loan.active && (largest == nullptr || loan.remaining > largest->remaining)) {
            largest = &loan;
        }
    }
    if (largest == nullptr) {
        return;
    }
    const bool idle = view.mines.empty() && view.companies[view.me.index()].plotsOwned == 0 &&
                      view.reservedCash == Money::zero();
    const bool flush = budget.canSpend(largest->remaining * 2 + kResearchFloor);
    const Money repay = flush ? largest->remaining : std::min(largest->remaining, budget.cash);
    if ((flush || idle) && repay > Money::zero()) {
        out.emplace_back(RepayLoan{view.me, largest->id, repay});
        budget.spend(repay);
    }
}

}  // namespace

void HeuristicController::decide(const CompanyView& view, std::vector<Command>& out) {
    if (view.bankrupt) {
        return;
    }
    const bool producing = std::ranges::any_of(
        view.mines, [](const MineView& m) { return m.state == MineState::Operating; });
    // Money earmarked for mines on plots the company is currently bidding for,
    // so standing bids from earlier days keep their development funding.
    Money earmarked = Money::zero();
    for (const PlotView& plot : view.plots) {
        if (plot.auction.has_value() && plot.auction->myBid > Money::zero()) {
            earmarked += plot.mineCapexQuote + constructionReserve(view);
        }
    }
    Budget budget{view.cash - view.reservedCash - earmarked - kCashReserve, view.cash};
    decideSales(*content_, view, budget, out);
    decideMothballing(view, out);
    decideBuilding(*content_, view, budget, out);
    const bool wantsLoan = decidePlots(*content_, view, budget, out);
    decideResearch(*content_, view, producing, budget, out);
    if (wantsLoan) {
        decideLoans(*content_, view, producing, true, budget, out);
    } else {
        decideRepayment(view, budget, out);
        decideLoans(*content_, view, producing, false, budget, out);
    }
}

}  // namespace sim
