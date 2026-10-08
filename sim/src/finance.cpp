#include "sim/finance.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/tech.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <cstdint>
#include <optional>

namespace sim {

namespace {

Money ceilDiv(Money amount, std::int64_t divisor) {
    const std::int64_t cents = amount.cents();
    return Money::fromCents((cents + divisor - 1) / divisor);
}

Notice loanNotice(NoticeKind kind, CompanyId company, LoanId loan, Money amount = Money::zero()) {
    Notice notice = makeNotice(kind, company, PlotId(), amount);
    notice.loan = loan;
    return notice;
}

void chargeStorage(const Content& content, WorldState& state, CompanyId companyId,
                   TickReport& report) {
    const Money cost = monthlyStorageCost(content, state, companyId);
    if (cost == Money::zero()) {
        return;
    }
    state.companies[companyId.index()].cash -= cost;
    report.notices.push_back(makeNotice(NoticeKind::StorageCharged, companyId, PlotId(), cost));
}

void payLoans(const Content& content, WorldState& state, CompanyId companyId, TickReport& report) {
    const Fixed monthlyRate =
        content.balance.loanAnnualRate / static_cast<std::int64_t>(kMonthsPerYear);
    for (std::uint32_t i = 0; i < state.loans.size(); ++i) {
        Loan& loan = state.loans[i];
        if (loan.active == 0 || loan.company != companyId) {
            continue;
        }
        const Money interest = loan.remaining.scaled(monthlyRate);
        const Money principalPart =
            loan.principalPerMonth < loan.remaining ? loan.principalPerMonth : loan.remaining;
        loan.remaining -= principalPart;
        if (loan.monthsRemaining > 0) {
            --loan.monthsRemaining;
        }
        state.companies[companyId.index()].cash -= interest + principalPart;
        report.notices.push_back(
            loanNotice(NoticeKind::LoanPayment, companyId, LoanId(i), interest + principalPart));
        if (loan.remaining == Money::zero()) {
            loan.active = 0;
            loan.monthsRemaining = 0;
            report.notices.push_back(loanNotice(NoticeKind::LoanRepaid, companyId, LoanId(i)));
        }
    }
}

void declareBankrupt(WorldState& state, CompanyId companyId, TickReport& report) {
    Company& company = state.companies[companyId.index()];
    company.bankrupt = 1;
    for (Mine& mine : state.mines) {
        if (mine.owner == companyId) {
            mine.state = MineState::Closed;
        }
    }
    for (Loan& loan : state.loans) {
        if (loan.company == companyId) {
            loan.active = 0;  // written off; the company is finished anyway
        }
    }
    report.notices.push_back(makeNotice(NoticeKind::Bankrupt, companyId, PlotId()));
}

void checkSolvency(const Content& content, WorldState& state, CompanyId companyId,
                   TickReport& report) {
    Company& company = state.companies[companyId.index()];
    if (company.cash >= Money::zero()) {
        company.monthsInsolvent = 0;
        return;
    }
    ++company.monthsInsolvent;
    const std::uint32_t grace = content.balance.bankruptcyGraceMonths;
    if (company.monthsInsolvent > grace) {
        declareBankrupt(state, companyId, report);
        return;
    }
    Notice warning = makeNotice(NoticeKind::InsolvencyWarning, companyId, PlotId(), company.cash);
    warning.tonnes = static_cast<std::int64_t>(grace + 1 - company.monthsInsolvent);
    report.notices.push_back(warning);
}

}  // namespace

Money outstandingPrincipal(const WorldState& state, CompanyId company) {
    Money total = Money::zero();
    for (const Loan& loan : state.loans) {
        if (loan.active != 0 && loan.company == company) {
            total += loan.remaining;
        }
    }
    return total;
}

Money monthlyStorageCost(const Content& content, const WorldState& state, CompanyId companyId) {
    const Company& company = state.companies[companyId.index()];
    const Fixed mult = techMultiplier(content, company, TechEffectType::StorageCostMult);
    Money total = Money::zero();
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        const std::int64_t tonnes = company.inventoryTonnes[r];
        if (tonnes == 0) {
            continue;
        }
        // Checked: tonnes <= 1e18 and cost <= 1e13 cents could overflow.
        const std::int64_t cents =
            checkedMulDiv(content.resources[r].storageCostPerTonneMonth.cents(), tonnes, 1)
                .value_or(kMaxCashCents);
        total += Money::fromCents(cents < kMaxCashCents ? cents : kMaxCashCents).scaled(mult);
    }
    return total;
}

std::optional<RejectReason> validateTakeLoan(const Content& content, const WorldState& state,
                                             const TakeLoan& command) {
    if (!command.company.valid() || command.company.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    const Company& company = state.companies[command.company.index()];
    if (company.bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (command.principal <= Money::zero()) {
        return RejectReason::InvalidAmount;
    }
    const Money capacity =
        content.balance.loanMaxPrincipal - outstandingPrincipal(state, command.company);
    if (command.principal > capacity) {
        return RejectReason::LoanLimitExceeded;
    }
    return std::nullopt;
}

void applyTakeLoan(const Content& content, WorldState& state, const TakeLoan& command,
                   TickReport& report) {
    Loan loan;
    loan.company = command.company;
    loan.principal = command.principal;
    loan.remaining = command.principal;
    loan.principalPerMonth =
        ceilDiv(command.principal, static_cast<std::int64_t>(content.balance.loanTermMonths));
    loan.takenDay = state.day;
    loan.monthsRemaining = content.balance.loanTermMonths;
    state.loans.push_back(loan);
    state.companies[command.company.index()].cash += command.principal;
    const LoanId id(static_cast<std::uint32_t>(state.loans.size() - 1));
    report.notices.push_back(
        loanNotice(NoticeKind::LoanTaken, command.company, id, command.principal));
}

std::optional<RejectReason> validateRepayLoan(const WorldState& state, const RepayLoan& command) {
    if (!command.company.valid() || command.company.index() >= state.companies.size()) {
        return RejectReason::UnknownCompany;
    }
    if (state.companies[command.company.index()].bankrupt != 0) {
        return RejectReason::CompanyBankrupt;
    }
    if (!command.loan.valid() || command.loan.index() >= state.loans.size()) {
        return RejectReason::UnknownLoan;
    }
    const Loan& loan = state.loans[command.loan.index()];
    if (loan.company != command.company) {
        return RejectReason::NotLoanHolder;
    }
    if (loan.active == 0) {
        return RejectReason::LoanNotActive;
    }
    if (command.amount <= Money::zero() || command.amount > loan.remaining) {
        return RejectReason::InvalidAmount;
    }
    if (command.amount > availableCash(state, command.company)) {
        return RejectReason::InsufficientFunds;
    }
    return std::nullopt;
}

void applyRepayLoan(WorldState& state, const RepayLoan& command, TickReport& report) {
    Loan& loan = state.loans[command.loan.index()];
    loan.remaining -= command.amount;
    state.companies[command.company.index()].cash -= command.amount;
    report.notices.push_back(
        loanNotice(NoticeKind::LoanPayment, command.company, command.loan, command.amount));
    if (loan.remaining == Money::zero()) {
        loan.active = 0;
        loan.monthsRemaining = 0;
        report.notices.push_back(loanNotice(NoticeKind::LoanRepaid, command.company, command.loan));
    }
}

void runFinance(const Content& content, WorldState& state, TickReport& report) {
    if (state.day == 0 || state.day % kDaysPerMonth != 0) {
        return;
    }
    for (std::uint32_t i = 0; i < state.companies.size(); ++i) {
        const CompanyId companyId(i);
        if (state.companies[i].bankrupt != 0) {
            continue;
        }
        chargeStorage(content, state, companyId, report);
        payLoans(content, state, companyId, report);
        checkSolvency(content, state, companyId, report);
    }
}

}  // namespace sim
