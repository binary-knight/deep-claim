#include "sim/save.h"

#include "sim/archive.h"
#include "sim/serialize.h"
#include "sim/types.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <vector>

namespace sim {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'D', 'C', 'S', 'V'};

bool validateDeposits(const Content& content, const WorldState& state) {
    for (const PlotDeposits& deposits : state.hidden.plotDeposits) {
        std::vector<std::uint8_t> seen(content.resources.size(), 0);
        for (const DepositTruth& truth : deposits.deposits) {
            if (!truth.resource.valid() || truth.resource.index() >= content.resources.size() ||
                seen[truth.resource.index()]++ != 0) {
                return false;
            }
            if (truth.tonnesTotal < 0 || truth.tonnesTotal > kMaxDepositTonnes ||
                truth.tonnesRemaining < 0 || truth.tonnesRemaining > truth.tonnesTotal) {
                return false;
            }
        }
    }
    return true;
}

// Completed techs: valid, unique, and closed under prerequisites.
bool validateCompletedTechs(const Content& content, const Company& company) {
    std::vector<std::uint8_t> seen(content.techs.size(), 0);
    for (const TechId tech : company.completedTechs) {
        if (!tech.valid() || tech.index() >= content.techs.size() || seen[tech.index()]++ != 0) {
            return false;
        }
    }
    for (const TechId tech : company.completedTechs) {
        for (const TechId prerequisite : content.techs[tech.index()].prerequisites) {
            if (seen[prerequisite.index()] == 0) {
                return false;
            }
        }
    }
    return true;
}

// Spec 001: one policy and one pair of month-to-date totals per resource,
// flags 0 or 1, quantities within the persisted bounds.
bool validateSellPolicies(const Content& content, const Company& company) {
    const std::size_t resources = content.resources.size();
    if (company.sellPolicies.size() != resources || company.autoSoldTonnes.size() != resources ||
        company.autoSoldProceeds.size() != resources || company.coverCosts > 1) {
        return false;
    }
    const bool policiesOk = std::ranges::all_of(company.sellPolicies, [](const SellPolicy& p) {
        return p.enabled <= 1 && p.reserveTonnes >= 0 && p.reserveTonnes <= kMaxStockTonnes &&
               p.floorPrice >= Money::zero() && p.floorPrice.cents() <= kMaxCashCents;
    });
    const bool tonnesOk = std::ranges::all_of(company.autoSoldTonnes, [](std::int64_t t) {
        return t >= 0 && t <= kMaxStockTonnes;
    });
    const bool proceedsOk = std::ranges::all_of(company.autoSoldProceeds, [](Money m) {
        return m >= Money::zero() && m.cents() <= kMaxCashCents;
    });
    return policiesOk && tonnesOk && proceedsOk;
}

bool validateCompany(const Content& content, const Company& company) {
    if (company.kind != CompanyKind::Player && company.kind != CompanyKind::Ai) {
        return false;
    }
    if (company.inventoryTonnes.size() != content.resources.size() || company.bankrupt > 1) {
        return false;
    }
    if (company.cash.cents() > kMaxCashCents || company.cash.cents() < -kMaxCashCents) {
        return false;
    }
    // Solvent companies are within the grace period; bankrupt ones stopped
    // exactly when they exceeded it, with the negative cash that caused it.
    const std::uint32_t grace = content.balance.bankruptcyGraceMonths;
    if (company.bankrupt == 0 && company.monthsInsolvent > grace) {
        return false;
    }
    if (company.bankrupt != 0 &&
        (company.monthsInsolvent != grace + 1 || company.cash >= Money::zero())) {
        return false;
    }
    const bool stocksOk = std::ranges::all_of(company.inventoryTonnes, [](std::int64_t tonnes) {
        return tonnes >= 0 && tonnes <= kMaxStockTonnes;
    });
    return stocksOk && validateCompletedTechs(content, company) &&
           validateSellPolicies(content, company);
}

bool validateCompanies(const Content& content, const WorldState& state) {
    if (state.companies.empty() || state.companies[0].kind != CompanyKind::Player) {
        return false;
    }
    return std::ranges::all_of(
        state.companies, [&](const Company& company) { return validateCompany(content, company); });
}

bool validatePlots(const Content& content, const WorldState& state) {
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        const Plot& plot = state.plots[i];
        if (!plot.region.valid() || plot.region.index() >= content.regions.size()) {
            return false;
        }
        if (plot.areaHectares < 1 || plot.askPrice < Money::zero() ||
            plot.basePrice < Money::zero()) {
            return false;
        }
        switch (plot.listing) {
            case PlotListing::Unlisted:
                if (plot.owner.valid() || plot.auction.valid()) {
                    return false;
                }
                break;
            case PlotListing::Listed: {
                if (plot.owner.valid() || !plot.auction.valid() ||
                    plot.auction.index() >= state.auctions.size()) {
                    return false;
                }
                const Auction& auction = state.auctions[plot.auction.index()];
                if (auction.open == 0 || auction.plot != PlotId(i)) {
                    return false;
                }
                break;
            }
            case PlotListing::Sold:
                if (!plot.owner.valid() || plot.owner.index() >= state.companies.size() ||
                    plot.auction.valid()) {
                    return false;
                }
                break;
            default:
                return false;
        }
    }
    return true;
}

// Checks one auction's bids: valid bidders, dates inside the window, one bid
// per company, unique order stamps below the counter, and (for open
// auctions) bounded reservation totals per company.
bool validateBids(const WorldState& state, const Auction& auction,
                  std::vector<std::uint8_t>& seenStamps, std::vector<std::int64_t>& reserved) {
    std::vector<std::uint8_t> bidders(state.companies.size(), 0);
    for (const Bid& bid : auction.bids) {
        if (!bid.company.valid() || bid.company.index() >= state.companies.size() ||
            bid.amount < Money::zero() || bid.day < auction.opensOnDay ||
            bid.day > auction.closesOnDay || bid.order >= state.bidCounter) {
            return false;
        }
        if (bidders[bid.company.index()]++ != 0 || seenStamps[bid.order]++ != 0) {
            return false;
        }
        if (auction.open != 0) {
            std::int64_t& total = reserved[bid.company.index()];
            if (bid.amount.cents() > kMaxCashCents - total) {
                return false;
            }
            total += bid.amount.cents();
        }
    }
    return true;
}

bool validateAuctions(const WorldState& state) {
    std::vector<std::int64_t> reserved(state.companies.size(), 0);
    std::vector<std::uint8_t> seenStamps(state.bidCounter, 0);
    for (std::uint32_t a = 0; a < state.auctions.size(); ++a) {
        const Auction& auction = state.auctions[a];
        if (!auction.plot.valid() || auction.plot.index() >= state.plots.size() ||
            auction.open > 1 || auction.closesOnDay < auction.opensOnDay ||
            auction.closesOnDay > kMaxDeadline) {
            return false;
        }
        const Plot& plot = state.plots[auction.plot.index()];
        if (auction.open != 0 &&
            (plot.listing != PlotListing::Listed || plot.auction != AuctionId(a))) {
            return false;  // exactly one open auction per listed plot
        }
        if (!validateBids(state, auction, seenStamps, reserved)) {
            return false;
        }
        if (auction.winner.valid() &&
            (auction.open != 0 || auction.winner.index() >= state.companies.size())) {
            return false;
        }
    }
    return true;
}

bool validateListingQueue(const WorldState& state) {
    if (state.listingQueue.size() != state.plots.size() ||
        state.nextListing > state.listingQueue.size()) {
        return false;
    }
    std::vector<std::uint8_t> seen(state.plots.size(), 0);
    for (std::uint32_t i = 0; i < state.listingQueue.size(); ++i) {
        const PlotId plot = state.listingQueue[i];
        if (!plot.valid() || plot.index() >= state.plots.size() || seen[plot.index()] != 0) {
            return false;
        }
        seen[plot.index()] = 1;
        // Consumed entries have been listed (and maybe sold); the rest have not.
        const bool consumed = i < state.nextListing;
        const bool listedOrSold = state.plots[plot.index()].listing != PlotListing::Unlisted;
        if (consumed != listedOrSold) {
            return false;
        }
    }
    return true;
}

// Resources present on a plot, as a membership bitmap indexed by ResourceId.
std::vector<std::uint8_t> presentResources(const Content& content, const WorldState& state,
                                           PlotId plot) {
    std::vector<std::uint8_t> present(content.resources.size(), 0);
    for (const DepositTruth& truth : state.hidden.plotDeposits[plot.index()].deposits) {
        present[truth.resource.index()] = 1;
    }
    return present;
}

bool validateEstimates(const Content& content, const WorldState& state, const Survey& survey) {
    const std::vector<std::uint8_t> present = presentResources(content, state, survey.plot);
    std::vector<std::uint8_t> reported(content.resources.size(), 0);
    for (const SurveyEstimate& estimate : survey.estimates) {
        if (!estimate.resource.valid() || estimate.resource.index() >= content.resources.size() ||
            present[estimate.resource.index()] == 0 || reported[estimate.resource.index()]++ != 0 ||
            estimate.tonnesEstimate < 0 || estimate.tonnesEstimate > kMaxDepositTonnes ||
            estimate.relativeError < Fixed::zero()) {
            return false;
        }
    }
    return true;
}

bool validateSurveys(const Content& content, const WorldState& state) {
    // (company, plot) pairs with a pending survey, for the one-pending rule.
    std::vector<std::uint8_t> pending(state.companies.size() * state.plots.size(), 0);
    for (const Survey& survey : state.surveys) {
        if (!survey.company.valid() || survey.company.index() >= state.companies.size() ||
            !survey.plot.valid() || survey.plot.index() >= state.plots.size() ||
            survey.complete > 1 || survey.completesDay > kMaxDeadline ||
            survey.completesDay != survey.commissionedDay + content.balance.surveyDurationDays) {
            return false;
        }
        // Surveys complete while processing completesDay; afterwards day > completesDay.
        const bool shouldBeComplete = survey.completesDay < state.day;
        if ((survey.complete != 0) != shouldBeComplete) {
            return false;
        }
        if (survey.complete == 0) {
            std::uint8_t& slot =
                pending[survey.company.index() * state.plots.size() + survey.plot.index()];
            if (slot++ != 0 || !survey.estimates.empty()) {
                return false;
            }
        } else if (!validateEstimates(content, state, survey)) {
            return false;
        }
    }
    return true;
}

bool validateMineAccounting(const Content& content, const WorldState& state, const Mine& mine) {
    // One mine per plot and only mines extract, so lifetime extraction equals
    // total minus remaining for every deposit, and is zero for absent ones.
    std::vector<std::int64_t> expected(content.resources.size(), 0);
    bool anythingLeft = false;
    for (const DepositTruth& truth : state.hidden.plotDeposits[mine.plot.index()].deposits) {
        expected[truth.resource.index()] = truth.tonnesTotal - truth.tonnesRemaining;
        anythingLeft = anythingLeft || truth.tonnesRemaining > 0;
    }
    for (std::size_t r = 0; r < expected.size(); ++r) {
        if (mine.extractedTonnes[r] != expected[r]) {
            return false;
        }
    }
    switch (mine.state) {
        case MineState::Building:
            return std::ranges::all_of(mine.extractedTonnes,
                                       [](std::int64_t tonnes) { return tonnes == 0; });
        case MineState::Depleted:
            return !anythingLeft;
        case MineState::Operating:
        case MineState::Mothballed:
            return anythingLeft;
        case MineState::Closed:
            return true;
    }
    return false;
}

// Ids, dates, state enum and the link back to the plot for one mine.
bool validateMineShape(const Content& content, const WorldState& state, const Mine& mine,
                       MineId id) {
    if (!mine.plot.valid() || mine.plot.index() >= state.plots.size() || !mine.owner.valid() ||
        mine.owner.index() >= state.companies.size() || mine.builtDay >= state.day ||
        static_cast<std::uint64_t>(mine.builtDay) + content.balance.mineBuildDays !=
            mine.operationalDay ||
        mine.operationalDay > kMaxDeadline ||
        mine.extractedTonnes.size() != content.resources.size() ||
        mine.lastDayTonnes.size() != content.resources.size() ||
        mine.lastOutputTonnes.size() != content.resources.size()) {
        return false;
    }
    const auto saneTonnes = [](std::int64_t tonnes) {
        return tonnes >= 0 && tonnes <= kMaxDepositTonnes;
    };
    if (!std::ranges::all_of(mine.lastDayTonnes, saneTonnes) ||
        !std::ranges::all_of(mine.lastOutputTonnes, saneTonnes)) {
        return false;
    }
    if (mine.state != MineState::Building && mine.state != MineState::Operating &&
        mine.state != MineState::Mothballed && mine.state != MineState::Depleted &&
        mine.state != MineState::Closed) {
        return false;
    }
    // Closed mines belong to bankrupt companies and vice versa.
    const bool ownerBankrupt = state.companies[mine.owner.index()].bankrupt != 0;
    if ((mine.state == MineState::Closed) != ownerBankrupt) {
        return false;
    }
    // Mines become operational while processing operationalDay.
    const bool shouldBeBuilding = mine.operationalDay >= state.day;
    if (mine.state != MineState::Closed &&
        (mine.state == MineState::Building) != shouldBeBuilding) {
        return false;
    }
    const Plot& plot = state.plots[mine.plot.index()];
    if (plot.owner != mine.owner || plot.mine != id) {
        return false;  // exactly one mine per plot, owned by the plot's owner
    }
    return std::ranges::all_of(mine.extractedTonnes, [](std::int64_t tonnes) {
        return tonnes >= 0 && tonnes <= kMaxStockTonnes;
    });
}

bool validateMines(const Content& content, const WorldState& state) {
    for (std::uint32_t i = 0; i < state.mines.size(); ++i) {
        const Mine& mine = state.mines[i];
        if (!validateMineShape(content, state, mine, MineId(i)) ||
            !validateMineAccounting(content, state, mine)) {
            return false;
        }
    }
    // Every plot that claims a mine must be that mine's plot.
    for (std::uint32_t i = 0; i < state.plots.size(); ++i) {
        const MineId mine = state.plots[i].mine;
        if (mine.valid() &&
            (mine.index() >= state.mines.size() || state.mines[mine.index()].plot != PlotId(i))) {
            return false;
        }
    }
    return true;
}

bool validateMarkets(const Content& content, const WorldState& state) {
    if (state.markets.size() != content.resources.size()) {
        return false;
    }
    for (std::uint32_t i = 0; i < state.markets.size(); ++i) {
        const Market& market = state.markets[i];
        const ResourceDef& resource = content.resources[i];
        const Money floor = resource.basePrice.scaled(content.balance.priceFloorMult);
        const Money cap = resource.basePrice.scaled(content.balance.priceCapMult);
        if (market.price < floor || market.price > cap || market.soldToday != 0 ||
            market.demandMult < content.balance.demandMultMin ||
            market.demandMult > content.balance.demandMultMax ||
            market.supplySmoothed < Fixed::zero() ||
            market.supplySmoothed > Fixed::fromInt(kMaxSupplyPerDay) ||
            market.priceHistory.size() != static_cast<std::size_t>(state.day) + 1 ||
            market.priceHistory.front() != resource.basePrice ||
            market.priceHistory.back() != market.price) {
            return false;
        }
        if (!std::ranges::all_of(market.priceHistory,
                                 [&](Money price) { return price >= floor && price <= cap; })) {
            return false;
        }
    }
    return true;
}

bool validateLoan(const Content& content, const WorldState& state, const Loan& loan,
                  std::vector<std::int64_t>& outstanding) {
    const BalanceParams& balance = content.balance;
    if (!loan.company.valid() || loan.company.index() >= state.companies.size() ||
        loan.active > 1 || loan.principal <= Money::zero() ||
        loan.principal > balance.loanMaxPrincipal || loan.remaining < Money::zero() ||
        loan.remaining > loan.principal || loan.takenDay > state.day ||
        loan.monthsRemaining > balance.loanTermMonths) {
        return false;
    }
    // Canonical instalment: ceil(principal / term).
    const std::int64_t term = balance.loanTermMonths;
    const Money instalment = Money::fromCents((loan.principal.cents() + term - 1) / term);
    if (loan.principalPerMonth != instalment) {
        return false;
    }
    const Company& company = state.companies[loan.company.index()];
    if (loan.active != 0) {
        if (company.bankrupt != 0 || loan.remaining == Money::zero() || loan.monthsRemaining == 0) {
            return false;
        }
        // The schedule must be able to clear the balance in the time left.
        if (loan.remaining.cents() >
            mulDiv(instalment.cents(), static_cast<std::int64_t>(loan.monthsRemaining), 1)) {
            return false;
        }
        // Check capacity before adding so a forged balance cannot overflow.
        std::int64_t& total = outstanding[loan.company.index()];
        if (loan.remaining.cents() > balance.loanMaxPrincipal.cents() - total) {
            return false;
        }
        total += loan.remaining.cents();
        return true;
    }
    // Inactive: paid off (zero balance) or written off at bankruptcy.
    return loan.monthsRemaining == 0 && (loan.remaining == Money::zero() || company.bankrupt != 0);
}

bool validateLoans(const Content& content, const WorldState& state) {
    std::vector<std::int64_t> outstanding(state.companies.size(), 0);
    return std::ranges::all_of(state.loans, [&](const Loan& loan) {
        return validateLoan(content, state, loan, outstanding);
    });
}

// Validity of one research record; `active` and `lastEnd` track per-company
// state across the records in vector (creation) order.
bool validateResearchRecord(const Content& content, const WorldState& state,
                            const Research& research, std::vector<std::uint8_t>& active,
                            std::vector<std::uint64_t>& lastEnd) {
    if (!research.company.valid() || research.company.index() >= state.companies.size() ||
        !research.tech.valid() || research.tech.index() >= content.techs.size() ||
        research.complete > 1 || research.startedDay >= state.day ||
        research.completesDay > kMaxDeadline) {
        return false;
    }
    const TechDef& tech = content.techs[research.tech.index()];
    if (static_cast<std::uint64_t>(research.startedDay) + tech.durationDays !=
        research.completesDay) {
        return false;
    }
    // Completes while processing completesDay; afterwards day > completesDay.
    const bool shouldBeComplete = research.completesDay < state.day;
    if ((research.complete != 0) != shouldBeComplete) {
        return false;
    }
    // One project at a time: each starts after the previous one finished.
    const std::size_t c = research.company.index();
    if (lastEnd[c] != 0 && static_cast<std::uint64_t>(research.startedDay) <= lastEnd[c]) {
        return false;
    }
    lastEnd[c] = research.completesDay;
    const Company& company = state.companies[c];
    const bool known = std::find(company.completedTechs.begin(), company.completedTechs.end(),
                                 research.tech) != company.completedTechs.end();
    if (research.complete == 0) {
        if (active[c]++ != 0 || known) {
            return false;
        }
        // Pending research must have been startable: prerequisites complete.
        return std::ranges::all_of(tech.prerequisites, [&](TechId p) {
            return std::find(company.completedTechs.begin(), company.completedTechs.end(), p) !=
                   company.completedTechs.end();
        });
    }
    return known;  // finished research must have granted its tech
}

bool validateResearch(const Content& content, const WorldState& state) {
    std::vector<std::uint8_t> active(state.companies.size(), 0);
    std::vector<std::uint64_t> lastEnd(state.companies.size(), 0);
    return std::ranges::all_of(state.research, [&](const Research& research) {
        return validateResearchRecord(content, state, research, active, lastEnd);
    });
}

bool validateEvents(const Content& content, const WorldState& state) {
    // Per definition: instances in vector order must not overlap (a new one
    // may start exactly on the day its predecessor ends).
    std::vector<std::uint64_t> lastEnd(content.events.size(), 0);
    std::vector<std::uint8_t> seen(content.events.size(), 0);
    for (const ActiveEvent& event : state.events) {
        if (!event.event.valid() || event.event.index() >= content.events.size() ||
            event.active > 1 || event.startedDay >= state.day || event.endsDay > kMaxDeadline) {
            return false;
        }
        const EventDef& def = content.events[event.event.index()];
        if (static_cast<std::uint64_t>(event.startedDay) + def.durationDays != event.endsDay ||
            event.startedDay / kDaysPerYear + 1 < def.minYear) {
            return false;
        }
        // Expires while processing endsDay; afterwards day > endsDay.
        const bool shouldBeActive = event.endsDay >= state.day;
        if ((event.active != 0) != shouldBeActive) {
            return false;
        }
        const std::size_t e = event.event.index();
        if (seen[e] != 0 && static_cast<std::uint64_t>(event.startedDay) < lastEnd[e]) {
            return false;
        }
        seen[e] = 1;
        lastEnd[e] = event.endsDay;
    }
    return true;
}

bool validateWorldImpl(const Content& content, const WorldState& state) {
    if (state.day > kMaxDay || state.plots.size() != state.hidden.plotDeposits.size()) {
        return false;
    }
    // Deposits first: later checks index by resource and assume valid ids.
    return validateDeposits(content, state) && validateCompanies(content, state) &&
           validatePlots(content, state) && validateAuctions(state) &&
           validateListingQueue(state) && validateSurveys(content, state) &&
           validateMines(content, state) && validateMarkets(content, state) &&
           validateLoans(content, state) && validateResearch(content, state) &&
           validateEvents(content, state);
}

}  // namespace

std::uint64_t hashWorld(const WorldState& state) {
    return hashOf(state);
}

std::uint64_t hashContent(const Content& content) {
    return hashOf(content);
}

bool validateWorld(const Content& content, const WorldState& state) {
    try {
        return validateWorldImpl(content, state);
    } catch (const std::bad_alloc&) {
        return false;
    }
}

std::vector<std::uint8_t> saveGame(const WorldState& state, const std::vector<Command>& pending,
                                   std::uint64_t contentHash) {
    WriteArchive archive;
    for (const std::uint8_t byte : kMagic) {
        archive(byte);
    }
    std::uint32_t version = kSaveVersion;
    archive(version);
    archive(contentHash);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    archive(const_cast<WorldState&>(state));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    archive(const_cast<std::vector<Command>&>(pending));
    return archive.release();
}

bool loadGame(std::span<const std::uint8_t> bytes, const Content& content, WorldState& outState,
              std::vector<Command>& outPending) {
    ReadArchive archive(bytes);
    for (const std::uint8_t expected : kMagic) {
        std::uint8_t byte = 0;
        archive(byte);
        if (byte != expected) {
            return false;
        }
    }
    std::uint32_t version = 0;
    archive(version);
    if (archive.failed() || version != kSaveVersion) {
        return false;
    }
    std::uint64_t savedContentHash = 0;
    archive(savedContentHash);
    if (archive.failed() || savedContentHash != hashContent(content)) {
        return false;
    }
    archive(outState);
    archive(outPending);
    return !archive.failed() && archive.atEnd() && validateWorld(content, outState);
}

}  // namespace sim
