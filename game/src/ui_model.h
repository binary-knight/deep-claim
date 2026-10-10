#pragma once

// Flat, string-formatted copies of the session's view for RmlUi data
// binding. RmlUi binds plain members, so everything the interface shows is
// prepared here each time the sim state changes; the RML never touches sim
// types directly. The layout follows docs/design/handoff.md: a status strip,
// an icon rail, the H4 dashboard (what needs you, then what you own, then
// the money) and the five tabs.

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
    // 5 open the Research tab.
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
    std::string price;       // "$37.41"
    std::string price_unit;  // "$37.41/t"
    std::string base_line;   // "base $60.00 · x0.62"
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
};

struct PauseRow {
    int index = 0;
    std::string label;
    bool on = false;
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
    int tab = 0;    // 0 dashboard, 1 plots, 2 market, 3 research, 4 finance, 5 log
    bool game_over = false;
    bool deck = false;        // Steam Deck sizes (narrow window or --deck)
    std::string status_line;  // transient feedback (saved, rejected, paused ...)
    bool status_danger = false;
    bool show_pause_menu = false;
    std::vector<PauseRow> pause_rows;

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
    std::string ui_game_over;
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

// A price per tonne as the player must be able to act on it: every dollar
// and cent, with thousands separators, never abbreviated ("$1,430.00",
// "$41.20", "-$0.05"). formatMoney rounds $1,430 to "$1.4k", which is no
// use as a threshold.
[[nodiscard]] std::string exactPrice(sim::Money amount);

// Rebuilds every field of `model` from the session.
void refreshModel(const Session& session, UiModel& model);

}  // namespace game
