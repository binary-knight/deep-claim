#pragma once

// Flat, string-formatted copies of the session's view for RmlUi data
// binding. RmlUi binds plain members, so everything the interface shows is
// prepared here each time the sim state changes; the RML never touches sim
// types directly. The layout follows docs/design/handoff.md: a status strip,
// an icon rail, the H4 dashboard (what needs you, then what you own, then
// the money), the Program tab (spec 010) and the five tabs.

#include "sim/content.h"
#include "sim/ids.h"
#include "sim/view.h"

#include "game/auto_pause.h"
#include "game/session.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <cstdint>
#include <string>
#include <vector>

namespace game {

// The order a control has waiting for the clock (spec 007). A control with
// `pending` set shows `text` as its chip or line in place of its button, and
// Cancel withdraws order `id`. When several waiting orders belong to one
// control (two repayments of one loan, say), this is the one given last.
struct OrderMark {
    bool pending = false;
    std::string text;  // "Survey ordered", "Bid placed: $1.20M", ... or ""
    int id = 0;        // Session::cancel's id; 0 when nothing is waiting
    // The money available when the order's turn comes does not cover it (a
    // loan given before it was cancelled). Display only: the sim decides.
    bool not_covered = false;
};

// One waiting order, for the pending-orders list and the Finance and Market
// sheets' pending lines.
struct PendingRow {
    int id = 0;  // Session::cancel's id
    std::string text;
    bool not_covered = false;
};

// One row of the dashboard's "Needs you" list.
struct NeedRow {
    int number = 0;
    std::string title;
    std::string sub;
    std::string days;  // "in 30 days" or ""
    // What the action button does: 0 none, 1 open the plot (Plots tab),
    // 2 mothball the mine, 3 open the Market tab, 4 open the Finance tab,
    // 5 open the Research tab, 6 open the Program tab, 7 open the Plots tab.
    int action = 0;
    int target = -1;  // plot index or mine index for actions 1 and 2
    std::string act_label;
    OrderMark order;  // action 2 only: a mothball order waiting for this mine
};

// A listed plot on the Plots tab's "For sale" table.
struct SaleRow {
    int index = 0;
    std::string name;  // "#18"
    std::string region;
    std::string area;  // "501 ha"
    std::string ask;
    std::string bids;    // "2"
    std::string closes;  // "day 1380"
    std::string survey;  // estimate summary or ""
    std::string error;   // "(x0.28)" or ""
    std::string flag;  // chip text: "2 bids", "surveyed", "not surveyed", "empty", "locked ore", ""
    std::string flag_class;  // chip class: "warn", "danger", "" (neutral)
    bool has_flag = false;
    bool major = false;  // the survey reports a major deposit of ore the player can mine
    // The missing ores the plot's region can hold (spec 011, BR-008):
    // "may hold nickel", and the stronger "survey shows nickel" for those
    // the player's survey of it shows. Each "" when there are none.
    std::string may_hold;
    std::string survey_shows;
};

// A settled auction in the record under the for-sale table (spec 006).
struct SoldRow {
    int index = 0;      // the plot, so a click selects it
    std::string name;   // "#12"
    std::string buyer;  // a company's name, or "You"
    std::string paid;   // "$1.14M"
    std::string over;   // "16% over the ask" or "at the ask"
    std::string day;    // "day 45"
    bool mine = false;  // the player bought it
};

// What one of the player's mines earns (spec 008), on its row and on the
// selected-plot panel. Every figure is the sim's (sim::MineView), formatted;
// nothing here is worked out by the client.
struct MineFigures {
    bool shown = false;  // false for a worked-out or closed mine, and for no mine
    // "+$12.3k/day" or "-$2.1k/day" at today's price; for a mothballed mine
    // what it costs to keep, as a loss; "not producing yet" with no figure.
    std::string earns;
    bool loss = false;        // `earns` is negative: the sign says so, the colour repeats it
    bool mothballed = false;  // `earns` is the mothballed cost; `reopen` has the margin
    std::string reopen;       // mothballed only: "+$12.3k/day" if reopened today, else ""
    bool reopen_loss = false;
    // "Break-even $1,430.00/t" or "": exact to the cent (exactPrice), never
    // abbreviated, because the player sells against it.
    std::string break_even;
    std::string note;  // "at today's price" beside a margin, else ""
};

// A plot the player owns, on the dashboard board and the Plots tab.
struct OwnedRow {
    int index = 0;  // plot index
    std::string name;
    std::string region;
    std::string area;
    std::string mine;        // chip text: "Operating", "Building", "No mine", ...
    std::string mine_class;  // chip class: good, warn, muted, danger
    std::string output;      // "60 t/day copper + 400 t/day iron" or "—"
    std::string remaining;   // "~41,000 t · ~690 days", "exhausted", "173 days to open"
    MineFigures figures;     // what the mine earns a day, and its break-even price
    bool can_toggle = false;
    bool mine_operating = false;
    bool can_build = false;
    int mine_index = -1;
    OrderMark build_order;  // a mine ordered here
    OrderMark mine_order;   // mothball or reopening ordered for this mine
};

// A survey estimate line on the selected-plot panel.
struct EstimateRow {
    std::string resource;
    std::string tonnes;  // "~185,330 t"
    std::string error;   // "(x0.28)"
    // "needs Lithium Processing", "mineable in 40 days", "one project away" or ""
    std::string needs;
    bool locked = false;
    bool major = false;  // more than twice the region's median deposit
};

// The selected plot's panel.
struct PlotPanel {
    bool any = false;
    int index = -1;
    std::string title;  // "Plot #18 · Copper Belt"
    std::string area;
    std::string ask;      // "" when not listed
    std::string auction;  // "closes day 1380 · 2 bids" or ""
    std::string capex;
    std::string odds;          // "copper 70% · iron 60%"
    std::string may_hold;      // as SaleRow's, while the plot is listed
    std::string survey_shows;  // as SaleRow's, while the plot is listed
    std::string owner;         // "Yours", rival name, or ""
    std::string sale;          // "day 45, $1.14M, 16% over the ask" once sold, else ""
    std::string survey_title;  // "Survey, day 1136" or "No survey yet"
    std::vector<EstimateRow> estimates;
    std::string value;         // "~$133M" or ""
    std::string notice;        // build risk: unsurveyed / survey out / empty / locked only
    std::string notice_class;  // "warn" or "danger"
    std::string contest;       // rival-bid warning with the repeat-bid note, or ""
    bool listed = false;
    bool owned = false;
    bool can_survey = false;
    std::string survey_label;  // "Survey ($150k)" or "Survey again ($150k)"
    // Why a survey would be wasted: the report lands after the auction
    // closes, or the mine here is worked out (then the button is disabled).
    std::string survey_note;
    // The money available covers the fee. When it does not, the button is
    // disabled and survey_blocked says why (spec 007).
    bool can_afford_survey = false;
    std::string survey_blocked;
    bool can_build = false;
    bool can_afford_mine = false;
    std::string build_label;    // "Build mine ($2.00M)"
    std::string build_blocked;  // the disabled button's text
    bool can_toggle_mine = false;
    bool mine_operating = false;
    int mine_index = -1;
    MineFigures figures;  // the player's own mine here: what it earns a day
    // Orders waiting on this plot. While one is pending its chip stands in
    // for the button, whatever the button's own state would be.
    OrderMark survey_order;
    OrderMark bid_order;
    OrderMark build_order;
    OrderMark mine_order;  // mothball or reopening
};

struct ResourceRow {
    int index = 0;
    std::string name;
    // Prices per tonne are exact to the cent (game::exactPrice), never
    // abbreviated: "$1,430.00", not "$1.4k" (spec 010 BR-014).
    std::string price;       // "$37.41"
    std::string price_unit;  // "$37.41/t"
    std::string base_line;   // "base $60.00, x0.62"
    std::string market_sub;  // "demand 1,600 t/day · supply 1,704 t/day"
    std::string stock;       // "3,633 t"
    std::string stock_sub;   // "stock 3,633 t" (prices panel)
    std::string stock_value;
    std::string ratio;        // "x0.62"
    std::string policy;       // standing order summary
    std::string auto_sold;    // "Sold 1,850 t this month for $1.6M." or ""
    std::string locked_note;  // "needs Lithium Processing" or ""
    bool locked = false;
    bool ratio_warn = false;  // price below 0.7 of base
    bool has_stock = false;
    bool policy_on = false;  // as it will stand once a waiting policy order is given
    bool selected = false;
    OrderMark sale_order;    // a manual sale of this ore
    OrderMark policy_order;  // a change to this ore's standing order
};

struct TechRow {
    int index = 0;
    std::string name;
    std::string needs;  // "needs Seismic Survey I, Core Sampling" or ""
    std::string cost;
    std::string days;
    std::string effects;
    std::string chip;        // done, researching, available, locked
    std::string chip_class;  // good, accent, muted
    std::string until;       // "until day 1420" or ""
    std::string blocked;     // why Start is disabled, or ""
    bool can_start = false;
    bool researching = false;
    OrderMark order;  // "Queued for the lab"; then can_start is off and blocked is ""
};

struct LoanRow {
    int index = 0;
    std::string name;  // "#1"
    std::string principal;
    std::string remaining;
    std::string instalment;
    std::string months;
    bool active = false;
    bool can_repay_all = false;  // the money available covers the balance
    OrderMark repay_order;       // a repayment of this loan
};

struct LogRow {
    std::string day;
    std::string text;
    bool warning = false;
    std::string mark;  // "Stopped the clock" on a line that stopped it (spec 011, BR-009), else ""
};

struct PauseRow {
    int index = 0;
    std::string label;
    bool on = false;
};

// Why the clock stopped (spec 011, BR-009, BR-010), from
// Session::stopsToday(): up while the day's stops stand, so until the clock
// is started again, and not over the end screen.
struct StopPanel {
    bool shown = false;
    bool race = false;    // a race stop (isRaceStop): its own heading and both founding days
    std::string heading;  // "Clock stopped: Survey complete", the rule as the Stops menu names it
    std::string line;     // the first stop's line, as the log has it
    std::string more;     // "+2 more" when several stopped the clock that day, else ""
    std::string days;     // a race stop's earliest founding days, else ""
    std::string go_to;    // "Go to Plots"
    int tab = 0;          // where the player acts on it, numbered as UiModel::tab
    int rule = 0;         // the PauseOn it stopped for
    std::string rule_label;  // "Stop for this", or "Not stopping for this" once the rule is off
};

// One company's place in the race for the program (spec 010), on the
// Dashboard's race strip. Every figure is the sim's (sim::ProgramPublicView),
// worded by the catalogue; nothing here is worked out by the client.
struct RaceRow {
    std::string name;  // "You" for the player, else the company's name
    std::string
        founding;       // "earliest founding Y17 M5 D3"; "" with no day or once the game has ended
    std::string stage;  // "1 of 3 stages complete"
    std::string progress;   // "42% delivered": the bar's figure, toward the current stage
    std::string bar_width;  // the bar's fill as an RCSS width, "42.5%"
    std::string research;   // "research not started", "research due Y12 M1 D1", "research done"
    bool you = false;
};

// One stage of the program in the Program tab's list of three (spec 010).
struct ProgramStageRow {
    std::string heading;      // "Stage 1: Launch site"
    std::string state;        // chip text: "complete", "current", "to come"
    std::string state_class;  // chip class: good, accent, muted
    bool current = false;
};

// One good the current stage asks for. The tonnes are the sim's
// (sim::ProgramRequirementView), formatted.
struct ProgramNeedRow {
    int index = 0;  // resource index: what the Deliver button delivers
    std::string good;
    std::string needs;      // "60,000 t"
    std::string delivered;  // "12,000 t"
    std::string stock;      // in the company's stock today
    std::string still;      // still needed
    // The most that can be delivered now: the lesser of the stock and what
    // is still needed. The button says how much; with nothing to deliver it
    // is off, reads "Deliver", and `note` under it says why.
    bool can_deliver = false;
    std::string deliver_label;  // "Deliver 12,000 t" or "Deliver"
    std::string note;
    std::string locked_note;  // "Lithium needs Lithium Processing before we can dig it." or ""
    OrderMark order;          // a delivery of this good waiting for the clock
};

// Where one good the program still needs comes from (spec 011, BR-006):
// display assembly from the content and the player's own view, each line
// "" when it has nothing to say.
struct WhereFromRow {
    std::string good;
    std::string regions;   // "Nickel may be found in Northern Shield (40%), ..."
    std::string research;  // the unlocking project while the good is locked, or ""
    std::string mines;     // our mines that yield it with their daily output, or that none does
    std::string surveys;   // the plots our latest surveys show it on, or ""
};

// The Program tab (spec 010): the stage the player is working on, or the
// line that says every stage is done. Whether a stage can be completed is
// the sim's answer (sim::ProgramStageView::ready); the lines that say what
// is missing are read off the same view's figures and decide nothing.
struct ProgramPanel {
    std::string title;          // "The program"
    std::string intro;          // what a stage needs, and that a delivery is final
    std::string cover_warning;  // cover costs is on and may sell held stock, or ""
    bool complete = false;      // every stage is complete: `done_line` stands alone
    std::string done_line;
    // The current stage; everything below is empty once the program is complete.
    bool has_stage = false;
    std::string heading;  // "Stage 1: Launch site"
    // Column headings, from the catalogue.
    std::string col_good;
    std::string col_needs;
    std::string col_delivered;
    std::string col_stock;
    std::string col_still;
    std::vector<ProgramNeedRow> needs;
    std::string cost;           // "Cost $5.00M"
    std::string research_name;  // the stage's project
    std::string research;       // "Research not started", "Research finishes day 240", ...
    // Start research: offered while the project has not started, live when
    // the Research tab would start it too (its row is the one source).
    bool research_open = false;
    bool can_research = false;
    bool pace_warning = false;     // sim::ProgramView::paceWarning: pace_line is a warning
    std::string research_label;    // "Start research ($2.00M)"
    std::string research_blocked;  // why it is off, as the Research tab says it, or ""
    OrderMark research_order;
    // Complete stage: live exactly when the sim says the stage is ready.
    bool ready = false;
    std::string complete_label;  // "Complete stage ($5.00M)"
    OrderMark complete_order;
    std::string ready_line;          // everything is in place, or ""
    std::string blocked_research;    // the research is not finished, or ""
    std::string blocked_deliveries;  // deliveries are outstanding, or ""
    std::string blocked_cash;        // spendable cash does not cover the cost, or ""
    // Keeping pace (spec 011, BR-004), read off sim::ProgramView: the last
    // day to start the stage's research, or that it has passed; and the day
    // a lab busy with another project is free. Each "" when the view is silent.
    std::string pace_line;
    std::string lab_line;
    // Where each good an incomplete stage needs comes from (spec 011, BR-006).
    std::string where_heading;
    std::vector<WhereFromRow> where_from;
};

// One stage's line under a company on the end screen (spec 010).
struct EndStageLine {
    std::string text;      // "Stage 1: day 412" or "Stage 3: not reached"
    bool reached = false;  // the words say so; the colour only repeats it
};

// One company's race on the end screen: the stages it completed and the day
// of each. Both are the sim's (sim::ProgramPublicView: stagesCompleted and
// stageDays), so they stand in a long game and in a loaded one: `lines` has
// one line a stage, always.
struct EndCompany {
    std::string name;    // "You" for the player, else the company's name
    std::string stages;  // "2 of 3 stages complete"
    std::vector<EndStageLine> lines;
    bool you = false;
};

// The end screen (spec 010, BR-009), which covers play once the session says
// the game is over. Which ending it was is the sim's (the view's bankrupt
// flag and outcome); the player's own bankruptcy comes before the race.
struct EndPanel {
    bool shown = false;
    // "founded", "partner", "lost", "deadline" or "bankrupt"; "" while playing.
    // Not shown: the title and body say it in words.
    std::string result;
    std::string title;
    std::string body;
    std::string day;                    // "The game ended on day 3396"
    std::vector<EndCompany> companies;  // the player first, then every rival
    std::string figures_title;          // "Your figures"
    std::string cash_label;
    std::string cash;
    std::string mines_label;
    std::string mines;  // how many mines the player has, in any state
    std::string stages_label;
    std::string stages;    // how many stages the player completed
    std::string new_game;  // the one button
};

// Grouped by screen rather than by size; the padding is a few dozen bytes.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct UiModel {
    // Header
    std::string company;
    std::string day;
    std::string cash;
    std::string spendable;  // cash less what bids hold in reserve
    // What is left to spend this turn: spendable, less what the waiting
    // orders will cost, plus what the waiting loans bring in (spec 007).
    // Every affordability check in the model uses this figure.
    std::string available;
    std::string committed;       // "Committed: $1.35M"
    bool has_committed = false;  // a waiting order costs money
    std::string debt;
    std::string speed_label;
    int speed = 0;  // 0 paused, 1, 2, 3
    // 0 dashboard, 1 plots, 2 market, 3 research, 4 finance, 5 log, 6 program
    // (second on the rail, after the Dashboard).
    int tab = 0;
    bool game_over = false;
    bool deck = false;        // Steam Deck sizes (narrow window or --deck)
    std::string status_line;  // transient feedback (saved, rejected, paused ...)
    bool status_danger = false;
    bool show_pause_menu = false;
    std::vector<PauseRow> pause_rows;
    StopPanel stop;

    // Orders waiting for the clock (spec 007), in the order given
    std::vector<PendingRow> pending_orders;
    int pending_count = 0;
    bool has_pending = false;
    std::string pending_line;        // "3 orders waiting for the clock" or ""
    bool pending_shortfall = false;  // some waiting order is not covered

    // Status strip
    std::string month_net;  // "+$1.9M"
    bool month_good = false;
    std::string storage_month;  // "$12k/mo"
    std::string solvency;       // chip: "solvent" / "insolvent"
    std::string solvency_sub;   // "3 months grace" / "2 months before bankruptcy"
    bool solvent = true;
    std::string next_auction;      // "#18 closes day 1380" / "none listed"
    std::string mines_operating;   // "2 operating" or ""
    std::string mines_building;    // "1 building" or ""
    std::string mines_mothballed;  // "1 mothballed" or ""
    std::string mines_depleted;    // "1 depleted" or ""
    std::string research_strip;    // "High Throughput I · day 1420" / "lab idle"
    std::string world_event;       // event name or ""
    std::string world_sub;         // "40 days left"
    bool has_world = false;

    // The race for the program (spec 010): the status strip's item on every
    // tab, and the Dashboard's race strip.
    bool has_race = false;  // the content has a program to race for
    // "Northfield Minerals could found the program in 6 y 120 d" while the
    // leading solvent rival's earliest founding day is before the deadline
    // (spec 011), else "".
    std::string race_rival;
    std::string race_deadline;  // "29 y 132 d to the deadline"; "" once a company has founded it
    std::string race_founded;   // "Northfield Minerals founded the program", else ""
    // The player against the leading solvent rival, in words (spec 011):
    // "Behind by 1,374 days", "Ahead by 12 days", "Level" on the same earliest
    // founding day, or "We cannot found the program in time" when the view
    // says the player's day is past the deadline; "" once the game has ended.
    // Stages complete, then progress, decide only where a day is missing.
    // Display only.
    std::string race_verdict;
    std::string race_verdict_class;  // chip class: "good" ahead, "warn" behind, "" level
    std::vector<RaceRow> race_rows;  // the player first, then every rival

    // Program tab (spec 010)
    std::vector<ProgramStageRow> program_stages;  // every stage, in order
    ProgramPanel program;

    // End screen (spec 010)
    EndPanel end;

    // Dashboard
    std::vector<NeedRow> needs;
    std::string needs_none;  // shown when the list is empty
    std::vector<OwnedRow> owned;
    std::string rival_line;
    // Finance row (dashboard and Finance tab)
    std::string capacity_sub;     // "can borrow $8.84M"
    std::string warehouse_value;  // "$0.8M"
    std::string storage_sub;      // "storage $12k/month"
    std::string instalment_sub;   // "instalment $53k"
    std::vector<LogRow> recent;   // last three log lines

    // Plots
    std::vector<SaleRow> for_sale;
    PlotPanel panel;
    std::vector<TechRow> techs;  // also the research tiles on the Plots tab
    // The land horizon under the for-sale table: "86 plots not yet listed."
    // while the queue has land, "No unlisted land remains." once it is empty.
    std::string no_unlisted;
    // What land has sold for: the five most recent settled auctions, newest
    // first, and the line shown while there are none.
    std::vector<SoldRow> recent_sales;
    std::string no_sales;

    // Market
    std::vector<ResourceRow> resources;
    std::string chart_title;  // "Copper ore · 360 days"
    std::string chart_note;
    std::string storage_tonnes;  // "4,850 t across two ores"
    std::string storage_rate;    // "5% of base price per tonne-month"
    std::string storage_note;
    std::string standing_title;  // "Standing order · Copper ore"
    std::string policy_summary;
    std::string policy_reserve_input;
    std::string policy_floor_input;
    bool policy_enabled = false;  // as it will stand once a waiting order is given
    std::string ui_policy_hint;
    OrderMark policy_order;                 // the selected ore's standing order
    std::vector<PendingRow> market_orders;  // waiting manual sales

    // Research
    std::string lab_line;  // "One project at a time"
    std::string lab_sub;   // "busy until day 1420 · High Throughput I" / "idle"

    // Finance
    std::vector<LoanRow> loans;
    std::string loan_rule;
    std::string loan_limits;
    std::string loan_credit;                 // "You can borrow up to $8.84M."
    std::string loan_blocked;                // why Take loan is disabled, or ""
    bool can_take_loan = false;              // credit covers the minimum loan and a slot is free
    OrderMark loan_order;                    // the loan asked for last
    std::vector<PendingRow> finance_orders;  // waiting loans and repayments
    bool cover_costs = false;                // as it will stand once a waiting order is given
    OrderMark cover_order;
    std::string cover_check;
    std::string cover_explain;
    std::string cover_last;  // "not needed" or what was sold last month
    std::string solvency_rule;
    std::string storage_month_long;  // "$12k/month"

    // Log
    std::vector<LogRow> log;  // newest first, after the filter
    int log_filter = 0;       // 0 all, 1 mine, 2 market, 3 rivals
    std::string log_empty;    // shown when the filter leaves nothing

    // Inputs bound to text fields
    std::string bid_input;
    std::string sell_input;
    std::string loan_input;
    std::string repay_input;
    int selected_plot = -1;
    int selected_resource = 0;

    // Prose from the catalogue
    std::string ui_shortcuts;
    std::string ui_pause_menu_title;
    std::string ui_cancel;
    std::string ui_cancel_all;
    std::string ui_not_covered;
    // Labels for a mine's figures (spec 008): the words beside a row's
    // figures, and the keys on the selected-plot panel.
    std::string ui_mine_loss;             // "loss"
    std::string ui_mine_if_reopened;      // "if reopened"
    std::string ui_mine_earns;            // "Earns"
    std::string ui_mine_loses;            // "Loses"
    std::string ui_mine_mothballed;       // "Mothballed"
    std::string ui_mine_if_reopened_key;  // "If reopened"
};

// Rebuilds every field of `model` from the session.
void refreshModel(const Session& session, UiModel& model);

}  // namespace game
