// Headless layout probe: loads the real interface with a stub renderer and
// lays out every tab at the desktop frame (1366x820) and the Steam Deck
// frame (1280x800), so layout problems can be diagnosed without a display.
// Also a regression test (spec 003): exits non-zero when the content pane
// collapses, anything overflows its scroll pane horizontally, text in the
// deck frame falls below 14dp, a no-wrap text is wider than the block that
// holds it, RmlUi logs a warning or error (unknown RCSS property, unbound
// data variable ...), or the playtest trace has no header. Each frame is
// probed twice: on a fresh game, and on a fixed-seed game played forward
// until the player owns a plot with a working mine, a loan and a research
// project, with a plot selected and the auto-pause menu open, so every
// table has rows. A third pass plays the same game on to the states spec
// 004 added (a survey report just in, a survey that would land late, credit
// exhausted, no unlisted land left) and lays out the Log tab under each of
// its filters. A last pass (spec 007) starts the game again, stops the clock
// with a plot bought and nothing built, and gives a bid, a survey, a mine
// and a research order through the real buttons: it exits non-zero when
// the pending chips, the Committed and Available figures, the waiting line
// or Cancel are wrong, and lays the turn out with the waiting-orders list
// open and closed, before and after one order is cancelled. Then (spec 008)
// a second app runs on the probe's own copy of the content, in which mining
// costs far more than ore fetches: it plays the fixed game to a mine that
// loses money and exits non-zero when the mine's row, the plot's panel, the
// log line or the clock are wrong, with the stop rule on and with it off.
// Before that app, the first one plays the race (spec 010): the fixed game
// to a mine with stock the first stage needs, delivered by orders, and it
// exits non-zero when the deadline, the verdict, either company's stage
// count, percentage, research state or bar differ from the sim's view, on
// the status strip of every tab and on the Dashboard's race strip; with the
// rival under a long name, the earliest founding days, the research due day,
// the rival's time to founding and the verdict's gap in days are checked
// too (spec 011), as are a player more delivered but Behind on research time
// and the cannot-found-in-time verdict, each on a copy of its own. The same
// game goes on to the Program tab (spec 010): a new game, a delivery given by
// its button and waiting, then made, the stage's research ordered and under
// way; then two apps on copies of the content, one whose first stage asks for
// a good the company cannot dig, and one whose stages are small enough for
// the fixed game to complete all three. It exits non-zero when a stage, a
// requirement's tonnes, the cost, the research state, a button's state, the
// reasons a stage cannot be completed, a pending chip or the Committed figure
// differ from the sim's view.
// Not part of the shipped game.
#include "sim/commands.h"
#include "sim/view.h"

#include "app.h"
#include "chart_element.h"
#include "content/loader.h"
#include "game/auto_pause.h"
#include "game/format.h"
#include "game/session.h"
#include "platform/services.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

class StubRender final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> /*vertices*/,
                                                Rml::Span<const int> /*indices*/) override {
        return 1;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle /*geometry*/, Rml::Vector2f /*translation*/,
                        Rml::TextureHandle /*texture*/) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle /*geometry*/) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions,
                                   const Rml::String& /*source*/) override {
        dimensions = {168, 24};  // the icon sheet's size, so sprites resolve
        return 1;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> /*source*/,
                                       Rml::Vector2i /*dimensions*/) override {
        return 1;
    }
    void ReleaseTexture(Rml::TextureHandle /*texture*/) override {}
    void EnableScissorRegion(bool /*enable*/) override {}
    void SetScissorRegion(Rml::Rectanglei /*region*/) override {}
};

// Collects RmlUi's warnings and errors: a stylesheet or binding mistake is
// reported there and nowhere else.
class LoggingSystem final : public Rml::SystemInterface {
public:
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_WARNING ||
            type == Rml::Log::LT_ASSERT) {
            problems.push_back(message);
        }
        return Rml::SystemInterface::LogMessage(type, message);
    }
    std::vector<std::string> problems;
};

// By tab number. The Program tab (spec 010) is the seventh by number and the
// second on the rail, after the Dashboard.
constexpr std::array<const char*, 7> kTabs = {"dashboard", "plots", "market", "research",
                                              "finance",   "log",   "program"};
constexpr float kDeckWidth = 1300.0F;
constexpr float kMinDeckText = 14.0F;
constexpr float kScrollbarWidth = 12.0F;

struct Frame {
    int width;
    int height;
};
constexpr std::array<Frame, 2> kFrames = {{{1366, 820}, {1280, 800}}};

// What hangs from the header's corner while a tab is laid out.
enum class Popover : std::uint8_t { None, PauseMenu, PendingList };

struct TabReport {
    float contentWidth = 0.0F;
    int elements = 0;
    int overflows = 0;
    float minText = 1000.0F;
    std::string minTextSample;
    std::vector<std::string> notes;
};

float rightEdge(Rml::Element* e) {
    return e->GetAbsoluteOffset(Rml::BoxArea::Border).x +
           e->GetBox().GetSize(Rml::BoxArea::Border).x;
}

std::string squeezed(const Rml::String& text) {
    std::string out;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            out += c;
        }
    }
    return out;
}

std::string describe(Rml::Element* e) {
    return std::format("<{} id={} class={}>", e->GetTagName(), e->GetId(), e->GetClassNames());
}

void noteText(Rml::Element* parent, Rml::ElementText* text, TabReport& report) {
    const std::string content = squeezed(text->GetText());
    if (content.empty()) {
        return;
    }
    // Text that may not wrap must fit the block or cell that holds it; RmlUi
    // does not grow a table column or a fixed-width flex child to fit.
    if (parent->GetComputedValues().white_space() == Rml::Style::WhiteSpace::Nowrap) {
        Rml::Element* block = parent;
        while (block->GetParentNode() != nullptr &&
               block->GetComputedValues().display() == Rml::Style::Display::Inline) {
            block = block->GetParentNode();
        }
        std::string line = text->GetText();
        const std::size_t first = line.find_first_not_of(" \t\r\n");
        const std::size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);
        const auto width = static_cast<float>(Rml::ElementUtilities::GetStringWidth(parent, line));
        const float room = block->GetBox().GetSize(Rml::BoxArea::Content).x;
        if (width > room + 1.0F) {
            ++report.overflows;
            if (report.notes.size() < 12) {
                report.notes.push_back(
                    std::format("text overflow: \"{}\" is {:.0f}dp in {} of {:.0f}dp",
                                line.substr(0, 40), width, describe(block), room));
            }
        }
    }
    const float fontSize = parent->GetComputedValues().font_size();
    if (fontSize < report.minText) {
        report.minText = fontSize;
        report.minTextSample = std::format("{} \"{}\"", describe(parent), content.substr(0, 24));
    }
}

// Walks the visible tree. `clipRight` is the right edge of the nearest
// scrollable ancestor (or the window): anything past it would need a
// horizontal scrollbar, which the design never shows. Descendants of
// overflow-hidden elements (the rail, the company name) are meant to clip
// and are not checked.
void inspect(Rml::Element* e, float clipRight, TabReport& report) {
    if (e == nullptr || !e->IsVisible(true)) {
        return;
    }
    const Rml::String& tag = e->GetTagName();
    if (tag == "scrollbarvertical" || tag == "scrollbarhorizontal") {
        return;  // RmlUi's own widget, laid out in the padding box's margin
    }
    const Rml::Vector2f size = e->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0F && size.y <= 0.0F) {
        return;
    }
    ++report.elements;
    if (rightEdge(e) > clipRight + 0.5F) {
        ++report.overflows;
        if (report.notes.size() < 12) {
            report.notes.push_back(std::format("overflow: {} right edge {:.0f} > {:.0f}",
                                               describe(e), rightEdge(e), clipRight));
        }
    }
    const Rml::Style::Overflow overflowX = e->GetComputedValues().overflow_x();
    const Rml::Style::Overflow overflowY = e->GetComputedValues().overflow_y();
    if (overflowX == Rml::Style::Overflow::Hidden) {
        return;  // intentional clip
    }
    float childClip = clipRight;
    if (overflowX != Rml::Style::Overflow::Visible || overflowY != Rml::Style::Overflow::Visible) {
        // Scrollable: a vertical scrollbar takes its width from the inside.
        const bool scrolls = e->GetScrollHeight() > e->GetClientHeight() + 0.5F;
        childClip = e->GetAbsoluteOffset(Rml::BoxArea::Padding).x +
                    e->GetBox().GetSize(Rml::BoxArea::Padding).x -
                    (scrolls ? kScrollbarWidth : 0.0F);
    }
    for (int i = 0; i < e->GetNumChildren(true); ++i) {
        Rml::Element* child = e->GetChild(i);
        if (auto* text = rmlui_dynamic_cast<Rml::ElementText*>(child)) {
            noteText(e, text, report);
        } else {
            inspect(child, childClip, report);
        }
    }
}

void dump(Rml::Element* e, int depth, int maxDepth) {
    if (e == nullptr || depth > maxDepth || !e->IsVisible(true)) {
        return;
    }
    const Rml::Vector2f size = e->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f pos = e->GetAbsoluteOffset(Rml::BoxArea::Border);
    std::cout << std::format("{:{}}{} at {:.0f},{:.0f} size {:.0f} x {:.0f}\n", "", depth * 2,
                             describe(e), pos.x, pos.y, size.x, size.y);
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        dump(e->GetChild(i), depth + 1, maxDepth);
    }
}

// The playtest trace must start with a new_game record naming this build.
bool checkTrace(const game::App& app) {
    std::ifstream trace(app.tracePath());
    std::string firstLine;
    std::getline(trace, firstLine);
    std::cout << std::format("trace: {}\n  {}\n", app.tracePath().string(), firstLine);
    if (firstLine.find(R"("t":"new_game")") == std::string::npos ||
        firstLine.find(R"("build":"probe")") == std::string::npos) {
        std::cout << "TRACE FAILURE: no new_game record\n";
        return false;
    }
    return true;
}

// Lays out one tab in the current frame and reports on it. Returns false on
// a failure, after dumping the content tree for diagnosis.
bool probeTab(Rml::Context& context, game::App& app, LoggingSystem& system, const Frame& frame,
              std::size_t tab, Popover popover) {
    const bool deck = static_cast<float>(frame.width) < kDeckWidth;
    app.setTab(static_cast<int>(tab));
    app.showPauseMenu(popover == Popover::PauseMenu);  // changing tab closes it
    if (popover == Popover::PendingList) {
        app.showPendingList(true);  // after the pause menu, which closes it
    }
    context.Update();
    context.Update();  // data bindings settle on the second pass
    Rml::ElementDocument* doc = context.GetDocument(0);
    Rml::Element* content = doc->GetElementById("content");
    TabReport report;
    bool failed = false;
    if (content == nullptr || content->GetNumChildren() == 0) {
        report.notes.emplace_back("content pane missing or empty");
        failed = true;
    } else {
        // The visible tab is the content pane's one visible child.
        for (int i = 0; i < content->GetNumChildren(); ++i) {
            Rml::Element* child = content->GetChild(i);
            if (child->IsVisible(true)) {
                report.contentWidth = child->GetBox().GetSize(Rml::BoxArea::Border).x;
            }
        }
        if (report.contentWidth < 100.0F) {
            report.notes.emplace_back("content width collapsed");
            failed = true;
        }
    }
    inspect(doc, static_cast<float>(frame.width), report);
    failed = failed || report.overflows > 0;
    if (deck && report.minText < kMinDeckText - 0.01F) {
        report.notes.push_back(std::format("text below {:.0f}dp in the deck frame: {}",
                                           kMinDeckText, report.minTextSample));
        failed = true;
    }
    for (const std::string& problem : system.problems) {
        report.notes.push_back("rmlui: " + problem);
        failed = true;
    }
    system.problems.clear();
    std::cout << std::format(
        "{:<9} content {:4.0f}dp wide, {:4} elements, {} overflow, smallest text {:.0f}dp{}\n",
        kTabs.at(tab), report.contentWidth, report.elements, report.overflows, report.minText,
        failed ? "  FAILED" : "");
    for (const std::string& note : report.notes) {
        std::cout << "    " << note << '\n';
    }
    if (failed) {
        dump(content, 0, 5);
    }
    return !failed;
}

constexpr std::uint64_t kScenarioSeed = 20261009;
constexpr int kScenarioMaxDays = 900;

// The player's purchase is in the sales record at half again the ask, and
// on the selected plot's panel (spec 006).
bool checkSalesRecord(const game::UiModel& model, const sim::Content& content, sim::PlotId target) {
    const std::string over = game::text(content, "ui.sale_over", {{"percent", "50"}});
    const bool recorded = std::any_of(
        model.recent_sales.begin(), model.recent_sales.end(), [&](const game::SoldRow& row) {
            return row.mine && row.index == static_cast<int>(target.value()) && row.over == over;
        });
    if (recorded && model.no_sales.empty() && model.recent_sales.size() <= 5 &&
        model.panel.sale.find(over) != std::string::npos) {
        return true;
    }
    std::cout << std::format(
        "SCENARIO: the player's purchase is missing from the sales record "
        "({} rows, panel \"{}\", expected \"{}\")\n",
        model.recent_sales.size(), model.panel.sale, over);
    return false;
}

// Plays a fixed game until every table has rows: a loan, a survey, a won
// auction, a mine that has opened and produced, a research project. Returns
// false (with a reason) when the fixed seed no longer gets there, which
// means the scenario needs retuning, not that the layout is wrong.
bool playScenario(game::App& app, const sim::Content& content) {
    app.startGame(kScenarioSeed);
    game::Session& session = app.session();
    game::AutoPause never;
    never.enabled.fill(false);
    session.setAutoPause(never);
    session.submit(sim::TakeLoan{{}, content.balance.loanMinPrincipal * 4});
    session.submit(sim::StartResearch{{}, sim::TechId(0)});
    // The cheapest listed plot, at half again its ask so the rival loses.
    sim::PlotId target;
    sim::Money ask;
    for (const sim::PlotView& plot : session.view().plots) {
        if (plot.listing == sim::PlotListing::Listed && plot.auction &&
            (!target.valid() || plot.askPrice < ask)) {
            target = plot.id;
            ask = plot.askPrice;
        }
    }
    if (target.valid()) {
        session.submit(sim::CommissionSurvey{{}, target});
        session.submit(sim::BidOnPlot{{}, target, ask + ask / 2});
    }
    if (!target.valid()) {
        std::cout << "SCENARIO: no plot is listed on day 0\n";
        return false;
    }
    bool building = false;
    bool produced = false;
    for (int day = 0; day < kScenarioMaxDays && !produced; ++day) {
        session.stepOneDay();
        const sim::CompanyView& view = session.view();
        if (!building && view.plots[target.index()].owner == game::Session::kPlayer) {
            session.submit(sim::BuildMine{{}, target});
            building = true;
        }
        for (const sim::MineView& mine : view.mines) {
            produced = produced || (mine.state == sim::MineState::Operating &&
                                    mine.operationalDay + 5 < view.day);
        }
    }
    app.refreshNow();
    app.selectPlot(static_cast<int>(target.value()));
    const game::UiModel& model = app.model();
    if (!produced || model.owned.empty() || model.loans.empty() || model.needs.empty()) {
        std::cout << std::format(
            "SCENARIO: seed {} did not reach a working mine ({} owned, {} loans, {} needs, {})\n",
            kScenarioSeed, model.owned.size(), model.loans.size(), model.needs.size(), model.day);
        for (std::size_t i = model.log.size(); i > 0 && i + 12 > model.log.size(); --i) {
            std::cout << "    " << model.log[i - 1].day << "  " << model.log[i - 1].text << '\n';
        }
        return false;
    }
    // While land is still queued the Plots tab shows how much (spec 005).
    const std::uint32_t unlisted = session.view().unlistedPlotsRemaining;
    const std::string horizon = game::textPlural(content, "ui.unlisted_plots", unlisted,
                                                 {{"count", std::to_string(unlisted)}});
    if (unlisted == 0 || model.no_unlisted != horizon) {
        std::cout << std::format(
            "SCENARIO: the Plots tab does not show the land horizon "
            "({} unlisted, \"{}\")\n",
            unlisted, model.no_unlisted);
        return false;
    }
    if (!checkSalesRecord(model, content, target)) {
        return false;
    }
    std::cout << std::format(
        "\nscenario: {}, {} owned ({}; {}), {} loans, {} needs-you rows, "
        "\"{}\"\n",
        model.day, model.owned.size(), model.owned[0].output, model.owned[0].remaining,
        model.loans.size(), model.needs.size(), model.no_unlisted);
    return true;
}

bool hasNeed(const game::UiModel& model, const std::string& title) {
    return std::any_of(model.needs.begin(), model.needs.end(),
                       [&](const game::NeedRow& row) { return row.title == title; });
}

// A new company has money and an empty lab: the dashboard must say so.
bool checkFreshNeeds(const game::App& app, const sim::Content& content) {
    // Nothing has sold on day 0: the sales record says so instead of
    // showing an empty table (spec 006).
    if (!app.model().recent_sales.empty() ||
        app.model().no_sales != game::text(content, "ui.no_sales")) {
        std::cout << "SCENARIO: a fresh game does not show an empty sales record\n";
        return false;
    }
    if (hasNeed(app.model(), game::text(content, "ui.need_lab_idle_title"))) {
        return true;
    }
    std::cout << "SCENARIO: a fresh game has no lab-idle row under Needs you\n";
    return false;
}

// Long enough for the shipped world's land queue to run dry (120 plots at
// one a month, spec 005) with room to spare.
constexpr int kLateMaxDays = 4500;

// Plays the populated game on to the states spec 004 added. In order: a
// survey report lands on a plot still at auction (needs-you row); the land
// queue runs dry; credit is drawn to nothing (loan form disabled with its
// reason); and a listed plot is selected whose auction closes before a
// survey ordered now could report (the survey warning). Returns false with
// a reason when the fixed seed no longer reaches one of them.
bool playLateScenario(game::App& app, const sim::Content& content) {
    game::Session& session = app.session();
    const auto fail = [&](const char* what) {
        std::cout << std::format("SCENARIO (late): {} ({})\n", what, app.model().day);
        return false;
    };
    // 1. Survey the listed plot whose auction closes last, and wait for the report.
    sim::PlotId surveyed;
    sim::Day latest = 0;
    for (const sim::PlotView& plot : session.view().plots) {
        if (plot.listing == sim::PlotListing::Listed && plot.auction &&
            plot.auction->closesOnDay > latest) {
            surveyed = plot.id;
            latest = plot.auction->closesOnDay;
        }
    }
    if (!surveyed.valid() || latest <= session.day() + content.balance.surveyDurationDays + 1) {
        return fail("no auction is open long enough to survey");
    }
    session.submit(sim::CommissionSurvey{{}, surveyed});
    for (sim::Day d = 0; d <= content.balance.surveyDurationDays; ++d) {
        session.stepOneDay();
    }
    app.refreshNow();
    const std::string reportRow = game::text(content, "ui.need_survey_in_title",
                                             {{"plot", std::format("#{}", surveyed.value() + 1)}});
    if (!hasNeed(app.model(), reportRow)) {
        return fail("the survey report did not raise a needs-you row");
    }
    std::cout << std::format("\nlate: {}, \"{}\" under Needs you\n", app.model().day, reportRow);
    return true;
}

// Why a game stopped short of a scenario's state: the day, the land still
// unlisted, and whether the player fell or the race ended.
std::string whereItStopped(const sim::CompanyView& view) {
    std::string race = "the race still running";
    if (view.outcome.ended) {
        race =
            view.outcome.founder.valid()
                ? std::format("{} founded the program on day {}",
                              view.companies[view.outcome.founder.index()].name, view.outcome.day)
                : std::format("the race ended on day {} with no founder", view.outcome.day);
    }
    return std::format("SCENARIO: stopped on day {}, {} plots unlisted, player {}, {}\n", view.day,
                       view.unlistedPlotsRemaining, view.bankrupt ? "bankrupt" : "solvent", race);
}

// The populated game played on until the land has run out, on the copy of
// the content in which the race cannot end first (landRunsOutContent).
bool playEndgameScenario(game::App& app, const sim::Content& content) {
    game::Session& session = app.session();
    const auto fail = [&](const char* what) {
        std::cout << std::format("SCENARIO (endgame): {} ({})\n", what, app.model().day);
        return false;
    };
    // 2. Until every plot has been offered once.
    for (int day = 0; day < kLateMaxDays && session.view().unlistedPlotsRemaining > 0; ++day) {
        session.stepOneDay();
    }
    if (session.view().unlistedPlotsRemaining > 0 || session.gameOver()) {
        std::cout << whereItStopped(session.view());
        return fail("the land queue did not run dry with the player solvent");
    }
    // 3. Draw the remaining credit, in as many loans as the bank allows.
    if (session.view().loanCapacity >= content.balance.loanMinPrincipal) {
        session.submit(sim::TakeLoan{{}, session.view().loanCapacity});
        session.stepOneDay();
    }
    // 4. A listed plot a survey ordered today would report on too late.
    sim::PlotId late;
    for (int day = 0; day < kLateMaxDays && !late.valid(); ++day) {
        for (const sim::PlotView& plot : session.view().plots) {
            if (plot.listing == sim::PlotListing::Listed && plot.auction &&
                session.day() + content.balance.surveyDurationDays >= plot.auction->closesOnDay) {
                late = plot.id;
                break;
            }
        }
        if (!late.valid()) {
            session.stepOneDay();
        }
    }
    if (!late.valid() || session.gameOver()) {
        return fail("no auction is about to close");
    }
    app.refreshNow();
    app.selectPlot(static_cast<int>(late.value()));
    const game::UiModel& model = app.model();
    const sim::CompanyView& view = session.view();
    const std::string ranOut =
        view.withdrawnPlots == 0
            ? game::text(content, "ui.no_unlisted")
            : game::textPlural(content, "ui.no_unlisted_withdrawn", view.withdrawnPlots,
                               {{"count", std::to_string(view.withdrawnPlots)}});
    if (model.no_unlisted != ranOut) {
        return fail("the Plots tab does not say the land has run out");
    }
    if (model.can_take_loan || model.loan_blocked.empty()) {
        return fail("the loan form is not disabled with a reason at zero credit");
    }
    if (model.panel.survey_note.empty()) {
        return fail("the selected plot carries no late-survey warning");
    }
    std::cout << std::format("\nendgame: {}\n  plots: {}\n  loans: {}\n  survey: {}\n", model.day,
                             model.no_unlisted, model.loan_blocked, model.panel.survey_note);
    return true;
}

// The Log tab under each filter, in both frames. Each filter must leave
// either rows or the "nothing here" line.
bool probeLogFilters(Rml::Context& context, game::App& app, LoggingSystem& system) {
    bool ok = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== log filters, frame {}x{}{} ==\n", frame.width, frame.height,
                                 deck ? " (deck)" : "");
        std::size_t all = 0;
        std::size_t parts = 0;
        for (int filter = 0; filter < 4; ++filter) {
            app.setLogFilter(filter);
            const std::size_t rows = app.model().log.size();
            std::cout << std::format("filter {}: {} rows{}\n", filter, rows,
                                     app.model().log_empty.empty() ? "" : " (empty line shown)");
            if ((rows == 0) == app.model().log_empty.empty()) {
                std::cout << "    filter shows neither rows nor the empty line\n";
                ok = false;
            }
            all = filter == 0 ? rows : all;
            parts += filter == 0 ? 0 : rows;
            const bool passed = probeTab(context, app, system, frame, 5, Popover::None);
            ok = ok && passed;
        }
        // Every entry belongs to exactly one filter (the list is capped, so
        // the parts can only exceed the capped whole).
        if (parts < all) {
            std::cout << std::format("    filters cover {} of {} rows\n", parts, all);
            ok = false;
        }
        app.setLogFilter(0);
    }
    return ok;
}

// Every tab in both frames. Returns false when any tab fails.
bool probeFrames(Rml::Context& context, game::App& app, LoggingSystem& system, const char* label,
                 Popover popover) {
    bool ok = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== {} game, frame {}x{}{} ==\n", label, frame.width,
                                 frame.height, deck ? " (deck)" : "");
        for (std::size_t tab = 0; tab < kTabs.size(); ++tab) {
            const bool passed = probeTab(context, app, system, frame, tab, popover);
            ok = ok && passed;
        }
    }
    return ok;
}

constexpr int kLayoutFailure = 3;
constexpr int kScenarioFailure = 5;

// The played game, stage by stage, each laid out before the next is played.
int probeProgramSurveyed(Rml::Context& context, game::App& app, LoggingSystem& system);

// Returns 0, or the exit code of the first stage that failed. The game's
// last stage, the land run out, is probeEndgame's.
int probeScenarios(Rml::Context& context, game::App& app, LoggingSystem& system,
                   const sim::Content& content) {
    if (!playScenario(app, content)) {
        return kScenarioFailure;
    }
    if (!probeFrames(context, app, system, "populated", Popover::PauseMenu)) {
        return kLayoutFailure;
    }
    if (!playLateScenario(app, content)) {
        return kScenarioFailure;
    }
    if (!probeFrames(context, app, system, "survey in", Popover::None)) {
        return kLayoutFailure;
    }
    return probeProgramSurveyed(context, app, system);
}

// The shipped content with a last stage no company can pay for, so that no
// program is founded while the land runs out: in the shipped game the rival
// founds it first (about day 3,400 on the fixed seed, with land still
// unlisted) and the game ends there. Only the last stage's cost differs, so
// the companies play as they do in the shipped game until one of them has
// everything else that stage needs.
constexpr std::int64_t kNeverPaidDollars = 1'000'000'000'000;

sim::Content landRunsOutContent(const sim::Content& shipped) {
    sim::Content content = shipped;
    if (!content.program.stages.empty()) {
        content.program.stages.back().cost = sim::Money::fromUnits(kNeverPaidDollars);
    }
    return content;
}

// The interface when no land is left, on that copy and so on an app of its
// own: the populated game again, then on until the queue is dry.
int probeEndgame(Rml::Context& context, game::App& app, LoggingSystem& system,
                 const sim::Content& content) {
    if (!playScenario(app, content) || !playEndgameScenario(app, content)) {
        return kScenarioFailure;
    }
    const bool laidOut = probeFrames(context, app, system, "endgame", Popover::None);
    const bool filtered = probeLogFilters(context, app, system);
    return laidOut && filtered ? 0 : kLayoutFailure;
}

// ---- orders waiting for the clock (spec 007) ----

constexpr int kPendingFailure = 6;

// Collects what is wrong with a stage's state, printing each failure under
// the stage's label.
class Checks {
public:
    explicit Checks(std::string_view label = "PENDING") : label_(label) {}
    void expect(bool held, std::string_view what) {
        if (!held) {
            std::cout << label_ << ": " << what << '\n';
            ok_ = false;
        }
    }
    template <typename T>
    void same(std::string_view what, const T& actual, const T& expected) {
        if (!(actual == expected)) {
            std::cout << std::format("{}: {}: \"{}\", expected \"{}\"\n", label_, what, actual,
                                     expected);
            ok_ = false;
        }
    }
    [[nodiscard]] bool ok() const { return ok_; }

private:
    std::string label_;
    bool ok_ = true;
};

// The Needs you rows against the view (spec 011, BR-014): the storage row
// stands exactly when the sim's view raises its storage warning, and each
// row's button reads its catalogue label. Returns whether the warning stood.
bool checkNeedRows(game::App& app, std::string_view where, Checks& checks) {
    const sim::Content& content = app.session().content();
    const bool warned = app.session().view().storageWarning;
    const game::UiModel& model = app.model();
    const std::string storage = game::text(content, "ui.need_storage_title");
    checks.same(std::format("{}: a storage row under Needs you", where),
                std::ranges::any_of(model.needs,
                                    [&](const game::NeedRow& row) { return row.title == storage; }),
                warned);
    // Action 1 is a plot's: Read, or the auction row's Bid and Raise bid.
    const std::array<const char*, 7> keys = {"ui.act_read",    "ui.act_mothball", "ui.act_sell",
                                             "ui.tab_finance", "ui.tab_research", "ui.tab_program",
                                             "ui.tab_plots"};
    for (const game::NeedRow& row : model.needs) {
        if (row.action >= 2 && row.action <= 7) {
            checks.same(std::format("{}: \"{}\" button", where, row.title), row.act_label,
                        game::text(content, keys.at(static_cast<std::size_t>(row.action) - 1)));
        } else if (row.action == 1 && row.act_label != "Bid" && row.act_label != "Raise bid") {
            checks.same(std::format("{}: \"{}\" button", where, row.title), row.act_label,
                        game::text(content, keys.at(0)));
        }
    }
    std::cout << std::format("    {}: storage warning {}, {} needs-you rows\n", where,
                             warned ? "raised" : "not raised", model.needs.size());
    return warned;
}

// The text under an element as the player reads it: hidden children left out.
std::string visibleText(Rml::Element* e) {
    std::string out;
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        Rml::Element* child = e->GetChild(i);
        if (!child->IsVisible(true)) {
            continue;
        }
        if (auto* text = rmlui_dynamic_cast<Rml::ElementText*>(child)) {
            out += text->GetText();
        } else {
            out += visibleText(child);
        }
    }
    return out;
}

void collectVisible(Rml::Element* e, std::string_view tag, std::string_view cls,
                    std::vector<Rml::Element*>& found) {
    if (e == nullptr || !e->IsVisible(true)) {
        return;
    }
    if (e->GetTagName() == tag && (cls.empty() || e->IsClassSet(std::string(cls)))) {
        found.push_back(e);
    }
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        collectVisible(e->GetChild(i), tag, cls, found);
    }
}

// The visible elements under `root` with this tag and class ("" for any), in
// document order.
std::vector<Rml::Element*> visibleElements(Rml::Element* root, std::string_view tag,
                                           std::string_view cls) {
    std::vector<Rml::Element*> found;
    collectVisible(root, tag, cls, found);
    return found;
}

// Those of them that read exactly `text`.
std::vector<Rml::Element*> visibleElements(Rml::Element* root, std::string_view tag,
                                           std::string_view cls, const std::string& text) {
    std::vector<Rml::Element*> found = visibleElements(root, tag, cls);
    std::erase_if(found, [&](Rml::Element* e) { return visibleText(e) != text; });
    return found;
}

void settle(Rml::Context& context) {
    context.Update();
    context.Update();  // data bindings settle on the second pass
}

// Clicks an element as the player would, so the real event binding runs.
void click(Rml::Context& context, Rml::Element* target, std::string_view what, Checks& checks) {
    checks.expect(target != nullptr, std::format("nothing to click: {}", what));
    if (target != nullptr) {
        target->Click();
        settle(context);
    }
}

void clickFirst(Rml::Context& context, std::string_view tag, std::string_view cls,
                const std::string& text, Checks& checks) {
    settle(context);
    const std::vector<Rml::Element*> found =
        visibleElements(context.GetDocument(0), tag, cls, text);
    click(context, found.empty() ? nullptr : found.front(),
          std::format("<{} class={}> \"{}\"", tag, cls, text), checks);
}

// The paused turn the pending pass is built on.
struct PendingTurn {
    sim::PlotId lot;             // listed: the bid and the survey
    sim::PlotId site;            // owned, nothing built: the mine
    std::size_t tech = 0;        // the research project
    sim::Money spendable;        // cash less reserved, before any order
    std::size_t logEntries = 0;  // the session's log, before any order
    sim::Day day = 0;            // the day the clock is stopped on
    std::string surveyLabel;     // the buttons the orders stand in for
    std::string buildLabel;
    std::size_t startButtons = 0;  // "Start" buttons on the Research tab
    sim::Money bid;                // what the bid field offered
};

// The cheapest plot at auction the player has not bid on.
sim::PlotId cheapestListed(const sim::CompanyView& view) {
    sim::PlotId found;
    sim::Money ask;
    for (const sim::PlotView& plot : view.plots) {
        if (plot.listing == sim::PlotListing::Listed && plot.auction &&
            plot.auction->myBid == sim::Money::zero() && (!found.valid() || plot.askPrice < ask)) {
            found = plot.id;
            ask = plot.askPrice;
        }
    }
    return found;
}

// Starts the fixed game again and plays it, clock stopped, to a turn with
// every one of the four orders open: a plot bought and not yet built on,
// another at auction that can be surveyed, an idle lab, and the money for
// all of it. Returns false with a reason when the fixed seed no longer gets
// there.
bool playPendingTurn(Rml::Context& context, game::App& app, const sim::Content& content,
                     PendingTurn& turn) {
    const auto fail = [&](std::string_view what) {
        std::cout << std::format("SCENARIO (pending): {} ({})\n", what, app.model().day);
        return false;
    };
    app.startGame(kScenarioSeed);
    game::Session& session = app.session();
    game::AutoPause never;
    never.enabled.fill(false);
    session.setAutoPause(never);
    turn.site = cheapestListed(session.view());
    if (!turn.site.valid()) {
        return fail("no plot is listed on day 0");
    }
    const sim::Money ask = session.view().plots[turn.site.index()].askPrice;
    session.submit(sim::TakeLoan{{}, content.balance.loanMinPrincipal * 4});
    session.submit(sim::BidOnPlot{{}, turn.site, ask + ask / 2});
    for (int day = 0; day < kScenarioMaxDays &&
                      session.view().plots[turn.site.index()].owner != game::Session::kPlayer;
         ++day) {
        session.stepOneDay();
    }
    const sim::CompanyView& view = session.view();
    turn.lot = cheapestListed(view);
    if (view.plots[turn.site.index()].owner != game::Session::kPlayer || !turn.lot.valid()) {
        return fail("no plot bought with another still at auction");
    }
    if (session.speed() != game::Speed::Paused || !session.pendingOrders().empty()) {
        return fail("the clock is not stopped with nothing waiting");
    }
    app.refreshNow();
    const game::UiModel& model = app.model();
    app.setTab(1);
    app.selectPlot(static_cast<int>(turn.lot.value()));
    turn.surveyLabel = model.panel.survey_label;
    const bool lotOpen = model.panel.listed && model.panel.can_survey;
    app.selectPlot(static_cast<int>(turn.site.value()));
    turn.buildLabel = model.panel.build_label;
    const bool siteOpen = model.panel.can_build && model.panel.can_afford_mine;
    const auto startable = std::ranges::find_if(model.techs, &game::TechRow::can_start);
    if (!lotOpen || !siteOpen || startable == model.techs.end()) {
        return fail("a survey, a mine or a research project cannot be ordered");
    }
    turn.tech = static_cast<std::size_t>(startable->index);
    turn.spendable = view.cash - view.reservedCash;
    turn.logEntries = session.log().size();
    turn.day = session.day();
    const sim::Money needed = content.balance.surveyCost + view.plots[turn.lot.index()].askPrice +
                              view.plots[turn.site.index()].mineCapexQuote +
                              content.techs[turn.tech].cost;
    if (needed > turn.spendable) {
        return fail(std::format("the four orders cost {} and {} is on hand",
                                game::formatMoney(needed), game::formatMoney(turn.spendable)));
    }
    app.setTab(3);
    settle(context);
    turn.startButtons = visibleElements(context.GetDocument(0), "button", "", "Start").size();
    std::cout << std::format(
        "\npending: {}, clock stopped, {} on hand; plot #{} at auction, plot #{} bought, "
        "\"{}\" ready\n",
        model.day, model.spendable, turn.lot.value() + 1, turn.site.value() + 1,
        content.techs[turn.tech].name);
    return true;
}

// The Cancel button that stands beside a pending chip.
Rml::Element* cancelBeside(Rml::Element* chip, const std::string& label) {
    for (Rml::Element* e = chip->GetNextSibling(); e != nullptr; e = e->GetNextSibling()) {
        if (e->IsVisible(true) && e->GetTagName() == "button" && visibleText(e) == label) {
            return e;
        }
    }
    return nullptr;
}

// Gives the four orders through their own buttons: bid, survey, mine, lab.
void giveOrders(Rml::Context& context, game::App& app, PendingTurn& turn, Checks& checks) {
    app.setTab(1);
    app.selectPlot(static_cast<int>(turn.lot.value()));
    clickFirst(context, "button", "", "Place bid", checks);
    clickFirst(context, "button", "", turn.surveyLabel, checks);
    app.selectPlot(static_cast<int>(turn.site.value()));
    clickFirst(context, "button", "", turn.buildLabel, checks);
    app.setTab(3);
    clickFirst(context, "button", "", "Start", checks);
    // What reached the queue is what the chips must describe.
    const std::vector<game::PendingOrder>& waiting = app.session().pendingOrders();
    const auto* bid = waiting.empty() ? nullptr : std::get_if<sim::BidOnPlot>(&waiting[0].command);
    const bool queued = waiting.size() == 4 && bid != nullptr && bid->plot == turn.lot &&
                        bid->amount > sim::Money::zero() &&
                        std::holds_alternative<sim::CommissionSurvey>(waiting[1].command) &&
                        std::get<sim::CommissionSurvey>(waiting[1].command).plot == turn.lot &&
                        std::holds_alternative<sim::BuildMine>(waiting[2].command) &&
                        std::get<sim::BuildMine>(waiting[2].command).plot == turn.site &&
                        std::holds_alternative<sim::StartResearch>(waiting[3].command) &&
                        std::get<sim::StartResearch>(waiting[3].command).tech.index() == turn.tech;
    checks.expect(queued, std::format("the buttons did not queue a bid, a survey, a mine and a "
                                      "research order ({} waiting)",
                                      waiting.size()));
    turn.bid = bid != nullptr ? bid->amount : sim::Money::zero();
}

// What one stage of the turn should show.
struct Expected {
    bool bid = false;
    bool survey = false;
    bool build = false;
    bool research = false;
    [[nodiscard]] int count() const {
        return static_cast<int>(bid) + static_cast<int>(survey) + static_cast<int>(build) +
               static_cast<int>(research);
    }
};

// The header and the strip: the waiting line, Committed, Available. Nothing
// has been charged or logged, and the clock has not moved.
void checkFigures(Rml::Context& context, game::App& app, const sim::Content& content,
                  const PendingTurn& turn, const Expected& expected, Checks& checks) {
    settle(context);
    const game::UiModel& model = app.model();
    const game::Session& session = app.session();
    const sim::CompanyView& view = session.view();
    Rml::ElementDocument* doc = context.GetDocument(0);
    const std::string bidText =
        game::text(content, "ui.pending_bid", {{"amount", game::formatMoney(turn.bid)}});
    std::vector<std::string> texts;
    sim::Money committed = sim::Money::zero();
    if (expected.bid) {
        texts.push_back(bidText);
        committed += turn.bid;
    }
    if (expected.survey) {
        texts.push_back(game::text(content, "ui.pending_survey"));
        committed += content.balance.surveyCost;
    }
    if (expected.build) {
        texts.push_back(game::text(content, "ui.pending_build"));
        committed += view.plots[turn.site.index()].mineCapexQuote;
    }
    if (expected.research) {
        texts.push_back(game::text(content, "ui.pending_research"));
        committed += content.techs[turn.tech].cost;
    }
    const int count = expected.count();
    const std::string line = count == 0 ? std::string()
                                        : game::textPlural(content, "ui.pending_count", count,
                                                           {{"count", std::to_string(count)}});
    const std::string committedText =
        count == 0
            ? std::string()
            : game::text(content, "ui.committed", {{"amount", game::formatMoney(committed)}});
    const std::string available = game::formatMoney(turn.spendable - committed);

    checks.same("orders waiting", model.pending_count, count);
    checks.same("the waiting line", model.pending_line, line);
    std::vector<std::string> listed;
    for (const game::PendingRow& row : model.pending_orders) {
        listed.push_back(row.text);
        checks.expect(!row.not_covered, std::format("\"{}\" is marked not covered", row.text));
    }
    checks.expect(listed == texts, "the waiting orders are not the ones given, in the order given");
    checks.same("Committed", model.committed, committedText);
    checks.same("Committed shown", model.has_committed, count > 0);
    checks.same("Available", model.available, available);
    checks.same("cash less reserved", model.spendable, game::formatMoney(turn.spendable));
    checks.expect((model.available != model.spendable) == (count > 0),
                  "Available is reduced exactly while orders that cost money wait");
    checks.expect(!model.pending_shortfall, "an order is marked not covered");
    checks.same("log entries", session.log().size(), turn.logEntries);
    checks.same("the day", session.day(), turn.day);
    checks.expect(session.speed() == game::Speed::Paused, "the clock is running");

    // On screen, not only in the model.
    const std::vector<Rml::Element*> waitingLines = visibleElements(doc, "div", "waiting");
    checks.same("waiting lines beside the speed controls", waitingLines.size(),
                std::size_t{count > 0 ? 1U : 0U});
    if (!waitingLines.empty()) {
        checks.same("the waiting line on screen", visibleText(waitingLines.front()), line);
    }
    Rml::Element* strip = doc->GetElementById("strip");
    std::size_t committedShown = 0;
    for (Rml::Element* e : visibleElements(strip, "span", "num")) {
        committedShown += visibleText(e) == committedText ? 1U : 0U;
    }
    checks.same("Committed figures in the strip", committedShown, std::size_t{count > 0 ? 1U : 0U});
    std::size_t availableShown = 0;
    for (Rml::Element* stat : visibleElements(doc, "div", "stat")) {
        const std::string shown = visibleText(stat);
        if (shown.starts_with("Available")) {
            ++availableShown;
            checks.same("Available on screen", shown, "Available" + available);
        }
    }
    checks.expect(availableShown > 0, "no Available figure on screen");
}

// How many accent chips read `text`, and how many buttons read `button`.
void checkControl(Rml::Context& context, std::string_view where, const std::string& chip,
                  std::size_t chips, const std::string& button, std::size_t buttons,
                  Checks& checks) {
    settle(context);
    Rml::ElementDocument* doc = context.GetDocument(0);
    std::size_t accent = 0;
    for (Rml::Element* e : visibleElements(doc, "span", "chip", chip)) {
        accent += e->IsClassSet("accent") ? 1U : 0U;
    }
    checks.same(std::format("{}: \"{}\" chips", where, chip), accent, chips);
    checks.same(std::format("{}: \"{}\" buttons", where, button),
                visibleElements(doc, "button", "", button).size(), buttons);
}

// Each order's chip stands in for its button, with Cancel beside it; an
// order that is not waiting leaves its button where it was.
void checkControls(Rml::Context& context, game::App& app, const sim::Content& content,
                   const PendingTurn& turn, const Expected& expected, Checks& checks) {
    const game::UiModel& model = app.model();
    const auto shown = [](bool pending) { return std::size_t{pending ? 1U : 0U}; };
    const std::string bidText =
        game::text(content, "ui.pending_bid", {{"amount", game::formatMoney(turn.bid)}});
    const std::string buildText = game::text(content, "ui.pending_build");
    const std::string researchText = game::text(content, "ui.pending_research");
    app.setTab(1);
    app.selectPlot(static_cast<int>(turn.lot.value()));
    checkControl(context, "plot at auction", bidText, shown(expected.bid), "Place bid",
                 shown(!expected.bid), checks);
    checkControl(context, "plot at auction", game::text(content, "ui.pending_survey"),
                 shown(expected.survey), turn.surveyLabel, shown(!expected.survey), checks);
    checks.same("the bid field shown",
                visibleElements(context.GetDocument(0), "input", "text").size(),
                shown(!expected.bid));
    checks.same("panel bid pending", model.panel.bid_order.pending, expected.bid);
    checks.same("panel survey pending", model.panel.survey_order.pending, expected.survey);
    // The mine: on the plot's panel and in the owned-plots table beside it,
    // and on the dashboard's board.
    app.selectPlot(static_cast<int>(turn.site.value()));
    checkControl(context, "plot bought", buildText, 2 * shown(expected.build), turn.buildLabel,
                 shown(!expected.build), checks);
    checks.same("panel mine pending", model.panel.build_order.pending, expected.build);
    app.setTab(0);
    checkControl(context, "dashboard", buildText, shown(expected.build), turn.buildLabel, 0,
                 checks);
    app.setTab(3);
    // Start is offered for the projects the money left still covers, never
    // for the one that is queued; with nothing queued, for all it was.
    const game::TechRow& tech = model.techs.at(turn.tech);
    const auto startable =
        static_cast<std::size_t>(std::ranges::count_if(model.techs, &game::TechRow::can_start));
    checkControl(context, "research", researchText, shown(expected.research), "Start", startable,
                 checks);
    checks.same("research pending", tech.order.pending, expected.research);
    checks.same("research can start", tech.can_start, !expected.research);
    if (!expected.research) {
        checks.same("projects that can start", startable, turn.startButtons);
    }
    // Every chip has its Cancel.
    const std::string cancel = game::text(content, "ui.cancel");
    for (const int tab : {0, 1, 3}) {
        app.setTab(tab);
        settle(context);
        for (Rml::Element* chip : visibleElements(context.GetDocument(0), "span", "chip")) {
            checks.expect(!chip->IsClassSet("accent") || cancelBeside(chip, cancel) != nullptr,
                          std::format("no Cancel beside \"{}\" on the {} tab", visibleText(chip),
                                      kTabs.at(static_cast<std::size_t>(tab))));
        }
    }
}

// The open list: one row an order, and all of it inside the frame.
void checkList(Rml::Context& context, const Frame& frame, const Expected& expected,
               Checks& checks) {
    Rml::Element* list = context.GetDocument(0)->GetElementById("pendinglist");
    if (list == nullptr || !list->IsVisible(true)) {
        checks.expect(false, "the waiting-orders list is not open");
        return;
    }
    checks.same("rows in the waiting-orders list", visibleElements(list, "div", "nrow").size(),
                static_cast<std::size_t>(expected.count()));
    const float bottom = list->GetAbsoluteOffset(Rml::BoxArea::Border).y +
                         list->GetBox().GetSize(Rml::BoxArea::Border).y;
    checks.expect(bottom <= static_cast<float>(frame.height) + 0.5F,
                  std::format("the waiting-orders list ends at {:.0f}dp in a frame {}dp high",
                              bottom, frame.height));
}

// Every tab in both frames, twice: the plot at auction selected with the
// waiting-orders list open, then the plot bought with the list closed.
bool probePendingFrames(Rml::Context& context, game::App& app, LoggingSystem& system,
                        const PendingTurn& turn, const Expected& expected, const char* label,
                        Checks& checks) {
    bool ok = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        for (const bool open : {true, false}) {
            std::cout << std::format(
                "\n== {}, {}, frame {}x{}{} ==\n", label,
                open ? "plot at auction, list open" : "plot bought, list closed", frame.width,
                frame.height, deck ? " (deck)" : "");
            app.selectPlot(static_cast<int>((open ? turn.lot : turn.site).value()));
            for (std::size_t tab = 0; tab < kTabs.size(); ++tab) {
                const bool passed = probeTab(context, app, system, frame, tab,
                                             open ? Popover::PendingList : Popover::None);
                ok = ok && passed;
                if (open) {
                    checkList(context, frame, expected, checks);
                }
            }
        }
    }
    return ok;
}

// Takes the bid back with the Cancel beside its chip.
void cancelBid(Rml::Context& context, game::App& app, const sim::Content& content,
               const PendingTurn& turn, Checks& checks) {
    app.setTab(1);
    app.selectPlot(static_cast<int>(turn.lot.value()));
    settle(context);
    const std::vector<Rml::Element*> chips = visibleElements(
        context.GetDocument(0), "span", "chip",
        game::text(content, "ui.pending_bid", {{"amount", game::formatMoney(turn.bid)}}));
    click(context,
          chips.empty() ? nullptr : cancelBeside(chips.front(), game::text(content, "ui.cancel")),
          "Cancel beside the bid chip", checks);
    checks.same("status after Cancel", app.model().status_line,
                game::text(content, "status.order_cancelled"));
}

// Opens the list from the waiting line and takes everything back.
void cancelTheRest(Rml::Context& context, const sim::Content& content, Checks& checks) {
    Rml::ElementDocument* doc = context.GetDocument(0);
    settle(context);
    const std::vector<Rml::Element*> lines = visibleElements(doc, "div", "waiting");
    click(context, lines.empty() ? nullptr : lines.front(), "the waiting line", checks);
    Rml::Element* list = doc->GetElementById("pendinglist");
    checks.expect(list != nullptr && list->IsVisible(true),
                  "clicking the waiting line did not open the list");
    clickFirst(context, "button", "", game::text(content, "ui.cancel_all"), checks);
    list = doc->GetElementById("pendinglist");
    checks.expect(list == nullptr || !list->IsVisible(true),
                  "the waiting-orders list is still open with nothing waiting");
}

// The pending pass: the four orders given, one cancelled, all cancelled.
// Each stage is checked (figures, controls) and the first two are laid out.
int probePendingOrders(Rml::Context& context, game::App& app, LoggingSystem& system,
                       const sim::Content& content) {
    PendingTurn turn;
    if (!playPendingTurn(context, app, content, turn)) {
        return kScenarioFailure;
    }
    Checks checks;
    bool laidOut = true;
    const auto stage = [&](const Expected& expected, const char* label, bool layOut) {
        checkFigures(context, app, content, turn, expected, checks);
        checkControls(context, app, content, turn, expected, checks);
        const game::UiModel& model = app.model();
        std::cout << std::format("\n{}: \"{}\", \"{}\", Available {} of {}, {} log entries\n",
                                 label, model.pending_line, model.committed, model.available,
                                 model.spendable, app.session().log().size());
        for (const game::PendingRow& row : model.pending_orders) {
            std::cout << "    " << row.text << '\n';
        }
        if (layOut) {
            laidOut =
                probePendingFrames(context, app, system, turn, expected, label, checks) && laidOut;
        }
    };
    stage({}, "nothing ordered", false);
    giveOrders(context, app, turn, checks);
    stage({true, true, true, true}, "four orders waiting", true);
    cancelBid(context, app, content, turn, checks);
    stage({false, true, true, true}, "bid cancelled", true);
    cancelTheRest(context, content, checks);
    stage({}, "all cancelled", false);
    if (!checks.ok()) {
        return kPendingFailure;
    }
    return laidOut ? 0 : kLayoutFailure;
}

// ---- a mine losing money (spec 008) ----

constexpr int kLosingFailure = 7;

// What a day's mining costs, for every ore, in the losing stage's own copy
// of the content. The shipped figures are 18,000 to 35,000; at this one a
// mine loses a little over $12M a day whatever it digs, so the fixed seed
// reaches a loss without a market having to fall first, and the figure on
// screen is the widest the client writes below $100M ("-$12.34M/day", 12
// characters), which is the one the Output column has least room for.
constexpr std::int64_t kLosingOpexDollars = 12'400'000;
constexpr std::size_t kWidestFigure = 12;
constexpr float kSpillTolerance = 0.5F;  // as `inspect` allows an edge
constexpr float kNothingMeasured = -10000.0F;
// Days due in one second at the fastest speed (game::daysPerSecond).
constexpr sim::Day kDaysDue = 20;

// The losing mine, and what the clock did when its notice arrived.
struct LosingMine {
    sim::PlotId plot;
    sim::MineId mine;
    sim::Day opened = 0;      // the day the clock was started on
    sim::Day stopped = 0;     // the day after one second at the fastest speed
    sim::Day noticeDay = 0;   // the log's day for the notice
    std::string noticeText;   // its line in the log
    std::size_t notices = 0;  // losing-mine lines in the log
    game::Speed speed = game::Speed::Paused;
    std::string status;    // the status line after that second
    game::StopPanel stop;  // the stop panel then (spec 011, BR-009)
};

// The shipped content with mining made ruinous. The app and its session read
// this copy; the shipped data and the sim's rules are untouched.
sim::Content losingContent(const sim::Content& shipped) {
    sim::Content content = shipped;
    for (sim::ResourceDef& ore : content.resources) {
        ore.mineOpexPerDay = sim::Money::fromUnits(kLosingOpexDollars);
    }
    return content;
}

const sim::MineView* mineOn(const sim::CompanyView& view, sim::PlotId plot) {
    const auto found = std::ranges::find_if(
        view.mines, [&](const sim::MineView& mine) { return mine.plot == plot; });
    return found == view.mines.end() ? nullptr : &*found;
}

std::size_t countLosingNotices(const game::Session& session) {
    return static_cast<std::size_t>(
        std::ranges::count_if(session.log(), [](const game::LogEntry& entry) {
            return entry.notice.kind == sim::NoticeKind::MineLosingMoney;
        }));
}

// Starts the fixed game on the losing content and plays it, with no stop
// rule, to the first day the player's mine has a day's output on record:
// the cheapest plot bought at half again its ask and built on. Then runs
// the clock for one second at the fastest speed under `rules`, through the
// app as the window's frame loop does. Returns false with a reason when the
// fixed seed no longer gets a mine and its notice.
bool playLosingMine(game::App& app, const game::AutoPause& rules, LosingMine& losing) {
    const auto fail = [&](std::string_view what) {
        std::cout << std::format("SCENARIO (losing mine): {} ({})\n", what, app.model().day);
        return false;
    };
    app.startGame(kScenarioSeed);
    game::Session& session = app.session();
    game::AutoPause never;
    never.enabled.fill(false);
    session.setAutoPause(never);
    losing.plot = cheapestListed(session.view());
    if (!losing.plot.valid()) {
        return fail("no plot is listed on day 0");
    }
    const sim::Money ask = session.view().plots[losing.plot.index()].askPrice;
    session.submit(sim::BidOnPlot{{}, losing.plot, ask + ask / 2});
    bool building = false;
    const sim::MineView* mine = nullptr;
    for (int day = 0; day < kScenarioMaxDays; ++day) {
        session.stepOneDay();
        if (!building &&
            session.view().plots[losing.plot.index()].owner == game::Session::kPlayer) {
            session.submit(sim::BuildMine{{}, losing.plot});
            building = true;
        }
        mine = mineOn(session.view(), losing.plot);
        if (mine != nullptr && mine->state == sim::MineState::Operating && mine->hasEarnings) {
            break;
        }
    }
    if (mine == nullptr || !mine->hasEarnings) {
        return fail("the player has no mine with a day's output");
    }
    if (countLosingNotices(session) != 0 || session.gameOver()) {
        return fail("the notice came, or the company fell, before the clock was started");
    }
    losing.mine = mine->id;
    losing.opened = session.day();
    session.setAutoPause(rules);
    session.setSpeed(game::Speed::Fastest);
    app.update(1.0);
    app.refreshNow();
    app.selectPlot(static_cast<int>(losing.plot.value()));
    losing.stopped = session.day();
    losing.speed = session.speed();
    losing.status = app.model().status_line;
    losing.stop = app.model().stop;
    losing.notices = countLosingNotices(session);
    for (const game::LogEntry& entry : session.log()) {
        if (entry.notice.kind == sim::NoticeKind::MineLosingMoney) {
            losing.noticeDay = entry.day;
            losing.noticeText = entry.text;
        }
    }
    if (losing.notices == 0 || session.gameOver()) {
        return fail(std::format("no losing-mine notice within {} days of the mine opening",
                                session.day() - losing.opened));
    }
    return true;
}

// What the mine's row and panel must read, formatted from the sim's view the
// way the client formats it (ui_model.cpp mineFigures; for a loss its signed
// money is formatMoney's own minus sign, and the break-even price is exact
// to the cent, game::exactPrice), with the catalogue's labels for them.
struct LosingFigures {
    std::string plot;       // "#18"
    std::string earns;      // "-$12.36M/day"
    std::string breakEven;  // "Break-even $31,000.00/t"
    std::string note;       // "at today's price"
    std::string lossWord;   // "loss", beside the row's figure
    std::string losesKey;   // "Loses", the panel's key for an operating mine's loss
    // Every key the panel can put on a mine's earnings lines: Loses, Earns,
    // Mothballed, If reopened.
    std::array<std::string, 4> panelKeys;
};

// The sim's figures for the mine: a loss, and a break-even price above what
// its principal ore fetches today.
LosingFigures simFigures(const game::Session& session, const LosingMine& losing, Checks& checks) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    LosingFigures figures;
    figures.plot = std::format("#{}", losing.plot.value() + 1);
    const sim::MineView* mine = mineOn(view, losing.plot);
    if (mine == nullptr || mine->state != sim::MineState::Operating || !mine->hasEarnings) {
        checks.expect(false, "the mine is no longer operating with earnings to show");
        return figures;
    }
    // The principal ore is the one earning most (spec 008, BR-005).
    std::size_t principal = 0;
    sim::Money most = sim::Money::zero();
    for (std::size_t ore = 0; ore < view.markets.size(); ++ore) {
        const sim::Money earned = view.markets[ore].price * mine->lastOutputTonnes.at(ore);
        if (earned > most) {
            principal = ore;
            most = earned;
        }
    }
    const sim::Money price = view.markets[principal].price;
    checks.expect(
        mine->dailyMargin < sim::Money::zero(),
        std::format("the sim's margin is {}, not a loss", game::formatMoney(mine->dailyMargin)));
    checks.expect(mine->breakEvenPrice > price,
                  std::format("the sim's break-even {} is not above {}'s price {}",
                              game::formatMoney(mine->breakEvenPrice),
                              content.resources[principal].name, game::formatMoney(price)));
    figures.earns =
        game::text(content, "ui.mine_margin", {{"amount", game::formatMoney(mine->dailyMargin)}});
    figures.breakEven = game::text(content, "ui.mine_break_even",
                                   {{"amount", game::exactPrice(mine->breakEvenPrice)}});
    figures.note = game::text(content, "ui.mine_margin_note");
    figures.lossWord = game::text(content, "ui.mine_loss");
    figures.losesKey = game::text(content, "ui.mine_loses");
    figures.panelKeys = {figures.losesKey, game::text(content, "ui.mine_earns"),
                         game::text(content, "ui.mine_mothballed"),
                         game::text(content, "ui.mine_if_reopened_key")};
    std::cout << std::format("  sim: margin {} a day ({} cents), break-even {} against {} at {}\n",
                             game::formatMoney(mine->dailyMargin), mine->dailyMargin.cents(),
                             game::exactPrice(mine->breakEvenPrice),
                             content.resources[principal].name, game::formatMoney(price));
    return figures;
}

// A break-even price is a threshold the player sells against, so it is
// written out in full (Codex finding 5 on spec 008: a nickel mine's $1,430
// read "$1.4k/t", though selling at $1,400 loses money).
void checkExactPrices(Checks& checks) {
    const auto shown = [&](std::int64_t cents, const std::string& expected) {
        checks.same(std::format("exact price of {} cents", cents),
                    game::exactPrice(sim::Money::fromCents(cents)), expected);
    };
    shown(143'000, "$1,430.00");
    shown(4'120, "$41.20");
    shown(5, "$0.05");
    shown(99'999, "$999.99");
    shown(100'000, "$1,000.00");
    shown(3'100'045, "$31,000.45");
    shown(123'456'789, "$1,234,567.89");
    shown(-143'000, "-$1,430.00");
}

void checkModelFigures(std::string_view where, const game::MineFigures& shown,
                       const LosingFigures& expected, Checks& checks) {
    checks.expect(shown.shown && shown.loss && !shown.mothballed,
                  std::format("{}: the figure is not shown as an operating mine's loss", where));
    checks.same(std::format("{}: daily figure", where), shown.earns, expected.earns);
    checks.same(std::format("{}: break-even", where), shown.break_even, expected.breakEven);
    checks.same(std::format("{}: note", where), shown.note, expected.note);
    checks.same(std::format("{}: if-reopened figure", where), shown.reopen, std::string());
}

// The visible owned-plots rows for this plot: its name cell beside an output
// cell (the sales record names plots too, without one).
std::vector<Rml::Element*> ownedRows(Rml::Element* root, const std::string& plot) {
    std::vector<Rml::Element*> rows = visibleElements(root, "tr", "row");
    std::erase_if(rows, [&](Rml::Element* row) {
        return visibleElements(row, "td", "name", plot).empty() ||
               visibleElements(row, "td", "figure").empty();
    });
    return rows;
}

// The widest word under `e`: text breaks at white space only, so in a cell
// that wraps this is what must fit. `inspect` cannot see it: it measures
// whole lines, and only where the text may not wrap.
void widestWord(Rml::Element* e, float& widest, std::string& word) {
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        Rml::Element* child = e->GetChild(i);
        if (!child->IsVisible(true)) {
            continue;
        }
        auto* text = rmlui_dynamic_cast<Rml::ElementText*>(child);
        if (text == nullptr) {
            widestWord(child, widest, word);
            continue;
        }
        std::string_view rest = text->GetText();
        while (!rest.empty()) {
            const std::size_t end = std::min(rest.find_first_of(" \t\r\n"), rest.size());
            const std::string candidate(rest.substr(0, end));
            const auto width =
                static_cast<float>(Rml::ElementUtilities::GetStringWidth(e, candidate));
            if (width > widest) {
                widest = width;
                word = candidate;
            }
            rest.remove_prefix(std::min(end + 1, rest.size()));
        }
    }
}

// The mine's row as the player reads it, on the tab showing: the sim's loss
// with the word "loss" beside it in the warning colour, and the break-even
// price under it, exact to the cent. "loss" and the panel's "Loses" come
// from the catalogue through the UI model (ui.mine_loss, ui.mine_loses), so
// they are read from the catalogue here. Returns how far the row's widest word runs past its Output
// cell (zero or less when it fits), which is reported, not judged, here.
float checkLosingRow(Rml::Context& context, std::string_view where, const LosingFigures& expected,
                     Checks& checks) {
    settle(context);
    Rml::Element* content = context.GetDocument(0)->GetElementById("content");
    const std::vector<Rml::Element*> rows = ownedRows(content, expected.plot);
    checks.same(std::format("{}: rows for plot {}", where, expected.plot), rows.size(),
                std::size_t{1});
    if (rows.size() != 1) {
        return kNothingMeasured;
    }
    Rml::Element* cell = visibleElements(rows.front(), "td", "figure").front();
    const std::vector<Rml::Element*> subs = visibleElements(cell, "span", "sub");
    checks.same(std::format("{}: lines under the output", where), subs.size(), std::size_t{2});
    if (subs.size() == 2) {
        checks.same(std::format("{}: daily figure on screen", where),
                    squeezed(visibleText(subs[0])), squeezed(expected.earns + expected.lossWord));
        checks.expect(subs[0]->IsClassSet("warn"),
                      std::format("{}: the loss is not in the warning colour", where));
        checks.same(std::format("{}: break-even on screen", where), visibleText(subs[1]),
                    expected.breakEven);
        checks.expect(!subs[1]->IsClassSet("warn"),
                      std::format("{}: the break-even price is in the warning colour", where));
        // The exact price is the longest thing the break-even line writes;
        // it is inside the cell-wide measure below, and reported on its own.
        float priceWidth = 0.0F;
        std::string price;
        widestWord(subs[1], priceWidth, price);
        std::cout << std::format("    {}: break-even word \"{}\" {:.1f}dp\n", where, price,
                                 priceWidth);
    }
    const float room = cell->GetBox().GetSize(Rml::BoxArea::Content).x;
    float widest = 0.0F;
    std::string word;
    widestWord(cell, widest, word);
    std::cout << std::format(
        "    {}: Output cell {:.1f}dp ({:.1f}dp with its padding), widest word \"{}\" {:.1f}dp{}\n",
        where, room, cell->GetBox().GetSize(Rml::BoxArea::Padding).x, word, widest,
        widest > room + kSpillTolerance ? std::format(": SPILLS by {:.1f}dp", widest - room)
                                        : std::string());
    return widest - room;
}

// The selected plot's panel on the Plots tab: "Loses", the same figure, the
// note that it is at today's price, and the break-even price.
void checkLosingPanel(Rml::Context& context, std::string_view where, const LosingFigures& expected,
                      Checks& checks) {
    settle(context);
    Rml::Element* content = context.GetDocument(0)->GetElementById("content");
    std::vector<Rml::Element*> lines = visibleElements(content, "div", "kv");
    std::erase_if(lines, [&](Rml::Element* line) {
        const std::string shown = visibleText(line);
        return std::ranges::none_of(expected.panelKeys, [&](const std::string& key) {
            return !key.empty() && shown.starts_with(key);
        });
    });
    checks.same(std::format("{}: earnings lines on the panel", where), lines.size(),
                std::size_t{1});
    if (lines.size() != 1) {
        return;
    }
    checks.same(std::format("{}: the panel's earnings line", where),
                squeezed(visibleText(lines.front())),
                squeezed(expected.losesKey + expected.earns + expected.note + expected.breakEven));
    const std::vector<Rml::Element*> figure =
        visibleElements(lines.front(), "span", "line", expected.earns);
    checks.expect(figure.size() == 1 && figure.front()->IsClassSet("warn"),
                  std::format("{}: the panel's loss is not in the warning colour", where));
}

// The notice's line on the Log tab, shown as a warning: main.rml binds the
// row's `warning` to the line's "warn" class, which main.rcss colours.
void checkLosingLogLine(Rml::Context& context, std::string_view where, const LosingMine& losing,
                        Checks& checks) {
    settle(context);
    Rml::Element* content = context.GetDocument(0)->GetElementById("content");
    std::size_t shown = 0;
    std::size_t warnings = 0;
    for (Rml::Element* line : visibleElements(content, "div", "line")) {
        const std::size_t here = visibleElements(line, "span", "t", losing.noticeText).size();
        shown += here;
        warnings += line->IsClassSet("warn") ? here : 0;
    }
    checks.same(std::format("{}: the notice's lines on the Log tab", where), shown, std::size_t{1});
    checks.same(std::format("{}: the notice's lines shown as a warning", where), warnings,
                std::size_t{1});
}

// The notice and the clock. With the rule on the clock stopped on the day
// the notice arrived (it is logged on the day it was raised, and the stop
// leaves the session on the next day, as in tests/session_test.cpp), the
// stop panel says why and the log marks the line; with the rule off every
// day that was due went by, the clock is running and nothing is marked.
// The status line no longer says why (spec 011, BR-009): the panel does.
void checkNotice(const game::App& app, const sim::Content& content, const LosingMine& losing,
                 bool ruleOn, Checks& checks) {
    const game::UiModel& model = app.model();
    const std::string plot = "plot #" + std::to_string(losing.plot.value() + 1);
    checks.same("losing-mine lines in the log", losing.notices, std::size_t{1});
    checks.expect(losing.noticeText.find(plot) != std::string::npos,
                  std::format("the notice does not name {}: \"{}\"", plot, losing.noticeText));
    const auto row = std::ranges::find_if(
        model.log, [&](const game::LogRow& shown) { return shown.text == losing.noticeText; });
    checks.expect(row != model.log.end(), "the notice is not among the Log tab's rows");
    checks.expect(row != model.log.end() && row->warning,
                  "the notice's row in the log is not a warning");
    const auto rule = static_cast<std::size_t>(game::PauseOn::MineLosing);
    checks.expect(
        model.pause_rows.size() > rule && model.pause_rows[rule].on == ruleOn &&
            model.pause_rows[rule].label == game::text(content, "ui.pause_on.mine_losing"),
        "the Stops menu does not show the losing-mine rule as it is set");
    const std::string paused =
        game::text(content, "status.paused_by", {{"text", losing.noticeText}});
    if (ruleOn) {
        checks.expect(losing.speed == game::Speed::Paused && model.speed == 0,
                      "the clock is still running with the rule on");
        checks.same("the day the clock stopped on", losing.stopped, losing.noticeDay + 1);
        checks.expect(losing.stopped - losing.opened < kDaysDue,
                      "every day that was due went by with the rule on");
        checks.expect(losing.status != paused, "the status line still says why the clock stopped");
        checks.expect(losing.stop.shown, "no stop panel with the clock stopped for the notice");
        checks.same("the stop panel's heading", losing.stop.heading,
                    game::text(content, "ui.stop_heading",
                               {{"rule", game::text(content, "ui.pause_on.mine_losing")}}));
        checks.same("the stop panel's line", losing.stop.line, losing.noticeText);
        checks.same("the stop panel's tab", losing.stop.tab, 1);
        checks.expect(row != model.log.end() && row->mark == game::text(content, "ui.log_stopped"),
                      "the notice's row in the log is not marked as stopping the clock");
    } else {
        checks.expect(losing.speed == game::Speed::Fastest && model.speed == 3,
                      "the clock stopped with the rule off");
        checks.same("days gone by with the rule off", losing.stopped - losing.opened, kDaysDue);
        checks.expect(!losing.stop.shown, "a stop panel with the clock running");
        checks.expect(row != model.log.end() && row->mark.empty(),
                      "the notice's row is marked though the rule is off");
        checks.expect(losing.stopped > losing.noticeDay + 1,
                      "the clock did not run on past the notice");
    }
}

// Every tab in both frames with the losing mine's plot selected, and after
// each of the Dashboard, Plots and Log tabs what it must show. Returns false
// when a tab fails its layout; `spill` takes the furthest the row's widest
// word runs past the Output cell, for the caller to judge.
bool probeLosingFrames(Rml::Context& context, game::App& app, LoggingSystem& system,
                       const LosingMine& losing, const LosingFigures& expected, const char* label,
                       Checks& checks, float& spill) {
    bool ok = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== {}, frame {}x{}{} ==\n", label, frame.width, frame.height,
                                 deck ? " (deck)" : "");
        for (std::size_t tab = 0; tab < kTabs.size(); ++tab) {
            const bool passed = probeTab(context, app, system, frame, tab, Popover::None);
            ok = ok && passed;
            const std::string where = std::string(kTabs.at(tab)) + ' ' +
                                      std::to_string(frame.width) + 'x' +
                                      std::to_string(frame.height);
            if (tab == 0 || tab == 1) {
                spill = std::max(spill, checkLosingRow(context, where, expected, checks));
            }
            if (tab == 1) {
                checkLosingPanel(context, where, expected, checks);
            }
            if (tab == 5) {
                checkLosingLogLine(context, where, losing, checks);
            }
        }
    }
    return ok;
}

// The same game again with every stop rule off: the same line on the same
// day, and the clock runs on. Returns false when the seed no longer gets
// there.
bool probeRuleOff(game::App& app, const sim::Content& content, const LosingMine& stopped,
                  Checks& checks) {
    game::AutoPause never;
    never.enabled.fill(false);
    LosingMine ranOn;
    if (!playLosingMine(app, never, ranOn)) {
        return false;
    }
    std::cout << std::format(
        "\nlosing mine, rule off: clock {} on day {}, notice logged day {}\n  log: {}\n",
        ranOn.speed == game::Speed::Paused ? "stopped" : "running", ranOn.stopped, ranOn.noticeDay,
        ranOn.noticeText);
    checkNotice(app, content, ranOn, false, checks);
    checks.same("the notice's day with the rule off", ranOn.noticeDay, stopped.noticeDay);
    checks.same("the notice's line with the rule off", ranOn.noticeText, stopped.noticeText);
    return true;
}

// The losing-mine pass, on an app of its own because an app keeps the
// content it was made with. First with the stop rule as the only rule on:
// the notice, the clock, the figures in the model and on screen in both
// frames, then the same with a mothball order waiting, which widens the
// tables' action column and leaves the Output column at its narrowest.
// Then the game again with every rule off.
int probeLosingMine(Rml::Context& context, game::App& app, LoggingSystem& system,
                    const sim::Content& content) {
    Checks checks("LOSING");
    game::AutoPause rules;
    rules.enabled.fill(false);
    checks.expect(game::AutoPause::defaults().isEnabled(game::PauseOn::MineLosing),
                  "the losing-mine rule is not on by default");
    rules.set(game::PauseOn::MineLosing, true);
    LosingMine stopped;
    if (!playLosingMine(app, rules, stopped)) {
        return kScenarioFailure;
    }
    std::cout << std::format(
        "\nlosing mine: plot #{} opened day {}; rule on: clock {} on day {}, notice logged day "
        "{}\n  log: {}\n  status: {}\n",
        stopped.plot.value() + 1, stopped.opened,
        stopped.speed == game::Speed::Paused ? "stopped" : "running", stopped.stopped,
        stopped.noticeDay, stopped.noticeText, stopped.status);
    checkNotice(app, content, stopped, true, checks);
    checkNeedRows(app, "losing mine", checks);
    checkExactPrices(checks);
    const LosingFigures expected = simFigures(app.session(), stopped, checks);
    if (expected.earns.size() != kWidestFigure) {
        std::cout << std::format("SCENARIO (losing mine): the figure \"{}\" is not {} characters\n",
                                 expected.earns, kWidestFigure);
        return kScenarioFailure;
    }
    const auto modelFigures = [&](std::string_view when) {
        const game::UiModel& model = app.model();
        const auto row = std::ranges::find_if(model.owned, [&](const game::OwnedRow& owned) {
            return owned.index == static_cast<int>(stopped.plot.value());
        });
        checks.expect(row != model.owned.end(), std::format("{}: the plot has no row", when));
        if (row != model.owned.end()) {
            checkModelFigures(std::format("{}, row", when), row->figures, expected, checks);
        }
        checkModelFigures(std::format("{}, panel", when), model.panel.figures, expected, checks);
    };
    modelFigures("losing");
    float spill = kNothingMeasured;
    float spillWaiting = kNothingMeasured;
    bool laidOut =
        probeLosingFrames(context, app, system, stopped, expected, "losing mine", checks, spill);
    // A mothball order waiting: nothing has reached the sim, so the figures stand.
    app.session().submit(sim::SetMineState{{}, stopped.mine, 0});
    app.refreshNow();
    modelFigures("mothball ordered");
    laidOut = probeLosingFrames(context, app, system, stopped, expected,
                                "losing mine, mothball ordered", checks, spillWaiting) &&
              laidOut;
    // The figure must fit its cell, with a mine order waiting too: that is
    // where the column is narrowest (the deck frame's Plots tab).
    std::cout << std::format(
        "\nknife-edge: \"{}\" ({} characters) runs {:.1f}dp past the Output cell at most, "
        "{:.1f}dp with a mine order waiting (zero or less fits)\n",
        expected.earns, expected.earns.size(), spill, spillWaiting);
    if (std::max(spill, spillWaiting) > kSpillTolerance) {
        std::cout << "    the figure does not fit the Output cell\n";
        laidOut = false;
    }
    if (!probeRuleOff(app, content, stopped, checks)) {
        return kScenarioFailure;
    }
    if (!checks.ok()) {
        return kLosingFailure;
    }
    return laidOut ? 0 : kLayoutFailure;
}

// ---- the race for the program (spec 010) ----

constexpr int kRaceFailure = 8;
constexpr float kBarTolerance = 1.0F;  // a bar's fill against its track, in dp
// The race scenario delivers once a fifth of what the stage needs of some
// good is in stock, so the player's percentage is a figure well above zero.
constexpr std::int64_t kRaceStockShare = 5;
// The race is played against a rival with a long name (spec 011, AC-010),
// so the status strip and the race strip are laid out with one.
constexpr std::string_view kLongRivalName = "Northern Shield Consolidated Mining Group";
// How long the rival-researching game may run, after the player's first
// delivery, for the rival to start its stage research.
constexpr int kRivalResearchMaxDays = 2400;
// Then the days the player waits on with its research unstarted.
constexpr int kRivalLeadDays = 120;

// One company's line on the race strip, as the catalogue words the sim's
// figures for it.
struct RaceLine {
    std::string who;  // "You", or the rival's name
    std::string
        founding;       // "earliest founding Y17 M5 D3"; "" with no day or once the game has ended
    std::string stage;  // "0 of 3 stages complete"
    std::string percent;   // "12% delivered"
    std::string research;  // "research not started", "research due Y2 M3 D4"
    std::int64_t permille = 0;
};

// What the strip must read, worked out here from the sim's view and the
// catalogue, independently of the client's model.
struct RaceExpected {
    std::string rival;            // "Northfield could found the program in 6 y 120 d", or ""
    std::string deadline;         // "29 y 201 d to the deadline"
    std::string verdict;          // "Behind by 1,374 days", "Level", "We cannot found ..."
    std::vector<RaceLine> lines;  // the player first, then every rival
};

// The current stage's research in words: its due day while it is under way
// and the game is open (spec 011).
std::string researchWords(const sim::Content& content, const sim::ProgramPublicView& program,
                          bool open) {
    switch (program.researchState) {
        case sim::ProgramResearchState::NotStarted:
            return game::text(content, "ui.race_research_not_started");
        case sim::ProgramResearchState::InProgress:
            return open && program.researchDueDay
                       ? game::text(content, "ui.race_research_due",
                                    {{"day", game::formatDay(*program.researchDueDay)}})
                       : game::text(content, "ui.race_research_in_progress");
        case sim::ProgramResearchState::Done:
            return game::text(content, "ui.race_research_done");
    }
    return {};
}

// The rival the player races (spec 011): the solvent one with the earliest
// founding day, or none when every rival is bankrupt (edge case 4).
const sim::ProgramPublicView* leadingRival(const sim::CompanyView& view) {
    const sim::ProgramPublicView* leader = nullptr;
    for (const sim::ProgramPublicView& program : view.programs) {
        if (program.company == game::Session::kPlayer || !program.earliestFoundingDay ||
            view.companies[program.company.index()].bankrupt) {
            continue;
        }
        if (leader == nullptr || *program.earliestFoundingDay < *leader->earliestFoundingDay) {
            leader = &program;
        }
    }
    return leader;
}

// The verdict: the player's earliest founding day against the leading
// rival's, with the gap in days, Level on the same day (edge case 5); "cannot
// found in time" when the player's day is past the deadline. Stages and
// share delivered decide only where a day is missing.
std::string simVerdict(const sim::Content& content, const sim::CompanyView& view) {
    const sim::ProgramPublicView& mine = view.programs[game::Session::kPlayer.index()];
    if (mine.pastDeadline) {
        return game::text(content, "ui.race_cannot_found");
    }
    const sim::ProgramPublicView* rival = leadingRival(view);
    if (rival != nullptr && mine.earliestFoundingDay) {
        const std::int64_t gap = static_cast<std::int64_t>(*mine.earliestFoundingDay) -
                                 static_cast<std::int64_t>(*rival->earliestFoundingDay);
        if (gap == 0) {
            return game::text(content, "ui.race_level");
        }
        const std::int64_t days = std::abs(gap);
        return game::textPlural(content, gap > 0 ? "ui.race_behind_by" : "ui.race_ahead_by", days,
                                {{"days", game::formatCount(days)}});
    }
    bool ahead = true;
    bool behind = false;
    for (const sim::ProgramPublicView& program : view.programs) {
        if (program.company == game::Session::kPlayer) {
            continue;
        }
        const auto theirs = std::pair(program.stagesCompleted, program.progressPermille);
        const auto ours = std::pair(mine.stagesCompleted, mine.progressPermille);
        ahead = ahead && ours > theirs;
        behind = behind || ours < theirs;
    }
    const char* verdict = "ui.race_level";
    if (behind) {
        verdict = "ui.race_behind";
    } else if (ahead) {
        verdict = "ui.race_ahead";
    }
    return game::text(content, verdict);
}

RaceExpected simRace(const game::Session& session) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const bool open = !view.outcome.ended;
    RaceExpected expected;
    const sim::Day left = view.deadlineDay > view.day ? view.deadlineDay - view.day : 0;
    expected.deadline = game::text(content, "ui.race_deadline",
                                   {{"years", std::to_string(left / sim::kDaysPerYear)},
                                    {"days", std::to_string(left % sim::kDaysPerYear)}});
    const std::string total = std::to_string(content.program.stages.size());
    for (const sim::ProgramPublicView& program : view.programs) {
        RaceLine line;
        line.who = program.company == game::Session::kPlayer
                       ? game::text(content, "ui.race_you")
                       : view.companies[program.company.index()].name;
        if (open && program.earliestFoundingDay) {
            line.founding = game::text(content, "ui.race_founding_day",
                                       {{"day", game::formatDay(*program.earliestFoundingDay)}});
        }
        line.stage =
            game::text(content, "ui.race_stage",
                       {{"done", std::to_string(program.stagesCompleted)}, {"total", total}});
        line.percent = game::text(content, "ui.race_progress",
                                  {{"percent", std::to_string(program.progressPermille / 10)}});
        line.research = researchWords(content, program, open);
        line.permille = program.progressPermille;
        expected.lines.push_back(line);
    }
    const sim::ProgramPublicView* rival = leadingRival(view);
    if (open && rival != nullptr && *rival->earliestFoundingDay < view.deadlineDay) {
        const sim::Day due = *rival->earliestFoundingDay;
        expected.rival = game::text(
            content, "ui.race_rival_could_found",
            {{"company", view.companies[rival->company.index()].name},
             {"time", game::formatTimeFromNow(content, due > view.day ? due - view.day : 0)}});
    }
    expected.verdict = open ? simVerdict(content, view) : std::string();
    return expected;
}

std::string onlyText(Rml::Element* root, std::string_view tag, std::string_view cls) {
    const std::vector<Rml::Element*> found = visibleElements(root, tag, cls);
    return found.size() == 1 ? visibleText(found.front()) : std::format("<{} found>", found.size());
}

// The status strip under the header, on whatever tab is showing: the time
// before the leading rival could found the program when that is before the
// deadline, the time left and the verdict.
void checkRaceItem(Rml::Context& context, std::string_view where, const RaceExpected& expected,
                   Checks& checks) {
    settle(context);
    Rml::Element* strip = context.GetDocument(0)->GetElementById("strip");
    checks.same(std::format("{}: the status strip's rival", where),
                expected.rival.empty()
                    ? std::format("<{} found>", visibleElements(strip, "span", "couldfound").size())
                    : onlyText(strip, "span", "couldfound"),
                expected.rival.empty() ? std::string("<0 found>") : expected.rival);
    checks.same(std::format("{}: the status strip's deadline", where),
                onlyText(strip, "span", "deadline"), expected.deadline);
    checks.same(std::format("{}: the status strip's verdict", where),
                onlyText(strip, "span", "verdict"), expected.verdict);
}

// The Dashboard's race strip: the deadline, the verdict in words, and a line
// a company with its stage count, a bar and the bar's figure.
void checkRaceStrip(Rml::Context& context, std::string_view where, const RaceExpected& expected,
                    Checks& checks) {
    settle(context);
    Rml::ElementDocument* doc = context.GetDocument(0);
    Rml::Element* strip = doc->GetElementById("race");
    checks.expect(strip != nullptr && strip->IsVisible(true),
                  std::format("{}: no race strip on the Dashboard", where));
    if (strip == nullptr || !strip->IsVisible(true)) {
        return;
    }
    checks.same(std::format("{}: the strip's deadline", where), onlyText(strip, "span", "deadline"),
                expected.deadline);
    checks.same(std::format("{}: the strip's verdict", where), onlyText(strip, "span", "verdict"),
                expected.verdict);
    const std::vector<Rml::Element*> rows = visibleElements(strip, "div", "rrow");
    checks.same(std::format("{}: companies on the strip", where), rows.size(),
                expected.lines.size());
    std::string shown;
    for (std::size_t i = 0; i < rows.size() && i < expected.lines.size(); ++i) {
        const RaceLine& line = expected.lines[i];
        const std::string who = std::format("{}: {}", where, line.who);
        checks.same(who + ": name", onlyText(rows[i], "span", "nm"), line.who);
        checks.same(
            who + ": earliest founding",
            line.founding.empty()
                ? std::format("<{} found>", visibleElements(rows[i], "span", "founding").size())
                : onlyText(rows[i], "span", "founding"),
            line.founding.empty() ? std::string("<0 found>") : line.founding);
        checks.same(who + ": stages", onlyText(rows[i], "span", "stage"), line.stage);
        checks.same(who + ": percentage", onlyText(rows[i], "span", "pct"), line.percent);
        checks.same(who + ": research", onlyText(rows[i], "span", "research"), line.research);
        const std::vector<Rml::Element*> tracks = visibleElements(rows[i], "div", "bar");
        float track = 0.0F;
        float fill = 0.0F;
        if (tracks.size() == 1 && tracks.front()->GetNumChildren() == 1) {
            track = tracks.front()->GetBox().GetSize(Rml::BoxArea::Content).x;
            fill = tracks.front()->GetChild(0)->GetBox().GetSize(Rml::BoxArea::Border).x;
        }
        const float want = track * static_cast<float>(line.permille) / 1000.0F;
        checks.expect(track > 0.0F && std::abs(fill - want) <= kBarTolerance,
                      std::format("{}: the bar is {:.1f}dp of {:.1f}dp, expected {:.1f}dp", who,
                                  fill, track, want));
        shown += std::format("\n      {}: {}; {}; bar {:.1f}dp of {:.1f}dp; {}; {}", line.who,
                             line.founding, line.stage, fill, track, line.percent, line.research);
    }
    std::cout << std::format("    race {}: \"{}\" \"{}\", {}{}\n", where, expected.rival,
                             expected.deadline, expected.verdict, shown);
}

// How much room the header has left: the width of the gap between its
// figures and its buttons.
float headerSlack(Rml::Context& context) {
    const std::vector<Rml::Element*> gaps =
        visibleElements(context.GetDocument(0)->GetElementById("header"), "div", "grow");
    return gaps.empty() ? 0.0F : gaps.front()->GetBox().GetSize(Rml::BoxArea::Border).x;
}

// Every tab in both frames, with the status strip's item checked on each and
// the race strip on the Dashboard.
bool probeRaceFrames(Rml::Context& context, game::App& app, LoggingSystem& system,
                     const RaceExpected& expected, const char* label, Checks& checks) {
    bool ok = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== {}, frame {}x{}{} ==\n", label, frame.width, frame.height,
                                 deck ? " (deck)" : "");
        for (std::size_t tab = 0; tab < kTabs.size(); ++tab) {
            const bool passed = probeTab(context, app, system, frame, tab, Popover::None);
            ok = ok && passed;
            const std::string where = std::string(kTabs.at(tab)) + ' ' +
                                      std::to_string(frame.width) + 'x' +
                                      std::to_string(frame.height);
            checkRaceItem(context, where, expected, checks);
            if (tab == 0) {
                checkRaceStrip(context, where, expected, checks);
                Rml::Element* strip = context.GetDocument(0)->GetElementById("strip");
                std::cout << std::format(
                    "    header {}: {:.0f}dp to spare; status strip {:.0f}dp tall\n", where,
                    headerSlack(context), strip->GetBox().GetSize(Rml::BoxArea::Border).y);
            }
        }
    }
    return ok;
}

// ---- the Program tab (spec 010) ----

constexpr int kProgramFailure = 10;
constexpr int kProgramTab = 6;

// One requirement's row, as the catalogue words the sim's figures for it.
struct NeedLine {
    sim::ResourceId resource;
    std::string good;
    std::string needs;
    std::string delivered;
    std::string stock;
    std::string still;
    std::int64_t deliverable = 0;  // the lesser of the stock and what is still needed
    std::string button;            // "Deliver 12,000 t", or "Deliver" with nothing to deliver
    std::string note;              // under a Deliver button that is off
    std::string locked;            // what unlocks a good the company cannot dig, or ""
    bool waiting = false;          // a delivery of this good is waiting for the clock
};

// Where one good comes from on the Program tab (spec 011, BR-006).
struct WhereLine {
    std::string good;
    std::string regions;
    std::string research;
    std::string mines;
    std::string surveys;
};

std::string joined(const std::vector<std::string>& parts, std::string_view between) {
    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : std::string(between)) + part;
    }
    return out;
}

// What the Program tab must read, worked out here from the sim's view, the
// session's queue and the catalogue, independently of the client's model.
struct ProgramExpected {
    std::string title;
    std::string intro;
    std::vector<std::pair<std::string, std::string>> stages;  // heading, then state
    std::string cover;                                        // the cover-costs warning, or ""
    bool complete = false;                                    // every stage is done
    std::string done;
    // The current stage; empty when the program is complete.
    std::string heading;
    std::vector<NeedLine> needs;
    std::string cost;
    std::string research;
    bool researchOpen = false;     // not started: Start research stands, live or off
    bool researchLive = false;     // the Research tab would start it too
    bool researchWaiting = false;  // its order is waiting for the clock
    std::string researchButton;
    bool ready = false;  // the sim would accept the stage's completion today
    bool completeWaiting = false;
    std::string completeButton;
    std::string blocked;  // the lines saying what is missing, " | " between them
    std::string readyLine;
    // The chips of the three orders, and Cancel.
    std::string deliverChip;
    std::string researchChip;
    std::string completeChip;
    std::string cancel;
    // Keeping pace (spec 011, BR-004), as the catalogue words the view.
    std::string pace;  // the last day to start the research, that it has passed, or ""
    bool paceWarning = false;
    std::string lab;  // the lab is busy elsewhere and free on a day, or ""
    // Where each good an incomplete stage needs comes from (BR-006).
    std::string whereHeading;
    std::vector<WhereLine> where;
};

// The regions that can hold a good, each with its odds, as data/regions.json
// gives them.
std::vector<std::string> simRegionOdds(const sim::Content& content, sim::ResourceId resource) {
    std::vector<std::string> regions;
    for (const sim::RegionDef& region : content.regions) {
        for (const sim::DepositPrior& prior : region.deposits) {
            if (prior.resource == resource && prior.presenceProb.raw() > 0) {
                const std::string odds =
                    std::to_string(prior.presenceProb.raw() / (sim::Fixed::kScale / 100)) + "%";
                regions.push_back(game::text(content, "ui.program_from_region_item",
                                             {{"region", region.name}, {"odds", odds}}));
            }
        }
    }
    return regions;
}

// Where one good comes from, built here from the content and the view as the
// player would read them off the data files and their own company.
WhereLine simWhere(const game::Session& session, sim::ResourceId resource) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const std::size_t r = resource.index();
    const sim::ResourceDef& good = content.resources[r];
    WhereLine line;
    line.good = good.name;
    const std::vector<std::string> regions = simRegionOdds(content, resource);
    if (!regions.empty()) {
        line.regions = game::text(content, "ui.program_from_regions",
                                  {{"resource", good.name}, {"regions", joined(regions, ", ")}});
    }
    if (good.unlockedBy.valid() &&
        std::ranges::find(view.completedTechs, good.unlockedBy) == view.completedTechs.end()) {
        line.research = game::text(
            content, "ui.program_from_research",
            {{"resource", good.name}, {"tech", content.techs[good.unlockedBy.index()].name}});
    }
    // A source as the view's missing-ore rule counts one (sim/src/view.cpp).
    std::vector<std::string> mines;
    for (const sim::MineView& mine : view.mines) {
        const bool open = mine.state == sim::MineState::Building ||
                          mine.state == sim::MineState::Operating ||
                          mine.state == sim::MineState::Mothballed;
        if (open && (mine.extractedTonnes[r] > 0 || mine.bestEstimateTonnes[r] > 0)) {
            mines.push_back(game::text(content, "ui.program_from_mine_item",
                                       {{"plot", std::format("#{}", mine.plot.value() + 1)},
                                        {"tonnes", game::formatTonnes(mine.lastOutputTonnes[r])}}));
        }
    }
    line.mines = mines.empty()
                     ? game::text(content, "ui.program_from_no_mines", {{"resource", good.name}})
                     : game::text(content, "ui.program_from_mines",
                                  {{"resource", good.name}, {"mines", joined(mines, "; ")}});
    // The latest complete survey of each plot.
    std::vector<std::string> plots;
    for (std::size_t i = 0; i < view.surveys.size(); ++i) {
        const sim::SurveyView& survey = view.surveys[i];
        const bool latest = survey.complete &&
                            std::none_of(view.surveys.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                         view.surveys.end(), [&](const sim::SurveyView& later) {
                                             return later.complete && later.plot == survey.plot;
                                         });
        if (latest && std::ranges::any_of(survey.estimates, [&](const sim::SurveyEstimateView& e) {
                return e.resource == resource && e.tonnesEstimate > 0;
            })) {
            plots.push_back(std::format("#{}", survey.plot.value() + 1));
        }
    }
    if (!plots.empty()) {
        line.surveys = game::text(content, "ui.program_from_surveys",
                                  {{"resource", good.name}, {"plots", joined(plots, ", ")}});
    }
    return line;
}

// The Needs you row for keeping pace (spec 011, AC-005): its title while the
// view warns, else "".
std::string simPaceNeed(const game::Session& session) {
    const sim::Content& content = session.content();
    const sim::ProgramView& program = session.view().program;
    if (!program.paceWarning || !program.stage) {
        return {};
    }
    const std::string number = std::to_string(program.stage->index + 1);
    return program.lastStartDay
               ? game::text(content, "ui.need_pace_title",
                            {{"number", number}, {"day", game::formatDay(*program.lastStartDay)}})
               : game::text(content, "ui.need_pace_passed_title", {{"number", number}});
}

// The pace row stands under Needs you exactly while the view warns, and its
// button goes to the Program tab.
void checkPaceNeed(game::App& app, const std::string& where, Checks& checks) {
    const std::string title = simPaceNeed(app.session());
    const game::NeedRow* row = nullptr;
    for (const game::NeedRow& need : app.model().needs) {
        if (need.action == 6) {
            row = &need;
        }
    }
    checks.same(where + ": the pace row under Needs you", row != nullptr ? row->title : "", title);
    std::cout << std::format("    {}: pace row \"{}\"{}\n", where, title,
                             row != nullptr ? " / " + row->sub : "");
}

// ---- a missing ore (spec 011, BR-007, BR-008) ----

constexpr int kPlotsTab = 1;
constexpr int kOpenPlotsAction = 7;

// The marks a listed plot carries, built here from the view and
// data/regions.json: each missing ore the plot's region can hold, as
// "survey shows" when the player's latest complete survey of the plot shows
// it there and "may hold" otherwise.
struct PlotMarks {
    std::string mayHold;
    std::string surveyShows;
};

PlotMarks simPlotMarks(const game::Session& session, const sim::PlotView& plot) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const sim::RegionDef& region = content.regions[plot.region.index()];
    const sim::SurveyView* latest = nullptr;
    for (const sim::SurveyView& survey : view.surveys) {
        if (survey.complete && survey.plot == plot.id) {
            latest = &survey;
        }
    }
    std::vector<std::string> may;
    std::vector<std::string> shows;
    for (std::size_t r = 0; r < view.program.missingOres.size(); ++r) {
        const sim::ResourceId resource(static_cast<std::uint32_t>(r));
        const bool canHold =
            std::ranges::any_of(region.deposits, [&](const sim::DepositPrior& prior) {
                return prior.resource == resource && prior.presenceProb.raw() > 0;
            });
        if (view.program.missingOres[r] == sim::MissingOre::None || !canHold) {
            continue;
        }
        const bool shown =
            latest != nullptr &&
            std::ranges::any_of(latest->estimates, [&](const sim::SurveyEstimateView& e) {
                return e.resource == resource && e.tonnesEstimate > 0;
            });
        (shown ? shows : may).push_back(content.resources[r].name);
    }
    PlotMarks marks;
    if (!may.empty()) {
        marks.mayHold =
            game::text(content, "ui.plot_mark_may_hold", {{"resource", joined(may, ", ")}});
    }
    if (!shows.empty()) {
        marks.surveyShows =
            game::text(content, "ui.plot_mark_survey_shows", {{"resource", joined(shows, ", ")}});
    }
    return marks;
}

// One Needs you row for each ore the view marks Soon, and its button opens
// the Plots tab.
void checkOreNeeds(game::App& app, const std::string& where, Checks& checks) {
    const sim::Content& content = app.session().content();
    const std::vector<sim::MissingOre>& missing = app.session().view().program.missingOres;
    std::vector<std::string> expected;
    for (std::size_t r = 0; r < missing.size(); ++r) {
        if (missing[r] == sim::MissingOre::Soon) {
            const sim::ResourceId resource(static_cast<std::uint32_t>(r));
            expected.push_back(
                game::text(content, "ui.need_ore_title",
                           {{"resource", content.resources[r].name}}) +
                " / " +
                game::text(content, "ui.need_ore_sub",
                           {{"regions", joined(simRegionOdds(content, resource), ", ")}}));
        }
    }
    std::vector<std::string> shown;
    for (const game::NeedRow& need : app.model().needs) {
        if (need.action == kOpenPlotsAction) {
            shown.push_back(need.title + " / " + need.sub);
        }
    }
    checks.same(where + ": the missing-ore rows under Needs you", joined(shown, " | "),
                joined(expected, " | "));
    std::cout << std::format("    {}: {} missing-ore rows\n", where, shown.size());
}

// Every for-sale row's marks, in the model and on the laid-out Plots tab,
// against simPlotMarks; then the panel of a marked plot, the strongest
// first. Returns how many rows carry the stronger mark.
std::size_t checkPlotMarks(Rml::Context& context, game::App& app, const std::string& where,
                           Checks& checks) {
    const game::Session& session = app.session();
    app.setTab(kPlotsTab);
    app.refreshNow();
    settle(context);
    const std::vector<game::SaleRow>& rows = app.model().for_sale;
    const std::vector<Rml::Element*> shown =
        visibleElements(context.GetDocument(0)->GetElementById("content"), "tr", "row");
    checks.expect(shown.size() >= rows.size(), where + ": fewer rows on the tab than for sale");
    std::size_t marked = 0;
    std::size_t strong = 0;
    int pick = -1;
    for (std::size_t i = 0; i < rows.size() && i < shown.size(); ++i) {
        const PlotMarks marks =
            simPlotMarks(session, session.view().plots[static_cast<std::size_t>(rows[i].index)]);
        const std::string plot = where + ": plot " + rows[i].name;
        checks.same(plot + ": may hold", rows[i].may_hold, marks.mayHold);
        checks.same(plot + ": survey shows", rows[i].survey_shows, marks.surveyShows);
        const std::string text = visibleText(shown[i]);
        for (const std::string& mark : {marks.mayHold, marks.surveyShows}) {
            checks.expect(text.find(mark) != std::string::npos,
                          plot + ": its row does not read \"" + mark + "\"");
        }
        marked += marks.mayHold.empty() && marks.surveyShows.empty() ? 0U : 1U;
        strong += marks.surveyShows.empty() ? 0U : 1U;
        if (!marks.surveyShows.empty() || (pick < 0 && !marks.mayHold.empty())) {
            pick = rows[i].index;
        }
    }
    if (pick >= 0) {
        app.selectPlot(pick);
        app.refreshNow();
        settle(context);
        const PlotMarks marks =
            simPlotMarks(session, session.view().plots[static_cast<std::size_t>(pick)]);
        const game::PlotPanel& panel = app.model().panel;
        checks.same(where + ": the panel's may hold", panel.may_hold, marks.mayHold);
        checks.same(where + ": the panel's survey shows", panel.survey_shows, marks.surveyShows);
        const std::string text = visibleText(context.GetDocument(0)->GetElementById("content"));
        for (const std::string& mark : {marks.mayHold, marks.surveyShows}) {
            checks.expect(text.find(mark) != std::string::npos,
                          where + ": the plot panel does not read \"" + mark + "\"");
        }
    }
    std::cout << std::format("    {}: {} of {} plots for sale marked, {} by a survey\n", where,
                             marked, rows.size(), strong);
    return strong;
}

// The Plots tab at both frames with its marks checked in each, and the
// Needs you rows for the ores missing soon. Returns how many rows carry the
// stronger mark, or nothing when a frame did not lay out.
std::optional<std::size_t> probeOreFrames(Rml::Context& context, game::App& app,
                                          LoggingSystem& system, const char* label,
                                          Checks& checks) {
    bool ok = true;
    std::size_t strong = 0;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== {}, frame {}x{}{} ==\n", label, frame.width, frame.height,
                                 deck ? " (deck)" : "");
        ok = probeTab(context, app, system, frame, kPlotsTab, Popover::None) && ok;
        const std::string where =
            "plots " + std::to_string(frame.width) + 'x' + std::to_string(frame.height);
        strong = checkPlotMarks(context, app, where, checks);
    }
    checkOreNeeds(app, label, checks);
    checkNeedRows(app, label, checks);
    return ok ? std::optional<std::size_t>(strong) : std::nullopt;
}

// Nothing is marked and no ore row stands once the game is over (edge case
// 10): the view's missing ores are all None.
void checkNoOreMarks(game::App& app, Checks& checks) {
    app.refreshNow();
    const game::UiModel& model = app.model();
    checks.expect(std::ranges::all_of(app.session().view().program.missingOres,
                                      [](sim::MissingOre m) { return m == sim::MissingOre::None; }),
                  "the view marks an ore missing after the game is over");
    checks.expect(std::ranges::all_of(model.for_sale,
                                      [](const game::SaleRow& row) {
                                          return row.may_hold.empty() && row.survey_shows.empty();
                                      }),
                  "a plot for sale is marked after the game is over");
    checks.expect(model.panel.may_hold.empty() && model.panel.survey_shows.empty(),
                  "the plot panel carries a mark after the game is over");
    checks.expect(
        std::ranges::none_of(
            model.needs, [](const game::NeedRow& need) { return need.action == kOpenPlotsAction; }),
        "a missing-ore row stands after the game is over");
}

// ---- a standing bid (spec 011, BR-013) ----

constexpr int kBidFailure = 15;

// A new game with the player's bid standing on the cheapest listed plot, at
// both frames: the plot's for-sale row and its panel read the bid, the panel
// labels a second bid as replacing it, and no other row reads a bid.
int probeStandingBid(Rml::Context& context, game::App& app, LoggingSystem& system,
                     const sim::Content& content) {
    Checks checks("BID");
    app.startGame(kScenarioSeed);
    game::Session& session = app.session();
    game::AutoPause never;
    never.enabled.fill(false);
    session.setAutoPause(never);
    session.submit(sim::TakeLoan{{}, content.balance.loanMinPrincipal * 4});
    const sim::PlotId target = cheapestListed(session.view());
    if (!target.valid()) {
        std::cout << "BID: no plot is listed on day 0\n";
        return kScenarioFailure;
    }
    const sim::Money bid = session.view().plots[target.index()].askPrice;
    session.submit(sim::BidOnPlot{{}, target, bid});
    session.stepOneDay();
    const sim::PlotView& plot = session.view().plots[target.index()];
    if (!plot.auction || plot.auction->myBid != bid) {
        std::cout << std::format("BID: the bid on plot {} does not stand on day {}\n",
                                 target.value(), session.day());
        return kScenarioFailure;
    }
    const std::string stands =
        game::text(content, "ui.bid_stands", {{"amount", game::formatMoney(bid)}});
    const std::string replaces =
        game::text(content, "ui.bid_replaces", {{"amount", game::formatMoney(bid)}});
    bool laidOut = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== standing bid, frame {}x{}{} ==\n", frame.width, frame.height,
                                 deck ? " (deck)" : "");
        app.selectPlot(static_cast<int>(target.value()));
        laidOut = probeTab(context, app, system, frame, kPlotsTab, Popover::None) && laidOut;
        app.refreshNow();
        settle(context);
        const std::string where = std::format("bid {}x{}", frame.width, frame.height);
        Rml::Element* root = context.GetDocument(0)->GetElementById("content");
        const std::vector<game::SaleRow>& rows = app.model().for_sale;
        const std::vector<Rml::Element*> shown = visibleElements(root, "tr", "row");
        checks.expect(shown.size() >= rows.size(), where + ": fewer rows on the tab than for sale");
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const bool mine = rows[i].index == static_cast<int>(target.value());
            checks.same(where + ": plot " + rows[i].name + "'s bid", rows[i].bid_stands,
                        mine ? stands : std::string());
            if (mine && i < shown.size()) {
                checks.expect(visibleText(shown[i]).find(stands) != std::string::npos,
                              where + ": the plot's row does not read \"" + stands + "\"");
            }
        }
        const game::PlotPanel& panel = app.model().panel;
        checks.same(where + ": the panel's bid", panel.bid_stands, stands);
        checks.same(where + ": the panel's second bid", panel.bid_replaces, replaces);
        const std::string text = visibleText(root);
        for (const std::string& line : {stands, replaces}) {
            checks.expect(text.find(line) != std::string::npos,
                          where + ": the plot panel does not read \"" + line + "\"");
        }
    }
    std::cout << std::format("\nstanding bid on plot {}, day {}: \"{}\"; \"{}\"\n", target.value(),
                             session.day(), stands, replaces);
    if (!checks.ok()) {
        return kBidFailure;
    }
    return laidOut ? 0 : kLayoutFailure;
}

template <typename T, typename Matches>
bool orderWaiting(const game::Session& session, Matches matches) {
    return std::ranges::any_of(session.pendingOrders(), [&](const game::PendingOrder& order) {
        const auto* command = std::get_if<T>(&order.command);
        return command != nullptr && matches(*command);
    });
}

std::string stageHeading(const sim::Content& content, std::size_t stage) {
    const std::string number = std::to_string(stage + 1);
    return game::text(content, "ui.program_stage_heading",
                      {{"number", number},
                       {"name", game::text(content, "ui.program_stage_" + number + "_name")}});
}

NeedLine simNeed(const game::Session& session, const sim::ProgramRequirementView& need) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const sim::ResourceDef& good = content.resources[need.resource.index()];
    NeedLine line;
    line.resource = need.resource;
    line.good = good.name;
    line.needs = game::formatTonnes(need.neededTonnes);
    line.delivered = game::formatTonnes(need.deliveredTonnes);
    line.stock = game::formatTonnes(need.stockTonnes);
    const std::int64_t still = std::max<std::int64_t>(need.neededTonnes - need.deliveredTonnes, 0);
    line.still = game::formatTonnes(still);
    line.deliverable = std::max<std::int64_t>(std::min(need.stockTonnes, still), 0);
    line.button = line.deliverable > 0
                      ? game::text(content, "ui.program_deliver_amount",
                                   {{"tonnes", game::formatTonnes(line.deliverable)}})
                      : game::text(content, "ui.program_deliver");
    line.note =
        line.deliverable > 0 ? std::string() : game::text(content, "ui.program_nothing_to_deliver");
    if (good.unlockedBy.valid() &&
        std::ranges::find(view.completedTechs, good.unlockedBy) == view.completedTechs.end()) {
        line.locked = game::text(
            content, "ui.program_blocked_locked",
            {{"resource", good.name}, {"tech", content.techs[good.unlockedBy.index()].name}});
    }
    const sim::ResourceId resource = need.resource;
    line.waiting = orderWaiting<sim::DeliverToProgram>(
        session, [&](const sim::DeliverToProgram& order) { return order.resource == resource; });
    return line;
}

std::string simResearchState(const sim::Content& content, const sim::ProgramStageView& stage) {
    switch (stage.researchState) {
        case sim::ProgramResearchState::NotStarted:
            return game::text(content, "ui.program_research_not_started");
        case sim::ProgramResearchState::InProgress:
            return game::text(
                content, "ui.program_research_in_progress",
                {{"day", std::to_string(stage.activeResearch ? stage.activeResearch->completesDay
                                                             : sim::Day{0})}});
        case sim::ProgramResearchState::Done:
            return game::text(content, "ui.program_research_done");
    }
    return {};
}

// What is missing for the stage to be completed, in the order the sim tests
// it (sim/src/program.cpp completionBlocker): research, deliveries, cash.
std::string simBlocked(const sim::Content& content, const sim::CompanyView& view,
                       const sim::ProgramStageView& stage) {
    std::string blocked;
    const auto add = [&](const std::string& line) {
        blocked += (blocked.empty() ? "" : " | ") + line;
    };
    if (stage.researchState != sim::ProgramResearchState::Done) {
        add(game::text(content, "ui.program_blocked_research"));
    }
    if (std::ranges::any_of(stage.requirements, [](const sim::ProgramRequirementView& need) {
            return need.deliveredTonnes < need.neededTonnes;
        })) {
        add(game::text(content, "ui.program_blocked_deliveries"));
    }
    if (stage.cost > view.cash - view.reservedCash) {
        add(game::text(content, "ui.program_blocked_cash",
                       {{"amount", game::formatMoney(stage.cost)}}));
    }
    return blocked;
}

ProgramExpected simProgram(game::App& app) {
    const game::Session& session = app.session();
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const sim::ProgramView& program = view.program;
    ProgramExpected expected;
    expected.title = game::text(content, "ui.program_title");
    expected.intro = game::text(content, "ui.program_intro");
    expected.deliverChip = game::text(content, "ui.pending_deliver");
    expected.researchChip = game::text(content, "ui.pending_research");
    expected.completeChip = game::text(content, "ui.pending_complete_stage");
    expected.cancel = game::text(content, "ui.cancel");
    for (std::size_t i = 0; i < content.program.stages.size(); ++i) {
        const char* state = "to come";
        if (i < program.stagesCompleted) {
            state = "complete";
        } else if (i == program.stagesCompleted) {
            state = "current";
        }
        expected.stages.emplace_back(stageHeading(content, i), state);
    }
    if (!program.stage) {
        expected.complete = true;
        expected.done = game::text(content, "ui.program_complete");
        return expected;
    }
    // Cover costs as it will stand once a waiting order for it is given.
    bool cover = view.coverCosts;
    for (const game::PendingOrder& order : session.pendingOrders()) {
        if (const auto* set = std::get_if<sim::SetCoverCosts>(&order.command)) {
            cover = set->enabled != 0;
        }
    }
    expected.cover = cover ? game::text(content, "ui.program_cover_costs_warning") : "";
    const sim::ProgramStageView& stage = *program.stage;
    expected.heading = stageHeading(content, stage.index);
    for (const sim::ProgramRequirementView& need : stage.requirements) {
        expected.needs.push_back(simNeed(session, need));
    }
    const std::string cost = game::formatMoney(stage.cost);
    expected.cost = game::text(content, "ui.program_cost", {{"amount", cost}});
    expected.research = simResearchState(content, stage);
    expected.researchOpen = stage.researchState == sim::ProgramResearchState::NotStarted;
    // "As the Research tab does it": the project's own row there.
    expected.researchLive = app.model().techs.at(stage.research.index()).can_start;
    const sim::TechId project = stage.research;
    expected.researchWaiting = orderWaiting<sim::StartResearch>(
        session, [&](const sim::StartResearch& order) { return order.tech == project; });
    expected.researchButton =
        game::text(content, "ui.program_start_research",
                   {{"amount", game::formatMoney(content.techs[project.index()].cost)}});
    expected.ready = stage.ready;
    expected.completeWaiting = orderWaiting<sim::CompleteProgramStage>(
        session, [](const sim::CompleteProgramStage&) { return true; });
    expected.completeButton = game::text(content, "ui.program_complete_stage", {{"amount", cost}});
    if (stage.ready) {
        expected.readyLine = game::text(content, "ui.program_stage_ready");
    } else {
        expected.blocked = simBlocked(content, view, stage);
    }
    if (program.lastStartDay) {
        expected.pace = game::text(content, "ui.program_last_start",
                                   {{"day", game::formatDay(*program.lastStartDay)}});
    } else if (program.lastStartPassed) {
        expected.pace = game::text(content, "ui.program_last_start_passed");
    }
    expected.paceWarning = program.paceWarning;
    if (program.labFreeDay) {
        expected.lab = game::text(content, "ui.program_lab_busy",
                                  {{"day", game::formatDay(*program.labFreeDay)}});
    }
    expected.whereHeading = game::text(content, "ui.program_where_from");
    std::vector<sim::ResourceId> goods;
    for (std::size_t i = program.stagesCompleted; i < content.program.stages.size(); ++i) {
        for (const sim::ProgramRequirement& need : content.program.stages[i].requirements) {
            if (std::ranges::find(goods, need.resource) == goods.end()) {
                goods.push_back(need.resource);
                expected.where.push_back(simWhere(session, need.resource));
            }
        }
    }
    return expected;
}

// How many buttons under `root` read `text` and are live (or off).
std::size_t buttonsReading(Rml::Element* root, const std::string& text, bool live) {
    std::size_t found = 0;
    for (Rml::Element* button : visibleElements(root, "button", "", text)) {
        found += button->IsClassSet("disabled") != live ? 1U : 0U;
    }
    return found;
}

// One control of the tab: its order's chip with Cancel beside it while the
// order waits, else its button, live or off, or neither.
struct ControlExpected {
    std::string button;
    bool waiting = false;
    bool live = false;
    bool off = false;
};

std::string checkProgramControl(Rml::Element* root, const std::string& where,
                                const ControlExpected& control, const std::string& chip,
                                const std::string& cancel, Checks& checks) {
    std::size_t chips = 0;
    std::size_t cancels = 0;
    for (Rml::Element* e : visibleElements(root, "span", "chip", chip)) {
        chips += e->IsClassSet("accent") ? 1U : 0U;
        cancels += cancelBeside(e, cancel) != nullptr ? 1U : 0U;
    }
    const auto count = [](bool on) { return std::size_t{on ? 1U : 0U}; };
    checks.same(where + ": \"" + chip + "\" chips", chips, count(control.waiting));
    checks.same(where + ": Cancel beside the chip", cancels, count(control.waiting));
    checks.same(where + ": live \"" + control.button + "\" buttons",
                buttonsReading(root, control.button, true),
                count(!control.waiting && control.live));
    checks.same(where + ": \"" + control.button + "\" buttons that are off",
                buttonsReading(root, control.button, false),
                count(!control.waiting && control.off));
    if (control.waiting) {
        return "waiting, with Cancel";
    }
    if (!control.live && !control.off) {
        return "not offered";
    }
    return std::format("\"{}\" {}", control.button, control.live ? "live" : "off");
}

std::string joinedText(const std::vector<Rml::Element*>& elements) {
    std::string out;
    for (Rml::Element* e : elements) {
        out += (out.empty() ? "" : " | ") + visibleText(e);
    }
    return out;
}

// Everything visible under `box` lies inside it. `inspect` holds elements to
// the pane that scrolls them, not to the table cell or the row they sit in,
// so a chip beside its Cancel could run out of its column unnoticed.
void checkInside(Rml::Element* box, const std::string& where, Checks& checks) {
    const float left = box->GetAbsoluteOffset(Rml::BoxArea::Border).x;
    const float right = rightEdge(box);
    for (int i = 0; i < box->GetNumChildren(); ++i) {
        Rml::Element* child = box->GetChild(i);
        if (!child->IsVisible(true) || rmlui_dynamic_cast<Rml::ElementText*>(child) != nullptr) {
            continue;
        }
        const float childLeft = child->GetAbsoluteOffset(Rml::BoxArea::Border).x;
        checks.expect(
            childLeft >= left - kSpillTolerance && rightEdge(child) <= right + kSpillTolerance,
            std::format("{}: {} \"{}\" spans {:.0f} to {:.0f}dp, outside {} at {:.0f} "
                        "to {:.0f}dp",
                        where, describe(child), visibleText(child).substr(0, 30), childLeft,
                        rightEdge(child), describe(box), left, right));
        checkInside(child, where, checks);
    }
}

// One requirement's row on screen against the sim's figures for it.
std::string checkNeedRow(Rml::Element* row, const std::string& where, const NeedLine& need,
                         const ProgramExpected& expected, Checks& checks) {
    const std::string at = where + ": " + need.good;
    checks.same(at + ": good", onlyText(row, "span", "gname"), need.good);
    const std::vector<Rml::Element*> figures = visibleElements(row, "td", "n");
    checks.same(at + ": figures in the row", figures.size(), std::size_t{4});
    if (figures.size() == 4) {
        checks.same(at + ": needs", visibleText(figures[0]), need.needs);
        checks.same(at + ": delivered", visibleText(figures[1]), need.delivered);
        checks.same(at + ": in stock", visibleText(figures[2]), need.stock);
        checks.same(at + ": still needed", visibleText(figures[3]), need.still);
    }
    checks.same(at + ": what unlocks it", joinedText(visibleElements(row, "span", "locked")),
                need.locked);
    const std::string control = checkProgramControl(
        row, at, {need.button, need.waiting, need.deliverable > 0, need.deliverable == 0},
        expected.deliverChip, expected.cancel, checks);
    checks.same(at + ": note under Deliver", joinedText(visibleElements(row, "span", "note")),
                need.waiting ? std::string() : need.note);
    for (Rml::Element* cell : visibleElements(row, "td", "")) {
        checkInside(cell, at, checks);
    }
    return std::format("\n      {}: needs {}, delivered {}, in stock {}, still needed {}; {}{}",
                       need.good, need.needs, need.delivered, need.stock, need.still, control,
                       need.locked.empty() ? "" : "; " + need.locked);
}

// The Program tab as the player reads it, against the sim: the three stages
// and their states, then the current stage's requirements, cost, research
// and controls, or the line that says the program is complete.
void checkProgramTab(Rml::Context& context, const std::string& where,
                     const ProgramExpected& expected, Checks& checks) {
    settle(context);
    Rml::Element* tab = context.GetDocument(0)->GetElementById("program");
    if (tab == nullptr || !tab->IsVisible(true)) {
        checks.expect(false, where + ": no Program tab on screen");
        return;
    }
    checks.same(where + ": title", onlyText(tab, "h1", ""), expected.title);
    checks.same(where + ": intro", onlyText(tab, "p", "intro"), expected.intro);
    const std::vector<Rml::Element*> cells = visibleElements(tab, "div", "pstage");
    checks.same(where + ": stages listed", cells.size(), expected.stages.size());
    std::string shown;
    for (std::size_t i = 0; i < cells.size() && i < expected.stages.size(); ++i) {
        const std::string at = std::format("{}: stage {}", where, i + 1);
        checks.same(at + ": heading", onlyText(cells[i], "span", "title"),
                    expected.stages[i].first);
        checks.same(at + ": state", onlyText(cells[i], "span", "chip"), expected.stages[i].second);
        shown += std::format("{}[{}: {}]", i == 0 ? "" : " ", expected.stages[i].first,
                             expected.stages[i].second);
    }
    checks.same(where + ": cover-costs warning", joinedText(visibleElements(tab, "span", "cover")),
                expected.cover);
    const std::vector<Rml::Element*> rows = visibleElements(tab, "tr", "need");
    checks.same(where + ": the program-complete line",
                joinedText(visibleElements(tab, "p", "done")), expected.done);
    checks.same(where + ": requirement rows", rows.size(), expected.needs.size());
    if (expected.complete) {
        checks.same(where + ": buttons with the program complete",
                    visibleElements(tab, "button", "").size(), std::size_t{0});
        std::cout << std::format("    {}: {}\n      \"{}\"\n", where, shown, expected.done);
        return;
    }
    checks.same(where + ": current stage", onlyText(tab, "span", "heading"), expected.heading);
    for (std::size_t i = 0; i < rows.size() && i < expected.needs.size(); ++i) {
        shown += checkNeedRow(rows[i], where, expected.needs[i], expected, checks);
    }
    checks.same(where + ": cost", onlyText(tab, "span", "cost"), expected.cost);
    checks.same(where + ": research", onlyText(tab, "span", "rstate"), expected.research);
    const std::vector<Rml::Element*> actions = visibleElements(tab, "div", "pactions");
    checks.same(where + ": action rows", actions.size(), std::size_t{1});
    if (actions.size() != 1) {
        return;
    }
    const std::string research =
        checkProgramControl(actions.front(), where + ": research",
                            {expected.researchButton, expected.researchWaiting,
                             expected.researchOpen && expected.researchLive,
                             expected.researchOpen && !expected.researchLive},
                            expected.researchChip, expected.cancel, checks);
    const std::string complete = checkProgramControl(
        actions.front(), where + ": completion",
        {expected.completeButton, expected.completeWaiting, expected.ready, !expected.ready},
        expected.completeChip, expected.cancel, checks);
    checks.same(where + ": what is missing", joinedText(visibleElements(tab, "span", "blocked")),
                expected.blocked);
    checks.same(where + ": the ready line", joinedText(visibleElements(tab, "span", "ready")),
                expected.readyLine);
    checkInside(actions.front(), where + ": actions", checks);
    for (Rml::Element* cell : cells) {
        checkInside(cell, where + ": stages", checks);
    }
    const std::vector<Rml::Element*> pace = visibleElements(tab, "span", "pace");
    checks.same(where + ": the last day to start", joinedText(pace), expected.pace);
    for (Rml::Element* line : pace) {
        checks.same(where + ": the pace line is a warning", line->IsClassSet("warn"),
                    expected.paceWarning);
    }
    checks.same(where + ": the busy lab", joinedText(visibleElements(tab, "span", "labbusy")),
                expected.lab);
    checks.same(where + ": where-from heading", onlyText(tab, "span", "wheading"),
                expected.whereHeading);
    const std::vector<Rml::Element*> sources = visibleElements(tab, "div", "wfrom");
    checks.same(where + ": where-from goods", sources.size(), expected.where.size());
    std::string from;
    for (std::size_t i = 0; i < sources.size() && i < expected.where.size(); ++i) {
        const WhereLine& line = expected.where[i];
        const std::string at = where + ": where " + line.good;
        checks.same(at + ": good", onlyText(sources[i], "span", "wgood"), line.good);
        checks.same(at + ": regions", joinedText(visibleElements(sources[i], "span", "regions")),
                    line.regions);
        checks.same(at + ": research", joinedText(visibleElements(sources[i], "span", "wresearch")),
                    line.research);
        checks.same(at + ": mines", joinedText(visibleElements(sources[i], "span", "wmines")),
                    line.mines);
        checks.same(at + ": surveys", joinedText(visibleElements(sources[i], "span", "wsurveys")),
                    line.surveys);
        checkInside(sources[i], at, checks);
        from += std::format("\n      {}: {} {} {} {}", line.good, line.regions, line.research,
                            line.mines, line.surveys);
    }
    std::cout << std::format("    {}: pace \"{}\"{}{}{}\n", where, expected.pace,
                             expected.paceWarning ? " (warning)" : "",
                             expected.lab.empty() ? "" : "; " + expected.lab, from);
    std::cout << std::format(
        "    {}: {}{}\n      {}; {}; research: {}; completion: {}\n      {}{}\n", where, shown,
        expected.cover.empty() ? "" : "\n      warning: " + expected.cover, expected.cost,
        expected.research, research, complete,
        expected.ready ? expected.readyLine : "missing: " + expected.blocked,
        joinedText(visibleElements(tab, "span", "rblocked")).empty()
            ? std::string()
            : "\n      research is off because: " +
                  joinedText(visibleElements(tab, "span", "rblocked")));
}

// The rail holds seven tabs, Program second, all of them inside it.
void checkRail(Rml::Context& context, const std::string& where, Checks& checks) {
    Rml::Element* rail = context.GetDocument(0)->GetElementById("rail");
    const std::vector<Rml::Element*> tabs = visibleElements(rail, "div", "tab");
    checks.same(where + ": tabs on the rail", tabs.size(), kTabs.size());
    if (tabs.size() != kTabs.size()) {
        return;
    }
    checks.expect(visibleElements(tabs[1], "span", "program").size() == 1,
                  where + ": the Program tab is not second on the rail");
    checks.expect(tabs[1]->IsClassSet("active"), where + ": the Program tab is not marked active");
    const float railBottom = rail->GetAbsoluteOffset(Rml::BoxArea::Content).y +
                             rail->GetBox().GetSize(Rml::BoxArea::Content).y;
    const float lastBottom = tabs.back()->GetAbsoluteOffset(Rml::BoxArea::Border).y +
                             tabs.back()->GetBox().GetSize(Rml::BoxArea::Border).y;
    checks.expect(lastBottom <= railBottom + 0.5F,
                  std::format("{}: the last tab ends at {:.0f}dp and the rail at {:.0f}dp", where,
                              lastBottom, railBottom));
    std::cout << std::format("    rail {}: {} tabs, the last ends at {:.0f}dp of {:.0f}dp\n", where,
                             tabs.size(), lastBottom, railBottom);
}

// The Program tab in both frames, laid out and checked against the sim.
bool probeProgramFrames(Rml::Context& context, game::App& app, LoggingSystem& system,
                        const char* label, Checks& checks) {
    bool ok = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== {}, frame {}x{}{} ==\n", label, frame.width, frame.height,
                                 deck ? " (deck)" : "");
        ok = probeTab(context, app, system, frame, kProgramTab, Popover::None) && ok;
        const std::string where =
            "program " + std::to_string(frame.width) + 'x' + std::to_string(frame.height);
        checkProgramTab(context, where, simProgram(app), checks);
        checkRail(context, where, checks);
    }
    return ok;
}

// ---- which projects can start (spec 010) ----

constexpr int kResearchTab = 3;

// Every project on the Research tab, against the sim: Start is offered
// exactly for the projects the view marks open (sim::CompanyView::
// researchOpen) while the lab is idle and the money left covers the cost.
// A project with its order already waiting shows its chip instead.
void checkResearchStartable(Rml::Context& context, game::App& app, const std::string& where,
                            Checks& checks) {
    const game::Session& session = app.session();
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    app.setTab(kResearchTab);
    app.refreshNow();
    settle(context);
    const std::vector<game::TechRow>& techs = app.model().techs;
    const std::vector<Rml::Element*> rows =
        visibleElements(context.GetDocument(0)->GetElementById("content"), "tr", "row");
    checks.same(where + ": projects in the model", techs.size(), content.techs.size());
    checks.same(where + ": projects on the Research tab", rows.size(), content.techs.size());
    checks.same(where + ": projects the view answers for", view.researchOpen.size(),
                content.techs.size());
    if (!checks.ok()) {
        return;
    }
    const sim::Money available =
        view.cash - view.reservedCash - session.committed() + session.pendingBorrowed();
    std::size_t open = 0;
    std::size_t startable = 0;
    std::size_t buttons = 0;
    for (std::size_t i = 0; i < content.techs.size(); ++i) {
        const sim::TechId tech(static_cast<std::uint32_t>(i));
        const bool waiting = orderWaiting<sim::StartResearch>(
            session, [&](const sim::StartResearch& order) { return order.tech == tech; });
        const bool isOpen = view.researchOpen[i] != 0;
        const bool expected = isOpen && !view.activeResearch.has_value() &&
                              content.techs[i].cost <= available && !waiting;
        const auto shown = std::ranges::count_if(
            visibleElements(rows[i], "button", ""),
            [](Rml::Element* button) { return visibleText(button) == "Start"; });
        const std::string project = where + ": " + content.techs[i].name;
        checks.same(project + ": can start", techs[i].can_start, expected);
        checks.same(project + ": Start buttons", static_cast<std::size_t>(shown),
                    std::size_t{expected ? 1U : 0U});
        checks.expect(visibleText(rows[i]).find(content.techs[i].name) != std::string::npos,
                      project + ": its row does not name it");
        open += isOpen ? 1 : 0;
        startable += expected ? 1 : 0;
        buttons += static_cast<std::size_t>(shown);
    }
    std::cout << std::format(
        "\n  research, {} ({}): {} projects, {} open to the sim, the lab {}, {} "
        "available; {} can start, {} Start buttons on the tab\n",
        where, app.model().day, content.techs.size(), open,
        view.activeResearch.has_value() ? "busy" : "idle", game::formatMoney(available), startable,
        buttons);
}

int programResult(const Checks& checks, bool laidOut) {
    if (!checks.ok()) {
        return kProgramFailure;
    }
    return laidOut ? 0 : kLayoutFailure;
}

// With a survey report in and a mine dug (spec 011, BR-006): the Program
// tab's "where from" lines name the plot the survey shows a good on, and the
// mine with its daily output.
int probeProgramSurveyed(Rml::Context& context, game::App& app, LoggingSystem& system) {
    Checks checks("PROGRAM");
    const ProgramExpected expected = simProgram(app);
    if (std::ranges::none_of(expected.where,
                             [](const WhereLine& line) { return !line.surveys.empty(); })) {
        std::cout << "SCENARIO (program): the survey report shows no good the program needs\n";
        return kScenarioFailure;
    }
    const bool laidOut =
        probeProgramFrames(context, app, system, "program, survey report in", checks);
    // The report on a plot still at auction shows an ore the program lacks:
    // the plot carries the stronger mark.
    const std::optional<std::size_t> strong =
        probeOreFrames(context, app, system, "plots, survey report in", checks);
    checks.expect(!strong || *strong > 0, "no plot for sale carries the survey's mark");
    return programResult(checks, laidOut && strong.has_value());
}

// A new game: nothing delivered, the research not started, and the stage's
// completion off with what is missing said.
int probeProgramFresh(Rml::Context& context, game::App& app, LoggingSystem& system) {
    Checks checks("PROGRAM");
    const sim::Content& content = app.session().content();
    const std::optional<sim::ProgramStageView> stage = app.session().view().program.stage;
    if (!stage || stage->index != 0) {
        std::cout << "SCENARIO (program): a new game is not on the first stage\n";
        return kScenarioFailure;
    }
    const ProgramExpected expected = simProgram(app);
    checks.expect(stage->researchState == sim::ProgramResearchState::NotStarted &&
                      std::ranges::all_of(stage->requirements,
                                          [](const sim::ProgramRequirementView& need) {
                                              return need.deliveredTonnes == 0;
                                          }),
                  "a new game has research or deliveries behind it");
    checks.expect(!expected.ready && expected.researchOpen, "a new game's stage is ready");
    // The one place Start research is held to the sim's own figures rather
    // than to the Research tab's row: an idle lab, a project the sim marks
    // open, and the cash for it.
    const sim::CompanyView& view = app.session().view();
    const sim::TechDef& project = content.techs[stage->research.index()];
    if (view.activeResearch || stage->research.index() >= view.researchOpen.size() ||
        view.researchOpen[stage->research.index()] == 0 ||
        project.cost > view.cash - view.reservedCash) {
        std::cout << "SCENARIO (program): a new company cannot start the first stage's project\n";
        return kScenarioFailure;
    }
    checks.expect(expected.researchLive,
                  "a new company with an idle lab and the cash cannot start the stage's project");
    checks.expect(expected.blocked.find(game::text(content, "ui.program_blocked_research")) !=
                          std::string::npos &&
                      expected.blocked.find(game::text(content, "ui.program_blocked_deliveries")) !=
                          std::string::npos,
                  "a new game is not missing both its research and its deliveries");
    checkResearchStartable(context, app, "new game", checks);
    checkPaceNeed(app, "new game", checks);
    // No mines yet: every ore the program needs is missing, and the land
    // that can hold one is marked.
    checks.expect(std::ranges::any_of(view.program.missingOres,
                                      [](sim::MissingOre m) { return m == sim::MissingOre::Soon; }),
                  "a new game misses no ore soon");
    const std::optional<std::size_t> strong =
        probeOreFrames(context, app, system, "plots, new game", checks);
    const bool laidOut = probeProgramFrames(context, app, system, "program, new game", checks);
    return programResult(checks, laidOut && strong.has_value());
}

// One delivery given through the tab, to compare with the sim a day later.
struct Delivery {
    sim::ResourceId resource;
    std::int64_t tonnes = 0;
    std::int64_t deliveredBefore = 0;
};

// Gives every delivery the current stage can take through its own Deliver
// button, takes the first back with its Cancel and gives it again. Nothing
// has reached the sim: the figures stand and nothing is committed.
std::vector<Delivery> deliverByButtons(Rml::Context& context, game::App& app, Checks& checks) {
    game::Session& session = app.session();
    const sim::Content& content = session.content();
    app.setTab(kProgramTab);
    app.refreshNow();
    const ProgramExpected before = simProgram(app);
    std::vector<Delivery> given;
    const std::optional<sim::ProgramStageView> stage = session.view().program.stage;
    for (std::size_t i = 0; stage && i < before.needs.size(); ++i) {
        const NeedLine& need = before.needs[i];
        if (need.deliverable > 0) {
            clickFirst(context, "button", "", need.button, checks);
            given.push_back(
                {need.resource, need.deliverable, stage->requirements[i].deliveredTonnes});
        }
    }
    const auto queued = [&] {
        return std::ranges::all_of(given, [&](const Delivery& delivery) {
            return orderWaiting<sim::DeliverToProgram>(
                session, [&](const sim::DeliverToProgram& order) {
                    return order.company == game::Session::kPlayer &&
                           order.resource == delivery.resource && order.tonnes == delivery.tonnes;
                });
        });
    };
    checks.expect(!given.empty() && session.pendingOrders().size() == given.size() && queued(),
                  std::format("the Deliver buttons did not queue the most each good can take "
                              "({} given, {} waiting)",
                              given.size(), session.pendingOrders().size()));
    checks.expect(!app.model().has_committed && session.committed() == sim::Money::zero(),
                  "a delivery waiting is counted as money committed");
    // Cancel beside the first chip, and the button is back.
    settle(context);
    const std::vector<Rml::Element*> chips =
        visibleElements(context.GetDocument(0), "span", "chip", before.deliverChip);
    click(context, chips.empty() ? nullptr : cancelBeside(chips.front(), before.cancel),
          "Cancel beside a delivery's chip", checks);
    checks.same("deliveries waiting after Cancel", session.pendingOrders().size(),
                given.empty() ? std::size_t{0} : given.size() - 1);
    checks.same("status after Cancel", app.model().status_line,
                game::text(content, "status.order_cancelled"));
    const auto first = std::ranges::find_if(
        before.needs, [](const NeedLine& need) { return need.deliverable > 0; });
    if (first != before.needs.end()) {
        clickFirst(context, "button", "", first->button, checks);
    }
    checks.expect(session.pendingOrders().size() == given.size() && queued(),
                  "the delivery taken back was not given again by its button");
    return given;
}

// The deliveries have been made: each requirement's delivered figure in the
// sim's view is up by what its button said.
void checkDelivered(const game::Session& session, const std::vector<Delivery>& given,
                    Checks& checks) {
    const std::optional<sim::ProgramStageView> stage = session.view().program.stage;
    for (const Delivery& delivery : given) {
        std::int64_t now = -1;
        for (const sim::ProgramRequirementView& need :
             stage ? stage->requirements : std::vector<sim::ProgramRequirementView>()) {
            now = need.resource == delivery.resource ? need.deliveredTonnes : now;
        }
        checks.same(std::format("{} delivered a day later",
                                session.content().resources[delivery.resource.index()].name),
                    now, delivery.deliveredBefore + delivery.tonnes);
    }
}

// The stage's research ordered by the tab's button, waiting with its cost
// committed, then under way a day later. Returns a failure code or 0.
int probeProgramResearch(Rml::Context& context, game::App& app, LoggingSystem& system,
                         Checks& checks, bool& laidOut) {
    game::Session& session = app.session();
    const sim::Content& content = session.content();
    app.setTab(kProgramTab);
    app.refreshNow();
    const ProgramExpected open = simProgram(app);
    const std::optional<sim::ProgramStageView> stage = session.view().program.stage;
    if (!stage || !open.researchOpen || !open.researchLive || !session.pendingOrders().empty()) {
        std::cout << std::format(
            "SCENARIO (program): the stage's research cannot be started ({})\n", app.model().day);
        return kScenarioFailure;
    }
    clickFirst(context, "button", "", open.researchButton, checks);
    const sim::TechId project = stage->research;
    checks.expect(orderWaiting<sim::StartResearch>(
                      session,
                      [&](const sim::StartResearch& order) {
                          return order.company == game::Session::kPlayer && order.tech == project;
                      }),
                  "Start research did not queue the stage's project");
    checks.same("Committed with the research waiting", app.model().committed,
                game::text(content, "ui.committed",
                           {{"amount", game::formatMoney(content.techs[project.index()].cost)}}));
    checkResearchStartable(context, app, "order waiting", checks);
    laidOut =
        probeProgramFrames(context, app, system, "program, research waiting", checks) && laidOut;
    session.stepOneDay();
    app.refreshNow();
    const std::optional<sim::ProgramStageView> after = session.view().program.stage;
    checks.expect(after && after->researchState == sim::ProgramResearchState::InProgress &&
                      after->activeResearch.has_value(),
                  "the stage's research is not under way a day after it was ordered");
    checkResearchStartable(context, app, "lab busy", checks);
    laidOut =
        probeProgramFrames(context, app, system, "program, research under way", checks) && laidOut;
    return 0;
}

// Starts the fixed game again and plays it, with the first stage's goods
// taken off their standing sell orders, to a mine with a fifth of what the stage needs of one of
// them in stock. Returns false with a reason when the fixed seed no longer gets there.
bool playToStock(game::App& app, const sim::Content& content) {
    const auto fail = [&](std::string_view what) {
        std::cout << std::format("SCENARIO (race): {} ({})\n", what, app.model().day);
        return false;
    };
    app.startGame(kScenarioSeed);
    game::Session& session = app.session();
    game::AutoPause never;
    never.enabled.fill(false);
    session.setAutoPause(never);
    const sim::PlotId site = cheapestListed(session.view());
    if (!site.valid()) {
        return fail("no plot is listed on day 0");
    }
    const sim::Money ask = session.view().plots[site.index()].askPrice;
    session.submit(sim::TakeLoan{{}, content.balance.loanMinPrincipal * 4});
    session.submit(sim::BidOnPlot{{}, site, ask + ask / 2});
    // A new company sells its ore as it is dug (the default standing order):
    // the stage's goods are held back, as a player racing would hold them.
    const std::optional<sim::ProgramStageView> stage = session.view().program.stage;
    if (!stage) {
        return fail("the content has no program");
    }
    for (const sim::ProgramRequirementView& need : stage->requirements) {
        session.submit(sim::SetSellPolicy{{}, need.resource, 0, 0, sim::Money::zero()});
    }
    bool building = false;
    const auto deliverable = [&] {
        const sim::ProgramView& program = session.view().program;
        return program.stage &&
               std::ranges::any_of(
                   program.stage->requirements, [](const sim::ProgramRequirementView& need) {
                       return need.stockTonnes * kRaceStockShare >= need.neededTonnes;
                   });
    };
    for (int day = 0; day < kScenarioMaxDays && !deliverable(); ++day) {
        session.stepOneDay();
        if (!building && session.view().plots[site.index()].owner == game::Session::kPlayer) {
            session.submit(sim::BuildMine{{}, site});
            building = true;
        }
    }
    app.refreshNow();
    return deliverable() || fail("the player has no ore in stock that the first stage needs");
}

// The race pass. The fixed game is played to a mine with ore in stock; the
// strip is checked there, with nothing delivered and the two companies level.
// Then everything in stock that the stage still needs is delivered through
// the Program tab's Deliver buttons, and the tab is laid out with the orders
// waiting; one day is played, and the strip and the tab are checked and laid
// out again: the player's figures have moved that day and the rival's are
// its own. Last, the stage's research is ordered by the tab's button and is
// under way a day later. `dug` takes what the player's mine digs a day, ore
// by ore, for the easy program's copy of the content.
int probeRace(Rml::Context& context, game::App& app, LoggingSystem& system,
              const sim::Content& content, std::vector<std::int64_t>& dug) {
    Checks checks("RACE");
    Checks tabChecks("PROGRAM");
    app.setRivalName(std::string(kLongRivalName));
    if (!playToStock(app, content)) {
        return kScenarioFailure;
    }
    game::Session& session = app.session();
    const auto place = [&](const char* when) {
        const sim::CompanyView& view = session.view();
        std::string line =
            std::format("\nrace, {}: day {} of {}", when, view.day, view.deadlineDay);
        for (const sim::ProgramPublicView& program : view.programs) {
            line += std::format("; {} stage {} at {} permille",
                                view.companies[program.company.index()].name,
                                program.stagesCompleted, program.progressPermille);
        }
        std::cout << line << '\n';
    };
    place("nothing delivered");
    const std::int64_t before =
        session.view().programs[game::Session::kPlayer.index()].progressPermille;
    app.setTab(0);
    checkRaceItem(context, "nothing delivered", simRace(session), checks);
    checkRaceStrip(context, "nothing delivered", simRace(session), checks);
    // playToStock left the player on a stage with goods to deliver.
    const std::vector<Delivery> given = deliverByButtons(context, app, tabChecks);
    bool laidOut =
        probeProgramFrames(context, app, system, "program, deliveries waiting", tabChecks);
    session.stepOneDay();
    app.refreshNow();
    place("delivered");
    checkDelivered(session, given, tabChecks);
    laidOut = probeProgramFrames(context, app, system, "program, delivered", tabChecks) && laidOut;
    const sim::CompanyView& view = session.view();
    const sim::ProgramPublicView& mine = view.programs[game::Session::kPlayer.index()];
    // Different places on screen: a different stage count or whole percentage.
    if (view.outcome.ended || view.programs.size() < 2 || mine.progressPermille <= before ||
        std::ranges::all_of(view.programs, [&](const sim::ProgramPublicView& program) {
            return program.stagesCompleted == mine.stagesCompleted &&
                   program.progressPermille / 10 == mine.progressPermille / 10;
        })) {
        std::cout << "SCENARIO (race): the delivery did not put the player and the rival at "
                     "different places in a game still running\n";
        return kScenarioFailure;
    }
    const RaceExpected expected = simRace(session);
    laidOut = probeRaceFrames(context, app, system, expected, "race", checks) && laidOut;
    for (const sim::MineView& working : view.mines) {
        if (working.state == sim::MineState::Operating) {
            dug = working.lastOutputTonnes;
        }
    }
    if (const int code = probeProgramResearch(context, app, system, tabChecks, laidOut);
        code != 0) {
        return code;
    }
    // The player's research applied: its earliest founding day and due day
    // are the sim's on the day it started (spec 011, AC-004).
    app.setTab(0);
    app.refreshNow();
    checkRaceItem(context, "research under way", simRace(session), checks);
    checkRaceStrip(context, "research under way", simRace(session), checks);
    if (!checks.ok()) {
        return kRaceFailure;
    }
    return programResult(tabChecks, laidOut);
}

// ---- the Stops menu (spec 010, BR-010) ----

constexpr int kStopsFailure = 12;

// The rule's row in the open menu, or nothing.
Rml::Element* stopsRow(Rml::Context& context, const std::string& label) {
    Rml::Element* menu = context.GetDocument(0)->GetElementById("pausemenu");
    const std::vector<Rml::Element*> rows = visibleElements(menu, "div", "check", label);
    return rows.size() == 1 ? rows.front() : nullptr;
}

// The menu lists "Rival program progress" with the other rules, inside the
// menu and the window in both frames, and a click on it switches the rule
// off in the session and on again, as it does for the others.
int probeStopsMenu(Rml::Context& context, game::App& app, LoggingSystem& system) {
    Checks checks("STOPS");
    const sim::Content& content = app.session().content();
    const std::string label = game::text(content, "ui.pause_on.rival_program");
    const auto ruleOn = [&] {
        return app.session().autoPause().isEnabled(game::PauseOn::RivalProgram);
    };
    bool laidOut = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== Stops menu, frame {}x{}{} ==\n", frame.width, frame.height,
                                 deck ? " (deck)" : "");
        laidOut = probeTab(context, app, system, frame, 0, Popover::PauseMenu) && laidOut;
        const std::string where = std::format("stops {}x{}", frame.width, frame.height);
        Rml::Element* menu = context.GetDocument(0)->GetElementById("pausemenu");
        if (menu == nullptr || !menu->IsVisible(true)) {
            checks.expect(false, where + ": the Stops menu is not open");
            continue;
        }
        checks.same(where + ": rules listed", visibleElements(menu, "div", "check").size(),
                    game::kPauseOnCount);
        const float bottom = menu->GetAbsoluteOffset(Rml::BoxArea::Border).y +
                             menu->GetBox().GetSize(Rml::BoxArea::Border).y;
        checks.expect(rightEdge(menu) <= static_cast<float>(frame.width) &&
                          bottom <= static_cast<float>(frame.height),
                      std::format("{}: the menu ends at {:.0f}dp across and {:.0f}dp down", where,
                                  rightEdge(menu), bottom));
        checkInside(menu, where, checks);
        const bool before = ruleOn();
        std::string states;
        for (int turn = 0; turn < 2; ++turn) {
            Rml::Element* row = stopsRow(context, label);
            if (row == nullptr) {
                checks.expect(false, std::format("{}: no row reads \"{}\"", where, label));
                break;
            }
            checks.expect(row->IsClassSet("on") == ruleOn(),
                          where + ": the row's tick does not match the session's rule");
            states += std::format("{}{}", states.empty() ? "" : " -> ", ruleOn() ? "on" : "off");
            const bool was = ruleOn();
            click(context, row, label, checks);
            checks.expect(ruleOn() != was, where + ": a click did not switch the rule");
        }
        Rml::Element* row = stopsRow(context, label);
        checks.expect(row != nullptr && ruleOn() == before && row->IsClassSet("on") == before,
                      where + ": the rule is not back as it was after two clicks");
        std::cout << std::format(
            "    {}: \"{}\" is one of {} rules; {} -> {}; menu ends {:.0f}dp "
            "down\n",
            where, label, game::kPauseOnCount, states, ruleOn() ? "on" : "off", bottom);
    }
    app.showPauseMenu(false);
    if (!checks.ok()) {
        return kStopsFailure;
    }
    return laidOut ? 0 : kLayoutFailure;
}

// ---- the stop panel (spec 011, BR-009, BR-010) ----

constexpr int kStopPanelFailure = 14;
constexpr int kStopSeconds = 200;  // seconds at the fastest speed to wait for a stop

// What the stop panel should read, from the session: the first stop's rule
// as the Stops menu names it, its marked line in the log, how many more
// stopped the clock that day, and for a race stop both earliest founding
// days. The tab is the caller's: where the player acts on that rule.
struct StopExpected {
    game::PauseOn rule = game::PauseOn::kCount;
    bool race = false;
    std::string heading;
    std::string line;
    std::string more;
    std::string days;
    int tab = 0;
    std::string goTo;
};

StopExpected simStop(const game::Session& session, int tab, const char* tabKey, Checks& checks) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const std::vector<sim::Notice>& stops = session.stopsToday();
    StopExpected expected;
    expected.tab = tab;
    expected.goTo = game::text(content, "ui.stop_go_to", {{"tab", game::text(content, tabKey)}});
    const std::optional<game::PauseOn> rule =
        stops.empty() ? std::nullopt
                      : game::AutoPause::reasonFor(stops.front(), game::Session::kPlayer);
    if (!rule) {
        checks.expect(false, "the clock did not stop for a notice under a Stops rule");
        return expected;
    }
    expected.rule = *rule;
    expected.race = game::isRaceStop(*rule);
    const std::string name =
        game::text(content, "ui.pause_on." + std::string(game::pauseOnKey(*rule)));
    expected.heading = game::text(
        content, expected.race ? "ui.stop_race_heading" : "ui.stop_heading", {{"rule", name}});
    // The day's stops are the marked lines of the last day the log marked.
    const std::vector<game::LogEntry>& log = session.log();
    const auto newestFirst = std::views::reverse(log);
    const auto last =
        std::ranges::find_if(newestFirst, [](const game::LogEntry& e) { return e.stoppedClock; });
    std::size_t marked = 0;
    for (const game::LogEntry& entry : log) {
        if (last != newestFirst.end() && entry.stoppedClock && entry.day == last->day) {
            expected.line = marked == 0 ? entry.text : expected.line;
            ++marked;
        }
    }
    checks.same("lines marked on the day the clock stopped", marked, stops.size());
    if (stops.size() > 1) {
        expected.more =
            game::text(content, "ui.stop_more", {{"count", std::to_string(stops.size() - 1)}});
    }
    if (expected.race) {
        const sim::Notice& first = stops.front();
        const sim::CompanyId rival = first.rival.valid() ? first.rival : first.company;
        const sim::ProgramPublicView* theirs = nullptr;
        for (const sim::ProgramPublicView& program : view.programs) {
            theirs = program.company == rival ? &program : theirs;
        }
        const sim::ProgramPublicView& mine = view.programs[game::Session::kPlayer.index()];
        checks.expect(rival != game::Session::kPlayer && theirs != nullptr &&
                          mine.earliestFoundingDay && theirs->earliestFoundingDay,
                      "a race stop without both companies' earliest founding days");
        if (rival != game::Session::kPlayer && theirs != nullptr && mine.earliestFoundingDay &&
            theirs->earliestFoundingDay) {
            expected.days =
                game::text(content, "ui.stop_race_days",
                           {{"day", game::formatDay(*mine.earliestFoundingDay)},
                            {"company", view.companies[rival.index()].name},
                            {"rival_day", game::formatDay(*theirs->earliestFoundingDay)}});
        }
    }
    return expected;
}

Rml::Element* stopPanel(Rml::Context& context) {
    settle(context);
    Rml::Element* panel = context.GetDocument(0)->GetElementById("stop");
    return panel != nullptr && panel->IsVisible(true) ? panel : nullptr;
}

// The panel as the player sees it on whatever tab is showing: its words, its
// two buttons, inside the window and under the header, so the clock's
// buttons stay clear of it.
void checkStopPanel(Rml::Context& context, const StopExpected& expected, const Frame& frame,
                    const std::string& where, Checks& checks) {
    Rml::Element* panel = stopPanel(context);
    if (panel == nullptr) {
        checks.expect(false, where + ": no stop panel with the clock stopped");
        return;
    }
    const auto shown = [&](const char* cls, const std::string& text) {
        checks.same(std::format("{}: the panel's {}", where, cls), onlyText(panel, "span", cls),
                    text.empty() ? std::string("<0 found>") : text);
    };
    shown("heading", expected.heading);
    shown("line", expected.line);
    shown("more", expected.more);
    shown("days", expected.days);
    checks.expect(panel->IsClassSet("race") == expected.race,
                  where + ": the panel is not set apart as a race stop, or is when it is not one");
    checks.same(where + ": \"" + expected.goTo + "\" buttons",
                visibleElements(panel, "button", "", expected.goTo).size(), std::size_t{1});
    Rml::Element* header = context.GetDocument(0)->GetElementById("header");
    const float top = panel->GetAbsoluteOffset(Rml::BoxArea::Border).y;
    const float bottom = top + panel->GetBox().GetSize(Rml::BoxArea::Border).y;
    const float headerBottom = header->GetAbsoluteOffset(Rml::BoxArea::Border).y +
                               header->GetBox().GetSize(Rml::BoxArea::Border).y;
    checks.expect(top >= headerBottom && rightEdge(panel) <= static_cast<float>(frame.width) &&
                      bottom <= static_cast<float>(frame.height),
                  std::format("{}: the panel runs {:.0f}-{:.0f}dp down, {:.0f}dp across, under a "
                              "header ending {:.0f}dp down",
                              where, top, bottom, rightEdge(panel), headerBottom));
    checkInside(panel, where, checks);
}

// Every tab in both frames with the panel up.
bool probeStopFrames(Rml::Context& context, game::App& app, LoggingSystem& system,
                     const StopExpected& expected, const char* label, Checks& checks) {
    bool ok = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== {}, frame {}x{}{} ==\n", label, frame.width, frame.height,
                                 deck ? " (deck)" : "");
        for (std::size_t tab = 0; tab < kTabs.size(); ++tab) {
            ok = probeTab(context, app, system, frame, tab, Popover::None) && ok;
            checkStopPanel(
                context, expected, frame,
                std::format("{} {} {}x{}", label, kTabs.at(tab), frame.width, frame.height),
                checks);
        }
    }
    std::cout << std::format("    {}: \"{}\" / \"{}\" / \"{}\" / \"{}\"\n", label, expected.heading,
                             expected.line, expected.more, expected.days);
    return ok;
}

// The panel's buttons, as the player clicks them: Go to opens the tab where
// the stop is acted on; the rule's button switches it off, says so, and
// switches it on again; starting the clock clears the panel.
void useStopPanel(Rml::Context& context, game::App& app, const StopExpected& expected,
                  Checks& checks) {
    if (expected.rule == game::PauseOn::kCount) {
        return;  // no stop to act on: simStop has said so
    }
    const sim::Content& content = app.session().content();
    app.setTab(0);
    clickFirst(context, "button", "", expected.goTo, checks);
    checks.same("the tab after \"" + expected.goTo + "\"", app.model().tab, expected.tab);
    const auto ruleOn = [&] { return app.session().autoPause().isEnabled(expected.rule); };
    const bool before = ruleOn();
    clickFirst(context, "button", "", game::text(content, "ui.stop_for_this"), checks);
    checks.expect(!ruleOn(), "the panel's rule button did not switch the rule off");
    clickFirst(context, "button", "", game::text(content, "ui.stop_rule_off"), checks);
    checks.expect(ruleOn() == before, "the panel's rule button did not switch the rule back on");
    checks.expect(stopPanel(context) != nullptr, "the panel went before the clock was started");
    clickFirst(context, "button", "", "1x", checks);
    checks.expect(
        app.session().speed() != game::Speed::Paused && app.session().stopsToday().empty(),
        "1x did not start the clock and clear the day's stops");
    checks.expect(stopPanel(context) == nullptr,
                  "the stop panel is still up with the clock running");
    clickFirst(context, "button", "", "Pause", checks);
}

int stopResult(const Checks& checks, bool laidOut) {
    if (!checks.ok()) {
        return kStopPanelFailure;
    }
    return laidOut ? 0 : kLayoutFailure;
}

// A race stop: the day the rival starts its program research (spec 010) the
// clock stops under its own heading, with both earliest founding days.
int probeRaceStop(Rml::Context& context, game::App& app, LoggingSystem& system) {
    app.refreshNow();
    Checks checks("STOP");
    const StopExpected expected = simStop(app.session(), 6, "ui.tab_program", checks);
    checks.expect(expected.race, "the rival's research did not stop the clock as a race stop");
    const bool laidOut = probeStopFrames(context, app, system, expected, "race stop", checks);
    useStopPanel(context, app, expected, checks);
    return stopResult(checks, laidOut);
}

// A stop with a count: two surveys ordered on one day report on one day,
// and the panel names the first and counts the other.
int probeStopCount(Rml::Context& context, game::App& app, LoggingSystem& system) {
    app.startGame(kScenarioSeed);
    game::Session& session = app.session();
    std::vector<sim::PlotId> lots;
    for (const sim::PlotView& plot : session.view().plots) {
        if (plot.listing == sim::PlotListing::Listed && plot.auction && lots.size() < 2) {
            lots.push_back(plot.id);
        }
    }
    if (lots.size() < 2) {
        std::cout << "SCENARIO (stop): fewer than two plots at auction on day 0\n";
        return kScenarioFailure;
    }
    for (const sim::PlotId lot : lots) {
        session.submit(sim::CommissionSurvey{{}, lot});
    }
    session.setSpeed(game::Speed::Fastest);
    for (int second = 0; second < kStopSeconds && session.speed() != game::Speed::Paused;
         ++second) {
        app.update(1.0);
    }
    app.refreshNow();
    Checks checks("STOP");
    const StopExpected expected = simStop(session, 1, "ui.tab_plots", checks);
    checks.expect(expected.rule == game::PauseOn::SurveyComplete && !expected.more.empty(),
                  std::format("the clock stopped on day {} with {} stops, not for two surveys",
                              session.day(), session.stopsToday().size()));
    const bool laidOut =
        probeStopFrames(context, app, system, expected, "stop with a count", checks);
    useStopPanel(context, app, expected, checks);
    return stopResult(checks, laidOut);
}

// ---- the end screen (spec 010) ----

constexpr int kEndFailure = 11;

// One company's column on the end screen.
struct EndColumn {
    std::string name;
    std::string count;               // "2 of 3 stages complete"
    std::vector<std::string> lines;  // one a stage
};

// What the end screen should read, from the session.
struct EndExpected {
    std::string title;
    std::string body;
    std::string day;
    std::vector<EndColumn> columns;  // the player first
    std::string cash;
    std::string mines;
    std::string stages;
    std::string button;
};

// The day the session's log has for a notice of this kind about this
// company; `number` picks a stage's completion (0 for any notice). The end
// screen's stage days are not read from here: they are the view's.
std::optional<sim::Day> loggedDay(const game::Session& session, sim::NoticeKind kind,
                                  sim::CompanyId company, std::int64_t number) {
    std::optional<sim::Day> day;
    for (const game::LogEntry& entry : session.log()) {
        if (entry.notice.kind == kind && entry.notice.company == company &&
            (number == 0 || entry.notice.tonnes == number)) {
            day = entry.day;
        }
    }
    return day;
}

EndColumn simEndColumn(const game::Session& session, const sim::ProgramPublicView& program,
                       Checks& checks) {
    const sim::Content& content = session.content();
    const std::size_t total = content.program.stages.size();
    EndColumn column;
    column.name = program.company == game::Session::kPlayer
                      ? game::text(content, "ui.race_you")
                      : session.view().companies[program.company.index()].name;
    column.count = game::text(
        content, "ui.race_stage",
        {{"done", std::to_string(program.stagesCompleted)}, {"total", std::to_string(total)}});
    for (std::size_t stage = 1; stage <= total; ++stage) {
        if (stage > program.stagesCompleted) {
            column.lines.push_back(
                game::text(content, "ui.end_stage_never", {{"number", std::to_string(stage)}}));
            continue;
        }
        // The sim's own record of the day (sim::ProgramPublicView::stageDays).
        const bool recorded = stage <= program.stageDays.size();
        checks.expect(recorded, std::format("the view has no day for stage {} completed by {}",
                                            stage, column.name));
        column.lines.push_back(game::text(
            content, "ui.end_stage_days",
            {{"number", std::to_string(stage)},
             {"day", std::to_string(recorded ? program.stageDays[stage - 1] : sim::Day{0})}}));
    }
    return column;
}

// `result` is the ending the stage played for: "founded", "partner", "lost",
// "deadline" or "bankrupt". The words are the catalogue's for it; the days
// and figures are the session's.
EndExpected simEnd(const game::Session& session, const std::string& result, Checks& checks) {
    const sim::Content& content = session.content();
    const sim::CompanyView& view = session.view();
    const sim::OutcomeView& outcome = session.outcome();
    const std::size_t player = game::Session::kPlayer.index();
    EndExpected expected;
    const bool bankrupt = result == "bankrupt";
    const std::string named = !bankrupt && outcome.founder.valid()
                                  ? view.companies[outcome.founder.index()].name
                                  : view.companies[player].name;
    expected.title = game::text(content, "ui.end_" + result + "_title");
    expected.body = game::text(content, "ui.end_" + result + "_body", {{"company", named}});
    const std::optional<sim::Day> fell =
        loggedDay(session, sim::NoticeKind::Bankrupt, game::Session::kPlayer, 0);
    checks.expect(!bankrupt || fell.has_value(), "the log has no bankruptcy of the player");
    expected.day = game::text(content, "ui.end_day",
                              {{"day", std::to_string(bankrupt ? fell.value_or(0) : outcome.day)}});
    checks.expect(view.program.stageDays == view.programs[player].stageDays,
                  "the player's own stage days and its public ones differ");
    expected.columns.push_back(simEndColumn(session, view.programs[player], checks));
    for (const sim::ProgramPublicView& program : view.programs) {
        if (program.company != game::Session::kPlayer) {
            expected.columns.push_back(simEndColumn(session, program, checks));
        }
    }
    expected.cash = game::formatMoney(view.cash);
    // Open mines (spec 011, BR-012): a worked-out mine, or one shut by the
    // owner's bankruptcy, is not counted.
    expected.mines = std::to_string(std::ranges::count_if(view.mines, [](const sim::MineView& m) {
        return m.state != sim::MineState::Depleted && m.state != sim::MineState::Closed;
    }));
    expected.stages = std::to_string(view.program.stagesCompleted);
    expected.button = game::text(content, "ui.end_new_game");
    return expected;
}

// The session's own answer to how the game ended, in the stages' words.
std::string sessionResult(const game::Session& session) {
    const sim::CompanyView& view = session.view();
    const sim::OutcomeView& outcome = session.outcome();
    if (view.bankrupt) {
        return "bankrupt";
    }
    if (!outcome.ended) {
        return "still playing";
    }
    switch (outcome.results[game::Session::kPlayer.index()]) {
        case sim::ProgramResult::Founded:
            return "founded";
        case sim::ProgramResult::LesserPartner:
            return "partner";
        case sim::ProgramResult::Lost:
            return outcome.founder.valid() ? "lost" : "deadline";
        case sim::ProgramResult::None:
            break;
    }
    return "no result";
}

// The end screen against the session: it covers the window, lies inside it,
// and reads what the session reports.
void checkEndScreen(Rml::Context& context, const Frame& frame, const EndExpected& expected,
                    Checks& checks) {
    const std::string where = std::format("end {}x{}", frame.width, frame.height);
    Rml::Element* overlay = context.GetDocument(0)->GetElementById("overlay");
    if (overlay == nullptr || !overlay->IsVisible(true)) {
        checks.expect(false, where + ": the end screen is not shown");
        return;
    }
    const Rml::Vector2f cover = overlay->GetBox().GetSize(Rml::BoxArea::Border);
    checks.expect(std::abs(cover.x - static_cast<float>(frame.width)) <= kSpillTolerance &&
                      std::abs(cover.y - static_cast<float>(frame.height)) <= kSpillTolerance,
                  std::format("{}: the end screen is {:.0f}x{:.0f}dp and does not cover the window",
                              where, cover.x, cover.y));
    const std::vector<Rml::Element*> panels = visibleElements(overlay, "div", "panel");
    checks.same(where + ": panels", panels.size(), std::size_t{1});
    if (panels.size() != 1) {
        return;
    }
    Rml::Element* panel = panels.front();
    const float top = panel->GetAbsoluteOffset(Rml::BoxArea::Border).y;
    const float bottom = top + panel->GetBox().GetSize(Rml::BoxArea::Border).y;
    checks.expect(top >= 0.0F && bottom <= static_cast<float>(frame.height) &&
                      panel->GetAbsoluteOffset(Rml::BoxArea::Border).x >= 0.0F &&
                      rightEdge(panel) <= static_cast<float>(frame.width),
                  std::format("{}: the panel runs from {:.0f} to {:.0f}dp down a {}dp window",
                              where, top, bottom, frame.height));
    checkInside(panel, where, checks);
    checks.same(where + ": title", onlyText(panel, "h1", ""), expected.title);
    checks.same(where + ": body", onlyText(panel, "p", "body"), expected.body);
    checks.same(where + ": day", onlyText(panel, "p", "day"), expected.day);
    std::vector<Rml::Element*> columns = visibleElements(panel, "div", "col");
    std::erase_if(columns, [](Rml::Element* e) { return e->IsClassSet("figures"); });
    checks.same(where + ": companies", columns.size(), expected.columns.size());
    std::string shown;
    for (std::size_t i = 0; i < columns.size() && i < expected.columns.size(); ++i) {
        const EndColumn& column = expected.columns[i];
        const std::string who = where + ": " + column.name;
        checks.same(who + ": name", onlyText(columns[i], "span", "who"), column.name);
        checks.same(who + ": stages complete", onlyText(columns[i], "span", "count"), column.count);
        std::string lines;
        for (const std::string& line : column.lines) {
            lines += (lines.empty() ? "" : " | ") + line;
        }
        checks.same(who + ": stage days", joinedText(visibleElements(columns[i], "span", "sline")),
                    lines);
        shown += std::format("\n      {}: {}; {}", column.name, column.count, lines);
    }
    const std::vector<Rml::Element*> figures = visibleElements(panel, "div", "figures");
    checks.same(where + ": figure columns", figures.size(), std::size_t{1});
    const std::string values =
        figures.empty() ? "" : joinedText(visibleElements(figures.front(), "span", "v"));
    checks.same(where + ": the player's figures", values,
                expected.cash + " | " + expected.mines + " | " + expected.stages);
    // New game is the only way on.
    const std::vector<Rml::Element*> buttons = visibleElements(overlay, "button", "");
    checks.same(where + ": buttons on the end screen", joinedText(buttons), expected.button);
    std::cout << std::format(
        "    {}: \"{}\"\n      \"{}\"\n      {}; panel {:.0f} to {:.0f}dp of {}dp{}\n      cash "
        "{}, "
        "mines {}, stages {}; button \"{}\"\n",
        where, expected.title, expected.body, expected.day, top, bottom, frame.height, shown,
        expected.cash, expected.mines, expected.stages, joinedText(buttons));
}

// The clock controls read as off, and neither they nor any control on the
// tab behind the end screen moves the game or queues an order. The controls
// are clicked directly, as if the end screen were not in the way.
void checkNothingMoves(Rml::Context& context, game::App& app, const std::string& where,
                       Checks& checks) {
    const game::Session& session = app.session();
    const sim::Day day = session.day();
    const std::size_t waiting = session.pendingOrders().size();
    Rml::ElementDocument* doc = context.GetDocument(0);
    const std::vector<Rml::Element*> headers = visibleElements(doc, "div", "speed");
    const std::size_t clock =
        headers.empty() ? 0 : visibleElements(headers.front(), "button", "").size();
    checks.expect(clock == 5, where + ": the header does not hold the five clock controls");
    for (std::size_t i = 0; i < clock; ++i) {
        // Found again each time: a click may rebuild what the model repeats.
        Rml::Element* button = visibleElements(headers.front(), "button", "").at(i);
        checks.expect(
            button->IsClassSet("disabled"),
            std::format("{}: clock control \"{}\" is not disabled", where, visibleText(button)));
        button->Click();
        settle(context);
    }
    Rml::Element* content = doc->GetElementById("content");
    std::size_t clicked = 0;
    for (const char* tag : {"button", "div"}) {
        const std::string_view cls = std::string_view(tag) == "div" ? "check" : "";
        const std::size_t controls = visibleElements(content, tag, cls).size();
        for (std::size_t i = 0; i < controls; ++i) {
            const std::vector<Rml::Element*> found = visibleElements(content, tag, cls);
            if (i < found.size()) {
                found[i]->Click();
                settle(context);
                ++clicked;
            }
        }
    }
    checks.expect(session.day() == day && session.speed() == game::Speed::Paused,
                  where + ": the clock moved after the game was over");
    checks.expect(session.pendingOrders().size() <= waiting,
                  where + ": an order was queued after the game was over");
    std::cout << std::format(
        "    {}: 5 clock controls off; {} controls behind the end screen "
        "clicked, day {} and {} waiting orders unchanged\n",
        where, clicked, session.day(), session.pendingOrders().size());
}

// The finished game saved and loaded again: the log starts again, with no
// stage's completion in it, and the end screen still shows every stage's day,
// because the days are the sim's and are saved with the game. The model's
// rows are held to what stood before the save in every ending; the screen is
// read again in the race's endings (a bankruptcy's own day is the log's,
// which a loaded game does not have).
//
// A load cuts the log to one line, and RmlUi warns once for each row of the
// Dashboard's Recent list it is about to remove ("Data array index out of
// bounds", for `recent[n].warning`). The rows do go. Those warnings, and only
// those, are set aside here and counted in the line printed, so that the next
// tab probed is not failed for them.
void checkLoadedEnd(Rml::Context& context, game::App& app, LoggingSystem& system,
                    const std::string& result, const EndExpected& expected, Checks& checks) {
    game::Session& session = app.session();
    const std::vector<std::uint8_t> bytes = session.save();
    const auto before = static_cast<std::ptrdiff_t>(system.problems.size());
    if (!session.load(bytes)) {
        checks.expect(false, "the finished game did not load from its own save");
        return;
    }
    app.refreshNow();
    settle(context);
    checks.expect(session.gameOver(), "the loaded game is not over");
    checks.expect(std::ranges::none_of(session.log(),
                                       [](const game::LogEntry& entry) {
                                           return entry.notice.kind ==
                                                  sim::NoticeKind::ProgramStageComplete;
                                       }),
                  "the loaded game's log still holds a stage's completion");
    const std::vector<game::EndCompany>& shown = app.model().end.companies;
    checks.same("loaded: companies on the end screen", shown.size(), expected.columns.size());
    std::string days;
    for (std::size_t i = 0; i < shown.size() && i < expected.columns.size(); ++i) {
        std::string lines;
        for (const game::EndStageLine& line : shown[i].lines) {
            lines += (lines.empty() ? "" : " | ") + line.text;
        }
        std::string saved;
        for (const std::string& line : expected.columns[i].lines) {
            saved += (saved.empty() ? "" : " | ") + line;
        }
        checks.same("loaded: " + expected.columns[i].name + ": stage days", lines, saved);
        days += std::format("\n      {}: {}", shown[i].name, lines);
    }
    std::cout << std::format(
        "\n  end screen, {}, saved and loaded on day {} ({} log lines, none "
        "a stage's completion):{}\n",
        result, session.day(), session.log().size(), days);
    for (const Frame& frame : kFrames) {
        if (result == "bankrupt") {
            break;
        }
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(static_cast<float>(frame.width) < kDeckWidth);
        app.setTab(0);
        app.refreshNow();
        settle(context);
        checkEndScreen(context, frame, simEnd(session, result, checks), checks);
    }
    const auto shrinking = [](const std::string& problem) {
        return problem == "Data array index out of bounds." ||
               (problem.starts_with("Could not get value from data variable 'recent[") &&
                problem.ends_with("].warning'."));
    };
    const std::size_t setAside =
        std::erase_if(system.problems, [&, at = std::ptrdiff_t{0}](const std::string& p) mutable {
            return at++ >= before && shrinking(p);
        });
    std::cout << std::format(
        "    loaded: {} RmlUi warnings for the shorter Recent list set aside\n", setAside);
}

// A finished game in both frames: every tab laid out under the end screen,
// the screen checked against the session, every control tried. `result` is
// the ending the stage played for; the session must report the same.
int probeEndScreen(Rml::Context& context, game::App& app, LoggingSystem& system,
                   const std::string& result) {
    Checks checks("END");
    const game::Session& session = app.session();
    app.refreshNow();
    checks.same("the session's ending", sessionResult(session), result);
    checks.expect(session.gameOver(), "the session does not report the game over");
    checkNoOreMarks(app, checks);
    checkNeedRows(app, "end " + result, checks);
    if (!checks.ok()) {
        return kEndFailure;
    }
    const EndExpected expected = simEnd(session, result, checks);
    bool laidOut = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== end screen, {}, frame {}x{}{} ==\n", result, frame.width,
                                 frame.height, deck ? " (deck)" : "");
        for (std::size_t tab = 0; tab < kTabs.size(); ++tab) {
            laidOut = probeTab(context, app, system, frame, tab, Popover::None) && laidOut;
            if (tab == 0) {
                checkEndScreen(context, frame, expected, checks);
            }
            checkNothingMoves(context, app,
                              std::format("end {}x{} {}", frame.width, frame.height, kTabs.at(tab)),
                              checks);
        }
    }
    checkLoadedEnd(context, app, system, result, expected, checks);
    // The one button starts a new game, and play is back.
    Rml::Element* overlay = context.GetDocument(0)->GetElementById("overlay");
    const std::vector<Rml::Element*> buttons = visibleElements(overlay, "button", "");
    click(context, buttons.size() == 1 ? buttons.front() : nullptr, "New game on the end screen",
          checks);
    app.refreshNow();
    settle(context);
    overlay = context.GetDocument(0)->GetElementById("overlay");
    checks.expect(!app.session().gameOver() && app.session().day() == 0 &&
                      (overlay == nullptr || !overlay->IsVisible(true)),
                  "New game did not start a new game and take the end screen away");
    checks.expect(
        visibleElements(context.GetDocument(0), "button", "disabled").size() ==
            visibleElements(context.GetDocument(0)->GetElementById("content"), "button", "disabled")
                .size(),
        "a header control is still disabled in the new game");
    std::cout << std::format("    New game: {}, the end screen gone, the clock controls live\n",
                             app.model().day);
    if (!checks.ok()) {
        return kEndFailure;
    }
    return laidOut ? 0 : kLayoutFailure;
}

// ---- the Program tab on the probe's own copies of the content ----

// The shipped content with one more requirement on its first stage, the
// first good any stage asks for that a new company cannot dig, and with the
// last stage's cost on the first. In the shipped program that good and that
// cost are two stages away, further than the probe plays, and a new company
// has the money for the first stage.
std::optional<sim::Content> lockedContent(const sim::Content& shipped) {
    sim::Content content = shipped;
    if (!content.program.stages.empty()) {
        content.program.stages.front().cost = content.program.stages.back().cost;
    }
    for (const sim::ProgramStage& stage : shipped.program.stages) {
        for (const sim::ProgramRequirement& need : stage.requirements) {
            if (shipped.resources[need.resource.index()].unlockedBy.valid() &&
                !content.program.stages.empty()) {
                content.program.stages.front().requirements.push_back(need);
                return content;
            }
        }
    }
    return std::nullopt;
}

// A new game on that copy: the requirement's row says what unlocks the good,
// the cash is among what the stage lacks,
// and the tab warns that cover costs is on, as it is for a new company. Then
// cover costs is switched off by an order, and the warning goes with it.
int probeProgramLocked(Rml::Context& context, game::App& app, LoggingSystem& system) {
    Checks checks("PROGRAM");
    app.setTab(kProgramTab);
    app.refreshNow();
    const ProgramExpected fresh = simProgram(app);
    checks.expect(
        std::ranges::any_of(fresh.needs, [](const NeedLine& need) { return !need.locked.empty(); }),
        "no requirement of the first stage is a good the company cannot dig");
    const sim::CompanyView& view = app.session().view();
    checks.expect(
        view.program.stage && view.program.stage->cost > view.cash - view.reservedCash &&
            fresh.blocked.find(game::text(
                app.session().content(), "ui.program_blocked_cash",
                {{"amount", game::formatMoney(view.program.stage->cost)}})) != std::string::npos,
        "the stage costs more than the company has, and the cash is not said to be missing");
    checks.expect(app.session().view().coverCosts && !fresh.cover.empty(),
                  "a new company does not have cover costs on, with its warning");
    bool laidOut =
        probeProgramFrames(context, app, system, "program, a good we cannot dig yet", checks);
    app.session().submit(sim::SetCoverCosts{game::Session::kPlayer, 0});
    app.refreshNow();
    checks.expect(simProgram(app).cover.empty(), "cover costs ordered off, and a warning expected");
    laidOut =
        probeProgramFrames(context, app, system, "program, cover costs ordered off", checks) &&
        laidOut;
    return programResult(checks, laidOut);
}

// The easy program: what each of its stages costs and needs. A stage asks
// for a few days of what the fixed game's mine digs, of each ore it digs,
// its project takes days, and the money is what a new company has. The
// shipped stages take years of play; the rules and the client are untouched.
constexpr std::int64_t kEasyOutputDays = 2;          // times the stage's number
constexpr std::int64_t kEasyStageDollars = 250'000;  // times the stage's number
constexpr std::int64_t kEasyResearchDollars = 100'000;
constexpr sim::Day kEasyResearchDays = 3;
constexpr int kEasyMaxDays = 120;  // from the mine's first stock to the last stage

sim::Content easyContent(const sim::Content& shipped, const std::vector<std::int64_t>& dug) {
    sim::Content content = shipped;
    std::int64_t number = 0;
    for (sim::ProgramStage& stage : content.program.stages) {
        ++number;
        stage.cost = sim::Money::fromUnits(kEasyStageDollars * number);
        stage.requirements.clear();
        for (std::uint32_t ore = 0; ore < dug.size(); ++ore) {
            if (dug[ore] > 0) {
                stage.requirements.push_back(
                    {sim::ResourceId(ore), dug[ore] * kEasyOutputDays * number});
            }
        }
        sim::TechDef& project = content.techs[stage.research.index()];
        project.cost = sim::Money::fromUnits(kEasyResearchDollars);
        project.durationDays = kEasyResearchDays;
    }
    return content;
}

// Everything in stock that the current stage still needs, by orders.
void deliverAll(game::Session& session) {
    const std::optional<sim::ProgramStageView> stage = session.view().program.stage;
    for (const sim::ProgramRequirementView& need :
         stage ? stage->requirements : std::vector<sim::ProgramRequirementView>()) {
        const std::int64_t tonnes =
            std::min(need.stockTonnes, need.neededTonnes - need.deliveredTonnes);
        if (tonnes > 0) {
            session.submit(sim::DeliverToProgram{game::Session::kPlayer, need.resource, tonnes});
        }
    }
}

bool stageReady(const sim::ProgramView& program) {
    return program.stage.has_value() && program.stage->ready;
}

// One day of the race by orders: the stage's project started if it has not
// been, everything deliverable delivered, the stage completed once the sim
// calls it ready.
void raceOneDay(game::Session& session) {
    if (const std::optional<sim::ProgramStageView> stage = session.view().program.stage) {
        if (stage->researchState == sim::ProgramResearchState::NotStarted) {
            session.submit(sim::StartResearch{game::Session::kPlayer, stage->research});
        }
        deliverAll(session);
        if (stage->ready) {
            session.submit(sim::CompleteProgramStage{game::Session::kPlayer});
        }
    }
    session.stepOneDay();
}

// The race on the easy copy, to its end. The first stage is researched by
// the tab's button and delivered to by orders until the sim calls it ready;
// it is completed by the tab's button, with the cost committed while the
// order waits, and the second stage is current a day later. The other two
// stages are played by orders, and the tab says the program is complete.
int probeProgramToTheEnd(Rml::Context& context, game::App& app, LoggingSystem& system,
                         const sim::Content& content) {
    Checks checks("PROGRAM");
    const auto fail = [&](std::string_view what) {
        std::cout << std::format("SCENARIO (program): {} ({})\n", what, app.model().day);
        return kScenarioFailure;
    };
    if (!playToStock(app, content)) {
        return kScenarioFailure;
    }
    game::Session& session = app.session();
    // The session's view is one object, refreshed in place each day.
    const sim::ProgramView& program = session.view().program;
    bool laidOut = true;
    if (const int code = probeProgramResearch(context, app, system, checks, laidOut); code != 0) {
        return code;
    }
    for (int day = 0; day < kEasyMaxDays && !stageReady(program) && !session.gameOver(); ++day) {
        deliverAll(session);
        session.stepOneDay();
    }
    if (!program.stage || !program.stage->ready || program.stage->index != 0 ||
        session.gameOver()) {
        return fail("the first stage did not become ready to complete");
    }
    const sim::Money cost = program.stage->cost;
    app.setTab(kProgramTab);
    app.refreshNow();
    const ProgramExpected atReady = simProgram(app);
    checks.expect(atReady.ready && atReady.blocked.empty() && !atReady.readyLine.empty(),
                  "the sim calls the stage ready and something is still said to be missing");
    laidOut = probeProgramFrames(context, app, system, "program, stage ready", checks) && laidOut;
    // Completed by its button: the chip, Cancel, and the cost in Committed.
    clickFirst(context, "button", "", atReady.completeButton, checks);
    checks.expect(session.pendingOrders().size() == 1 &&
                      orderWaiting<sim::CompleteProgramStage>(
                          session,
                          [](const sim::CompleteProgramStage& order) {
                              return order.company == game::Session::kPlayer;
                          }),
                  "Complete stage did not queue the stage's completion alone");
    checks.same("Committed with the completion waiting", app.model().committed,
                game::text(content, "ui.committed", {{"amount", game::formatMoney(cost)}}));
    checks.expect(session.committed() == cost, "the session does not commit the stage's cost");
    laidOut =
        probeProgramFrames(context, app, system, "program, completion waiting", checks) && laidOut;
    const sim::Money cashBefore = session.view().cash;
    session.stepOneDay();
    app.refreshNow();
    if (program.stagesCompleted != 1 || !program.stage || program.stage->index != 1) {
        return fail("the first stage was not completed by its order");
    }
    checks.expect(session.view().cash <= cashBefore - cost,
                  "completing the stage did not take its cost");
    checks.expect(!app.model().has_committed, "money is still committed after the completion");
    checkResearchStartable(context, app, "one stage complete", checks);
    laidOut = probeProgramFrames(context, app, system, "program, second stage current", checks) &&
              laidOut;
    // The rest of the race by orders, as the buttons give them.
    for (int day = 0; day < kEasyMaxDays && !program.complete && !session.gameOver(); ++day) {
        raceOneDay(session);
    }
    const sim::CompanyView& view = session.view();
    if (!view.program.complete || view.outcome.founder != game::Session::kPlayer) {
        return fail(std::format("the player did not found the program ({} stages complete)",
                                view.program.stagesCompleted));
    }
    app.refreshNow();
    std::cout << std::format("\nprogram: founded on day {} with {} of {} stages complete\n",
                             view.outcome.day, view.program.stagesCompleted,
                             content.program.stages.size());
    laidOut = probeProgramFrames(context, app, system, "program complete", checks) && laidOut;
    // The game is over and the player founded the program: the end screen.
    // Last, because its New game button starts the game again.
    const int end = probeEndScreen(context, app, system, "founded");
    const int tab = programResult(checks, laidOut);
    return tab != 0 ? tab : end;
}

// Where an app finds its assets and writes its playtest trace.
struct AppPaths {
    std::filesystem::path assets;
    std::filesystem::path playtest;
};

// What a pass on a copy of the content needs to make its own app.
struct ProbeHost {
    platform::Services* services = nullptr;
    LoggingSystem* system = nullptr;
    AppPaths paths;
};

// Runs `pass` on an app made with `content`, in a context of its own: an app
// keeps the content it was made with, and a context keeps the data types an
// app declared in it and warns when they are declared again. Returns the
// pass's exit code, or 2 when the document did not load.
template <typename Pass>
int onItsOwnApp(const ProbeHost& host, const char* name, const sim::Content& content,
                const Pass& pass) {
    int exitCode = 0;
    Rml::Context* context =
        Rml::CreateContext(name, Rml::Vector2i(kFrames[0].width, kFrames[0].height));
    {
        game::App app(*context, *host.services, content, host.paths.assets, host.paths.playtest,
                      "probe");
        if (!app.loadDocument()) {
            exitCode = 2;
        }
        app.update(0.0);
        if (const int code = pass(*context, app); code != 0) {
            exitCode = code;
        }
    }
    Rml::RemoveContext(name);
    return exitCode;
}

// ---- exact prices on the Market tab (spec 010 BR-014) ----

constexpr int kPricesFailure = 13;
constexpr int kDashboardTab = 0;
constexpr int kMarketTab = 2;
// The widest price with five digits of dollars, "$99,999.99": lithium trades
// in the tens of thousands, well above the price it opens at.
constexpr std::int64_t kWidePriceCents = 9'999'999;
constexpr std::int64_t kHeldBackTonnes = 1'000;

// The shipped content with every ore at that price. A new game opens at the
// base price, so the Market tab writes it as today's price and as the base.
sim::Content widePriceContent(const sim::Content& shipped) {
    sim::Content content = shipped;
    for (sim::ResourceDef& ore : content.resources) {
        ore.basePrice = sim::Money::fromCents(kWidePriceCents);
    }
    return content;
}

// What `e` reads, as wide as its own font writes it on one line.
float lineWidth(Rml::Element* e, std::string& line) {
    line = visibleText(e);
    return static_cast<float>(Rml::ElementUtilities::GetStringWidth(e, line));
}

// The Market tab's Price cells, row by row: today's price and the base price
// to the cent (game::exactPrice), as the mine's break-even price is written.
// Returns the least room a cell has to spare beside its widest line (below
// zero when a line does not fit), which is reported and judged by the caller.
float checkMarketPriceCells(Rml::Context& context, game::App& app, const std::string& where,
                            Checks& checks) {
    const sim::Content& content = app.session().content();
    const sim::CompanyView& view = app.session().view();
    Rml::Element* pane = context.GetDocument(0)->GetElementById("content");
    const std::vector<Rml::Element*> rows = visibleElements(pane, "tr", "row");
    checks.same(std::format("{}: resource rows", where), rows.size(), view.markets.size());
    float spare = -kNothingMeasured;
    float room = 0.0F;
    float widest = 0.0F;
    std::string widestLine;
    for (std::size_t r = 0; r < rows.size() && r < view.markets.size(); ++r) {
        const sim::MarketView& market = view.markets[r];
        // The row's right-aligned cells are the price and the stock.
        const std::vector<Rml::Element*> cells = visibleElements(rows[r], "td", "n");
        const std::vector<Rml::Element*> lines =
            cells.empty() ? cells : visibleElements(cells.front(), "span", "num");
        checks.same(std::format("{}: lines in the price cell of row {}", where, r), lines.size(),
                    std::size_t{2});
        if (lines.size() != 2) {
            continue;
        }
        checks.same(std::format("{}: price of {}", where, content.resources[r].name),
                    visibleText(lines[0]), game::exactPrice(market.price) + "/t");
        checks.same(
            std::format("{}: base price of {}", where, content.resources[r].name),
            visibleText(lines[1]),
            game::text(content, "ui.market_base",
                       {{"price", game::exactPrice(market.basePrice)},
                        {"ratio", game::formatRatio(market.price.ratioTo(market.basePrice))}}));
        room = cells.front()->GetBox().GetSize(Rml::BoxArea::Content).x;
        for (Rml::Element* line : lines) {
            std::string text;
            const float width = lineWidth(line, text);
            if (width > widest) {
                widest = width;
                widestLine = text;
            }
            spare = std::min(spare, room - width);
        }
    }
    std::cout << std::format(
        "    {}: Price cell {:.1f}dp, widest line \"{}\" {:.1f}dp, {:.1f}dp to spare\n", where,
        room, widestLine, widest, spare);
    return spare;
}

// The floor price of a standing order is a price per tonne too: exact in
// the row's summary and in the selected resource's panel. And nothing on
// the tab is a price per tonne in formatMoney's short form ("$1.4k/t").
void checkMarketFloor(Rml::Context& context, game::App& app, const std::string& where,
                      const sim::SetSellPolicy& order, Checks& checks) {
    const sim::Content& content = app.session().content();
    Rml::Element* pane = context.GetDocument(0)->GetElementById("content");
    const std::string reserve = game::formatTonnes(order.reserveTonnes);
    const std::string floor = game::exactPrice(order.floorPrice);
    const std::string summary =
        game::text(content, "ui.policy_auto_keep_floor", {{"reserve", reserve}, {"price", floor}});
    checks.same(std::format("{}: rows reading \"{}\"", where, summary),
                visibleElements(pane, "span", "sub", summary).size(), std::size_t{1});
    const std::string panel =
        game::text(content, "ui.policy_summary_on",
                   {{"resource", content.resources[order.resource.index()].name},
                    {"reserve", reserve},
                    {"price", floor}});
    checks.same(std::format("{}: panels reading \"{}\"", where, panel),
                visibleElements(pane, "p", "text2", panel).size(), std::size_t{1});
    const std::string all = visibleText(pane);
    for (const char* shortForm : {"k/t", "M/t", "B/t"}) {
        checks.expect(
            all.find(shortForm) == std::string::npos,
            std::format("{}: a price per tonne is abbreviated (\"{}\")", where, shortForm));
    }
}

// The Dashboard's Prices panel beside the mines' break-even prices: the same
// exact price, with the ratio to base under it. Reports the room the price
// leaves the resource's name, which the panel clips.
void checkDashboardPrices(Rml::Context& context, game::App& app, const std::string& where,
                          Checks& checks) {
    const sim::Content& content = app.session().content();
    const sim::CompanyView& view = app.session().view();
    Rml::Element* pane = context.GetDocument(0)->GetElementById("content");
    const std::vector<Rml::Element*> rows = visibleElements(pane, "div", "pr");
    checks.same(std::format("{}: rows in the Prices panel", where), rows.size(),
                view.markets.size());
    float tightest = -kNothingMeasured;
    std::string clipped;
    for (std::size_t r = 0; r < rows.size() && r < view.markets.size(); ++r) {
        const sim::MarketView& market = view.markets[r];
        const std::vector<Rml::Element*> figures = visibleElements(rows[r], "span", "num");
        const std::vector<Rml::Element*> names = visibleElements(rows[r], "span", "nm");
        if (figures.empty() || names.empty()) {
            checks.expect(false, std::format("{}: row {} has no name or no price", where, r));
            continue;
        }
        checks.same(std::format("{}: price of {}", where, content.resources[r].name),
                    visibleText(figures.front()),
                    game::exactPrice(market.price) +
                        game::formatRatio(market.price.ratioTo(market.basePrice)));
        const float room = names.front()->GetBox().GetSize(Rml::BoxArea::Content).x;
        const auto width = static_cast<float>(
            Rml::ElementUtilities::GetStringWidth(names.front(), content.resources[r].name));
        if (room - width < tightest) {
            tightest = room - width;
            clipped = content.resources[r].name;
        }
    }
    std::cout << std::format("    {}: Prices panel, \"{}\" has {:.1f}dp to spare in its name\n",
                             where, clipped, tightest);
}

// The Dashboard and the Market tab in both frames, with a standing order
// waiting that holds ore back under a floor at today's price: the row's
// longest summary, and the chip beside it. Returns 0, the layout's failure
// or this stage's own.
int probeExactPrices(Rml::Context& context, game::App& app, LoggingSystem& system,
                     const char* label) {
    Checks checks("PRICES");
    const sim::CompanyView& view = app.session().view();
    const int selected = app.model().selected_resource;
    if (selected < 0 || static_cast<std::size_t>(selected) >= view.markets.size()) {
        std::cout << "SCENARIO (prices): the Market tab has no resource selected\n";
        return kScenarioFailure;
    }
    const sim::ResourceId resource(static_cast<std::uint32_t>(selected));
    const sim::SetSellPolicy order{
        {}, resource, 1, kHeldBackTonnes, view.markets[resource.index()].price};
    app.session().submit(order);
    app.refreshNow();
    bool laidOut = true;
    for (const Frame& frame : kFrames) {
        const bool deck = static_cast<float>(frame.width) < kDeckWidth;
        context.SetDimensions(Rml::Vector2i(frame.width, frame.height));
        app.forceDeck(deck);
        std::cout << std::format("\n== {}, frame {}x{}{} ==\n", label, frame.width, frame.height,
                                 deck ? " (deck)" : "");
        const std::string size = std::format("{}x{}", frame.width, frame.height);
        laidOut = probeTab(context, app, system, frame, kDashboardTab, Popover::None) && laidOut;
        checkDashboardPrices(context, app, "dashboard " + size, checks);
        laidOut = probeTab(context, app, system, frame, kMarketTab, Popover::None) && laidOut;
        const float spare = checkMarketPriceCells(context, app, "market " + size, checks);
        checks.expect(spare >= -kSpillTolerance,
                      std::format("market {}: a price runs {:.1f}dp past its cell", size, -spare));
        checkMarketFloor(context, app, "market " + size, order, checks);
    }
    if (!checks.ok()) {
        return kPricesFailure;
    }
    return laidOut ? 0 : kLayoutFailure;
}

// Exact prices on the shipped game and on the copy with the widest ones,
// each on an app and a context of its own.
int probePrices(const ProbeHost& host, const sim::Content& shipped) {
    const int opening =
        onItsOwnApp(host, "prices", shipped, [&](Rml::Context& context, game::App& app) {
            return probeExactPrices(context, app, *host.system, "exact prices, new game");
        });
    const sim::Content wide = widePriceContent(shipped);
    const int widest =
        onItsOwnApp(host, "wideprices", wide, [&](Rml::Context& context, game::App& app) {
            return probeExactPrices(context, app, *host.system,
                                    "exact prices, every ore at $99,999.99");
        });
    return widest != 0 ? widest : opening;
}

// ---- the endings the player does not win (spec 010) ----

// The rival's race, on a copy the heuristic rival finishes within the
// probe's reach: stages that ask for no goods (the easy program with nothing
// dug), and no cash held back before a stage's research. The rival is not
// given orders: it plays its own game, and founds the program a few days
// after its first mine opens. The rules and the client are untouched.
constexpr int kRivalMaxDays = 2000;
constexpr std::uint32_t kPartnerStages = 2;  // BR-006: two stages make a lesser partner

sim::Content rivalWinsContent(const sim::Content& shipped) {
    sim::Content content = easyContent(shipped, {});
    content.ai.programCashBuffer = sim::Money::zero();
    return content;
}

// The stages each company completed, by the day the view first showed them:
// the probe's own record, to hold the log's days against.
struct StageWatch {
    std::vector<std::vector<sim::Day>> days;  // by company, a day a stage
};

void stepWatched(game::Session& session, StageWatch& watch) {
    const sim::Day today = session.day();
    std::vector<std::uint32_t> before;
    for (const sim::ProgramPublicView& program : session.view().programs) {
        before.push_back(program.stagesCompleted);
    }
    session.stepOneDay();
    watch.days.resize(before.size());
    for (std::size_t c = 0; c < before.size(); ++c) {
        for (std::uint32_t s = before[c]; s < session.view().programs[c].stagesCompleted; ++s) {
            watch.days[c].push_back(today);
        }
    }
}

// The day the probe saw each stage completed is the day the view records for
// it (what the end screen shows) and the day the log has.
void checkWatchedDays(const game::Session& session, const StageWatch& watch, Checks& checks) {
    const std::vector<sim::ProgramPublicView>& programs = session.view().programs;
    for (std::size_t c = 0; c < watch.days.size(); ++c) {
        checks.expect(c < programs.size() && programs[c].stageDays == watch.days[c],
                      std::format("the view's stage days for company {} are not the days the "
                                  "probe saw its stages completed",
                                  c));
        for (std::size_t s = 0; s < watch.days[c].size(); ++s) {
            const std::optional<sim::Day> logged = loggedDay(
                session, sim::NoticeKind::ProgramStageComplete,
                sim::CompanyId(static_cast<std::uint32_t>(c)), static_cast<std::int64_t>(s + 1));
            checks.expect(logged == watch.days[c][s],
                          std::format("company {} completed stage {} on day {}, and the log says "
                                      "day {}",
                                      c, s + 1, watch.days[c][s], logged.value_or(0)));
        }
    }
}

// A new game on the rival's copy, played until the rival founds the program.
// The player first completes `playerStages` stages by orders, then waits.
int probeRivalFounds(Rml::Context& context, game::App& app, LoggingSystem& system,
                     std::uint32_t playerStages, const std::string& result) {
    app.startGame(kScenarioSeed);
    game::Session& session = app.session();
    StageWatch watch;
    for (int day = 0; day < kRivalMaxDays && !session.gameOver(); ++day) {
        const std::optional<sim::ProgramStageView> stage = session.view().program.stage;
        if (stage && session.view().program.stagesCompleted < playerStages) {
            if (stage->researchState == sim::ProgramResearchState::NotStarted) {
                session.submit(sim::StartResearch{game::Session::kPlayer, stage->research});
            }
            if (stage->ready) {
                session.submit(sim::CompleteProgramStage{game::Session::kPlayer});
            }
        }
        stepWatched(session, watch);
    }
    const sim::CompanyView& view = session.view();
    if (!view.outcome.ended || !view.outcome.founder.valid() ||
        view.outcome.founder == game::Session::kPlayer ||
        view.program.stagesCompleted != playerStages) {
        std::cout << whereItStopped(view);
        std::cout << std::format(
            "SCENARIO (end, {}): the rival did not found the program with "
            "the player at {} stages (the player has {})\n",
            result, playerStages, view.program.stagesCompleted);
        return kScenarioFailure;
    }
    Checks checks("END");
    checkWatchedDays(session, watch, checks);
    std::cout << std::format(
        "\nend, {}: {} founded the program on day {}; the player completed {} "
        "of {} stages\n",
        result, view.companies[view.outcome.founder.index()].name, view.outcome.day,
        view.program.stagesCompleted, session.content().program.stages.size());
    const int code = probeEndScreen(context, app, system, result);
    return code == 0 && !checks.ok() ? kEndFailure : code;
}

// The deadline passing: the shipped content with the deadline a few weeks
// into the game, before either company can have completed a stage.
constexpr sim::Day kNearDeadline = 45;

sim::Content nearDeadlineContent(const sim::Content& shipped) {
    sim::Content content = shipped;
    content.program.deadlineDay = kNearDeadline;
    return content;
}

// The last day to start the stage's research passed on day 0 (spec 011,
// AC-005): with another project put in the lab, the Program tab says the day
// has passed and names the day the lab is free, and Needs you lists it.
int probePacePassed(Rml::Context& context, game::App& app, LoggingSystem& system) {
    game::Session& session = app.session();
    const sim::Content& content = session.content();
    sim::TechId other;
    {
        const sim::CompanyView& view = session.view();
        for (std::uint32_t i = 0; i < content.techs.size() && view.program.stage; ++i) {
            if (i != view.program.stage->research.index() && i < view.researchOpen.size() &&
                view.researchOpen[i] != 0 &&
                content.techs[i].cost <= view.cash - view.reservedCash) {
                other = sim::TechId(i);
                break;
            }
        }
    }
    if (!other.valid()) {
        std::cout << "SCENARIO (program): no project but the stage's can start on day 0\n";
        return kScenarioFailure;
    }
    session.submit(sim::StartResearch{{}, other});
    session.stepOneDay();
    app.refreshNow();
    const sim::ProgramView& program = session.view().program;
    if (!program.lastStartPassed || !program.labFreeDay || !program.paceWarning) {
        std::cout << std::format(
            "SCENARIO (program): with {} in the lab the view does not say the last day to start "
            "has passed and when the lab is free (day {})\n",
            content.techs[other.index()].name, session.view().day);
        return kScenarioFailure;
    }
    Checks checks("PROGRAM");
    checkPaceNeed(app, "pace passed, lab busy", checks);
    const bool laidOut =
        probeProgramFrames(context, app, system, "program, pace passed, lab busy", checks);
    return programResult(checks, laidOut);
}

int probeDeadline(Rml::Context& context, game::App& app, LoggingSystem& system) {
    app.startGame(kScenarioSeed);
    game::Session& session = app.session();
    // Day 0, the deadline closer than any company's research: the verdict
    // says the program cannot be founded in time (spec 011, AC-002), and no
    // rival is named in the status strip.
    app.refreshNow();
    if (!session.view().programs[game::Session::kPlayer.index()].pastDeadline) {
        std::cout << "SCENARIO (race): the player can still found the program before a "
                     "deadline 45 days away\n";
        return kScenarioFailure;
    }
    Checks race("RACE");
    const RaceExpected expected = simRace(session);
    race.same("past the deadline: the verdict", expected.verdict,
              game::text(session.content(), "ui.race_cannot_found"));
    const bool laidOut =
        probeRaceFrames(context, app, system, expected, "race, cannot found in time", race);
    if (!race.ok()) {
        return kRaceFailure;
    }
    if (!laidOut) {
        return kLayoutFailure;
    }
    if (const int code = probePacePassed(context, app, system); code != 0) {
        return code;
    }
    for (sim::Day day = 0; day <= kNearDeadline + 1 && !session.gameOver(); ++day) {
        session.stepOneDay();
    }
    const sim::CompanyView& view = session.view();
    if (!view.outcome.ended || view.outcome.founder.valid() || view.bankrupt) {
        std::cout << whereItStopped(view);
        std::cout << "SCENARIO (end, deadline): the deadline did not end the game\n";
        return kScenarioFailure;
    }
    std::cout << std::format(
        "\nend, deadline: the game ended on day {} of a deadline on day {} "
        "with no founder\n",
        view.outcome.day, view.deadlineDay);
    return probeEndScreen(context, app, system, "deadline");
}

// The player bankrupt: the losing mine's game (its opex is the probe's, far
// above anything its ore earns) played on until the courts step in. The
// outcome of the race is still open; the end screen says bankrupt.
constexpr int kBankruptMaxDays = 720;

int probeBankrupt(Rml::Context& context, game::App& app, LoggingSystem& system) {
    game::Session& session = app.session();
    for (int day = 0; day < kBankruptMaxDays && !session.gameOver(); ++day) {
        session.stepOneDay();
    }
    const sim::CompanyView& view = session.view();
    if (!view.bankrupt) {
        std::cout << whereItStopped(view);
        std::cout << "SCENARIO (end, bankrupt): the losing mine did not bankrupt the player\n";
        return kScenarioFailure;
    }
    std::cout << std::format("\nend, bankrupt: the player fell with cash {}, the race {}\n",
                             game::formatMoney(view.cash),
                             view.outcome.ended ? "ended" : "still open");
    return probeEndScreen(context, app, system, "bankrupt");
}

// The endings the rival or the calendar decides, each on a copy of the
// content and so on an app of its own. Returns 0, or the exit code of the
// last pass that failed.
int probeEndings(const ProbeHost& host, const sim::Content& shipped) {
    int exitCode = 0;
    const sim::Content rivalWins = rivalWinsContent(shipped);
    const int rival =
        onItsOwnApp(host, "rival", rivalWins, [&](Rml::Context& context, game::App& app) {
            const int lost = probeRivalFounds(context, app, *host.system, 0, "lost");
            const int partner =
                probeRivalFounds(context, app, *host.system, kPartnerStages, "partner");
            return partner != 0 ? partner : lost;
        });
    exitCode = rival != 0 ? rival : exitCode;
    const sim::Content nearDeadline = nearDeadlineContent(shipped);
    const int deadline =
        onItsOwnApp(host, "deadline", nearDeadline, [&](Rml::Context& context, game::App& app) {
            return probeDeadline(context, app, *host.system);
        });
    return deadline != 0 ? deadline : exitCode;
}

// The Program tab's later states (spec 010), each on a copy of the content
// and so on an app and a context of its own: a first stage that asks for a
// good the company cannot dig, then a program small enough to complete.
// Returns 0, or the exit code of the last pass that failed.
int probeProgramCopies(const ProbeHost& host, const sim::Content& shipped,
                       const std::vector<std::int64_t>& dug) {
    int exitCode = 0;
    if (const std::optional<sim::Content> locked = lockedContent(shipped)) {
        const int code =
            onItsOwnApp(host, "locked", *locked, [&](Rml::Context& context, game::App& app) {
                return probeProgramLocked(context, app, *host.system);
            });
        exitCode = code != 0 ? code : exitCode;
    } else {
        std::cout << "SCENARIO (program): no stage asks for a good that needs research\n";
        exitCode = kScenarioFailure;
    }
    if (std::ranges::any_of(dug, [](std::int64_t tonnes) { return tonnes > 0; })) {
        const sim::Content easy = easyContent(shipped, dug);
        const int code =
            onItsOwnApp(host, "easy", easy, [&](Rml::Context& context, game::App& app) {
                return probeProgramToTheEnd(context, app, *host.system, easy);
            });
        exitCode = code != 0 ? code : exitCode;
    } else {
        std::cout << "SCENARIO (program): the fixed game's mine dug nothing to race with\n";
        exitCode = kScenarioFailure;
    }
    return exitCode;
}

// The shipped content with a rival that keeps no share of its stock for the
// program: it delivers nothing, and starts its stage research as the AI
// always does, once it is producing and can pay for it.
sim::Content rivalResearchesContent(const sim::Content& shipped) {
    sim::Content content = shipped;
    content.ai.programReserveShare = sim::Fixed::zero();
    return content;
}

// The player delivers what it digs and leaves its research unstarted while
// the rival starts its own (spec 011, AC-002): more delivered but later on
// research time, the player reads Behind with the gap in days, on every tab
// in both frames, against a rival with a long name. Returns a failure code
// or 0.
int probeRivalResearching(Rml::Context& context, game::App& app, LoggingSystem& system,
                          const sim::Content& content) {
    app.setRivalName(std::string(kLongRivalName));
    if (!playToStock(app, content)) {
        return kScenarioFailure;
    }
    game::Session& session = app.session();
    const auto rival = [&]() -> const sim::ProgramPublicView* {
        for (const sim::ProgramPublicView& program : session.view().programs) {
            if (program.company != game::Session::kPlayer) {
                return &program;
            }
        }
        return nullptr;
    };
    const auto researching = [&] {
        return rival() != nullptr &&
               rival()->researchState == sim::ProgramResearchState::InProgress;
    };
    // The race's rules on (playToStock switched every rule off), so the day
    // the rival starts its research stops the clock as a race stop.
    game::AutoPause race;
    race.enabled.fill(false);
    race.set(game::PauseOn::RivalProgram, true);
    race.set(game::PauseOn::FellBehind, true);
    session.setAutoPause(race);
    for (int day = 0; day < kRivalResearchMaxDays && !researching() && !session.gameOver(); ++day) {
        deliverAll(session);
        session.stepOneDay();
    }
    // The day the rival started its research stopped the clock (spec 011).
    if (researching()) {
        if (const int code = probeRaceStop(context, app, system); code != 0) {
            return code;
        }
    }
    // Each day the player's research waits moves its earliest founding a day
    // later, and the rival's stays: a gap of more than one day.
    for (int day = 0; day < kRivalLeadDays && researching() && !session.gameOver(); ++day) {
        deliverAll(session);
        session.stepOneDay();
    }
    app.refreshNow();
    const sim::CompanyView& view = session.view();
    const sim::ProgramPublicView& mine = view.programs[game::Session::kPlayer.index()];
    const sim::ProgramPublicView* theirs = rival();
    std::cout << std::format(
        "\nrace, rival researching: day {}; you at {} permille, earliest founding {}; rival at "
        "{} permille, earliest founding {}\n",
        view.day, mine.progressPermille, mine.earliestFoundingDay.value_or(0),
        theirs != nullptr ? theirs->progressPermille : 0,
        theirs != nullptr ? theirs->earliestFoundingDay.value_or(0) : 0);
    if (!researching() || view.outcome.ended ||
        mine.researchState != sim::ProgramResearchState::NotStarted ||
        mine.progressPermille <= theirs->progressPermille || !mine.earliestFoundingDay ||
        !theirs->earliestFoundingDay || *mine.earliestFoundingDay <= *theirs->earliestFoundingDay) {
        std::cout << "SCENARIO (race): the rival did not start its research while the player "
                     "had delivered more and had not started its own\n";
        return kScenarioFailure;
    }
    Checks checks("RACE");
    const RaceExpected expected = simRace(session);
    const std::int64_t gap = static_cast<std::int64_t>(*mine.earliestFoundingDay) -
                             static_cast<std::int64_t>(*theirs->earliestFoundingDay);
    checks.same(
        "more delivered, research not started: the verdict", expected.verdict,
        game::textPlural(content, "ui.race_behind_by", gap, {{"days", game::formatCount(gap)}}));
    const bool laidOut =
        probeRaceFrames(context, app, system, expected, "race, rival researching", checks);
    if (!checks.ok()) {
        return kRaceFailure;
    }
    return laidOut ? 0 : kLayoutFailure;
}

constexpr int kUncaught = 9;

// The whole probe; main reports whatever it throws as a failure.
int run(std::span<char*> args) {
    const std::filesystem::path assets = args.size() > 1 ? args[1] : DC_ASSETS_DIR;
    const content::LoadResult loaded = content::loadFromDirectory(DC_DATA_DIR);
    if (!loaded.content) {
        return 1;
    }
    StubRender render;
    LoggingSystem system;
    Rml::SetRenderInterface(&render);
    Rml::SetSystemInterface(&system);
    Rml::Initialise();
    for (const char* face : {"Barlow-Regular.ttf", "Barlow-Medium.ttf", "Barlow-SemiBold.ttf",
                             "BarlowCondensed-SemiBold.ttf", "BarlowCondensed-Bold.ttf",
                             "JetBrainsMono-Regular.ttf", "JetBrainsMono-Medium.ttf"}) {
        Rml::LoadFontFace((assets / "fonts" / face).string());
    }
    game::registerChartElement();
    Rml::Context* context =
        Rml::CreateContext("main", Rml::Vector2i(kFrames[0].width, kFrames[0].height));
    auto services = platform::makeNullServices(std::filesystem::temp_directory_path(), "Probe");
    // The playtest trace is exercised too: the app writes one into a scratch
    // directory and it must start with a new_game record.
    const std::filesystem::path playtest =
        std::filesystem::temp_directory_path() / "deep_claim_probe_playtest";
    std::filesystem::remove_all(playtest);
    int exitCode = 0;
    std::vector<std::int64_t> dug;  // what the fixed game's mine digs a day, ore by ore
    {
        game::App app(*context, *services, *loaded.content, assets, playtest, "probe");
        if (!app.loadDocument()) {
            exitCode = 2;
        }
        if (!checkTrace(app)) {
            exitCode = 4;
        }
        app.update(0.0);
        if (!checkFreshNeeds(app, *loaded.content)) {
            exitCode = 5;
        }
        // The Program tab on a new game (spec 010).
        if (const int code = probeProgramFresh(*context, app, system); code != 0) {
            exitCode = code;
        }
        // A fresh log has nothing from rivals yet: the filters' empty line.
        if (!probeFrames(*context, app, system, "fresh", Popover::None) ||
            !probeLogFilters(*context, app, system)) {
            exitCode = 3;
        }
        // The Stops menu's row for the rival's progress (spec 010).
        if (const int code = probeStopsMenu(*context, app, system); code != 0) {
            exitCode = code;
        }
        if (const int code = probeScenarios(*context, app, system, *loaded.content); code != 0) {
            exitCode = code;
        }
        // Last, because it starts the game again.
        if (const int code = probePendingOrders(*context, app, system, *loaded.content);
            code != 0) {
            exitCode = code;
        }
        // The race (spec 010), on a game of its own.
        if (const int code = probeRace(*context, app, system, *loaded.content, dug); code != 0) {
            exitCode = code;
        }
        // A standing bid (spec 011), on a game of its own.
        if (const int code = probeStandingBid(*context, app, system, *loaded.content); code != 0) {
            exitCode = code;
        }
    }
    // The losing mine has an app of its own, made once the first is gone: an
    // app keeps the content it was made with, and this one is the probe's
    // copy. It has a context of its own too, because a context keeps the
    // data types an app declared in it and warns when they are declared again.
    Rml::RemoveContext("main");
    context = Rml::CreateContext("losing", Rml::Vector2i(kFrames[0].width, kFrames[0].height));
    {
        const sim::Content losing = losingContent(*loaded.content);
        game::App app(*context, *services, losing, assets, playtest, "probe");
        if (!app.loadDocument()) {
            exitCode = 2;
        }
        app.update(0.0);
        if (const int code = probeLosingMine(*context, app, system, losing); code != 0) {
            exitCode = code;
        }
        // The same game on to the player's bankruptcy: the end screen.
        if (const int code = probeBankrupt(*context, app, system); code != 0) {
            exitCode = code;
        }
    }
    Rml::RemoveContext("losing");
    const ProbeHost host{services.get(), &system, {assets, playtest}};
    if (const int code = probeProgramCopies(host, *loaded.content, dug); code != 0) {
        exitCode = code;
    }
    // Exact prices on the Market tab and the Dashboard (spec 010 BR-014).
    if (const int code = probePrices(host, *loaded.content); code != 0) {
        exitCode = code;
    }
    // The endings the player does not win (spec 010).
    if (const int code = probeEndings(host, *loaded.content); code != 0) {
        exitCode = code;
    }
    // Behind on research time with more delivered (spec 011).
    const sim::Content rivalResearches = rivalResearchesContent(*loaded.content);
    if (const int code = onItsOwnApp(host, "rivalresearch", rivalResearches,
                                     [&](Rml::Context& own, game::App& app) {
                                         return probeRivalResearching(own, app, system,
                                                                      rivalResearches);
                                     });
        code != 0) {
        exitCode = code;
    }
    // A stop with a count (spec 011, BR-009).
    if (const int code = onItsOwnApp(
            host, "stopcount", *loaded.content,
            [&](Rml::Context& own, game::App& app) { return probeStopCount(own, app, system); });
        code != 0) {
        exitCode = code;
    }
    // The land run out, on the copy where the race does not end first.
    const sim::Content landRunsOut = landRunsOutContent(*loaded.content);
    if (const int code = onItsOwnApp(host, "endgame", landRunsOut,
                                     [&](Rml::Context& own, game::App& app) {
                                         return probeEndgame(own, app, system, landRunsOut);
                                     });
        code != 0) {
        exitCode = code;
    }
    std::cout << (exitCode == 0 ? "\nLAYOUT OK\n" : "\nLAYOUT FAILURE\n");
    // The app, its document and data model are gone before RmlUi shuts down.
    Rml::Shutdown();
    return exitCode;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& error) {
        std::fputs("LAYOUT FAILURE: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputs("\n", stderr);
    } catch (...) {
        std::fputs("LAYOUT FAILURE: unknown exception\n", stderr);
    }
    return kUncaught;
}
