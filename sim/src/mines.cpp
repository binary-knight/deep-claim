#include "sim/mines.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/tech.h"
#include "sim/world_state.h"

#include <cstdint>
#include <optional>

namespace sim {

namespace {

Notice mineNotice(NoticeKind kind, const Mine& mine, MineId id, Money amount = Money::zero()) {
    Notice notice = makeNotice(kind, mine.owner, mine.plot, amount);
    notice.mine = id;
    return notice;
}

}  // namespace

Money mineCapex(const Content& content, const Company& company, const Plot& plot) {
    const Money base = content.balance.mineCapexPerHectare * plot.areaHectares;
    return base.scaled(techMultiplier(content, company, TechEffectType::ExtractionCapexMult));
}

Money mineDailyOpex(const Content& content, const Company& company, MineState state) {
    switch (state) {
        case MineState::Operating:
            return content.balance.mineOpexPerDay.scaled(
                techMultiplier(content, company, TechEffectType::OpexMult));
        case MineState::Mothballed:
            return content.balance.mineOpexPerDay
                .scaled(techMultiplier(content, company, TechEffectType::OpexMult))
                .scaled(content.balance.mineMothballOpexMult);
        case MineState::Building:
        case MineState::Depleted:
            return Money::zero();
    }
    return Money::zero();
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
    std::int64_t output = mulDiv(base, (techMult * depletion).raw(), Fixed::kScale);
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
    state.mines.push_back(mine);
    const MineId id(static_cast<std::uint32_t>(state.mines.size() - 1));
    plot.mine = id;
    report.notices.push_back(mineNotice(NoticeKind::MineStarted, mine, id, capex));
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

void runMines(const Content& content, WorldState& state, TickReport& report) {
    for (std::uint32_t i = 0; i < state.mines.size(); ++i) {
        Mine& mine = state.mines[i];
        const MineId id(i);
        Company& owner = state.companies[mine.owner.index()];

        if (mine.state == MineState::Building) {
            if (mine.operationalDay <= state.day) {
                mine.state = MineState::Operating;
                report.notices.push_back(mineNotice(NoticeKind::MineOperational, mine, id));
            } else {
                continue;
            }
        }

        if (mine.state == MineState::Operating) {
            bool anythingLeft = false;
            for (DepositTruth& truth : state.hidden.plotDeposits[mine.plot.index()].deposits) {
                if (truth.tonnesRemaining <= 0) {
                    continue;
                }
                anythingLeft = true;
                if (!resourceUnlocked(content, owner, truth.resource)) {
                    continue;  // stays in the ground until the tech is known
                }
                const std::int64_t output = mineDailyOutput(
                    content, owner, truth.resource, truth.tonnesTotal, truth.tonnesRemaining);
                truth.tonnesRemaining -= output;
                owner.inventoryTonnes[truth.resource.index()] += output;
                mine.extractedTonnes[truth.resource.index()] += output;
            }
            if (!anythingLeft) {
                mine.state = MineState::Depleted;
                report.notices.push_back(mineNotice(NoticeKind::MineDepleted, mine, id));
                continue;
            }
        }

        owner.cash -= mineDailyOpex(content, owner, mine.state);
    }
}

}  // namespace sim
