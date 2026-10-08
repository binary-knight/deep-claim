#pragma once

// The built-in competitor. A stateless set of heuristics over CompanyView so
// that its decisions depend only on the view and content, which keeps
// replays and saves exact without persisting controller memory.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/controller.h"
#include "sim/view.h"

#include <vector>

namespace sim {

class HeuristicController final : public Controller {
public:
    explicit HeuristicController(const Content& content) : content_(&content) {}

    void decide(const CompanyView& view, std::vector<Command>& out) override;

private:
    const Content* content_;
};

}  // namespace sim
