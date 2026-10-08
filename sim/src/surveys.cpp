#include "sim/surveys.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/math.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/random.h"
#include "sim/tech.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <cstdint>
#include <optional>

namespace sim {

namespace {

// Noise draws are clamped so one reading cannot be absurd, and the log-space
// shift is clamped inside fixedExp's domain.
const Fixed kMaxNoiseSigma = Fixed::fromInt(3);
const Fixed kMaxShift = Fixed::fromInt(20);

}  // namespace

Fixed surveyNoise(const Content& content, const Company& company) {
    return content.balance.surveyBaseNoise *
           techMultiplier(content, company, TechEffectType::SurveyNoiseMult);
}

std::optional<RejectReason> validateSurvey(const Content& content, const WorldState& state,
                                           const CommissionSurvey& command) {
    if (!command.company.valid() || command.company.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    if (state.companies[command.company.index()].bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (!command.plot.valid() || command.plot.index() >= state.plots.size()) {
        return RejectReason::UnknownPlot;
    }
    const Plot& plot = state.plots[command.plot.index()];
    if (plot.owner.valid() && plot.owner != command.company) {
        return RejectReason::PlotOwnedByOther;
    }
    for (const Survey& survey : state.surveys) {
        if (survey.complete == 0 && survey.company == command.company &&
            survey.plot == command.plot) {
            return RejectReason::SurveyInProgress;
        }
    }
    if (content.balance.surveyCost > availableCash(state, command.company)) {
        return RejectReason::InsufficientFunds;
    }
    return std::nullopt;
}

void applySurvey(const Content& content, WorldState& state, const CommissionSurvey& command,
                 TickReport& report) {
    state.companies[command.company.index()].cash -= content.balance.surveyCost;
    Survey survey;
    survey.company = command.company;
    survey.plot = command.plot;
    survey.commissionedDay = state.day;
    survey.completesDay = state.day + content.balance.surveyDurationDays;  // <= kMaxDeadline
    state.surveys.push_back(survey);
    report.notices.push_back(makeNotice(NoticeKind::SurveyStarted, command.company, command.plot,
                                        content.balance.surveyCost));
}

void runSurveys(const Content& content, WorldState& state, TickReport& report) {
    for (Survey& survey : state.surveys) {
        if (survey.complete != 0 || survey.completesDay > state.day) {
            continue;
        }
        const Fixed sigma = surveyNoise(content, state.companies[survey.company.index()]);
        for (const DepositTruth& truth : state.hidden.plotDeposits[survey.plot.index()].deposits) {
            // Reading = remaining tonnes * exp(sigma * z): unbiased in log space.
            const Fixed z =
                clampFixed(approxNormal(state.rng.survey), -kMaxNoiseSigma, kMaxNoiseSigma);
            const Fixed shift = clampFixed(sigma * z, -kMaxShift, kMaxShift);
            std::int64_t estimate = 0;
            if (shift >= Fixed::zero()) {
                estimate =
                    checkedMulDiv(truth.tonnesRemaining, fixedExp(shift).raw(), Fixed::kScale)
                        .value_or(kMaxDepositTonnes);
            } else {
                estimate = mulDiv(truth.tonnesRemaining, Fixed::kScale, fixedExp(-shift).raw());
            }
            if (estimate > kMaxDepositTonnes) {
                estimate = kMaxDepositTonnes;
            }
            SurveyEstimate reading;
            reading.resource = truth.resource;
            reading.tonnesEstimate = estimate;
            reading.relativeError = sigma;
            survey.estimates.push_back(reading);
        }
        survey.complete = 1;
        report.notices.push_back(
            makeNotice(NoticeKind::SurveyComplete, survey.company, survey.plot));
    }
}

}  // namespace sim
