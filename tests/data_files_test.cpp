// Loads the real data/ files so that a typo in game content fails CI.
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/money.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/world_state.h"
#include "sim/worldgen.h"

#include "content/loader.h"
#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

TEST_CASE("Shipped data files load cleanly", "[content][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;

    CHECK(c.resources.size() == 4);
    CHECK(c.techs.size() == 13);  // ten improvements and the three stage projects (spec 010)
    CHECK(c.regions.size() == 3);
    CHECK(c.events.size() == 4);

    std::uint32_t plots = 0;
    for (const sim::RegionDef& region : c.regions) {
        plots += region.plotCount;
    }
    CHECK(plots == 120);  // spec 005: 40 per region

    CHECK(c.findResource("lithium").valid());
    CHECK(c.resources[c.findResource("lithium").index()].unlockedBy ==
          c.findTech("lithium_processing"));
}

TEST_CASE("Shipped research tree holds the three program stage projects",
          "[content][data][spec010]") {
    // Spec 010, step 1: each stage of the program needs its own research
    // project, and a project cannot start before the stage ahead of it is
    // complete. The projects change nothing by themselves, so they carry no
    // effects. Costs and durations are provisional until the race is tuned.
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;

    struct StageProject {
        std::string_view id;
        std::uint32_t requiresStage;
        std::int64_t cost;
        sim::Day durationDays;
    };
    constexpr std::array<StageProject, 3> kProjects = {{
        {"program_stage_1", 0, 2'000'000, 240},
        {"program_stage_2", 1, 6'000'000, 360},
        {"program_stage_3", 2, 12'000'000, 480},
    }};
    for (const StageProject& project : kProjects) {
        INFO(project.id);
        const sim::TechId id = c.findTech(project.id);
        REQUIRE(id.valid());
        const sim::TechDef& tech = c.techs[id.index()];
        CHECK(tech.requiresProgramStage == project.requiresStage);
        CHECK(tech.cost == sim::Money::fromUnits(project.cost));
        CHECK(tech.durationDays == project.durationDays);
        CHECK(tech.effects.empty());
        CHECK(tech.prerequisites.empty());
    }
}

TEST_CASE("Shipped program has the deadline and the three provisional stages",
          "[content][data][spec010]") {
    // Spec 010, step 1 (BR-001, BR-002, AC-001): the race ends on day 10,800,
    // thirty game years in. Each stage needs cash, its own research project
    // and ore the company mined itself; only the third needs lithium, the ore
    // behind research. The costs and tonnages are provisional until the race
    // is tuned (task 016), so this pins what ships, not what the loader allows.
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;

    CHECK(c.program.deadlineDay == 10800);
    CHECK(c.program.deadlineDay == 30 * sim::kDaysPerYear);

    struct Need {
        std::string_view resource;
        std::int64_t tonnes;
    };
    struct Stage {
        std::string_view id;
        std::int64_t cost;
        std::string_view research;
        std::vector<Need> needs;
    };
    const std::array<Stage, 3> kStages = {{
        {"stage_1", 5'000'000, "program_stage_1", {{"iron", 60'000}, {"copper", 8'000}}},
        {"stage_2",
         15'000'000,
         "program_stage_2",
         {{"iron", 150'000}, {"copper", 20'000}, {"nickel", 6'000}}},
        {"stage_3",
         30'000'000,
         "program_stage_3",
         {{"iron", 200'000}, {"copper", 30'000}, {"nickel", 12'000}, {"lithium", 2'000}}},
    }};
    REQUIRE(c.program.stages.size() == kStages.size());
    std::size_t i = 0;  // the stage's position, counting from zero
    for (const Stage& expected : kStages) {
        const sim::ProgramStage& stage = c.program.stages[i];
        INFO(expected.id);
        CHECK(stage.id == expected.id);
        CHECK(stage.cost == sim::Money::fromUnits(expected.cost));
        REQUIRE(c.findTech(expected.research).valid());
        CHECK(stage.research == c.findTech(expected.research));
        CHECK(c.techs[stage.research.index()].requiresProgramStage == i);
        REQUIRE(stage.requirements.size() == expected.needs.size());
        for (std::size_t r = 0; r < expected.needs.size(); ++r) {
            INFO(expected.needs[r].resource);
            REQUIRE(c.findResource(expected.needs[r].resource).valid());
            CHECK(stage.requirements[r].resource == c.findResource(expected.needs[r].resource));
            CHECK(stage.requirements[r].tonnes == expected.needs[r].tonnes);
        }
        ++i;
    }
}

TEST_CASE("Shipped ores break even at the chosen share of base price", "[content][data][spec008]") {
    // Spec 008 (BR-003, AC-001): the share of an ore's base price a mine at
    // base output must earn to cover its running cost (cost per day / base
    // output per day / base price) is chosen, not accidental: 0.5 to 0.7 for
    // iron, copper and nickel, and 0.4 to 0.6 for lithium, the late ore that
    // pays a premium. A range on the shipped files only: the loader accepts
    // any positive cost, so experiments with --data stay possible.
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;
    for (const sim::ResourceDef& resource : c.resources) {
        const sim::Money revenue = resource.basePrice * resource.baseOutputTonnesPerDay;
        INFO(resource.id);
        REQUIRE(revenue > sim::Money::zero());
        const sim::Fixed share = resource.mineOpexPerDay.ratioTo(revenue);
        INFO("break-even share of base price " << sim::toString(share));
        const std::int64_t lowestTenths = resource.id == "lithium" ? 4 : 5;
        CHECK(share >= sim::Fixed::fromRatio(lowestTenths, 10));
        CHECK(share <= sim::Fixed::fromRatio(lowestTenths + 2, 10));
    }
}

TEST_CASE("Shipped iron earns the least per mine at base", "[content][data]") {
    // Spec 006: every other ore earns more per mine than iron, so iron is
    // the floor and the Copper Belt stays the prize.
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;
    const sim::ResourceDef& iron = c.resources[c.findResource("iron").index()];
    const sim::Money revenue = iron.basePrice * iron.baseOutputTonnesPerDay;
    for (const sim::ResourceDef& resource : c.resources) {
        CHECK((resource.basePrice * resource.baseOutputTonnesPerDay) >= revenue);
    }
}

TEST_CASE("Shipped deposits are capped at four medians", "[content][data][worldgen]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;
    for (const sim::RegionDef& region : c.regions) {
        for (const sim::DepositPrior& prior : region.deposits) {
            CHECK(prior.tonnesMaxMult == sim::Fixed::fromInt(4));
        }
    }
    for (sim::Seed seed = 1; seed <= 20; ++seed) {
        const sim::Simulation simulation(c, seed, test::setup());
        const sim::WorldState& state = simulation.state();
        for (std::size_t p = 0; p < state.plots.size(); ++p) {
            const sim::RegionDef& region = c.regions[state.plots[p].region.index()];
            for (const sim::DepositTruth& truth : state.hidden.plotDeposits[p].deposits) {
                for (const sim::DepositPrior& prior : region.deposits) {
                    if (prior.resource == truth.resource) {
                        CHECK(truth.tonnesTotal <= prior.tonnesMedian * 4);
                    }
                }
            }
        }
    }
}

TEST_CASE("The shipped competitor's knobs are the tuned ones", "[content][data][ai]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::AiParams& ai = result.content->ai;
    CHECK(ai.supplyDiscount == sim::Fixed::one());
    CHECK(ai.firstMineRequiredReturn <= ai.requiredReturn);
    CHECK(ai.firstMineRecoverableShare >= ai.recoverableShare);
    CHECK(result.content->balance.openingMineablePresence == sim::Fixed::fromRatio(1, 2));
}

TEST_CASE("A world generates from the shipped data", "[content][data][worldgen]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Simulation simulation(*result.content, 1, test::setup());
    const sim::WorldState& state = simulation.state();
    CHECK(state.plots.size() == 120);
    std::size_t depositsTotal = 0;
    for (const sim::PlotDeposits& deposits : state.hidden.plotDeposits) {
        depositsTotal += deposits.deposits.size();
        for (const sim::DepositTruth& truth : deposits.deposits) {
            CHECK(truth.tonnesTotal >= 1);
        }
    }
    // Priors sum to roughly 1 deposit per plot (1.1, 1.3 and 0.7 by region).
    CHECK(depositsTotal > 85);
    CHECK(depositsTotal < 165);
}

TEST_CASE("Shipped regions give every seed a fair opening hand", "[content][data][worldgen]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;
    // The promise the spec makes to the player: Salt Flats is the locked
    // region, the other two are mineable from day one.
    const auto kindOf = [&c](std::string_view id) {
        for (const sim::RegionDef& region : c.regions) {
            if (region.id == id) {
                return sim::classifyRegionAtStart(c, region);
            }
        }
        return sim::RegionStart::Other;
    };
    CHECK(kindOf("northern_shield") == sim::RegionStart::Mineable);
    CHECK(kindOf("copper_belt") == sim::RegionStart::Mineable);
    CHECK(kindOf("salt_flats") == sim::RegionStart::Locked);
    REQUIRE(c.balance.plotsListedAtStart >= c.balance.openingMinMineable);

    for (sim::Seed seed = 1; seed <= 200; ++seed) {
        const sim::Simulation simulation(c, seed, test::setup());
        const sim::WorldState& state = simulation.state();
        std::uint32_t mineable = 0;
        std::uint32_t locked = 0;
        std::uint32_t listed = 0;
        for (const sim::Plot& plot : state.plots) {
            if (plot.listing != sim::PlotListing::Listed) {
                continue;
            }
            ++listed;
            const sim::RegionStart kind =
                sim::classifyRegionAtStart(c, c.regions[plot.region.index()]);
            mineable += kind == sim::RegionStart::Mineable ? 1U : 0U;
            locked += kind == sim::RegionStart::Locked ? 1U : 0U;
        }
        CHECK(listed == c.balance.plotsListedAtStart);
        CHECK(mineable >= c.balance.openingMinMineable);
        CHECK(locked <= c.balance.openingMaxLockedRegion);
        // After the hand no region supplies three listings in a row, except
        // a forced run when the tail of the queue is one region.
        const auto regionOf = [&state](std::size_t i) {
            return state.plots[state.listingQueue[i].index()].region;
        };
        // The last plots of the hand count as predecessors, so start at the
        // first monthly listing.
        const std::size_t hand = c.balance.plotsListedAtStart;
        for (std::size_t i = std::max<std::size_t>(hand, 2); i < state.listingQueue.size(); ++i) {
            if (regionOf(i) != regionOf(i - 1) || regionOf(i) != regionOf(i - 2)) {
                continue;
            }
            for (std::size_t j = i; j < state.listingQueue.size(); ++j) {
                CHECK(regionOf(j) == regionOf(i));
            }
        }
    }
}
