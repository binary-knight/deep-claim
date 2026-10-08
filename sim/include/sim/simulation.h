#pragma once

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/controller.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"
#include "sim/view.h"
#include "sim/world_state.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sim {

struct GameSetup {
    std::string playerName = "Player";
    std::vector<std::string> aiNames;  // one AI company per name
};

class Simulation {
public:
    // Generates a new world from `content`, which must outlive the Simulation
    // and never change while it is in use. Company 0 is the player.
    Simulation(const Content& content, Seed seed, const GameSetup& setup);

    // Queues a command for the next tick. The command's company id is taken
    // as given; hosts that run controllers should use collectCommands, which
    // binds every command to the controller's own company.
    void submit(const Command& command);
    void collectCommands(CompanyId company, Controller& controller);
    [[nodiscard]] const std::vector<Command>& pendingCommands() const { return pending_; }

    // Advances the world by one in-game day and returns the unfiltered report.
    // UIs must show a company only what reportFor(company) returns.
    const TickReport& tick();
    void tick(Day days);
    [[nodiscard]] const TickReport& lastReport() const { return report_; }
    [[nodiscard]] std::vector<Notice> reportFor(CompanyId viewer) const;

    // Precondition: company is a valid index; violating it aborts.
    [[nodiscard]] CompanyView view(CompanyId company) const;

    // Public market data for charts: one price per day since the start.
    [[nodiscard]] const std::vector<Money>& priceHistory(ResourceId resource) const {
        return state_.markets[resource.index()].priceHistory;
    }

    [[nodiscard]] const Content& content() const { return *content_; }
    [[nodiscard]] const WorldState& state() const { return state_; }
    [[nodiscard]] std::uint64_t stateHash() const;

    // Saves the world and any commands queued for the next tick.
    [[nodiscard]] std::vector<std::uint8_t> save() const;
    // Replaces the current state and queued commands. Returns false and
    // leaves everything untouched if the bytes are not a valid save for this
    // content.
    [[nodiscard]] bool load(std::span<const std::uint8_t> bytes);

private:
    void applyCommands();
    void dispatch(const Command& command);
    void handle(const BidOnPlot& c);
    void handle(const CommissionSurvey& c);
    void handle(const BuildMine& c);
    void handle(const SetMineState& c);
    void handle(const SellOre& c);
    void handle(const TakeLoan& c);
    void handle(const RepayLoan& c);
    void reject(CompanyId company, PlotId plot, Money amount, RejectReason reason);

    const Content* content_;
    std::uint64_t contentHash_;
    WorldState state_;
    std::vector<Command> pending_;
    TickReport report_;
};

}  // namespace sim
