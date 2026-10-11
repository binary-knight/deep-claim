// Headless balance runner. Every company, the player's slot included, is
// driven by the built-in heuristic AI for a number of in-game days. Writes a
// monthly CSV and a JSON summary, and with --sanity exits non-zero when the
// economy misbehaves (runaway money, prices pinned at their bounds, invalid
// AI commands, a dead economy, or a world that fails validation).
//
// The game ends (spec 010): the first company to complete the program founds
// it, or the deadline passes with no founder. A run plays until the game ends
// or the requested days run out, whichever is first, and never ticks a
// finished world. The deadline closes the day after the last one a run of
// exactly that length would play, so such a run plays that one day more:
// --years 30 reaches the deadline outcome. Nothing else plays past its days.
// Days in the output are 1-based (Outcome::day + 1 for the day the game
// ended); a day that never came is printed as never_day, one past the last
// day the run could have played.
//
// With --sweep N it instead plays N consecutive seeds and reports, per seed
// and in summary, how the world's land and opening behaved (spec 005): the
// opening hand, the listing queue, how long unlisted land lasts and when the
// first mine opens; and how the contest between the companies went (spec
// 006): mines per company, bankruptcies, how long mines last and where
// prices sat; and whether the markets bite (spec 008): how often each ore
// sells below what a mine at base output needs, how far its yearly price
// moves, how long mines stand idle and what a company holds at year 10; and how the
// race went (spec 010): who founded the program and when, the day of each
// stage for each company, and how each seed's game ended. A seed stops when
// its game ends, and a figure sampled at a year that seed never reached is
// absent (null), never the value of the finished world. Its exit code is the
// gated checks, with or without --sanity: the race (spec 010, BR-013) and the
// checks that hold in a game that ends early. The checks of specs 005 to 008
// that assumed a 20-year open-ended game are reported and not gated.
#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/program.h"
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
#include <optional>
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
    // Set once the content is loaded: the most days a run may play. It is
    // `days`, and one more when `days` stops exactly on the deadline, whose
    // day is the next one (see tickLimitFor).
    sim::Day tickLimit = 0;
};

// The deadline closes at the end of the day numbered Content::program's
// deadlineDay, counting from zero: one day after a run of exactly that many
// days would stop. Such a run plays it, so a run to the deadline always has
// an outcome. Any other run plays its days and no more.
sim::Day tickLimitFor(sim::Day days, const sim::Content& content) {
    return days == content.program.deadlineDay ? days + 1 : days;
}

// One past the last day the run could have played: printed for a day that
// never came, and later than any day that did.
std::int64_t neverDay(const Options& options) {
    return std::int64_t{options.tickLimit} + 1;
}

bool keepPlaying(const Options& options, const sim::WorldState& state) {
    return !sim::gameEnded(state) && state.day < options.tickLimit;
}

// How a run's game stood when it stopped.
enum class RunOutcome : std::uint8_t {
    Running,   // the run stopped before the game ended
    Founded,   // a company founded the program
    Deadline,  // the deadline passed with no founder
};

RunOutcome outcomeOf(const sim::WorldState& state) {
    if (!sim::gameEnded(state)) {
        return RunOutcome::Running;
    }
    return state.outcome.founder.valid() ? RunOutcome::Founded : RunOutcome::Deadline;
}

std::string_view outcomeName(RunOutcome outcome) {
    switch (outcome) {
        case RunOutcome::Founded:
            return "founded";
        case RunOutcome::Deadline:
            return "deadline";
        case RunOutcome::Running:
            break;
    }
    return "running";
}

// The founder's index, or -1 when nobody founded the program.
std::int64_t founderIndex(const sim::WorldState& state) {
    if (outcomeOf(state) != RunOutcome::Founded) {
        return -1;
    }
    return static_cast<std::int64_t>(state.outcome.founder.index());
}

// A 1-based day, or the never value for a day recorded as zero.
std::int64_t dayOrNever(sim::Day day, std::int64_t never) {
    return day == 0 ? never : std::int64_t{day};
}

// The stage days of every company start as never (zero).
std::vector<std::vector<sim::Day>> noStageDays(const sim::Content& content, std::size_t companies) {
    return {companies, std::vector<sim::Day>(content.program.stages.size(), 0)};
}

// The day a company completed a stage, from the day's notices (1-based).
void recordStageDays(const sim::TickReport& report, std::vector<std::vector<sim::Day>>& stageDays) {
    for (const sim::Notice& notice : report.notices) {
        if (notice.kind != sim::NoticeKind::ProgramStageComplete) {
            continue;
        }
        std::vector<sim::Day>& days = stageDays[notice.company.index()];
        const auto stage = static_cast<std::size_t>(notice.tonnes);  // 1 for the first
        if (stage >= 1 && stage <= days.size() && days[stage - 1] == 0) {
            days[stage - 1] = report.day + 1;
        }
    }
}

// An order refused because the game had just ended is expected: a company
// cannot see the other's founding coming, and its orders queued behind the
// one that founds the program are refused the same day. It does not count
// against the AI.
bool countsAgainstAi(const sim::Notice& notice) {
    return notice.kind == sim::NoticeKind::CommandRejected &&
           notice.reason != sim::RejectReason::GameEnded;
}

template <class Range>
std::string joined(const Range& values) {
    std::string out;
    for (const auto& value : values) {
        out += (out.empty() ? "" : ", ") + std::to_string(value);
    }
    return out;
}

// A company's stage days as a JSON list, never for a stage not reached.
std::string stageDaysJson(const std::vector<sim::Day>& days, std::int64_t never) {
    std::vector<std::int64_t> printed;
    printed.reserve(days.size());
    for (const sim::Day day : days) {
        printed.push_back(dayOrNever(day, never));
    }
    return "[" + joined(printed) + "]";
}

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
    if (out.sweep > 0) {
        // A sweep's seeds must not wrap round. It has its own checks and
        // always exits by them, so --sanity is accepted and adds nothing.
        const sim::Seed lastOffset = out.sweep - 1;
        if (out.seed > std::numeric_limits<sim::Seed>::max() - lastOffset) {
            return false;
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
    std::uint32_t rejectedAgainstAi = 0;  // all but those refused because the game had ended
    std::vector<std::vector<sim::Day>> stageDays;  // per company and stage; 1-based, 0 = never
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
    csv << "day,company,cash,debt,plots,mines_operating,mines_mothballed,mines_depleted,"
           "inventory_value";
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
        const auto inState = [&view](sim::MineState state) {
            return std::count_if(view.mines.begin(), view.mines.end(),
                                 [state](const auto& m) { return m.state == state; });
        };
        std::int64_t inventoryValue = 0;
        for (std::size_t r = 0; r < view.inventoryTonnes.size(); ++r) {
            inventoryValue = saturatingAdd(
                inventoryValue,
                sim::checkedMulDiv(view.markets[r].price.cents(), view.inventoryTonnes[r], 1)
                    .value_or(std::numeric_limits<std::int64_t>::max()));
        }
        csv << day << ',' << c << ',' << sim::toString(view.cash) << ','
            << sim::toString(view.outstandingPrincipal) << ',' << view.companies[c].plotsOwned
            << ',' << inState(sim::MineState::Operating) << ','
            << inState(sim::MineState::Mothballed) << ',' << inState(sim::MineState::Depleted)
            << ',' << sim::toString(sim::Money::fromCents(inventoryValue));
        for (const sim::MarketView& market : view.markets) {
            csv << ',' << sim::toString(market.price);
        }
        csv << '\n';
    }
}

void recordDay(const sim::Content& content, const sim::Simulation& simulation,
               const sim::TickReport& report, RunStats& stats) {
    for (const sim::Notice& notice : report.notices) {
        stats.rejectedAgainstAi += countsAgainstAi(notice) ? 1U : 0U;
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
    recordStageDays(report, stats.stageDays);
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
    stats.stageDays = noStageDays(content, companyCount);
    while (keepPlaying(options, simulation.state())) {
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

std::vector<Check> buildChecks(const sim::Content& content, const sim::WorldState& final,
                               const RunStats& stats) {
    std::vector<Check> checks;
    // Prices were recorded on the days played, which stop when the game ends.
    const sim::Day played = std::max<sim::Day>(final.day, 1);
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
        const std::int64_t mean = market.sumCents / static_cast<std::int64_t>(played);
        const bool pinned = (market.daysAtFloor + market.daysAtCap) * 4 > played;
        const bool sane = mean * 10 >= base * 3 && mean <= base * 3;
        pricesOk = pricesOk && !pinned && sane;
        detail += std::format("{}: mean {:.2f}x base, {} days at floor, {} at cap; ",
                              content.resources[r].id,
                              static_cast<double>(mean) / static_cast<double>(base),
                              market.daysAtFloor, market.daysAtCap);
    }
    checks.push_back({"prices_sane", pricesOk, detail});

    checks.push_back({"ai_commands_valid", stats.rejectedAgainstAi == 0,
                      std::format("{} rejected commands, not counting those refused because the "
                                  "game had ended",
                                  stats.rejectedAgainstAi)});
    checks.push_back({"economy_alive", played < sim::kDaysPerYear * 2 || stats.minesBuilt > 0,
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
    // How the game ended, or that the run stopped first. end_day is the last
    // day played, which is the day the game ended when it did.
    const std::int64_t never = neverDay(options);
    const bool founded = outcomeOf(final) == RunOutcome::Founded;
    std::cout << std::format(
        "  \"outcome\": {}, \"founder\": {}, \"founded_day\": {}, \"end_day\": {}, "
        "\"never_day\": {},\n",
        jsonString(outcomeName(outcomeOf(final))), founderIndex(final),
        founded ? std::int64_t{final.day} : never, final.day, never);
    std::cout << "  \"companies\": [\n";
    for (std::uint32_t c = 0; c < final.companies.size(); ++c) {
        const sim::Company& company = final.companies[c];
        const sim::CompanyView view = simulation.view(sim::CompanyId(c));
        std::cout << std::format(
            "    {{\"name\": {}, \"cash\": {}, \"debt\": {}, \"bankrupt\": {}, \"bankrupt_day\": "
            "{}, "
            "\"plots\": {}, \"mines\": {}, \"techs\": {}, \"first_mine_day\": {}, "
            "\"peak_cash\": {}, \"peak_debt\": {}, \"program_stage\": {}, \"stage_days\": {}}}{}\n",
            jsonString(company.name), sim::toString(company.cash),
            sim::toString(view.outstandingPrincipal), company.bankrupt != 0 ? "true" : "false",
            stats.bankruptDay[c], view.companies[c].plotsOwned, view.mines.size(),
            company.completedTechs.size(), stats.firstMineDay[c],
            sim::toString(sim::Money::fromCents(stats.peakCashCents[c])),
            sim::toString(sim::Money::fromCents(stats.peakDebtCents[c])), company.programStage,
            stageDaysJson(stats.stageDays[c], never), c + 1 < final.companies.size() ? "," : "");
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
            static_cast<double>(market.sumCents) /
                static_cast<double>(std::max<sim::Day>(final.day, 1)) / base,
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
// Goal thresholds from docs/specs/005-world-and-opening.md. All three gate
// the exit code; the first-mine day was reported only until spec 006 found
// why the AI-played company bought late (its bids always lost to the
// rival's) and fixed it.
constexpr std::uint32_t kGoalMineableOpening = 2;
constexpr std::uint32_t kGoalLandYear = 8;
constexpr sim::Day kGoalFirstMineDay = 250;
// Goal thresholds from docs/specs/006-balance.md. The spec's targets are for
// 50 seeds over ten years; the gates are what a ten-seed, eight-year CTest
// run can hold without noise (the ratio gate is looser than its target).
constexpr std::int64_t kGoalBankruptSeedsPer50 = 1;
constexpr std::int64_t kGoalMinesRatioPercent = 250;  // leader over trailer, median seed
constexpr std::int64_t kGateMinesRatioPercent = 300;
constexpr sim::Day kGoalMineLifeMinDays = 5 * sim::kDaysPerYear;
constexpr sim::Day kGoalMineLifeMaxDays = 7 * sim::kDaysPerYear;
constexpr std::int64_t kGoalWithdrawnYear8 = 25;
// Mines opened this long before the end of the run form the cohort whose
// lives are measured: long enough that a mine inside the goal has finished.
constexpr sim::Day kMineLifeCohortDays = kGoalMineLifeMaxDays;
// The spec 006 goals were set for an open-ended game and are reported, not
// gated, now that the game ends (spec 010): a seed is measured only as far as
// its game went. Bankruptcies are gated by the race check of that name.

// The race gates of spec 010 (BR-013), judged by a sweep that covers the
// deadline, so that every seed has an outcome. They are set for 50 seeds: at
// least 80 percent end by a founding, no company founds in more than 70
// percent, and at most one seed has a bankruptcy. A smaller sweep is held to
// the same shares, rounded up to whole seeds.
constexpr std::int64_t kGoalFoundedSeedsPer50 = 40;
constexpr std::int64_t kGoalMostWinsPer50 = 35;

// Thresholds from docs/specs/008-markets-that-bite.md (BR-010) and decision
// record 003. They are set for 50 seeds over 20 years with one rival, and are
// evaluated only by a sweep that may run those 20 years: a shorter one reports
// them as not evaluated. Since spec 010 a game ends when the program is
// founded, so each seed is measured up to the end of its own game, and these
// checks are reported, not gated.
constexpr std::uint32_t kBiteFirstYear = 3;  // price below break-even is counted from here
constexpr std::uint32_t kBiteLastYear = 20;  // to here, and the gates need a run this long
constexpr sim::Day kBiteFirstDay = (kBiteFirstYear - 1) * sim::kDaysPerYear;  // last day before
constexpr sim::Day kBiteLastDay = kBiteLastYear * sim::kDaysPerYear;
// The ores whose price must spend this share of those days below base-output
// break-even in the median seed.
constexpr std::array<std::string_view, 3> kBiteOres = {"iron", "copper", "nickel"};
constexpr std::int64_t kBiteMinPermille = 100;
constexpr std::int64_t kBiteMaxPermille = 500;
// A seed has an idled mine when one stood mothballed this many days in a row
// and was then reopened within the run. A mine left mothballed, because its
// extractable ore ran out or for any other reason, shows no price decision
// and does not count.
constexpr std::uint32_t kIdledRunDays = 30;
constexpr std::int64_t kGoalIdledSeedsPer50 = 40;
constexpr std::uint32_t kCashYear = 10;
constexpr sim::Day kCashDay = kCashYear * sim::kDaysPerYear;
// Company 0's median cash at the end of year 10, in whole currency units, in
// the economy before spec 008 retuned it: the cash gate asks for at most half
// of this. Measured once, on 2026-10-10 at commit e02f48d, with a release
// build: sim_runner --sweep 50 --years 20 --ai 1 (seeds 1 to 50), reading
// cash_y10_0.median. Measure it again if data from before spec 008 changes.
constexpr std::int64_t kBaselineCashY10 = 765'667'709;
constexpr std::int64_t kCashY10Divisor = 2;
// The aim for an ore's mean price over base, reported and not gated. Lithium
// aims higher: few companies reach it.
constexpr std::int64_t kAimPriceMinPermille = 700;
constexpr std::int64_t kAimPriceMaxPermille = 1300;
constexpr std::string_view kLithiumId = "lithium";
constexpr std::int64_t kAimLithiumMinPermille = 1000;
constexpr std::int64_t kAimLithiumMaxPermille = 1400;

// Mine counts are sampled at the sweep years and at the end of the run.
constexpr std::size_t kMineSamples = kSweepYears.size() + 1;
constexpr std::size_t kMineStates = 5;  // sim::MineState, in declaration order

struct SweepRow {
    sim::Seed seed = 0;
    std::uint32_t openingMineable = 0;   // opening listings in regions mineable on day one
    std::uint32_t openingLocked = 0;     // opening listings in regions whose main ore is locked
    std::uint32_t longestRegionRun = 0;  // among the monthly listings, after the opening hand
    // 0 = unlisted land never ran out, or the opening hand was the whole world
    sim::Day lastNewListingDay = 0;
    std::array<std::uint32_t, kSweepYears.size()> unlisted{};
    std::array<std::uint32_t, kSweepYears.size()> withdrawn{};
    sim::Day firstMineStartedDay = 0;    // company 0; 1-based, 0 = never
    sim::Day firstMineOperatingDay = 0;  // company 0; 1-based, 0 = never
    std::vector<std::int64_t> finalCashCents;
    bool worldValid = true;
    // Spec 006, per company: mines ever built at each sample, mines by state
    // at the end, and the day of bankruptcy (1-based, 0 = never).
    std::vector<std::array<std::uint32_t, kMineSamples>> minesBuilt;
    std::vector<std::array<std::uint32_t, kMineStates>> minesByState;
    std::vector<sim::Day> bankruptDay;
    sim::Day firstDepletionDay = 0;        // any company; 1-based, 0 = never
    std::uint32_t depletedByGoalYear = 0;  // mines depleted by the end of year 8
    // Days from opening to depletion for every mine in the cohort (opened at
    // least kMineLifeCohortDays before the seed's last day); a mine still
    // running counts as never.
    std::vector<std::int64_t> mineLives;
    std::vector<std::int64_t> priceMeanPermille;  // per resource: mean price over base
    // Spec 008. Per resource: the share of days in years 3 to 20 on which the
    // price was below base-output break-even, and the lowest and highest
    // yearly mean price over base. Per company: mine-days spent mothballed
    // and cash at the end of year 10, in cents.
    std::vector<std::int64_t> belowBreakEvenPermille;
    std::vector<std::int64_t> priceYearMinPermille;
    std::vector<std::int64_t> priceYearMaxPermille;
    std::vector<std::int64_t> mineDaysIdled;
    std::vector<std::int64_t> cashY10Cents;
    std::uint32_t longestIdleRun = 0;  // most days in a row any one mine stood mothballed
    // Times a mine went back to operating after kIdledRunDays or more mothballed.
    std::uint32_t idleReopenSpells = 0;
    // Spec 010. The days played, which stop when the game ends: the last day
    // played, and the day the game ended when it did. Then how the game
    // stood, who founded the program (-1 = nobody), the day each company
    // completed each stage (1-based, 0 = never) and the orders refused, not
    // counting those refused because the game had ended.
    sim::Day endDay = 0;
    RunOutcome outcome = RunOutcome::Running;
    std::int64_t founder = -1;
    std::vector<std::vector<sim::Day>> stageDays;
    std::uint32_t rejectedCommands = 0;
    // Working state while the seed runs.
    std::vector<sim::Day> depletedDay;             // per mine, 1-based, 0 = not yet
    std::vector<std::int64_t> priceSumCents;       // per resource, saturating
    std::vector<std::int64_t> daysBelowBreakEven;  // per resource, within years 3 to 20
    std::vector<std::int64_t> yearPriceSumCents;   // per resource, this year so far; saturating
    std::vector<std::uint32_t> idleRun;            // per mine, days mothballed in a row
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

// The contest (spec 006), sampled once a day after the tick.
void recordContest(const sim::WorldState& state, const sim::TickReport& report, SweepRow& row) {
    for (const sim::Notice& notice : report.notices) {
        if (notice.kind == sim::NoticeKind::Bankrupt &&
            row.bankruptDay[notice.company.index()] == 0) {
            row.bankruptDay[notice.company.index()] = report.day + 1;
        }
    }
    row.depletedDay.resize(state.mines.size(), 0);
    for (std::size_t m = 0; m < state.mines.size(); ++m) {
        if (state.mines[m].state != sim::MineState::Depleted || row.depletedDay[m] != 0) {
            continue;
        }
        row.depletedDay[m] = report.day + 1;
        if (row.firstDepletionDay == 0) {
            row.firstDepletionDay = report.day + 1;
        }
        row.depletedByGoalYear += state.day <= kGoalLandYear * sim::kDaysPerYear ? 1U : 0U;
    }
    for (std::size_t r = 0; r < state.markets.size(); ++r) {
        row.priceSumCents[r] = saturatingAdd(row.priceSumCents[r], state.markets[r].price.cents());
    }
    for (std::size_t y = 0; y < kSweepYears.size(); ++y) {
        if (state.day != kSweepYears.at(y) * sim::kDaysPerYear) {
            continue;
        }
        for (const sim::Mine& mine : state.mines) {
            ++row.minesBuilt[mine.owner.index()].at(y);
        }
    }
}

// A price sum over `days` days as permille of the base price, truncated once
// so a cheap ore's mean is not rounded to whole cents first. The sum
// saturates only at prices no content bound allows over a permitted run.
std::int64_t meanPermille(std::int64_t sumCents, sim::Money base, sim::Day days) {
    const std::int64_t span = sim::checkedMulDiv(base.cents(), std::int64_t{days}, 1).value_or(0);
    return span > 0 ? sim::checkedMulDiv(sumCents, 1000, span).value_or(0) : 0;
}

// End of the seed: final mine counts, the lives of the cohort, mean prices
// over the days played.
void finishContest(const Options& options, const sim::Content& content,
                   const sim::WorldState& state, SweepRow& row) {
    row.depletedDay.resize(state.mines.size(), 0);
    for (std::size_t m = 0; m < state.mines.size(); ++m) {
        const sim::Mine& mine = state.mines[m];
        ++row.minesBuilt[mine.owner.index()].at(kMineSamples - 1);
        ++row.minesByState[mine.owner.index()].at(static_cast<std::size_t>(mine.state));
        const bool opened = mine.state != sim::MineState::Building;
        if (!opened || row.endDay < kMineLifeCohortDays ||
            mine.operationalDay > row.endDay - kMineLifeCohortDays) {
            continue;
        }
        // A mine closed by its owner's bankruptcy before it ran out says
        // nothing about deposits; one that ran out first keeps its life.
        if (mine.state == sim::MineState::Closed && row.depletedDay[m] == 0) {
            continue;
        }
        row.mineLives.push_back(row.depletedDay[m] == 0
                                    ? neverDay(options)
                                    : std::int64_t{row.depletedDay[m]} - mine.operationalDay);
    }
    for (std::size_t r = 0; r < row.priceSumCents.size(); ++r) {
        row.priceMeanPermille.push_back(
            meanPermille(row.priceSumCents[r], content.resources[r].basePrice, row.endDay));
    }
}

// Whether one mine at tech level zero, selling its whole base output of this
// ore at `price`, takes less than the ore's running cost: price below
// mineOpexPerDay / baseOutputTonnesPerDay, compared without the division. No
// company's research or event multiplier applies; this is the data's own
// break-even, the same for every seed.
bool belowBaseBreakEven(const sim::ResourceDef& resource, sim::Money price) {
    const std::optional<std::int64_t> revenue =
        sim::checkedMulDiv(price.cents(), resource.baseOutputTonnesPerDay, 1);
    return revenue.has_value() && *revenue < resource.mineOpexPerDay.cents();
}

// Prices against break-even and by year (spec 008), once a day after the tick.
void recordBitePrices(const sim::Content& content, const sim::WorldState& state, SweepRow& row) {
    const bool counted = state.day > kBiteFirstDay && state.day <= kBiteLastDay;
    const bool yearEnd = state.day % sim::kDaysPerYear == 0;
    for (std::size_t r = 0; r < state.markets.size(); ++r) {
        const sim::ResourceDef& resource = content.resources[r];
        const sim::Money price = state.markets[r].price;
        row.daysBelowBreakEven[r] += counted && belowBaseBreakEven(resource, price) ? 1 : 0;
        row.yearPriceSumCents[r] = saturatingAdd(row.yearPriceSumCents[r], price.cents());
        if (!yearEnd) {
            continue;
        }
        const std::int64_t mean =
            meanPermille(row.yearPriceSumCents[r], resource.basePrice, sim::kDaysPerYear);
        row.priceYearMinPermille[r] = std::min(row.priceYearMinPermille[r], mean);
        row.priceYearMaxPermille[r] = std::max(row.priceYearMaxPermille[r], mean);
        row.yearPriceSumCents[r] = 0;
    }
}

// Idled mines and cash at year 10 (spec 008), once a day after the tick.
// Mine-days idled and the longest idle run count every mothballed day,
// whoever chose it and why. An idle-and-reopen spell is a long enough run
// that ends with the mine operating again: one that ends depleted or closed,
// or is still running when the sweep stops, is not one.
void recordBiteCompanies(const sim::WorldState& state, SweepRow& row) {
    row.idleRun.resize(state.mines.size(), 0);
    for (std::size_t m = 0; m < state.mines.size(); ++m) {
        if (state.mines[m].state != sim::MineState::Mothballed) {
            const bool reopened = state.mines[m].state == sim::MineState::Operating;
            if (reopened && row.idleRun[m] >= kIdledRunDays) {
                ++row.idleReopenSpells;
            }
            row.idleRun[m] = 0;
            continue;
        }
        ++row.mineDaysIdled[state.mines[m].owner.index()];
        row.longestIdleRun = std::max(row.longestIdleRun, ++row.idleRun[m]);
    }
    if (state.day == kCashDay) {
        for (std::size_t c = 0; c < state.companies.size(); ++c) {
            row.cashY10Cents[c] = state.companies[c].cash.cents();
        }
    }
}

// The days of years 3 to 20 that a run of this length covers.
sim::Day biteDaysCovered(sim::Day days) {
    return std::min(days, kBiteLastDay) - std::min(days, kBiteFirstDay);
}

// End of the seed: days below break-even as a share of the days counted,
// which stop with the seed's game.
void finishBite(SweepRow& row) {
    const std::int64_t covered = biteDaysCovered(row.endDay);
    for (const std::int64_t below : row.daysBelowBreakEven) {
        row.belowBreakEvenPermille.push_back(covered > 0 ? below * 1000 / covered : 0);
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
    row.worldValid = sim::validateWorld(content, state);

    const std::size_t companyCount = state.companies.size();
    row.minesBuilt.assign(companyCount, {});
    row.minesByState.assign(companyCount, {});
    row.bankruptDay.assign(companyCount, 0);
    row.priceSumCents.assign(content.resources.size(), 0);
    row.daysBelowBreakEven.assign(content.resources.size(), 0);
    row.yearPriceSumCents.assign(content.resources.size(), 0);
    row.priceYearMinPermille.assign(content.resources.size(),
                                    std::numeric_limits<std::int64_t>::max());
    row.priceYearMaxPermille.assign(content.resources.size(), 0);
    row.mineDaysIdled.assign(companyCount, 0);
    row.cashY10Cents.assign(companyCount, 0);
    row.stageDays = noStageDays(content, companyCount);
    while (keepPlaying(options, state)) {
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
        recordContest(state, report, row);
        recordBitePrices(content, state, row);
        recordBiteCompanies(state, row);
        recordStageDays(report, row.stageDays);
        row.rejectedCommands +=
            static_cast<std::uint32_t>(std::ranges::count_if(report.notices, countsAgainstAi));
    }
    row.endDay = state.day;
    row.outcome = outcomeOf(state);
    row.founder = founderIndex(state);
    finishContest(options, content, state, row);
    finishBite(row);
    row.worldValid = row.worldValid && sim::validateWorld(content, state);
    for (const sim::Company& company : state.companies) {
        row.finalCashCents.push_back(company.cash.cents());
    }
    return row;
}

// Whether a seed that played this many days reached the y-th sample year. A
// year it stopped short of, because the run was that short or its game had
// ended, is printed as null (blank in the CSV), never as zero and never as
// the value of the finished world.
bool yearSampled(sim::Day days, std::size_t y) {
    return days >= kSweepYears.at(y) * sim::kDaysPerYear;
}

std::string sampleList(const std::array<std::uint32_t, kSweepYears.size()>& counts, sim::Day days) {
    std::string out;
    for (std::size_t y = 0; y < counts.size(); ++y) {
        out += y == 0 ? "" : ", ";
        out += yearSampled(days, y) ? std::to_string(counts.at(y)) : "null";
    }
    return out;
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

// A figure sampled at a point only some seeds reach: the spread over those
// that did, and how many they were.
struct Sample {
    Spread spread;
    std::int64_t seeds = 0;
};

template <class Reached, class Get>
Sample sampleOver(const std::vector<SweepRow>& rows, const Reached& reached, const Get& get) {
    std::vector<std::int64_t> values;
    for (const SweepRow& row : rows) {
        if (reached(row)) {
            values.push_back(static_cast<std::int64_t>(get(row)));
        }
    }
    const auto seeds = static_cast<std::int64_t>(values.size());
    return {spreadOf(std::move(values)), seeds};
}

// The seeds whose game was still running at the end of the goal year.
bool reachedGoalYear(const SweepRow& row) {
    return row.endDay >= kGoalLandYear * sim::kDaysPerYear;
}
bool reachedCashYear(const SweepRow& row) {
    return row.endDay >= kCashDay;
}

std::string spreadJson(const Spread& spread) {
    return std::format(R"({{"min": {}, "median": {}, "max": {}}})", spread.min, spread.median,
                       spread.max);
}

std::uint32_t minesAtEnd(const SweepRow& row, std::size_t company) {
    return row.minesBuilt[company].at(kMineSamples - 1);
}

// The ratio of a seed in which some company never built a mine: there is no
// contest to measure, and it must fail any limit. Larger than any real ratio.
constexpr std::int64_t kNoContestPercent = 1'000'000'000;

// Mines ever built by the company with most over the company with fewest, as
// a percentage. A bankrupt company keeps the mines it built.
std::int64_t minesRatioPercent(const SweepRow& row) {
    std::uint32_t leader = 0;
    std::uint32_t trailer = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t c = 0; c < row.minesBuilt.size(); ++c) {
        leader = std::max(leader, minesAtEnd(row, c));
        trailer = std::min(trailer, minesAtEnd(row, c));
    }
    if (row.minesBuilt.empty() || trailer == 0) {
        return kNoContestPercent;
    }
    return std::int64_t{leader} * 100 / std::int64_t{trailer};
}

bool anyBankrupt(const SweepRow& row) {
    return std::ranges::any_of(row.bankruptDay, [](sim::Day day) { return day != 0; });
}

// Lives of every cohort mine in every seed, pooled.
std::vector<std::int64_t> pooledMineLives(const std::vector<SweepRow>& rows) {
    std::vector<std::int64_t> lives;
    for (const SweepRow& row : rows) {
        lives.insert(lives.end(), row.mineLives.begin(), row.mineLives.end());
    }
    return lives;
}

// The spec 006 part of a row: per company, mines built at each sample (null
// for a year the run stops short of) and mines by state at the end, in the
// order building, operating, mothballed, depleted, closed.
std::string contestJson(const SweepRow& row) {
    std::string built;
    std::string states;
    for (std::size_t c = 0; c < row.minesBuilt.size(); ++c) {
        std::string samples;
        for (std::size_t y = 0; y < kMineSamples; ++y) {
            const bool sampled = y + 1 == kMineSamples || yearSampled(row.endDay, y);
            samples += (y == 0 ? "" : ", ") +
                       (sampled ? std::to_string(row.minesBuilt[c].at(y)) : std::string("null"));
        }
        built += (c == 0 ? "[" : ", [") + samples + "]";
        states += (c == 0 ? "[" : ", [") + joined(row.minesByState[c]) + "]";
    }
    std::string prices;
    for (const std::int64_t permille : row.priceMeanPermille) {
        prices += std::format("{}{:.3f}", prices.empty() ? "" : ", ",
                              static_cast<double>(permille) / 1000.0);
    }
    // A seed that stops short of year 8 has not measured depletion by then.
    const std::string depleted =
        reachedGoalYear(row) ? std::to_string(row.depletedByGoalYear) : std::string("null");
    return std::format(
        "\"mines_built\": [{}], \"mines_by_state\": [{}], \"bankrupt_day\": [{}], "
        "\"first_depletion_day\": {}, \"depleted_by_y{}\": {}, "
        "\"cohort_mines\": {}, \"price_mean\": [{}]",
        built, states, joined(row.bankruptDay), row.firstDepletionDay, kGoalLandYear, depleted,
        row.mineLives.size(), prices);
}

// What a seed that played this many days has measured of spec 008. A figure
// it has not is printed as null, never as zero.
bool biteMeasured(sim::Day days) {
    return biteDaysCovered(days) > 0;
}
bool priceYearsMeasured(sim::Day days) {
    return days >= sim::kDaysPerYear;
}
bool cashY10Measured(sim::Day days) {
    return days >= kCashDay;
}
bool laterYearsMeasured(sim::Day days) {
    return days > kCashDay;
}
// The checks of spec 008 are set for a 20-year sweep and judge nothing shorter.
bool biteGatesApply(sim::Day days) {
    return days >= kBiteLastDay;
}

template <class Range>
std::string listOrNull(bool measured, const Range& values) {
    return measured ? "[" + joined(values) + "]" : std::string("null");
}

// The spec 008 part of a row. Cash at year 10 is in whole currency units.
std::string biteJson(const SweepRow& row) {
    std::vector<std::int64_t> cash;
    cash.reserve(row.cashY10Cents.size());
    for (const std::int64_t cents : row.cashY10Cents) {
        cash.push_back(cents / 100);
    }
    const bool years = priceYearsMeasured(row.endDay);
    return std::format(
        "\"below_breakeven_permille\": {}, \"price_year_min_permille\": {}, "
        "\"price_year_max_permille\": {}, \"mine_days_idled\": [{}], \"cash_y{}\": {}, "
        "\"longest_idle_run_days\": {}, \"idle_reopen_spells\": {}",
        listOrNull(biteMeasured(row.endDay), row.belowBreakEvenPermille),
        listOrNull(years, row.priceYearMinPermille), listOrNull(years, row.priceYearMaxPermille),
        joined(row.mineDaysIdled), kCashYear, listOrNull(cashY10Measured(row.endDay), cash),
        row.longestIdleRun, row.idleReopenSpells);
}

// The day a seed's program was founded, or never.
std::int64_t foundedDay(const SweepRow& row, std::int64_t never) {
    return row.outcome == RunOutcome::Founded ? std::int64_t{row.endDay} : never;
}
bool isFounded(const SweepRow& row) {
    return row.outcome == RunOutcome::Founded;
}

// How many seeds each company founded the program in.
std::vector<std::int64_t> winsByCompany(const std::vector<SweepRow>& rows) {
    std::vector<std::int64_t> wins(rows.empty() ? 0 : rows.front().stageDays.size(), 0);
    for (const SweepRow& row : rows) {
        if (isFounded(row)) {
            ++wins[static_cast<std::size_t>(row.founder)];
        }
    }
    return wins;
}

// The spec 010 part of a row. end_day is the last day played, which is the
// day the game ended when it did.
std::string raceJson(const Options& options, const SweepRow& row) {
    const std::int64_t never = neverDay(options);
    std::string stages;
    for (const std::vector<sim::Day>& days : row.stageDays) {
        stages += (stages.empty() ? "" : ", ") + stageDaysJson(days, never);
    }
    return std::format(
        "\"outcome\": {}, \"founder\": {}, \"founded_day\": {}, \"end_day\": {}, "
        "\"stage_days\": [{}], \"rejected_commands\": {}",
        jsonString(outcomeName(row.outcome)), row.founder, foundedDay(row, never), row.endDay,
        stages, row.rejectedCommands);
}

// Whether any company went bankrupt on a day after `after` and up to `upTo`
// (days are 1-based).
bool bankruptWithin(const SweepRow& row, sim::Day after, sim::Day upTo) {
    return std::ranges::any_of(row.bankruptDay,
                               [=](sim::Day day) { return day > after && day <= upTo; });
}
bool bankruptEarly(const SweepRow& row) {
    return bankruptWithin(row, 0, kCashDay);
}
bool bankruptLater(const SweepRow& row) {
    return bankruptWithin(row, kCashDay, kBiteLastDay);
}
bool hasIdledMine(const SweepRow& row) {
    return row.idleReopenSpells > 0;
}

// The spec 006 columns of one CSV row.
void writeContestCsv(std::ostream& csv, const SweepRow& row) {
    for (std::size_t c = 0; c < row.minesBuilt.size(); ++c) {
        csv << ',' << minesAtEnd(row, c) << ','
            << row.minesByState[c].at(static_cast<std::size_t>(sim::MineState::Operating)) << ','
            << row.bankruptDay[c];
    }
    csv << ',' << row.firstDepletionDay << ',';
    if (reachedGoalYear(row)) {
        csv << row.depletedByGoalYear;
    }
    for (const std::int64_t permille : row.priceMeanPermille) {
        csv << ',' << permille;
    }
}

// The spec 010 columns of one CSV row, which end it.
void writeRaceCsv(std::ostream& csv, const SweepRow& row, std::int64_t never) {
    csv << ',' << outcomeName(row.outcome) << ',' << row.founder << ',' << foundedDay(row, never)
        << ',' << row.endDay;
    for (const std::vector<sim::Day>& days : row.stageDays) {
        for (const sim::Day day : days) {
            csv << ',' << dayOrNever(day, never);
        }
    }
    csv << ',' << row.rejectedCommands;
}

void writeSweepCsv(std::ostream& csv, const std::vector<SweepRow>& rows, std::int64_t never) {
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
    for (std::size_t c = 0; c < companies; ++c) {
        csv << ",mines_built_" << c << ",mines_operating_" << c << ",bankrupt_day_" << c;
    }
    csv << ",first_depletion_day,depleted_by_y" << kGoalLandYear;
    const std::size_t resources = rows.empty() ? 0 : rows.front().priceMeanPermille.size();
    for (std::size_t r = 0; r < resources; ++r) {
        csv << ",price_mean_permille_" << r;
    }
    csv << ",outcome,founder,founded_day,end_day";
    for (std::size_t c = 0; c < companies; ++c) {
        for (std::size_t stage = 1; stage <= rows.front().stageDays[c].size(); ++stage) {
            csv << ",stage_" << stage << "_day_" << c;
        }
    }
    csv << ",rejected_commands\n";
    for (const SweepRow& row : rows) {
        csv << row.seed << ',' << row.openingMineable << ',' << row.openingLocked << ','
            << row.longestRegionRun << ',' << row.lastNewListingDay;
        for (const auto* counts : {&row.unlisted, &row.withdrawn}) {
            for (std::size_t y = 0; y < counts->size(); ++y) {
                csv << ',';
                if (yearSampled(row.endDay, y)) {
                    csv << counts->at(y);
                }
            }
        }
        csv << ',' << row.firstMineStartedDay << ',' << row.firstMineOperatingDay << ','
            << (row.worldValid ? 1 : 0);
        for (const std::int64_t cents : row.finalCashCents) {
            csv << ',' << sim::toString(sim::Money::fromCents(cents));
        }
        writeContestCsv(csv, row);
        writeRaceCsv(csv, row, never);
        csv << '\n';
    }
}

// The detail of a figure that no seed's game lasted long enough to measure.
std::string noSeedReached(std::string_view what) {
    return std::format("not evaluated: no seed's game reached {}", what);
}

// The checks that hold in a game that ends early, which gate the exit code,
// and the land left at year 8 (spec 005), which is reported. A threshold the
// run is too short to measure is reported as not evaluated and does not fail
// the sweep.
std::vector<Check> sweepChecks(const Options& options, const std::vector<SweepRow>& rows) {
    std::vector<Check> checks;
    const std::int64_t never = neverDay(options);
    const Spread mineable = spreadOver(rows, [](const SweepRow& r) { return r.openingMineable; });
    checks.push_back({"opening_has_mineable_plots", mineable.min >= kGoalMineableOpening,
                      std::format("fewest mineable-region opening plots in any seed: {} (goal: "
                                  "at least {})",
                                  mineable.min, kGoalMineableOpening)});

    const Sample land = sampleOver(rows, reachedGoalYear, [](const SweepRow& r) {
        return r.unlisted.at(kSweepYears.size() - 1);
    });
    checks.push_back(
        {"unlisted_land_at_year_8", land.seeds == 0 || land.spread.min > 0,
         land.seeds > 0
             ? std::format("fewest unlisted plots at year {} in any of the {} of {} seeds "
                           "still playing then: {} (goal: more than 0)",
                           kGoalLandYear, land.seeds, rows.size(), land.spread.min)
             : noSeedReached(std::format("year {}", kGoalLandYear)),
         false});

    const bool mineMeasured = options.days >= kGoalFirstMineDay;
    const Spread firstMine = spreadOver(
        rows, [&](const SweepRow& r) { return dayOrNever(r.firstMineOperatingDay, never); });
    const auto neverOpened = std::count_if(
        rows.begin(), rows.end(), [](const SweepRow& r) { return r.firstMineOperatingDay == 0; });
    checks.push_back(
        {"median_first_mine_day", !mineMeasured || firstMine.median < kGoalFirstMineDay,
         mineMeasured
             ? std::format("median day the AI-played company 0 has an operating mine: {} "
                           "(goal: under {}); {} of {} seeds never opened one",
                           firstMine.median >= never ? std::string("never")
                                                     : std::to_string(firstMine.median),
                           kGoalFirstMineDay, neverOpened, rows.size())
             : std::format("not evaluated: the run is shorter than {} days", kGoalFirstMineDay)});

    const bool valid = std::ranges::all_of(rows, [](const SweepRow& r) { return r.worldValid; });
    checks.push_back(
        {"world_valid", valid, "validation at the start, every year end and the end, every seed"});

    std::int64_t rejected = 0;
    for (const SweepRow& row : rows) {
        rejected += row.rejectedCommands;
    }
    const auto rejecting = std::count_if(rows.begin(), rows.end(),
                                         [](const SweepRow& r) { return r.rejectedCommands > 0; });
    checks.push_back({"ai_commands_valid", rejected == 0,
                      std::format("{} rejected commands in {} of {} seeds, not counting those "
                                  "refused because the game had ended",
                                  rejected, rejecting, rows.size())});
    return checks;
}

// The race (spec 010, BR-013). A sweep that stops before the deadline leaves
// games unfinished and evaluates none of these.
void raceChecks(const Options& options, const sim::Content& content,
                const std::vector<SweepRow>& rows, std::vector<Check>& checks) {
    const bool applies = options.days >= content.program.deadlineDay;
    const std::string skipped = "not evaluated: the sweep stops before the deadline, day " +
                                std::to_string(content.program.deadlineDay);
    const auto add = [&](std::string name, bool passed, const std::string& detail) {
        checks.push_back({std::move(name), !applies || passed, applies ? detail : skipped});
    };
    const auto seeds = static_cast<std::int64_t>(rows.size());

    const auto founded = std::count_if(rows.begin(), rows.end(), isFounded);
    const std::int64_t needed = (seeds * kGoalFoundedSeedsPer50 + 49) / 50;
    add("founded_before_deadline", founded >= needed,
        std::format("{} of {} seeds ended by a founding (goal: at least {})", founded, seeds,
                    needed));

    const std::vector<std::int64_t> wins = winsByCompany(rows);
    const std::int64_t most = wins.empty() ? 0 : *std::ranges::max_element(wins);
    const std::int64_t allowed = (seeds * kGoalMostWinsPer50 + 49) / 50;
    add("win_balance", most <= allowed,
        std::format("wins by company: [{}] of {} seeds (goal: at most {} each)", joined(wins),
                    seeds, allowed));

    // One seed in fifty, rounded up, so a ten-seed run still allows one.
    const auto bankrupt = std::count_if(rows.begin(), rows.end(), anyBankrupt);
    const std::int64_t allowedBankrupt = (seeds * kGoalBankruptSeedsPer50 + 49) / 50;
    add("bankruptcies", bankrupt <= allowedBankrupt,
        std::format("{} of {} seeds had a bankrupt company (goal: at most {})", bankrupt, seeds,
                    allowedBankrupt));
}

// The goals of docs/specs/006-balance.md, reported and not gated. Each needs
// seeds whose games ran long enough to measure it; with none it is reported
// as not evaluated and passes.
void contestChecks(const Options& options, const sim::Content& content,
                   const std::vector<SweepRow>& rows, std::vector<Check>& checks) {
    const auto seeds = static_cast<std::int64_t>(rows.size());
    const auto bankrupt = std::count_if(rows.begin(), rows.end(), anyBankrupt);
    // One seed in fifty, rounded up, so a ten-seed run still allows one.
    const std::int64_t allowed = (seeds * kGoalBankruptSeedsPer50 + 49) / 50;
    checks.push_back({"seeds_with_bankruptcy", bankrupt <= allowed,
                      std::format("{} of {} seeds had a bankrupt company (goal: at most {})",
                                  bankrupt, seeds, allowed),
                      false});

    // A contest needs two companies; with one there is nothing to compare.
    const bool contested = options.aiCompanies >= 1;
    const Spread ratio = spreadOver(rows, minesRatioPercent);
    std::string ratioDetail = "not evaluated: the sweep has one company (use --ai 1 or more)";
    if (contested && ratio.median >= kNoContestPercent) {
        ratioDetail =
            "in the median seed a company never built a mine (goal: leader over "
            "trailer at most 2.5x)";
    } else if (contested) {
        ratioDetail = std::format(
            "mines built, leader over trailer, median seed: {:.2f}x (goal: "
            "at most {:.1f}x; gate: {:.1f}x)",
            static_cast<double>(ratio.median) / 100.0,
            static_cast<double>(kGoalMinesRatioPercent) / 100.0,
            static_cast<double>(kGateMinesRatioPercent) / 100.0);
    }
    checks.push_back({"mines_leader_over_trailer",
                      !contested || ratio.median <= kGateMinesRatioPercent, ratioDetail, false});

    bool pricesOk = true;
    std::string prices;
    for (std::size_t r = 0; r < content.resources.size(); ++r) {
        const Spread mean =
            spreadOver(rows, [r](const SweepRow& row) { return row.priceMeanPermille[r]; });
        const bool lithium = content.resources[r].id == kLithiumId;
        pricesOk = pricesOk &&
                   mean.median >= (lithium ? kAimLithiumMinPermille : kAimPriceMinPermille) &&
                   mean.median <= (lithium ? kAimLithiumMaxPermille : kAimPriceMaxPermille);
        prices += std::format("{}{} {:.2f}x", r == 0 ? "" : ", ", content.resources[r].id,
                              static_cast<double>(mean.median) / 1000.0);
    }
    checks.push_back({"mean_prices", pricesOk,
                      std::format("mean price over base on the days each seed played, median "
                                  "seed: {} (aim: {:.2f}x to {:.2f}x; {} {:.2f}x to {:.2f}x)",
                                  prices, static_cast<double>(kAimPriceMinPermille) / 1000.0,
                                  static_cast<double>(kAimPriceMaxPermille) / 1000.0, kLithiumId,
                                  static_cast<double>(kAimLithiumMinPermille) / 1000.0,
                                  static_cast<double>(kAimLithiumMaxPermille) / 1000.0),
                      false});

    // The cohort is the mines opened long enough before their own seed's last
    // day; a seed whose game was shorter than that has none.
    const bool lifeMeasured =
        std::ranges::any_of(rows, [](const SweepRow& r) { return r.endDay > kMineLifeCohortDays; });
    const std::vector<std::int64_t> lives = pooledMineLives(rows);
    const Spread life = spreadOf(lives);
    const bool lifeOk = !lives.empty() && life.median >= kGoalMineLifeMinDays &&
                        life.median <= kGoalMineLifeMaxDays;
    checks.push_back(
        {"median_mine_life", !lifeMeasured || lifeOk,
         lifeMeasured
             ? std::format("median days from opening to depletion, {} mines opened {} days or more "
                           "before their seed's last day: {} (goal: {} to {})",
                           lives.size(), kMineLifeCohortDays,
                           life.median >= neverDay(options) ? std::string("never")
                                                            : std::to_string(life.median),
                           kGoalMineLifeMinDays, kGoalMineLifeMaxDays)
             : std::format("not evaluated: no seed played more than {} days", kMineLifeCohortDays),
         false});

    const std::string goalYear = "year " + std::to_string(kGoalLandYear);
    const Sample depleted =
        sampleOver(rows, reachedGoalYear, [](const SweepRow& r) { return r.depletedByGoalYear; });
    const auto undepleted = std::count_if(rows.begin(), rows.end(), [](const SweepRow& r) {
        return reachedGoalYear(r) && r.depletedByGoalYear == 0;
    });
    checks.push_back(
        {"depletion_by_year_8", undepleted == 0,
         depleted.seeds > 0
             ? std::format("{} of the {} of {} seeds still playing at year {} had no depleted "
                           "mine by then (goal: none)",
                           undepleted, depleted.seeds, seeds, kGoalLandYear)
             : noSeedReached(goalYear),
         false});

    const Sample withdrawn = sampleOver(rows, reachedGoalYear, [](const SweepRow& r) {
        return r.withdrawn.at(kSweepYears.size() - 1);
    });
    checks.push_back(
        {"withdrawn_land_at_year_8",
         withdrawn.seeds == 0 || withdrawn.spread.median <= kGoalWithdrawnYear8,
         withdrawn.seeds > 0
             ? std::format("median withdrawn plots at year {}, the {} of {} seeds still "
                           "playing then: {} (goal: {} or fewer)",
                           kGoalLandYear, withdrawn.seeds, seeds, withdrawn.spread.median,
                           kGoalWithdrawnYear8)
             : noSeedReached(goalYear),
         false});
}

// The checks of spec 008 (BR-010, decision record 003) and the bankruptcies
// it reports. None is gated since spec 010: each seed is measured up to the
// end of its own game, which the 20-year thresholds did not foresee. A sweep
// shorter than 20 years evaluates none of them.
void biteChecks(const Options& options, const sim::Content& content,
                const std::vector<SweepRow>& rows, std::vector<Check>& checks) {
    const bool applies = biteGatesApply(options.days);
    const std::string skipped =
        "not evaluated: the sweep is shorter than " + std::to_string(kBiteLastYear) + " years";
    const auto add = [&](std::string name, bool passed, const std::string& detail) {
        checks.push_back({std::move(name), !applies || passed, applies ? detail : skipped, false});
    };
    const auto seeds = static_cast<std::int64_t>(rows.size());
    const auto biteReached = [](const SweepRow& row) { return biteMeasured(row.endDay); };

    for (const std::string_view ore : kBiteOres) {
        const auto found = std::ranges::find(content.resources, ore, &sim::ResourceDef::id);
        const auto r = static_cast<std::size_t>(found - content.resources.begin());
        if (found == content.resources.end()) {
            add(std::format("bite_{}", ore), false,
                std::format("{}: the content has no such ore", ore));
            continue;
        }
        const Sample below = sampleOver(
            rows, biteReached, [r](const SweepRow& row) { return row.belowBreakEvenPermille[r]; });
        add(std::format("bite_{}", ore),
            below.seeds == 0 || (below.spread.median >= kBiteMinPermille &&
                                 below.spread.median <= kBiteMaxPermille),
            below.seeds > 0
                ? std::format("{}: price below base-output break-even on {} permille of days "
                              "from year {} to year {} or the end of the seed's game, median "
                              "of {} seeds (goal: {} to {})",
                              ore, below.spread.median, kBiteFirstYear, kBiteLastYear, below.seeds,
                              kBiteMinPermille, kBiteMaxPermille)
                : std::format("{}: {}", ore,
                              noSeedReached(std::format("year {}", kBiteFirstYear))));
    }

    // Forty seeds in fifty, rounded up, so a smaller sweep is held to the
    // same share and never a looser one.
    const auto idled = std::count_if(rows.begin(), rows.end(), hasIdledMine);
    const std::int64_t needed = (seeds * kGoalIdledSeedsPer50 + 49) / 50;
    add("idled_mines", idled >= needed,
        std::format("{} of {} seeds had a mine mothballed for {} days in a row or more and then "
                    "reopened (goal: at least {})",
                    idled, seeds, kIdledRunDays, needed));

    const Sample cash = sampleOver(
        rows, reachedCashYear, [](const SweepRow& row) { return row.cashY10Cents.front() / 100; });
    add("cash_y10", cash.seeds == 0 || cash.spread.median <= kBaselineCashY10 / kCashY10Divisor,
        cash.seeds > 0
            ? std::format("company 0's cash at the end of year {}, median of the {} of {} seeds "
                          "still playing then: {} (goal: at most {}, half the {} measured "
                          "before spec 008)",
                          kCashYear, cash.seeds, seeds, cash.spread.median,
                          kBaselineCashY10 / kCashY10Divisor, kBaselineCashY10)
            : noSeedReached(std::format("year {}", kCashYear)));

    // One seed in fifty, rounded up, as for the whole-run count above.
    const std::int64_t allowed = (seeds * kGoalBankruptSeedsPer50 + 49) / 50;
    const auto early = std::count_if(rows.begin(), rows.end(), bankruptEarly);
    add("bankruptcy_y1_10", early <= allowed,
        std::format("{} of {} seeds had a bankruptcy in years 1 to {} (goal: at most {})", early,
                    seeds, kCashYear, allowed));
    const auto later = std::count_if(rows.begin(), rows.end(), bankruptLater);
    add("bankruptcy_y11_20", later <= allowed,
        std::format("{} of {} seeds had a bankruptcy in years {} to {} (reported against the "
                    "limit for years 1 to {}: at most {})",
                    later, seeds, kCashYear + 1, kBiteLastYear, kCashYear, allowed));
}

void printSweepRows(const Options& options, const std::vector<SweepRow>& rows) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const SweepRow& row = rows[i];
        std::string cash;
        for (std::size_t c = 0; c < row.finalCashCents.size(); ++c) {
            cash +=
                (c == 0 ? "" : ", ") + sim::toString(sim::Money::fromCents(row.finalCashCents[c]));
        }
        std::cout << std::format(
            "    {{\"seed\": {}, \"opening_mineable\": {}, \"opening_locked\": {}, "
            "\"longest_region_run\": {}, \"last_new_listing_day\": {}, \"unlisted\": [{}], "
            "\"withdrawn\": [{}], \"first_mine_started_day\": {}, "
            "\"first_mine_operating_day\": {}, \"final_cash\": [{}], {}}}{}\n",
            row.seed, row.openingMineable, row.openingLocked, row.longestRegionRun,
            row.lastNewListingDay, sampleList(row.unlisted, row.endDay),
            sampleList(row.withdrawn, row.endDay), row.firstMineStartedDay,
            row.firstMineOperatingDay, cash,
            contestJson(row) + ", " + biteJson(row) + ", " + raceJson(options, row),
            i + 1 < rows.size() ? "," : "");
    }
}

// One summary line: the spread over the seeds that measured the figure, or
// null when none did.
void printSampleLine(const std::string& name, const Sample& sample) {
    std::cout << std::format("    {}: {},\n", jsonString(name),
                             sample.seeds > 0 ? spreadJson(sample.spread) : std::string("null"));
}

bool always(const SweepRow& /*row*/) {
    return true;
}

// The spec 008 lines of the summary, which end it. Cash is in whole currency
// units, as final_cash is.
void printBiteSummary(const sim::Content& content, const std::vector<SweepRow>& rows) {
    using Figures = std::vector<std::int64_t> SweepRow::*;
    using Measured = bool (*)(sim::Day);
    const auto each = [&](std::string_view key, Measured measured, Figures figures) {
        for (std::size_t r = 0; r < content.resources.size(); ++r) {
            printSampleLine(std::format("{}_{}", key, content.resources[r].id),
                            sampleOver(
                                rows, [=](const SweepRow& row) { return measured(row.endDay); },
                                [=](const SweepRow& row) { return (row.*figures)[r]; }));
        }
    };
    each("below_breakeven_permille", biteMeasured, &SweepRow::belowBreakEvenPermille);
    each("price_year_min_permille", priceYearsMeasured, &SweepRow::priceYearMinPermille);
    each("price_year_max_permille", priceYearsMeasured, &SweepRow::priceYearMaxPermille);
    const std::size_t companies = rows.empty() ? 0 : rows.front().mineDaysIdled.size();
    for (std::size_t c = 0; c < companies; ++c) {
        printSampleLine(
            std::format("mine_days_idled_{}", c),
            sampleOver(rows, always, [c](const SweepRow& row) { return row.mineDaysIdled[c]; }));
    }
    for (std::size_t c = 0; c < companies; ++c) {
        printSampleLine(std::format("cash_y{}_{}", kCashYear, c),
                        sampleOver(rows, reachedCashYear,
                                   [c](const SweepRow& row) { return row.cashY10Cents[c] / 100; }));
    }
    // Bankruptcies after year 10 are counted only when some seed played on.
    const bool laterYears = std::ranges::any_of(
        rows, [](const SweepRow& row) { return laterYearsMeasured(row.endDay); });
    const auto later = std::count_if(rows.begin(), rows.end(), bankruptLater);
    std::cout << std::format(
        "    \"seeds_with_bankruptcy_y1_{}\": {},\n    \"seeds_with_bankruptcy_y{}_{}\": {},\n"
        "    \"seeds_with_idled_mine\": {}\n",
        kCashYear, std::count_if(rows.begin(), rows.end(), bankruptEarly), kCashYear + 1,
        kBiteLastYear, laterYears ? std::to_string(later) : "null",
        std::count_if(rows.begin(), rows.end(), hasIdledMine));
}

// The spec 010 lines of the summary: how the seeds ended, each company's
// wins, the founding day and the founder's day for each stage over the seeds
// that ended by a founding, and how many seeds were still playing at each
// year a figure below is sampled at.
void printRaceSummary(const Options& options, const std::vector<SweepRow>& rows) {
    const auto count = [&](RunOutcome outcome) {
        return std::count_if(rows.begin(), rows.end(),
                             [=](const SweepRow& row) { return row.outcome == outcome; });
    };
    const std::int64_t never = neverDay(options);
    std::cout << std::format(
        "    \"seeds_founded\": {},\n    \"seeds_deadline\": {},\n    \"seeds_running\": {},\n"
        "    \"wins\": [{}],\n",
        count(RunOutcome::Founded), count(RunOutcome::Deadline), count(RunOutcome::Running),
        joined(winsByCompany(rows)));
    printSampleLine("founded_day", sampleOver(rows, isFounded, [=](const SweepRow& row) {
                        return foundedDay(row, never);
                    }));
    const std::size_t stages =
        rows.empty() || rows.front().stageDays.empty() ? 0 : rows.front().stageDays.front().size();
    for (std::size_t stage = 0; stage < stages; ++stage) {
        printSampleLine(std::format("founder_stage_day_{}", stage + 1),
                        sampleOver(rows, isFounded, [=](const SweepRow& row) {
                            const auto founder = static_cast<std::size_t>(row.founder);
                            return dayOrNever(row.stageDays[founder][stage], never);
                        }));
    }
    for (std::size_t y = 0; y < kSweepYears.size(); ++y) {
        std::cout << std::format("    \"seeds_playing_at_y{}\": {},\n", kSweepYears.at(y),
                                 std::count_if(rows.begin(), rows.end(), [y](const SweepRow& row) {
                                     return yearSampled(row.endDay, y);
                                 }));
    }
    std::cout << std::format("    \"seeds_playing_at_y{}\": {},\n", kCashYear,
                             std::count_if(rows.begin(), rows.end(), reachedCashYear));
    std::cout << std::format("    \"seeds_with_rejected_commands\": {},\n",
                             std::count_if(rows.begin(), rows.end(), [](const SweepRow& row) {
                                 return row.rejectedCommands > 0;
                             }));
}

// The spec 006 lines of the summary; the spec 008 lines follow them.
void printContestSummary(const Options& options, const sim::Content& content,
                         const std::vector<SweepRow>& rows) {
    const auto line = [](std::string_view name, const Spread& spread) {
        std::cout << std::format("    {}: {},\n", jsonString(name), spreadJson(spread));
    };
    const std::size_t companies = rows.empty() ? 0 : rows.front().finalCashCents.size();
    for (std::size_t c = 0; c < companies; ++c) {
        line(std::format("mines_built_{}", c),
             spreadOver(rows, [c](const SweepRow& r) { return minesAtEnd(r, c); }));
    }
    // kNoContestPercent in a seed where a company never built a mine.
    line("mines_leader_over_trailer_percent", spreadOver(rows, minesRatioPercent));
    const std::size_t resources = rows.empty() ? 0 : rows.front().priceMeanPermille.size();
    for (std::size_t r = 0; r < resources; ++r) {
        line(std::format("price_mean_permille_{}", content.resources[r].id),
             spreadOver(rows, [r](const SweepRow& row) { return row.priceMeanPermille[r]; }));
    }
    line("first_depletion_day", spreadOver(rows, [&](const SweepRow& r) {
             return dayOrNever(r.firstDepletionDay, neverDay(options));
         }));
    // Unmeasured figures are null, never zero: depletion by a year no seed
    // was still playing at, and the life of a cohort with no mines in it.
    printSampleLine(
        std::format("depleted_by_y{}", kGoalLandYear),
        sampleOver(rows, reachedGoalYear, [](const SweepRow& r) { return r.depletedByGoalYear; }));
    const std::vector<std::int64_t> lives = pooledMineLives(rows);
    printSampleLine("cohort_mine_life_days",
                    {spreadOf(lives), static_cast<std::int64_t>(lives.size())});
    std::cout << std::format("    \"cohort_mines\": {},\n    \"seeds_with_bankruptcy\": {},\n",
                             lives.size(), std::count_if(rows.begin(), rows.end(), anyBankrupt));
}

void printSweep(const Options& options, const sim::Content& content,
                const std::vector<SweepRow>& rows, const std::vector<Check>& checks,
                bool allPassed) {
    std::cout << std::format(
        "{{\n  \"sweep\": {{\"first_seed\": {}, \"seeds\": {}, \"days\": {}, "
        "\"ai\": {}, \"deadline_day\": {}}},\n  \"rows\": [\n",
        options.seed, rows.size(), options.days, options.aiCompanies, content.program.deadlineDay);
    printSweepRows(options, rows);
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
    // A sample year covers the seeds still playing then; null when none was.
    const auto playing = [](std::size_t y) {
        return [y](const SweepRow& r) { return yearSampled(r.endDay, y); };
    };
    for (std::size_t y = 0; y < kSweepYears.size(); ++y) {
        printSampleLine(
            std::format("unlisted_y{}", kSweepYears.at(y)),
            sampleOver(rows, playing(y), [y](const SweepRow& r) { return r.unlisted.at(y); }));
    }
    for (std::size_t y = 0; y < kSweepYears.size(); ++y) {
        printSampleLine(
            std::format("withdrawn_y{}", kSweepYears.at(y)),
            sampleOver(rows, playing(y), [y](const SweepRow& r) { return r.withdrawn.at(y); }));
    }
    const std::int64_t never = neverDay(options);
    line("first_mine_started_day", spreadOver(rows, [&](const SweepRow& r) {
             return dayOrNever(r.firstMineStartedDay, never);
         }));
    line("first_mine_operating_day", spreadOver(rows, [&](const SweepRow& r) {
             return dayOrNever(r.firstMineOperatingDay, never);
         }));
    const std::size_t companies = rows.empty() ? 0 : rows.front().finalCashCents.size();
    for (std::size_t c = 0; c < companies; ++c) {
        // Whole currency units: a spread of cents reads badly at this size.
        line(std::format("final_cash_{}", c),
             spreadOver(rows, [c](const SweepRow& r) { return r.finalCashCents[c] / 100; }));
    }
    printRaceSummary(options, rows);
    printContestSummary(options, content, rows);
    printBiteSummary(content, rows);
    std::cout << "  },\n  \"never_day\": " << never << ",\n  \"checks\": [\n";
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
    std::vector<Check> checks = sweepChecks(options, rows);
    raceChecks(options, content, rows, checks);
    contestChecks(options, content, rows, checks);
    biteChecks(options, content, rows, checks);
    const bool allPassed = std::ranges::all_of(
        checks, [](const Check& check) { return check.passed || !check.gated; });
    printSweep(options, content, rows, checks, allPassed);
    if (!options.csvPath.empty()) {
        std::ofstream csv(options.csvPath);
        writeSweepCsv(csv, rows, neverDay(options));
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
                     "[--csv FILE] [--sanity] [--sweep N]\n"
                     "  --sanity  exit non-zero when a check of a single run fails; with --sweep "
                     "it has no further effect, since a sweep always exits non-zero when a gated "
                     "check fails\n";
        return 2;
    }
    const content::LoadResult loaded = content::loadFromDirectory(options.dataDir);
    if (!loaded.content.has_value()) {
        std::cerr << "content errors:\n" << content::formatErrors(loaded.errors);
        return 1;
    }
    const sim::Content& gameContent = *loaded.content;
    options.tickLimit = tickLimitFor(options.days, gameContent);
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
    const std::vector<Check> checks = buildChecks(gameContent, simulation.state(), stats);
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
