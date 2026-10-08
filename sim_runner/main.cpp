// Headless balance runner. Every company, the player's slot included, is
// driven by the built-in heuristic AI for a number of in-game days. Writes a
// monthly CSV and a JSON summary, and with --sanity exits non-zero when the
// economy misbehaves (runaway money, prices pinned at their bounds, invalid
// AI commands, a dead economy, or a world that fails validation).
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

#include "content/loader.h"

#include <algorithm>
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
#include <vector>

namespace {

struct Options {
    sim::Seed seed = 1;
    sim::Day days = sim::kDaysPerYear * 20;
    std::uint32_t aiCompanies = 1;  // in addition to the player's slot
    std::string dataDir = DC_DATA_DIR;
    std::string csvPath;
    bool sanity = false;
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
    } else if (arg == "--days" && number <= std::numeric_limits<sim::Day>::max()) {
        out.days = static_cast<sim::Day>(number);
    } else if (arg == "--years" && number <= 10'000) {
        out.days = static_cast<sim::Day>(number) * sim::kDaysPerYear;
    } else if (arg == "--ai" && number <= 16) {
        out.aiCompanies = static_cast<std::uint32_t>(number);
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

struct MarketStats {
    std::int64_t minCents = std::numeric_limits<std::int64_t>::max();
    std::int64_t maxCents = 0;
    std::int64_t sumCents = 0;  // bounded: price <= 1e13 cents, days <= 3.6e6
    std::uint32_t daysAtFloor = 0;
    std::uint32_t daysAtCap = 0;
};

struct RunStats {
    std::vector<MarketStats> markets;
    std::map<std::string, std::uint32_t> rejections;
    std::vector<sim::Day> bankruptDay;
    std::uint32_t validationFailures = 0;
    std::uint32_t minesBuilt = 0;
};

struct Check {
    std::string name;
    bool passed = true;
    std::string detail;
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
            inventoryValue +=
                sim::checkedMulDiv(view.markets[r].price.cents(), view.inventoryTonnes[r], 1)
                    .value_or(0);
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
        }
    }
    const sim::WorldState& state = simulation.state();
    for (std::size_t r = 0; r < stats.markets.size(); ++r) {
        const sim::Money price = state.markets[r].price;
        const sim::Money base = content.resources[r].basePrice;
        MarketStats& market = stats.markets[r];
        market.minCents = std::min(market.minCents, price.cents());
        market.maxCents = std::max(market.maxCents, price.cents());
        market.sumCents += price.cents();
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
    const bool noRunaway = std::ranges::all_of(final.companies, [&](const sim::Company& company) {
        return company.cash.cents() <= limit && company.cash.cents() >= -limit;
    });
    checks.push_back(
        {"no_runaway_money", noRunaway, "every company within 1000x starting capital"});

    bool pricesOk = true;
    std::string detail;
    for (std::size_t r = 0; r < stats.markets.size(); ++r) {
        const MarketStats& market = stats.markets[r];
        const std::int64_t base = content.resources[r].basePrice.cents();
        const std::int64_t mean = market.sumCents / static_cast<std::int64_t>(options.days);
        const bool pinned =
            market.daysAtFloor * 4 > options.days || market.daysAtCap * 4 > options.days;
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
            "\"plots\": {}, \"mines\": {}, \"techs\": {}}}{}\n",
            jsonString(company.name), sim::toString(company.cash),
            sim::toString(view.outstandingPrincipal), company.bankrupt != 0 ? "true" : "false",
            stats.bankruptDay[c], view.companies[c].plotsOwned, view.mines.size(),
            company.completedTechs.size(), c + 1 < final.companies.size() ? "," : "");
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

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(std::span<char*>(argv, static_cast<std::size_t>(argc)), options)) {
        std::cerr << "usage: sim_runner [--seed N] [--days N | --years N] [--ai N] [--data DIR] "
                     "[--csv FILE] [--sanity]\n";
        return 2;
    }
    const content::LoadResult loaded = content::loadFromDirectory(options.dataDir);
    if (!loaded.content.has_value()) {
        std::cerr << "content errors:\n" << content::formatErrors(loaded.errors);
        return 1;
    }
    const sim::Content& gameContent = *loaded.content;

    sim::GameSetup setup;
    setup.playerName = "Player (AI)";
    for (std::uint32_t i = 0; i < options.aiCompanies; ++i) {
        setup.aiNames.push_back(std::format("Rival {}", i + 1));
    }
    sim::Simulation simulation(gameContent, options.seed, setup);

    std::ofstream csv;
    if (!options.csvPath.empty()) {
        csv.open(options.csvPath);
        if (!csv) {
            std::cerr << "cannot write " << options.csvPath << '\n';
            return 1;
        }
        writeCsvHeader(csv, gameContent);
    }

    const RunStats stats = run(options, gameContent, simulation, csv ? &csv : nullptr);
    const std::vector<Check> checks = buildChecks(options, gameContent, simulation.state(), stats);
    const bool allPassed =
        std::ranges::all_of(checks, [](const Check& check) { return check.passed; });
    printSummary(options, gameContent, simulation, stats, checks, allPassed);
    return options.sanity && !allPassed ? 3 : 0;
}
