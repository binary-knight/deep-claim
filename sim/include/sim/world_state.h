#pragma once

#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/random.h"
#include "sim/sell_policy.h"
#include "sim/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

enum class PlotListing : std::uint8_t {
    Unlisted,  // held back; listed on the schedule in balance.json
    Listed,    // open auction in progress
    Sold,      // owned by a company
};

// Public plot data. Everything here may be shown to every company.
struct Plot {
    RegionId region;
    std::int64_t areaHectares = 0;
    Money basePrice;  // generated land value
    Money askPrice;   // current minimum bid; drops when an auction ends unsold
    PlotListing listing = PlotListing::Unlisted;
    CompanyId owner;    // valid only when Sold
    AuctionId auction;  // valid only while Listed
    MineId mine;        // valid once a mine has been built here
    // Auctions in a row that closed without a sale since the plot was last
    // put up; at balance.maxUnsoldRounds the plot is withdrawn (spec 004).
    std::uint32_t unsoldCount = 0;
    // Set while withdrawn: the first day the plot may be listed again.
    Day relistNotBefore = 0;
};

// True deposit of one resource on one plot. Hidden from every company;
// surveys produce noisy estimates of tonnesTotal and extraction reveals it.
struct DepositTruth {
    ResourceId resource;
    std::int64_t tonnesTotal = 0;
    std::int64_t tonnesRemaining = 0;
};

struct PlotDeposits {
    std::vector<DepositTruth> deposits;  // only resources actually present
};

// State that no company view may read. Kept in one struct so the view code
// can be audited for never touching it.
struct HiddenState {
    std::vector<PlotDeposits> plotDeposits;  // indexed by PlotId
};

enum class CompanyKind : std::uint8_t { Player, Ai };

struct Company {
    std::string name;
    CompanyKind kind = CompanyKind::Ai;
    Money cash;                                 // may go negative; see finance
    std::vector<std::int64_t> inventoryTonnes;  // indexed by ResourceId
    std::vector<TechId> completedTechs;
    std::uint32_t monthsInsolvent = 0;  // consecutive month-ends with negative cash
    std::uint8_t bankrupt = 0;
    // Standing sell orders (spec 001), indexed by ResourceId, and the
    // month-to-date totals of what they sold (reset at month end).
    std::vector<SellPolicy> sellPolicies;
    std::uint8_t coverCosts = 0;  // sell at month end to keep cash at zero or above
    std::vector<std::int64_t> autoSoldTonnes;
    std::vector<Money> autoSoldProceeds;
    // The race for the program (spec 010): how many of Content::program's
    // stages the company has completed, and the tonnes of each resource it
    // has delivered toward the stage it is working on (indexed by ResourceId;
    // zero for a resource that stage does not need, and all zero once the
    // program is complete).
    std::uint32_t programStage = 0;
    std::vector<std::int64_t> programDelivered;
};

// One commodity market per resource. Everything here is public.
struct Market {
    Money price;                      // today's trading price
    Fixed demandMult;                 // random-walk demand factor around 1
    Fixed supplySmoothed;             // EMA of tonnes sold per day by all companies
    std::int64_t soldToday = 0;       // reset by the daily price update
    std::vector<Money> priceHistory;  // one entry per day, index = day
};

// Equal principal instalments plus interest on the remaining balance.
struct Loan {
    CompanyId company;
    Money principal;          // original amount
    Money remaining;          // principal still owed
    Money principalPerMonth;  // ceil(principal / term)
    Day takenDay = 0;
    std::uint32_t monthsRemaining = 0;
    std::uint8_t active = 1;
};

// Sealed bid. A company holds at most one bid per auction; a new bid replaces
// the old one. Ties resolve to the earlier (day, order).
struct Bid {
    CompanyId company;
    Money amount;
    Day day = 0;
    std::uint32_t order = 0;
};

struct Auction {
    PlotId plot;
    Day opensOnDay = 0;
    Day closesOnDay = 0;
    std::vector<Bid> bids;
    std::uint8_t open = 1;
    CompanyId winner;  // valid once closed with a sale
    Money winningAmount;
    // The minimum bid this auction opened with. Kept so the record of what
    // land sold for, and how far over the ask, survives later changes to the
    // plot (spec 006). Auctions are never erased, so this is the history.
    Money askPrice;
};

// A survey's noisy reading of one deposit. relativeError is the log-normal
// sigma used, so the viewer knows roughly how wide the error band is.
struct SurveyEstimate {
    ResourceId resource;
    std::int64_t tonnesEstimate = 0;
    Fixed relativeError;
};

// Surveys belong to the commissioning company and are never shown to others.
struct Survey {
    CompanyId company;
    PlotId plot;
    Day commissionedDay = 0;
    Day completesDay = 0;
    std::uint8_t complete = 0;
    std::vector<SurveyEstimate> estimates;  // filled on completion
};

enum class MineState : std::uint8_t {
    Building,    // capex paid, not yet producing; no opex
    Operating,   // producing, full opex
    Mothballed,  // not producing, reduced opex
    Depleted,    // every deposit exhausted; no opex
    Closed,      // owner went bankrupt; permanently shut, no opex
};

struct Mine {
    PlotId plot;
    CompanyId owner;
    Day builtDay = 0;
    Day operationalDay = 0;
    MineState state = MineState::Building;
    std::vector<std::int64_t> extractedTonnes;   // indexed by ResourceId, lifetime total
    std::vector<std::int64_t> lastDayTonnes;     // what came out on the last processed day
    std::vector<std::int64_t> lastOutputTonnes;  // output on the most recent operating day
    // The owner's survey that MineRunningLow has already been raised for
    // (spec 004): the warning is given once per survey, whatever the mine
    // does afterwards. Invalid until the first warning.
    SurveyId lowWarnedSurvey;
    // Consecutive losing days (spec 008): operating, with output to price,
    // and a loss at the price the mines step saw of at least the share of
    // the running cost set in BalanceParams::mineLossNoticeMinShare (spec
    // 010). Zero after any other day and while it is not operating.
    std::uint32_t lossDays = 0;
    // Set when the losing-mine notice is raised, so it is said once; cleared
    // when lossClearDays reaches BalanceParams::mineLossRearmDays or when
    // the mine stops operating.
    std::uint8_t lossAnnounced = 0;
    // Consecutive days that were not losing days since the notice (spec
    // 010). Counted only while lossAnnounced is set; a losing day ends the
    // run. Zero otherwise.
    std::uint32_t lossClearDays = 0;
};

// One research project. A company runs at most one at a time; the cost is
// paid when it starts and the tech joins completedTechs when it finishes.
struct Research {
    CompanyId company;
    TechId tech;
    Day startedDay = 0;
    Day completesDay = 0;
    std::uint8_t complete = 0;
};

// A world event in progress or finished. Public.
struct ActiveEvent {
    EventId event;
    Day startedDay = 0;
    Day endsDay = 0;
    std::uint8_t active = 1;
};

// How the race ended for one company (spec 010).
enum class ProgramResult : std::uint8_t {
    None,           // the game has not ended
    Founded,        // completed the last stage first
    LesserPartner,  // another company founded the program; this one was close enough to join
    Lost,
};

// How the game ended, or that it has not. Public. The sim sets it once and
// nothing changes afterwards (decision record 001 of spec 010).
struct Outcome {
    std::uint8_t ended = 0;
    Day day = 0;                         // the day the game ended; zero until it has
    CompanyId founder;                   // invalid when nobody founded the program
    std::vector<ProgramResult> results;  // indexed by CompanyId; None until the game has ended
};

// The complete dynamic state of a game. Everything that affects the future
// of the simulation lives here so that save, load, hashing and replay all
// operate on one struct. Entities are stored in vectors indexed by StrongId
// and are never erased; dead entities carry a flag instead.
struct WorldState {
    Day day = 0;
    RngStreams rng;
    std::vector<Company> companies;  // index 0 is the player
    std::vector<Plot> plots;
    std::vector<Auction> auctions;
    std::vector<Survey> surveys;
    std::vector<Mine> mines;
    std::vector<Market> markets;  // indexed by ResourceId
    std::vector<Loan> loans;
    std::vector<Research> research;
    std::vector<ActiveEvent> events;
    std::vector<PlotId> listingQueue;  // shuffled at generation; listed in order
    std::uint32_t nextListing = 0;     // index into listingQueue
    // Plots withdrawn after going unsold, oldest first. They return to sale
    // behind the never-listed plots, once their cooldown has passed.
    std::vector<PlotId> withdrawnQueue;
    std::uint32_t bidCounter = 0;  // monotonic order stamp for tie-breaks
    HiddenState hidden;
    Outcome outcome;
};

}  // namespace sim
