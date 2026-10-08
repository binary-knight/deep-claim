#include "sim/commands.h"

#include <variant>

namespace sim {

CompanyId commandCompany(const Command& command) {
    return std::visit([](const auto& concrete) { return concrete.company; }, command);
}

void setCommandCompany(Command& command, CompanyId company) {
    std::visit([company](auto& concrete) { concrete.company = company; }, command);
}

bool noticeIsPublic(NoticeKind kind) {
    switch (kind) {
        case NoticeKind::PlotListed:
        case NoticeKind::PlotRelisted:
        case NoticeKind::PlotSold:
        case NoticeKind::MineStarted:
        case NoticeKind::Bankrupt:
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
            return false;
    }
    return false;
}

bool noticeVisibleTo(const Notice& notice, CompanyId viewer) {
    return noticeIsPublic(notice.kind) || notice.company == viewer;
}

}  // namespace sim
