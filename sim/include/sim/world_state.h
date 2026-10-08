#pragma once

#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/random.h"
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
    std::uint8_t bankrupt = 0;
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
};

struct Mine {
    PlotId plot;
    CompanyId owner;
    Day builtDay = 0;
    Day operationalDay = 0;
    MineState state = MineState::Building;
    std::vector<std::int64_t> extractedTonnes;  // indexed by ResourceId, lifetime total
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
    std::vector<PlotId> listingQueue;  // shuffled at generation; listed in order
    std::uint32_t nextListing = 0;     // index into listingQueue
    std::uint32_t bidCounter = 0;      // monotonic order stamp for tie-breaks
    HiddenState hidden;
};

}  // namespace sim
