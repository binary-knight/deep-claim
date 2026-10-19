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

// How far a company's research into its current program stage has got.
enum class ProgramResearchState : std::uint8_t {
    NotStarted,
    InProgress,
    Done,
};

// One good the viewer's current stage asks for (spec 010).
struct ProgramRequirementView {
    ResourceId resource;
    std::int64_t neededTonnes = 0;     // what the stage asks for
    std::int64_t deliveredTonnes = 0;  // delivered to the program so far
    std::int64_t stockTonnes = 0;      // in the viewer's stock, deliverable today
};

// The stage of the program the viewer is working on.
struct ProgramStageView {
    std::uint32_t index = 0;  // position in Content::program.stages, from zero
    Money cost;
    TechId research;  // the project the stage needs
    ProgramResearchState researchState = ProgramResearchState::NotStarted;
    // The project while it is in progress, with its finish day; empty when
    // it has not started or is done.
    std::optional<ResearchView> activeResearch;
    std::vector<ProgramRequirementView> requirements;  // in the stage's own order
    std::int64_t progressPermille = 0;                 // sim::programProgressPermille
    // A CompleteProgramStage order would be accepted today
    // (sim::programStageReady): research, deliveries and cash are in place.
    bool ready = false;
};

// The viewer's own program.
struct ProgramView {
    std::uint32_t stagesCompleted = 0;
    // Every stage is completed. Also true when the content has no program.
    bool complete = false;
    // The day each completed stage was completed, oldest first
    // (Company::programStageDays): stagesCompleted of them.
    std::vector<Day> stageDays;
    std::optional<ProgramStageView> stage;  // the current stage; empty when complete
    // Keeping pace (spec 011, BR-004), while the current stage's project has
    // not started and the game is being played: the last day it can start
    // and still found the program no later than the leading solvent rival
    // could (sim::leadingRivalFoundingDay), or the deadline day if that is
    // earlier. Empty once that day is before today, which lastStartPassed
    // then says. Empty and false for a bankrupt viewer.
    std::optional<Day> lastStartDay;
    bool lastStartPassed = false;
    // Under the same conditions, when the lab is busy with another project:
    // the day after that project ends, the first day the stage's project can
    // start.
    std::optional<Day> labFreeDay;
    // The last day to start has passed or is no more than
    // ProgramDef::paceWarningDays from today.
    bool paceWarning = false;
};

// Any company's place in the race. Public (spec 010, BR-007): a rival's
// entry is as full as the viewer's own.
struct ProgramPublicView {
    CompanyId company;
    std::uint32_t stagesCompleted = 0;
    bool complete = false;
    // The day it completed each stage, oldest first: stagesCompleted of them.
    std::vector<Day> stageDays;
    // Toward its current stage (sim::programProgressPermille); 1000 when
    // the program is complete.
    std::int64_t progressPermille = 0;
    // Of its current stage's project; Done when the program is complete.
    ProgramResearchState researchState = ProgramResearchState::NotStarted;
    // The earliest day it could found the program (sim::earliestFoundingDay,
    // spec 011); empty when it is bankrupt.
    std::optional<Day> earliestFoundingDay;
    // The finish day of its current stage's project while that is under way.
    std::optional<Day> researchDueDay;
    // Its earliest founding day is after the deadline day
    // (sim::foundsPastDeadline): it can no longer found the program in time.
    bool pastDeadline = false;
};

// How the game ended, or that it has not: the world's Outcome, public.
struct OutcomeView {
    bool ended = false;
    Day day = 0;                         // the day the game ended; zero until it has
    CompanyId founder;                   // invalid when nobody founded the program
    std::vector<ProgramResult> results;  // indexed by CompanyId; None until the game has ended
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
    // Per research project, indexed by TechId: 1 when the viewer may start it,
    // money and a busy lab aside (sim::researchOpenTo): it is not known and
    // not in progress, its prerequisites are complete and so are the program
    // stages it waits for. A StartResearch order for a project marked 0 is
    // always refused.
    std::vector<std::uint8_t> researchOpen;
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
    // The race for the program (spec 010). All of it is public.
    Day deadlineDay = 0;  // the game ends this day if nobody has founded the program
    OutcomeView outcome;
    ProgramView program;                      // the viewer's own
    std::vector<ProgramPublicView> programs;  // every company, indexed by CompanyId
};

[[nodiscard]] CompanyView makeCompanyView(const Content& content, const WorldState& state,
                                          CompanyId company);

}  // namespace sim
