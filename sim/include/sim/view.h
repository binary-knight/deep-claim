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

// A settled auction: public, like the purchase notice it repeats.
struct SaleView {
    PlotId plot;
    CompanyId buyer;
    Money ask;      // the minimum bid the auction opened with
    Money paid;     // the winning bid
    Day day = 0;    // the day the auction closed
    Fixed premium;  // paid over the ask, less one: 0.16 is 16% over
};

// How many settled auctions CompanyView::recentSales keeps.
constexpr std::size_t kRecentSales = 16;

struct PlotView {
    PlotId id;
    RegionId region;
    std::int64_t areaHectares = 0;
    Money askPrice;
    PlotListing listing = PlotListing::Unlisted;
    CompanyId owner;
    std::optional<AuctionView> auction;
    std::optional<MinePublicView> mine;
    Money mineCapexQuote;          // what the viewer would pay to build here today
    std::optional<SaleView> sale;  // the auction this plot was sold at, once sold
};

struct SurveyEstimateView {
    ResourceId resource;
    std::int64_t tonnesEstimate = 0;
    Fixed relativeError;
    // The estimate is more than kMajorDepositMult times the median deposit
    // of this ore in the plot's region: a find that will outlast most.
    bool major = false;
};

constexpr std::int64_t kMajorDepositMult = 2;

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
    // The owner's most recent completed survey estimate per resource for this
    // plot, zero where never surveyed; the interface shows remaining tonnes
    // and days from it. Indexed by ResourceId.
    std::vector<std::int64_t> bestEstimateTonnes;
    // What that survey implies is left today (sim::estimatedRemainingTonnes),
    // -1 for a resource the survey did not report or with no survey at all.
    std::vector<std::int64_t> estimatedRemainingTonnes;
    // What the mine costs today in its present state (sim::mineRunningCost):
    // the mothballed share when mothballed, zero when it is building,
    // depleted or closed.
    Money dailyOpex;
    // What it costs, or would cost, as an operating mine; equal to dailyOpex
    // for an operating mine. The reopening decision weighs dailyMargin
    // against this.
    Money operatingOpex;
    // Spec 008. True when the mine has earnings to show: it is operating or
    // mothballed and has output on record. A mine that is building, has not
    // produced yet, or is depleted or closed has none, and the two figures
    // below are then zero and mean nothing.
    bool hasEarnings = false;
    // What the mine earns in a day at today's prices (sim::mineDailyMargin):
    // its most recent operating day's output less operatingOpex, negative
    // for a loss. For a mothballed mine, what it would earn if reopened.
    Money dailyMargin;
    // The price per tonne of its principal ore at which dailyMargin is zero
    // (sim::mineBreakEvenPrice); zero when its other ores cover the cost.
    Money breakEvenPrice;
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
    // Per ore (spec 008): what an operating mine producing that ore costs
    // the viewer a day, today. Indexed by ResourceId; quoted for every ore,
    // unlocked or not.
    std::vector<Money> mineOpexQuotes;
    Fixed surveyNoiseQuote;  // relative error of the viewer's surveys today
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
    // Per resource (spec 004), for ore the viewer cannot mine yet: days until
    // the viewer's research in progress unlocks it (0 when that tech is not
    // being researched), and whether the unlocking tech could be started now
    // (its prerequisites are complete and it is not done). Indexed by
    // ResourceId; both zero for unlocked resources.
    std::vector<Day> unlockDaysRemaining;
    std::vector<std::uint8_t> unlockReady;
    // Land not yet offered: plots never listed, and plots withdrawn after
    // going unsold that will return to sale.
    std::uint32_t unlistedPlotsRemaining = 0;
    std::uint32_t withdrawnPlots = 0;
    // The most recent settled auctions, newest first, at most kRecentSales.
    std::vector<SaleView> recentSales;
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
