#include "sim/simulation.h"

#include "sim/commands.h"
#include "sim/fatal.h"
#include "sim/plot_market.h"
#include "sim/save.h"
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
    return company;
}

}  // namespace

Simulation::Simulation(const Content& content, Seed seed, const GameSetup& setup)
    : content_(&content), contentHash_(hashContent(content)) {
    state_.rng = RngStreams(seed);
    generateWorld(content, state_);
    state_.companies.push_back(makeCompany(content, setup.playerName, CompanyKind::Player));
    for (const std::string& name : setup.aiNames) {
        state_.companies.push_back(makeCompany(content, name, CompanyKind::Ai));
    }
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

void Simulation::applyCommands() {
    // Deterministic order: by company id, then submission order.
    std::stable_sort(pending_.begin(), pending_.end(), [](const Command& a, const Command& b) {
        return commandCompany(a) < commandCompany(b);
    });
    for (const Command& command : pending_) {
        std::visit(
            [&](const auto& concrete) {
                using T = std::decay_t<decltype(concrete)>;
                if constexpr (std::is_same_v<T, BidOnPlot>) {
                    if (const std::optional<RejectReason> reason = validateBid(state_, concrete)) {
                        report_.notices.push_back(
                            makeNotice(NoticeKind::CommandRejected, concrete.company, concrete.plot,
                                       AuctionId(), concrete.amount, *reason));
                    } else {
                        applyBid(state_, concrete);
                    }
                }
            },
            command);
    }
    pending_.clear();
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
