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

std::optional<RejectReason> validateStartResearch(const Content& content, const WorldState& state,
                                                  const StartResearch& command) {
    if (!command.company.valid() || command.company.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    const Company& company = state.companies[command.company.index()];
    if (company.bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (!command.tech.valid() || command.tech.index() >= content.techs.size()) {
        return RejectReason::UnknownTech;
    }
    if (techCompleted(company, command.tech)) {
        return RejectReason::TechAlreadyKnown;
    }
    if (activeResearch(state, command.company)) {
        return RejectReason::ResearchBusy;
    }
    if (!prerequisitesMet(content, company, command.tech)) {
        return RejectReason::PrerequisitesMissing;
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
}

void runResearch(WorldState& state, TickReport& report) {
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
    }
}

}  // namespace sim
