#include "sim/ai_controller.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/math.h"
#include "sim/money.h"
#include "sim/sell_policy.h"
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

// A company with no mine yet buys on easier terms (spec 006): until it
// produces it earns nothing, so the first purchase should not wait for a
// plot to be marked down.
Fixed requiredReturn(const Content& content, const CompanyView& view) {
    return view.mines.empty() ? content.ai.firstMineRequiredReturn : content.ai.requiredReturn;
}

Fixed recoverableShare(const Content& content, const CompanyView& view) {
    return view.mines.empty() ? content.ai.firstMineRecoverableShare : content.ai.recoverableShare;
}

// Zero to six percent added to a bid, fixed for a given plot and company, so
// identical competitors do not tie on every auction and neither wins them
// all. (Until spec 006 this was (plot * 7 + company * 13) % 7, in which the
// plot cancels out: company 1 outbid company 0 by six points every time.)
std::int64_t bidJitterPercent(PlotId plot, CompanyId company) {
    std::uint32_t mix = (plot.value() + 1U) * 2654435761U;
    mix ^= (company.value() + 1U) * 0x9E3779B1U;
    mix ^= mix >> 15U;
    mix *= 0x85EBCA6BU;
    mix ^= mix >> 13U;
    return static_cast<std::int64_t>(mix % 7U);
}

// A region is worth prospecting only if some resource it may hold is unlocked.
bool regionWorthProspecting(const Content& content, const CompanyView& view, RegionId region) {
    return std::ranges::any_of(
        content.regions[region.index()].deposits, [&](const DepositPrior& prior) {
            return prior.presenceProb > Fixed::zero() &&
                   resourceUnlocked(content, view.completedTechs, prior.resource);
        });
}

// The stage of the program the company is working on (spec 010); nothing
// once its program is complete, or when the content has no program.
const ProgramStageView* currentStage(const CompanyView& view) {
    return view.program.stage.has_value() && !view.program.complete ? &*view.program.stage
                                                                    : nullptr;
}

std::int64_t stillNeeded(const ProgramRequirementView& requirement) {
    return std::max<std::int64_t>(requirement.neededTonnes - requirement.deliveredTonnes, 0);
}

// Tonnes of a good the program still asks for: what is left of the current
// stage's need and the whole need of every stage after it.
std::int64_t programNeedTonnes(const Content& content, const CompanyView& view,
                               ResourceId resource) {
    const ProgramStageView* stage = currentStage(view);
    if (stage == nullptr) {
        return 0;
    }
    std::int64_t need = 0;
    for (const ProgramRequirementView& requirement : stage->requirements) {
        if (requirement.resource == resource) {
            need = checkedAdd(need, stillNeeded(requirement));
        }
    }
    for (std::size_t s = stage->index + 1; s < content.program.stages.size(); ++s) {
        for (const ProgramRequirement& requirement : content.program.stages[s].requirements) {
            if (requirement.resource == resource) {
                need = checkedAdd(need, requirement.tonnes);
            }
        }
    }
    return need;
}

// The program needs more of this ore than the company's running mines will
// dig before the deadline at the rate they dug it yesterday.
bool programShortOf(const Content& content, const CompanyView& view, ResourceId resource) {
    const std::int64_t need = programNeedTonnes(content, view, resource);
    if (need <= 0) {
        return false;
    }
    std::int64_t perDay = 0;
    for (const MineView& mine : view.mines) {
        if (mine.state == MineState::Operating) {
            perDay += mine.lastOutputTonnes[resource.index()];
        }
    }
    const std::int64_t daysLeft = view.deadlineDay > view.day ? view.deadlineDay - view.day : 0;
    return checkedMulDiv(perDay, daysLeft, 1).value_or(kMaxStockTonnes) < need;
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
        if (surveyedNetValue(content, view, survey) >=
            outlay.scaled(requiredReturn(content, view))) {
            ++count;
        }
    }
    return count;
}

// The price per tonne the company should expect for a new mine's ore
// (spec 006). Never above base: a spike is not a business plan. Then the
// market's own rule is applied to the mine's own output: adding `added`
// tonnes a day to a market of `before` moves the price by
// (before / (before + added)) ^ elasticity, and that lower price is also
// paid for everything the company already sells there. A company with many
// mines of an ore therefore values one more of them far below a newcomer.
Money valuedPrice(const Content& content, const CompanyView& view, ResourceId resource) {
    const ResourceDef& def = content.resources[resource.index()];
    const MarketView& market = view.markets[resource.index()];
    const Money today = std::min(market.price, market.basePrice);
    const std::int64_t added = std::max<std::int64_t>(def.baseOutputTonnesPerDay, 1);
    const std::int64_t before =
        market.baseDemandTonnesPerDay + std::max<std::int64_t>(market.recentSupplyTonnesPerDay, 0);
    if (before <= 0 || today <= Money::zero()) {
        return std::max(today, Money::zero());
    }
    // Clamped at the market's own floor (balance.marketRatioMin), so the
    // power stays tame and a tuned clamp cannot diverge from the valuation.
    const Fixed ratio = clampFixed(Fixed::fromRatio(before, before + added),
                                   content.balance.marketRatioMin, Fixed::one());
    const Money after = today.scaled(fixedPow(ratio, def.elasticity));
    std::int64_t own = 0;
    for (const MineView& mine : view.mines) {
        if (mine.state == MineState::Operating) {
            own += mine.lastOutputTonnes[resource.index()];
        }
    }
    // What the price drop costs on existing output, per tonne of the new mine.
    const Money drop = today - after;
    const std::int64_t lostCents = checkedMulDiv(drop.cents(), own, added).value_or(today.cents());
    const Money adjustment = (drop + Money::fromCents(std::min(lostCents, today.cents())))
                                 .scaled(content.ai.supplyDiscount);
    return std::max(today - adjustment, Money::zero());
}

// Net value of developing a surveyed plot: unlocked resources only, a share
// of the estimated tonnage (survey error haircut), at most the payback
// horizon of output, at the valued price, minus the opex of running the mine
// for as long as the slowest deposit takes to extract (output declines, so
// 1.3x the nominal time). The opex is the view's quote for the dearest of
// those ores, as a mine pays for the dearest ore it produces (spec 008). A
// buy must cover land plus capex out of this. An ore the program is short of
// counts for more than it sells for (spec 010): the company cannot buy goods
// for its program, so that ore is worth the race to it.
Money surveyedNetValue(const Content& content, const CompanyView& view, const SurveyView& survey) {
    Money revenue = Money::zero();
    Money opexPerDay = Money::zero();
    std::int64_t extractionDays = 0;
    for (const SurveyEstimateView& estimate : survey.estimates) {
        if (!resourceUnlocked(content, view.completedTechs, estimate.resource)) {
            continue;
        }
        const ResourceDef& def = content.resources[estimate.resource.index()];
        opexPerDay = std::max(opexPerDay, view.mineOpexQuotes[estimate.resource.index()]);
        const std::int64_t output = std::max<std::int64_t>(def.baseOutputTonnesPerDay, 1);
        const std::int64_t horizonTonnes =
            checkedMulDiv(output, content.ai.paybackHorizonDays, 1).value_or(kMaxDepositTonnes);
        const std::int64_t tonnes =
            std::min(checkedMulDiv(estimate.tonnesEstimate, recoverableShare(content, view).raw(),
                                   Fixed::kScale)
                         .value_or(kMaxDepositTonnes),
                     horizonTonnes);
        Money price = valuedPrice(content, view, estimate.resource);
        if (programShortOf(content, view, estimate.resource)) {
            price = price.scaled(content.ai.programOreLandWeight);
        }
        const std::int64_t cents = checkedMulDiv(price.cents(), tonnes, 1).value_or(kMaxCashCents);
        revenue += Money::fromCents(std::min(cents, kMaxCashCents - revenue.cents()));
        const std::int64_t days = (tonnes + output - 1) / output;
        extractionDays = std::max(extractionDays, days * 13 / 10);
    }
    const std::int64_t opexCents =
        checkedMulDiv(opexPerDay.cents(), extractionDays, 1).value_or(kMaxCashCents);
    return revenue - Money::fromCents(std::min(opexCents, kMaxCashCents));
}

// What a new mine costs to run before it has produced anything: the sim
// charges it for the dearest ore its owner can dig (ADR-001), so that is the
// quote the company plans on when it does not yet know what a plot holds.
Money newMineOpexQuote(const Content& content, const CompanyView& view) {
    Money dearest = Money::zero();
    for (std::uint32_t r = 0; r < view.mineOpexQuotes.size(); ++r) {
        if (resourceUnlocked(content, view.completedTechs, ResourceId(r))) {
            dearest = std::max(dearest, view.mineOpexQuotes[r]);
        }
    }
    return dearest;
}

Money constructionReserve(const Content& content, const CompanyView& view) {
    return newMineOpexQuote(content, view) * content.ai.opexReserveDays;
}

// Cash the company could raise with one more loan right now.
Money borrowingRoom(const Content& content, const CompanyView& view) {
    if (activeLoanCount(view) >= content.balance.maxActiveLoans ||
        view.loanCapacity < content.balance.loanMinPrincipal) {
        return Money::zero();
    }
    return std::min(view.loanCapacity, content.ai.bootstrapLoan);
}

// Complete the stage the day the sim would accept it. Until then, once the
// stage's project is done and its goods are in, the stage's cost is the only
// thing missing: it is earmarked, so no other rule spends it. Returns true
// when the order given founds the program: that ends the game, and the sim
// refuses whatever is queued behind it.
bool decideStage(const Content& content, const CompanyView& view, Budget& budget,
                 std::vector<Command>& out) {
    const ProgramStageView* stage = currentStage(view);
    if (stage == nullptr) {
        return false;
    }
    if (stage->ready) {
        out.emplace_back(CompleteProgramStage{view.me});
        budget.spend(stage->cost);
        return stage->index + 1 >= content.program.stages.size();
    }
    const bool delivered = std::ranges::all_of(
        stage->requirements,
        [](const ProgramRequirementView& requirement) { return stillNeeded(requirement) == 0; });
    if (delivered && stage->researchState == ProgramResearchState::Done) {
        budget.commit(stage->cost);
    }
    return false;
}

// Tonnes of a good the current stage still needs; zero without a stage.
std::int64_t stageNeedTonnes(const CompanyView& view, ResourceId resource) {
    const ProgramStageView* stage = currentStage(view);
    std::int64_t need = 0;
    if (stage != nullptr) {
        for (const ProgramRequirementView& requirement : stage->requirements) {
            if (requirement.resource == resource) {
                need = checkedAdd(need, stillNeeded(requirement));
            }
        }
    }
    return need;
}

// Keep a standing sell order from selling what the stage needs. Sell
// policies run the day the ore is dug, before the company's next orders
// (tick steps 4 and 4b), so a company that has them (the player's company
// starts with them, and the runner plays it with this controller) would
// never hold stock to deliver. The order's reserve is set to what the stage
// still needs, once: it is raised only when it falls short and released when
// the need is met, so this costs two orders a good a stage.
void decideReserves(const Content& content, const CompanyView& view, std::vector<Command>& out) {
    const bool keeps = content.ai.programReserveShare > Fixed::zero();
    for (std::uint32_t r = 0; r < view.sellPolicies.size(); ++r) {
        const SellPolicy& policy = view.sellPolicies[r];
        if (policy.enabled == 0) {
            continue;
        }
        const std::int64_t need = keeps ? stageNeedTonnes(view, ResourceId(r)) : 0;
        const bool raise = need > policy.reserveTonnes;
        const bool release = need == 0 && policy.reserveTonnes > 0;
        if (raise || release) {
            out.emplace_back(SetSellPolicy{view.me, ResourceId(r), 1, need, policy.floorPrice});
        }
    }
}

// Forward declaration: the project the program wants next (defined below).
std::optional<TechId> programResearchGoal(const Content& content, const CompanyView& view);

// Cash the program's next project is waiting for: its cost while the lab is
// free and the company could start it; nothing otherwise.
Money pendingProjectCost(const Content& content, const CompanyView& view, bool producing) {
    if (!producing || view.activeResearch.has_value()) {
        return Money::zero();
    }
    const std::optional<TechId> goal = programResearchGoal(content, view);
    return goal ? content.techs[goal->index()].cost : Money::zero();
}

// Deliver to the program the reserved share of the stock its current stage
// still needs, and return the tonnes delivered per resource so the selling
// rule offers only the rest. The delivery is placed before the day's SellOre
// orders and is applied first; for an AI company, which has no sell policy
// and no cover-costs rule (Simulation's makeCompany), those orders are the
// only way stock leaves. Delivered as it arrives, at most one order a good a
// day: stock held back would pay storage for nothing. A company with less
// spendable cash than its buffer sells instead, since a delivery earns
// nothing; so does one that cannot yet pay for the project the program wants
// next, or it would sit at its buffer and never start it.
std::vector<std::int64_t> decideDeliveries(const Content& content, const CompanyView& view,
                                           bool producing, const Budget& budget,
                                           std::vector<Command>& out) {
    std::vector<std::int64_t> delivered(view.inventoryTonnes.size(), 0);
    const ProgramStageView* stage = currentStage(view);
    const Money floor = content.ai.programCashBuffer + pendingProjectCost(content, view, producing);
    if (stage == nullptr || budget.cash < floor) {
        return delivered;
    }
    for (const ProgramRequirementView& requirement : stage->requirements) {
        const std::size_t r = requirement.resource.index();
        const std::int64_t stock =
            std::min(requirement.stockTonnes, view.inventoryTonnes[r]) - delivered[r];
        // The share is taken of the stock and then cut to the need, so the
        // last tonnes a stage needs are not rounded away.
        const std::int64_t share =
            checkedMulDiv(stock, content.ai.programReserveShare.raw(), Fixed::kScale).value_or(0);
        const std::int64_t tonnes = std::min(share, stillNeeded(requirement));
        if (tonnes <= 0) {
            continue;
        }
        out.emplace_back(DeliverToProgram{view.me, requirement.resource, tonnes});
        delivered[r] += tonnes;
    }
    return delivered;
}

// Sell stock when the price is good or holding it costs too much. `kept` is
// what today's deliveries to the program take, per resource.
void decideSales(const Content& content, const CompanyView& view,
                 const std::vector<std::int64_t>& kept, Budget& budget, std::vector<Command>& out) {
    const Fixed storageMult =
        techMultiplier(content, view.completedTechs, TechEffectType::StorageCostMult);
    for (std::uint32_t r = 0; r < view.inventoryTonnes.size(); ++r) {
        std::int64_t tonnes = view.inventoryTonnes[r] - kept[r];
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
        const bool goodPrice = price >= market.basePrice.scaled(content.ai.sellAboveBase);
        const bool costlyToHold =
            storage > Money::fromCents(valueCents).scaled(content.ai.dumpStorageShare);
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

// Mothball mines that lose money on a day's output (after a grace period so a
// new mine gets to produce); reopen when the output would clear the cost of
// running with a margin, so prices must recover before the mine restarts.
// What a mine earns is the sim's figure on the view (R-001): the AI weighs
// it and never works it out. A running mine with no earnings to show has
// produced nothing and only costs money, so it is idled too.
void decideMothballing(const Content& content, const CompanyView& view, std::vector<Command>& out) {
    for (const MineView& mine : view.mines) {
        if (mine.state != MineState::Operating && mine.state != MineState::Mothballed) {
            continue;
        }
        if (view.day <= mine.operationalDay + content.ai.idleMineGraceDays) {
            continue;
        }
        if (mine.state == MineState::Operating) {
            if (!mine.hasEarnings || mine.dailyMargin < Money::zero()) {
                out.emplace_back(SetMineState{view.me, mine.id, 0});
            }
            continue;
        }
        // Revenue of reopenMargin times the running cost is a margin of that
        // less the cost itself.
        const Money needed =
            mine.operatingOpex.scaled(content.ai.reopenMargin) - mine.operatingOpex;
        if (mine.hasEarnings && mine.dailyMargin >= needed) {
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
        if (surveyedNetValue(content, view, *survey) >=
                capex.scaled(requiredReturn(content, view)) &&
            budget.canSpend(capex + constructionReserve(content, view))) {
            out.emplace_back(BuildMine{view.me, plot.id});
            budget.spend(capex);
            budget.commit(constructionReserve(content, view));
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
    bool surveyedToday = openProspects(content, view) >= content.ai.maxOpenProspects;
    for (const PlotView& plot : view.plots) {
        if (plot.listing != PlotListing::Listed || !plot.auction.has_value() ||
            plot.auction->myBid > Money::zero() ||
            !regionWorthProspecting(content, view, plot.region)) {
            continue;
        }
        const Money capex = plot.mineCapexQuote;
        const Money fullCost = plot.askPrice + capex + constructionReserve(content, view);
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
        if (value < (plot.askPrice + capex).scaled(requiredReturn(content, view))) {
            continue;  // not worth the land plus the mine
        }
        // Bids are sealed and ties go to the earlier bidder, so pay a premium
        // for clearly valuable plots, plus a small company-specific offset so
        // identical competitors do not tie on every auction.
        Fixed premium = Fixed::one();
        if (value >= (plot.askPrice + capex) * 4) {
            premium = content.ai.bidPremiumHigh;
        } else if (plot.auction->bidCount > 0 || value >= (plot.askPrice + capex) * 2) {
            premium = content.ai.bidPremium;
        }
        premium = premium + Fixed::fromRatio(bidJitterPercent(plot.id, view.me), 100);
        const Money bid = plot.askPrice.scaled(premium);
        if (value < (bid + capex).scaled(requiredReturn(content, view))) {
            continue;  // the premium would eat the return
        }
        const Money development = capex + constructionReserve(content, view);
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

bool techKnown(const CompanyView& view, TechId id) {
    return std::find(view.completedTechs.begin(), view.completedTechs.end(), id) !=
           view.completedTechs.end();
}

// A project the sim would let the company start, money aside: the sim's own
// answer, read from the view (sim::researchOpenTo).
bool techStartable(const CompanyView& view, TechId id) {
    return view.researchOpen[id.index()] != 0;
}

// The cheapest project that can be started now on the way to `target`: the
// target itself or a prerequisite still missing, however far back. Nothing
// when the target is done or nothing on the way can start yet.
std::optional<TechId> nextStepToward(const Content& content, const CompanyView& view,
                                     TechId target) {
    std::vector<std::uint8_t> seen(content.techs.size(), 0);
    std::vector<TechId> open{target};
    std::optional<TechId> best;
    while (!open.empty()) {
        const TechId id = open.back();
        open.pop_back();
        if (seen[id.index()] != 0 || techKnown(view, id)) {
            continue;
        }
        seen[id.index()] = 1;
        const TechDef& def = content.techs[id.index()];
        if (!techStartable(view, id)) {
            open.insert(open.end(), def.prerequisites.begin(), def.prerequisites.end());
            continue;
        }
        const Money bestCost = best ? content.techs[best->index()].cost : def.cost;
        if (!best || def.cost < bestCost || (def.cost == bestCost && id < *best)) {
            best = id;
        }
    }
    return best;
}

// The next project toward the first ore, among those the stages in
// [first, end) need, that the company cannot dig yet.
std::optional<TechId> stepToLockedNeed(const Content& content, const CompanyView& view,
                                       std::size_t first, std::size_t end) {
    for (std::size_t s = first; s < std::min(end, content.program.stages.size()); ++s) {
        for (const ProgramRequirement& requirement : content.program.stages[s].requirements) {
            if (resourceUnlocked(content, view.completedTechs, requirement.resource)) {
                continue;
            }
            const TechId unlock = content.resources[requirement.resource.index()].unlockedBy;
            if (const std::optional<TechId> step = nextStepToward(content, view, unlock)) {
                return step;
            }
        }
    }
    return std::nullopt;
}

// The project the program wants next, ahead of any ordinary one (spec 010).
// First whatever unlocks an ore the current stage needs, as the stage cannot
// be finished without it. Then the stage's own project, once enough of its
// goods are in that the project will not be finished long before them. Then
// whatever unlocks an ore a coming stage needs: those roads are long (four
// projects to lithium in the shipped tree), so they are walked from the start.
std::optional<TechId> programResearchGoal(const Content& content, const CompanyView& view) {
    const ProgramStageView* stage = currentStage(view);
    if (stage == nullptr) {
        return std::nullopt;
    }
    if (const auto step = stepToLockedNeed(content, view, stage->index, stage->index + 1)) {
        return step;
    }
    const bool farEnough =
        Fixed::fromRatio(stage->progressPermille, 1000) >= content.ai.programResearchProgress;
    if (farEnough && techStartable(view, stage->research)) {
        return stage->research;
    }
    return stepToLockedNeed(content, view, stage->index + 1, content.program.stages.size());
}

// The cheapest project the ordinary rule can afford: paid out of net worth
// (cash after debt), so a loan never funds a lab.
std::optional<TechId> ordinaryResearch(const Content& content, const CompanyView& view,
                                       const Budget& budget) {
    const Money netCash = budget.cash - view.outstandingPrincipal;
    std::optional<TechId> best;
    if (netCash < content.ai.researchFloor) {
        return best;
    }
    for (std::uint32_t i = 0; i < content.techs.size(); ++i) {
        const TechId id(i);
        if (!techStartable(view, id) ||
            content.techs[i].cost + content.ai.researchFloor / 2 > netCash) {
            continue;
        }
        if (!best || content.techs[i].cost < content.techs[best->index()].cost) {
            best = id;
        }
    }
    return best;
}

// Research only once the company is producing. A project the program wants
// comes first and is paid for out of spendable cash, debt or no debt, as
// long as the cash buffer is left; while it cannot be afforded nothing else
// is started, so the money is not spent from under it. Otherwise the
// cheapest available project, by the ordinary rule.
void decideResearch(const Content& content, const CompanyView& view, bool producing, Budget& budget,
                    std::vector<Command>& out) {
    if (!producing || view.activeResearch.has_value()) {
        return;
    }
    std::optional<TechId> chosen;
    if (const std::optional<TechId> goal = programResearchGoal(content, view)) {
        if (budget.canSpend(content.techs[goal->index()].cost + content.ai.programCashBuffer)) {
            chosen = goal;
        }
    } else {
        chosen = ordinaryResearch(content, view, budget);
    }
    if (chosen) {
        out.emplace_back(StartResearch{view.me, *chosen});
        budget.spend(content.techs[chosen->index()].cost);
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
               !budget.canSpend(plot.mineCapexQuote + constructionReserve(content, view));
    });
    const bool shortWhileProducing = producing && view.cash < content.ai.loanFloor;
    const bool bootstrap = !producing && view.mines.empty() && activeLoans == 0 && wantsLoan;
    if (!needsCapex && !shortWhileProducing && !bootstrap) {
        return;
    }
    const Money chunk =
        std::min(bootstrap ? content.ai.bootstrapLoan : content.ai.loanChunk, view.loanCapacity);
    if (chunk >= content.balance.loanMinPrincipal && chunk.cents() <= budget.headroomCents()) {
        out.emplace_back(TakeLoan{view.me, chunk});
    }
}

// Repay the largest loan outright when cash comfortably exceeds it, or pay
// down as much as possible when the company has nothing to spend on, so idle
// money never sits next to 8% debt.
void decideRepayment(const Content& content, const CompanyView& view, Budget& budget,
                     std::vector<Command>& out) {
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
    const bool flush = budget.canSpend(largest->remaining * 2 + content.ai.researchFloor);
    const Money repay = flush ? largest->remaining : std::min(largest->remaining, budget.cash);
    if ((flush || idle) && repay > Money::zero()) {
        out.emplace_back(RepayLoan{view.me, largest->id, repay});
        budget.spend(repay);
    }
}

}  // namespace

Money aiValuedPrice(const Content& content, const CompanyView& view, ResourceId resource) {
    return valuedPrice(content, view, resource);
}

std::int64_t aiBidJitterPercent(PlotId plot, CompanyId company) {
    return bidJitterPercent(plot, company);
}

void HeuristicController::decide(const CompanyView& view, std::vector<Command>& out) {
    // A bankrupt company gives no orders, and a finished game takes none
    // (spec 010): the sim would refuse every one.
    if (view.bankrupt || view.outcome.ended) {
        return;
    }
    const bool producing = std::ranges::any_of(
        view.mines, [](const MineView& m) { return m.state == MineState::Operating; });
    // Money earmarked for mines on plots the company is currently bidding for,
    // so standing bids from earlier days keep their development funding.
    Money earmarked = Money::zero();
    for (const PlotView& plot : view.plots) {
        if (plot.auction.has_value() && plot.auction->myBid > Money::zero()) {
            earmarked += plot.mineCapexQuote + constructionReserve(*content_, view);
        }
    }
    Budget budget{view.cash - view.reservedCash - earmarked - content_->ai.cashReserve, view.cash};
    // The program's orders are placed first: the stage is completed before
    // any other rule can spend its cost, and stock is delivered before the
    // selling rule can offer it.
    if (decideStage(*content_, view, budget, out)) {
        return;
    }
    decideReserves(*content_, view, out);
    const std::vector<std::int64_t> delivered =
        decideDeliveries(*content_, view, producing, budget, out);
    decideSales(*content_, view, delivered, budget, out);
    decideMothballing(*content_, view, out);
    decideBuilding(*content_, view, budget, out);
    const bool wantsLoan = decidePlots(*content_, view, budget, out);
    decideResearch(*content_, view, producing, budget, out);
    if (wantsLoan) {
        decideLoans(*content_, view, producing, true, budget, out);
    } else {
        decideRepayment(*content_, view, budget, out);
        decideLoans(*content_, view, producing, false, budget, out);
    }
}

}  // namespace sim
