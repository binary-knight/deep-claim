#include "app.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/types.h"
#include "sim/view.h"

#include "chart_element.h"
#include "game/auto_pause.h"
#include "game/format.h"
#include "game/session.h"
#include "game/trace.h"
#include "platform/services.h"
#include "ui_model.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Variant.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace game {

namespace {

constexpr double kStatusSeconds = 6.0;
constexpr int kMaxScreenshots = 200;   // about 3.4 MB each, uncompressed
constexpr float kDeckWidth = 1300.0F;  // narrower windows get the Steam Deck sizes
const char* const kSaveFile = "autosave.dcsave";
const char* const kPauseFile = "autopause.txt";
// By tab number; Program (spec 010) is the seventh, and second on the rail.
constexpr std::array<const char*, 7> kTabNames = {"dashboard", "plots", "market", "research",
                                                  "finance",   "log",   "program"};

std::uint64_t clockSeed() {
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

// Local wall-clock stamp for file names: "20261008-143015".
std::string timeStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}", local.tm_year + 1900, local.tm_mon + 1,
                       local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
}

bool writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) byte buffer to stream
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    return static_cast<bool>(file);
}

// Notices the status line shows in the danger colour.
bool noticeIsDanger(sim::NoticeKind kind) {
    switch (kind) {
        case sim::NoticeKind::CommandRejected:
        case sim::NoticeKind::InsolvencyWarning:
        case sim::NoticeKind::Bankrupt:
        case sim::NoticeKind::MineFoundNothing:
        case sim::NoticeKind::IdleMineWarning:
        case sim::NoticeKind::BidForfeited:
            return true;
        default:
            return false;
    }
}

// The status line for the first of the day's stops (until task 013's stop
// panel replaces it).
std::string stopStatus(const sim::Content& content, const Session& session) {
    const std::string line = describeNotice(content, session.view(), session.stopsToday().front());
    return text(content, "status.paused_by", {{"text", line}});
}

}  // namespace

App::App(Rml::Context& context, platform::Services& services, const sim::Content& content,
         std::filesystem::path assetsDir, std::filesystem::path playtestDir, std::string buildId)
    : context_(&context),
      services_(&services),
      content_(&content),
      assetsDir_(std::move(assetsDir)),
      playtestDir_(std::move(playtestDir)),
      buildId_(std::move(buildId)) {
    openTrace();
    loadPauseRules();
    newGame(clockSeed());
    bindModel();
    PriceChartElement::setProvider([this](int resource, int days) {
        ChartSeries series;
        if (resource < 0 || resource >= static_cast<int>(content_->resources.size())) {
            return series;
        }
        const sim::ResourceId id(static_cast<std::uint32_t>(resource));
        const std::vector<sim::Money>& history = session_->priceHistory(id);
        const std::size_t count =
            std::min<std::size_t>(history.size(), static_cast<std::size_t>(std::max(days, 2)));
        for (std::size_t i = history.size() - count; i < history.size(); ++i) {
            series.values.push_back(history[i].cents());
        }
        series.baseline = content_->resources[id.index()].basePrice.cents();
        return series;
    });
    PriceChartElement::setCashProvider([this](int days) {
        ChartSeries series;
        const std::vector<sim::Money>& history = session_->cashHistory();
        const std::size_t count =
            std::min<std::size_t>(history.size(), static_cast<std::size_t>(std::max(days, 2)));
        for (std::size_t i = history.size() - count; i < history.size(); ++i) {
            series.values.push_back(history[i].cents());
        }
        series.baseline = 0;
        series.hasBaseline = true;  // the zero line: below it is insolvency
        return series;
    });
}

App::~App() {
    traceEnd();
    PriceChartElement::setProvider(nullptr);
    PriceChartElement::setCashProvider(nullptr);
    if (document_ != nullptr) {
        document_->Close();
    }
    context_->RemoveDataModel("game");
}

void App::traceEnd() {
    // With the outcome, when the game was played to its end (spec 010). The
    // writer keeps one end record a game, so this is safe to call again.
    if (trace_ && session_) {
        trace_->end(session_->day(), session_->view());
    }
}

void App::newGame(std::uint64_t seed) {
    // The game being replaced ends here as far as the trace goes: without
    // this a finished game followed by New game would lose its outcome.
    traceEnd();
    sim::GameSetup setup;
    setup.playerName = std::string(services_->playerName());
    setup.aiNames = {"Northfield Minerals"};
    session_ = std::make_unique<Session>(*content_, seed, setup);
    session_->setTrace(trace_.get());
    session_->setAutoPause(pauseRules_);
    if (trace_) {
        trace_->newGame(seed, setup, sim::hashContent(*content_), buildId_);
    }
    model_.selected_plot = -1;
    model_.selected_resource = 0;
    model_.tab = 0;
    resetLoanInput();
    syncPolicyInputs();
    lastRefreshedDay_ = session_->day();
}

void App::loadPauseRules() {
    const std::ifstream file(services_->saveDirectory() / kPauseFile);
    if (!file) {
        return;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    AutoPause rules = AutoPause::defaults();
    rules.fromText(buffer.str());  // malformed lines keep their defaults
    pauseRules_ = rules;
}

void App::savePauseRules() {
    std::ofstream file(services_->saveDirectory() / kPauseFile, std::ios::trunc);
    file << pauseRules_.toText();
}

void App::openTrace() {
    if (playtestDir_.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(playtestDir_, error);
    // Two launches in the same second must not overwrite each other.
    const std::string stamp = "trace-" + timeStamp();
    traceStem_ = stamp;
    for (int n = 2; std::filesystem::exists(playtestDir_ / (traceStem_ + ".jsonl"), error); ++n) {
        traceStem_ = std::format("{}-{}", stamp, n);
    }
    tracePath_ = playtestDir_ / (traceStem_ + ".jsonl");
    traceFile_.open(tracePath_, std::ios::trunc);
    if (!traceFile_) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) RmlUi logging is printf-style
        Rml::Log::Message(Rml::Log::LT_WARNING, "Could not open the playtest trace %s",
                          tracePath_.string().c_str());
        tracePath_.clear();
        return;
    }
    trace_ = std::make_unique<TraceWriter>(traceFile_);
}

void App::traceUi(std::string_view kind, std::string_view detail) {
    if (trace_) {
        trace_->ui(session_->day(), kind, detail);
    }
}

std::string App::keepSaveCopy(const char* role, const std::vector<std::uint8_t>& bytes) {
    if (!trace_) {
        return {};
    }
    ++saveCopies_;
    std::string name = std::format("{}-{}-{}.dcsave", traceStem_, role, saveCopies_);
    if (!writeFile(playtestDir_ / name, bytes)) {
        return {};
    }
    return name;
}

void App::requestScreenshot() {
    if (!trace_ || screenshots_ >= kMaxScreenshots || screenshotRequest_.has_value()) {
        return;
    }
    ++screenshots_;
    screenshotRequest_ = playtestDir_ / std::format("{}-shot-{:03}.png", traceStem_, screenshots_);
}

std::optional<std::filesystem::path> App::takeScreenshotRequest() {
    std::optional<std::filesystem::path> request;
    request.swap(screenshotRequest_);
    return request;
}

void App::screenshotDone(const std::filesystem::path& file, bool ok) {
    if (!ok) {
        setStatus(text(*content_, "status.screenshot_failed"), true);
        return;
    }
    if (trace_) {
        trace_->screenshot(session_->day(), file.filename().string());
    }
    setStatus(text(*content_, "status.screenshot_saved", {{"name", file.filename().string()}}));
}

void App::bindModel() {
    Rml::DataModelConstructor ctor = context_->CreateDataModel("game");
    if (!ctor) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) RmlUi logging is printf-style
        Rml::Log::Message(Rml::Log::LT_ERROR, "Could not create the game data model");
        return;
    }
    // The order marks first: the rows and the panel below hold them.
    if (auto h = ctor.RegisterStruct<OrderMark>()) {
        h.RegisterMember("pending", &OrderMark::pending);
        h.RegisterMember("text", &OrderMark::text);
        h.RegisterMember("id", &OrderMark::id);
        h.RegisterMember("not_covered", &OrderMark::not_covered);
    }
    if (auto h = ctor.RegisterStruct<PendingRow>()) {
        h.RegisterMember("id", &PendingRow::id);
        h.RegisterMember("text", &PendingRow::text);
        h.RegisterMember("not_covered", &PendingRow::not_covered);
    }
    if (auto h = ctor.RegisterStruct<NeedRow>()) {
        h.RegisterMember("number", &NeedRow::number);
        h.RegisterMember("title", &NeedRow::title);
        h.RegisterMember("sub", &NeedRow::sub);
        h.RegisterMember("days", &NeedRow::days);
        h.RegisterMember("action", &NeedRow::action);
        h.RegisterMember("target", &NeedRow::target);
        h.RegisterMember("act_label", &NeedRow::act_label);
        h.RegisterMember("order", &NeedRow::order);
    }
    if (auto h = ctor.RegisterStruct<SaleRow>()) {
        h.RegisterMember("index", &SaleRow::index);
        h.RegisterMember("name", &SaleRow::name);
        h.RegisterMember("region", &SaleRow::region);
        h.RegisterMember("area", &SaleRow::area);
        h.RegisterMember("ask", &SaleRow::ask);
        h.RegisterMember("bids", &SaleRow::bids);
        h.RegisterMember("closes", &SaleRow::closes);
        h.RegisterMember("survey", &SaleRow::survey);
        h.RegisterMember("error", &SaleRow::error);
        h.RegisterMember("flag", &SaleRow::flag);
        h.RegisterMember("flag_class", &SaleRow::flag_class);
        h.RegisterMember("has_flag", &SaleRow::has_flag);
        h.RegisterMember("major", &SaleRow::major);
    }
    if (auto h = ctor.RegisterStruct<SoldRow>()) {
        h.RegisterMember("index", &SoldRow::index);
        h.RegisterMember("name", &SoldRow::name);
        h.RegisterMember("buyer", &SoldRow::buyer);
        h.RegisterMember("paid", &SoldRow::paid);
        h.RegisterMember("over", &SoldRow::over);
        h.RegisterMember("day", &SoldRow::day);
        h.RegisterMember("mine", &SoldRow::mine);
    }
    if (auto h = ctor.RegisterStruct<MineFigures>()) {
        h.RegisterMember("shown", &MineFigures::shown);
        h.RegisterMember("earns", &MineFigures::earns);
        h.RegisterMember("loss", &MineFigures::loss);
        h.RegisterMember("mothballed", &MineFigures::mothballed);
        h.RegisterMember("reopen", &MineFigures::reopen);
        h.RegisterMember("reopen_loss", &MineFigures::reopen_loss);
        h.RegisterMember("break_even", &MineFigures::break_even);
        h.RegisterMember("note", &MineFigures::note);
    }
    if (auto h = ctor.RegisterStruct<OwnedRow>()) {
        h.RegisterMember("index", &OwnedRow::index);
        h.RegisterMember("name", &OwnedRow::name);
        h.RegisterMember("region", &OwnedRow::region);
        h.RegisterMember("area", &OwnedRow::area);
        h.RegisterMember("mine", &OwnedRow::mine);
        h.RegisterMember("mine_class", &OwnedRow::mine_class);
        h.RegisterMember("output", &OwnedRow::output);
        h.RegisterMember("remaining", &OwnedRow::remaining);
        h.RegisterMember("figures", &OwnedRow::figures);
        h.RegisterMember("can_toggle", &OwnedRow::can_toggle);
        h.RegisterMember("mine_operating", &OwnedRow::mine_operating);
        h.RegisterMember("can_build", &OwnedRow::can_build);
        h.RegisterMember("mine_index", &OwnedRow::mine_index);
        h.RegisterMember("build_order", &OwnedRow::build_order);
        h.RegisterMember("mine_order", &OwnedRow::mine_order);
    }
    if (auto h = ctor.RegisterStruct<EstimateRow>()) {
        h.RegisterMember("resource", &EstimateRow::resource);
        h.RegisterMember("tonnes", &EstimateRow::tonnes);
        h.RegisterMember("error", &EstimateRow::error);
        h.RegisterMember("needs", &EstimateRow::needs);
        h.RegisterMember("locked", &EstimateRow::locked);
        h.RegisterMember("major", &EstimateRow::major);
    }
    ctor.RegisterArray<std::vector<EstimateRow>>();
    if (auto h = ctor.RegisterStruct<PlotPanel>()) {
        h.RegisterMember("any", &PlotPanel::any);
        h.RegisterMember("index", &PlotPanel::index);
        h.RegisterMember("title", &PlotPanel::title);
        h.RegisterMember("area", &PlotPanel::area);
        h.RegisterMember("ask", &PlotPanel::ask);
        h.RegisterMember("auction", &PlotPanel::auction);
        h.RegisterMember("capex", &PlotPanel::capex);
        h.RegisterMember("odds", &PlotPanel::odds);
        h.RegisterMember("owner", &PlotPanel::owner);
        h.RegisterMember("survey_title", &PlotPanel::survey_title);
        h.RegisterMember("estimates", &PlotPanel::estimates);
        h.RegisterMember("value", &PlotPanel::value);
        h.RegisterMember("notice", &PlotPanel::notice);
        h.RegisterMember("notice_class", &PlotPanel::notice_class);
        h.RegisterMember("contest", &PlotPanel::contest);
        h.RegisterMember("listed", &PlotPanel::listed);
        h.RegisterMember("owned", &PlotPanel::owned);
        h.RegisterMember("can_survey", &PlotPanel::can_survey);
        h.RegisterMember("survey_label", &PlotPanel::survey_label);
        h.RegisterMember("survey_note", &PlotPanel::survey_note);
        h.RegisterMember("can_afford_survey", &PlotPanel::can_afford_survey);
        h.RegisterMember("survey_blocked", &PlotPanel::survey_blocked);
        h.RegisterMember("sale", &PlotPanel::sale);
        h.RegisterMember("can_build", &PlotPanel::can_build);
        h.RegisterMember("can_afford_mine", &PlotPanel::can_afford_mine);
        h.RegisterMember("build_label", &PlotPanel::build_label);
        h.RegisterMember("build_blocked", &PlotPanel::build_blocked);
        h.RegisterMember("can_toggle_mine", &PlotPanel::can_toggle_mine);
        h.RegisterMember("mine_operating", &PlotPanel::mine_operating);
        h.RegisterMember("mine_index", &PlotPanel::mine_index);
        h.RegisterMember("figures", &PlotPanel::figures);
        h.RegisterMember("survey_order", &PlotPanel::survey_order);
        h.RegisterMember("bid_order", &PlotPanel::bid_order);
        h.RegisterMember("build_order", &PlotPanel::build_order);
        h.RegisterMember("mine_order", &PlotPanel::mine_order);
    }
    if (auto h = ctor.RegisterStruct<ResourceRow>()) {
        h.RegisterMember("index", &ResourceRow::index);
        h.RegisterMember("name", &ResourceRow::name);
        h.RegisterMember("price", &ResourceRow::price);
        h.RegisterMember("price_unit", &ResourceRow::price_unit);
        h.RegisterMember("base_line", &ResourceRow::base_line);
        h.RegisterMember("market_sub", &ResourceRow::market_sub);
        h.RegisterMember("stock", &ResourceRow::stock);
        h.RegisterMember("stock_sub", &ResourceRow::stock_sub);
        h.RegisterMember("stock_value", &ResourceRow::stock_value);
        h.RegisterMember("ratio", &ResourceRow::ratio);
        h.RegisterMember("policy", &ResourceRow::policy);
        h.RegisterMember("auto_sold", &ResourceRow::auto_sold);
        h.RegisterMember("locked_note", &ResourceRow::locked_note);
        h.RegisterMember("locked", &ResourceRow::locked);
        h.RegisterMember("ratio_warn", &ResourceRow::ratio_warn);
        h.RegisterMember("has_stock", &ResourceRow::has_stock);
        h.RegisterMember("policy_on", &ResourceRow::policy_on);
        h.RegisterMember("selected", &ResourceRow::selected);
        h.RegisterMember("sale_order", &ResourceRow::sale_order);
        h.RegisterMember("policy_order", &ResourceRow::policy_order);
    }
    if (auto h = ctor.RegisterStruct<TechRow>()) {
        h.RegisterMember("index", &TechRow::index);
        h.RegisterMember("name", &TechRow::name);
        h.RegisterMember("needs", &TechRow::needs);
        h.RegisterMember("cost", &TechRow::cost);
        h.RegisterMember("days", &TechRow::days);
        h.RegisterMember("effects", &TechRow::effects);
        h.RegisterMember("chip", &TechRow::chip);
        h.RegisterMember("chip_class", &TechRow::chip_class);
        h.RegisterMember("until", &TechRow::until);
        h.RegisterMember("blocked", &TechRow::blocked);
        h.RegisterMember("can_start", &TechRow::can_start);
        h.RegisterMember("researching", &TechRow::researching);
        h.RegisterMember("order", &TechRow::order);
    }
    if (auto h = ctor.RegisterStruct<LoanRow>()) {
        h.RegisterMember("index", &LoanRow::index);
        h.RegisterMember("name", &LoanRow::name);
        h.RegisterMember("principal", &LoanRow::principal);
        h.RegisterMember("remaining", &LoanRow::remaining);
        h.RegisterMember("instalment", &LoanRow::instalment);
        h.RegisterMember("months", &LoanRow::months);
        h.RegisterMember("active", &LoanRow::active);
        h.RegisterMember("can_repay_all", &LoanRow::can_repay_all);
        h.RegisterMember("repay_order", &LoanRow::repay_order);
    }
    if (auto h = ctor.RegisterStruct<LogRow>()) {
        h.RegisterMember("day", &LogRow::day);
        h.RegisterMember("text", &LogRow::text);
        h.RegisterMember("warning", &LogRow::warning);
    }
    if (auto h = ctor.RegisterStruct<RaceRow>()) {
        h.RegisterMember("name", &RaceRow::name);
        h.RegisterMember("stage", &RaceRow::stage);
        h.RegisterMember("progress", &RaceRow::progress);
        h.RegisterMember("bar_width", &RaceRow::bar_width);
        h.RegisterMember("research", &RaceRow::research);
        h.RegisterMember("you", &RaceRow::you);
    }
    if (auto h = ctor.RegisterStruct<ProgramStageRow>()) {
        h.RegisterMember("heading", &ProgramStageRow::heading);
        h.RegisterMember("state", &ProgramStageRow::state);
        h.RegisterMember("state_class", &ProgramStageRow::state_class);
        h.RegisterMember("current", &ProgramStageRow::current);
    }
    if (auto h = ctor.RegisterStruct<ProgramNeedRow>()) {
        h.RegisterMember("index", &ProgramNeedRow::index);
        h.RegisterMember("good", &ProgramNeedRow::good);
        h.RegisterMember("needs", &ProgramNeedRow::needs);
        h.RegisterMember("delivered", &ProgramNeedRow::delivered);
        h.RegisterMember("stock", &ProgramNeedRow::stock);
        h.RegisterMember("still", &ProgramNeedRow::still);
        h.RegisterMember("can_deliver", &ProgramNeedRow::can_deliver);
        h.RegisterMember("deliver_label", &ProgramNeedRow::deliver_label);
        h.RegisterMember("note", &ProgramNeedRow::note);
        h.RegisterMember("locked_note", &ProgramNeedRow::locked_note);
        h.RegisterMember("order", &ProgramNeedRow::order);
    }
    ctor.RegisterArray<std::vector<ProgramNeedRow>>();
    if (auto h = ctor.RegisterStruct<ProgramPanel>()) {
        h.RegisterMember("title", &ProgramPanel::title);
        h.RegisterMember("intro", &ProgramPanel::intro);
        h.RegisterMember("cover_warning", &ProgramPanel::cover_warning);
        h.RegisterMember("complete", &ProgramPanel::complete);
        h.RegisterMember("done_line", &ProgramPanel::done_line);
        h.RegisterMember("has_stage", &ProgramPanel::has_stage);
        h.RegisterMember("heading", &ProgramPanel::heading);
        h.RegisterMember("col_good", &ProgramPanel::col_good);
        h.RegisterMember("col_needs", &ProgramPanel::col_needs);
        h.RegisterMember("col_delivered", &ProgramPanel::col_delivered);
        h.RegisterMember("col_stock", &ProgramPanel::col_stock);
        h.RegisterMember("col_still", &ProgramPanel::col_still);
        h.RegisterMember("needs", &ProgramPanel::needs);
        h.RegisterMember("cost", &ProgramPanel::cost);
        h.RegisterMember("research_name", &ProgramPanel::research_name);
        h.RegisterMember("research", &ProgramPanel::research);
        h.RegisterMember("research_open", &ProgramPanel::research_open);
        h.RegisterMember("can_research", &ProgramPanel::can_research);
        h.RegisterMember("research_label", &ProgramPanel::research_label);
        h.RegisterMember("research_blocked", &ProgramPanel::research_blocked);
        h.RegisterMember("research_order", &ProgramPanel::research_order);
        h.RegisterMember("ready", &ProgramPanel::ready);
        h.RegisterMember("complete_label", &ProgramPanel::complete_label);
        h.RegisterMember("complete_order", &ProgramPanel::complete_order);
        h.RegisterMember("ready_line", &ProgramPanel::ready_line);
        h.RegisterMember("blocked_research", &ProgramPanel::blocked_research);
        h.RegisterMember("blocked_deliveries", &ProgramPanel::blocked_deliveries);
        h.RegisterMember("blocked_cash", &ProgramPanel::blocked_cash);
    }
    if (auto h = ctor.RegisterStruct<EndStageLine>()) {
        h.RegisterMember("text", &EndStageLine::text);
        h.RegisterMember("reached", &EndStageLine::reached);
    }
    ctor.RegisterArray<std::vector<EndStageLine>>();
    if (auto h = ctor.RegisterStruct<EndCompany>()) {
        h.RegisterMember("name", &EndCompany::name);
        h.RegisterMember("stages", &EndCompany::stages);
        h.RegisterMember("lines", &EndCompany::lines);
        h.RegisterMember("you", &EndCompany::you);
    }
    ctor.RegisterArray<std::vector<EndCompany>>();
    if (auto h = ctor.RegisterStruct<EndPanel>()) {
        h.RegisterMember("shown", &EndPanel::shown);
        h.RegisterMember("title", &EndPanel::title);
        h.RegisterMember("body", &EndPanel::body);
        h.RegisterMember("day", &EndPanel::day);
        h.RegisterMember("companies", &EndPanel::companies);
        h.RegisterMember("figures_title", &EndPanel::figures_title);
        h.RegisterMember("cash_label", &EndPanel::cash_label);
        h.RegisterMember("cash", &EndPanel::cash);
        h.RegisterMember("mines_label", &EndPanel::mines_label);
        h.RegisterMember("mines", &EndPanel::mines);
        h.RegisterMember("stages_label", &EndPanel::stages_label);
        h.RegisterMember("stages", &EndPanel::stages);
        h.RegisterMember("new_game", &EndPanel::new_game);
    }
    if (auto h = ctor.RegisterStruct<PauseRow>()) {
        h.RegisterMember("index", &PauseRow::index);
        h.RegisterMember("label", &PauseRow::label);
        h.RegisterMember("on", &PauseRow::on);
    }
    ctor.RegisterArray<std::vector<NeedRow>>();
    ctor.RegisterArray<std::vector<SaleRow>>();
    ctor.RegisterArray<std::vector<SoldRow>>();
    ctor.RegisterArray<std::vector<OwnedRow>>();
    ctor.RegisterArray<std::vector<ResourceRow>>();
    ctor.RegisterArray<std::vector<TechRow>>();
    ctor.RegisterArray<std::vector<LoanRow>>();
    ctor.RegisterArray<std::vector<LogRow>>();
    ctor.RegisterArray<std::vector<PauseRow>>();
    ctor.RegisterArray<std::vector<RaceRow>>();
    ctor.RegisterArray<std::vector<ProgramStageRow>>();
    ctor.RegisterArray<std::vector<PendingRow>>();

    // Header and strip
    ctor.Bind("company", &model_.company);
    ctor.Bind("day", &model_.day);
    ctor.Bind("cash", &model_.cash);
    ctor.Bind("spendable", &model_.spendable);
    ctor.Bind("available", &model_.available);
    ctor.Bind("committed", &model_.committed);
    ctor.Bind("has_committed", &model_.has_committed);
    ctor.Bind("debt", &model_.debt);
    ctor.Bind("speed_label", &model_.speed_label);
    ctor.Bind("speed", &model_.speed);
    ctor.Bind("tab", &model_.tab);
    ctor.Bind("game_over", &model_.game_over);
    ctor.Bind("deck", &model_.deck);
    ctor.Bind("status_line", &model_.status_line);
    ctor.Bind("status_danger", &model_.status_danger);
    ctor.Bind("show_pause_menu", &model_.show_pause_menu);
    ctor.Bind("pause_rows", &model_.pause_rows);
    // Orders waiting for the clock (spec 007)
    ctor.Bind("pending_orders", &model_.pending_orders);
    ctor.Bind("pending_count", &model_.pending_count);
    ctor.Bind("has_pending", &model_.has_pending);
    ctor.Bind("pending_line", &model_.pending_line);
    ctor.Bind("pending_shortfall", &model_.pending_shortfall);
    ctor.Bind("show_pending_list", &showPendingList_);
    ctor.Bind("owned_orders", &ownedOrders_);
    ctor.Bind("month_net", &model_.month_net);
    ctor.Bind("month_good", &model_.month_good);
    ctor.Bind("storage_month", &model_.storage_month);
    ctor.Bind("storage_month_long", &model_.storage_month_long);
    ctor.Bind("solvency", &model_.solvency);
    ctor.Bind("solvency_sub", &model_.solvency_sub);
    ctor.Bind("solvent", &model_.solvent);
    ctor.Bind("next_auction", &model_.next_auction);
    ctor.Bind("mines_operating", &model_.mines_operating);
    ctor.Bind("mines_building", &model_.mines_building);
    ctor.Bind("mines_mothballed", &model_.mines_mothballed);
    ctor.Bind("mines_depleted", &model_.mines_depleted);
    ctor.Bind("research_strip", &model_.research_strip);
    ctor.Bind("world_event", &model_.world_event);
    ctor.Bind("world_sub", &model_.world_sub);
    ctor.Bind("has_world", &model_.has_world);
    // The race for the program (spec 010)
    ctor.Bind("has_race", &model_.has_race);
    ctor.Bind("race_deadline", &model_.race_deadline);
    ctor.Bind("race_founded", &model_.race_founded);
    ctor.Bind("race_verdict", &model_.race_verdict);
    ctor.Bind("race_verdict_class", &model_.race_verdict_class);
    ctor.Bind("race_rows", &model_.race_rows);
    // The Program tab (spec 010)
    ctor.Bind("program_stages", &model_.program_stages);
    ctor.Bind("program", &model_.program);
    // Dashboard
    ctor.Bind("needs", &model_.needs);
    ctor.Bind("needs_none", &model_.needs_none);
    ctor.Bind("owned", &model_.owned);
    ctor.Bind("rival_line", &model_.rival_line);
    ctor.Bind("capacity_sub", &model_.capacity_sub);
    ctor.Bind("warehouse_value", &model_.warehouse_value);
    ctor.Bind("storage_sub", &model_.storage_sub);
    ctor.Bind("instalment_sub", &model_.instalment_sub);
    ctor.Bind("recent", &model_.recent);
    // Plots
    ctor.Bind("for_sale", &model_.for_sale);
    ctor.Bind("panel", &model_.panel);
    ctor.Bind("techs", &model_.techs);
    ctor.Bind("no_unlisted", &model_.no_unlisted);
    ctor.Bind("recent_sales", &model_.recent_sales);
    ctor.Bind("no_sales", &model_.no_sales);
    // Market
    ctor.Bind("resources", &model_.resources);
    ctor.Bind("chart_title", &model_.chart_title);
    ctor.Bind("chart_note", &model_.chart_note);
    ctor.Bind("storage_tonnes", &model_.storage_tonnes);
    ctor.Bind("storage_rate", &model_.storage_rate);
    ctor.Bind("storage_note", &model_.storage_note);
    ctor.Bind("standing_title", &model_.standing_title);
    ctor.Bind("policy_summary", &model_.policy_summary);
    ctor.Bind("policy_reserve_input", &model_.policy_reserve_input);
    ctor.Bind("policy_floor_input", &model_.policy_floor_input);
    ctor.Bind("policy_enabled", &model_.policy_enabled);
    ctor.Bind("ui_policy_hint", &model_.ui_policy_hint);
    ctor.Bind("policy_order", &model_.policy_order);
    ctor.Bind("market_orders", &model_.market_orders);
    // Research
    ctor.Bind("lab_line", &model_.lab_line);
    ctor.Bind("lab_sub", &model_.lab_sub);
    // Finance
    ctor.Bind("loans", &model_.loans);
    ctor.Bind("loan_rule", &model_.loan_rule);
    ctor.Bind("loan_limits", &model_.loan_limits);
    ctor.Bind("loan_credit", &model_.loan_credit);
    ctor.Bind("loan_blocked", &model_.loan_blocked);
    ctor.Bind("can_take_loan", &model_.can_take_loan);
    ctor.Bind("loan_order", &model_.loan_order);
    ctor.Bind("finance_orders", &model_.finance_orders);
    ctor.Bind("cover_costs", &model_.cover_costs);
    ctor.Bind("cover_order", &model_.cover_order);
    ctor.Bind("cover_check", &model_.cover_check);
    ctor.Bind("cover_explain", &model_.cover_explain);
    ctor.Bind("cover_last", &model_.cover_last);
    ctor.Bind("solvency_rule", &model_.solvency_rule);
    // Log and inputs
    ctor.Bind("log", &model_.log);
    ctor.Bind("log_filter", &model_.log_filter);
    ctor.Bind("log_empty", &model_.log_empty);
    ctor.Bind("bid_input", &model_.bid_input);
    ctor.Bind("sell_input", &model_.sell_input);
    ctor.Bind("loan_input", &model_.loan_input);
    ctor.Bind("repay_input", &model_.repay_input);
    ctor.Bind("selected_plot", &model_.selected_plot);
    ctor.Bind("selected_resource", &model_.selected_resource);
    ctor.Bind("ui_shortcuts", &model_.ui_shortcuts);
    ctor.Bind("end", &model_.end);
    ctor.Bind("ui_pause_menu_title", &model_.ui_pause_menu_title);
    ctor.Bind("ui_cancel", &model_.ui_cancel);
    ctor.Bind("ui_cancel_all", &model_.ui_cancel_all);
    ctor.Bind("ui_not_covered", &model_.ui_not_covered);
    ctor.Bind("ui_mine_loss", &model_.ui_mine_loss);
    ctor.Bind("ui_mine_if_reopened", &model_.ui_mine_if_reopened);
    ctor.Bind("ui_mine_earns", &model_.ui_mine_earns);
    ctor.Bind("ui_mine_loses", &model_.ui_mine_loses);
    ctor.Bind("ui_mine_mothballed", &model_.ui_mine_mothballed);
    ctor.Bind("ui_mine_if_reopened_key", &model_.ui_mine_if_reopened_key);

    const auto intArg = [](const Rml::VariantList& args, std::size_t i) {
        return i < args.size() ? args[i].Get<int>() : 0;
    };
    using Args = const Rml::VariantList&;
    ctor.BindEventCallback("set_tab", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onSetTab(intArg(a, 0));
    });
    ctor.BindEventCallback("set_speed", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onSetSpeed(intArg(a, 0));
    });
    ctor.BindEventCallback("step", [this](Rml::DataModelHandle, Rml::Event&, Args) { onStep(); });
    ctor.BindEventCallback("select_plot", [this, intArg](Rml::DataModelHandle, Rml::Event&,
                                                         Args a) { onSelectPlot(intArg(a, 0)); });
    ctor.BindEventCallback("select_resource",
                           [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
                               onSelectResource(intArg(a, 0));
                           });
    ctor.BindEventCallback("need_action",
                           [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
                               onNeedAction(intArg(a, 0), intArg(a, 1));
                           });
    ctor.BindEventCallback("toggle_pause_menu", [this](Rml::DataModelHandle, Rml::Event&, Args) {
        showPopover(!model_.show_pause_menu, false);
    });
    ctor.BindEventCallback("toggle_pending_list", [this](Rml::DataModelHandle, Rml::Event&, Args) {
        showPopover(false, !showPendingList_);
    });
    ctor.BindEventCallback("cancel", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onCancel(intArg(a, 0));
    });
    ctor.BindEventCallback("cancel_all",
                           [this](Rml::DataModelHandle, Rml::Event&, Args) { onCancelAll(); });
    ctor.BindEventCallback("toggle_pause", [this, intArg](Rml::DataModelHandle, Rml::Event&,
                                                          Args a) { onTogglePause(intArg(a, 0)); });
    ctor.BindEventCallback("survey", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onSurvey(intArg(a, 0));
    });
    ctor.BindEventCallback(
        "bid", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) { onBid(intArg(a, 0)); });
    ctor.BindEventCallback("build", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onBuild(intArg(a, 0));
    });
    ctor.BindEventCallback("toggle_mine", [this, intArg](Rml::DataModelHandle, Rml::Event&,
                                                         Args a) { onToggleMine(intArg(a, 0)); });
    ctor.BindEventCallback("sell", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onSell(intArg(a, 0), false);
    });
    ctor.BindEventCallback("sell_all", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onSell(intArg(a, 0), true);
    });
    ctor.BindEventCallback("take_loan",
                           [this](Rml::DataModelHandle, Rml::Event&, Args) { onTakeLoan(); });
    ctor.BindEventCallback("apply_policy", [this](Rml::DataModelHandle, Rml::Event&, Args) {
        onApplyPolicy(model_.policy_enabled);
    });
    ctor.BindEventCallback("toggle_policy", [this](Rml::DataModelHandle, Rml::Event&, Args) {
        onApplyPolicy(!model_.policy_enabled);
    });
    ctor.BindEventCallback("toggle_cover_costs", [this](Rml::DataModelHandle, Rml::Event&, Args) {
        onToggleCoverCosts();
    });
    ctor.BindEventCallback("repay", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onRepay(intArg(a, 0));
    });
    ctor.BindEventCallback("repay_all", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onRepayAll(intArg(a, 0));
    });
    ctor.BindEventCallback("set_log_filter",
                           [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
                               onSetLogFilter(intArg(a, 0));
                           });
    ctor.BindEventCallback("research", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onResearch(intArg(a, 0));
    });
    // The Program tab's three orders (spec 010).
    ctor.BindEventCallback("deliver", [this, intArg](Rml::DataModelHandle, Rml::Event&, Args a) {
        onDeliver(intArg(a, 0));
    });
    ctor.BindEventCallback("program_research", [this](Rml::DataModelHandle, Rml::Event&, Args) {
        onProgramResearch();
    });
    ctor.BindEventCallback("complete_stage",
                           [this](Rml::DataModelHandle, Rml::Event&, Args) { onCompleteStage(); });
    ctor.BindEventCallback("save", [this](Rml::DataModelHandle, Rml::Event&, Args) { onSave(); });
    ctor.BindEventCallback("load", [this](Rml::DataModelHandle, Rml::Event&, Args) { onLoad(); });
    ctor.BindEventCallback("new_game",
                           [this](Rml::DataModelHandle, Rml::Event&, Args) { onNewGame(); });
    handle_ = ctor.GetModelHandle();
    refresh();
}

bool App::loadDocument() {
    const std::u8string u8 = (assetsDir_ / "ui" / "main.rml").u8string();
    const std::string path(u8.begin(), u8.end());  // RmlUi takes UTF-8
    document_ = context_->LoadDocument(path);
    if (document_ == nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) RmlUi logging is printf-style
        Rml::Log::Message(Rml::Log::LT_ERROR, "Could not load %s", path.c_str());
        return false;
    }
    document_->Show();
    applyDeck(model_.deck);
    return true;
}

void App::refresh() {
    // Any order the sim rejected since the last refresh is the most useful
    // thing to tell the player right now.
    const std::vector<LogEntry>& log = session_->log();
    if (seenLogEntries_ > log.size()) {
        seenLogEntries_ = 0;  // the log was reset by a load or new game
    }
    for (std::size_t i = seenLogEntries_; i < log.size(); ++i) {
        if (log[i].notice.kind == sim::NoticeKind::CommandRejected) {
            traceUi("status", log[i].text);
            model_.status_line = log[i].text;
            model_.status_danger = true;
            statusAge_ = 0.0;
        }
    }
    seenLogEntries_ = log.size();
    if (session_->pendingOrders().empty()) {
        showPendingList_ = false;  // nothing left to list
    }
    syncLoanInput();
    refreshModel(*session_, model_);
    ownedOrders_ = std::ranges::any_of(model_.owned, [](const OwnedRow& row) {
        return row.mine_order.pending || row.build_order.pending;
    });
    if (handle_) {
        handle_.DirtyAllVariables();
    }
    PriceChartElement::invalidateAll();
    lastRefreshedDay_ = session_->day();
}

void App::setStatus(std::string text, bool danger) {
    traceUi("status", text);
    model_.status_line = std::move(text);
    model_.status_danger = danger;
    statusAge_ = 0.0;
    if (handle_) {
        handle_.DirtyVariable("status_line");
        handle_.DirtyVariable("status_danger");
    }
}

void App::applyDeck(bool on) {
    model_.deck = on;
    if (document_ != nullptr) {
        document_->SetClass("deck", on);
    }
    if (handle_) {
        handle_.DirtyVariable("deck");
    }
}

void App::forceDeck(bool on) {
    deckForced_ = on;
    applyDeck(on || static_cast<float>(context_->GetDimensions().x) < kDeckWidth);
}

void App::setTab(int tab) {
    onSetTab(tab);
}

void App::startGame(std::uint64_t seed) {
    newGame(seed);
    refresh();
}

void App::selectPlot(int index) {
    onSelectPlot(index);
}

void App::setLogFilter(int filter) {
    onSetLogFilter(filter);
}

void App::showPopover(bool pauseMenu, bool pendingList) {
    model_.show_pause_menu = pauseMenu;
    showPendingList_ = pendingList;
    if (handle_) {
        handle_.DirtyVariable("show_pause_menu");
        handle_.DirtyVariable("show_pending_list");
    }
}

void App::showPauseMenu(bool on) {
    showPopover(on, false);
}

void App::showPendingList(bool on) {
    showPopover(false, on);
}

void App::refreshNow() {
    refresh();
}

void App::update(double seconds) {
    const int ticked = session_->advance(seconds);
    if (session_->gameOver()) {
        // Recorded the moment the game ends, however it got there (the
        // clock, a single step, a finished save loaded), not when the
        // window closes.
        traceEnd();
    }
    statusAge_ += seconds;
    // Each tick starts the day's stops afresh, so after a tick a non-empty
    // list is a new stop. Task 013 replaces this status line with the stop
    // panel, which keeps the reason on screen until the clock restarts.
    if (ticked > 0 && !session_->stopsToday().empty()) {
        // The clock stopped for this: say so and keep it on screen longer.
        setStatus(stopStatus(*content_, *session_),
                  noticeIsDanger(session_->stopsToday().front().kind));
        statusAge_ = -kStatusSeconds;
    }
    if (statusAge_ > kStatusSeconds && !model_.status_line.empty()) {
        model_.status_line.clear();
        model_.status_danger = false;
        if (handle_) {
            handle_.DirtyVariable("status_line");
            handle_.DirtyVariable("status_danger");
        }
    }
    const bool deck = deckForced_ || static_cast<float>(context_->GetDimensions().x) < kDeckWidth;
    if (deck != model_.deck) {
        applyDeck(deck);
    }
    if (ticked > 0 || session_->day() != lastRefreshedDay_) {
        refresh();
    }
}

bool App::onKey(Rml::Input::KeyIdentifier key) {
    switch (key) {
        case Rml::Input::KI_SPACE:
            if (!session_->gameOver()) {
                session_->togglePause();
                refresh();
            }
            return true;
        case Rml::Input::KI_1:
            onSetSpeed(1);
            return true;
        case Rml::Input::KI_2:
            onSetSpeed(2);
            return true;
        case Rml::Input::KI_3:
            onSetSpeed(3);
            return true;
        case Rml::Input::KI_OEM_PERIOD:
            onStep();
            return true;
        case Rml::Input::KI_F9:
            if (trace_) {
                trace_->marker(session_->day(), "F9");
            }
            requestScreenshot();
            return true;
        case Rml::Input::KI_F12:
            requestScreenshot();
            return true;
        default:
            return false;
    }
}

void App::onSetTab(int tab) {
    if (tab >= 0 && tab < static_cast<int>(kTabNames.size())) {
        traceUi("tab", kTabNames.at(static_cast<std::size_t>(tab)));
    }
    model_.tab = tab;
    if (handle_) {
        handle_.DirtyVariable("tab");
    }
    showPopover(false, false);
}

// The clock controls are off once the game is over (spec 010, edge case 11).
// The session would ignore them too; the client does not ask.
void App::onSetSpeed(int speed) {
    if (session_->gameOver()) {
        return;
    }
    session_->setSpeed(static_cast<Speed>(std::clamp(speed, 0, 3)));
    refresh();
}

void App::onStep() {
    if (session_->gameOver()) {
        return;
    }
    session_->stepOneDay();
    if (session_->gameOver()) {
        traceEnd();
    }
    refresh();
    if (!session_->stopsToday().empty()) {
        // The step stopped the clock: say why, as update() does, over any
        // refusal refresh() just put on the status line.
        setStatus(stopStatus(*content_, *session_),
                  noticeIsDanger(session_->stopsToday().front().kind));
        statusAge_ = -kStatusSeconds;
    }
}

void App::onSelectPlot(int index) {
    traceUi("select_plot", "#" + std::to_string(index));
    model_.selected_plot = index;
    if (index >= 0 && index < static_cast<int>(session_->view().plots.size())) {
        // Exact text so the default bid is the ask to the cent, not a rounded label.
        model_.bid_input =
            exactMoney(session_->view().plots[static_cast<std::size_t>(index)].askPrice);
    }
    refresh();
}

void App::onSelectResource(int index) {
    if (index >= 0 && index < static_cast<int>(content_->resources.size())) {
        traceUi("select_resource", content_->resources[static_cast<std::size_t>(index)].id);
    }
    // Prefill the policy inputs only on an actual change of selection so a
    // draft is not lost by a click on the already selected row.
    const bool changed = index != model_.selected_resource;
    model_.selected_resource = index;
    if (changed) {
        syncPolicyInputs();
    }
    refresh();
}

void App::onNeedAction(int action, int target) {
    switch (action) {
        case 1:  // open the plot
            onSetTab(1);
            onSelectPlot(target);
            return;
        case 2:  // mothball the idle mine
            onToggleMine(target);
            return;
        case 3:  // sell: the Market tab
            onSetTab(2);
            return;
        case 4:  // the Finance tab
            onSetTab(4);
            return;
        case 5:  // the Research tab
            onSetTab(3);
            return;
        default:
            return;
    }
}

void App::onTogglePause(int index) {
    if (index < 0 || index >= static_cast<int>(kPauseOnCount)) {
        return;
    }
    const auto reason = static_cast<PauseOn>(index);
    pauseRules_.set(reason, !pauseRules_.isEnabled(reason));
    session_->setAutoPause(pauseRules_);
    savePauseRules();
    traceUi("pause_on",
            std::string(pauseOnKey(reason)) + (pauseRules_.isEnabled(reason) ? "=1" : "=0"));
    refresh();
}

void App::syncPolicyInputs() {
    const sim::CompanyView& view = session_->view();
    const int index = model_.selected_resource;
    if (index < 0 || index >= static_cast<int>(view.sellPolicies.size())) {
        return;
    }
    const sim::SellPolicy& policy = view.sellPolicies[static_cast<std::size_t>(index)];
    model_.policy_reserve_input = std::to_string(policy.reserveTonnes);
    model_.policy_floor_input = exactMoney(policy.floorPrice);
}

void App::onApplyPolicy(bool enabled) {
    const int index = model_.selected_resource;
    if (index < 0 || index >= static_cast<int>(content_->resources.size())) {
        return;
    }
    std::int64_t reserve = 0;
    sim::Money floor;
    if (!game::parseTonnes(model_.policy_reserve_input, reserve)) {
        setStatus(text(*content_, "status.reserve_prompt"), true);
        return;
    }
    if (!game::parseMoney(model_.policy_floor_input, floor)) {
        setStatus(text(*content_, "status.floor_prompt"), true);
        return;
    }
    sim::SetSellPolicy command;
    command.company = Session::kPlayer;
    command.resource = sim::ResourceId(static_cast<std::uint32_t>(index));
    command.enabled = enabled ? std::uint8_t{1} : std::uint8_t{0};
    command.reserveTonnes = reserve;
    command.floorPrice = floor;
    if (!order(command)) {
        return;
    }
    const std::string& name = content_->resources[static_cast<std::size_t>(index)].name;
    setStatus(text(*content_, enabled ? "status.policy_starts" : "status.policy_stops",
                   {{"resource", name}}));
}

void App::onToggleCoverCosts() {
    // A second click while the first is still waiting takes that order back
    // rather than queueing its opposite behind it.
    if (model_.cover_order.pending) {
        onCancel(model_.cover_order.id);
        return;
    }
    const bool enable = !session_->view().coverCosts;
    if (!order(sim::SetCoverCosts{Session::kPlayer, enable ? std::uint8_t{1} : std::uint8_t{0}})) {
        return;
    }
    setStatus(text(*content_, enable ? "status.cover_on" : "status.cover_off"));
}

bool App::order(const sim::Command& command) {
    // Every order the client gives goes through here, and none is taken once
    // the game is over (spec 010): the session refuses it and says so.
    if (!session_->submit(command)) {
        return false;
    }
    // Shown at once: with the clock stopped nothing else would redraw the
    // control until the next tick (spec 007).
    refresh();
    return true;
}

void App::onSurvey(int plotIndex) {
    if (!order(sim::CommissionSurvey{Session::kPlayer,
                                     sim::PlotId(static_cast<std::uint32_t>(plotIndex))})) {
        return;
    }
    setStatus(text(*content_, "status.survey_queued"));
}

void App::onBid(int plotIndex) {
    sim::Money amount;
    if (!game::parseMoney(model_.bid_input, amount)) {
        setStatus(text(*content_, "status.bid_prompt"), true);
        return;
    }
    if (!order(sim::BidOnPlot{Session::kPlayer, sim::PlotId(static_cast<std::uint32_t>(plotIndex)),
                              amount})) {
        return;
    }
    setStatus(text(*content_, "status.bid_queued", {{"amount", formatMoney(amount)}}));
}

void App::onBuild(int plotIndex) {
    if (!order(
            sim::BuildMine{Session::kPlayer, sim::PlotId(static_cast<std::uint32_t>(plotIndex))})) {
        return;
    }
    setStatus(text(*content_, "status.build_queued"));
}

void App::onToggleMine(int mineIndex) {
    const sim::MineId id(static_cast<std::uint32_t>(mineIndex));
    for (const sim::MineView& mine : session_->view().mines) {
        if (mine.id == id) {
            const bool operate = mine.state != sim::MineState::Operating;
            if (!order(sim::SetMineState{Session::kPlayer, id,
                                         operate ? std::uint8_t{1} : std::uint8_t{0}})) {
                return;
            }
            setStatus(text(*content_, operate ? "status.mine_reopen" : "status.mine_mothball"));
            return;
        }
    }
}

void App::onSell(int resourceIndex, bool all) {
    if (resourceIndex < 0 ||
        resourceIndex >= static_cast<int>(session_->view().inventoryTonnes.size())) {
        return;
    }
    std::int64_t tonnes = session_->view().inventoryTonnes[static_cast<std::size_t>(resourceIndex)];
    if (!all && !game::parseTonnes(model_.sell_input, tonnes)) {
        setStatus(text(*content_, "status.sell_prompt"), true);
        return;
    }
    // The sim decides whether the quantity is valid; its reason shows in the log.
    if (!order(sim::SellOre{Session::kPlayer,
                            sim::ResourceId(static_cast<std::uint32_t>(resourceIndex)), tonnes})) {
        return;
    }
    setStatus(text(*content_, "status.sale_queued", {{"tonnes", formatTonnes(tonnes)}}));
}

void App::syncLoanInput() {
    // Anything in the field other than what was last put there is the
    // player's, and stays the player's even if credit later equals it.
    loanEdited_ = loanEdited_ || model_.loan_input != loanPrefill_;
    if (loanEdited_) {
        return;
    }
    loanPrefill_ = exactMoney(std::max(session_->view().loanCapacity, sim::Money::zero()));
    model_.loan_input = loanPrefill_;
}

void App::resetLoanInput() {
    loanEdited_ = false;
    loanPrefill_.clear();
    model_.loan_input.clear();
}

void App::onSetLogFilter(int filter) {
    static constexpr std::array<const char*, 4> kFilters = {"all", "mine", "market", "rivals"};
    if (filter < 0 || filter >= static_cast<int>(kFilters.size())) {
        return;
    }
    traceUi("log_filter", kFilters.at(static_cast<std::size_t>(filter)));
    model_.log_filter = filter;
    refresh();
}

void App::onRepayAll(int loanIndex) {
    const sim::LoanId id(static_cast<std::uint32_t>(loanIndex));
    for (const sim::LoanView& loan : session_->view().loans) {
        if (loan.id == id && loan.active) {
            // No money is counted here: the button's state comes from the
            // model, and the sim alone turns a repayment away (R-001).
            // The balance to the cent, read now rather than from the label.
            if (!order(sim::RepayLoan{Session::kPlayer, id, loan.remaining})) {
                return;
            }
            setStatus(text(*content_, "status.repayment_ordered",
                           {{"amount", formatMoney(loan.remaining)}}));
            return;
        }
    }
}

void App::onTakeLoan() {
    if (!model_.can_take_loan) {
        // The button is disabled; a stale click still gets the reason.
        setStatus(model_.loan_blocked, true);
        return;
    }
    sim::Money amount;
    if (!game::parseMoney(model_.loan_input, amount)) {
        setStatus(text(*content_, "status.loan_prompt"), true);
        return;
    }
    if (!order(sim::TakeLoan{Session::kPlayer, amount})) {
        return;
    }
    resetLoanInput();  // the field follows the credit that is left
    setStatus(text(*content_, "status.loan_requested", {{"amount", formatMoney(amount)}}));
}

void App::onRepay(int loanIndex) {
    sim::Money amount;
    if (!game::parseMoney(model_.repay_input, amount)) {
        setStatus(text(*content_, "status.repay_prompt"), true);
        return;
    }
    if (!order(sim::RepayLoan{Session::kPlayer, sim::LoanId(static_cast<std::uint32_t>(loanIndex)),
                              amount})) {
        return;
    }
    setStatus(text(*content_, "status.repayment_ordered", {{"amount", formatMoney(amount)}}));
}

void App::onResearch(int techIndex) {
    if (!order(sim::StartResearch{Session::kPlayer,
                                  sim::TechId(static_cast<std::uint32_t>(techIndex))})) {
        return;
    }
    setStatus(text(*content_, "status.research_queued"));
}

// Delivers the most the current stage can take of one good today: the
// lesser of the stock and what is still needed, as the view has them and as
// the button said (spec 010). The sim decides whether it is accepted.
void App::onDeliver(int resourceIndex) {
    const std::optional<sim::ProgramStageView>& stage = session_->view().program.stage;
    if (!stage) {
        return;
    }
    for (const sim::ProgramRequirementView& need : stage->requirements) {
        if (static_cast<int>(need.resource.value()) != resourceIndex) {
            continue;
        }
        const std::int64_t tonnes =
            std::min(need.stockTonnes, need.neededTonnes - need.deliveredTonnes);
        if (tonnes > 0 && order(sim::DeliverToProgram{Session::kPlayer, need.resource, tonnes})) {
            setStatus(text(*content_, "status.delivery_queued",
                           {{"tonnes", formatTonnes(tonnes)},
                            {"resource", content_->resources[need.resource.index()].name}}));
        }
        return;
    }
}

// The current stage's project, through the same order as the Research tab.
void App::onProgramResearch() {
    if (const std::optional<sim::ProgramStageView>& stage = session_->view().program.stage) {
        onResearch(static_cast<int>(stage->research.value()));
    }
}

// The status line names the cost the view quotes for the current stage; the
// sim decides whether the order is accepted.
void App::onCompleteStage() {
    const std::optional<sim::ProgramStageView>& stage = session_->view().program.stage;
    if (!stage) {
        return;
    }
    const sim::Money cost = stage->cost;
    if (order(sim::CompleteProgramStage{Session::kPlayer})) {
        setStatus(
            text(*content_, "status.stage_completion_queued", {{"amount", formatMoney(cost)}}));
    }
}

void App::onCancel(int orderId) {
    // Nothing is logged: the sim never saw the order. An id that is no
    // longer waiting (the clock moved first) changes nothing.
    if (orderId > 0 && session_->cancel(static_cast<std::uint64_t>(orderId))) {
        refresh();
        setStatus(text(*content_, "status.order_cancelled"));
        return;
    }
    refresh();
}

void App::onCancelAll() {
    if (session_->pendingOrders().empty()) {
        return;
    }
    session_->cancelAll();
    refresh();
    setStatus(text(*content_, "status.order_cancelled"));
}

void App::onSave() {
    const std::filesystem::path path = services_->saveDirectory() / kSaveFile;
    const std::filesystem::path temp = path.string() + ".tmp";
    const std::vector<std::uint8_t> bytes = session_->save();
    if (!writeFile(temp, bytes)) {
        setStatus(text(*content_, "status.save_write_failed", {{"name", temp.string()}}), true);
        return;
    }
    // Replace the previous save only once the new one is fully on disk.
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        setStatus(text(*content_, "status.save_replace_failed",
                       {{"name", path.string()}, {"reason", error.message()}}),
                  true);
        return;
    }
    setStatus(text(*content_, "status.saved", {{"name", path.string()}}));
    if (trace_) {
        trace_->saved(session_->day(), keepSaveCopy("save", bytes));
    }
}

void App::onLoad() {
    const std::filesystem::path path = services_->saveDirectory() / kSaveFile;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setStatus(text(*content_, "status.no_save", {{"name", path.string()}}), true);
        return;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    if (!session_->load(bytes)) {
        setStatus(text(*content_, "status.save_invalid"), true);
        return;
    }
    model_.selected_plot = -1;
    syncPolicyInputs();
    resetLoanInput();
    if (trace_) {
        const std::string copy = keepSaveCopy("load", bytes);
        trace_->loaded(session_->day(), copy, session_->stateHash());
        if (copy.empty()) {
            setStatus(text(*content_, "status.loaded_copy_failed"), true);
            refresh();
            return;
        }
    }
    refresh();
    setStatus(text(*content_, "status.loaded"));
}

void App::onNewGame() {
    newGame(clockSeed());
    refresh();
    setStatus(text(*content_, "status.new_game"));
}

}  // namespace game
