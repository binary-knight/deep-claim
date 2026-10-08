#include "sim/plot_market.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fatal.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace sim {

namespace {

Notice auctionNotice(NoticeKind kind, CompanyId company, PlotId plot, AuctionId auction,
                     Money amount = Money::zero()) {
    Notice notice = makeNotice(kind, company, plot, amount);
    notice.auction = auction;
    return notice;
}

// Ask price multiplier after an auction ends without a sale, and the floor
// relative to the generated land value.
const Fixed kUnsoldAskMult = Fixed::fromRatio(9, 10);
const Fixed kAskFloorMult = Fixed::fromRatio(1, 2);

AuctionId openAuction(const Content& content, WorldState& state, PlotId plotId) {
    Auction auction;
    auction.plot = plotId;
    auction.opensOnDay = state.day;
    auction.closesOnDay = state.day + content.balance.auctionWindowDays;
    state.auctions.push_back(auction);

    const AuctionId id(static_cast<std::uint32_t>(state.auctions.size() - 1));
    Plot& plot = state.plots[plotId.index()];
    plot.listing = PlotListing::Listed;
    plot.auction = id;
    return id;
}

bool bidBeats(const Bid& a, const Bid& b) {
    if (a.amount != b.amount) {
        return a.amount > b.amount;
    }
    if (a.day != b.day) {
        return a.day < b.day;
    }
    return a.order < b.order;
}

void closeAuction(const Content& content, WorldState& state, AuctionId auctionId,
                  TickReport& report) {
    // Copy what we need: relisting below appends to state.auctions, which
    // would invalidate a reference into it.
    const PlotId plotId = state.auctions[auctionId.index()].plot;
    const std::vector<Bid> ranked = [&] {
        std::vector<Bid> bids = state.auctions[auctionId.index()].bids;
        std::sort(bids.begin(), bids.end(), bidBeats);
        return bids;
    }();
    state.auctions[auctionId.index()].open = 0;
    Plot& plot = state.plots[plotId.index()];
    plot.auction = AuctionId();

    CompanyId winner;
    for (const Bid& bid : ranked) {
        Company& company = state.companies[bid.company.index()];
        // A bidder may have lost the cash to other costs since bidding.
        if (company.bankrupt != 0 || company.cash < bid.amount) {
            report.notices.push_back(auctionNotice(NoticeKind::BidForfeited, bid.company, plotId,
                                                   auctionId, bid.amount));
            continue;
        }
        company.cash -= bid.amount;
        plot.owner = bid.company;
        plot.listing = PlotListing::Sold;
        state.auctions[auctionId.index()].winner = bid.company;
        state.auctions[auctionId.index()].winningAmount = bid.amount;
        winner = bid.company;
        report.notices.push_back(
            auctionNotice(NoticeKind::PlotSold, bid.company, plotId, auctionId, bid.amount));
        break;
    }
    if (winner.valid()) {
        bool pastWinner = false;
        for (const Bid& other : ranked) {
            if (other.company == winner) {
                pastWinner = true;
                continue;
            }
            if (pastWinner) {
                // Ranked below the winner: outbid.
                report.notices.push_back(
                    auctionNotice(NoticeKind::AuctionLost, other.company, plotId, auctionId));
            }
        }
        return;
    }

    // Unsold: drop the ask (never below half the land value) and relist.
    const Money floor = plot.basePrice.scaled(kAskFloorMult);
    plot.askPrice = std::max(plot.askPrice.scaled(kUnsoldAskMult), floor);
    const Money newAsk = plot.askPrice;
    const AuctionId relisted = openAuction(content, state, plotId);  // may reallocate auctions
    report.notices.push_back(
        auctionNotice(NoticeKind::PlotRelisted, CompanyId(), plotId, relisted, newAsk));
}

}  // namespace

void listPlots(const Content& content, WorldState& state, std::uint32_t count, TickReport& report) {
    for (std::uint32_t i = 0; i < count && state.nextListing < state.listingQueue.size(); ++i) {
        const PlotId plotId = state.listingQueue[state.nextListing++];
        const AuctionId auction = openAuction(content, state, plotId);
        report.notices.push_back(auctionNotice(NoticeKind::PlotListed, CompanyId(), plotId, auction,
                                               state.plots[plotId.index()].askPrice));
    }
}

void runPlotMarket(const Content& content, WorldState& state, TickReport& report) {
    // Index loop: closing an unsold auction appends a new one.
    const std::size_t existing = state.auctions.size();
    for (std::size_t i = 0; i < existing; ++i) {
        if (state.auctions[i].open != 0 && state.auctions[i].closesOnDay <= state.day) {
            closeAuction(content, state, AuctionId(static_cast<std::uint32_t>(i)), report);
        }
    }
    if (state.day > 0 && state.day % kDaysPerMonth == 0) {
        listPlots(content, state, content.balance.plotsListedPerMonth, report);
    }
}

Money reservedCash(const WorldState& state, CompanyId company) {
    Money total = Money::zero();
    for (const Auction& auction : state.auctions) {
        if (auction.open == 0) {
            continue;
        }
        for (const Bid& bid : auction.bids) {
            if (bid.company == company) {
                total += bid.amount;
            }
        }
    }
    return total;
}

Money availableCash(const WorldState& state, CompanyId company) {
    return state.companies[company.index()].cash - reservedCash(state, company);
}

std::optional<RejectReason> validateBid(const WorldState& state, const BidOnPlot& bid) {
    if (!bid.company.valid() || bid.company.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    const Company& company = state.companies[bid.company.index()];
    if (company.bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (!bid.plot.valid() || bid.plot.index() >= state.plots.size()) {
        return RejectReason::UnknownPlot;
    }
    const Plot& plot = state.plots[bid.plot.index()];
    if (plot.listing != PlotListing::Listed || !plot.auction.valid()) {
        return RejectReason::PlotNotForSale;
    }
    if (bid.amount < plot.askPrice) {
        return RejectReason::BidBelowAsk;
    }
    // Cash committed elsewhere stays committed; a rebid on this plot only
    // needs to cover the difference from the standing bid.
    Money available = availableCash(state, bid.company);
    for (const Bid& existing : state.auctions[plot.auction.index()].bids) {
        if (existing.company == bid.company) {
            available += existing.amount;
        }
    }
    if (bid.amount > available) {
        return RejectReason::InsufficientFunds;
    }
    return std::nullopt;
}

void applyBid(WorldState& state, const BidOnPlot& bid) {
    if (state.bidCounter == std::numeric_limits<std::uint32_t>::max()) {
        fatal("applyBid: bid counter exhausted");
    }
    Auction& auction = state.auctions[state.plots[bid.plot.index()].auction.index()];
    Bid entry;
    entry.company = bid.company;
    entry.amount = bid.amount;
    entry.day = state.day;
    entry.order = state.bidCounter++;
    for (Bid& existing : auction.bids) {
        if (existing.company == bid.company) {
            existing = entry;
            return;
        }
    }
    auction.bids.push_back(entry);
}

}  // namespace sim
