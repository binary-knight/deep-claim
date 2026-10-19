// This file must never read the concealed deposit state. A test greps for it.
#include "sim/view.h"

#include "sim/content.h"
#include "sim/finance.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/mines.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/program.h"
#include "sim/research.h"
#include "sim/surveys.h"
#include "sim/tech.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sim {

namespace {

PlotView makePlotView(const Content& content, const WorldState& state, std::uint32_t index,
                      CompanyId viewer) {
    const Plot& plot = state.plots[index];
    PlotView plotView;
    plotView.mineCapexQuote =
        mineCapexQuote(content, state.companies[viewer.index()].completedTechs, plot.areaHectares);
    plotView.id = PlotId(index);
    plotView.region = plot.region;
    plotView.areaHectares = plot.areaHectares;
    plotView.askPrice = plot.askPrice;
    plotView.listing = plot.listing;
    plotView.owner = plot.owner;
    if (plot.auction.valid()) {
        const Auction& auction = state.auctions[plot.auction.index()];
        AuctionView auctionView;
        auctionView.id = plot.auction;
        auctionView.closesOnDay = auction.closesOnDay;
        auctionView.bidCount = static_cast<std::uint32_t>(auction.bids.size());
        for (const Bid& bid : auction.bids) {
            if (bid.company == viewer) {
                auctionView.myBid = bid.amount;
            }
        }
        plotView.auction = auctionView;
    }
    if (plot.mine.valid()) {
        const Mine& mine = state.mines[plot.mine.index()];
        MinePublicView mineView;
        mineView.id = plot.mine;
        mineView.owner = mine.owner;
        mineView.state = mine.state;
        plotView.mine = mineView;
    }
    return plotView;
}

const Fixed kMaxSalePremium = Fixed::fromInt(1000);

SaleView makeSaleView(const Auction& auction) {
    SaleView sale;
    sale.plot = auction.plot;
    sale.buyer = auction.winner;
    sale.ask = auction.askPrice;
    sale.paid = auction.winningAmount;
    sale.day = auction.closesOnDay;
    // A bid of thousands of times the ask does not fit a Fixed; the premium
    // saturates there, far past anything the interface needs to tell apart.
    const std::int64_t over = (auction.winningAmount - auction.askPrice).cents();
    if (auction.askPrice > Money::zero() && over > 0) {
        sale.premium =
            Fixed::fromRaw(std::min(checkedMulDiv(over, Fixed::kScale, auction.askPrice.cents())
                                        .value_or(kMaxSalePremium.raw()),
                                    kMaxSalePremium.raw()));
    }
    return sale;
}

// Settled auctions, newest first: the last kRecentSales for the market's
// record, and each sold plot's own sale. Auctions are stored in the order
// they opened and all run the same window, so that is also closing order.
void addSales(const WorldState& state, CompanyView& view) {
    for (std::size_t i = state.auctions.size(); i-- > 0;) {
        const Auction& auction = state.auctions[i];
        if (auction.open != 0 || !auction.winner.valid()) {
            continue;
        }
        const SaleView sale = makeSaleView(auction);
        if (view.recentSales.size() < kRecentSales) {
            view.recentSales.push_back(sale);
        }
        PlotView& plot = view.plots[auction.plot.index()];
        if (!plot.sale.has_value()) {
            plot.sale = sale;
        }
    }
}

// More than kMajorDepositMult medians, exactly: the quotient and remainder
// keep the comparison inside 64 bits for any estimate.
bool isMajorDeposit(std::int64_t estimate, std::int64_t median) {
    if (median <= 0) {
        return false;
    }
    const std::int64_t whole = estimate / median;
    return whole > kMajorDepositMult || (whole == kMajorDepositMult && estimate % median > 0);
}

// The median deposit of `resource` in `region`, zero when the region's
// priors do not list it.
std::int64_t regionalMedianTonnes(const Content& content, RegionId region, ResourceId resource) {
    for (const DepositPrior& prior : content.regions[region.index()].deposits) {
        if (prior.resource == resource) {
            return prior.tonnesMedian;
        }
    }
    return 0;
}

void addOwnSurveys(const Content& content, const WorldState& state, CompanyId viewer,
                   CompanyView& view) {
    for (std::uint32_t i = 0; i < state.surveys.size(); ++i) {
        const Survey& survey = state.surveys[i];
        if (survey.company != viewer) {
            continue;
        }
        SurveyView surveyView;
        surveyView.id = SurveyId(i);
        surveyView.plot = survey.plot;
        surveyView.commissionedDay = survey.commissionedDay;
        surveyView.completesDay = survey.completesDay;
        surveyView.complete = survey.complete != 0;
        const RegionId region = state.plots[survey.plot.index()].region;
        for (const SurveyEstimate& estimate : survey.estimates) {
            const std::int64_t median = regionalMedianTonnes(content, region, estimate.resource);
            const bool major = isMajorDeposit(estimate.tonnesEstimate, median);
            surveyView.estimates.push_back(
                {estimate.resource, estimate.tonnesEstimate, estimate.relativeError, major});
        }
        view.surveys.push_back(surveyView);
    }
}

// Today's price of every ore, indexed by ResourceId.
std::vector<Money> currentPrices(const WorldState& state) {
    std::vector<Money> prices;
    prices.reserve(state.markets.size());
    for (const Market& market : state.markets) {
        prices.push_back(market.price);
    }
    return prices;
}

// A mine has earnings to show once it runs, or has run and is mothballed,
// and has a day's output on record (spec 008, edge case 4).
bool hasEarnings(const Mine& mine) {
    if (mine.state != MineState::Operating && mine.state != MineState::Mothballed) {
        return false;
    }
    return std::ranges::any_of(mine.lastOutputTonnes,
                               [](std::int64_t tonnes) { return tonnes > 0; });
}

// Cost, margin and break-even are the sim's own figures (R-001), worked out
// at the price as it stands now. A mine with no earnings to show keeps the
// zero margin and break-even it was constructed with.
void addMineEconomics(const Content& content, const WorldState& state, const Company& owner,
                      const Mine& mine, std::span<const Money> prices, MineView& mineView) {
    mineView.dailyOpex = mineRunningCost(content, state, owner, mine, mine.state);
    if (mine.state == MineState::Operating || mine.state == MineState::Mothballed) {
        mineView.operatingOpex = mineRunningCost(content, state, owner, mine, MineState::Operating);
    }
    mineView.hasEarnings = hasEarnings(mine);
    if (mineView.hasEarnings) {
        mineView.dailyMargin = mineDailyMargin(content, state, owner, mine, prices);
        mineView.breakEvenPrice = mineBreakEvenPrice(content, state, owner, mine, prices);
    }
}

// What an operating mine producing each ore would cost the viewer today.
void addMineOpexQuotes(const Content& content, const WorldState& state, const Company& viewer,
                       CompanyView& view) {
    view.mineOpexQuotes.reserve(content.resources.size());
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        view.mineOpexQuotes.push_back(mineOpexQuote(content, state, viewer, ResourceId(r)));
    }
}

void addOwnMines(const Content& content, const WorldState& state, CompanyId viewer,
                 CompanyView& view) {
    const Company& self = state.companies[viewer.index()];
    const std::vector<Money> prices = currentPrices(state);
    for (std::uint32_t i = 0; i < state.mines.size(); ++i) {
        const Mine& mine = state.mines[i];
        if (mine.owner != viewer) {
            continue;
        }
        MineView mineView;
        mineView.id = MineId(i);
        mineView.plot = mine.plot;
        mineView.state = mine.state;
        mineView.operationalDay = mine.operationalDay;
        mineView.extractedTonnes = mine.extractedTonnes;
        mineView.lastDayTonnes = mine.lastDayTonnes;
        mineView.lastOutputTonnes = mine.lastOutputTonnes;
        mineView.bestEstimateTonnes.assign(content.resources.size(), 0);
        mineView.estimatedRemainingTonnes.assign(content.resources.size(), -1);
        if (const Survey* survey = latestOwnSurvey(state, mine)) {
            for (const SurveyEstimate& estimate : survey->estimates) {
                mineView.bestEstimateTonnes[estimate.resource.index()] = estimate.tonnesEstimate;
                mineView.estimatedRemainingTonnes[estimate.resource.index()] =
                    estimatedRemainingTonnes(state, mine, *survey, estimate);
            }
        }
        addMineEconomics(content, state, self, mine, prices, mineView);
        view.mines.push_back(mineView);
    }
}

void addMarkets(const Content& content, const WorldState& state, CompanyView& view) {
    for (std::uint32_t i = 0; i < state.markets.size(); ++i) {
        const Market& market = state.markets[i];
        MarketView marketView;
        marketView.resource = ResourceId(i);
        marketView.price = market.price;
        marketView.basePrice = content.resources[i].basePrice;
        marketView.baseDemandTonnesPerDay = content.resources[i].baseDemandTonnesPerDay;
        marketView.recentSupplyTonnesPerDay = market.supplySmoothed.truncToInt();
        const std::size_t count = std::min(market.priceHistory.size(), kRecentPriceDays);
        marketView.recentPrices.assign(
            market.priceHistory.end() - static_cast<std::ptrdiff_t>(count),
            market.priceHistory.end());
        view.markets.push_back(marketView);
    }
}

void addOwnLoans(const WorldState& state, CompanyId viewer, CompanyView& view) {
    for (std::uint32_t i = 0; i < state.loans.size(); ++i) {
        const Loan& loan = state.loans[i];
        if (loan.company != viewer) {
            continue;
        }
        LoanView loanView;
        loanView.id = LoanId(i);
        loanView.principal = loan.principal;
        loanView.remaining = loan.remaining;
        loanView.principalPerMonth = loan.principalPerMonth;
        loanView.monthsRemaining = loan.monthsRemaining;
        loanView.active = loan.active != 0;
        view.loans.push_back(loanView);
    }
}

void addResearchAndEvents(const WorldState& state, CompanyId viewer, CompanyView& view) {
    for (const Research& research : state.research) {
        if (research.company == viewer && research.complete == 0) {
            ResearchView researchView;
            researchView.tech = research.tech;
            researchView.startedDay = research.startedDay;
            researchView.completesDay = research.completesDay;
            view.activeResearch = researchView;
        }
    }
    for (std::uint32_t i = 0; i < state.events.size(); ++i) {
        const ActiveEvent& active = state.events[i];
        if (active.active == 0) {
            continue;
        }
        ActiveEventView eventView;
        eventView.id = ActiveEventId(i);
        eventView.event = active.event;
        eventView.startedDay = active.startedDay;
        eventView.endsDay = active.endsDay;
        view.events.push_back(eventView);
    }
}

// Which projects the viewer may start, money and a busy lab aside: the sim's
// own answer, project by project, so nothing downstream restates the rule.
void addResearchOpen(const Content& content, const WorldState& state, CompanyId viewer,
                     CompanyView& view) {
    view.researchOpen.assign(content.techs.size(), 0);
    for (std::uint32_t t = 0; t < content.techs.size(); ++t) {
        view.researchOpen[t] = researchOpenTo(content, state, viewer, TechId(t)) ? 1 : 0;
    }
}

// How close the viewer is to each resource it cannot mine yet.
void addUnlockProgress(const Content& content, const WorldState& state, CompanyId viewer,
                       CompanyView& view) {
    const Company& self = state.companies[viewer.index()];
    view.unlockDaysRemaining.assign(content.resources.size(), 0);
    view.unlockReady.assign(content.resources.size(), 0);
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        const TechId tech = content.resources[r].unlockedBy;
        if (!tech.valid() || resourceUnlocked(content, self, ResourceId(r))) {
            continue;
        }
        if (view.activeResearch.has_value() && view.activeResearch->tech == tech) {
            // Research completes while its completion day is processed, so
            // the resource is mineable the day after.
            const Day completes = view.activeResearch->completesDay;
            view.unlockDaysRemaining[r] = completes >= state.day ? completes - state.day + 1 : 1;
            continue;
        }
        // The sim's own test for starting a project, money aside.
        view.unlockReady[r] = researchOpenTo(content, state, viewer, tech) ? 1 : 0;
    }
}

void addCompanies(const WorldState& state, CompanyView& view) {
    for (std::uint32_t i = 0; i < state.companies.size(); ++i) {
        const Company& other = state.companies[i];
        CompanyPublicView publicView;
        publicView.id = CompanyId(i);
        publicView.name = other.name;
        publicView.kind = other.kind;
        publicView.bankrupt = other.bankrupt != 0;
        for (const Plot& plot : state.plots) {
            if (plot.owner == CompanyId(i)) {
                ++publicView.plotsOwned;
            }
        }
        view.companies.push_back(publicView);
    }
}

// The stage a company is working on; nothing once it has completed them all.
const ProgramStage* currentProgramStage(const Content& content, const Company& company) {
    const std::size_t stage = company.programStage;
    return stage < content.program.stages.size() ? &content.program.stages[stage] : nullptr;
}

// A company's project for `tech` while it is in progress. Only ever asked
// about a program stage's project, which is public. A company's other
// research stays its own but for one thing: its earliest founding day counts
// a lab busy with another project, so every view shows when that project
// ends, though never what it is (spec 011, GA-008).
std::optional<ResearchView> researchInProgress(const WorldState& state, CompanyId company,
                                               TechId tech) {
    for (const Research& research : state.research) {
        if (research.company == company && research.tech == tech && research.complete == 0) {
            return ResearchView{research.tech, research.startedDay, research.completesDay};
        }
    }
    return std::nullopt;
}

ProgramResearchState programResearchState(const WorldState& state, CompanyId id,
                                          const ProgramStage* stage) {
    if (stage == nullptr || techCompleted(state.companies[id.index()], stage->research)) {
        return ProgramResearchState::Done;
    }
    return researchInProgress(state, id, stage->research).has_value()
               ? ProgramResearchState::InProgress
               : ProgramResearchState::NotStarted;
}

// Keeping pace with the race (spec 011, BR-004) while the viewer's stage
// project has not started. The last day to start is worked out in signed
// days: against a rival close to founding it can fall before day 0, and it
// is then only ever reported as passed.
void addOwnPace(const Content& content, const WorldState& state, CompanyId viewer,
                const ProgramStageView& stage, ProgramView& program) {
    if (stage.researchState != ProgramResearchState::NotStarted) {
        return;
    }
    if (gameEnded(state) || !earliestFoundingDay(content, state, viewer)) {
        return;
    }
    // Not the stage's project, which has not started: another.
    if (const std::optional<ResearchId> active = activeResearch(state, viewer)) {
        program.labFreeDay = state.research[active->index()].completesDay + 1;
    }
    const Day deadline = content.program.deadlineDay;
    const Day target =
        std::min(leadingRivalFoundingDay(content, state, viewer).value_or(deadline), deadline);
    const std::int64_t lastStart =
        std::int64_t{target} - std::int64_t{foundingDayIfStartedOn(content, stage.index, 0)};
    const std::int64_t today = state.day;
    if (lastStart < today) {
        program.lastStartPassed = true;
    } else {
        program.lastStartDay = static_cast<Day>(lastStart);
    }
    program.paceWarning =
        program.lastStartPassed || today + content.program.paceWarningDays >= lastStart;
}

// The stage lists the ore among its requirements.
bool stageNeeds(const ProgramStage& stage, ResourceId resource) {
    return std::ranges::any_of(stage.requirements, [resource](const ProgramRequirement& r) {
        return r.resource == resource;
    });
}

// Tonnes of each ore the stages not yet complete still ask for: what is left
// of the current stage's need and the whole need of every stage after it.
std::vector<std::int64_t> remainingNeedTonnes(const Content& content, const Company& self) {
    std::vector<std::int64_t> need(content.resources.size(), 0);
    for (std::size_t s = self.programStage; s < content.program.stages.size(); ++s) {
        for (const ProgramRequirement& requirement : content.program.stages[s].requirements) {
            const std::size_t r = requirement.resource.index();
            const std::int64_t delivered = s == self.programStage ? self.programDelivered[r] : 0;
            need[r] =
                checkedAdd(need[r], std::max<std::int64_t>(requirement.tonnes - delivered, 0));
        }
    }
    return need;
}

// The ores the viewer's program lacks (spec 011, BR-007), from the viewer's
// stock and its own mines as the view shows them. A locked ore counts like
// any other: no mine yields it (edge case 8).
void addMissingOres(const Content& content, const WorldState& state, const Company& self,
                    CompanyView& view) {
    std::vector<MissingOre>& missing = view.program.missingOres;
    missing.assign(content.resources.size(), MissingOre::None);
    const std::size_t stage = self.programStage;
    if (stage >= content.program.stages.size() || gameEnded(state) || self.bankrupt != 0) {
        return;
    }
    const std::vector<std::int64_t> need = remainingNeedTonnes(content, self);
    for (std::size_t r = 0; r < need.size(); ++r) {
        const ResourceId resource(static_cast<std::uint32_t>(r));
        if (need[r] <= self.inventoryTonnes[r] ||
            std::ranges::any_of(view.mines, [resource](const MineView& mine) {
                return mineYieldsOre(mine, resource);
            })) {
            continue;
        }
        const bool soon = stageNeeds(content.program.stages[stage], resource) ||
                          (stage + 1 < content.program.stages.size() &&
                           stageNeeds(content.program.stages[stage + 1], resource));
        missing[r] = soon ? MissingOre::Soon : MissingOre::Later;
    }
}

// The viewer's own program: what its current stage asks for and how far
// each part has got. The figures are the sim's own (R-001).
void addOwnProgram(const Content& content, const WorldState& state, CompanyId viewer,
                   CompanyView& view) {
    const Company& self = state.companies[viewer.index()];
    const ProgramStage* stage = currentProgramStage(content, self);
    view.program.stagesCompleted = self.programStage;
    view.program.complete = stage == nullptr;
    view.program.stageDays = self.programStageDays;
    addMissingOres(content, state, self, view);
    if (stage == nullptr) {
        return;
    }
    ProgramStageView stageView;
    stageView.index = self.programStage;
    stageView.cost = stage->cost;
    stageView.research = stage->research;
    stageView.researchState = programResearchState(state, viewer, stage);
    if (stageView.researchState == ProgramResearchState::InProgress) {
        stageView.activeResearch = researchInProgress(state, viewer, stage->research);
    }
    for (const ProgramRequirement& requirement : stage->requirements) {
        const std::size_t r = requirement.resource.index();
        stageView.requirements.push_back({requirement.resource, requirement.tonnes,
                                          self.programDelivered[r], self.inventoryTonnes[r]});
    }
    stageView.progressPermille = programProgressPermille(content, state, viewer);
    stageView.ready = programStageReady(content, state, viewer);
    addOwnPace(content, state, viewer, stageView, view.program);
    view.program.stage = stageView;
}

// The deadline, the outcome and every company's place in the race, the
// viewer's included: all public (spec 010, BR-007).
void addRace(const Content& content, const WorldState& state, CompanyView& view) {
    view.deadlineDay = content.program.deadlineDay;
    view.outcome.ended = gameEnded(state);
    view.outcome.day = state.outcome.day;
    view.outcome.founder = state.outcome.founder;
    view.outcome.results = state.outcome.results;
    view.programs.reserve(state.companies.size());
    for (std::uint32_t i = 0; i < state.companies.size(); ++i) {
        const CompanyId id(i);
        const ProgramStage* stage = currentProgramStage(content, state.companies[i]);
        ProgramPublicView entry;
        entry.company = id;
        entry.stagesCompleted = state.companies[i].programStage;
        entry.complete = stage == nullptr;
        entry.stageDays = state.companies[i].programStageDays;
        entry.progressPermille = programProgressPermille(content, state, id);
        entry.researchState = programResearchState(state, id, stage);
        entry.earliestFoundingDay = earliestFoundingDay(content, state, id);
        if (stage != nullptr) {
            if (const std::optional<ResearchView> due =
                    researchInProgress(state, id, stage->research)) {
                entry.researchDueDay = due->completesDay;
            }
        }
        entry.pastDeadline = foundsPastDeadline(content, state, id);
        view.programs.push_back(entry);
    }
}

}  // namespace

bool mineYieldsOre(const MineView& mine, ResourceId resource) {
    if (mine.state != MineState::Building && mine.state != MineState::Operating &&
        mine.state != MineState::Mothballed) {
        return false;
    }
    const std::size_t r = resource.index();
    return mine.extractedTonnes[r] > 0 || mine.bestEstimateTonnes[r] > 0;
}

CompanyView makeCompanyView(const Content& content, const WorldState& state, CompanyId company) {
    CompanyView view;
    view.me = company;
    view.day = state.day;

    const Company& self = state.companies[company.index()];
    view.cash = self.cash;
    view.reservedCash = reservedCash(state, company);
    view.inventoryTonnes = self.inventoryTonnes;
    view.sellPolicies = self.sellPolicies;
    view.coverCosts = self.coverCosts != 0;
    view.autoSoldTonnes = self.autoSoldTonnes;
    view.autoSoldProceeds = self.autoSoldProceeds;
    view.completedTechs = self.completedTechs;
    view.outstandingPrincipal = outstandingPrincipal(state, company);
    view.loanCapacity = content.balance.loanMaxPrincipal - view.outstandingPrincipal;
    view.monthlyStorageCost = monthlyStorageCost(content, state, company);
    view.storageWarning =
        view.monthlyStorageCost > Money::zero() &&
        (view.cash <= Money::zero() ||
         view.monthlyStorageCost > view.cash.scaled(content.balance.storageWarningCashShare));
    addMineOpexQuotes(content, state, self, view);
    view.surveyNoiseQuote = surveyNoise(content, self);
    view.monthsInsolvent = self.monthsInsolvent;
    view.bankrupt = self.bankrupt != 0;

    view.plots.reserve(state.plots.size());
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        view.plots.push_back(makePlotView(content, state, i, company));
    }
    addSales(state, view);
    addOwnSurveys(content, state, company, view);
    addOwnMines(content, state, company, view);
    addMarkets(content, state, view);
    addOwnLoans(state, company, view);
    addResearchAndEvents(state, company, view);
    addResearchOpen(content, state, company, view);
    addUnlockProgress(content, state, company, view);
    view.unlistedPlotsRemaining =
        static_cast<std::uint32_t>(state.listingQueue.size()) - state.nextListing;
    view.withdrawnPlots = static_cast<std::uint32_t>(state.withdrawnQueue.size());
    addCompanies(state, view);
    addOwnProgram(content, state, company, view);
    addRace(content, state, view);
    return view;
}

}  // namespace sim
