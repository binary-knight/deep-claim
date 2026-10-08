#pragma once

// Finance (tick pipeline step 6): loans, month-end charges, bankruptcy.

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/world_state.h"

#include <optional>

namespace sim {

// Principal a company still owes across its active loans.
[[nodiscard]] Money outstandingPrincipal(const WorldState& state, CompanyId company);

// What a month-end would charge for storing the company's current inventory.
[[nodiscard]] Money monthlyStorageCost(const Content& content, const WorldState& state,
                                       CompanyId company);

[[nodiscard]] std::optional<RejectReason> validateTakeLoan(const Content& content,
                                                           const WorldState& state,
                                                           const TakeLoan& command);
void applyTakeLoan(const Content& content, WorldState& state, const TakeLoan& command,
                   TickReport& report);

[[nodiscard]] std::optional<RejectReason> validateRepayLoan(const WorldState& state,
                                                            const RepayLoan& command);
void applyRepayLoan(WorldState& state, const RepayLoan& command, TickReport& report);

// On month boundaries: storage charges, loan instalments, then the
// insolvency check that can bankrupt a company.
void runFinance(const Content& content, WorldState& state, TickReport& report);

}  // namespace sim
