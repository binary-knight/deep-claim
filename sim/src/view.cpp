// This file must never read the concealed deposit state. A test greps for it.
#include "sim/view.h"

#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/world_state.h"

#include <cstdint>

namespace sim {

CompanyView makeCompanyView(const Content& content, const WorldState& state, CompanyId company) {
    (void)content;  // reserved for name lookups as views grow
    CompanyView view;
    view.me = company;
    view.day = state.day;

    const Company& self = state.companies[company.index()];
    view.cash = self.cash;
    view.reservedCash = reservedCash(state, company);
    view.inventoryTonnes = self.inventoryTonnes;

    view.plots.reserve(state.plots.size());
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        const Plot& plot = state.plots[i];
        PlotView plotView;
        plotView.id = PlotId(i);
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
                if (bid.company == company) {
                    auctionView.myBid = bid.amount;
                }
            }
            plotView.auction = auctionView;
        }
        view.plots.push_back(plotView);
    }

    view.companies.reserve(state.companies.size());
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
    return view;
}

}  // namespace sim
