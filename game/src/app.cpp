#include "app.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/types.h"
#include "sim/view.h"

#include "chart_element.h"
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
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace game {

namespace {

constexpr double kStatusSeconds = 6.0;
constexpr int kMaxScreenshots = 200;  // about 3.4 MB each, uncompressed
const char* const kSaveFile = "autosave.dcsave";
constexpr std::array<const char*, 6> kTabNames = {"dashboard", "plots",   "market",
                                                  "research",  "finance", "log"};

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
    newGame(clockSeed());
    bindModel();
    PriceChartElement::setProvider([this](int resource) {
        ChartSeries series;
        if (resource < 0 || resource >= static_cast<int>(content_->resources.size())) {
            return series;
        }
        const sim::ResourceId id(static_cast<std::uint32_t>(resource));
        const std::vector<sim::Money>& history = session_->priceHistory(id);
        const std::size_t count = std::min<std::size_t>(history.size(), sim::kRecentPriceDays);
        for (std::size_t i = history.size() - count; i < history.size(); ++i) {
            series.values.push_back(history[i].cents());
        }
        series.baseline = content_->resources[id.index()].basePrice.cents();
        return series;
    });
}

App::~App() {
    if (trace_) {
        trace_->end(session_->day());
    }
    PriceChartElement::setProvider(nullptr);
    if (document_ != nullptr) {
        document_->Close();
    }
    context_->RemoveDataModel("game");
}

void App::newGame(std::uint64_t seed) {
    sim::GameSetup setup;
    setup.playerName = std::string(services_->playerName());
    setup.aiNames = {"Northfield Minerals"};
    session_ = std::make_unique<Session>(*content_, seed, setup);
    session_->setTrace(trace_.get());
    if (trace_) {
        trace_->newGame(seed, setup, sim::hashContent(*content_), buildId_);
    }
    model_.selected_plot = -1;
    model_.selected_resource = 0;
    model_.tab = 0;
    syncPolicyInputs();
    lastRefreshedDay_ = session_->day();
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
        setStatus("Screenshot failed.");
        return;
    }
    if (trace_) {
        trace_->screenshot(session_->day(), file.filename().string());
    }
    setStatus("Screenshot saved: " + file.filename().string());
}

void App::bindModel() {
    Rml::DataModelConstructor ctor = context_->CreateDataModel("game");
    if (!ctor) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) RmlUi logging is printf-style
        Rml::Log::Message(Rml::Log::LT_ERROR, "Could not create the game data model");
        return;
    }
    if (auto h = ctor.RegisterStruct<PlotRow>()) {
        h.RegisterMember("index", &PlotRow::index);
        h.RegisterMember("name", &PlotRow::name);
        h.RegisterMember("region", &PlotRow::region);
        h.RegisterMember("area", &PlotRow::area);
        h.RegisterMember("status", &PlotRow::status);
        h.RegisterMember("ask", &PlotRow::ask);
        h.RegisterMember("auction", &PlotRow::auction);
        h.RegisterMember("survey", &PlotRow::survey);
        h.RegisterMember("mine", &PlotRow::mine);
        h.RegisterMember("capex", &PlotRow::capex);
        h.RegisterMember("odds", &PlotRow::odds);
        h.RegisterMember("can_afford_mine", &PlotRow::can_afford_mine);
        h.RegisterMember("listed", &PlotRow::listed);
        h.RegisterMember("mine_here", &PlotRow::mine_here);
        h.RegisterMember("owned", &PlotRow::owned);
        h.RegisterMember("can_survey", &PlotRow::can_survey);
        h.RegisterMember("can_build", &PlotRow::can_build);
        h.RegisterMember("can_toggle_mine", &PlotRow::can_toggle_mine);
        h.RegisterMember("mine_operating", &PlotRow::mine_operating);
        h.RegisterMember("mine_index", &PlotRow::mine_index);
    }
    if (auto h = ctor.RegisterStruct<ResourceRow>()) {
        h.RegisterMember("index", &ResourceRow::index);
        h.RegisterMember("name", &ResourceRow::name);
        h.RegisterMember("price", &ResourceRow::price);
        h.RegisterMember("base", &ResourceRow::base);
        h.RegisterMember("ratio", &ResourceRow::ratio);
        h.RegisterMember("supply", &ResourceRow::supply);
        h.RegisterMember("stock", &ResourceRow::stock);
        h.RegisterMember("stock_value", &ResourceRow::stock_value);
        h.RegisterMember("policy", &ResourceRow::policy);
        h.RegisterMember("auto_sold", &ResourceRow::auto_sold);
        h.RegisterMember("has_stock", &ResourceRow::has_stock);
        h.RegisterMember("selected", &ResourceRow::selected);
    }
    if (auto h = ctor.RegisterStruct<TechRow>()) {
        h.RegisterMember("index", &TechRow::index);
        h.RegisterMember("name", &TechRow::name);
        h.RegisterMember("cost", &TechRow::cost);
        h.RegisterMember("duration", &TechRow::duration);
        h.RegisterMember("effects", &TechRow::effects);
        h.RegisterMember("status", &TechRow::status);
        h.RegisterMember("can_start", &TechRow::can_start);
    }
    if (auto h = ctor.RegisterStruct<LoanRow>()) {
        h.RegisterMember("index", &LoanRow::index);
        h.RegisterMember("remaining", &LoanRow::remaining);
        h.RegisterMember("instalment", &LoanRow::instalment);
        h.RegisterMember("months", &LoanRow::months);
        h.RegisterMember("active", &LoanRow::active);
    }
    if (auto h = ctor.RegisterStruct<LogRow>()) {
        h.RegisterMember("day", &LogRow::day);
        h.RegisterMember("text", &LogRow::text);
        h.RegisterMember("warning", &LogRow::warning);
    }
    ctor.RegisterArray<std::vector<PlotRow>>();
    ctor.RegisterArray<std::vector<ResourceRow>>();
    ctor.RegisterArray<std::vector<TechRow>>();
    ctor.RegisterArray<std::vector<LoanRow>>();
    ctor.RegisterArray<std::vector<LogRow>>();
    ctor.RegisterArray<std::vector<std::string>>();

    ctor.Bind("company", &model_.company);
    ctor.Bind("day", &model_.day);
    ctor.Bind("cash", &model_.cash);
    ctor.Bind("spendable", &model_.spendable);
    ctor.Bind("debt", &model_.debt);
    ctor.Bind("insolvency", &model_.insolvency);
    ctor.Bind("show_intro", &model_.show_intro);
    ctor.Bind("survey_cost", &model_.survey_cost);
    ctor.Bind("survey_days", &model_.survey_days);
    ctor.Bind("mine_build_days", &model_.mine_build_days);
    ctor.Bind("grace_months", &model_.grace_months);
    ctor.Bind("loan_terms", &model_.loan_terms);
    ctor.Bind("speed_label", &model_.speed_label);
    ctor.Bind("speed", &model_.speed);
    ctor.Bind("tab", &model_.tab);
    ctor.Bind("game_over", &model_.game_over);
    ctor.Bind("status_line", &model_.status_line);
    ctor.Bind("loan_capacity", &model_.loan_capacity);
    ctor.Bind("storage_cost", &model_.storage_cost);
    ctor.Bind("mine_opex", &model_.mine_opex);
    ctor.Bind("survey_noise", &model_.survey_noise);
    ctor.Bind("mines_summary", &model_.mines_summary);
    ctor.Bind("plots_summary", &model_.plots_summary);
    ctor.Bind("research_summary", &model_.research_summary);
    ctor.Bind("rivals", &model_.rivals);
    ctor.Bind("events", &model_.events);
    ctor.Bind("plots", &model_.plots);
    ctor.Bind("resources", &model_.resources);
    ctor.Bind("techs", &model_.techs);
    ctor.Bind("loans", &model_.loans);
    ctor.Bind("log", &model_.log);
    ctor.Bind("storage_warning", &model_.storage_warning);
    ctor.Bind("policy_reserve_input", &model_.policy_reserve_input);
    ctor.Bind("policy_floor_input", &model_.policy_floor_input);
    ctor.Bind("policy_enabled", &model_.policy_enabled);
    ctor.Bind("policy_summary", &model_.policy_summary);
    ctor.Bind("cover_costs", &model_.cover_costs);
    ctor.Bind("cover_costs_label", &model_.cover_costs_label);
    ctor.Bind("bid_input", &model_.bid_input);
    ctor.Bind("sell_input", &model_.sell_input);
    ctor.Bind("loan_input", &model_.loan_input);
    ctor.Bind("repay_input", &model_.repay_input);
    ctor.Bind("selected_plot", &model_.selected_plot);
    ctor.Bind("selected_resource", &model_.selected_resource);
    ctor.Bind("selected_plot_title", &model_.selected_plot_title);
    ctor.Bind("selected_resource_title", &model_.selected_resource_title);

    const auto intArg = [](const Rml::VariantList& args, std::size_t i) {
        return i < args.size() ? args[i].Get<int>() : 0;
    };
    ctor.BindEventCallback(
        "set_tab", [this, intArg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
            onSetTab(intArg(a, 0));
        });
    ctor.BindEventCallback(
        "set_speed", [this, intArg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
            onSetSpeed(intArg(a, 0));
        });
    ctor.BindEventCallback(
        "step", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { onStep(); });
    ctor.BindEventCallback(
        "select_plot", [this, intArg](Rml::DataModelHandle, Rml::Event&,
                                      const Rml::VariantList& a) { onSelectPlot(intArg(a, 0)); });
    ctor.BindEventCallback("select_resource", [this, intArg](Rml::DataModelHandle, Rml::Event&,
                                                             const Rml::VariantList& a) {
        onSelectResource(intArg(a, 0));
    });
    ctor.BindEventCallback(
        "survey", [this, intArg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
            onSurvey(intArg(a, 0));
        });
    ctor.BindEventCallback(
        "bid", [this, intArg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
            onBid(intArg(a, 0));
        });
    ctor.BindEventCallback(
        "build", [this, intArg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
            onBuild(intArg(a, 0));
        });
    ctor.BindEventCallback(
        "toggle_mine", [this, intArg](Rml::DataModelHandle, Rml::Event&,
                                      const Rml::VariantList& a) { onToggleMine(intArg(a, 0)); });
    ctor.BindEventCallback(
        "sell", [this, intArg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
            onSell(intArg(a, 0), false);
        });
    ctor.BindEventCallback(
        "sell_all", [this, intArg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
            onSell(intArg(a, 0), true);
        });
    ctor.BindEventCallback("take_loan", [this](Rml::DataModelHandle, Rml::Event&,
                                               const Rml::VariantList&) { onTakeLoan(); });
    ctor.BindEventCallback("apply_policy",
                           [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                               onApplyPolicy(model_.policy_enabled);
                           });
    ctor.BindEventCallback("toggle_policy",
                           [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                               onApplyPolicy(!model_.policy_enabled);
                           });
    ctor.BindEventCallback("toggle_cover_costs",
                           [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                               onToggleCoverCosts();
                           });
    ctor.BindEventCallback(
        "repay", [this, intArg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
            onRepay(intArg(a, 0));
        });
    ctor.BindEventCallback(
        "research", [this, intArg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
            onResearch(intArg(a, 0));
        });
    ctor.BindEventCallback(
        "save", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { onSave(); });
    ctor.BindEventCallback(
        "load", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { onLoad(); });
    ctor.BindEventCallback("hide_intro",
                           [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
                               model_.show_intro = false;
                               handle_.DirtyVariable("show_intro");
                           });
    ctor.BindEventCallback("new_game", [this](Rml::DataModelHandle, Rml::Event&,
                                              const Rml::VariantList&) { onNewGame(); });
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
            statusAge_ = 0.0;
        }
    }
    seenLogEntries_ = log.size();
    refreshModel(*session_, model_);
    if (handle_) {
        handle_.DirtyAllVariables();
    }
    PriceChartElement::invalidateAll();
    lastRefreshedDay_ = session_->day();
}

void App::setStatus(std::string text) {
    traceUi("status", text);
    model_.status_line = std::move(text);
    statusAge_ = 0.0;
    if (handle_) {
        handle_.DirtyVariable("status_line");
    }
}

void App::update(double seconds) {
    const int ticked = session_->advance(seconds);
    statusAge_ += seconds;
    if (statusAge_ > kStatusSeconds && !model_.status_line.empty()) {
        model_.status_line.clear();
        if (handle_) {
            handle_.DirtyVariable("status_line");
        }
    }
    if (ticked > 0 || session_->day() != lastRefreshedDay_) {
        refresh();
    }
}

bool App::onKey(Rml::Input::KeyIdentifier key) {
    switch (key) {
        case Rml::Input::KI_SPACE:
            session_->togglePause();
            refresh();
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
    handle_.DirtyVariable("tab");
}

void App::onSetSpeed(int speed) {
    session_->setSpeed(static_cast<Speed>(std::clamp(speed, 0, 3)));
    refresh();
}

void App::onStep() {
    session_->stepOneDay();
    refresh();
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
        setStatus("Set the reserve in tonnes to keep on the pile, such as 0 or 2,500.");
        return;
    }
    if (!game::parseMoney(model_.policy_floor_input, floor)) {
        setStatus("Set the floor price per tonne to accept, such as 0 or 850.");
        return;
    }
    sim::SetSellPolicy command;
    command.company = Session::kPlayer;
    command.resource = sim::ResourceId(static_cast<std::uint32_t>(index));
    command.enabled = enabled ? std::uint8_t{1} : std::uint8_t{0};
    command.reserveTonnes = reserve;
    command.floorPrice = floor;
    session_->submit(command);
    const std::string& name = content_->resources[static_cast<std::size_t>(index)].name;
    setStatus(enabled ? "Auto-sell for " + name + " starts tomorrow."
                      : "Auto-sell for " + name + " shuts off tomorrow.");
}

void App::onToggleCoverCosts() {
    const bool enable = !session_->view().coverCosts;
    session_->submit(
        sim::SetCoverCosts{Session::kPlayer, enable ? std::uint8_t{1} : std::uint8_t{0}});
    setStatus(enable ? "Cover costs at month end turns on tomorrow."
                     : "Cover costs at month end turns off tomorrow.");
}

void App::onSurvey(int plotIndex) {
    session_->submit(sim::CommissionSurvey{Session::kPlayer,
                                           sim::PlotId(static_cast<std::uint32_t>(plotIndex))});
    setStatus("Survey queued; it starts tomorrow and results arrive in the log.");
}

void App::onBid(int plotIndex) {
    sim::Money amount;
    if (!game::parseMoney(model_.bid_input, amount)) {
        setStatus("Enter a bid amount such as 1.2M, 850k or 1,200,000.");
        return;
    }
    session_->submit(sim::BidOnPlot{Session::kPlayer,
                                    sim::PlotId(static_cast<std::uint32_t>(plotIndex)), amount});
    setStatus(
        "Bid of " + formatMoney(amount) +
        " queued for tomorrow; sealed until the auction closes. Rejections appear in the log.");
}

void App::onBuild(int plotIndex) {
    session_->submit(
        sim::BuildMine{Session::kPlayer, sim::PlotId(static_cast<std::uint32_t>(plotIndex))});
    setStatus("Mine queued; construction starts tomorrow if the order is accepted.");
}

void App::onToggleMine(int mineIndex) {
    const sim::MineId id(static_cast<std::uint32_t>(mineIndex));
    for (const sim::MineView& mine : session_->view().mines) {
        if (mine.id == id) {
            const bool operate = mine.state != sim::MineState::Operating;
            session_->submit(sim::SetMineState{Session::kPlayer, id,
                                               operate ? std::uint8_t{1} : std::uint8_t{0}});
            setStatus(operate ? "Reopening the mine." : "Mothballing the mine.");
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
        setStatus("Enter a quantity in tonnes, such as 1,500.");
        return;
    }
    // The sim decides whether the quantity is valid; its reason shows in the log.
    session_->submit(sim::SellOre{
        Session::kPlayer, sim::ResourceId(static_cast<std::uint32_t>(resourceIndex)), tonnes});
    setStatus("Sale of " + formatTonnes(tonnes) + " queued at today's price.");
}

void App::onTakeLoan() {
    sim::Money amount;
    if (!game::parseMoney(model_.loan_input, amount)) {
        setStatus("Enter a loan amount such as 2M.");
        return;
    }
    session_->submit(sim::TakeLoan{Session::kPlayer, amount});
    setStatus("Loan of " + formatMoney(amount) + " requested.");
}

void App::onRepay(int loanIndex) {
    sim::Money amount;
    if (!game::parseMoney(model_.repay_input, amount)) {
        setStatus("Enter an amount to repay.");
        return;
    }
    session_->submit(sim::RepayLoan{Session::kPlayer,
                                    sim::LoanId(static_cast<std::uint32_t>(loanIndex)), amount});
    setStatus("Repayment of " + formatMoney(amount) + " ordered.");
}

void App::onResearch(int techIndex) {
    session_->submit(
        sim::StartResearch{Session::kPlayer, sim::TechId(static_cast<std::uint32_t>(techIndex))});
    setStatus("Research queued; it starts tomorrow if the order is accepted.");
}

void App::onSave() {
    const std::filesystem::path path = services_->saveDirectory() / kSaveFile;
    const std::filesystem::path temp = path.string() + ".tmp";
    const std::vector<std::uint8_t> bytes = session_->save();
    if (!writeFile(temp, bytes)) {
        setStatus("Could not write " + temp.string());
        return;
    }
    // Replace the previous save only once the new one is fully on disk.
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        setStatus("Could not replace " + path.string() + ": " + error.message());
        return;
    }
    setStatus("Saved to " + path.string());
    if (trace_) {
        trace_->saved(session_->day(), keepSaveCopy("save", bytes));
    }
}

void App::onLoad() {
    const std::filesystem::path path = services_->saveDirectory() / kSaveFile;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setStatus("No save found at " + path.string());
        return;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    if (!session_->load(bytes)) {
        setStatus("That save is not valid for this version of the game data.");
        return;
    }
    model_.selected_plot = -1;
    syncPolicyInputs();
    if (trace_) {
        const std::string copy = keepSaveCopy("load", bytes);
        trace_->loaded(session_->day(), copy, session_->stateHash());
        if (copy.empty()) {
            setStatus("Game loaded, but the playtest copy of the save could not be written.");
            refresh();
            return;
        }
    }
    refresh();
    setStatus("Game loaded.");
}

void App::onNewGame() {
    newGame(clockSeed());
    refresh();
    setStatus("New game started.");
}

}  // namespace game
