#include "sim/content.h"

#include "sim/fixed.h"
#include "sim/money.h"
#include "sim/save.h"

#include "content/loader.h"
#include "test_content.h"
#include "test_strings.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

// Minimal valid content. Tests mutate copies of this to provoke errors.
content::Sources goodSources() {
    content::Sources s;
    s.resources = R"([
      {"id": "iron", "name": "Iron", "base_price": 110, "base_demand_tonnes_per_day": 800,
       "base_output_tonnes_per_day": 400, "elasticity": 0.6, "volatility": 0.02, "storage_cost_per_tonne_month": 1.5,
       "mine_opex_per_day": 9000, "unlocked_by": null},
      {"id": "lithium", "name": "Lithium", "base_price": 14000.25,
       "base_demand_tonnes_per_day": 8, "base_output_tonnes_per_day": 5, "elasticity": 1.1, "volatility": 0.05,
       "storage_cost_per_tonne_month": 8, "mine_opex_per_day": 7000.5,
       "unlocked_by": "lithium_processing"}
    ])";
    s.techs = R"([
      {"id": "seismic_1", "cost": 600000, "duration_days": 90,
       "requires": [], "effects": [{"type": "survey_noise_mult", "value": 0.8}]},
      {"id": "lithium_processing", "cost": 4000000, "duration_days": 240,
       "requires": ["seismic_1"], "effects": [{"type": "unlock_resource", "resource": "lithium"}]},
      {"id": "program_stage_1", "cost": 5000000, "duration_days": 120,
       "requires_program_stage": 0, "effects": []},
      {"id": "program_stage_2", "cost": 6000000, "duration_days": 150,
       "requires_program_stage": 1, "effects": []},
      {"id": "program_stage_3", "cost": 7000000, "duration_days": 210,
       "requires_program_stage": 2, "effects": []}
    ])";
    s.regions = R"([
      {"id": "north", "name": "North", "plot_count": 10,
       "land_price": {"min": 800000, "max": 2500000},
       "plot_area_hectares": {"min": 200, "max": 1200},
       "deposits": {"iron": {"presence_prob": 0.8, "tonnes_median": 700000, "tonnes_spread": 2.5,
                           "tonnes_max_mult": 4}}}
    ])";
    s.events = R"([
      {"id": "boom", "daily_weight": 0.002, "min_year": 2, "duration_days": 180,
       "effects": [{"type": "demand_mult", "resource": "lithium", "value": 1.6},
                   {"type": "opex_mult", "value": 1.3}]}
    ])";
    s.balance = R"({
      "starting_capital": 5000000, "loan_annual_rate": 0.08, "loan_term_months": 60,
      "loan_max_principal": 10000000, "loan_min_principal": 250000, "max_active_loans": 4, "survey_cost": 150000, "survey_duration_days": 45,
      "survey_base_noise": 0.5, "mine_capex_per_hectare": 4000, "mine_build_days": 180,
      "mine_mothball_opex_mult": 0.2,
      "auction_window_days": 30, "plots_listed_at_start": 3, "plots_listed_per_month": 1,
      "opening_min_mineable": 2, "opening_max_locked_region": 1,
      "opening_mineable_presence": 0.5,
      "max_unsold_rounds": 3, "relist_cooldown_days": 360,
      "bankruptcy_grace_months": 3, "market_supply_smoothing_days": 30,
      "market_price_adjust_days": 10, "demand_reversion_rate": 0.01, "demand_mult_min": 0.3,
      "demand_mult_max": 3, "price_floor_mult": 0.1, "price_cap_mult": 10,
      "demand_growth_per_year": 0.04,
      "market_ratio_min": 0.2, "market_ratio_max": 5,
      "demand_factor_min": 0.01, "demand_factor_max": 1000,
      "mine_loss_notice_days": 5,
      "mine_loss_notice_min_share": 0.05, "mine_loss_rearm_days": 30,
      "storage_warning_cash_share": 0.1,
      "default_sell_policy": {"enabled": true, "reserve_tonnes": 0, "floor_price": 0},
      "default_cover_costs": true
    })";
    s.ai = R"({
      "sell_above_base": 0.85, "dump_storage_share": 0.02, "recoverable_share": 0.5,
      "required_return": 1.2, "payback_horizon_days": 1800, "bid_premium": 1.1,
      "bid_premium_high": 1.25, "max_open_prospects": 2, "reopen_margin": 1.25,
      "idle_mine_grace_days": 30, "opex_reserve_days": 60, "cash_reserve": 500000,
      "research_floor": 2500000, "loan_floor": 750000, "loan_chunk": 2000000,
      "bootstrap_loan": 4000000, "first_mine_recoverable_share": 0.6,
      "first_mine_required_return": 1.1, "supply_discount": 1,
      "program_reserve_share": 0.75, "program_cash_buffer": 1500000,
      "program_ore_land_weight": 1.5, "program_research_progress": 0.5
    })";
    s.program = R"({
      "deadline_day": 10800,
      "program_pace_warning_days": 360,
      "stages": [
        {"id": "stage_1", "cost": 1000000, "research": "program_stage_1",
         "requires": [{"resource": "iron", "tonnes": 1000}]},
        {"id": "stage_2", "cost": 2000000.5, "research": "program_stage_2",
         "requires": [{"resource": "iron", "tonnes": 2000}]},
        {"id": "stage_3", "cost": 3000000, "research": "program_stage_3",
         "requires": [{"resource": "iron", "tonnes": 3000}, {"resource": "lithium", "tonnes": 50}]}
      ]
    })";
    s.strings =
        test::fixtureStrings({"boom"}, {"seismic_1", "lithium_processing", "program_stage_1",
                                        "program_stage_2", "program_stage_3"});
    return s;
}

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

bool hasError(const content::LoadResult& result, std::string_view file, std::string_view path,
              std::string_view messagePart) {
    return std::any_of(result.errors.begin(), result.errors.end(), [&](const auto& e) {
        return e.file == file && e.path == path && e.message.find(messagePart) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("Valid content loads with resolved references and converted numbers", "[content]") {
    const content::LoadResult result = content::loadFromStrings(goodSources());
    REQUIRE(result.ok());
    INFO(content::formatErrors(result.errors));
    const sim::Content& c = *result.content;

    REQUIRE(c.resources.size() == 2);
    CHECK(c.resources[0].id == "iron");
    CHECK(c.resources[0].basePrice == sim::Money::fromUnits(110));
    CHECK(c.resources[0].elasticity == sim::Fixed::fromRatio(6, 10));
    CHECK(c.resources[0].storageCostPerTonneMonth == sim::Money::fromCents(150));
    CHECK(c.resources[0].baseOutputTonnesPerDay == 400);
    CHECK_FALSE(c.resources[0].unlockedBy.valid());
    CHECK(c.resources[1].basePrice == sim::Money::fromCents(1'400'025));
    CHECK(c.resources[1].unlockedBy == c.findTech("lithium_processing"));

    REQUIRE(c.techs.size() == 5);  // two improvements and the three stage projects
    CHECK(c.techs[1].prerequisites == std::vector<sim::TechId>{c.findTech("seismic_1")});
    REQUIRE(c.techs[1].effects.size() == 1);
    CHECK(c.techs[1].effects[0].type == sim::TechEffectType::UnlockResource);
    CHECK(c.techs[1].effects[0].resource == c.findResource("lithium"));
    CHECK(c.techs[0].effects[0].value == sim::Fixed::fromRatio(8, 10));
    CHECK(c.techs[0].durationDays == 90);

    REQUIRE(c.regions.size() == 1);
    CHECK(c.regions[0].plotCount == 10);
    CHECK(c.regions[0].landPriceMax == sim::Money::fromUnits(2'500'000));
    REQUIRE(c.regions[0].deposits.size() == 1);
    CHECK(c.regions[0].deposits[0].resource == c.findResource("iron"));
    CHECK(c.regions[0].deposits[0].tonnesSpread == sim::Fixed::fromRatio(5, 2));

    REQUIRE(c.events.size() == 1);
    CHECK(c.events[0].minYear == 2);
    REQUIRE(c.events[0].effects.size() == 2);
    CHECK(c.events[0].effects[0].resource == c.findResource("lithium"));
    CHECK_FALSE(c.events[0].effects[1].resource.valid());

    CHECK(c.balance.startingCapital == sim::Money::fromUnits(5'000'000));
    CHECK(c.balance.loanAnnualRate == sim::Fixed::fromRatio(8, 100));
    CHECK(c.balance.bankruptcyGraceMonths == 3);
    CHECK_FALSE(c.findResource("gold").valid());
}

TEST_CASE("Invalid JSON is reported per file", "[content]") {
    content::Sources s = goodSources();
    s.techs = "[ {";
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "techs.json", "", "invalid JSON"));
}

TEST_CASE("Unknown keys and missing required keys are reported with paths", "[content]") {
    content::Sources s = goodSources();
    s.resources = replaceOnce(s.resources, R"("name": "Iron",)", R"("nmae": "Iron",)");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "resources.json", "[0].nmae", "unknown key"));
    CHECK(hasError(result, "resources.json", "[0].name", "missing required key"));
}

TEST_CASE("Wrong types and out-of-range values are reported", "[content]") {
    content::Sources s = goodSources();
    s.resources = replaceOnce(s.resources, R"("base_price": 110)", R"("base_price": "110")");
    s.resources = replaceOnce(s.resources, R"("volatility": 0.02)", R"("volatility": 1.5)");
    s.balance = replaceOnce(s.balance, R"("loan_term_months": 60)", R"("loan_term_months": 60.0)");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "resources.json", "[0].base_price", "expected a number"));
    CHECK(hasError(result, "resources.json", "[0].volatility", "must be between"));
    CHECK(hasError(result, "balance.json", "loan_term_months", "expected an integer"));
}

TEST_CASE("Numbers with too much precision or out of range are rejected", "[content]") {
    content::Sources s = goodSources();
    s.resources = replaceOnce(s.resources, R"("base_price": 110)", R"("base_price": 110.005)");
    s.resources = replaceOnce(s.resources, R"("elasticity": 0.6)", R"("elasticity": 0.1234567)");
    s.balance = replaceOnce(s.balance, R"("survey_cost": 150000)", R"("survey_cost": 1e999)");
    s.resources =
        replaceOnce(s.resources, R"("base_price": 14000.25)", R"("base_price": 200000000000)");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "resources.json", "[0].base_price", "2 decimal places"));
    CHECK(hasError(result, "resources.json", "[0].elasticity", "6 decimal places"));
    CHECK(hasError(result, "resources.json", "[1].base_price", "magnitude"));
    // nlohmann rejects 1e999 while parsing; the loader's own finite check is a
    // second line of defence that JSON text cannot normally reach.
    CHECK(hasError(result, "balance.json", "", "number overflow"));
}

TEST_CASE("Unknown references are reported", "[content]") {
    content::Sources s = goodSources();
    s.techs = replaceOnce(s.techs, R"("requires": ["seismic_1"])", R"("requires": ["magic"])");
    s.regions =
        replaceOnce(s.regions, R"("iron": {"presence_prob")", R"("gold": {"presence_prob")");
    s.events = replaceOnce(s.events, R"("resource": "lithium")", R"("resource": "unobtainium")");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "techs.json", "[1].requires[0]", "unknown tech 'magic'"));
    CHECK(hasError(result, "regions.json", "[0].deposits.gold", "unknown resource 'gold'"));
    CHECK(hasError(result, "events.json", "[0].effects[0].resource", "unknown resource"));
}

TEST_CASE("Duplicate and malformed ids are reported", "[content]") {
    content::Sources s = goodSources();
    s.resources = replaceOnce(s.resources, R"("id": "lithium")", R"("id": "iron")");
    s.techs = replaceOnce(s.techs, R"("id": "seismic_1")", R"("id": "Seismic-1")");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "resources.json", "[1].id", "duplicate id 'iron'"));
    CHECK(hasError(result, "techs.json", "[0].id", "a-z, 0-9 and underscore"));
}

TEST_CASE("Resource unlocks and tech effects must agree", "[content]") {
    content::Sources s = goodSources();
    s.resources = replaceOnce(s.resources, R"("unlocked_by": "lithium_processing")",
                              R"("unlocked_by": "seismic_1")");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "resources.json", "[1].unlocked_by", "no unlock_resource effect"));
    CHECK(hasError(result, "techs.json", "[1].effects[0].resource", "not marked unlocked_by"));
}

TEST_CASE("Prerequisite cycles and self-requirements are reported", "[content]") {
    content::Sources s = goodSources();
    s.techs = replaceOnce(
        s.techs, R"("requires": [], "effects": [{"type": "survey_noise_mult")",
        R"("requires": ["lithium_processing"], "effects": [{"type": "survey_noise_mult")");
    const auto cycle = content::loadFromStrings(s);
    CHECK_FALSE(cycle.ok());
    CHECK(hasError(cycle, "techs.json", "[0].requires", "cycle"));

    content::Sources self = goodSources();
    self.techs =
        replaceOnce(self.techs, R"("requires": [], "effects": [{"type": "survey_noise_mult")",
                    R"("requires": ["seismic_1"], "effects": [{"type": "survey_noise_mult")");
    const auto selfResult = content::loadFromStrings(self);
    CHECK(hasError(selfResult, "techs.json", "[0].requires[0]", "cannot require itself"));
}

TEST_CASE("A research project may require a program stage; without the key it requires none",
          "[content]") {
    const auto plain = content::loadFromStrings(goodSources());
    REQUIRE(plain.ok());
    CHECK(plain.content->techs[0].requiresProgramStage == 0);
    CHECK(plain.content->techs[1].requiresProgramStage == 0);

    for (const std::uint32_t stage : {0U, 1U, 2U, 3U}) {
        content::Sources s = goodSources();
        s.techs = replaceOnce(
            s.techs, R"("duration_days": 240,)",
            R"("duration_days": 240, "requires_program_stage": )" + std::to_string(stage) + ",");
        const auto staged = content::loadFromStrings(s);
        INFO(content::formatErrors(staged.errors));
        REQUIRE(staged.ok());
        CHECK(staged.content->techs[0].requiresProgramStage == 0);
        CHECK(staged.content->techs[1].requiresProgramStage == stage);
    }
}

TEST_CASE("A required program stage must be a whole number from 0 to 3", "[content]") {
    const auto loadWith = [](std::string_view value) {
        content::Sources s = goodSources();
        s.techs = replaceOnce(
            s.techs, R"("duration_days": 240,)",
            R"("duration_days": 240, "requires_program_stage": )" + std::string(value) + ",");
        return content::loadFromStrings(s);
    };
    const std::string_view path = "[1].requires_program_stage";

    const auto tooHigh = loadWith("4");
    CHECK_FALSE(tooHigh.ok());
    CHECK(hasError(tooHigh, "techs.json", path, "must be between 0 and 3"));

    const auto negative = loadWith("-1");
    CHECK_FALSE(negative.ok());
    CHECK(hasError(negative, "techs.json", path, "must be between 0 and 3"));

    const auto fractional = loadWith("1.5");
    CHECK_FALSE(fractional.ok());
    CHECK(hasError(fractional, "techs.json", path, "expected an integer, got a number"));

    const auto text = loadWith(R"("2")");
    CHECK_FALSE(text.ok());
    CHECK(hasError(text, "techs.json", path, "expected an integer, got a string"));
}

TEST_CASE("A project's required program stage is part of the content fingerprint", "[content]") {
    // A save or trace made before a project was tied to a stage must not open
    // against content that ties it.
    const auto plain = content::loadFromStrings(goodSources());
    REQUIRE(plain.ok());
    sim::Content staged = *plain.content;
    staged.techs[1].requiresProgramStage = 1;
    CHECK(sim::hashContent(staged) != sim::hashContent(*plain.content));
    sim::Content later = staged;
    later.techs[1].requiresProgramStage = 2;
    CHECK(sim::hashContent(later) != sim::hashContent(staged));
}

TEST_CASE("Effect fields are validated against the effect type", "[content]") {
    content::Sources s = goodSources();
    s.techs = replaceOnce(s.techs, R"({"type": "unlock_resource", "resource": "lithium"})",
                          R"({"type": "unlock_resource", "resource": "lithium", "value": 2})");
    s.events = replaceOnce(s.events, R"({"type": "opex_mult", "value": 1.3})",
                           R"({"type": "opex_mult", "value": 1.3, "resource": "iron"})");
    s.events = replaceOnce(s.events, R"("type": "demand_mult")", R"("type": "demand_boost")");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "techs.json", "[1].effects[0].value", "not allowed"));
    CHECK(hasError(result, "events.json", "[0].effects[1].resource", "not allowed"));
    CHECK(hasError(result, "events.json", "[0].effects[0].type", "unknown event effect type"));
}

TEST_CASE("Default sell policy keys load and are validated", "[content]") {
    content::Sources s = goodSources();
    const auto good = content::loadFromStrings(s);
    REQUIRE(good.ok());
    CHECK(good.content->balance.defaultSellPolicy.enabled == 1);
    CHECK(good.content->balance.defaultSellPolicy.reserveTonnes == 0);
    CHECK(good.content->balance.defaultSellPolicy.floorPrice == sim::Money::zero());
    CHECK(good.content->balance.defaultCoverCosts);

    s.balance =
        replaceOnce(s.balance, R"("default_cover_costs": true)", R"("default_cover_costs": "yes")");
    s.balance = replaceOnce(s.balance, R"("reserve_tonnes": 0)", R"("reserve_tonnes": -1)");
    const auto bad = content::loadFromStrings(s);
    CHECK_FALSE(bad.ok());
    CHECK(hasError(bad, "balance.json", "default_cover_costs", "expected true or false"));
    CHECK(hasError(bad, "balance.json", "default_sell_policy.reserve_tonnes", "must be between"));
}

TEST_CASE("Opening-hand keys load and are validated", "[content]") {
    content::Sources s = goodSources();
    const auto good = content::loadFromStrings(s);
    REQUIRE(good.ok());
    CHECK(good.content->balance.openingMinMineable == 2);
    CHECK(good.content->balance.openingMaxLockedRegion == 1);

    // More mineable plots than the hand holds is a contradiction.
    content::Sources tooMany = goodSources();
    tooMany.balance = replaceOnce(tooMany.balance, R"("opening_min_mineable": 2)",
                                  R"("opening_min_mineable": 4)");
    const auto contradictory = content::loadFromStrings(tooMany);
    CHECK_FALSE(contradictory.ok());
    CHECK(hasError(contradictory, "balance.json", "opening_min_mineable",
                   "must not exceed plots_listed_at_start"));

    s.balance =
        replaceOnce(s.balance, R"("opening_min_mineable": 2)", R"("opening_min_mineable": -1)");
    s.balance = replaceOnce(s.balance, R"("opening_max_locked_region": 1)",
                            R"("opening_max_locked_region": 0.5)");
    const auto bad = content::loadFromStrings(s);
    CHECK_FALSE(bad.ok());
    CHECK(hasError(bad, "balance.json", "opening_min_mineable", "must be between"));
    CHECK(hasError(bad, "balance.json", "opening_max_locked_region", ""));

    content::Sources missing = goodSources();
    missing.balance = replaceOnce(missing.balance, R"("opening_min_mineable": 2, )", "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "balance.json", "opening_min_mineable", ""));

    // Zero for both is valid: no promise about the hand, no locked regions.
    content::Sources none = goodSources();
    none.balance =
        replaceOnce(none.balance, R"("opening_min_mineable": 2)", R"("opening_min_mineable": 0)");
    none.balance = replaceOnce(none.balance, R"("opening_max_locked_region": 1)",
                               R"("opening_max_locked_region": 0)");
    CHECK(content::loadFromStrings(none).ok());
}

TEST_CASE("The competitor's knobs load with bounds and are all required", "[content][ai]") {
    const auto good = content::loadFromStrings(goodSources());
    REQUIRE(good.ok());
    const sim::AiParams& ai = good.content->ai;
    CHECK(ai.sellAboveBase == sim::Fixed::fromRatio(85, 100));
    CHECK(ai.dumpStorageShare == sim::Fixed::fromRatio(2, 100));
    CHECK(ai.recoverableShare == sim::Fixed::fromRatio(1, 2));
    CHECK(ai.requiredReturn == sim::Fixed::fromRatio(6, 5));
    CHECK(ai.paybackHorizonDays == 1800);
    CHECK(ai.bidPremium == sim::Fixed::fromRatio(11, 10));
    CHECK(ai.bidPremiumHigh == sim::Fixed::fromRatio(5, 4));
    CHECK(ai.maxOpenProspects == 2);
    CHECK(ai.reopenMargin == sim::Fixed::fromRatio(5, 4));
    CHECK(ai.idleMineGraceDays == 30);
    CHECK(ai.opexReserveDays == 60);
    CHECK(ai.cashReserve == sim::Money::fromUnits(500'000));
    CHECK(ai.researchFloor == sim::Money::fromUnits(2'500'000));
    CHECK(ai.loanFloor == sim::Money::fromUnits(750'000));
    CHECK(ai.loanChunk == sim::Money::fromUnits(2'000'000));
    CHECK(ai.bootstrapLoan == sim::Money::fromUnits(4'000'000));
    CHECK(ai.firstMineRecoverableShare == sim::Fixed::fromRatio(6, 10));
    CHECK(ai.firstMineRequiredReturn == sim::Fixed::fromRatio(11, 10));
    CHECK(ai.supplyDiscount == sim::Fixed::one());
    CHECK(ai.programReserveShare == sim::Fixed::fromRatio(3, 4));
    CHECK(ai.programCashBuffer == sim::Money::fromUnits(1'500'000));
    CHECK(ai.programOreLandWeight == sim::Fixed::fromRatio(3, 2));
    CHECK(ai.programResearchProgress == sim::Fixed::fromRatio(1, 2));

    content::Sources s = goodSources();
    s.ai = replaceOnce(s.ai, R"("program_reserve_share": 0.75)", R"("program_reserve_share": 1.1)");
    s.ai = replaceOnce(s.ai, R"("program_cash_buffer": 1500000)", R"("program_cash_buffer": -1)");
    s.ai =
        replaceOnce(s.ai, R"("program_ore_land_weight": 1.5)", R"("program_ore_land_weight": 0.9)");
    s.ai = replaceOnce(s.ai, R"("program_research_progress": 0.5)",
                       R"("program_research_progress": 1.5)");
    s.ai = replaceOnce(s.ai, R"("required_return": 1.2)", R"("required_return": 0.9)");
    s.ai = replaceOnce(s.ai, R"("recoverable_share": 0.5)", R"("recoverable_share": 1.5)");
    s.ai = replaceOnce(s.ai, R"("max_open_prospects": 2)", R"("max_open_prospects": 0)");
    s.ai = replaceOnce(s.ai, R"("bid_premium_high": 1.25)", R"("bid_premium_high": 1.05)");
    s.ai = replaceOnce(s.ai, R"("loan_chunk": 2000000)", R"("loan_chunk": 0)");
    s.ai = replaceOnce(s.ai, R"("cash_reserve": 500000)", R"("cash_reserve": -1, "typo": 1)");
    s.ai = replaceOnce(s.ai, R"("supply_discount": 1)", R"("supply_discount": 1.5)");
    s.ai = replaceOnce(s.ai, R"("first_mine_required_return": 1.1)",
                       R"("first_mine_required_return": 0.5)");
    s.ai = replaceOnce(s.ai, R"("first_mine_recoverable_share": 0.6)",
                       R"("first_mine_recoverable_share": -0.1)");
    const auto bad = content::loadFromStrings(s);
    CHECK_FALSE(bad.ok());
    CHECK(hasError(bad, "ai.json", "required_return", "must be between"));
    CHECK(hasError(bad, "ai.json", "recoverable_share", "must be between"));
    CHECK(hasError(bad, "ai.json", "max_open_prospects", "must be between"));
    CHECK(hasError(bad, "ai.json", "bid_premium_high", "must not be below bid_premium"));
    CHECK(hasError(bad, "ai.json", "loan_chunk", "must be at least"));
    CHECK(hasError(bad, "ai.json", "cash_reserve", "must be at least"));
    CHECK(hasError(bad, "ai.json", "typo", "unknown key"));
    CHECK(hasError(bad, "ai.json", "supply_discount", "must be between"));
    CHECK(hasError(bad, "ai.json", "first_mine_required_return", "must be between"));
    CHECK(hasError(bad, "ai.json", "first_mine_recoverable_share", "must be between"));
    CHECK(hasError(bad, "ai.json", "program_reserve_share", "must be between"));
    CHECK(hasError(bad, "ai.json", "program_cash_buffer", "must be at least"));
    CHECK(hasError(bad, "ai.json", "program_ore_land_weight", "must be between"));
    CHECK(hasError(bad, "ai.json", "program_research_progress", "must be between"));

    content::Sources missing = goodSources();
    missing.ai = replaceOnce(missing.ai, R"("reopen_margin": 1.25,)", "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "ai.json", "reopen_margin", "missing required key"));

    content::Sources array = goodSources();
    array.ai = "[]";
    CHECK(hasError(content::loadFromStrings(array), "ai.json", "", "expected a top-level object"));
}

TEST_CASE("A deposit's size cap loads and must be at least the median", "[content]") {
    const auto good = content::loadFromStrings(goodSources());
    REQUIRE(good.ok());
    CHECK(good.content->regions[0].deposits[0].tonnesMaxMult == sim::Fixed::fromInt(4));

    content::Sources s = goodSources();
    s.regions = replaceOnce(s.regions, R"("tonnes_max_mult": 4)", R"("tonnes_max_mult": 0.5)");
    const auto bad = content::loadFromStrings(s);
    CHECK_FALSE(bad.ok());
    CHECK(hasError(bad, "regions.json", "[0].deposits.iron.tonnes_max_mult", "must be between"));

    content::Sources missing = goodSources();
    missing.regions = replaceOnce(missing.regions, R"(,
                           "tonnes_max_mult": 4)",
                                  "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "regions.json", "[0].deposits.iron.tonnes_max_mult",
                   "missing required key"));
}

TEST_CASE("The mineable-region threshold loads from the balance file", "[content]") {
    const auto good = content::loadFromStrings(goodSources());
    REQUIRE(good.ok());
    CHECK(good.content->balance.openingMineablePresence == sim::Fixed::fromRatio(1, 2));

    content::Sources s = goodSources();
    s.balance = replaceOnce(s.balance, R"("opening_mineable_presence": 0.5)",
                            R"("opening_mineable_presence": 1.5)");
    const auto bad = content::loadFromStrings(s);
    CHECK_FALSE(bad.ok());
    CHECK(hasError(bad, "balance.json", "opening_mineable_presence", "must be between"));
}

TEST_CASE("Unsold-land keys load and are validated", "[content]") {
    content::Sources s = goodSources();
    const auto good = content::loadFromStrings(s);
    REQUIRE(good.ok());
    CHECK(good.content->balance.maxUnsoldRounds == 3);
    CHECK(good.content->balance.relistCooldownDays == 360);

    s.balance = replaceOnce(s.balance, R"("max_unsold_rounds": 3)", R"("max_unsold_rounds": -1)");
    s.balance =
        replaceOnce(s.balance, R"("relist_cooldown_days": 360)", R"("relist_cooldown_days": 1.5)");
    const auto bad = content::loadFromStrings(s);
    CHECK_FALSE(bad.ok());
    CHECK(hasError(bad, "balance.json", "max_unsold_rounds", "must be between"));
    CHECK(hasError(bad, "balance.json", "relist_cooldown_days", ""));

    content::Sources missing = goodSources();
    missing.balance = replaceOnce(missing.balance, R"("max_unsold_rounds": 3, )", "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "balance.json", "max_unsold_rounds", ""));
}

TEST_CASE("Each ore's mine running cost loads and must be above zero", "[content]") {
    const auto good = content::loadFromStrings(goodSources());
    INFO(content::formatErrors(good.errors));
    REQUIRE(good.ok());
    CHECK(good.content->resources[0].mineOpexPerDay == sim::Money::fromUnits(9'000));
    CHECK(good.content->resources[1].mineOpexPerDay == sim::Money::fromCents(700'050));

    content::Sources s = goodSources();
    s.resources =
        replaceOnce(s.resources, R"("mine_opex_per_day": 9000)", R"("mine_opex_per_day": 0)");
    s.resources =
        replaceOnce(s.resources, R"("mine_opex_per_day": 7000.5)", R"("mine_opex_per_day": -1)");
    const auto bad = content::loadFromStrings(s);
    CHECK_FALSE(bad.ok());
    CHECK(hasError(bad, "resources.json", "[0].mine_opex_per_day", "must be at least 0.01"));
    CHECK(hasError(bad, "resources.json", "[1].mine_opex_per_day", "must be at least 0.01"));

    content::Sources missing = goodSources();
    missing.resources = replaceOnce(missing.resources, R"("mine_opex_per_day": 9000, )", "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "resources.json", "[0].mine_opex_per_day", "missing required key"));

    content::Sources text = goodSources();
    text.resources = replaceOnce(text.resources, R"("mine_opex_per_day": 9000)",
                                 R"("mine_opex_per_day": "9000")");
    const auto wrongType = content::loadFromStrings(text);
    CHECK_FALSE(wrongType.ok());
    CHECK(hasError(wrongType, "resources.json", "[0].mine_opex_per_day", "expected a number"));
}

TEST_CASE("A balance file still carrying the single mine running cost is refused", "[content]") {
    content::Sources s = goodSources();
    s.balance = replaceOnce(s.balance, R"("mine_mothball_opex_mult": 0.2,)",
                            R"("mine_opex_per_day": 12000, "mine_mothball_opex_mult": 0.2,)");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "balance.json", "mine_opex_per_day", "unknown key"));
}

TEST_CASE("Market clamps load with the values the market code used", "[content]") {
    const auto good = content::loadFromStrings(goodSources());
    INFO(content::formatErrors(good.errors));
    REQUIRE(good.ok());
    // The same expressions as the constants in sim/src/markets.cpp, so the
    // decimal in the file is shown to be the identical fixed-point value.
    CHECK(good.content->balance.marketRatioMin == sim::Fixed::fromRatio(1, 5));
    CHECK(good.content->balance.marketRatioMax == sim::Fixed::fromInt(5));
    CHECK(good.content->balance.demandFactorMin == sim::Fixed::fromRatio(1, 100));
    CHECK(good.content->balance.demandFactorMax == sim::Fixed::fromInt(1000));
}

TEST_CASE("Market clamps must be above zero and each min below its max", "[content]") {
    content::Sources s = goodSources();
    s.balance = replaceOnce(s.balance, R"("market_ratio_min": 0.2)", R"("market_ratio_min": 0)");
    s.balance =
        replaceOnce(s.balance, R"("demand_factor_min": 0.01)", R"("demand_factor_min": -0.5)");
    const auto notPositive = content::loadFromStrings(s);
    CHECK_FALSE(notPositive.ok());
    CHECK(hasError(notPositive, "balance.json", "market_ratio_min", "must be between"));
    CHECK(hasError(notPositive, "balance.json", "demand_factor_min", "must be between"));
    // A rejected min is reported once, not again as an ordering error.
    CHECK_FALSE(hasError(notPositive, "balance.json", "market_ratio_max", ""));
    CHECK_FALSE(hasError(notPositive, "balance.json", "demand_factor_max", ""));

    // The ratio clamp has to straddle 1, so its two ends can only meet there.
    content::Sources equal = goodSources();
    equal.balance =
        replaceOnce(equal.balance, R"("market_ratio_min": 0.2)", R"("market_ratio_min": 1)");
    equal.balance =
        replaceOnce(equal.balance, R"("market_ratio_max": 5)", R"("market_ratio_max": 1)");
    equal.balance =
        replaceOnce(equal.balance, R"("demand_factor_max": 1000)", R"("demand_factor_max": 0.01)");
    const auto notBelow = content::loadFromStrings(equal);
    CHECK_FALSE(notBelow.ok());
    CHECK(hasError(notBelow, "balance.json", "market_ratio_max",
                   "must be greater than market_ratio_min"));
    CHECK(hasError(notBelow, "balance.json", "demand_factor_max",
                   "must be greater than demand_factor_min"));

    content::Sources inverted = goodSources();
    inverted.balance = replaceOnce(inverted.balance, R"("demand_factor_min": 0.01)",
                                   R"("demand_factor_min": 500)");
    inverted.balance = replaceOnce(inverted.balance, R"("demand_factor_max": 1000)",
                                   R"("demand_factor_max": 100)");
    const auto above = content::loadFromStrings(inverted);
    CHECK_FALSE(above.ok());
    CHECK(hasError(above, "balance.json", "demand_factor_max",
                   "must be greater than demand_factor_min"));
}

TEST_CASE("The market ratio clamp must straddle 1", "[content]") {
    // Base price is the price at a ratio of 1. A minimum above 1 or a maximum
    // below 1 would put the price permanently on one side of base, and the AI
    // clamps its supply discount between the minimum and 1.
    content::Sources high = goodSources();
    high.balance =
        replaceOnce(high.balance, R"("market_ratio_min": 0.2)", R"("market_ratio_min": 2)");
    const auto minAboveOne = content::loadFromStrings(high);
    CHECK_FALSE(minAboveOne.ok());
    CHECK(hasError(minAboveOne, "balance.json", "market_ratio_min",
                   "must be between 0.010000 and 1.000000"));
    // The rejected minimum is reported once, not again as an ordering error.
    CHECK_FALSE(hasError(minAboveOne, "balance.json", "market_ratio_max", ""));

    content::Sources low = goodSources();
    low.balance =
        replaceOnce(low.balance, R"("market_ratio_max": 5)", R"("market_ratio_max": 0.5)");
    const auto maxBelowOne = content::loadFromStrings(low);
    CHECK_FALSE(maxBelowOne.ok());
    CHECK(hasError(maxBelowOne, "balance.json", "market_ratio_max",
                   "must be between 1.000000 and 10.000000"));
    CHECK_FALSE(hasError(maxBelowOne, "balance.json", "market_ratio_min", ""));

    // Either end may sit exactly on 1, as long as the other does not.
    content::Sources pinnedLow = goodSources();
    pinnedLow.balance =
        replaceOnce(pinnedLow.balance, R"("market_ratio_min": 0.2)", R"("market_ratio_min": 1)");
    const auto minAtOne = content::loadFromStrings(pinnedLow);
    INFO(content::formatErrors(minAtOne.errors));
    CHECK(minAtOne.ok());

    content::Sources pinnedHigh = goodSources();
    pinnedHigh.balance =
        replaceOnce(pinnedHigh.balance, R"("market_ratio_max": 5)", R"("market_ratio_max": 1)");
    const auto maxAtOne = content::loadFromStrings(pinnedHigh);
    INFO(content::formatErrors(maxAtOne.errors));
    CHECK(maxAtOne.ok());
}

TEST_CASE("Market clamps are bounded so the price arithmetic cannot overflow", "[content]") {
    content::Sources s = goodSources();
    s.balance = replaceOnce(s.balance, R"("market_ratio_max": 5)", R"("market_ratio_max": 10.5)");
    s.balance =
        replaceOnce(s.balance, R"("demand_factor_max": 1000)", R"("demand_factor_max": 1001)");
    s.balance =
        replaceOnce(s.balance, R"("market_ratio_min": 0.2)", R"("market_ratio_min": 0.009)");
    const auto bad = content::loadFromStrings(s);
    CHECK_FALSE(bad.ok());
    CHECK(hasError(bad, "balance.json", "market_ratio_max", "must be between"));
    CHECK(hasError(bad, "balance.json", "demand_factor_max", "must be between"));
    CHECK(hasError(bad, "balance.json", "market_ratio_min", "must be between"));

    content::Sources widest = goodSources();
    widest.balance =
        replaceOnce(widest.balance, R"("market_ratio_min": 0.2)", R"("market_ratio_min": 0.01)");
    widest.balance =
        replaceOnce(widest.balance, R"("market_ratio_max": 5)", R"("market_ratio_max": 10)");
    widest.balance = replaceOnce(widest.balance, R"("demand_factor_min": 0.01)",
                                 R"("demand_factor_min": 0.000001)");
    const auto accepted = content::loadFromStrings(widest);
    INFO(content::formatErrors(accepted.errors));
    CHECK(accepted.ok());
}

TEST_CASE("Every market clamp key is required", "[content]") {
    content::Sources s = goodSources();
    s.balance = replaceOnce(s.balance, R"("market_ratio_min": 0.2, "market_ratio_max": 5,)", "");
    s.balance =
        replaceOnce(s.balance, R"("demand_factor_min": 0.01, "demand_factor_max": 1000,)", "");
    const auto absent = content::loadFromStrings(s);
    CHECK_FALSE(absent.ok());
    for (const std::string_view key :
         {"market_ratio_min", "market_ratio_max", "demand_factor_min", "demand_factor_max"}) {
        INFO(key);
        CHECK(hasError(absent, "balance.json", key, "missing required key"));
    }
}

TEST_CASE("The losing-mine notice threshold loads and must be at least one day", "[content]") {
    const auto good = content::loadFromStrings(goodSources());
    REQUIRE(good.ok());
    CHECK(good.content->balance.mineLossNoticeDays == 5);

    content::Sources s = goodSources();
    s.balance =
        replaceOnce(s.balance, R"("mine_loss_notice_days": 5)", R"("mine_loss_notice_days": 0)");
    const auto zero = content::loadFromStrings(s);
    CHECK_FALSE(zero.ok());
    CHECK(hasError(zero, "balance.json", "mine_loss_notice_days", "must be between 1 and"));

    content::Sources fractional = goodSources();
    fractional.balance = replaceOnce(fractional.balance, R"("mine_loss_notice_days": 5)",
                                     R"("mine_loss_notice_days": 2.5)");
    const auto notWhole = content::loadFromStrings(fractional);
    CHECK_FALSE(notWhole.ok());
    CHECK(hasError(notWhole, "balance.json", "mine_loss_notice_days", "expected an integer"));

    content::Sources missing = goodSources();
    missing.balance = replaceOnce(missing.balance, R"("mine_loss_notice_days": 5,)", "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "balance.json", "mine_loss_notice_days", "missing required key"));
}

TEST_CASE("The losing-mine notice's smallest share loads and is a fraction", "[content]") {
    const auto good = content::loadFromStrings(goodSources());
    REQUIRE(good.ok());
    CHECK(good.content->balance.mineLossNoticeMinShare == sim::Fixed::fromRatio(5, 100));

    const auto with = [](std::string_view value) {
        content::Sources s = goodSources();
        s.balance =
            replaceOnce(s.balance, R"("mine_loss_notice_min_share": 0.05)",
                        std::string(R"("mine_loss_notice_min_share": )") + std::string(value));
        return content::loadFromStrings(s);
    };
    const auto none = with("0");
    REQUIRE(none.ok());
    CHECK(none.content->balance.mineLossNoticeMinShare == sim::Fixed::zero());
    const auto whole = with("1");
    REQUIRE(whole.ok());
    CHECK(whole.content->balance.mineLossNoticeMinShare == sim::Fixed::one());

    for (const std::string_view bad : {"-0.01", "1.01"}) {
        INFO(bad);
        const auto result = with(bad);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "balance.json", "mine_loss_notice_min_share", "must be between"));
    }

    content::Sources missing = goodSources();
    missing.balance = replaceOnce(missing.balance, R"("mine_loss_notice_min_share": 0.05,)", "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "balance.json", "mine_loss_notice_min_share", "missing required key"));
}

TEST_CASE("The losing-mine notice's re-arm days load and are bounded", "[content]") {
    const auto good = content::loadFromStrings(goodSources());
    REQUIRE(good.ok());
    CHECK(good.content->balance.mineLossRearmDays == 30);

    const auto with = [](std::string_view value) {
        content::Sources s = goodSources();
        s.balance = replaceOnce(s.balance, R"("mine_loss_rearm_days": 30)",
                                std::string(R"("mine_loss_rearm_days": )") + std::string(value));
        return content::loadFromStrings(s);
    };
    const auto least = with("1");
    REQUIRE(least.ok());
    CHECK(least.content->balance.mineLossRearmDays == 1);
    const auto most = with("3600");
    REQUIRE(most.ok());
    CHECK(most.content->balance.mineLossRearmDays == 3600);

    for (const std::string_view bad : {"0", "3601"}) {
        INFO(bad);
        const auto result = with(bad);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "balance.json", "mine_loss_rearm_days", "must be between 1 and"));
    }
    const auto notWhole = with("2.5");
    CHECK_FALSE(notWhole.ok());
    CHECK(hasError(notWhole, "balance.json", "mine_loss_rearm_days", "expected an integer"));

    content::Sources missing = goodSources();
    missing.balance = replaceOnce(missing.balance, R"( "mine_loss_rearm_days": 30,)", "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "balance.json", "mine_loss_rearm_days", "missing required key"));
}

TEST_CASE("Large valid money values with cents are accepted", "[content]") {
    content::Sources s = goodSources();
    s.balance = replaceOnce(s.balance, R"("loan_max_principal": 10000000)",
                            R"("loan_max_principal": 99999999999.99)");
    const auto result = content::loadFromStrings(s);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    CHECK(result.content->balance.loanMaxPrincipal == sim::Money::fromCents(9'999'999'999'999LL));
}

TEST_CASE("Non-object array entries keep later error paths aligned", "[content]") {
    content::Sources s = goodSources();
    s.resources =
        replaceOnce(s.resources, "[\n      {\"id\": \"iron\"", "[ 5,\n      {\"id\": \"iron\"");
    s.resources = replaceOnce(s.resources, R"("unlocked_by": "lithium_processing")",
                              R"("unlocked_by": "nope")");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "resources.json", "[0]", "expected an object"));
    CHECK(hasError(result, "resources.json", "[2].unlocked_by", "unknown tech 'nope'"));
    // The placeholder element reports nothing beyond the type error.
    CHECK_FALSE(hasError(result, "resources.json", "[0].id", "missing"));
}

TEST_CASE("Range objects must be ordered", "[content]") {
    content::Sources s = goodSources();
    s.regions = replaceOnce(s.regions, R"({"min": 800000, "max": 2500000})",
                            R"({"min": 3000000, "max": 2500000})");
    const auto result = content::loadFromStrings(s);
    CHECK(hasError(result, "regions.json", "[0].land_price.max", "below min"));
}

TEST_CASE("All errors are collected in one pass", "[content]") {
    content::Sources s = goodSources();
    s.resources = "[]";
    s.regions = "{}";
    s.balance = replaceOnce(s.balance, R"("starting_capital": 5000000,)", "");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "resources.json", "", "at least one resource"));
    CHECK(hasError(result, "regions.json", "", "top-level array"));
    CHECK(hasError(result, "balance.json", "starting_capital", "missing required key"));
    CHECK(result.errors.size() >= 4);  // plus the dangling lithium unlock reference
    CHECK_FALSE(content::formatErrors(result.errors).empty());
}

TEST_CASE("Missing directory reports unreadable files", "[content]") {
    const auto result = content::loadFromDirectory("/definitely/not/a/real/dir");
    CHECK_FALSE(result.ok());
    CHECK(result.errors.size() == 8);  // one per data file, the catalogue included
    CHECK(std::any_of(result.errors.begin(), result.errors.end(), [](const auto& e) {
        return e.file == "program.json" && e.message.find("cannot read") != std::string::npos;
    }));
    CHECK(result.errors[0].message.find("cannot read") != std::string::npos);
}

// ---- the program (program.json, spec 010 BR-001, BR-002, AC-001) ----

TEST_CASE("The program loads with its deadline and three stages in order", "[content][spec010]") {
    const auto result = content::loadFromStrings(goodSources());
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& c = *result.content;

    CHECK(c.program.deadlineDay == 10800);
    REQUIRE(c.program.stages.size() == 3);
    CHECK(c.program.stages[0].id == "stage_1");
    CHECK(c.program.stages[0].cost == sim::Money::fromUnits(1'000'000));
    CHECK(c.program.stages[0].research == c.findTech("program_stage_1"));
    REQUIRE(c.program.stages[0].requirements.size() == 1);
    CHECK(c.program.stages[0].requirements[0].resource == c.findResource("iron"));
    CHECK(c.program.stages[0].requirements[0].tonnes == 1000);

    CHECK(c.program.stages[1].id == "stage_2");
    CHECK(c.program.stages[1].cost == sim::Money::fromCents(200'000'050));
    CHECK(c.program.stages[1].research == c.findTech("program_stage_2"));

    CHECK(c.program.stages[2].id == "stage_3");
    CHECK(c.program.stages[2].research == c.findTech("program_stage_3"));
    REQUIRE(c.program.stages[2].requirements.size() == 2);
    CHECK(c.program.stages[2].requirements[1].resource == c.findResource("lithium"));
    CHECK(c.program.stages[2].requirements[1].tonnes == 50);
}

TEST_CASE("The program must have exactly three stages", "[content][spec010]") {
    content::Sources missing = goodSources();
    missing.program =
        replaceOnce(missing.program,
                    R"({"id": "stage_2", "cost": 2000000.5, "research": "program_stage_2",
         "requires": [{"resource": "iron", "tonnes": 2000}]},)",
                    "");
    const auto two = content::loadFromStrings(missing);
    CHECK_FALSE(two.ok());
    CHECK(hasError(two, "program.json", "stages", "exactly 3 stages"));

    content::Sources extra = goodSources();
    extra.program = replaceOnce(extra.program, R"({"resource": "lithium", "tonnes": 50}]})",
                                R"({"resource": "lithium", "tonnes": 50}]},
        {"id": "stage_4", "cost": 1, "research": "program_stage_3",
         "requires": [{"resource": "lithium", "tonnes": 1}]})");
    const auto four = content::loadFromStrings(extra);
    CHECK_FALSE(four.ok());
    CHECK(hasError(four, "program.json", "stages", "exactly 3 stages"));

    content::Sources none = goodSources();
    none.program = R"({"deadline_day": 10800, "stages": []})";
    CHECK(hasError(content::loadFromStrings(none), "program.json", "stages", "exactly 3 stages"));

    // An element that is not a stage is reported as such, and once.
    content::Sources number = goodSources();
    number.program =
        replaceOnce(number.program,
                    R"({"id": "stage_2", "cost": 2000000.5, "research": "program_stage_2",
         "requires": [{"resource": "iron", "tonnes": 2000}]},)",
                    "7,");
    const auto notAStage = content::loadFromStrings(number);
    CHECK_FALSE(notAStage.ok());
    CHECK(hasError(notAStage, "program.json", "stages[1]", "expected an object"));
    CHECK_FALSE(hasError(notAStage, "program.json", "stages", "exactly 3 stages"));
}

TEST_CASE("The deadline must be a positive whole day", "[content][spec010]") {
    const auto loadWith = [](std::string_view value) {
        content::Sources s = goodSources();
        s.program = replaceOnce(s.program, R"("deadline_day": 10800)",
                                R"("deadline_day": )" + std::string(value));
        return content::loadFromStrings(s);
    };
    for (const std::string_view value : {"0", "-360", "36001"}) {
        INFO(value);
        const auto result = loadWith(value);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "program.json", "deadline_day", "must be between 1 and 36000"));
    }
    CHECK(hasError(loadWith("10800.5"), "program.json", "deadline_day", "expected an integer"));

    content::Sources missing = goodSources();
    missing.program = replaceOnce(missing.program, R"("deadline_day": 10800,)", "");
    CHECK(hasError(content::loadFromStrings(missing), "program.json", "deadline_day",
                   "missing required key"));
}

TEST_CASE("A stage's cost and required tonnes must be positive", "[content][spec010]") {
    for (const std::string_view cost : {"0", "-5"}) {
        INFO(cost);
        content::Sources s = goodSources();
        s.program =
            replaceOnce(s.program, R"("cost": 1000000,)", R"("cost": )" + std::string(cost) + ",");
        const auto result = content::loadFromStrings(s);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "program.json", "stages[0].cost", "must be at least 0.01"));
    }
    for (const std::string_view tonnes : {"0", "-1"}) {
        INFO(tonnes);
        content::Sources s = goodSources();
        s.program =
            replaceOnce(s.program, R"("tonnes": 2000)", R"("tonnes": )" + std::string(tonnes));
        const auto result = content::loadFromStrings(s);
        CHECK_FALSE(result.ok());
        CHECK(
            hasError(result, "program.json", "stages[1].requires[0].tonnes", "must be between 1"));
    }
    // Whole tonnes only.
    content::Sources fractional = goodSources();
    fractional.program = replaceOnce(fractional.program, R"("tonnes": 2000)", R"("tonnes": 0.5)");
    CHECK(hasError(content::loadFromStrings(fractional), "program.json",
                   "stages[1].requires[0].tonnes", "expected an integer"));
}

TEST_CASE("A stage's goods and research project must exist", "[content][spec010]") {
    content::Sources s = goodSources();
    s.program = replaceOnce(s.program, R"({"resource": "iron", "tonnes": 2000})",
                            R"({"resource": "steel", "tonnes": 2000})");
    s.program = replaceOnce(s.program, R"("research": "program_stage_1")",
                            R"("research": "program_stage_9")");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "program.json", "stages[1].requires[0].resource",
                   "unknown resource 'steel'"));
    CHECK(hasError(result, "program.json", "stages[0].research", "unknown tech 'program_stage_9'"));
}

TEST_CASE("A stage may not name the same good twice", "[content][spec010]") {
    content::Sources s = goodSources();
    s.program = replaceOnce(s.program, R"({"resource": "lithium", "tonnes": 50})",
                            R"({"resource": "lithium", "tonnes": 50},
                               {"resource": "iron", "tonnes": 1})");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "program.json", "stages[2].requires[2].resource",
                   "duplicate resource 'iron'"));
}

TEST_CASE("A stage's research project must require exactly the stages before it",
          "[content][spec010]") {
    // Stage 1's project requires no completed stage, stage 2's one, stage 3's
    // two: a project tied to the wrong stage would start early or never.
    content::Sources swapped = goodSources();
    swapped.program = replaceOnce(swapped.program, R"("research": "program_stage_2")",
                                  R"("research": "program_stage_3")");
    const auto wrong = content::loadFromStrings(swapped);
    CHECK_FALSE(wrong.ok());
    CHECK(hasError(wrong, "program.json", "stages[1].research",
                   "tech 'program_stage_3' must have requires_program_stage 1"));

    // A project that carries no stage requirement counts as requiring none,
    // so it fits the first stage only.
    content::Sources ordinary = goodSources();
    ordinary.program = replaceOnce(ordinary.program, R"("research": "program_stage_3")",
                                   R"("research": "seismic_1")");
    const auto plain = content::loadFromStrings(ordinary);
    CHECK_FALSE(plain.ok());
    CHECK(hasError(plain, "program.json", "stages[2].research",
                   "tech 'seismic_1' must have requires_program_stage 2"));

    content::Sources retied = goodSources();
    retied.techs = replaceOnce(retied.techs, R"("requires_program_stage": 0, "effects": [])",
                               R"("requires_program_stage": 1, "effects": [])");
    const auto late = content::loadFromStrings(retied);
    CHECK_FALSE(late.ok());
    CHECK(hasError(late, "program.json", "stages[0].research",
                   "tech 'program_stage_1' must have requires_program_stage 0"));
}

TEST_CASE("A program stage must require at least one good", "[content][spec010]") {
    content::Sources s = goodSources();
    s.program = replaceOnce(s.program, R"("requires": [{"resource": "iron", "tonnes": 2000}])",
                            R"("requires": [])");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "program.json", "stages[1].requires", "at least one good"));
}

TEST_CASE("The third stage must require lithium", "[content][spec010]") {
    content::Sources s = goodSources();
    s.program = replaceOnce(s.program, R"(, {"resource": "lithium", "tonnes": 50})", "");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "program.json", "stages[2].requires", "must require lithium"));

    // Lithium in an earlier stage does not stand in for it.
    content::Sources early = goodSources();
    early.program = replaceOnce(early.program, R"(, {"resource": "lithium", "tonnes": 50})", "");
    early.program = replaceOnce(early.program, R"({"resource": "iron", "tonnes": 1000})",
                                R"({"resource": "lithium", "tonnes": 50})");
    CHECK(hasError(content::loadFromStrings(early), "program.json", "stages[2].requires",
                   "must require lithium"));

    // Content with no lithium at all cannot satisfy the rule.
    content::Sources renamed = goodSources();
    renamed.resources = replaceOnce(renamed.resources, R"("id": "lithium")", R"("id": "brine")");
    CHECK(hasError(content::loadFromStrings(renamed), "program.json", "stages[2].requires",
                   "must require lithium"));
}

TEST_CASE("Program keys are checked like every other data file", "[content][spec010]") {
    content::Sources s = goodSources();
    s.program = replaceOnce(s.program, R"("deadline_day": 10800,)",
                            R"("deadline_day": 10800, "deadline": 1,)");
    s.program = replaceOnce(s.program, R"({"id": "stage_1", "cost": 1000000,)",
                            R"({"id": "stage_1", "price": 1,)");
    s.program = replaceOnce(s.program, R"({"resource": "iron", "tonnes": 3000})",
                            R"({"resource": "iron", "tonnes": 3000, "unit": "t"})");
    s.program = replaceOnce(s.program, R"("id": "stage_2")", R"("id": "stage_1")");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "program.json", "deadline", "unknown key"));
    CHECK(hasError(result, "program.json", "stages[0].price", "unknown key"));
    CHECK(hasError(result, "program.json", "stages[0].cost", "missing required key"));
    CHECK(hasError(result, "program.json", "stages[2].requires[0].unit", "unknown key"));
    CHECK(hasError(result, "program.json", "stages[1].id", "duplicate id 'stage_1'"));

    content::Sources array = goodSources();
    array.program = "[]";
    CHECK(hasError(content::loadFromStrings(array), "program.json", "",
                   "expected a top-level object"));

    content::Sources broken = goodSources();
    broken.program = "{";
    CHECK(hasError(content::loadFromStrings(broken), "program.json", "", "invalid JSON"));

    content::Sources bare = goodSources();
    bare.program = R"({"deadline_day": 10800})";
    CHECK(
        hasError(content::loadFromStrings(bare), "program.json", "stages", "missing required key"));
}

TEST_CASE("The whole program is part of the content fingerprint", "[content][spec010]") {
    // A save or trace must not open against a different race.
    const auto loaded = content::loadFromStrings(goodSources());
    REQUIRE(loaded.ok());
    const sim::Content& original = *loaded.content;
    const std::uint64_t hash = sim::hashContent(original);

    sim::Content deadline = original;
    deadline.program.deadlineDay += 1;
    CHECK(sim::hashContent(deadline) != hash);

    sim::Content id = original;
    id.program.stages[0].id = "first";
    CHECK(sim::hashContent(id) != hash);

    sim::Content cost = original;
    cost.program.stages[1].cost += sim::Money::fromCents(1);
    CHECK(sim::hashContent(cost) != hash);

    sim::Content research = original;
    research.program.stages[2].research = original.findTech("seismic_1");
    CHECK(sim::hashContent(research) != hash);

    sim::Content good = original;
    good.program.stages[0].requirements[0].resource = original.findResource("lithium");
    CHECK(sim::hashContent(good) != hash);

    sim::Content tonnes = original;
    tonnes.program.stages[2].requirements[1].tonnes += 1;
    CHECK(sim::hashContent(tonnes) != hash);

    sim::Content fewer = original;
    fewer.program.stages.pop_back();
    CHECK(sim::hashContent(fewer) != hash);
}

TEST_CASE("The in-code fixture carries a program the loader would accept", "[content][spec010]") {
    // test::makeContent() is built by hand, so nothing else holds it to the
    // loader's program rules.
    const sim::Content c = test::makeContent();
    CHECK(c.program.deadlineDay > 0);
    REQUIRE(c.program.stages.size() == 3);
    for (std::size_t i = 0; i < c.program.stages.size(); ++i) {
        const sim::ProgramStage& stage = c.program.stages[i];
        INFO(stage.id);
        CHECK_FALSE(stage.id.empty());
        CHECK(stage.cost > sim::Money::zero());
        REQUIRE(stage.research.valid());
        REQUIRE(stage.research.index() < c.techs.size());
        CHECK(c.techs[stage.research.index()].requiresProgramStage == i);
        for (std::size_t r = 0; r < stage.requirements.size(); ++r) {
            const sim::ProgramRequirement& requirement = stage.requirements[r];
            REQUIRE(requirement.resource.valid());
            REQUIRE(requirement.resource.index() < c.resources.size());
            CHECK(requirement.tonnes > 0);
            for (std::size_t earlier = 0; earlier < r; ++earlier) {
                CHECK(stage.requirements[earlier].resource != requirement.resource);
            }
        }
    }
    const auto& last = c.program.stages[2].requirements;
    CHECK(std::any_of(last.begin(), last.end(), [&c](const sim::ProgramRequirement& requirement) {
        return requirement.resource == c.findResource("lithium");
    }));
}

TEST_CASE("The pace warning loads and lies between 0 and the deadline", "[content][spec011]") {
    // Spec 011 (BR-004): how many days before the last day to start research
    // the Dashboard lists it. A warning after the deadline could never show.
    const auto good = content::loadFromStrings(goodSources());
    REQUIRE(good.ok());
    CHECK(good.content->program.paceWarningDays == 360);

    const auto with = [](std::string_view value) {
        content::Sources s = goodSources();
        s.program = replaceOnce(s.program, R"("program_pace_warning_days": 360)",
                                R"("program_pace_warning_days": )" + std::string(value));
        return content::loadFromStrings(s);
    };
    const auto none = with("0");
    REQUIRE(none.ok());
    CHECK(none.content->program.paceWarningDays == 0);
    const auto whole = with("10800");
    REQUIRE(whole.ok());
    CHECK(whole.content->program.paceWarningDays == 10800);

    const auto negative = with("-1");
    CHECK_FALSE(negative.ok());
    CHECK(hasError(negative, "program.json", "program_pace_warning_days", "must be between 0"));
    const auto late = with("10801");
    CHECK_FALSE(late.ok());
    CHECK(hasError(late, "program.json", "program_pace_warning_days",
                   "must not exceed deadline_day"));
    const auto notWhole = with("2.5");
    CHECK_FALSE(notWhole.ok());
    CHECK(hasError(notWhole, "program.json", "program_pace_warning_days", "expected an integer"));

    content::Sources missing = goodSources();
    missing.program = replaceOnce(missing.program, R"("program_pace_warning_days": 360,)", "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "program.json", "program_pace_warning_days", "missing required key"));

    // A deadline already refused is not reported a second time through the
    // warning that depends on it.
    content::Sources noDeadline = goodSources();
    noDeadline.program = replaceOnce(noDeadline.program, R"("deadline_day": 10800,)", "");
    const auto orphan = content::loadFromStrings(noDeadline);
    CHECK(hasError(orphan, "program.json", "deadline_day", "missing required key"));
    CHECK_FALSE(hasError(orphan, "program.json", "program_pace_warning_days", ""));
}

TEST_CASE("The storage warning's share of cash loads and is above 0 and at most 1",
          "[content][spec011]") {
    // Spec 011 (BR-014): a month's storage above this share of cash is a
    // problem the Dashboard lists.
    const auto good = content::loadFromStrings(goodSources());
    REQUIRE(good.ok());
    CHECK(good.content->balance.storageWarningCashShare == sim::Fixed::fromRatio(1, 10));

    const auto with = [](std::string_view value) {
        content::Sources s = goodSources();
        s.balance =
            replaceOnce(s.balance, R"("storage_warning_cash_share": 0.1)",
                        std::string(R"("storage_warning_cash_share": )") + std::string(value));
        return content::loadFromStrings(s);
    };
    const auto least = with("0.000001");
    REQUIRE(least.ok());
    CHECK(least.content->balance.storageWarningCashShare == sim::Fixed::fromRaw(1));
    const auto whole = with("1");
    REQUIRE(whole.ok());
    CHECK(whole.content->balance.storageWarningCashShare == sim::Fixed::one());

    for (const std::string_view bad : {"0", "-0.1", "1.01"}) {
        INFO(bad);
        const auto result = with(bad);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "balance.json", "storage_warning_cash_share", "must be between"));
    }
    const auto text = with(R"("a tenth")");
    CHECK_FALSE(text.ok());
    CHECK(hasError(text, "balance.json", "storage_warning_cash_share", "expected a number"));

    content::Sources missing = goodSources();
    missing.balance = replaceOnce(missing.balance, R"("storage_warning_cash_share": 0.1,)", "");
    const auto absent = content::loadFromStrings(missing);
    CHECK_FALSE(absent.ok());
    CHECK(hasError(absent, "balance.json", "storage_warning_cash_share", "missing required key"));
}

TEST_CASE("The pace and storage warnings are part of the content fingerprint",
          "[content][spec011]") {
    const auto loaded = content::loadFromStrings(goodSources());
    REQUIRE(loaded.ok());
    const sim::Content& original = *loaded.content;
    const std::uint64_t hash = sim::hashContent(original);

    sim::Content pace = original;
    pace.program.paceWarningDays += 1;
    CHECK(sim::hashContent(pace) != hash);

    sim::Content storage = original;
    storage.balance.storageWarningCashShare = sim::Fixed::fromRatio(1, 5);
    CHECK(sim::hashContent(storage) != hash);
}

TEST_CASE("A stage's research project may not require other research", "[content][spec011]") {
    // Spec 011 (GA-009): the earliest founding day counts a stage project's
    // time and nothing before it, so a stage project with ordinary
    // prerequisites would make the race's figures wrong.
    content::Sources s = goodSources();
    s.techs =
        replaceOnce(s.techs, R"("requires_program_stage": 1, "effects": [])",
                    R"("requires_program_stage": 1, "requires": ["seismic_1"], "effects": [])");
    const auto result = content::loadFromStrings(s);
    CHECK_FALSE(result.ok());
    CHECK(hasError(result, "program.json", "stages[1].research",
                   "tech 'program_stage_2' must not require other research"));
}
