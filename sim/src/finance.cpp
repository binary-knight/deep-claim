#include "sim/finance.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/markets.h"
#include "sim/mines.h"
#include "sim/money.h"
#include "sim/plot_market.h"
#include "sim/tech.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace sim {

namespace {

Money ceilDiv(Money amount, std::int64_t divisor) {
    const std::int64_t cents = amount.cents();
    return Money::fromCents((cents + divisor - 1) / divisor);
}

std::int64_t saturatingAddCents(std::int64_t a, std::int64_t b) {
    return b > kMaxCashCents - a ? kMaxCashCents : a + b;
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
    Company& company = state.companies[companyId.index()];
    company.cash = debitCash(company.cash, cost);
    report.notices.push_back(makeNotice(NoticeKind::StorageCharged, companyId, PlotId(), cost));
}

Notice saleNotice(NoticeKind kind, CompanyId company, ResourceId resource, std::int64_t tonnes,
                  Money amount) {
    Notice notice = makeNotice(kind, company, PlotId(), amount);
    notice.resource = resource;
    notice.tonnes = tonnes;
    return notice;
}

// Spec 011 (BR-011): tonnes of each good the current stage still needs, its
// requirement less what has been delivered; none once the program is
// complete.
std::vector<std::int64_t> stageStillNeeds(const Content& content, const Company& company) {
    std::vector<std::int64_t> need(company.inventoryTonnes.size(), 0);
    const std::vector<ProgramStage>& stages = content.program.stages;
    if (company.programStage >= stages.size()) {
        return need;
    }
    for (const ProgramRequirement& requirement : stages[company.programStage].requirements) {
        const std::size_t r = requirement.resource.index();
        need[r] = std::max<std::int64_t>(0, requirement.tonnes - company.programDelivered[r]);
    }
    return need;
}

// The two tiers cover costs sells in turn: the stock above the stage's need,
// then the stock the stage still needs.
enum class CoverTier : std::uint8_t { Free, Needed };

std::int64_t tierTonnes(CoverTier tier, std::int64_t stock, std::int64_t need) {
    const std::int64_t free = std::max<std::int64_t>(0, stock - need);
    return tier == CoverTier::Free ? free : stock - free;
}

// One pass of cover costs over a tier: just enough to reach zero, by the
// value of the tier's tonnes descending; ties keep the lower id first.
void sellTier(const std::vector<std::uint32_t>& eligible, const std::vector<std::int64_t>& need,
              CoverTier tier, WorldState& state, CompanyId companyId, TickReport& report) {
    Company& company = state.companies[companyId.index()];
    std::vector<std::pair<std::uint32_t, std::int64_t>> order;  // resource, tier tonnes
    for (const std::uint32_t r : eligible) {
        const std::int64_t tonnes = tierTonnes(tier, company.inventoryTonnes[r], need[r]);
        if (tonnes > 0) {
            order.emplace_back(r, tonnes);
        }
    }
    // Exact comparison: price times tonnes can exceed 64 bits.
    std::stable_sort(order.begin(), order.end(), [&](const auto& a, const auto& b) {
        return compareProducts(state.markets[a.first].price.cents(), a.second,
                               state.markets[b.first].price.cents(), b.second) > 0;
    });
    for (const auto& [r, inTier] : order) {
        if (company.cash >= Money::zero()) {
            break;
        }
        const std::int64_t price = state.markets[r].price.cents();
        if (price <= 0) {
            continue;
        }
        const std::int64_t shortfall = -company.cash.cents();
        const std::int64_t wanted = std::min((shortfall + price - 1) / price, inTier);
        const std::int64_t tonnes = saleTonnesWithinBounds(state, companyId, ResourceId(r), wanted);
        if (tonnes <= 0) {
            continue;
        }
        // After the market stage: the sale must not touch today's supply.
        const Money proceeds = executeSale(state, companyId, ResourceId(r), tonnes, false);
        recordAutoSale(company, ResourceId(r), tonnes, proceeds);
        report.notices.push_back(
            saleNotice(NoticeKind::CoverCostsSale, companyId, ResourceId(r), tonnes, proceeds));
    }
}

// Spec 001: with the flag set and cash below zero after the month's
// charges, sell just enough stock to reach zero, most valuable stock first.
// Spec 011 (BR-011): of each good, the stock above what the current stage
// still needs goes before any of the stock it needs. A good sold in both
// tiers gets a sale notice for each.
void coverCosts(const Content& content, WorldState& state, CompanyId companyId,
                TickReport& report) {
    const Company& company = state.companies[companyId.index()];
    if (company.coverCosts == 0) {
        return;
    }
    // Unlocked resources with stock.
    std::vector<std::uint32_t> eligible;
    for (std::uint32_t r = 0; r < state.markets.size(); ++r) {
        if (company.inventoryTonnes[r] > 0 && resourceUnlocked(content, company, ResourceId(r))) {
            eligible.push_back(r);
        }
    }
    const std::vector<std::int64_t> need = stageStillNeeds(content, company);
    sellTier(eligible, need, CoverTier::Free, state, companyId, report);
    sellTier(eligible, need, CoverTier::Needed, state, companyId, report);
}

// Spec 001: one log line per resource the policies sold this month.
void summarizeAutoSales(WorldState& state, CompanyId companyId, TickReport& report) {
    Company& company = state.companies[companyId.index()];
    for (std::uint32_t r = 0; r < state.markets.size(); ++r) {
        if (company.autoSoldTonnes[r] == 0 && company.autoSoldProceeds[r] == Money::zero()) {
            continue;
        }
        report.notices.push_back(saleNotice(NoticeKind::AutoSaleSummary, companyId, ResourceId(r),
                                            company.autoSoldTonnes[r],
                                            company.autoSoldProceeds[r]));
        company.autoSoldTonnes[r] = 0;
        company.autoSoldProceeds[r] = Money::zero();
    }
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
        const Money principalPart = std::min(loan.principalPerMonth, loan.remaining);
        loan.remaining -= principalPart;
        if (loan.monthsRemaining > 0) {
            --loan.monthsRemaining;
        }
        Company& company = state.companies[companyId.index()];
        company.cash = debitCash(company.cash, interest + principalPart);
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
            loan.monthsRemaining = 0;
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

Money debitCash(Money cash, Money charge) {
    // Both operands are inside the persisted bound, so the subtraction cannot
    // overflow; only the result needs clamping.
    const Money result = cash - charge;
    return result < Money::fromCents(-kMaxCashCents) ? Money::fromCents(-kMaxCashCents) : result;
}

Money outstandingPrincipal(const WorldState& state, CompanyId company) {
    Money total = Money::zero();
    for (const Loan& loan : state.loans) {
        if (loan.active != 0 && loan.company == company) {
            total += loan.remaining;
        }
    }
    return total;
}

std::uint32_t activeLoanCount(const WorldState& state, CompanyId company) {
    std::uint32_t count = 0;
    for (const Loan& loan : state.loans) {
        if (loan.active != 0 && loan.company == company) {
            ++count;
        }
    }
    return count;
}

Money monthlyStorageCost(const Content& content, const WorldState& state, CompanyId companyId) {
    const Company& company = state.companies[companyId.index()];
    const Fixed mult = techMultiplier(content, company, TechEffectType::StorageCostMult);
    std::int64_t totalCents = 0;
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        const std::int64_t tonnes = company.inventoryTonnes[r];
        if (tonnes == 0) {
            continue;
        }
        // Every step is checked and saturates at the cash bound: tonnes reach
        // 1e18 and per-tonne costs 1e13 cents in valid content.
        const std::int64_t base =
            checkedMulDiv(content.resources[r].storageCostPerTonneMonth.cents(), tonnes, 1)
                .value_or(kMaxCashCents);
        const std::int64_t scaled =
            checkedMulDiv(std::min(base, kMaxCashCents), mult.raw(), Fixed::kScale)
                .value_or(kMaxCashCents);
        totalCents = saturatingAddCents(totalCents, std::min(scaled, kMaxCashCents));
    }
    return Money::fromCents(totalCents);
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
    if (command.principal < content.balance.loanMinPrincipal) {
        return RejectReason::LoanTooSmall;
    }
    const Money capacity =
        content.balance.loanMaxPrincipal - outstandingPrincipal(state, command.company);
    if (command.principal > capacity) {
        return RejectReason::LoanLimitExceeded;
    }
    if (activeLoanCount(state, command.company) >= content.balance.maxActiveLoans) {
        return RejectReason::TooManyLoans;
    }
    if (command.principal.cents() > kMaxCashCents - company.cash.cents()) {
        return RejectReason::CashLimitExceeded;
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
        coverCosts(content, state, companyId, report);
        summarizeAutoSales(state, companyId, report);
        checkSolvency(content, state, companyId, report);
    }
    warnIdleMines(content, state, report);
    warnRunningLow(content, state, report);
}

}  // namespace sim
