#pragma once

// In-code content fixture shared by sim tests. Independent of data/*.json so
// the golden determinism hash does not move when balance numbers are tuned.

#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/simulation.h"

#include <cstdint>

namespace test {

inline sim::Content makeContent() {
    sim::Content c;

    sim::ResourceDef iron;
    iron.id = "iron";
    iron.name = "Iron";
    iron.basePrice = sim::Money::fromUnits(60);
    iron.baseDemandTonnesPerDay = 800;
    iron.baseOutputTonnesPerDay = 400;
    iron.elasticity = sim::Fixed::fromRatio(6, 10);
    iron.volatility = sim::Fixed::fromRatio(2, 100);
    iron.storageCostPerTonneMonth = sim::Money::fromCents(150);
    c.resources.push_back(iron);

    sim::ResourceDef copper;
    copper.id = "copper";
    copper.name = "Copper";
    copper.basePrice = sim::Money::fromUnits(900);
    copper.baseDemandTonnesPerDay = 100;
    copper.baseOutputTonnesPerDay = 60;
    copper.elasticity = sim::Fixed::fromRatio(8, 10);
    copper.volatility = sim::Fixed::fromRatio(3, 100);
    copper.storageCostPerTonneMonth = sim::Money::fromCents(250);
    c.resources.push_back(copper);

    sim::ResourceDef lithium;
    lithium.id = "lithium";
    lithium.name = "Lithium";
    lithium.basePrice = sim::Money::fromUnits(14000);
    lithium.baseDemandTonnesPerDay = 8;
    lithium.baseOutputTonnesPerDay = 5;
    lithium.elasticity = sim::Fixed::fromRatio(11, 10);
    lithium.volatility = sim::Fixed::fromRatio(5, 100);
    lithium.storageCostPerTonneMonth = sim::Money::fromUnits(8);
    lithium.unlockedBy = sim::TechId(1);
    c.resources.push_back(lithium);

    sim::TechDef survey;
    survey.id = "seismic_1";
    survey.name = "Seismic I";
    survey.cost = sim::Money::fromUnits(600'000);
    survey.durationDays = 90;
    survey.effects.push_back(
        {sim::TechEffectType::SurveyNoiseMult, sim::Fixed::fromRatio(8, 10), sim::ResourceId()});
    c.techs.push_back(survey);

    sim::TechDef lithiumTech;
    lithiumTech.id = "lithium_processing";
    lithiumTech.name = "Lithium Processing";
    lithiumTech.cost = sim::Money::fromUnits(4'000'000);
    lithiumTech.durationDays = 240;
    lithiumTech.prerequisites.push_back(sim::TechId(0));
    lithiumTech.effects.push_back(
        {sim::TechEffectType::UnlockResource, sim::Fixed::zero(), sim::ResourceId(2)});
    c.techs.push_back(lithiumTech);

    sim::RegionDef north;
    north.id = "north";
    north.name = "North";
    north.plotCount = 20;
    north.landPriceMin = sim::Money::fromUnits(800'000);
    north.landPriceMax = sim::Money::fromUnits(2'500'000);
    north.plotAreaMinHectares = 200;
    north.plotAreaMaxHectares = 1200;
    north.deposits.push_back(
        {sim::ResourceId(0), sim::Fixed::fromRatio(8, 10), 700'000, sim::Fixed::fromRatio(5, 2)});
    north.deposits.push_back(
        {sim::ResourceId(1), sim::Fixed::fromRatio(3, 10), 90'000, sim::Fixed::fromRatio(5, 2)});
    c.regions.push_back(north);

    // Fixed plot area so deposit sizes can be checked against the prior.
    sim::RegionDef flats;
    flats.id = "flats";
    flats.name = "Flats";
    flats.plotCount = 10;
    flats.landPriceMin = sim::Money::fromUnits(400'000);
    flats.landPriceMax = sim::Money::fromUnits(1'500'000);
    flats.plotAreaMinHectares = 500;
    flats.plotAreaMaxHectares = 500;
    flats.deposits.push_back(
        {sim::ResourceId(2), sim::Fixed::fromRatio(1, 2), 6'000, sim::Fixed::fromInt(3)});
    c.regions.push_back(flats);

    sim::EventDef boom;
    boom.id = "boom";
    boom.name = "Boom";
    boom.dailyWeight = sim::Fixed::fromRatio(6, 10'000);
    boom.minYear = 2;
    boom.durationDays = 180;
    boom.effects.push_back(
        {sim::EventEffectType::DemandMult, sim::ResourceId(2), sim::Fixed::fromRatio(16, 10)});
    c.events.push_back(boom);

    c.balance.startingCapital = sim::Money::fromUnits(5'000'000);
    c.balance.loanAnnualRate = sim::Fixed::fromRatio(8, 100);
    c.balance.loanTermMonths = 60;
    c.balance.loanMaxPrincipal = sim::Money::fromUnits(10'000'000);
    c.balance.loanMinPrincipal = sim::Money::fromUnits(250'000);
    c.balance.maxActiveLoans = 4;
    c.balance.surveyCost = sim::Money::fromUnits(150'000);
    c.balance.surveyDurationDays = 20;
    c.balance.surveyBaseNoise = sim::Fixed::fromRatio(1, 2);
    c.balance.mineCapexPerHectare = sim::Money::fromUnits(4'000);
    c.balance.mineBuildDays = 180;
    c.balance.mineOpexPerDay = sim::Money::fromUnits(8'000);
    c.balance.mineMothballOpexMult = sim::Fixed::fromRatio(3, 20);
    c.balance.auctionWindowDays = 45;
    c.balance.plotsListedAtStart = 3;
    c.balance.plotsListedPerMonth = 1;
    c.balance.bankruptcyGraceMonths = 3;
    c.balance.marketSupplySmoothingDays = 30;
    c.balance.marketPriceAdjustDays = 10;
    c.balance.demandReversionRate = sim::Fixed::fromRatio(1, 100);
    c.balance.demandMultMin = sim::Fixed::fromRatio(3, 10);
    c.balance.demandMultMax = sim::Fixed::fromInt(3);
    c.balance.priceFloorMult = sim::Fixed::fromRatio(1, 10);
    c.balance.priceCapMult = sim::Fixed::fromInt(10);
    c.balance.demandGrowthPerYear = sim::Fixed::fromRatio(4, 100);
    return c;
}

inline sim::GameSetup setup() {
    sim::GameSetup s;
    s.playerName = "Player";
    s.aiNames = {"Rival"};
    return s;
}

}  // namespace test
