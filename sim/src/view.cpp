// This file must never read the concealed deposit state. A test greps for it.
#include "sim/view.h"

#include "sim/content.h"
#include "sim/finance.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/mines.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/surveys.h"
#include "sim/tech.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

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
        sale.premium = Fixed::fromRaw(
            std::min(checkedMulDiv(over, Fixed::kScale, auction.askPrice.cents())
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
            // Dividing keeps the comparison inside 64 bits for any estimate.
            const bool major = median > 0 && estimate.tonnesEstimate / kMajorDepositMult > median;
            surveyView.estimates.push_back(
                {estimate.resource, estimate.tonnesEstimate, estimate.relativeError, major});
        }
        view.surveys.push_back(surveyView);
    }
}

void addOwnMines(const Content& content, const WorldState& state, CompanyId viewer,
                 CompanyView& view) {
    const Company& self = state.companies[viewer.index()];
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
        mineView.dailyOpex = mineDailyOpex(content, state, self, mine.state);
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
        const TechDef& def = content.techs[tech.index()];
        const bool ready = std::ranges::all_of(def.prerequisites, [&](TechId p) {
            return std::find(self.completedTechs.begin(), self.completedTechs.end(), p) !=
                   self.completedTechs.end();
        });
        view.unlockReady[r] = ready ? 1 : 0;
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

}  // namespace

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
    view.mineOpexQuote = mineDailyOpex(content, state, self, MineState::Operating);
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
    addUnlockProgress(content, state, company, view);
    view.unlistedPlotsRemaining =
        static_cast<std::uint32_t>(state.listingQueue.size()) - state.nextListing;
    view.withdrawnPlots = static_cast<std::uint32_t>(state.withdrawnQueue.size());
    addCompanies(state, view);
    return view;
}

}  // namespace sim
