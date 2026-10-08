#pragma once

// Static game content: resources, tech tree, regions, events and balance
// numbers. Loaded once from data/*.json by the content library and never
// modified during play. The sim only sees these plain structs.

#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"

#include <cstdint>
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
    TechId unlockedBy;  // invalid when available from the start
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
    std::string name;
    Money cost;
    Day durationDays = 0;
    std::vector<TechId> prerequisites;
    std::vector<TechEffect> effects;
};

// Log-normal prior for one resource in one region. tonnesSpread is the ratio
// between the 84th percentile and the median (e^sigma), so a spread of 2.5
// means roughly a third of deposits are 2.5x bigger or smaller than median.
struct DepositPrior {
    ResourceId resource;
    Fixed presenceProb;
    std::int64_t tonnesMedian = 0;
    Fixed tonnesSpread;
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
    std::string name;
    Fixed dailyWeight;  // probability of firing on any given day once eligible
    std::uint32_t minYear = 1;
    Day durationDays = 0;
    std::vector<EventEffect> effects;
};

struct BalanceParams {
    Money startingCapital;
    Fixed loanAnnualRate;
    std::uint32_t loanTermMonths = 0;
    Money loanMaxPrincipal;
    Money surveyCost;
    Day surveyDurationDays = 0;
    Fixed surveyBaseNoise;  // relative standard deviation of tonnage estimates
    Money mineCapexPerHectare;
    Day mineBuildDays = 0;
    Money mineOpexPerDay;
    Day auctionWindowDays = 0;
    std::uint32_t plotsListedPerMonth = 0;
    std::uint32_t bankruptcyGraceMonths = 0;
};

struct Content {
    std::vector<ResourceDef> resources;
    std::vector<TechDef> techs;
    std::vector<RegionDef> regions;
    std::vector<EventDef> events;
    BalanceParams balance;

    // Lookups by string id. Return an invalid id when absent.
    [[nodiscard]] ResourceId findResource(std::string_view id) const;
    [[nodiscard]] TechId findTech(std::string_view id) const;
    [[nodiscard]] RegionId findRegion(std::string_view id) const;
    [[nodiscard]] EventId findEvent(std::string_view id) const;
};

}  // namespace sim
