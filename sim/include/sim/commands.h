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

// Sell stored ore at today's market price.
struct SellOre {
    CompanyId company;
    ResourceId resource;
    std::int64_t tonnes = 0;
};

// Borrow up to the remaining loan capacity; repaid in equal monthly
// principal instalments plus interest on the balance.
struct TakeLoan {
    CompanyId company;
    Money principal;
};

// Pay down a loan's principal early.
struct RepayLoan {
    CompanyId company;
    LoanId loan;
    Money amount;
};

// Begin researching a tech whose prerequisites are complete. One project at
// a time per company; the cost is paid up front.
struct StartResearch {
    CompanyId company;
    TechId tech;
};

// Standing sell order for one resource (spec 001): every day, when the
// price is at or above the floor, sell everything above the reserve.
struct SetSellPolicy {
    CompanyId company;
    ResourceId resource;
    std::uint8_t enabled = 0;
    std::int64_t reserveTonnes = 0;
    Money floorPrice;
};

// Month-end safety rule (spec 001): sell just enough stock to bring cash
// back to zero after storage and loan charges.
struct SetCoverCosts {
    CompanyId company;
    std::uint8_t enabled = 0;
};

// Move stock to the company's program, toward the stage it is working on
// (spec 010): at most what that stage still needs of the good. Not a sale,
// and it cannot be undone.
struct DeliverToProgram {
    CompanyId company;
    ResourceId resource;
    std::int64_t tonnes = 0;
};

// Complete the program stage the company is working on: its research done,
// every delivery met, and the stage's cost paid now (spec 010).
struct CompleteProgramStage {
    CompanyId company;
};

// Saves write a command as its position here, so new orders go at the end
// and nothing already listed moves.
using Command = std::variant<BidOnPlot, CommissionSurvey, BuildMine, SetMineState, SellOre,
                             TakeLoan, RepayLoan, StartResearch, SetSellPolicy, SetCoverCosts,
                             DeliverToProgram, CompleteProgramStage>;

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
    UnknownResource,
    InvalidQuantity,
    InsufficientInventory,
    SaleTooLarge,
    InvalidAmount,
    LoanLimitExceeded,
    LoanTooSmall,
    TooManyLoans,
    CashLimitExceeded,
    UnknownLoan,
    NotLoanHolder,
    LoanNotActive,
    UnknownTech,
    TechAlreadyKnown,
    ResearchBusy,
    PrerequisitesMissing,
    ProgramNotNeeded,      // the current stage does not need that good, or not that much more
    ProgramStageNotReady,  // the stage's research or a delivery is outstanding
    ProgramComplete,       // the company has completed every stage
};

// Private notices are visible only to `company`; public ones to everyone.
enum class NoticeKind : std::uint8_t {
    CommandRejected,    // private: company, plot/mine, amount, reason
    PlotListed,         // public: plot, auction, amount = ask price
    PlotRelisted,       // public: plot, auction (the new one), amount = new ask
    PlotSold,           // public: company (buyer), plot, auction, amount paid
    AuctionLost,        // private: company was outbid on plot/auction
    BidForfeited,       // private: company could no longer pay its bid at close
    SurveyStarted,      // private: company, plot, amount = cost
    SurveyComplete,     // private: company, plot
    MineStarted,        // public: company, plot, mine, amount = capex
    MineOperational,    // private: company, plot, mine
    MineDepleted,       // private: company, plot, mine
    MineSwitched,       // private: company, plot, mine (state changed)
    OreSold,            // private: company, resource, tonnes, amount = proceeds
    StorageCharged,     // private: company, amount
    LoanTaken,          // private: company, loan, amount = principal
    LoanPayment,        // private: company, loan, amount = interest + principal paid
    LoanRepaid,         // private: company, loan (fully paid off)
    InsolvencyWarning,  // private: company, amount = cash (negative), tonnes = months left
    Bankrupt,           // public: company
    ResearchStarted,    // private: company, tech, amount = cost
    ResearchComplete,   // private: company, tech
    EventStarted,       // public: event
    EventEnded,         // public: event
    CoverCostsSale,     // private: company, resource, tonnes, amount = proceeds (month end)
    AutoSaleSummary,    // private: company, resource, tonnes, amount sold by policy this month
    IdleMineWarning,    // private: company, plot, mine (month end; operating, nothing extractable)
    MineFoundNothing,   // private: company, plot, mine (opened on a plot with nothing extractable)
    MineRunningLow,     // private: company, plot, mine, tonnes = days of ore left by the low
                        // end of the owner's survey at the current output (month end, once,
                        // when 30 days or fewer remain)
    PlotWithdrawn,      // public: plot, amount = the ask it left with (unsold too many times)
    RivalUnlockedResource,  // public: company, tech, resource (research unlocked a resource)
    MineLosingMoney,        // private: company, plot, mine, amount = the day's loss (positive),
                            // tonnes = consecutive losing days (once per run of losing days)
    ProgramDelivered,       // private: company, resource, tonnes moved from stock to the program
    ProgramStageComplete,   // public: company, amount = the stage's cost, tonnes = the stage
                            // just completed (1 for the first), tech = that stage's research
};

struct Notice {
    NoticeKind kind = NoticeKind::CommandRejected;
    CompanyId company;
    PlotId plot;
    AuctionId auction;
    MineId mine;
    ResourceId resource;
    LoanId loan;
    TechId tech;
    EventId event;
    std::int64_t tonnes = 0;
    Money amount;
    RejectReason reason = RejectReason::None;
};

// Stable identifiers for logs, CSV and UI text lookup.
[[nodiscard]] const char* rejectReasonName(RejectReason reason);
[[nodiscard]] const char* noticeKindName(NoticeKind kind);

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
