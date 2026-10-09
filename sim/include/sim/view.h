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
    Money mineCapexQuote;  // what the viewer would pay to build here today
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

// Public commodity market data.
struct MarketView {
    ResourceId resource;
    Money price;
    Money basePrice;
    std::int64_t baseDemandTonnesPerDay = 0;
    std::int64_t recentSupplyTonnesPerDay = 0;  // smoothed company sales
    // The most recent prices, oldest first, at most kRecentPriceDays entries.
    // The full history is available from Simulation::priceHistory for charts.
    std::vector<Money> recentPrices;
};

constexpr std::size_t kRecentPriceDays = 90;

struct ResearchView {
    TechId tech;
    Day startedDay = 0;
    Day completesDay = 0;
};

struct ActiveEventView {
    ActiveEventId id;
    EventId event;
    Day startedDay = 0;
    Day endsDay = 0;
};

struct LoanView {
    LoanId id;
    Money principal;
    Money remaining;
    Money principalPerMonth;
    std::uint32_t monthsRemaining = 0;
    bool active = false;
};

// One of the viewer's own mines, with what extraction has revealed so far.
struct MineView {
    MineId id;
    PlotId plot;
    MineState state = MineState::Building;
    Day operationalDay = 0;
    std::vector<std::int64_t> extractedTonnes;   // indexed by ResourceId
    std::vector<std::int64_t> lastDayTonnes;     // output on the last processed day
    std::vector<std::int64_t> lastOutputTonnes;  // output on the most recent operating day
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
    Money outstandingPrincipal;
    Money loanCapacity;        // how much more may be borrowed
    Money monthlyStorageCost;  // at current inventory
    Money mineOpexQuote;       // daily opex of an operating mine for the viewer today
    Fixed surveyNoiseQuote;    // relative error of the viewer's surveys today
    std::uint32_t monthsInsolvent = 0;
    bool bankrupt = false;
    std::vector<std::int64_t> inventoryTonnes;
    // Standing sell orders (spec 001), indexed by ResourceId, with what they
    // sold so far this month.
    std::vector<SellPolicy> sellPolicies;
    bool coverCosts = false;
    std::vector<std::int64_t> autoSoldTonnes;
    std::vector<Money> autoSoldProceeds;
    std::vector<TechId> completedTechs;  // own research only
    std::optional<ResearchView> activeResearch;
    std::vector<PlotView> plots;
    std::vector<SurveyView> surveys;
    std::vector<MineView> mines;
    std::vector<MarketView> markets;      // indexed by ResourceId
    std::vector<LoanView> loans;          // own loans only
    std::vector<ActiveEventView> events;  // public, active only
    std::vector<CompanyPublicView> companies;
};

[[nodiscard]] CompanyView makeCompanyView(const Content& content, const WorldState& state,
                                          CompanyId company);

}  // namespace sim
