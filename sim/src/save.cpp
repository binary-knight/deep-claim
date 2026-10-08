#include "sim/save.h"

#include "sim/archive.h"
#include "sim/serialize.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sim {

namespace {
constexpr std::array<std::uint8_t, 4> kMagic = {'D', 'C', 'S', 'V'};
}  // namespace

std::uint64_t hashWorld(const WorldState& state) {
    return hashOf(state);
}

std::uint64_t hashContent(const Content& content) {
    return hashOf(content);
}

namespace {

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

bool validateAuctions(const WorldState& state) {
    for (const Auction& auction : state.auctions) {
        if (!auction.plot.valid() || auction.plot.index() >= state.plots.size() ||
            auction.open > 1 || auction.closesOnDay < auction.opensOnDay) {
            return false;
        }
        if (auction.open != 0 && state.plots[auction.plot.index()].listing != PlotListing::Listed) {
            return false;
        }
        for (const Bid& bid : auction.bids) {
            if (!bid.company.valid() || bid.company.index() >= state.companies.size() ||
                bid.amount < Money::zero()) {
                return false;
            }
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
    for (const PlotId plot : state.listingQueue) {
        if (!plot.valid() || plot.index() >= state.plots.size() || seen[plot.index()] != 0) {
            return false;
        }
        seen[plot.index()] = 1;
    }
    return true;
}

}  // namespace

bool validateWorld(const Content& content, const WorldState& state) {
    if (state.plots.size() != state.hidden.plotDeposits.size()) {
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

std::vector<std::uint8_t> saveWorld(const WorldState& state, std::uint64_t contentHash) {
    WriteArchive archive;
    for (const std::uint8_t byte : kMagic) {
        archive(byte);
    }
    std::uint32_t version = kSaveVersion;
    archive(version);
    archive(contentHash);
    archive(const_cast<WorldState&>(state));  // NOLINT(cppcoreguidelines-pro-type-const-cast)
    return archive.release();
}

bool loadWorld(std::span<const std::uint8_t> bytes, const Content& content, WorldState& out) {
    const std::uint64_t contentHash = hashContent(content);
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
    if (archive.failed() || savedContentHash != contentHash) {
        return false;
    }
    archive(out);
    return !archive.failed() && archive.atEnd() && validateWorld(content, out);
}

}  // namespace sim
