#include "sim/save.h"

#include "sim/archive.h"
#include "sim/serialize.h"
#include "sim/types.h"

#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <vector>

namespace sim {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'D', 'C', 'S', 'V'};

bool validateCompanies(const Content& content, const WorldState& state) {
    if (state.companies.empty() || state.companies[0].kind != CompanyKind::Player) {
        return false;
    }
    for (const Company& company : state.companies) {
        if (company.kind != CompanyKind::Player && company.kind != CompanyKind::Ai) {
            return false;
        }
        if (company.inventoryTonnes.size() != content.resources.size() || company.bankrupt > 1) {
            return false;
        }
        for (const std::int64_t tonnes : company.inventoryTonnes) {
            if (tonnes < 0) {
                return false;
            }
        }
    }
    return true;
}

bool validatePlots(const Content& content, const WorldState& state) {
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        const Plot& plot = state.plots[i];
        if (!plot.region.valid() || plot.region.index() >= content.regions.size()) {
            return false;
        }
        if (plot.areaHectares < 1 || plot.askPrice < Money::zero() ||
            plot.basePrice < Money::zero()) {
            return false;
        }
        switch (plot.listing) {
            case PlotListing::Unlisted:
                if (plot.owner.valid() || plot.auction.valid()) {
                    return false;
                }
                break;
            case PlotListing::Listed: {
                if (plot.owner.valid() || !plot.auction.valid() ||
                    plot.auction.index() >= state.auctions.size()) {
                    return false;
                }
                const Auction& auction = state.auctions[plot.auction.index()];
                if (auction.open == 0 || auction.plot != PlotId(i)) {
                    return false;
                }
                break;
            }
            case PlotListing::Sold:
                if (!plot.owner.valid() || plot.owner.index() >= state.companies.size() ||
                    plot.auction.valid()) {
                    return false;
                }
                break;
            default:
                return false;
        }
    }
    return true;
}

// Checks one auction's bids: valid bidders, dates inside the window, one bid
// per company, unique order stamps below the counter, and (for open
// auctions) representable reservation totals per company.
bool validateBids(const WorldState& state, const Auction& auction,
                  std::vector<std::uint8_t>& seenStamps, std::vector<std::int64_t>& reserved) {
    std::vector<std::uint8_t> bidders(state.companies.size(), 0);
    for (const Bid& bid : auction.bids) {
        if (!bid.company.valid() || bid.company.index() >= state.companies.size() ||
            bid.amount < Money::zero() || bid.day < auction.opensOnDay ||
            bid.day > auction.closesOnDay || bid.order >= state.bidCounter) {
            return false;
        }
        if (bidders[bid.company.index()]++ != 0 || seenStamps[bid.order]++ != 0) {
            return false;
        }
        if (auction.open != 0) {
            std::int64_t& total = reserved[bid.company.index()];
            if (bid.amount.cents() > std::numeric_limits<std::int64_t>::max() - total) {
                return false;
            }
            total += bid.amount.cents();
        }
    }
    return true;
}

bool validateAuctions(const WorldState& state) {
    std::vector<std::int64_t> reserved(state.companies.size(), 0);
    std::vector<std::uint8_t> seenStamps(state.bidCounter, 0);
    for (std::uint32_t a = 0; a < state.auctions.size(); ++a) {
        const Auction& auction = state.auctions[a];
        if (!auction.plot.valid() || auction.plot.index() >= state.plots.size() ||
            auction.open > 1 || auction.closesOnDay < auction.opensOnDay ||
            auction.closesOnDay > kMaxDay) {
            return false;
        }
        const Plot& plot = state.plots[auction.plot.index()];
        if (auction.open != 0 &&
            (plot.listing != PlotListing::Listed || plot.auction != AuctionId(a))) {
            return false;  // exactly one open auction per listed plot
        }
        if (!validateBids(state, auction, seenStamps, reserved)) {
            return false;
        }
        if (auction.winner.valid() &&
            (auction.open != 0 || auction.winner.index() >= state.companies.size())) {
            return false;
        }
    }
    return true;
}

bool validateListingQueue(const WorldState& state) {
    if (state.listingQueue.size() != state.plots.size() ||
        state.nextListing > state.listingQueue.size()) {
        return false;
    }
    std::vector<std::uint8_t> seen(state.plots.size(), 0);
    for (std::uint32_t i = 0; i < state.listingQueue.size(); ++i) {
        const PlotId plot = state.listingQueue[i];
        if (!plot.valid() || plot.index() >= state.plots.size() || seen[plot.index()] != 0) {
            return false;
        }
        seen[plot.index()] = 1;
        // Consumed entries have been listed (and maybe sold); the rest have not.
        const bool consumed = i < state.nextListing;
        const bool listedOrSold = state.plots[plot.index()].listing != PlotListing::Unlisted;
        if (consumed != listedOrSold) {
            return false;
        }
    }
    return true;
}

bool validateWorldImpl(const Content& content, const WorldState& state) {
    if (state.day > kMaxDay || state.plots.size() != state.hidden.plotDeposits.size()) {
        return false;
    }
    if (!validateCompanies(content, state) || !validatePlots(content, state) ||
        !validateAuctions(state) || !validateListingQueue(state)) {
        return false;
    }
    for (const PlotDeposits& deposits : state.hidden.plotDeposits) {
        for (const DepositTruth& truth : deposits.deposits) {
            if (!truth.resource.valid() || truth.resource.index() >= content.resources.size()) {
                return false;
            }
            if (truth.tonnesTotal < 0 || truth.tonnesRemaining < 0 ||
                truth.tonnesRemaining > truth.tonnesTotal) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

std::uint64_t hashWorld(const WorldState& state) {
    return hashOf(state);
}

std::uint64_t hashContent(const Content& content) {
    return hashOf(content);
}

bool validateWorld(const Content& content, const WorldState& state) {
    try {
        return validateWorldImpl(content, state);
    } catch (const std::bad_alloc&) {
        return false;
    }
}

std::vector<std::uint8_t> saveGame(const WorldState& state, const std::vector<Command>& pending,
                                   std::uint64_t contentHash) {
    WriteArchive archive;
    for (const std::uint8_t byte : kMagic) {
        archive(byte);
    }
    std::uint32_t version = kSaveVersion;
    archive(version);
    archive(contentHash);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    archive(const_cast<WorldState&>(state));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    archive(const_cast<std::vector<Command>&>(pending));
    return archive.release();
}

bool loadGame(std::span<const std::uint8_t> bytes, const Content& content, WorldState& outState,
              std::vector<Command>& outPending) {
    ReadArchive archive(bytes);
    for (const std::uint8_t expected : kMagic) {
        std::uint8_t byte = 0;
        archive(byte);
        if (byte != expected) {
            return false;
        }
    }
    std::uint32_t version = 0;
    archive(version);
    if (archive.failed() || version != kSaveVersion) {
        return false;
    }
    std::uint64_t savedContentHash = 0;
    archive(savedContentHash);
    if (archive.failed() || savedContentHash != hashContent(content)) {
        return false;
    }
    archive(outState);
    archive(outPending);
    return !archive.failed() && archive.atEnd() && validateWorld(content, outState);
}

}  // namespace sim
