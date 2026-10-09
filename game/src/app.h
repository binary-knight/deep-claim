#pragma once

// The client application: owns the content, the session, the RmlUi document
// and the data model, and turns interface events into sim commands.

#include "sim/content.h"

#include "game/auto_pause.h"
#include "game/session.h"
#include "game/trace.h"
#include "platform/services.h"
#include "ui_model.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game {

class App {
public:
    // `playtestDir` empty turns the playtest trace off; otherwise every
    // session writes a trace, save copies and screenshots there (see
    // game/trace.h). `buildId` is recorded in the trace header.
    App(Rml::Context& context, platform::Services& services, const sim::Content& content,
        std::filesystem::path assetsDir, std::filesystem::path playtestDir = {},
        std::string buildId = {});
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    [[nodiscard]] bool loadDocument();
    // Advances the session by real time and refreshes the interface.
    void update(double seconds);
    // Keyboard shortcuts: space pauses, 1/2/3 set speed, period steps a day,
    // F9 marks the trace and takes a screenshot, F12 takes a screenshot.
    bool onKey(Rml::Input::KeyIdentifier key);

    // Screenshots are captured by the host between rendering and presenting:
    // it asks for a pending request, writes the file, then reports back.
    [[nodiscard]] std::optional<std::filesystem::path> takeScreenshotRequest();
    void screenshotDone(const std::filesystem::path& file, bool ok);
    [[nodiscard]] const std::filesystem::path& tracePath() const { return tracePath_; }

    // Steam Deck sizes (40dp controls, 14dp secondary text): forced on by
    // the host's --deck flag, otherwise follows the window width.
    void forceDeck(bool on);
    // For the layout probe: switch tabs without a click.
    void setTab(int tab);
    [[nodiscard]] const UiModel& model() const { return model_; }
    // For the layout probe, which plays a fixed game to fill every table.
    void startGame(std::uint64_t seed);
    void selectPlot(int index);
    void showPauseMenu(bool on);
    void setLogFilter(int filter);
    void refreshNow();
    [[nodiscard]] Session& session() { return *session_; }

private:
    void newGame(std::uint64_t seed);
    void openTrace();
    void traceUi(std::string_view kind, std::string_view detail);
    // Keeps a copy of save bytes next to the trace so a replay can follow a load.
    std::string keepSaveCopy(const char* role, const std::vector<std::uint8_t>& bytes);
    void requestScreenshot();
    void bindModel();
    void refresh();
    void setStatus(std::string text, bool danger = false);
    void applyDeck(bool on);
    // Auto-pause rules live in a small text file in the per-user directory.
    void loadPauseRules();
    void savePauseRules();

    // Event handlers wired to data-event-click in the RML.
    void onSetTab(int tab);
    void onSetSpeed(int speed);
    void onStep();
    void onSelectPlot(int index);
    void onSelectResource(int index);
    void onNeedAction(int action, int target);
    void onTogglePause(int index);
    void onSurvey(int plotIndex);
    void onBid(int plotIndex);
    void onBuild(int plotIndex);
    void onToggleMine(int mineIndex);
    void onSell(int resourceIndex, bool all);
    // Standing sell policies (spec 001).
    void syncPolicyInputs();
    void onApplyPolicy(bool enabled);
    void onToggleCoverCosts();
    void onTakeLoan();
    void onRepay(int loanIndex);
    void onRepayAll(int loanIndex);
    void onSetLogFilter(int filter);
    // Keeps the loan field showing the remaining credit until the player
    // types something else (clearing it counts); a new game, a load or a
    // loan taken hands the field back.
    void syncLoanInput();
    void resetLoanInput();
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
    std::size_t seenLogEntries_ = 0;  // log entries already checked for rejections
    AutoPause pauseRules_ = AutoPause::defaults();
    bool deckForced_ = false;
    std::string loanPrefill_;  // what syncLoanInput last put in the field
    bool loanEdited_ = false;  // the player has typed over it

    std::filesystem::path playtestDir_;
    std::string buildId_;
    std::string traceStem_;  // "trace-YYYYMMDD-HHMMSS", shared by the trace's files
    std::filesystem::path tracePath_;
    std::ofstream traceFile_;
    std::unique_ptr<TraceWriter> trace_;
    int saveCopies_ = 0;
    int screenshots_ = 0;
    std::optional<std::filesystem::path> screenshotRequest_;
};

}  // namespace game
