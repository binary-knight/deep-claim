#include "sim/program.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/plot_market.h"
#include "sim/research.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace sim {

namespace {

constexpr std::int64_t kPermille = 1000;

// Stages a company must have completed to finish as a lesser partner when
// another founds the program (spec 010, BR-006). Part of the rule, not a
// tuning number: the spec fixes it at two of the three stages.
constexpr std::uint32_t kLesserPartnerStages = 2;

bool knownCompany(const WorldState& state, CompanyId company) {
    return company.valid() && company.index() < state.companies.size();
}

// The stage the company is working on; nothing once it has completed them
// all (or the content has no program).
const ProgramStage* currentStage(const Content& content, const Company& company) {
    const std::size_t stage = company.programStage;
    return stage < content.program.stages.size() ? &content.program.stages[stage] : nullptr;
}

// Tonnes of the good the stage asks for; nothing when it does not need it.
std::optional<std::int64_t> stageNeed(const ProgramStage& stage, ResourceId resource) {
    for (const ProgramRequirement& requirement : stage.requirements) {
        if (requirement.resource == resource) {
            return requirement.tonnes;
        }
    }
    return std::nullopt;
}

bool deliveriesMet(const ProgramStage& stage, const Company& company) {
    return std::ranges::all_of(stage.requirements, [&](const ProgramRequirement& requirement) {
        return company.programDelivered[requirement.resource.index()] >= requirement.tonnes;
    });
}

// The one test of whether a stage may be completed: validation and
// programStageReady both answer from it.
std::optional<RejectReason> completionBlocker(const Content& content, const WorldState& state,
                                              CompanyId id) {
    if (!knownCompany(state, id)) {
        return RejectReason::UnknownCompany;
    }
    const Company& company = state.companies[id.index()];
    if (company.bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    const ProgramStage* stage = currentStage(content, company);
    if (stage == nullptr) {
        return RejectReason::ProgramComplete;
    }
    if (!techCompleted(company, stage->research) || !deliveriesMet(*stage, company)) {
        return RejectReason::ProgramStageNotReady;
    }
    if (stage->cost > availableCash(state, id)) {
        return RejectReason::InsufficientFunds;
    }
    return std::nullopt;
}

// How the race ended for a company that did not found the program. Without
// a founder nobody is a partner to anything; a bankrupt company has lost
// whatever it had completed.
ProgramResult resultBesideFounder(const Company& company, bool founded) {
    const bool partner =
        founded && company.bankrupt == 0 && company.programStage >= kLesserPartnerStages;
    return partner ? ProgramResult::LesserPartner : ProgramResult::Lost;
}

// Sets the outcome, once: today, the founder if there is one, and a result
// for every company.
void endGame(WorldState& state, CompanyId founder) {
    Outcome& outcome = state.outcome;
    outcome.ended = 1;
    outcome.day = state.day;
    outcome.founder = founder;
    outcome.results.assign(state.companies.size(), ProgramResult::Lost);
    for (std::size_t c = 0; c < state.companies.size(); ++c) {
        const bool isFounder = founder.valid() && founder.index() == c;
        outcome.results[c] = isFounder ? ProgramResult::Founded
                                       : resultBesideFounder(state.companies[c], founder.valid());
    }
}

}  // namespace

std::optional<RejectReason> validateDeliverToProgram(const Content& content,
                                                     const WorldState& state,
                                                     const DeliverToProgram& command) {
    if (!knownCompany(state, command.company)) {
        return RejectReason::UnknownCompany;
    }
    const Company& company = state.companies[command.company.index()];
    if (company.bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (!command.resource.valid() || command.resource.index() >= content.resources.size()) {
        return RejectReason::UnknownResource;
    }
    const ProgramStage* stage = currentStage(content, company);
    if (stage == nullptr) {
        return RejectReason::ProgramComplete;
    }
    const std::optional<std::int64_t> need = stageNeed(*stage, command.resource);
    if (!need) {
        return RejectReason::ProgramNotNeeded;
    }
    if (command.tonnes <= 0) {
        return RejectReason::InvalidQuantity;
    }
    const std::size_t r = command.resource.index();
    if (command.tonnes > company.inventoryTonnes[r]) {
        return RejectReason::InsufficientInventory;
    }
    // Subtracting keeps the comparison inside the range of the need.
    if (command.tonnes > *need - company.programDelivered[r]) {
        return RejectReason::ProgramNotNeeded;
    }
    return std::nullopt;
}

void applyDeliverToProgram(WorldState& state, const DeliverToProgram& command, TickReport& report) {
    Company& company = state.companies[command.company.index()];
    const std::size_t r = command.resource.index();
    company.inventoryTonnes[r] -= command.tonnes;
    company.programDelivered[r] += command.tonnes;
    Notice notice = makeNotice(NoticeKind::ProgramDelivered, command.company, PlotId());
    notice.resource = command.resource;
    notice.tonnes = command.tonnes;
    report.notices.push_back(notice);
}

std::optional<RejectReason> validateCompleteProgramStage(const Content& content,
                                                         const WorldState& state,
                                                         const CompleteProgramStage& command) {
    return completionBlocker(content, state, command.company);
}

void applyCompleteProgramStage(const Content& content, WorldState& state,
                               const CompleteProgramStage& command, TickReport& report) {
    Company& company = state.companies[command.company.index()];
    const ProgramStage& stage = content.program.stages[company.programStage];
    company.cash -= stage.cost;
    std::ranges::fill(company.programDelivered, 0);
    ++company.programStage;
    Notice notice =
        makeNotice(NoticeKind::ProgramStageComplete, command.company, PlotId(), stage.cost);
    notice.tonnes = company.programStage;
    notice.tech = stage.research;
    report.notices.push_back(notice);
    // The first company to complete the last stage founds the program, and
    // the game ends with this order.
    if (company.programStage == content.program.stages.size() && !gameEnded(state)) {
        endGame(state, command.company);
        report.notices.push_back(makeNotice(NoticeKind::ProgramFounded, command.company, PlotId()));
    }
}

bool programStageReady(const Content& content, const WorldState& state, CompanyId company) {
    return !completionBlocker(content, state, company);
}

std::int64_t programProgressPermille(const Content& content, const WorldState& state,
                                     CompanyId company) {
    if (!knownCompany(state, company)) {
        return 0;
    }
    const Company& racer = state.companies[company.index()];
    const ProgramStage* stage = currentStage(content, racer);
    if (stage == nullptr || stage->requirements.empty()) {
        return kPermille;
    }
    // A need may be as large as kMaxStockTonnes, so each share is taken with
    // the wide multiply; it is at most 1000, so the sum cannot overflow.
    std::int64_t shares = 0;
    for (const ProgramRequirement& requirement : stage->requirements) {
        if (requirement.tonnes <= 0) {
            shares += kPermille;
            continue;
        }
        const std::int64_t delivered = std::clamp<std::int64_t>(
            racer.programDelivered[requirement.resource.index()], 0, requirement.tonnes);
        shares += checkedMulDiv(delivered, kPermille, requirement.tonnes).value_or(kPermille);
    }
    return shares / static_cast<std::int64_t>(stage->requirements.size());
}

bool gameEnded(const WorldState& state) {
    return state.outcome.ended != 0;
}

void runProgram(const Content& content, WorldState& state, TickReport& report) {
    if (gameEnded(state) || state.day < content.program.deadlineDay) {
        return;
    }
    endGame(state, CompanyId());
    report.notices.push_back(makeNotice(NoticeKind::DeadlinePassed, CompanyId(), PlotId()));
}

}  // namespace sim
