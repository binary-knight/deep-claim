#pragma once

// The client application: owns the content, the session, the RmlUi document
// and the data model, and turns interface events into sim commands.

#include "sim/content.h"

#include "game/session.h"
#include "platform/services.h"
#include "ui_model.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace game {

class App {
public:
    App(Rml::Context& context, platform::Services& services, const sim::Content& content,
        std::filesystem::path assetsDir);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    [[nodiscard]] bool loadDocument();
    // Advances the session by real time and refreshes the interface.
    void update(double seconds);
    // Keyboard shortcuts: space pauses, 1/2/3 set speed, period steps a day.
    bool onKey(Rml::Input::KeyIdentifier key);

private:
    void newGame(std::uint64_t seed);
    void bindModel();
    void refresh();
    void setStatus(std::string text);
    bool parseMoney(const std::string& text, sim::Money& out) const;

    // Event handlers wired to data-event-click in the RML.
    void onSetTab(int tab);
    void onSetSpeed(int speed);
    void onStep();
    void onSelectPlot(int index);
    void onSelectResource(int index);
    void onSurvey(int plotIndex);
    void onBid(int plotIndex);
    void onBuild(int plotIndex);
    void onToggleMine(int mineIndex);
    void onSell(int resourceIndex, bool all);
    void onTakeLoan();
    void onRepay(int loanIndex);
    void onResearch(int techIndex);
    void onSave();
    void onLoad();
    void onNewGame();

    Rml::Context* context_;
    platform::Services* services_;
    const sim::Content* content_;
    std::filesystem::path assetsDir_;
    std::unique_ptr<Session> session_;
    UiModel model_;
    Rml::DataModelHandle handle_;
    Rml::ElementDocument* document_ = nullptr;
    double statusAge_ = 0.0;
    sim::Day lastRefreshedDay_ = 0;
};

}  // namespace game
