#include "sim/simulation.h"

#include "sim/commands.h"
#include "sim/events.h"
#include "sim/fatal.h"
#include "sim/finance.h"
#include "sim/markets.h"
#include "sim/mines.h"
#include "sim/plot_market.h"
#include "sim/research.h"
#include "sim/save.h"
#include "sim/surveys.h"
#include "sim/view.h"
#include "sim/worldgen.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <variant>

namespace sim {

namespace {

Company makeCompany(const Content& content, std::string name, CompanyKind kind) {
    Company company;
    company.name = std::move(name);
    company.kind = kind;
    company.cash = content.balance.startingCapital;
    company.inventoryTonnes.assign(content.resources.size(), 0);
    // Spec 001: the defaults are for the player; the AI keeps its own daily
    // selling rule until the AI tuning step moves it onto policies.
    const bool usesDefaults = kind == CompanyKind::Player;
    company.sellPolicies.assign(content.resources.size(),
                                usesDefaults ? content.balance.defaultSellPolicy : SellPolicy{});
    company.coverCosts = usesDefaults && content.balance.defaultCoverCosts ? 1 : 0;
    company.autoSoldTonnes.assign(content.resources.size(), 0);
    company.autoSoldProceeds.assign(content.resources.size(), Money::zero());
    company.programDelivered.assign(content.resources.size(), 0);
    return company;
}

}  // namespace

Simulation::Simulation(const Content& content, Seed seed, const GameSetup& setup)
    : content_(&content), contentHash_(hashContent(content)) {
    state_.rng = RngStreams(seed);
    generateWorld(content, state_);
    initMarkets(content, state_);
    state_.companies.push_back(makeCompany(content, setup.playerName, CompanyKind::Player));
    for (const std::string& name : setup.aiNames) {
        state_.companies.push_back(makeCompany(content, name, CompanyKind::Ai));
    }
    // Spec 010: one result per company, none until the game has ended.
    state_.outcome.results.assign(state_.companies.size(), ProgramResult::None);
    report_.day = 0;
    listPlots(content, state_, content.balance.plotsListedAtStart, report_);
}

void Simulation::submit(const Command& command) {
    pending_.push_back(command);
}

void Simulation::collectCommands(CompanyId company, Controller& controller) {
    std::vector<Command> commands;
    controller.decide(view(company), commands);
    for (Command& command : commands) {
        setCommandCompany(command, company);
        pending_.push_back(command);
    }
}

void Simulation::reject(CompanyId company, PlotId plot, Money amount, RejectReason reason) {
    report_.notices.push_back(
        makeNotice(NoticeKind::CommandRejected, company, plot, amount, reason));
}

void Simulation::applyCommands() {
    // Deterministic order: by company id, then submission order.
    std::stable_sort(pending_.begin(), pending_.end(), [](const Command& a, const Command& b) {
        return commandCompany(a) < commandCompany(b);
    });
    for (const Command& command : pending_) {
        dispatch(command);
    }
    pending_.clear();
}

void Simulation::handle(const BidOnPlot& c) {
    if (const auto reason = validateBid(state_, c)) {
        reject(c.company, c.plot, c.amount, *reason);
    } else {
        applyBid(state_, c);
    }
}

void Simulation::handle(const CommissionSurvey& c) {
    if (const auto reason = validateSurvey(*content_, state_, c)) {
        reject(c.company, c.plot, Money::zero(), *reason);
    } else {
        applySurvey(*content_, state_, c, report_);
    }
}

void Simulation::handle(const BuildMine& c) {
    if (const auto reason = validateBuildMine(*content_, state_, c)) {
        reject(c.company, c.plot, Money::zero(), *reason);
    } else {
        applyBuildMine(*content_, state_, c, report_);
    }
}

void Simulation::handle(const SetMineState& c) {
    if (const auto reason = validateSetMineState(state_, c)) {
        Notice notice =
            makeNotice(NoticeKind::CommandRejected, c.company, PlotId(), Money::zero(), *reason);
        notice.mine = c.mine;
        report_.notices.push_back(notice);
    } else {
        applySetMineState(state_, c, report_);
    }
}

void Simulation::handle(const SellOre& c) {
    if (const auto reason = validateSell(state_, c)) {
        Notice notice =
            makeNotice(NoticeKind::CommandRejected, c.company, PlotId(), Money::zero(), *reason);
        notice.resource = c.resource;
        notice.tonnes = c.tonnes;
        report_.notices.push_back(notice);
    } else {
        applySell(state_, c, report_);
    }
}

void Simulation::handle(const TakeLoan& c) {
    if (const auto reason = validateTakeLoan(*content_, state_, c)) {
        reject(c.company, PlotId(), c.principal, *reason);
    } else {
        applyTakeLoan(*content_, state_, c, report_);
    }
}

void Simulation::handle(const RepayLoan& c) {
    if (const auto reason = validateRepayLoan(state_, c)) {
        Notice notice =
            makeNotice(NoticeKind::CommandRejected, c.company, PlotId(), c.amount, *reason);
        notice.loan = c.loan;
        report_.notices.push_back(notice);
    } else {
        applyRepayLoan(state_, c, report_);
    }
}

void Simulation::handle(const StartResearch& c) {
    if (const auto reason = validateStartResearch(*content_, state_, c)) {
        Notice notice =
            makeNotice(NoticeKind::CommandRejected, c.company, PlotId(), Money::zero(), *reason);
        notice.tech = c.tech;
        report_.notices.push_back(notice);
    } else {
        applyStartResearch(*content_, state_, c, report_);
    }
}

void Simulation::handle(const SetSellPolicy& c) {
    if (const auto reason = validateSetSellPolicy(state_, c)) {
        Notice notice =
            makeNotice(NoticeKind::CommandRejected, c.company, PlotId(), c.floorPrice, *reason);
        notice.resource = c.resource;
        notice.tonnes = c.reserveTonnes;
        report_.notices.push_back(notice);
    } else {
        applySetSellPolicy(state_, c);
    }
}

void Simulation::handle(const SetCoverCosts& c) {
    if (const auto reason = validateSetCoverCosts(state_, c)) {
        reject(c.company, PlotId(), Money::zero(), *reason);
    } else {
        applySetCoverCosts(state_, c);
    }
}

void Simulation::dispatch(const Command& command) {
    std::visit([this](const auto& concrete) { handle(concrete); }, command);
}

const TickReport& Simulation::tick() {
    if (state_.day >= kMaxDay) {
        fatal("Simulation::tick: day limit reached");
    }
    report_ = TickReport{};
    report_.day = state_.day;
    // Pipeline order is fixed (see DESIGN.md). Steps for systems that do not
    // exist yet are absent, not stubbed.
    applyCommands();                            // 1
    runPlotMarket(*content_, state_, report_);  // 2
    runSurveys(*content_, state_, report_);     // 3
    runMines(*content_, state_, report_);       // 4
    runSellPolicies(*content_, state_);         // 4b (spec 001)
    runMarkets(*content_, state_);              // 5
    runFinance(*content_, state_, report_);     // 6
    runResearch(*content_, state_, report_);    // 7
    runEvents(*content_, state_, report_);      // 8
    ++state_.day;                               // 9
    return report_;
}

void Simulation::tick(Day days) {
    for (Day i = 0; i < days; ++i) {
        tick();
    }
}

std::vector<Notice> Simulation::reportFor(CompanyId viewer) const {
    std::vector<Notice> visible;
    for (const Notice& notice : report_.notices) {
        if (noticeVisibleTo(notice, viewer)) {
            visible.push_back(notice);
        }
    }
    return visible;
}

CompanyView Simulation::view(CompanyId company) const {
    if (!company.valid() || company.index() >= state_.companies.size()) {
        fatal("Simulation::view: invalid company id");
    }
    return makeCompanyView(*content_, state_, company);
}

std::uint64_t Simulation::stateHash() const {
    return hashWorld(state_);
}

std::vector<std::uint8_t> Simulation::save() const {
    return saveGame(state_, pending_, contentHash_);
}

bool Simulation::load(std::span<const std::uint8_t> bytes) {
    WorldState loadedState;
    std::vector<Command> loadedPending;
    if (!loadGame(bytes, *content_, loadedState, loadedPending)) {
        return false;
    }
    state_ = std::move(loadedState);
    pending_ = std::move(loadedPending);
    report_ = TickReport{};
    report_.day = state_.day;
    return true;
}

}  // namespace sim
