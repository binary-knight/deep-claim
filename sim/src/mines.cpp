#include "sim/mines.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/events.h"
#include "sim/finance.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/tech.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace sim {

namespace {

Notice mineNotice(NoticeKind kind, const Mine& mine, MineId id, Money amount = Money::zero()) {
    Notice notice = makeNotice(kind, mine.owner, mine.plot, amount);
    notice.mine = id;
    return notice;
}

}  // namespace

Money mineCapexQuote(const Content& content, const std::vector<TechId>& completed,
                     std::int64_t areaHectares) {
    const Money base = content.balance.mineCapexPerHectare * areaHectares;
    return base.scaled(techMultiplier(content, completed, TechEffectType::ExtractionCapexMult));
}

Money mineCapex(const Content& content, const Company& company, const Plot& plot) {
    return mineCapexQuote(content, company.completedTechs, plot.areaHectares);
}

namespace {

// Money arithmetic for the derived mine figures: every step is clamped to
// +/- kMaxCashCents, so nothing here can overflow or abort.
std::int64_t clampCash(std::int64_t cents) {
    return std::clamp(cents, -kMaxCashCents, kMaxCashCents);
}

// a * b / c, clamped; `whenTooLarge` when the quotient does not fit.
std::int64_t saturatingMulDiv(std::int64_t a, std::int64_t b, std::int64_t c,
                              std::int64_t whenTooLarge) {
    return clampCash(checkedMulDiv(a, b, c).value_or(whenTooLarge));
}

// The mine's output of one ore on its most recent operating day, and the
// price it is valued at; nothing when either is missing or negative.
std::int64_t outputTonnes(const Mine& mine, std::size_t resource) {
    return resource < mine.lastOutputTonnes.size()
               ? std::max<std::int64_t>(0, mine.lastOutputTonnes[resource])
               : 0;
}

std::int64_t priceCents(std::span<const Money> prices, std::size_t resource) {
    return resource < prices.size() ? std::max<std::int64_t>(0, prices[resource].cents()) : 0;
}

std::int64_t revenueCents(const Mine& mine, std::span<const Money> prices, std::size_t resource) {
    return saturatingMulDiv(outputTonnes(mine, resource), priceCents(prices, resource), 1,
                            kMaxCashCents);
}

// The highest running cost among the mine's cost ores (see mines.h), before
// multipliers. Reads observed output and the owner's unlocks only.
std::int64_t costOreOpexCents(const Content& content, const Company& company, const Mine& mine) {
    std::int64_t produced = 0;
    std::int64_t extractable = 0;
    bool producing = false;
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        const std::int64_t opex = content.resources[r].mineOpexPerDay.cents();
        if (outputTonnes(mine, r) > 0) {
            produced = std::max(produced, opex);
            producing = true;
        } else if (resourceUnlocked(content, company, ResourceId(r))) {
            extractable = std::max(extractable, opex);
        }
    }
    // Every ore with output is unlocked, so when nothing is produced the
    // second figure covers all the owner can extract.
    return producing ? produced : extractable;
}

// The ore earning most at `prices` among those the mine produces, the
// lowest id on a tie; nullopt when it produces nothing.
std::optional<std::size_t> principalOre(const Content& content, const Mine& mine,
                                        std::span<const Money> prices) {
    std::optional<std::size_t> best;
    for (std::size_t r = 0; r < content.resources.size(); ++r) {
        if (outputTonnes(mine, r) <= 0) {
            continue;
        }
        if (!best || compareProducts(outputTonnes(mine, r), priceCents(prices, r),
                                     outputTonnes(mine, *best), priceCents(prices, *best)) > 0) {
            best = r;
        }
    }
    return best;
}

// A running cost of `baseCents` a day as `company` pays it today: times its
// research cost multiplier and the active event cost multiplier.
std::int64_t operatingOpexCents(const Content& content, const WorldState& world,
                                const Company& company, std::int64_t baseCents) {
    const std::int64_t mult =
        checkedMulDiv(techMultiplier(content, company, TechEffectType::OpexMult).raw(),
                      eventMultiplier(content, world, EventEffectType::OpexMult).raw(),
                      Fixed::kScale)
            .value_or(kMaxCashCents);
    return saturatingMulDiv(baseCents, mult, Fixed::kScale, kMaxCashCents);
}

}  // namespace

Money mineRunningCost(const Content& content, const WorldState& world, const Company& company,
                      const Mine& mine, MineState as) {
    if (as != MineState::Operating && as != MineState::Mothballed) {
        return Money::zero();
    }
    std::int64_t cents =
        operatingOpexCents(content, world, company, costOreOpexCents(content, company, mine));
    if (as == MineState::Mothballed) {
        cents = saturatingMulDiv(cents, content.balance.mineMothballOpexMult.raw(), Fixed::kScale,
                                 kMaxCashCents);
    }
    return Money::fromCents(cents);
}

Money mineOpexQuote(const Content& content, const WorldState& world, const Company& company,
                    ResourceId resource) {
    if (!resource.valid() || resource.index() >= content.resources.size()) {
        return Money::zero();
    }
    return Money::fromCents(operatingOpexCents(
        content, world, company, content.resources[resource.index()].mineOpexPerDay.cents()));
}

Money mineDailyMargin(const Content& content, const WorldState& world, const Company& company,
                      const Mine& mine, std::span<const Money> prices) {
    std::int64_t revenue = 0;
    for (std::size_t r = 0; r < content.resources.size(); ++r) {
        revenue = clampCash(revenue + revenueCents(mine, prices, r));
    }
    const Money cost = mineRunningCost(content, world, company, mine, MineState::Operating);
    return Money::fromCents(clampCash(revenue - cost.cents()));
}

Money mineBreakEvenPrice(const Content& content, const WorldState& world, const Company& company,
                         const Mine& mine, std::span<const Money> prices) {
    const std::optional<std::size_t> principal = principalOre(content, mine, prices);
    const std::int64_t tonnes = principal ? outputTonnes(mine, *principal) : 0;
    if (!principal || tonnes <= 0) {
        return Money::zero();  // no output to price
    }
    std::int64_t others = 0;
    for (std::size_t r = 0; r < content.resources.size(); ++r) {
        if (r != *principal) {
            others = clampCash(others + revenueCents(mine, prices, r));
        }
    }
    const Money cost = mineRunningCost(content, world, company, mine, MineState::Operating);
    const std::int64_t needed = clampCash(cost.cents() - others);
    if (needed <= 0) {
        return Money::zero();
    }
    return Money::fromCents(needed / tonnes + (needed % tonnes != 0 ? 1 : 0));
}

std::int64_t mineDailyOutput(const Content& content, const Company& company, ResourceId resource,
                             std::int64_t tonnesTotal, std::int64_t tonnesRemaining) {
    if (tonnesRemaining <= 0 || tonnesTotal <= 0) {
        return 0;
    }
    const std::int64_t base = content.resources[resource.index()].baseOutputTonnesPerDay;
    const Fixed techMult = techMultiplier(content, company, TechEffectType::OutputMult);
    // depletion factor = 0.5 + 0.5 * remaining / total
    const Fixed share = Fixed::fromRaw(mulDiv(tonnesRemaining, Fixed::kScale, tonnesTotal));
    const Fixed depletion = Fixed::fromRatio(1, 2) + share / 2;
    std::int64_t output =
        checkedMulDiv(base, (techMult * depletion).raw(), Fixed::kScale).value_or(tonnesRemaining);
    if (output < 1) {
        output = 1;
    }
    return output < tonnesRemaining ? output : tonnesRemaining;
}

std::optional<RejectReason> validateBuildMine(const Content& content, const WorldState& state,
                                              const BuildMine& command) {
    if (!command.company.valid() || command.company.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    const Company& company = state.companies[command.company.index()];
    if (company.bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (!command.plot.valid() || command.plot.index() >= state.plots.size()) {
        return RejectReason::UnknownPlot;
    }
    const Plot& plot = state.plots[command.plot.index()];
    if (plot.owner != command.company) {
        return RejectReason::NotPlotOwner;
    }
    if (plot.mine.valid()) {
        return RejectReason::MineExists;
    }
    if (mineCapex(content, company, plot) > availableCash(state, command.company)) {
        return RejectReason::InsufficientFunds;
    }
    return std::nullopt;
}

void applyBuildMine(const Content& content, WorldState& state, const BuildMine& command,
                    TickReport& report) {
    Company& company = state.companies[command.company.index()];
    Plot& plot = state.plots[command.plot.index()];
    const Money capex = mineCapex(content, company, plot);
    company.cash -= capex;

    Mine mine;
    mine.plot = command.plot;
    mine.owner = command.company;
    mine.builtDay = state.day;
    mine.operationalDay = state.day + content.balance.mineBuildDays;
    mine.state = MineState::Building;
    mine.extractedTonnes.assign(content.resources.size(), 0);
    mine.lastDayTonnes.assign(content.resources.size(), 0);
    mine.lastOutputTonnes.assign(content.resources.size(), 0);
    state.mines.push_back(mine);
    const MineId id(static_cast<std::uint32_t>(state.mines.size() - 1));
    plot.mine = id;
    // Public notice: that a mine is being built, not what it cost.
    report.notices.push_back(mineNotice(NoticeKind::MineStarted, mine, id));
}

std::optional<RejectReason> validateSetMineState(const WorldState& state,
                                                 const SetMineState& command) {
    if (!command.company.valid() || command.company.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    if (state.companies[command.company.index()].bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (!command.mine.valid() || command.mine.index() >= state.mines.size()) {
        return RejectReason::UnknownMine;
    }
    const Mine& mine = state.mines[command.mine.index()];
    if (mine.owner != command.company) {
        return RejectReason::NotMineOwner;
    }
    if (mine.state != MineState::Operating && mine.state != MineState::Mothballed) {
        return RejectReason::MineNotSwitchable;
    }
    return std::nullopt;
}

void applySetMineState(WorldState& state, const SetMineState& command, TickReport& report) {
    Mine& mine = state.mines[command.mine.index()];
    const MineState target = command.operate != 0 ? MineState::Operating : MineState::Mothballed;
    if (mine.state == target) {
        return;
    }
    mine.state = target;
    report.notices.push_back(mineNotice(NoticeKind::MineSwitched, mine, command.mine));
}

bool mineHasExtractable(const Content& content, const WorldState& state, const Mine& mine) {
    const Company& owner = state.companies[mine.owner.index()];
    return std::ranges::any_of(
        state.hidden.plotDeposits[mine.plot.index()].deposits, [&](const DepositTruth& truth) {
            return truth.tonnesRemaining > 0 && resourceUnlocked(content, owner, truth.resource);
        });
}

// One day of extraction: every unlocked deposit with ore left yields its
// daily output into the owner's inventory. Returns whether any ore remains
// (locked ore counts: the mine is not depleted).
bool extractDay(const Content& content, WorldState& state, Company& owner, Mine& mine) {
    bool anythingLeft = false;
    for (DepositTruth& truth : state.hidden.plotDeposits[mine.plot.index()].deposits) {
        if (truth.tonnesRemaining > 0 && resourceUnlocked(content, owner, truth.resource)) {
            const std::int64_t output = mineDailyOutput(content, owner, truth.resource,
                                                        truth.tonnesTotal, truth.tonnesRemaining);
            truth.tonnesRemaining -= output;
            std::int64_t& stock = owner.inventoryTonnes[truth.resource.index()];
            stock = checkedAdd(stock, output);
            std::int64_t& lifetime = mine.extractedTonnes[truth.resource.index()];
            lifetime = checkedAdd(lifetime, output);
            mine.lastDayTonnes[truth.resource.index()] = output;
        }
        anythingLeft = anythingLeft || truth.tonnesRemaining > 0;
    }
    return anythingLeft;
}

void warnIdleMines(const Content& content, const WorldState& state, TickReport& report) {
    for (std::uint32_t i = 0; i < state.mines.size(); ++i) {
        const Mine& mine = state.mines[i];
        if (mine.state != MineState::Operating ||
            state.companies[mine.owner.index()].bankrupt != 0 ||
            mineHasExtractable(content, state, mine)) {
            continue;
        }
        report.notices.push_back(mineNotice(NoticeKind::IdleMineWarning, mine, MineId(i)));
    }
}

const Survey* latestOwnSurvey(const WorldState& state, const Mine& mine) {
    // Surveys are appended in commission order: the last complete one of the
    // owner for this plot is the latest.
    const Survey* latest = nullptr;
    for (const Survey& survey : state.surveys) {
        if (survey.company == mine.owner && survey.plot == mine.plot && survey.complete != 0) {
            latest = &survey;
        }
    }
    return latest;
}

namespace {

// Tonnes of a resource taken out since the survey reported (see mines.h).
std::int64_t extractedSinceSurvey(const WorldState& state, const Mine& mine, const Survey& survey,
                                  ResourceId resource) {
    const std::size_t r = resource.index();
    std::int64_t since = mine.extractedTonnes[r];
    if (mine.state != MineState::Building && survey.completesDay >= mine.operationalDay) {
        const std::int64_t days = state.day > survey.completesDay
                                      ? static_cast<std::int64_t>(state.day) -
                                            static_cast<std::int64_t>(survey.completesDay)
                                      : 0;
        since = std::min(since, checkedMulDiv(mine.lastOutputTonnes[r], days, 1).value_or(since));
    }
    return since;
}

std::int64_t estimateLowerBound(const SurveyEstimate& estimate) {
    // estimate x (1 - relative error), never below zero.
    const Fixed share = std::max(Fixed::zero(), Fixed::one() - estimate.relativeError);
    return mulDiv(estimate.tonnesEstimate, share.raw(), Fixed::kScale);
}

}  // namespace

std::int64_t estimatedRemainingTonnes(const WorldState& state, const Mine& mine,
                                      const Survey& survey, const SurveyEstimate& estimate) {
    return std::max<std::int64_t>(
        0, estimate.tonnesEstimate - extractedSinceSurvey(state, mine, survey, estimate.resource));
}

void warnRunningLow(const Content& /*content*/, WorldState& state, TickReport& report) {
    for (std::uint32_t i = 0; i < state.mines.size(); ++i) {
        Mine& mine = state.mines[i];
        if (mine.state != MineState::Operating ||
            state.companies[mine.owner.index()].bankrupt != 0) {
            continue;
        }
        const Survey* latest = latestOwnSurvey(state, mine);
        if (latest == nullptr) {
            continue;
        }
        const SurveyId latestId(static_cast<std::uint32_t>(latest - state.surveys.data()));
        if (mine.lowWarnedSurvey == latestId) {
            continue;  // said already for this survey
        }
        // Days of ore left by the cautious end of the survey, for the
        // resource that lasts longest. Signed: negative once the low end
        // is passed.
        bool any = false;
        std::int64_t daysLeft = 0;
        for (const SurveyEstimate& estimate : latest->estimates) {
            const std::int64_t output = mine.lastOutputTonnes[estimate.resource.index()];
            if (output <= 0) {
                continue;
            }
            const std::int64_t used = extractedSinceSurvey(state, mine, *latest, estimate.resource);
            const std::int64_t days = (estimateLowerBound(estimate) - used) / output;
            daysLeft = any ? std::max(daysLeft, days) : days;
            any = true;
        }
        // Said at the first month end the figure is within kRunningLowDays,
        // however far past the low end the mine already is, and remembered
        // on the mine so mothballing, a new unlock or a change of output
        // cannot bring it back. A newer survey starts afresh.
        if (!any || daysLeft > static_cast<std::int64_t>(kRunningLowDays)) {
            continue;
        }
        mine.lowWarnedSurvey = latestId;
        Notice notice = mineNotice(NoticeKind::MineRunningLow, mine, MineId(i));
        notice.tonnes = std::max<std::int64_t>(0, daysLeft);
        report.notices.push_back(notice);
    }
}

namespace {

// A mine's margin says something about the market only when the mine is
// working and has a day's output to price. The view's rule for showing
// earnings, less the mothballed case: an idle mine is not losing money.
bool countsTowardLosses(const WorldState& state, const Mine& mine) {
    return mine.state == MineState::Operating &&
           state.companies[mine.owner.index()].bankrupt == 0 &&
           std::ranges::any_of(mine.lastOutputTonnes,
                               [](std::int64_t tonnes) { return tonnes > 0; });
}

// A losing day for the notice (spec 010, BR-014): a loss of at least the set
// share of the day's running cost. A share of nothing counts every loss; a
// day at exactly break-even is never one.
bool isLosingDay(const Content& content, Money margin, Money runningCost) {
    return margin < Money::zero() &&
           -margin >= runningCost.scaled(content.balance.mineLossNoticeMinShare);
}

// A day that was not a losing day ends the count. After a notice it also
// adds to the run of such days, and the full run re-arms the notice.
void countClearDay(const Content& content, Mine& mine) {
    mine.lossDays = 0;
    if (mine.lossAnnounced == 0) {
        return;
    }
    // A save can carry a count already at the re-arm number.
    if (mine.lossClearDays < content.balance.mineLossRearmDays) {
        ++mine.lossClearDays;
    }
    if (mine.lossClearDays >= content.balance.mineLossRearmDays) {
        mine.lossAnnounced = 0;
        mine.lossClearDays = 0;
    }
}

// Advances every mine's count of consecutive losing days (spec 008), at the
// prices as the mines step sees them: yesterday's close, before today's
// market update. A mine that stops counting (see above) starts afresh. On
// the day the count reaches the notice threshold the owner is told, once:
// the notice carries the day's loss and the count. It is not said again
// until the mine has had the re-arm run of days that are not losing days,
// or has been stopped (spec 010, BR-014).
void updateMineLosses(const Content& content, WorldState& state, TickReport& report) {
    std::vector<Money> prices;
    prices.reserve(state.markets.size());
    for (const Market& market : state.markets) {
        prices.push_back(market.price);
    }
    for (std::uint32_t i = 0; i < state.mines.size(); ++i) {
        Mine& mine = state.mines[i];
        const Company& owner = state.companies[mine.owner.index()];
        if (!countsTowardLosses(state, mine)) {
            mine.lossDays = 0;
            mine.lossAnnounced = 0;
            mine.lossClearDays = 0;
            continue;
        }
        const Money margin = mineDailyMargin(content, state, owner, mine, prices);
        const Money cost = mineRunningCost(content, state, owner, mine, MineState::Operating);
        if (!isLosingDay(content, margin, cost)) {
            countClearDay(content, mine);
            continue;
        }
        mine.lossClearDays = 0;
        // Saturates: a save can carry any count, and a wrap would read as a reset.
        if (mine.lossDays < std::numeric_limits<std::uint32_t>::max()) {
            ++mine.lossDays;
        }
        // At or past the threshold, so a save that carries a longer run not
        // yet announced is told on its next losing day instead of never.
        if (mine.lossAnnounced != 0 || mine.lossDays < content.balance.mineLossNoticeDays) {
            continue;
        }
        mine.lossAnnounced = 1;
        // The margin is clamped to +/- kMaxCashCents, so negating it cannot overflow.
        Notice notice = mineNotice(NoticeKind::MineLosingMoney, mine, MineId(i), -margin);
        notice.tonnes = static_cast<std::int64_t>(mine.lossDays);
        report.notices.push_back(notice);
    }
}

}  // namespace

void runMines(const Content& content, WorldState& state, TickReport& report) {
    for (std::uint32_t i = 0; i < state.mines.size(); ++i) {
        Mine& mine = state.mines[i];
        const MineId id(i);
        Company& owner = state.companies[mine.owner.index()];

        std::fill(mine.lastDayTonnes.begin(), mine.lastDayTonnes.end(), 0);
        if (mine.state == MineState::Closed || mine.state == MineState::Depleted) {
            continue;
        }
        // A mine that opens on a plot with nothing it can extract (no deposit,
        // or only locked ore) says so once instead of "operational" followed
        // by "depleted" on the same day.
        bool foundNothing = false;
        if (mine.state == MineState::Building) {
            if (mine.operationalDay <= state.day) {
                mine.state = MineState::Operating;
                foundNothing = !mineHasExtractable(content, state, mine);
                report.notices.push_back(mineNotice(
                    foundNothing ? NoticeKind::MineFoundNothing : NoticeKind::MineOperational, mine,
                    id));
            } else {
                continue;
            }
        }

        if (mine.state == MineState::Operating) {
            const bool anythingLeft = extractDay(content, state, owner, mine);
            // The whole vector is replaced so a resource that ran out reads zero.
            mine.lastOutputTonnes = mine.lastDayTonnes;
            // Today's production day is paid for even if it emptied the mine.
            owner.cash = debitCash(
                owner.cash, mineRunningCost(content, state, owner, mine, MineState::Operating));
            if (!anythingLeft) {
                mine.state = MineState::Depleted;
                if (!foundNothing) {
                    report.notices.push_back(mineNotice(NoticeKind::MineDepleted, mine, id));
                }
            }
            continue;
        }

        owner.cash =
            debitCash(owner.cash, mineRunningCost(content, state, owner, mine, mine.state));
    }
    updateMineLosses(content, state, report);
}

}  // namespace sim
