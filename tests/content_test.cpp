#include "sim/content.h"

#include "sim/fixed.h"
#include "sim/money.h"

#include "content/loader.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace {

// Minimal valid content. Tests mutate copies of this to provoke errors.
content::Sources goodSources() {
    content::Sources s;
    s.resources = R"([
      {"id": "iron", "name": "Iron", "base_price": 110, "base_demand_tonnes_per_day": 800,
       "base_output_tonnes_per_day": 400, "elasticity": 0.6, "volatility": 0.02, "storage_cost_per_tonne_month": 1.5,
       "unlocked_by": null},
      {"id": "lithium", "name": "Lithium", "base_price": 14000.25,
       "base_demand_tonnes_per_day": 8, "base_output_tonnes_per_day": 5, "elasticity": 1.1, "volatility": 0.05,
       "storage_cost_per_tonne_month": 8, "unlocked_by": "lithium_processing"}
    ])";
    s.techs = R"([
      {"id": "seismic_1", "name": "Seismic I", "cost": 600000, "duration_days": 90,
       "requires": [], "effects": [{"type": "survey_noise_mult", "value": 0.8}]},
      {"id": "lithium_processing", "name": "Lithium", "cost": 4000000, "duration_days": 240,
       "requires": ["seismic_1"], "effects": [{"type": "unlock_resource", "resource": "lithium"}]}
    ])";
    s.regions = R"([
      {"id": "north", "name": "North", "plot_count": 10,
       "land_price": {"min": 800000, "max": 2500000},
       "plot_area_hectares": {"min": 200, "max": 1200},
       "deposits": {"iron": {"presence_prob": 0.8, "tonnes_median": 700000, "tonnes_spread": 2.5}}}
    ])";
    s.events = R"([
      {"id": "boom", "name": "Boom", "daily_weight": 0.002, "min_year": 2, "duration_days": 180,
       "effects": [{"type": "demand_mult", "resource": "lithium", "value": 1.6},
                   {"type": "opex_mult", "value": 1.3}]}
    ])";
    s.balance = R"({
      "starting_capital": 5000000, "loan_annual_rate": 0.08, "loan_term_months": 60,
      "loan_max_principal": 10000000, "loan_min_principal": 250000, "max_active_loans": 4, "survey_cost": 150000, "survey_duration_days": 45,
      "survey_base_noise": 0.5, "mine_capex_per_hectare": 4000, "mine_build_days": 180,
      "mine_opex_per_day": 12000, "mine_mothball_opex_mult": 0.2,
      "auction_window_days": 30, "plots_listed_at_start": 3, "plots_listed_per_month": 1,
      "bankruptcy_grace_months": 3, "market_supply_smoothing_days": 30,
      "market_price_adjust_days": 10, "demand_reversion_rate": 0.01, "demand_mult_min": 0.3,
      "demand_mult_max": 3, "price_floor_mult": 0.1, "price_cap_mult": 10,
      "demand_growth_per_year": 0.04,
      "default_sell_policy": {"enabled": true, "reserve_tonnes": 0, "floor_price": 0},
      "default_cover_costs": true
    })";
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

    REQUIRE(c.techs.size() == 2);
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
    CHECK(result.errors.size() == 5);
    CHECK(result.errors[0].message.find("cannot read") != std::string::npos);
}
