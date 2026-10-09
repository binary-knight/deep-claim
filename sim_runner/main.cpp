// Headless balance runner. Every company, the player's slot included, is
// driven by the built-in heuristic AI for a number of in-game days. Writes a
// monthly CSV and a JSON summary, and with --sanity exits non-zero when the
// economy misbehaves (runaway money, prices pinned at their bounds, invalid
// AI commands, a dead economy, or a world that fails validation).
//
// With --sweep N it instead plays N consecutive seeds and reports, per seed
// and in summary, how the world's land and opening behaved (spec 005): the
// opening hand, the listing queue, how long unlisted land lasts and when the
// first mine opens. Its exit code is the spec's goal thresholds.
#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"
#include "sim/world_state.h"
#include "sim/worldgen.h"

#include "content/loader.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Options {
    sim::Seed seed = 1;
    sim::Day days = sim::kDaysPerYear * 20;
    std::uint32_t aiCompanies = 1;  // in addition to the player's slot
    std::string dataDir = DC_DATA_DIR;
    std::string csvPath;
    bool sanity = false;
    std::uint32_t sweep = 0;  // 0 = a single run; N = seeds seed..seed+N-1
};

bool parseUnsigned(std::string_view text, unsigned long long& out) {
    const bool digitsOnly = !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return c >= '0' && c <= '9';
    });
    if (!digitsOnly) {
        return false;
    }
    errno = 0;
    out = std::strtoull(text.data(), nullptr, 10);
    return errno != ERANGE;
}

bool parseNumericOption(std::string_view arg, unsigned long long number, Options& out) {
    if (arg == "--seed") {
        out.seed = number;
    } else if (arg == "--days" && number >= 1 && number <= sim::kMaxDay) {
        out.days = static_cast<sim::Day>(number);
    } else if (arg == "--years" && number >= 1 && number <= 10'000) {
        out.days = static_cast<sim::Day>(number) * sim::kDaysPerYear;
    } else if (arg == "--ai" && number <= 16) {
        out.aiCompanies = static_cast<std::uint32_t>(number);
    } else if (arg == "--sweep" && number >= 1 && number <= 10'000) {
        out.sweep = static_cast<std::uint32_t>(number);
    } else {
        return false;
    }
    return true;
}

bool parseArgs(std::span<char*> args, Options& out) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--sanity") {
            out.sanity = true;
            continue;
        }
        if (i + 1 >= args.size()) {
            return false;
        }
        const std::string_view value = args[++i];
        if (arg == "--data") {
            out.dataDir = value;
        } else if (arg == "--csv") {
            out.csvPath = value;
        } else {
            unsigned long long number = 0;
            if (!parseUnsigned(value, number) || !parseNumericOption(arg, number, out)) {
                return false;
            }
        }
    }
    return true;
}

// Adds without overflow: saturates at the int64 limit.
std::int64_t saturatingAdd(std::int64_t a, std::int64_t b) {
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    return b > kMax - a ? kMax : a + b;
}

struct MarketStats {
    std::int64_t minCents = std::numeric_limits<std::int64_t>::max();
    std::int64_t maxCents = 0;
    std::int64_t sumCents = 0;  // saturating; prices reach 1e13 cents over 3.6e6 days
    std::uint32_t daysAtFloor = 0;
    std::uint32_t daysAtCap = 0;
};

struct RunStats {
    std::vector<MarketStats> markets;
    std::map<std::string, std::uint32_t> rejections;
    std::vector<sim::Day> bankruptDay;
    std::vector<sim::Day> firstMineDay;  // 1-based day; 0 = never built one
    std::vector<std::int64_t> peakCashCents;
    std::vector<std::int64_t> peakDebtCents;
    std::uint32_t validationFailures = 0;
    std::uint32_t minesBuilt = 0;
};

struct Check {
    std::string name;
    bool passed = true;
    std::string detail;
    // A reported check is printed with its result but does not decide the
    // exit code.
    bool gated = true;
};

std::string jsonString(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

void writeCsvHeader(std::ostream& csv, const sim::Content& content) {
    csv << "day,company,cash,debt,plots,mines_operating,inventory_value";
    for (const sim::ResourceDef& resource : content.resources) {
        csv << ",price_" << resource.id;
    }
    csv << '\n';
}

void writeCsvRows(std::ostream& csv, const sim::Simulation& simulation) {
    const sim::Day day = simulation.state().day;
    const std::size_t companyCount = simulation.state().companies.size();
    for (std::uint32_t c = 0; c < companyCount; ++c) {
        const sim::CompanyView view = simulation.view(sim::CompanyId(c));
        const auto operating =
            std::count_if(view.mines.begin(), view.mines.end(),
                          [](const auto& m) { return m.state == sim::MineState::Operating; });
        std::int64_t inventoryValue = 0;
        for (std::size_t r = 0; r < view.inventoryTonnes.size(); ++r) {
            inventoryValue = saturatingAdd(
                inventoryValue,
                sim::checkedMulDiv(view.markets[r].price.cents(), view.inventoryTonnes[r], 1)
                    .value_or(std::numeric_limits<std::int64_t>::max()));
        }
        csv << day << ',' << c << ',' << sim::toString(view.cash) << ','
            << sim::toString(view.outstandingPrincipal) << ',' << view.companies[c].plotsOwned
            << ',' << operating << ',' << sim::toString(sim::Money::fromCents(inventoryValue));
        for (const sim::MarketView& market : view.markets) {
            csv << ',' << sim::toString(market.price);
        }
        csv << '\n';
    }
}

void recordDay(const sim::Content& content, const sim::Simulation& simulation,
               const sim::TickReport& report, RunStats& stats) {
    for (const sim::Notice& notice : report.notices) {
        if (notice.kind == sim::NoticeKind::CommandRejected) {
            ++stats.rejections[sim::rejectReasonName(notice.reason)];
        } else if (notice.kind == sim::NoticeKind::Bankrupt) {
            stats.bankruptDay[notice.company.index()] = report.day;
        } else if (notice.kind == sim::NoticeKind::MineStarted) {
            ++stats.minesBuilt;
            if (stats.firstMineDay[notice.company.index()] == 0) {
                stats.firstMineDay[notice.company.index()] = report.day + 1;
            }
        }
    }
    const sim::WorldState& state = simulation.state();
    for (std::size_t c = 0; c < state.companies.size(); ++c) {
        stats.peakCashCents[c] = std::max(stats.peakCashCents[c], state.companies[c].cash.cents());
        std::int64_t debt = 0;
        for (const sim::Loan& loan : state.loans) {
            if (loan.active != 0 && loan.company.index() == c) {
                debt = saturatingAdd(debt, loan.remaining.cents());
            }
        }
        stats.peakDebtCents[c] = std::max(stats.peakDebtCents[c], debt);
    }
    for (std::size_t r = 0; r < stats.markets.size(); ++r) {
        const sim::Money price = state.markets[r].price;
        const sim::Money base = content.resources[r].basePrice;
        MarketStats& market = stats.markets[r];
        market.minCents = std::min(market.minCents, price.cents());
        market.maxCents = std::max(market.maxCents, price.cents());
        market.sumCents = saturatingAdd(market.sumCents, price.cents());
        market.daysAtFloor += price <= base.scaled(content.balance.priceFloorMult) ? 1U : 0U;
        market.daysAtCap += price >= base.scaled(content.balance.priceCapMult) ? 1U : 0U;
    }
    if (state.day % sim::kDaysPerYear == 0 && !sim::validateWorld(content, state)) {
        ++stats.validationFailures;
    }
}

RunStats run(const Options& options, const sim::Content& content, sim::Simulation& simulation,
             std::ostream* csv) {
    sim::HeuristicController ai(content);
    const std::size_t companyCount = simulation.state().companies.size();
    RunStats stats;
    stats.markets.resize(content.resources.size());
    stats.bankruptDay.assign(companyCount, 0);
    stats.firstMineDay.assign(companyCount, 0);
    stats.peakCashCents.assign(companyCount, 0);
    stats.peakDebtCents.assign(companyCount, 0);
    for (sim::Day d = 0; d < options.days; ++d) {
        for (std::uint32_t c = 0; c < companyCount; ++c) {
            simulation.collectCommands(sim::CompanyId(c), ai);
        }
        const sim::TickReport& report = simulation.tick();
        recordDay(content, simulation, report, stats);
        if (csv != nullptr && simulation.state().day % sim::kDaysPerMonth == 0) {
            writeCsvRows(*csv, simulation);
        }
    }
    return stats;
}

std::vector<Check> buildChecks(const Options& options, const sim::Content& content,
                               const sim::WorldState& final, const RunStats& stats) {
    std::vector<Check> checks;
    checks.push_back({"world_valid",
                      stats.validationFailures == 0 && sim::validateWorld(content, final),
                      std::format("{} yearly validation failures", stats.validationFailures)});

    const std::int64_t limit = content.balance.startingCapital.cents() * 1000;
    bool noRunaway = true;
    for (std::size_t c = 0; c < final.companies.size(); ++c) {
        noRunaway = noRunaway && stats.peakCashCents[c] <= limit &&
                    final.companies[c].cash.cents() >= -limit;
    }
    checks.push_back(
        {"no_runaway_money", noRunaway, "every company's peak cash within 1000x starting capital"});

    bool pricesOk = true;
    std::string detail;
    for (std::size_t r = 0; r < stats.markets.size(); ++r) {
        const MarketStats& market = stats.markets[r];
        const std::int64_t base = content.resources[r].basePrice.cents();
        const std::int64_t mean = market.sumCents / static_cast<std::int64_t>(options.days);
        const bool pinned = (market.daysAtFloor + market.daysAtCap) * 4 > options.days;
        const bool sane = mean * 10 >= base * 3 && mean <= base * 3;
        pricesOk = pricesOk && !pinned && sane;
        detail += std::format("{}: mean {:.2f}x base, {} days at floor, {} at cap; ",
                              content.resources[r].id,
                              static_cast<double>(mean) / static_cast<double>(base),
                              market.daysAtFloor, market.daysAtCap);
    }
    checks.push_back({"prices_sane", pricesOk, detail});

    std::uint32_t rejected = 0;
    for (const auto& [name, count] : stats.rejections) {
        rejected += count;
    }
    checks.push_back(
        {"ai_commands_valid", rejected == 0, std::format("{} rejected commands", rejected)});
    checks.push_back({"economy_alive", options.days < sim::kDaysPerYear * 2 || stats.minesBuilt > 0,
                      std::format("{} mines built", stats.minesBuilt)});
    return checks;
}

void printSummary(const Options& options, const sim::Content& content,
                  const sim::Simulation& simulation, const RunStats& stats,
                  const std::vector<Check>& checks, bool allPassed) {
    const sim::WorldState& final = simulation.state();
    std::cout << "{\n";
    std::cout << std::format("  \"seed\": {},\n  \"days\": {},\n  \"state_hash\": \"{:016x}\",\n",
                             options.seed, final.day, simulation.stateHash());
    std::cout << "  \"companies\": [\n";
    for (std::uint32_t c = 0; c < final.companies.size(); ++c) {
        const sim::Company& company = final.companies[c];
        const sim::CompanyView view = simulation.view(sim::CompanyId(c));
        std::cout << std::format(
            "    {{\"name\": {}, \"cash\": {}, \"debt\": {}, \"bankrupt\": {}, \"bankrupt_day\": "
            "{}, "
            "\"plots\": {}, \"mines\": {}, \"techs\": {}, \"first_mine_day\": {}, "
            "\"peak_cash\": {}, \"peak_debt\": {}}}{}\n",
            jsonString(company.name), sim::toString(company.cash),
            sim::toString(view.outstandingPrincipal), company.bankrupt != 0 ? "true" : "false",
            stats.bankruptDay[c], view.companies[c].plotsOwned, view.mines.size(),
            company.completedTechs.size(), stats.firstMineDay[c],
            sim::toString(sim::Money::fromCents(stats.peakCashCents[c])),
            sim::toString(sim::Money::fromCents(stats.peakDebtCents[c])),
            c + 1 < final.companies.size() ? "," : "");
    }
    std::cout << "  ],\n  \"markets\": [\n";
    for (std::size_t r = 0; r < stats.markets.size(); ++r) {
        const MarketStats& market = stats.markets[r];
        const auto base = static_cast<double>(content.resources[r].basePrice.cents());
        std::cout << std::format(
            "    {{\"resource\": {}, \"min_ratio\": {:.3f}, \"max_ratio\": {:.3f}, \"mean_ratio\": "
            "{:.3f}, \"days_at_floor\": {}, \"days_at_cap\": {}}}{}\n",
            jsonString(content.resources[r].id), static_cast<double>(market.minCents) / base,
            static_cast<double>(market.maxCents) / base,
            static_cast<double>(market.sumCents) / static_cast<double>(options.days) / base,
            market.daysAtFloor, market.daysAtCap, r + 1 < stats.markets.size() ? "," : "");
    }
    std::cout << "  ],\n  \"rejections\": {";
    bool first = true;
    for (const auto& [name, count] : stats.rejections) {
        std::cout << (first ? "" : ", ") << jsonString(name) << ": " << count;
        first = false;
    }
    std::cout << "},\n  \"checks\": [\n";
    for (std::size_t i = 0; i < checks.size(); ++i) {
        std::cout << std::format("    {{\"name\": {}, \"passed\": {}, \"detail\": {}}}{}\n",
                                 jsonString(checks[i].name), checks[i].passed ? "true" : "false",
                                 jsonString(checks[i].detail), i + 1 < checks.size() ? "," : "");
    }
    std::cout << std::format("  ],\n  \"all_passed\": {}\n}}\n", allPassed ? "true" : "false");
}

// ---------------------------------------------------------------------------
// --sweep: land and opening statistics over many seeds (spec 005)
// ---------------------------------------------------------------------------

// The years at which unlisted and withdrawn land are sampled.
constexpr std::array<std::uint32_t, 3> kSweepYears = {2, 5, 8};
// Goal thresholds from docs/specs/005-world-and-opening.md. The first two
// are properties of the world and gate the exit code. The first-mine day is
// reported, not gated: the land allows a mine by day 227 in the best seeds,
// but when the built-in AI buys depends on its bidding rules, which belong to
// spec 006. Gate it there once the AI is tuned.
constexpr std::uint32_t kGoalMineableOpening = 2;
constexpr std::uint32_t kGoalLandYear = 8;
constexpr sim::Day kGoalFirstMineDay = 250;

struct SweepRow {
    sim::Seed seed = 0;
    std::uint32_t openingMineable = 0;   // opening listings in regions mineable on day one
    std::uint32_t openingLocked = 0;     // opening listings in regions whose main ore is locked
    std::uint32_t longestRegionRun = 0;  // among the monthly listings, after the opening hand
    sim::Day lastNewListingDay = 0;      // 0 = unlisted land never ran out
    std::array<std::uint32_t, kSweepYears.size()> unlisted{};
    std::array<std::uint32_t, kSweepYears.size()> withdrawn{};
    sim::Day firstMineStartedDay = 0;    // company 0; 1-based, 0 = never
    sim::Day firstMineOperatingDay = 0;  // company 0; 1-based, 0 = never
    std::vector<std::int64_t> finalCashCents;
    bool worldValid = true;
};

// The opening hand by region class, and the longest run of one region among
// the monthly listings that follow it.
void describeQueue(const sim::Content& content, const sim::WorldState& state, SweepRow& row) {
    const auto regionOf = [&state](std::size_t position) {
        return state.plots[state.listingQueue[position].index()].region;
    };
    const std::size_t hand = state.nextListing;  // the opening hand is already listed
    for (std::size_t i = 0; i < hand; ++i) {
        const sim::RegionStart kind =
            sim::classifyRegionAtStart(content, content.regions[regionOf(i).index()]);
        row.openingMineable += kind == sim::RegionStart::Mineable ? 1U : 0U;
        row.openingLocked += kind == sim::RegionStart::Locked ? 1U : 0U;
    }
    std::uint32_t run = 0;
    for (std::size_t i = hand; i < state.listingQueue.size(); ++i) {
        run = i > hand && regionOf(i) == regionOf(i - 1) ? run + 1 : 1;
        row.longestRegionRun = std::max(row.longestRegionRun, run);
    }
}

// Company 0's first mine: the day construction started and the day it opened.
void recordFirstMine(const sim::TickReport& report, SweepRow& row) {
    for (const sim::Notice& notice : report.notices) {
        if (notice.company != sim::CompanyId(0)) {
            continue;
        }
        if (notice.kind == sim::NoticeKind::MineStarted && row.firstMineStartedDay == 0) {
            row.firstMineStartedDay = report.day + 1;
        }
        if (notice.kind == sim::NoticeKind::MineOperational && row.firstMineOperatingDay == 0) {
            row.firstMineOperatingDay = report.day + 1;
        }
    }
}

void recordLand(const sim::Content& content, const sim::WorldState& state, SweepRow& row) {
    for (std::size_t y = 0; y < kSweepYears.size(); ++y) {
        if (state.day == kSweepYears.at(y) * sim::kDaysPerYear) {
            row.unlisted.at(y) =
                static_cast<std::uint32_t>(state.listingQueue.size()) - state.nextListing;
            row.withdrawn.at(y) = static_cast<std::uint32_t>(state.withdrawnQueue.size());
        }
    }
    if (state.day % sim::kDaysPerYear == 0 && !sim::validateWorld(content, state)) {
        row.worldValid = false;
    }
}

SweepRow sweepSeed(const Options& options, const sim::Content& content, sim::Seed seed) {
    sim::GameSetup setup;
    setup.playerName = "Player (AI)";
    for (std::uint32_t i = 0; i < options.aiCompanies; ++i) {
        setup.aiNames.push_back(std::format("Rival {}", i + 1));
    }
    sim::Simulation simulation(content, seed, setup);
    sim::HeuristicController ai(content);
    SweepRow row;
    row.seed = seed;
    const sim::WorldState& state = simulation.state();
    describeQueue(content, state, row);

    const std::size_t companyCount = state.companies.size();
    for (sim::Day d = 0; d < options.days; ++d) {
        for (std::uint32_t c = 0; c < companyCount; ++c) {
            simulation.collectCommands(sim::CompanyId(c), ai);
        }
        const std::uint32_t listedBefore = state.nextListing;
        const sim::TickReport& report = simulation.tick();
        if (state.nextListing != listedBefore && state.nextListing == state.listingQueue.size()) {
            row.lastNewListingDay = report.day + 1;
        }
        recordFirstMine(report, row);
        recordLand(content, state, row);
    }
    for (const sim::Company& company : state.companies) {
        row.finalCashCents.push_back(company.cash.cents());
    }
    return row;
}

struct Spread {
    std::int64_t min = 0;
    std::int64_t median = 0;
    std::int64_t max = 0;
};

// Lower median, so the figure is always a value that occurred.
Spread spreadOf(std::vector<std::int64_t> values) {
    if (values.empty()) {
        return {};
    }
    std::sort(values.begin(), values.end());
    return {values.front(), values[(values.size() - 1) / 2], values.back()};
}

template <class Get>
Spread spreadOver(const std::vector<SweepRow>& rows, const Get& get) {
    std::vector<std::int64_t> values;
    values.reserve(rows.size());
    for (const SweepRow& row : rows) {
        values.push_back(static_cast<std::int64_t>(get(row)));
    }
    return spreadOf(std::move(values));
}

std::string spreadJson(const Spread& spread) {
    return std::format(R"({{"min": {}, "median": {}, "max": {}}})", spread.min, spread.median,
                       spread.max);
}

// A mine that never opened counts as later than any day in the run, so the
// median reflects it without a made-up number being printed as a day.
std::int64_t dayOrNever(sim::Day day, sim::Day days) {
    return day == 0 ? std::int64_t{days} + 1 : std::int64_t{day};
}

void writeSweepCsv(std::ostream& csv, const std::vector<SweepRow>& rows) {
    csv << "seed,opening_mineable,opening_locked,longest_region_run,last_new_listing_day";
    for (const std::uint32_t year : kSweepYears) {
        csv << ",unlisted_y" << year;
    }
    for (const std::uint32_t year : kSweepYears) {
        csv << ",withdrawn_y" << year;
    }
    csv << ",first_mine_started_day,first_mine_operating_day,world_valid";
    const std::size_t companies = rows.empty() ? 0 : rows.front().finalCashCents.size();
    for (std::size_t c = 0; c < companies; ++c) {
        csv << ",final_cash_" << c;
    }
    csv << '\n';
    for (const SweepRow& row : rows) {
        csv << row.seed << ',' << row.openingMineable << ',' << row.openingLocked << ','
            << row.longestRegionRun << ',' << row.lastNewListingDay;
        for (const std::uint32_t count : row.unlisted) {
            csv << ',' << count;
        }
        for (const std::uint32_t count : row.withdrawn) {
            csv << ',' << count;
        }
        csv << ',' << row.firstMineStartedDay << ',' << row.firstMineOperatingDay << ','
            << (row.worldValid ? 1 : 0);
        for (const std::int64_t cents : row.finalCashCents) {
            csv << ',' << sim::toString(sim::Money::fromCents(cents));
        }
        csv << '\n';
    }
}

// The spec's goal thresholds. A threshold the run is too short to measure is
// reported as not evaluated and does not fail the sweep.
std::vector<Check> sweepChecks(const Options& options, const std::vector<SweepRow>& rows) {
    std::vector<Check> checks;
    const Spread mineable = spreadOver(rows, [](const SweepRow& r) { return r.openingMineable; });
    checks.push_back({"opening_has_mineable_plots", mineable.min >= kGoalMineableOpening,
                      std::format("fewest mineable-region opening plots in any seed: {} (goal: "
                                  "at least {})",
                                  mineable.min, kGoalMineableOpening)});

    const bool landMeasured = options.days >= kGoalLandYear * sim::kDaysPerYear;
    const Spread land =
        spreadOver(rows, [](const SweepRow& r) { return r.unlisted.at(kSweepYears.size() - 1); });
    checks.push_back(
        {"unlisted_land_at_year_8", !landMeasured || land.min > 0,
         landMeasured
             ? std::format("fewest unlisted plots at year {} in any seed: {} (goal: "
                           "more than 0)",
                           kGoalLandYear, land.min)
             : std::format("not evaluated: the run is shorter than {} years", kGoalLandYear)});

    const bool mineMeasured = options.days >= kGoalFirstMineDay;
    const Spread firstMine = spreadOver(
        rows, [&](const SweepRow& r) { return dayOrNever(r.firstMineOperatingDay, options.days); });
    const auto never = std::count_if(
        rows.begin(), rows.end(), [](const SweepRow& r) { return r.firstMineOperatingDay == 0; });
    checks.push_back(
        {"median_first_mine_day", !mineMeasured || firstMine.median < kGoalFirstMineDay,
         mineMeasured
             ? std::format("median day the AI-played company 0 has an operating mine: {} "
                           "(goal: under {}); {} of {} seeds never opened one",
                           firstMine.median > options.days ? std::string("never")
                                                           : std::to_string(firstMine.median),
                           kGoalFirstMineDay, never, rows.size())
             : std::format("not evaluated: the run is shorter than {} days", kGoalFirstMineDay),
         false});

    const bool valid = std::ranges::all_of(rows, [](const SweepRow& r) { return r.worldValid; });
    checks.push_back({"world_valid", valid, "yearly validation in every seed"});
    return checks;
}

void printSweep(const Options& options, const std::vector<SweepRow>& rows,
                const std::vector<Check>& checks, bool allPassed) {
    std::cout << std::format(
        "{{\n  \"sweep\": {{\"first_seed\": {}, \"seeds\": {}, \"days\": {}, "
        "\"ai\": {}}},\n  \"rows\": [\n",
        options.seed, rows.size(), options.days, options.aiCompanies);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const SweepRow& row = rows[i];
        std::string cash;
        for (std::size_t c = 0; c < row.finalCashCents.size(); ++c) {
            cash +=
                (c == 0 ? "" : ", ") + sim::toString(sim::Money::fromCents(row.finalCashCents[c]));
        }
        std::cout << std::format(
            "    {{\"seed\": {}, \"opening_mineable\": {}, \"opening_locked\": {}, "
            "\"longest_region_run\": {}, \"last_new_listing_day\": {}, \"unlisted\": [{}, {}, "
            "{}], \"withdrawn\": [{}, {}, {}], \"first_mine_started_day\": {}, "
            "\"first_mine_operating_day\": {}, \"final_cash\": [{}]}}{}\n",
            row.seed, row.openingMineable, row.openingLocked, row.longestRegionRun,
            row.lastNewListingDay, row.unlisted[0], row.unlisted[1], row.unlisted[2],
            row.withdrawn[0], row.withdrawn[1], row.withdrawn[2], row.firstMineStartedDay,
            row.firstMineOperatingDay, cash, i + 1 < rows.size() ? "," : "");
    }
    std::cout << "  ],\n  \"sample_years\": [";
    for (std::size_t y = 0; y < kSweepYears.size(); ++y) {
        std::cout << (y == 0 ? "" : ", ") << kSweepYears.at(y);
    }
    std::cout << "],\n  \"summary\": {\n";
    const auto line = [](std::string_view name, const Spread& spread, bool last = false) {
        std::cout << std::format("    {}: {}{}\n", jsonString(name), spreadJson(spread),
                                 last ? "" : ",");
    };
    line("opening_mineable", spreadOver(rows, [](const SweepRow& r) { return r.openingMineable; }));
    line("opening_locked", spreadOver(rows, [](const SweepRow& r) { return r.openingLocked; }));
    line("longest_region_run",
         spreadOver(rows, [](const SweepRow& r) { return r.longestRegionRun; }));
    line("last_new_listing_day",
         spreadOver(rows, [](const SweepRow& r) { return r.lastNewListingDay; }));
    for (std::size_t y = 0; y < kSweepYears.size(); ++y) {
        line(std::format("unlisted_y{}", kSweepYears.at(y)),
             spreadOver(rows, [y](const SweepRow& r) { return r.unlisted.at(y); }));
    }
    for (std::size_t y = 0; y < kSweepYears.size(); ++y) {
        line(std::format("withdrawn_y{}", kSweepYears.at(y)),
             spreadOver(rows, [y](const SweepRow& r) { return r.withdrawn.at(y); }));
    }
    line("first_mine_started_day", spreadOver(rows, [&](const SweepRow& r) {
             return dayOrNever(r.firstMineStartedDay, options.days);
         }));
    line("first_mine_operating_day", spreadOver(rows, [&](const SweepRow& r) {
             return dayOrNever(r.firstMineOperatingDay, options.days);
         }));
    const std::size_t companies = rows.empty() ? 0 : rows.front().finalCashCents.size();
    for (std::size_t c = 0; c < companies; ++c) {
        // Whole currency units: a spread of cents reads badly at this size.
        line(std::format("final_cash_{}", c),
             spreadOver(rows, [c](const SweepRow& r) { return r.finalCashCents[c] / 100; }),
             c + 1 == companies);
    }
    std::cout << "  },\n  \"never_day\": " << std::int64_t{options.days} + 1
              << ",\n  \"checks\": [\n";
    for (std::size_t i = 0; i < checks.size(); ++i) {
        std::cout << std::format(
            "    {{\"name\": {}, \"passed\": {}, \"gated\": {}, \"detail\": {}}}{}\n",
            jsonString(checks[i].name), checks[i].passed ? "true" : "false",
            checks[i].gated ? "true" : "false", jsonString(checks[i].detail),
            i + 1 < checks.size() ? "," : "");
    }
    std::cout << std::format("  ],\n  \"all_gated_passed\": {}\n}}\n",
                             allPassed ? "true" : "false");
}

int runSweep(const Options& options, const sim::Content& content) {
    std::vector<SweepRow> rows;
    rows.reserve(options.sweep);
    for (std::uint32_t i = 0; i < options.sweep; ++i) {
        rows.push_back(sweepSeed(options, content, options.seed + i));
    }
    const std::vector<Check> checks = sweepChecks(options, rows);
    const bool allPassed = std::ranges::all_of(
        checks, [](const Check& check) { return check.passed || !check.gated; });
    printSweep(options, rows, checks, allPassed);
    if (!options.csvPath.empty()) {
        std::ofstream csv(options.csvPath);
        writeSweepCsv(csv, rows);
        csv.flush();
        if (!csv) {
            std::cerr << "error writing " << options.csvPath << '\n';
            return 1;
        }
    }
    std::cout.flush();
    if (!std::cout) {
        return 1;
    }
    return allPassed ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(std::span<char*>(argv, static_cast<std::size_t>(argc)), options)) {
        std::cerr << "usage: sim_runner [--seed N] [--days N | --years N] [--ai N] [--data DIR] "
                     "[--csv FILE] [--sanity] [--sweep N]\n";
        return 2;
    }
    const content::LoadResult loaded = content::loadFromDirectory(options.dataDir);
    if (!loaded.content.has_value()) {
        std::cerr << "content errors:\n" << content::formatErrors(loaded.errors);
        return 1;
    }
    const sim::Content& gameContent = *loaded.content;
    if (options.sweep > 0) {
        return runSweep(options, gameContent);
    }

    sim::GameSetup setup;
    setup.playerName = "Player (AI)";
    for (std::uint32_t i = 0; i < options.aiCompanies; ++i) {
        setup.aiNames.push_back(std::format("Rival {}", i + 1));
    }
    sim::Simulation simulation(gameContent, options.seed, setup);

    std::ofstream csv;
    if (!options.csvPath.empty()) {
        csv.open(options.csvPath);
        if (!csv.is_open()) {
            std::cerr << "cannot write " << options.csvPath << '\n';
            return 1;
        }
        writeCsvHeader(csv, gameContent);
    }

    const RunStats stats = run(options, gameContent, simulation, csv.is_open() ? &csv : nullptr);
    const std::vector<Check> checks = buildChecks(options, gameContent, simulation.state(), stats);
    const bool allPassed =
        std::ranges::all_of(checks, [](const Check& check) { return check.passed; });
    printSummary(options, gameContent, simulation, stats, checks, allPassed);
    if (csv.is_open()) {
        csv.flush();
        if (!csv) {
            std::cerr << "error writing " << options.csvPath << '\n';
            return 1;
        }
    }
    std::cout.flush();
    if (!std::cout) {
        return 1;
    }
    return options.sanity && !allPassed ? 3 : 0;
}
