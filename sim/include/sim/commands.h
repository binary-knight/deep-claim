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

// Any plot not owned by another company may be surveyed; the cost is paid
// up front and results arrive after the survey duration.
struct CommissionSurvey {
    CompanyId company;
    PlotId plot;
};

// One mine per plot, on a plot the company owns. Capex is paid up front.
struct BuildMine {
    CompanyId company;
    PlotId plot;
};

// Switch an Operating mine to Mothballed (reduced opex, no output) or back.
struct SetMineState {
    CompanyId company;
    MineId mine;
    std::uint8_t operate = 1;  // 1 = Operating, 0 = Mothballed
};

using Command = std::variant<BidOnPlot, CommissionSurvey, BuildMine, SetMineState>;

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
    PlotOwnedByOther,
    NotPlotOwner,
    SurveyInProgress,
    MineExists,
    UnknownMine,
    NotMineOwner,
    MineNotSwitchable,
};

// Private notices are visible only to `company`; public ones to everyone.
enum class NoticeKind : std::uint8_t {
    CommandRejected,  // private: company, plot/mine, amount, reason
    PlotListed,       // public: plot, auction, amount = ask price
    PlotRelisted,     // public: plot, auction (the new one), amount = new ask
    PlotSold,         // public: company (buyer), plot, auction, amount paid
    AuctionLost,      // private: company was outbid on plot/auction
    BidForfeited,     // private: company could no longer pay its bid at close
    SurveyStarted,    // private: company, plot, amount = cost
    SurveyComplete,   // private: company, plot
    MineStarted,      // public: company, plot, mine, amount = capex
    MineOperational,  // private: company, plot, mine
    MineDepleted,     // private: company, plot, mine
    MineSwitched,     // private: company, plot, mine (state changed)
};

struct Notice {
    NoticeKind kind = NoticeKind::CommandRejected;
    CompanyId company;
    PlotId plot;
    AuctionId auction;
    MineId mine;
    Money amount;
    RejectReason reason = RejectReason::None;
};

[[nodiscard]] bool noticeIsPublic(NoticeKind kind);
[[nodiscard]] bool noticeVisibleTo(const Notice& notice, CompanyId viewer);

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
// not part of WorldState. Hosts must filter it per viewer with
// Simulation::reportFor; the unfiltered report is for tests and tooling.
struct TickReport {
    Day day = 0;
    std::vector<Notice> notices;
};

}  // namespace sim
