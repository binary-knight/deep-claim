#include "app.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/view.h"

#include "chart_element.h"
#include "game/format.h"
#include "game/session.h"
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
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace game {

namespace {

constexpr double kStatusSeconds = 6.0;
const char* const kSaveFile = "autosave.dcsave";

std::uint64_t clockSeed() {
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

}  // namespace

App::App(Rml::Context& context, platform::Services& services, const sim::Content& content,
         std::filesystem::path assetsDir)
    : context_(&context),
      services_(&services),
      content_(&content),
      assetsDir_(std::move(assetsDir)) {
    newGame(clockSeed());
    bindModel();
    PriceChartElement::setProvider([this](int resource) {
        ChartSeries series;
        if (resource < 0 || resource >= static_cast<int>(content_->resources.size())) {
            return series;
        }
        const sim::ResourceId id(static_cast<std::uint32_t>(resource));
        const std::vector<sim::Money>& history = session_->simulation().priceHistory(id);
        const std::size_t count = std::min<std::size_t>(history.size(), sim::kRecentPriceDays);
        for (std::size_t i = history.size() - count; i < history.size(); ++i) {
            series.values.push_back(history[i].cents());
        }
        series.baseline = content_->resources[id.index()].basePrice.cents();
        return series;
    });
}

App::~App() {
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
    model_.selected_plot = -1;
    model_.selected_resource = 0;
    model_.tab = 0;
    lastRefreshedDay_ = session_->day();
}

void App::bindModel() {
    Rml::DataModelConstructor ctor = context_->CreateDataModel("game");
    if (!ctor) {
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
    ctor.Bind("debt", &model_.debt);
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
    ctor.BindEventCallback("new_game", [this](Rml::DataModelHandle, Rml::Event&,
                                              const Rml::VariantList&) { onNewGame(); });
    handle_ = ctor.GetModelHandle();
    refresh();
}

bool App::loadDocument() {
    const std::string path = (assetsDir_ / "ui" / "main.rml").string();
    document_ = context_->LoadDocument(path);
    if (document_ == nullptr) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Could not load %s", path.c_str());
        return false;
    }
    document_->Show();
    return true;
}

void App::refresh() {
    refreshModel(*session_, model_);
    if (handle_) {
        handle_.DirtyAllVariables();
    }
    PriceChartElement::invalidateAll();
    lastRefreshedDay_ = session_->day();
}

void App::setStatus(std::string text) {
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
        default:
            return false;
    }
}

bool App::parseMoney(const std::string& text, sim::Money& out) const {
    // Accepts "1200000", "1,200,000", "1.2M", "450k", "$3.5m".
    std::string digits;
    double multiplier = 1.0;
    for (const char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.') {
            digits += c;
        } else if (c == 'k' || c == 'K') {
            multiplier = 1'000.0;
        } else if (c == 'm' || c == 'M') {
            multiplier = 1'000'000.0;
        } else if (c == 'b' || c == 'B') {
            multiplier = 1'000'000'000.0;
        }
    }
    if (digits.empty()) {
        return false;
    }
    try {
        const double units = std::stod(digits) * multiplier;
        if (units < 0.0 || units > 1.0e15) {
            return false;
        }
        out = sim::Money::fromCents(static_cast<std::int64_t>(units * 100.0 + 0.5));
        return true;
    } catch (...) {
        return false;
    }
}

void App::onSetTab(int tab) {
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
    model_.selected_plot = index;
    if (index >= 0 && index < static_cast<int>(session_->view().plots.size())) {
        model_.bid_input =
            formatMoney(session_->view().plots[static_cast<std::size_t>(index)].askPrice);
    }
    refresh();
}

void App::onSelectResource(int index) {
    model_.selected_resource = index;
    refresh();
}

void App::onSurvey(int plotIndex) {
    session_->submit(sim::CommissionSurvey{Session::kPlayer,
                                           sim::PlotId(static_cast<std::uint32_t>(plotIndex))});
    setStatus("Survey ordered; it will start tomorrow.");
}

void App::onBid(int plotIndex) {
    sim::Money amount;
    if (!parseMoney(model_.bid_input, amount)) {
        setStatus("Enter a bid amount such as 1.2M or 850k.");
        return;
    }
    session_->submit(sim::BidOnPlot{Session::kPlayer,
                                    sim::PlotId(static_cast<std::uint32_t>(plotIndex)), amount});
    setStatus("Bid of " + formatMoney(amount) + " placed; it is sealed until the auction closes.");
}

void App::onBuild(int plotIndex) {
    session_->submit(
        sim::BuildMine{Session::kPlayer, sim::PlotId(static_cast<std::uint32_t>(plotIndex))});
    setStatus("Mine ordered; construction starts tomorrow.");
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
    const std::int64_t stock =
        session_->view().inventoryTonnes[static_cast<std::size_t>(resourceIndex)];
    std::int64_t tonnes = stock;
    if (!all) {
        try {
            tonnes = std::stoll(model_.sell_input);
        } catch (...) {
            setStatus("Enter a quantity in tonnes.");
            return;
        }
    }
    if (tonnes <= 0 || tonnes > stock) {
        setStatus("You hold " + formatTonnes(stock) + " of that resource.");
        return;
    }
    session_->submit(sim::SellOre{
        Session::kPlayer, sim::ResourceId(static_cast<std::uint32_t>(resourceIndex)), tonnes});
    setStatus("Sale of " + formatTonnes(tonnes) + " ordered at today's price.");
}

void App::onTakeLoan() {
    sim::Money amount;
    if (!parseMoney(model_.loan_input, amount)) {
        setStatus("Enter a loan amount such as 2M.");
        return;
    }
    session_->submit(sim::TakeLoan{Session::kPlayer, amount});
    setStatus("Loan of " + formatMoney(amount) + " requested.");
}

void App::onRepay(int loanIndex) {
    sim::Money amount;
    if (!parseMoney(model_.repay_input, amount)) {
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
    setStatus("Research ordered.");
}

void App::onSave() {
    const std::filesystem::path path = services_->saveDirectory() / kSaveFile;
    const std::vector<std::uint8_t> bytes = session_->save();
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(
                   bytes.data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
               static_cast<std::streamsize>(bytes.size()));
    setStatus(file ? "Saved to " + path.string() : "Could not write " + path.string());
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
    refresh();
    setStatus("Game loaded.");
}

void App::onNewGame() {
    newGame(clockSeed());
    refresh();
    setStatus("New game started.");
}

}  // namespace game
