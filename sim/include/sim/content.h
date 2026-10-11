#pragma once

// Static game content: resources, tech tree, regions, events and balance
// numbers. Loaded once from data/*.json by the content library and never
// modified during play. The sim only sees these plain structs.

#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/sell_policy.h"
#include "sim/types.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sim {

struct ResourceDef {
    std::string id;
    std::string name;
    Money basePrice;  // per tonne
    std::int64_t baseDemandTonnesPerDay = 0;
    std::int64_t baseOutputTonnesPerDay = 0;  // one mine at tech level zero
    Fixed elasticity;                         // price response to supply/demand imbalance
    Fixed volatility;                         // daily demand random-walk step as a fraction
    Money storageCostPerTonneMonth;
    Money mineOpexPerDay;  // a mine producing this ore, per day, before multipliers (spec 008)
    TechId unlockedBy;     // invalid when available from the start
};

enum class TechEffectType : std::uint8_t {
    SurveyNoiseMult,
    ExtractionCapexMult,
    OpexMult,
    OutputMult,
    StorageCostMult,
    UnlockResource,
};

struct TechEffect {
    TechEffectType type = TechEffectType::SurveyNoiseMult;
    Fixed value;          // multiplier; unused for UnlockResource
    ResourceId resource;  // UnlockResource only
};

struct TechDef {
    std::string id;
    std::string name;         // from the prose catalogue (tech.<id>.name)
    std::string description;  // from the prose catalogue; not part of the content hash
    Money cost;
    Day durationDays = 0;
    // Program stages the company must have completed before this project may
    // start: 0 for none, otherwise 1 to 3 (spec 010).
    std::uint32_t requiresProgramStage = 0;
    std::vector<TechId> prerequisites;
    std::vector<TechEffect> effects;
};

// Log-normal prior for one resource in one region. tonnesSpread is the ratio
// between the 84th percentile and the median (e^sigma), so a spread of 2.5
// means roughly a third of deposits are 2.5x bigger or smaller than median.
// tonnesMaxMult caps a deposit at that multiple of the median, after the
// plot's area has scaled it (spec 006): the tail of the distribution is
// what made mines outlive the game. The default never binds (the draw is
// already clamped to three sigma), which is what in-code fixtures use.
struct DepositPrior {
    ResourceId resource;
    Fixed presenceProb;
    std::int64_t tonnesMedian = 0;
    Fixed tonnesSpread;
    Fixed tonnesMaxMult = Fixed::fromInt(1'000'000);
};

struct RegionDef {
    std::string id;
    std::string name;
    std::uint32_t plotCount = 0;
    Money landPriceMin;
    Money landPriceMax;
    std::int64_t plotAreaMinHectares = 0;
    std::int64_t plotAreaMaxHectares = 0;
    std::vector<DepositPrior> deposits;
};

enum class EventEffectType : std::uint8_t {
    DemandMult,     // one resource
    OpexMult,       // all mines
    LandPriceMult,  // new plot listings
};

struct EventEffect {
    EventEffectType type = EventEffectType::DemandMult;
    ResourceId resource;  // DemandMult only
    Fixed value;
};

struct EventDef {
    std::string id;
    std::string name;         // from the prose catalogue (event.<id>.name)
    std::string description;  // from the prose catalogue; not part of the content hash
    Fixed dailyWeight;        // probability of firing on any given day once eligible
    std::uint32_t minYear = 1;
    Day durationDays = 0;
    std::vector<EventEffect> effects;
};

struct BalanceParams {
    Money startingCapital;
    Fixed loanAnnualRate;
    std::uint32_t loanTermMonths = 0;
    Money loanMaxPrincipal;
    Money loanMinPrincipal;
    std::uint32_t maxActiveLoans = 0;
    Money surveyCost;
    Day surveyDurationDays = 0;
    Fixed surveyBaseNoise;  // relative standard deviation of tonnage estimates
    Money mineCapexPerHectare;
    Day mineBuildDays = 0;
    Fixed mineMothballOpexMult;  // share of opex a mothballed mine still pays
    Day auctionWindowDays = 0;
    std::uint32_t plotsListedAtStart = 0;
    std::uint32_t plotsListedPerMonth = 0;
    // The opening hand (spec 005): of the plots listed at the start, at
    // least this many come from regions a new company can mine on day one,
    // and at most this many from regions whose main ore is locked. Both are
    // promises about regions and odds, never about hidden deposits.
    std::uint32_t openingMinMineable = 0;
    std::uint32_t openingMaxLockedRegion = 0;
    // A region counts as mineable on day one when an ore that needs no
    // research turns up on at least this share of its plots.
    Fixed openingMineablePresence;
    // Land nobody wants (spec 004): after this many auctions in a row
    // without a sale a plot is withdrawn, and may be listed again only after
    // the cooldown. Zero rounds means plots relist forever.
    std::uint32_t maxUnsoldRounds = 0;
    Day relistCooldownDays = 0;
    std::uint32_t bankruptcyGraceMonths = 0;
    // Consecutive losing days before a mine's owner is told (spec 008).
    std::uint32_t mineLossNoticeDays = 0;
    // Spec 010 (BR-014). A day is a losing day for that notice only when the
    // loss is at least this share of the mine's running cost that day; zero
    // counts every loss.
    Fixed mineLossNoticeMinShare;
    // After a notice, the consecutive days that are not losing days a mine
    // must have before it can be told again.
    std::uint32_t mineLossRearmDays = 0;
    // Commodity market dynamics (see DESIGN.md, step 9).
    std::uint32_t marketSupplySmoothingDays = 0;  // EMA window for company supply
    std::uint32_t marketPriceAdjustDays = 0;      // days for price to close most of the gap
    Fixed demandReversionRate;                    // daily pull of the demand walk toward 1
    Fixed demandMultMin;
    Fixed demandMultMax;
    Fixed priceFloorMult;  // relative to base price
    Fixed priceCapMult;
    Fixed demandGrowthPerYear;  // compound growth of every resource's base demand
    // Spec 008: the market's two clamps, as data. Demand over supply is held
    // inside the ratio pair before the power law; the combined demand factor
    // (walk x growth x events) is held inside the factor pair.
    Fixed marketRatioMin;
    Fixed marketRatioMax;
    Fixed demandFactorMin;
    Fixed demandFactorMax;
    // Standing sell orders every company starts with (spec 001).
    SellPolicy defaultSellPolicy;
    bool defaultCoverCosts = false;
};

// The built-in competitor's tuning (data/ai.json, spec 006). The controller
// runs inside the deterministic sim, so these are content like any price:
// they are hashed into the content fingerprint, and a changed knob
// invalidates saves and traces.
struct AiParams {
    // Selling stock by hand (the AI does not rely on its sell policy).
    Fixed sellAboveBase;     // sell when the price is at least this share of base
    Fixed dumpStorageShare;  // ...or a month's storage exceeds this share of the stock's value
    // Valuing a surveyed plot.
    Fixed recoverableShare;  // haircut on surveyed tonnage
    Fixed requiredReturn;    // net value must cover the outlay times this
    // The same two for a company that has no mine yet: easier, so the first
    // purchase can happen at the opening ask.
    Fixed firstMineRecoverableShare;
    Fixed firstMineRequiredReturn;
    // How much of the market's response to the company's own supply the
    // valuation counts: 0 values ore at today's price whatever the company
    // already sells, 1 applies the market's elasticity rule in full.
    Fixed supplyDiscount;
    std::int64_t paybackHorizonDays = 0;  // value at most this many days of output
    // Bidding.
    Fixed bidPremium;                    // over the ask, for contested or clearly good plots
    Fixed bidPremiumHigh;                // over the ask, for plots worth four times their cost
    std::uint32_t maxOpenProspects = 0;  // surveyed-but-unbought listed plots at once
    // Running mines.
    Fixed reopenMargin;                // reopen a mothballed mine at this multiple of opex
    Day idleMineGraceDays = 0;         // a new mine is not mothballed before this
    std::int64_t opexReserveDays = 0;  // opex kept in hand per mine being built
    // Cash and debt.
    Money cashReserve;    // never committed
    Money researchFloor;  // net cash needed before any research
    Money loanFloor;      // borrow when producing with less cash than this
    Money loanChunk;      // size of an ordinary loan
    Money bootstrapLoan;  // size of the one loan taken to buy a first plot
    // Racing for the program (spec 010, BR-011).
    // Share of the stock of a good the current stage still needs that is
    // delivered instead of offered for sale: 1 keeps everything needed, 0
    // sells it all and never delivers.
    Fixed programReserveShare;
    // Spendable cash kept for operations: a program project (a stage's, or
    // one that unlocks a needed ore) starts only if this much is left after
    // it, and with less than this, plus the cost of such a project while one
    // is waiting to start, the company sells rather than delivers.
    Money programCashBuffer;
    // Multiplier, at least 1, on the surveyed value of an ore a current or
    // coming stage needs and the company's mines will not supply in time.
    Fixed programOreLandWeight;
    // Share of the current stage's deliveries met from which its project is
    // started ahead of every ordinary project.
    Fixed programResearchProgress;
};

// One good a program stage needs, in whole tonnes of the company's own
// production (spec 010).
struct ProgramRequirement {
    ResourceId resource;
    std::int64_t tonnes = 0;
};

// One stage of the space program: cash, one research project and goods. The
// project is the tech whose requiresProgramStage equals this stage's
// position, counting from zero.
struct ProgramStage {
    std::string id;
    Money cost;
    TechId research;
    std::vector<ProgramRequirement> requirements;  // "requires" in the data file
};

// The race (data/program.json, spec 010): the day the game ends if nobody has
// founded the program, and the program's stages in the order a company must
// complete them. The loader guarantees exactly three stages; in-code content
// may leave the program empty.
struct ProgramDef {
    Day deadlineDay = 0;
    std::vector<ProgramStage> stages;
};

struct Content {
    std::vector<ResourceDef> resources;
    std::vector<TechDef> techs;
    std::vector<RegionDef> regions;
    std::vector<EventDef> events;
    BalanceParams balance;
    AiParams ai;
    ProgramDef program;
    // Every sentence a player reads, keyed as in docs/specs/002-prose-catalogue.md
    // (data/strings.json). Prose only: it is not serialized and not part of
    // the content hash, so a wording fix never invalidates a save. The sim
    // never reads it; the client looks keys up through game::text.
    std::map<std::string, std::string, std::less<>> strings;  // heterogeneous lookup by view

    // Lookups by string id. Return an invalid id when absent.
    [[nodiscard]] ResourceId findResource(std::string_view id) const;
    [[nodiscard]] TechId findTech(std::string_view id) const;
    [[nodiscard]] RegionId findRegion(std::string_view id) const;
    [[nodiscard]] EventId findEvent(std::string_view id) const;
};

}  // namespace sim
