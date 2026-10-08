#pragma once

// Flat, string-formatted copies of the session's view for RmlUi data
// binding. RmlUi binds plain members, so everything the interface shows is
// prepared here each time the sim state changes; the RML never touches sim
// types directly.

#include "sim/content.h"
#include "sim/ids.h"
#include "sim/view.h"

#include "game/session.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <cstdint>
#include <string>
#include <vector>

namespace game {

struct PlotRow {
    int index = 0;
    std::string name;
    std::string region;
    std::string area;
    std::string status;   // "For sale", "Yours", "Rival 1", "Not listed"
    std::string ask;      // ask price while listed, else ""
    std::string auction;  // "closes Y1 M2 D5, 2 bids, your bid $1.2M"
    std::string survey;   // latest own survey summary or ""
    std::string mine;     // mine state or ""
    std::string capex;    // capex quote
    bool listed = false;
    bool mine_here = false;
    bool owned = false;  // by the player
    bool can_survey = false;
    bool can_build = false;
    bool can_toggle_mine = false;
    bool mine_operating = false;
    int mine_index = -1;
};

struct ResourceRow {
    int index = 0;
    std::string name;
    std::string price;
    std::string base;
    std::string ratio;   // price / base
    std::string supply;  // recent company supply
    std::string stock;   // player's tonnes
    std::string stock_value;
    bool has_stock = false;
    bool selected = false;
};

struct TechRow {
    int index = 0;
    std::string name;
    std::string cost;
    std::string duration;
    std::string effects;
    std::string status;  // "Known", "In progress (12 days)", "Available", "Locked"
    bool can_start = false;
};

struct LoanRow {
    int index = 0;
    std::string remaining;
    std::string instalment;
    std::string months;
    bool active = false;
};

struct LogRow {
    std::string day;
    std::string text;
    bool warning = false;
};

struct UiModel {
    // Header
    std::string company;
    std::string day;
    std::string cash;
    std::string debt;
    std::string speed_label;
    int speed = 0;  // 0 paused, 1, 2, 3
    int tab = 0;    // 0 dashboard, 1 plots, 2 market, 3 research, 4 finance, 5 log
    bool game_over = false;
    std::string status_line;  // transient feedback (saved, rejected, ...)

    // Dashboard
    std::string loan_capacity;
    std::string storage_cost;
    std::string mine_opex;
    std::string survey_noise;
    std::string mines_summary;
    std::string plots_summary;
    std::string research_summary;
    std::vector<std::string> rivals;
    std::vector<std::string> events;

    std::vector<PlotRow> plots;
    std::vector<ResourceRow> resources;
    std::vector<TechRow> techs;
    std::vector<LoanRow> loans;
    std::vector<LogRow> log;

    // Inputs bound to text fields
    std::string bid_input;
    std::string sell_input;
    std::string loan_input;
    std::string repay_input;
    int selected_plot = -1;
    int selected_resource = 0;
    std::string selected_plot_title;
    std::string selected_resource_title;
};

// Rebuilds every field of `model` from the session.
void refreshModel(const Session& session, UiModel& model);

}  // namespace game
