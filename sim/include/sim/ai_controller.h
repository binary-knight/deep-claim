#pragma once

// The built-in competitor. A stateless set of heuristics over CompanyView so
// that its decisions depend only on the view and content, which keeps
// replays and saves exact without persisting controller memory.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/controller.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/view.h"

#include <cstdint>
#include <vector>

namespace sim {

// Two of the controller's rules, exposed for tests and tooling.
//
// The price per tonne the company should expect for the ore of one more
// mine: today's price but never above base, lowered by what the mine's own
// output does to the market and by what that costs on the ore the company
// already sells (AiParams::supplyDiscount scales the whole adjustment).
[[nodiscard]] Money aiValuedPrice(const Content& content, const CompanyView& view,
                                  ResourceId resource);
// Zero to six percent added to a bid, fixed for a given plot and company.
[[nodiscard]] std::int64_t aiBidJitterPercent(PlotId plot, CompanyId company);

class HeuristicController final : public Controller {
public:
    explicit HeuristicController(const Content& content) : content_(&content) {}

    void decide(const CompanyView& view, std::vector<Command>& out) override;

private:
    const Content* content_;
};

}  // namespace sim
