#pragma once

// Player and AI actions enter the sim only as commands. Commands are queued
// with Simulation::submit and applied at the start of the next tick in
// (company id, submission order), so a replay is the seed plus the command
// log. Validation failures become CommandRejected notices, never errors.

#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace sim {

struct BidOnPlot {
    CompanyId company;
    PlotId plot;
    Money amount;  // must be at least the plot's current ask price
};

using Command = std::variant<BidOnPlot>;

[[nodiscard]] CompanyId commandCompany(const Command& command);
void setCommandCompany(Command& command, CompanyId company);

enum class RejectReason : std::uint8_t {
    None,
    UnknownCompany,
    CompanyBankrupt,
    UnknownPlot,
    PlotNotForSale,
    BidBelowAsk,
    InsufficientFunds,
};

// Private notices are visible only to `company`; public ones to everyone.
enum class NoticeKind : std::uint8_t {
    CommandRejected,  // private: company, plot, amount, reason
    PlotListed,       // public: plot, auction, amount = ask price
    PlotRelisted,     // public: plot, auction (the new one), amount = new ask
    PlotSold,         // public: company (buyer), plot, auction, amount paid
    AuctionLost,      // private: company was outbid on plot/auction
    BidForfeited,     // private: company could no longer pay its bid at close
};

struct Notice {
    NoticeKind kind = NoticeKind::CommandRejected;
    CompanyId company;
    PlotId plot;
    AuctionId auction;
    Money amount;
    RejectReason reason = RejectReason::None;
};

[[nodiscard]] bool noticeIsPublic(NoticeKind kind);
[[nodiscard]] bool noticeVisibleTo(const Notice& notice, CompanyId viewer);

[[nodiscard]] inline Notice makeNotice(NoticeKind kind, CompanyId company, PlotId plot,
                                       AuctionId auction, Money amount = Money::zero(),
                                       RejectReason reason = RejectReason::None) {
    Notice notice;
    notice.kind = kind;
    notice.company = company;
    notice.plot = plot;
    notice.auction = auction;
    notice.amount = amount;
    notice.reason = reason;
    return notice;
}

// Everything that happened during one tick, for UIs and logs. Transient:
// not part of WorldState. Hosts must filter it per viewer with
// Simulation::reportFor; the unfiltered report is for tests and tooling.
struct TickReport {
    Day day = 0;
    std::vector<Notice> notices;
};

}  // namespace sim
