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
// open and closed, before and after one order is cancelled.
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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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
        dimensions = {144, 24};  // the icon sheet's size, so sprites resolve
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

constexpr std::array<const char*, 6> kTabs = {"dashboard", "plots",   "market",
                                              "research",  "finance", "log"};
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

// The second half of the late scenario, probed separately so the survey row
// above is still on the dashboard when it is laid out.
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
// Returns 0, or the exit code of the first stage that failed.
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
    if (!playEndgameScenario(app, content)) {
        return kScenarioFailure;
    }
    const bool laidOut = probeFrames(context, app, system, "endgame", Popover::None);
    const bool filtered = probeLogFilters(context, app, system);
    return laidOut && filtered ? 0 : kLayoutFailure;
}

// ---- orders waiting for the clock (spec 007) ----

constexpr int kPendingFailure = 6;

// Collects what is wrong with the pending state, printing each failure.
class Checks {
public:
    void expect(bool held, std::string_view what) {
        if (!held) {
            std::cout << "PENDING: " << what << '\n';
            ok_ = false;
        }
    }
    template <typename T>
    void same(std::string_view what, const T& actual, const T& expected) {
        if (!(actual == expected)) {
            std::cout << std::format("PENDING: {}: \"{}\", expected \"{}\"\n", what, actual,
                                     expected);
            ok_ = false;
        }
    }
    [[nodiscard]] bool ok() const { return ok_; }

private:
    bool ok_ = true;
};

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

}  // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
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
        // A fresh log has nothing from rivals yet: the filters' empty line.
        if (!probeFrames(*context, app, system, "fresh", Popover::None) ||
            !probeLogFilters(*context, app, system)) {
            exitCode = 3;
        }
        if (const int code = probeScenarios(*context, app, system, *loaded.content); code != 0) {
            exitCode = code;
        }
        // Last, because it starts the game again.
        if (const int code = probePendingOrders(*context, app, system, *loaded.content);
            code != 0) {
            exitCode = code;
        }
        std::cout << (exitCode == 0 ? "\nLAYOUT OK\n" : "\nLAYOUT FAILURE\n");
    }
    // The app, its document and data model are gone before RmlUi shuts down.
    Rml::Shutdown();
    return exitCode;
}
