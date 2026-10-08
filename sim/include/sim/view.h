#pragma once

// What one company is allowed to know. Built from WorldState without ever
// reading WorldState::hidden; the UI and AI controllers see only this.

#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sim {

struct AuctionView {
    AuctionId id;
    Day closesOnDay = 0;
    std::uint32_t bidCount = 0;  // bids from everyone, amounts sealed
    Money myBid;                 // zero when this company has not bid
};

struct PlotView {
    PlotId id;
    RegionId region;
    std::int64_t areaHectares = 0;
    Money askPrice;
    PlotListing listing = PlotListing::Unlisted;
    CompanyId owner;
    std::optional<AuctionView> auction;
};

struct CompanyPublicView {
    CompanyId id;
    std::string name;
    CompanyKind kind = CompanyKind::Ai;
    std::uint32_t plotsOwned = 0;
    bool bankrupt = false;
};

struct CompanyView {
    CompanyId me;
    Day day = 0;
    Money cash;
    Money reservedCash;  // committed to standing bids
    std::vector<std::int64_t> inventoryTonnes;
    std::vector<PlotView> plots;
    std::vector<CompanyPublicView> companies;
};

[[nodiscard]] CompanyView makeCompanyView(const Content& content, const WorldState& state,
                                          CompanyId company);

}  // namespace sim
