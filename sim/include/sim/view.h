#pragma once

// What one company is allowed to know. Built from WorldState without ever
// reading WorldState::hidden; the UI and AI controllers see only this.

#include "sim/content.h"
#include "sim/fixed.h"
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

// A mine's public face: that it exists, whose it is, whether it runs.
struct MinePublicView {
    MineId id;
    CompanyId owner;
    MineState state = MineState::Building;
};

struct PlotView {
    PlotId id;
    RegionId region;
    std::int64_t areaHectares = 0;
    Money askPrice;
    PlotListing listing = PlotListing::Unlisted;
    CompanyId owner;
    std::optional<AuctionView> auction;
    std::optional<MinePublicView> mine;
};

struct SurveyEstimateView {
    ResourceId resource;
    std::int64_t tonnesEstimate = 0;
    Fixed relativeError;
};

// One of the viewer's own surveys, pending or complete.
struct SurveyView {
    SurveyId id;
    PlotId plot;
    Day commissionedDay = 0;
    Day completesDay = 0;
    bool complete = false;
    std::vector<SurveyEstimateView> estimates;
};

// One of the viewer's own mines, with what extraction has revealed so far.
struct MineView {
    MineId id;
    PlotId plot;
    MineState state = MineState::Building;
    Day operationalDay = 0;
    std::vector<std::int64_t> extractedTonnes;  // indexed by ResourceId
    Money dailyOpex;
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
    std::vector<SurveyView> surveys;
    std::vector<MineView> mines;
    std::vector<CompanyPublicView> companies;
};

[[nodiscard]] CompanyView makeCompanyView(const Content& content, const WorldState& state,
                                          CompanyId company);

}  // namespace sim
