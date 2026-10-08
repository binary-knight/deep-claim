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

enum class RejectReason : std::uint8_t {
    None,
    UnknownCompany,
    CompanyBankrupt,
    UnknownPlot,
    PlotNotForSale,
    BidBelowAsk,
    InsufficientFunds,
};

enum class NoticeKind : std::uint8_t {
    CommandRejected,  // company, plot, reason
    PlotListed,       // plot, amount = ask price
    PlotRelisted,     // plot, amount = reduced ask price
    AuctionWon,       // company, plot, amount paid
    AuctionLost,      // company (a bidder who lost), plot
};

struct Notice {
    NoticeKind kind = NoticeKind::CommandRejected;
    CompanyId company;
    PlotId plot;
    Money amount;
    RejectReason reason = RejectReason::None;
};

[[nodiscard]] inline Notice makeNotice(NoticeKind kind, CompanyId company, PlotId plot,
                                       Money amount = Money::zero(),
                                       RejectReason reason = RejectReason::None) {
    Notice notice;
    notice.kind = kind;
    notice.company = company;
    notice.plot = plot;
    notice.amount = amount;
    notice.reason = reason;
    return notice;
}

// Everything that happened during one tick, for UIs and logs. Transient:
// not part of WorldState.
struct TickReport {
    Day day = 0;
    std::vector<Notice> notices;
};

}  // namespace sim
