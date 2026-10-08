// Headless replay of a playtest trace. Usage:
//   deep_claim_replay <trace.jsonl> [--day N] [--save out.dcsave] [--data dir] [--log]
// Rebuilds the player's game from the trace, reports the first day whose
// state hash differs from what the client recorded, and prints the player's
// view at the stop day (the end of the trace by default). Save files the
// trace refers to are read from the trace's directory.
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/view.h"
#include "sim/world_state.h"

#include "content/loader.h"
#include "game/format.h"
#include "game/replay.h"
#include "game/session.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

const char* mineStateName(sim::MineState state) {
    switch (state) {
        case sim::MineState::Building:
            return "building";
        case sim::MineState::Operating:
            return "operating";
        case sim::MineState::Mothballed:
            return "mothballed";
        case sim::MineState::Depleted:
            return "depleted";
        case sim::MineState::Closed:
            return "closed";
    }
    return "?";
}

std::string companyName(const sim::CompanyView& view, sim::CompanyId id) {
    for (const sim::CompanyPublicView& company : view.companies) {
        if (company.id == id) {
            return company.name;
        }
    }
    return "-";
}

std::string plotStatus(const sim::CompanyView& v, const sim::PlotView& p) {
    switch (p.listing) {
        case sim::PlotListing::Unlisted:
            return "unlisted";
        case sim::PlotListing::Listed:
            if (p.auction.has_value()) {
                return std::format("listed, auction closes day {} ({} bids, mine {})",
                                   p.auction->closesOnDay, p.auction->bidCount,
                                   game::formatMoney(p.auction->myBid));
            }
            return "listed";
        case sim::PlotListing::Sold:
            return "owned by " + companyName(v, p.owner);
    }
    return "?";
}

void printPlots(const sim::Content& content, const sim::CompanyView& v) {
    std::cout << "\nplots:\n";
    for (const sim::PlotView& p : v.plots) {
        std::string mine;
        if (p.mine.has_value()) {
            mine = std::format("  mine #{} {} ({})", p.mine->id.value(),
                               mineStateName(p.mine->state), companyName(v, p.mine->owner));
        }
        std::cout << std::format("  #{:<3} {:<18} {:>7} ha  ask {:>9}  capex {:>9}  {}{}\n",
                                 p.id.value(), content.regions[p.region.index()].name,
                                 p.areaHectares, game::formatMoney(p.askPrice),
                                 game::formatMoney(p.mineCapexQuote), plotStatus(v, p), mine);
    }
}

void printSurveys(const sim::Content& content, const sim::CompanyView& v) {
    if (v.surveys.empty()) {
        return;
    }
    std::cout << "\nsurveys:\n";
    for (const sim::SurveyView& s : v.surveys) {
        std::cout << std::format("  plot #{} commissioned day {} completes day {}{}\n",
                                 s.plot.value(), s.commissionedDay, s.completesDay,
                                 s.complete ? "" : " (pending)");
        for (const sim::SurveyEstimateView& e : s.estimates) {
            std::cout << std::format(
                "    {:<10} ~{} (error {})\n", content.resources[e.resource.index()].name,
                game::formatTonnes(e.tonnesEstimate), game::formatRatio(e.relativeError));
        }
    }
}

void printMines(const sim::Content& content, const sim::CompanyView& v) {
    if (v.mines.empty()) {
        return;
    }
    std::cout << "\nmines:\n";
    for (const sim::MineView& m : v.mines) {
        std::cout << std::format("  #{} on plot #{} {} since day {}, opex {}/day\n", m.id.value(),
                                 m.plot.value(), mineStateName(m.state), m.operationalDay,
                                 game::formatMoney(m.dailyOpex));
        for (std::size_t r = 0; r < m.extractedTonnes.size(); ++r) {
            if (m.extractedTonnes[r] == 0 && m.lastOutputTonnes[r] == 0) {
                continue;
            }
            std::cout << std::format("    {:<10} {}/day, {} to date\n", content.resources[r].name,
                                     game::formatTonnes(m.lastOutputTonnes[r]),
                                     game::formatTonnes(m.extractedTonnes[r]));
        }
    }
}

void printMarkets(const sim::Content& content, const sim::CompanyView& v) {
    std::cout << "\nmarkets:\n";
    for (std::size_t r = 0; r < v.markets.size(); ++r) {
        const sim::MarketView& m = v.markets[r];
        const std::string stock =
            r < v.inventoryTonnes.size() ? game::formatTonnes(v.inventoryTonnes[r]) : "-";
        std::cout << std::format(
            "  {:<10} {:>10}/t (base {:>9})  demand {:>8} t/day  supply {:>8} t/day  stock {}\n",
            content.resources[r].name, game::formatMoney(m.price), game::formatMoney(m.basePrice),
            m.baseDemandTonnesPerDay, m.recentSupplyTonnesPerDay, stock);
    }
}

void printFinanceAndResearch(const sim::Content& content, const sim::CompanyView& v) {
    if (!v.loans.empty()) {
        std::cout << "\nloans:\n";
        for (const sim::LoanView& l : v.loans) {
            std::cout << std::format("  #{} principal {} remaining {} ({}/month, {} months){}\n",
                                     l.id.value(), game::formatMoney(l.principal),
                                     game::formatMoney(l.remaining),
                                     game::formatMoney(l.principalPerMonth), l.monthsRemaining,
                                     l.active ? "" : "  repaid");
        }
    }
    std::cout << "\nresearch: ";
    for (const sim::TechId tech : v.completedTechs) {
        std::cout << content.techs[tech.index()].name << "; ";
    }
    if (v.activeResearch.has_value()) {
        std::cout << std::format("researching {} until day {}",
                                 content.techs[v.activeResearch->tech.index()].name,
                                 v.activeResearch->completesDay);
    }
    std::cout << '\n';
    if (!v.events.empty()) {
        std::cout << "events:\n";
        for (const sim::ActiveEventView& e : v.events) {
            std::cout << std::format("  {} until day {}\n", content.events[e.event.index()].name,
                                     e.endsDay);
        }
    }
}

void printView(const sim::Content& content, const game::Session& session) {
    const sim::CompanyView& v = session.view();
    std::cout << std::format("\n== {} ({}) ==\n", game::formatDay(v.day), v.day);
    std::cout << std::format(
        "cash {}  reserved {}  debt {}  loan capacity {}  insolvent months {}{}\n",
        game::formatMoney(v.cash), game::formatMoney(v.reservedCash),
        game::formatMoney(v.outstandingPrincipal), game::formatMoney(v.loanCapacity),
        v.monthsInsolvent, v.bankrupt ? "  BANKRUPT" : "");
    std::cout << std::format("storage {}/month  mine opex quote {}/day  survey noise {}\n",
                             game::formatMoney(v.monthlyStorageCost),
                             game::formatMoney(v.mineOpexQuote),
                             game::formatRatio(v.surveyNoiseQuote));
    std::cout << "\ncompanies:\n";
    for (const sim::CompanyPublicView& c : v.companies) {
        std::cout << std::format("  #{} {:<24} plots {}{}\n", c.id.value(), c.name, c.plotsOwned,
                                 c.bankrupt ? "  bankrupt" : "");
    }
    printPlots(content, v);
    printSurveys(content, v);
    printMines(content, v);
    printMarkets(content, v);
    printFinanceAndResearch(content, v);
}

struct Args {
    std::filesystem::path trace;
    std::filesystem::path data = DC_DATA_DIR;
    std::filesystem::path save;
    std::optional<sim::Day> stopDay;
    bool printLog = false;
};

bool parseDay(std::string_view text, sim::Day& out) {
    const char* end = std::next(text.data(), static_cast<std::ptrdiff_t>(text.size()));
    const auto result = std::from_chars(text.data(), end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

// Returns false after printing usage.
bool parseArgs(std::span<char*> argv, Args& args) {
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argv.size();
        if (arg == "--day" && hasValue) {
            sim::Day day = 0;
            if (!parseDay(argv[++i], day)) {
                std::fputs("--day needs a non-negative integer\n", stderr);
                return false;
            }
            args.stopDay = day;
        } else if (arg == "--save" && hasValue) {
            args.save = argv[++i];
        } else if (arg == "--data" && hasValue) {
            args.data = argv[++i];
        } else if (arg == "--log") {
            args.printLog = true;
        } else if (!arg.starts_with("--") && args.trace.empty()) {
            args.trace = arg;
        } else {
            std::fputs(
                "usage: deep_claim_replay <trace.jsonl> [--day N] [--save out.dcsave] "
                "[--data dir] [--log]\n",
                stderr);
            return false;
        }
    }
    if (args.trace.empty()) {
        std::fputs("a trace file is required\n", stderr);
        return false;
    }
    return true;
}

bool writeSave(const std::filesystem::path& path, const game::Session& session) {
    const std::vector<std::uint8_t> bytes = session.save();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) byte buffer to stream
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

void printSummary(const game::ReplayResult& result) {
    std::cout << std::format("{} lines, {} orders, {} days replayed\n", result.lines,
                             result.commands, result.daysTicked);
    if (!result.ok()) {
        std::cout << "trace problem: " << result.error << '\n';
    }
    if (result.firstDivergence.has_value()) {
        std::cout << std::format(
            "DIVERGENCE: the replay stopped matching the client's state on day {}\n",
            *result.firstDivergence);
    } else if (result.daysTicked > 0) {
        std::cout << "every recorded day matched the client's state hash\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(std::span<char*>(argv, static_cast<std::size_t>(argc)), args)) {
        return 2;
    }
    const content::LoadResult loaded = content::loadFromDirectory(args.data);
    if (!loaded.content.has_value()) {
        std::fputs(content::formatErrors(loaded.errors).c_str(), stderr);
        return 1;
    }
    std::ifstream trace(args.trace);
    if (!trace) {
        std::cerr << "cannot open " << args.trace.string() << '\n';
        return 1;
    }
    game::ReplayOptions options;
    options.stopDay = args.stopDay;
    const std::filesystem::path traceDir = args.trace.parent_path();
    options.readSave = [traceDir](const std::string& file) {
        std::ifstream in(traceDir / file, std::ios::binary);
        if (!in) {
            return std::vector<std::uint8_t>{};
        }
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    };

    const game::ReplayResult result = game::replayTrace(*loaded.content, trace, options);
    printSummary(result);
    if (result.session == nullptr) {
        return result.ok() ? 0 : 1;
    }
    printView(*loaded.content, *result.session);
    if (args.printLog) {
        std::cout << "\nlog:\n";
        for (const game::LogEntry& entry : result.session->log()) {
            std::cout << std::format("  {:<10} {}\n", game::formatDay(entry.day), entry.text);
        }
    }
    if (!args.save.empty()) {
        if (!writeSave(args.save, *result.session)) {
            std::cerr << "could not write " << args.save.string() << '\n';
            return 1;
        }
        std::cout << "saved " << args.save.string() << '\n';
    }
    return result.ok() && !result.firstDivergence.has_value() ? 0 : 1;
}
