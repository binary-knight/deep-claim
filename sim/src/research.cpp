#include "sim/research.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace sim {

namespace {

Notice researchNotice(NoticeKind kind, CompanyId company, TechId tech,
                      Money amount = Money::zero()) {
    Notice notice = makeNotice(kind, company, PlotId(), amount);
    notice.tech = tech;
    return notice;
}

// The program stage `tech` is the research for, counting from 1 (spec 010);
// nothing for an ordinary project.
std::optional<std::uint32_t> programStageOf(const Content& content, TechId tech) {
    for (std::uint32_t i = 0; i < content.program.stages.size(); ++i) {
        if (content.program.stages[i].research == tech) {
            return i + 1;
        }
    }
    return std::nullopt;
}

bool inProgress(const WorldState& state, CompanyId company, TechId tech) {
    return std::ranges::any_of(state.research, [&](const Research& research) {
        return research.company == company && research.tech == tech && research.complete == 0;
    });
}

// Why the company may not start the project, money and a busy lab aside;
// nothing when it may. The one test of that: researchOpenTo and
// validateStartResearch both answer from it.
std::optional<RejectReason> closedBy(const Content& content, const WorldState& state, CompanyId id,
                                     TechId tech) {
    if (!id.valid() || id.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    const Company& company = state.companies[id.index()];
    if (company.bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (!tech.valid() || tech.index() >= content.techs.size()) {
        return RejectReason::UnknownTech;
    }
    if (techCompleted(company, tech)) {
        return RejectReason::TechAlreadyKnown;
    }
    // One project at a time, and this one is the one.
    if (inProgress(state, id, tech)) {
        return RejectReason::ResearchBusy;
    }
    if (!prerequisitesMet(content, company, tech)) {
        return RejectReason::PrerequisitesMissing;
    }
    // A stage's project waits for the stages before it (spec 010, BR-004).
    if (content.techs[tech.index()].requiresProgramStage > company.programStage) {
        return RejectReason::TechNeedsProgramStage;
    }
    return std::nullopt;
}

}  // namespace

bool techCompleted(const Company& company, TechId tech) {
    return std::find(company.completedTechs.begin(), company.completedTechs.end(), tech) !=
           company.completedTechs.end();
}

bool prerequisitesMet(const Content& content, const Company& company, TechId tech) {
    return std::ranges::all_of(content.techs[tech.index()].prerequisites,
                               [&](TechId p) { return techCompleted(company, p); });
}

std::optional<ResearchId> activeResearch(const WorldState& state, CompanyId company) {
    for (std::uint32_t i = 0; i < state.research.size(); ++i) {
        if (state.research[i].company == company && state.research[i].complete == 0) {
            return ResearchId(i);
        }
    }
    return std::nullopt;
}

bool researchOpenTo(const Content& content, const WorldState& state, CompanyId company,
                    TechId tech) {
    return !closedBy(content, state, company, tech);
}

std::optional<RejectReason> validateStartResearch(const Content& content, const WorldState& state,
                                                  const StartResearch& command) {
    const std::optional<RejectReason> closed =
        closedBy(content, state, command.company, command.tech);
    // A busy lab is the reason given ahead of what the project itself still
    // waits for, so those two step aside for it.
    const bool waiting = closed == RejectReason::PrerequisitesMissing ||
                         closed == RejectReason::TechNeedsProgramStage;
    if (closed && !waiting) {
        return closed;
    }
    if (activeResearch(state, command.company)) {
        return RejectReason::ResearchBusy;
    }
    if (closed) {
        return closed;
    }
    if (content.techs[command.tech.index()].cost > availableCash(state, command.company)) {
        return RejectReason::InsufficientFunds;
    }
    return std::nullopt;
}

void applyStartResearch(const Content& content, WorldState& state, const StartResearch& command,
                        TickReport& report) {
    const TechDef& tech = content.techs[command.tech.index()];
    state.companies[command.company.index()].cash -= tech.cost;
    Research research;
    research.company = command.company;
    research.tech = command.tech;
    research.startedDay = state.day;
    research.completesDay = state.day + tech.durationDays;
    state.research.push_back(research);
    report.notices.push_back(
        researchNotice(NoticeKind::ResearchStarted, command.company, command.tech, tech.cost));
    // Work on a stage of the program is news for everyone (spec 010, BR-004).
    if (const std::optional<std::uint32_t> stage = programStageOf(content, command.tech)) {
        Notice announced =
            researchNotice(NoticeKind::ProgramResearchStarted, command.company, command.tech);
        announced.tonnes = *stage;
        report.notices.push_back(announced);
    }
}

void runResearch(const Content& content, WorldState& state, TickReport& report) {
    for (Research& research : state.research) {
        if (research.complete != 0 || research.completesDay > state.day) {
            continue;
        }
        research.complete = 1;
        Company& company = state.companies[research.company.index()];
        if (!techCompleted(company, research.tech)) {
            company.completedTechs.push_back(research.tech);
        }
        report.notices.push_back(
            researchNotice(NoticeKind::ResearchComplete, research.company, research.tech));
        // What a company can newly mine is public news (spec 004): once per
        // resource the finished tech unlocks, in resource order.
        for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
            if (content.resources[r].unlockedBy == research.tech) {
                Notice unlocked = researchNotice(NoticeKind::RivalUnlockedResource,
                                                 research.company, research.tech);
                unlocked.resource = ResourceId(r);
                report.notices.push_back(unlocked);
            }
        }
    }
}

}  // namespace sim
