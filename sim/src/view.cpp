// This file must never read the concealed deposit state. A test greps for it.
#include "sim/view.h"

#include "sim/content.h"
#include "sim/ids.h"
#include "sim/mines.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/world_state.h"

#include <cstdint>

namespace sim {

namespace {

PlotView makePlotView(const WorldState& state, std::uint32_t index, CompanyId viewer) {
    const Plot& plot = state.plots[index];
    PlotView plotView;
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

void addOwnSurveys(const WorldState& state, CompanyId viewer, CompanyView& view) {
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
        for (const SurveyEstimate& estimate : survey.estimates) {
            surveyView.estimates.push_back(
                {estimate.resource, estimate.tonnesEstimate, estimate.relativeError});
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
        mineView.dailyOpex = mineDailyOpex(content, self, mine.state);
        view.mines.push_back(mineView);
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
    view.completedTechs = self.completedTechs;

    view.plots.reserve(state.plots.size());
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        view.plots.push_back(makePlotView(state, i, company));
    }
    addOwnSurveys(state, company, view);
    addOwnMines(content, state, company, view);
    addCompanies(state, view);
    return view;
}

}  // namespace sim
