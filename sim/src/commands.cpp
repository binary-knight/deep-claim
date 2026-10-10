#include "sim/commands.h"

#include <variant>

namespace sim {

CompanyId commandCompany(const Command& command) {
    return std::visit([](const auto& concrete) { return concrete.company; }, command);
}

void setCommandCompany(Command& command, CompanyId company) {
    std::visit([company](auto& concrete) { concrete.company = company; }, command);
}

const char* rejectReasonName(RejectReason reason) {
    switch (reason) {
        case RejectReason::None:
            return "none";
        case RejectReason::UnknownCompany:
            return "unknown_company";
        case RejectReason::CompanyBankrupt:
            return "company_bankrupt";
        case RejectReason::UnknownPlot:
            return "unknown_plot";
        case RejectReason::PlotNotForSale:
            return "plot_not_for_sale";
        case RejectReason::BidBelowAsk:
            return "bid_below_ask";
        case RejectReason::InsufficientFunds:
            return "insufficient_funds";
        case RejectReason::PlotOwnedByOther:
            return "plot_owned_by_other";
        case RejectReason::NotPlotOwner:
            return "not_plot_owner";
        case RejectReason::SurveyInProgress:
            return "survey_in_progress";
        case RejectReason::MineExists:
            return "mine_exists";
        case RejectReason::UnknownMine:
            return "unknown_mine";
        case RejectReason::NotMineOwner:
            return "not_mine_owner";
        case RejectReason::MineNotSwitchable:
            return "mine_not_switchable";
        case RejectReason::UnknownResource:
            return "unknown_resource";
        case RejectReason::InvalidQuantity:
            return "invalid_quantity";
        case RejectReason::InsufficientInventory:
            return "insufficient_inventory";
        case RejectReason::SaleTooLarge:
            return "sale_too_large";
        case RejectReason::InvalidAmount:
            return "invalid_amount";
        case RejectReason::LoanLimitExceeded:
            return "loan_limit_exceeded";
        case RejectReason::LoanTooSmall:
            return "loan_too_small";
        case RejectReason::TooManyLoans:
            return "too_many_loans";
        case RejectReason::CashLimitExceeded:
            return "cash_limit_exceeded";
        case RejectReason::UnknownLoan:
            return "unknown_loan";
        case RejectReason::NotLoanHolder:
            return "not_loan_holder";
        case RejectReason::LoanNotActive:
            return "loan_not_active";
        case RejectReason::UnknownTech:
            return "unknown_tech";
        case RejectReason::TechAlreadyKnown:
            return "tech_already_known";
        case RejectReason::ResearchBusy:
            return "research_busy";
        case RejectReason::PrerequisitesMissing:
            return "prerequisites_missing";
    }
    return "unknown";
}

const char* noticeKindName(NoticeKind kind) {
    switch (kind) {
        case NoticeKind::CommandRejected:
            return "command_rejected";
        case NoticeKind::PlotListed:
            return "plot_listed";
        case NoticeKind::PlotRelisted:
            return "plot_relisted";
        case NoticeKind::PlotSold:
            return "plot_sold";
        case NoticeKind::AuctionLost:
            return "auction_lost";
        case NoticeKind::BidForfeited:
            return "bid_forfeited";
        case NoticeKind::SurveyStarted:
            return "survey_started";
        case NoticeKind::SurveyComplete:
            return "survey_complete";
        case NoticeKind::MineStarted:
            return "mine_started";
        case NoticeKind::MineOperational:
            return "mine_operational";
        case NoticeKind::MineDepleted:
            return "mine_depleted";
        case NoticeKind::MineSwitched:
            return "mine_switched";
        case NoticeKind::OreSold:
            return "ore_sold";
        case NoticeKind::StorageCharged:
            return "storage_charged";
        case NoticeKind::LoanTaken:
            return "loan_taken";
        case NoticeKind::LoanPayment:
            return "loan_payment";
        case NoticeKind::LoanRepaid:
            return "loan_repaid";
        case NoticeKind::InsolvencyWarning:
            return "insolvency_warning";
        case NoticeKind::Bankrupt:
            return "bankrupt";
        case NoticeKind::ResearchStarted:
            return "research_started";
        case NoticeKind::ResearchComplete:
            return "research_complete";
        case NoticeKind::EventStarted:
            return "event_started";
        case NoticeKind::EventEnded:
            return "event_ended";
        case NoticeKind::CoverCostsSale:
            return "cover_costs_sale";
        case NoticeKind::AutoSaleSummary:
            return "auto_sale_summary";
        case NoticeKind::IdleMineWarning:
            return "idle_mine_warning";
        case NoticeKind::MineFoundNothing:
            return "mine_found_nothing";
        case NoticeKind::MineRunningLow:
            return "mine_running_low";
        case NoticeKind::PlotWithdrawn:
            return "plot_withdrawn";
        case NoticeKind::RivalUnlockedResource:
            return "rival_unlocked_resource";
        case NoticeKind::MineLosingMoney:
            return "mine_losing_money";
    }
    return "unknown";
}

bool noticeIsPublic(NoticeKind kind) {
    switch (kind) {
        case NoticeKind::PlotListed:
        case NoticeKind::PlotRelisted:
        case NoticeKind::PlotSold:
        case NoticeKind::MineStarted:
        case NoticeKind::Bankrupt:
        case NoticeKind::EventStarted:
        case NoticeKind::EventEnded:
        case NoticeKind::PlotWithdrawn:
        case NoticeKind::RivalUnlockedResource:
            return true;
        case NoticeKind::CommandRejected:
        case NoticeKind::AuctionLost:
        case NoticeKind::BidForfeited:
        case NoticeKind::SurveyStarted:
        case NoticeKind::SurveyComplete:
        case NoticeKind::MineOperational:
        case NoticeKind::MineDepleted:
        case NoticeKind::MineSwitched:
        case NoticeKind::OreSold:
        case NoticeKind::StorageCharged:
        case NoticeKind::LoanTaken:
        case NoticeKind::LoanPayment:
        case NoticeKind::LoanRepaid:
        case NoticeKind::InsolvencyWarning:
        case NoticeKind::ResearchStarted:
        case NoticeKind::ResearchComplete:
        case NoticeKind::CoverCostsSale:
        case NoticeKind::AutoSaleSummary:
        case NoticeKind::IdleMineWarning:
        case NoticeKind::MineFoundNothing:
        case NoticeKind::MineRunningLow:
        case NoticeKind::MineLosingMoney:
            return false;
    }
    return false;
}

bool noticeVisibleTo(const Notice& notice, CompanyId viewer) {
    return noticeIsPublic(notice.kind) || notice.company == viewer;
}

}  // namespace sim
