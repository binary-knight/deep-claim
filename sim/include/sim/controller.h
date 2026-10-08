#pragma once

// A controller decides a company's commands for the day from its view. The
// player's controller lives in the game client and forwards UI actions; AI
// controllers live in the sim library so sim_runner can use them. Both go
// through the same command interface.

#include "sim/commands.h"
#include "sim/view.h"

#include <vector>

namespace sim {

class Controller {
public:
    Controller() = default;
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;
    virtual ~Controller() = default;

    virtual void decide(const CompanyView& view, std::vector<Command>& out) = 0;
};

}  // namespace sim
