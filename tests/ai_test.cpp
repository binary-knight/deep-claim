#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/sell_policy.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"
#include "sim/world_state.h"

#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace {

const sim::CompanyId kRival(1);

// Runs the game with the heuristic AI driving company 1.
void playYears(sim::Simulation& simulation, sim::HeuristicController& ai, sim::Day days,
               std::uint32_t& rejections) {
    for (sim::Day d = 0; d < days; ++d) {
        simulation.collectCommands(kRival, ai);
        const sim::TickReport& report = simulation.tick();
        for (const sim::Notice& n : report.notices) {
            if (n.kind == sim::NoticeKind::CommandRejected && n.company == kRival) {
                ++rejections;
            }
        }
    }
}

}  // namespace

TEST_CASE("The AI plays twenty years without invalid commands or crashes", "[ai]") {
    const sim::Content content = test::makeContent();
    for (sim::Seed seed = 1; seed <= 3; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        sim::HeuristicController ai(content);
        std::uint32_t rejections = 0;
        playYears(simulation, ai, sim::kDaysPerYear * 20, rejections);
        CHECK(rejections == 0);
        const sim::CompanyView view = simulation.view(kRival);
        // It did something: owns land, built mines, sold ore or researched.
        const bool active =
            view.companies[1].plotsOwned > 0 || !view.mines.empty() || !view.completedTechs.empty();
        CHECK(active);
    }
}

TEST_CASE("The AI is deterministic and reproducible from a save", "[ai][determinism]") {
    const sim::Content content = test::makeContent();
    sim::Simulation a(content, 11, test::setup());
    sim::Simulation b(content, 11, test::setup());
    sim::HeuristicController ai(content);
    std::uint32_t ignored = 0;
    playYears(a, ai, 400, ignored);
    playYears(b, ai, 400, ignored);
    CHECK(a.stateHash() == b.stateHash());

    sim::Simulation restored(content, 99, test::setup());
    REQUIRE(restored.load(a.save()));
    playYears(a, ai, 400, ignored);
    playYears(restored, ai, 400, ignored);
    CHECK(a.stateHash() == restored.stateHash());
}

TEST_CASE("A changed AI knob changes the content fingerprint", "[ai][content]") {
    // The controller runs inside the sim: a save or trace made with one set
    // of knobs must not open against another.
    const sim::Content content = test::makeContent();
    sim::Content tuned = content;
    tuned.ai.requiredReturn = sim::Fixed::fromRatio(11, 10);
    CHECK(sim::hashContent(tuned) != sim::hashContent(content));
    sim::Content reserve = content;
    reserve.ai.cashReserve = sim::Money::fromUnits(500'001);
    CHECK(sim::hashContent(reserve) != sim::hashContent(content));
    sim::Content supply = content;
    supply.ai.supplyDiscount = sim::Fixed::fromRatio(1, 2);
    CHECK(sim::hashContent(supply) != sim::hashContent(content));
    sim::Content first = content;
    first.ai.firstMineRecoverableShare = sim::Fixed::fromRatio(6, 10);
    CHECK(sim::hashContent(first) != sim::hashContent(content));
    sim::Content firstReturn = content;
    firstReturn.ai.firstMineRequiredReturn = sim::Fixed::fromRatio(11, 10);
    CHECK(sim::hashContent(firstReturn) != sim::hashContent(content));
    // Spec 010: the knobs the AI races with.
    sim::Content share = content;
    share.ai.programReserveShare = sim::Fixed::fromRatio(1, 3);
    CHECK(sim::hashContent(share) != sim::hashContent(content));
    sim::Content buffer = content;
    buffer.ai.programCashBuffer = sim::Money::fromUnits(123'456);
    CHECK(sim::hashContent(buffer) != sim::hashContent(content));
    sim::Content weight = content;
    weight.ai.programOreLandWeight = sim::Fixed::fromInt(7);
    CHECK(sim::hashContent(weight) != sim::hashContent(content));
    sim::Content progress = content;
    progress.ai.programResearchProgress = sim::Fixed::fromRatio(1, 3);
    CHECK(sim::hashContent(progress) != sim::hashContent(content));
    sim::Content capped = content;
    capped.regions[0].deposits[0].tonnesMaxMult = sim::Fixed::fromInt(4);
    CHECK(sim::hashContent(capped) != sim::hashContent(content));
    sim::Content threshold = content;
    threshold.balance.openingMineablePresence = sim::Fixed::fromRatio(6, 10);
    CHECK(sim::hashContent(threshold) != sim::hashContent(content));

    const sim::Simulation original(content, 3, test::setup());
    sim::Simulation other(tuned, 3, test::setup());
    CHECK_FALSE(other.load(original.save()));
}

TEST_CASE("The AI values an ore lower the more of it the company already sells", "[ai]") {
    sim::Content content = test::makeContent();
    content.ai.supplyDiscount = sim::Fixed::one();
    const sim::ResourceId copper(1);
    const sim::ResourceDef& def = content.resources[copper.index()];

    // A bare view: copper at base, nobody selling, the company without mines.
    sim::CompanyView view;
    view.me = sim::CompanyId(0);
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        sim::MarketView market;
        market.resource = sim::ResourceId(r);
        market.price = content.resources[r].basePrice;
        market.basePrice = content.resources[r].basePrice;
        market.baseDemandTonnesPerDay = content.resources[r].baseDemandTonnesPerDay;
        view.markets.push_back(market);
    }
    const auto withMines = [&](std::uint32_t count) {
        sim::CompanyView owner = view;
        for (std::uint32_t i = 0; i < count; ++i) {
            sim::MineView mine;
            mine.state = sim::MineState::Operating;
            mine.lastOutputTonnes.assign(content.resources.size(), 0);
            mine.lastOutputTonnes[copper.index()] = def.baseOutputTonnesPerDay;
            owner.mines.push_back(mine);
        }
        // What those mines sell is already in the market's supply.
        owner.markets[copper.index()].recentSupplyTonnesPerDay = def.baseOutputTonnesPerDay * count;
        return owner;
    };

    // A newcomer: base price lowered only by its own mine's effect, which is
    // the market rule itself: base x (100 / 160) ^ 0.8 = 0.687 x base.
    const sim::Money alone = sim::aiValuedPrice(content, view, copper);
    CHECK(alone < def.basePrice);
    CHECK(alone > def.basePrice.scaled(sim::Fixed::fromRatio(68, 100)));
    CHECK(alone < def.basePrice.scaled(sim::Fixed::fromRatio(69, 100)));

    // With two mines of its own the next one is worth less per tonne, and
    // less than the same market looks to a company that owns none of it.
    const sim::CompanyView two = withMines(2);
    sim::CompanyView outsider = two;
    outsider.mines.clear();
    const sim::Money third = sim::aiValuedPrice(content, two, copper);
    const sim::Money entrant = sim::aiValuedPrice(content, outsider, copper);
    CHECK(third < entrant);
    CHECK(sim::aiValuedPrice(content, withMines(4), copper) < third);
    // A mothballed mine sells nothing and costs nothing in the valuation.
    sim::CompanyView idle = two;
    for (sim::MineView& mine : idle.mines) {
        mine.state = sim::MineState::Mothballed;
    }
    CHECK(sim::aiValuedPrice(content, idle, copper) == entrant);

    // A spike is never chased: above base the valuation starts from base.
    sim::CompanyView spike = view;
    spike.markets[copper.index()].price = def.basePrice * 3;
    CHECK(sim::aiValuedPrice(content, spike, copper) == alone);
    // Below base it starts from today's price.
    sim::CompanyView slump = view;
    slump.markets[copper.index()].price = def.basePrice / 2;
    CHECK(sim::aiValuedPrice(content, slump, copper) < alone);

    // With the rule switched off the price is today's, capped at base,
    // whatever the company owns; and the valuation never goes below zero.
    content.ai.supplyDiscount = sim::Fixed::zero();
    CHECK(sim::aiValuedPrice(content, view, copper) == def.basePrice);
    CHECK(sim::aiValuedPrice(content, withMines(4), copper) == def.basePrice);
    content.ai.supplyDiscount = sim::Fixed::one();
    CHECK(sim::aiValuedPrice(content, withMines(500), copper) >= sim::Money::zero());
}

TEST_CASE("Bid jitter favours neither company across plots", "[ai]") {
    // The old formula let company 1 outbid company 0 by six points on every
    // plot. Over the first 120 plots each must come out ahead often.
    std::uint32_t zeroAhead = 0;
    std::uint32_t oneAhead = 0;
    for (std::uint32_t p = 0; p < 120; ++p) {
        const std::int64_t zero = sim::aiBidJitterPercent(sim::PlotId(p), sim::CompanyId(0));
        const std::int64_t one = sim::aiBidJitterPercent(sim::PlotId(p), sim::CompanyId(1));
        CHECK(zero >= 0);
        CHECK(zero <= 6);
        CHECK(one >= 0);
        CHECK(one <= 6);
        zeroAhead += zero > one ? 1U : 0U;
        oneAhead += one > zero ? 1U : 0U;
    }
    CHECK(zeroAhead >= 35);
    CHECK(oneAhead >= 35);
}

namespace {

// Days until the rival first bids, or `limit` when it never does.
sim::Day firstBidDay(const sim::Content& content, sim::Seed seed, sim::Day limit) {
    sim::Simulation simulation(content, seed, test::setup());
    sim::HeuristicController ai(content);
    for (sim::Day d = 0; d < limit; ++d) {
        simulation.collectCommands(kRival, ai);
        simulation.tick();
        for (const sim::Auction& auction : simulation.state().auctions) {
            for (const sim::Bid& bid : auction.bids) {
                if (bid.company == kRival) {
                    return d;
                }
            }
        }
    }
    return limit;
}

}  // namespace

TEST_CASE("A company with no mine buys on the first-mine terms", "[ai]") {
    // General terms nothing can meet; first-mine terms anything worth
    // surveying can. The AI must bid inside the first auction window.
    sim::Content content = test::makeContent();
    content.ai.requiredReturn = sim::Fixed::fromInt(1000);
    content.ai.firstMineRequiredReturn = sim::Fixed::one();
    content.ai.firstMineRecoverableShare = sim::Fixed::one();
    bool bidInWindow = false;
    for (sim::Seed seed = 1; seed <= 4; ++seed) {
        bidInWindow = bidInWindow || firstBidDay(content, seed, content.balance.auctionWindowDays) <
                                         content.balance.auctionWindowDays;
    }
    CHECK(bidInWindow);

    // With the first-mine terms as strict as the general ones it never bids.
    content.ai.firstMineRequiredReturn = sim::Fixed::fromInt(1000);
    for (sim::Seed seed = 1; seed <= 4; ++seed) {
        CHECK(firstBidDay(content, seed, sim::kDaysPerYear) == sim::kDaysPerYear);
    }
}

namespace {

// The orders the AI gives for a hand-built view. The sim's views say which
// projects are open (CompanyView::researchOpen, from sim::researchOpenTo);
// a view built by hand is given the same marks here, from what it holds:
// a project not known and not in progress, its prerequisites known and the
// stages it waits for complete.
std::vector<sim::Command> decideFor(const sim::Content& content, sim::CompanyView view) {
    const auto known = [&](sim::TechId tech) {
        return std::find(view.completedTechs.begin(), view.completedTechs.end(), tech) !=
               view.completedTechs.end();
    };
    view.researchOpen.assign(content.techs.size(), 0);
    for (std::uint32_t t = 0; t < content.techs.size(); ++t) {
        const sim::TechDef& def = content.techs[t];
        const bool researching = view.activeResearch && view.activeResearch->tech == sim::TechId(t);
        const bool open = !known(sim::TechId(t)) && !researching &&
                          std::all_of(def.prerequisites.begin(), def.prerequisites.end(), known) &&
                          def.requiresProgramStage <= view.program.stagesCompleted;
        view.researchOpen[t] = open ? 1 : 0;
    }
    sim::HeuristicController ai(content);
    std::vector<sim::Command> out;
    ai.decide(view, out);
    return out;
}

// A hand-built view for the rival: one listed plot at an ask of 1M with a
// 2M mine quote, surveyed at `tonnes` of iron, and cash for anything. In the
// fixture iron sells at 60 and a mine costs 8,000 a day for 400 t, so every
// recoverable tonne is worth 34 after opex (60 less 8,000 x 1.3 / 400).
sim::CompanyView prospectView(const sim::Content& content, std::int64_t tonnes, bool hasMine) {
    sim::CompanyView view;
    view.me = kRival;
    view.day = 30;
    view.cash = sim::Money::fromUnits(20'000'000);
    view.loanCapacity = content.balance.loanMaxPrincipal;
    for (const sim::ResourceDef& resource : content.resources) {
        view.mineOpexQuotes.push_back(resource.mineOpexPerDay);
    }
    view.inventoryTonnes.assign(content.resources.size(), 0);
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        sim::MarketView market;
        market.resource = sim::ResourceId(r);
        market.price = content.resources[r].basePrice;
        market.basePrice = content.resources[r].basePrice;
        market.baseDemandTonnesPerDay = content.resources[r].baseDemandTonnesPerDay;
        view.markets.push_back(market);
    }
    view.companies.resize(2);
    sim::PlotView plot;
    plot.id = sim::PlotId(0);
    plot.region = sim::RegionId(0);
    plot.areaHectares = 500;
    plot.askPrice = sim::Money::fromUnits(1'000'000);
    plot.listing = sim::PlotListing::Listed;
    plot.auction = sim::AuctionView{sim::AuctionId(0), 60, 0, sim::Money::zero()};
    plot.mineCapexQuote = sim::Money::fromUnits(2'000'000);
    view.plots.push_back(plot);
    sim::SurveyView survey;
    survey.id = sim::SurveyId(0);
    survey.plot = plot.id;
    survey.completesDay = 25;
    survey.complete = true;
    survey.estimates.push_back({sim::ResourceId(0), tonnes, sim::Fixed::fromRatio(1, 2), false});
    view.surveys.push_back(survey);
    if (hasMine) {
        // A mine elsewhere, too new to be judged, producing nothing yet.
        sim::MineView mine;
        mine.id = sim::MineId(0);
        mine.state = sim::MineState::Operating;
        mine.operationalDay = view.day;
        mine.lastOutputTonnes.assign(content.resources.size(), 0);
        view.mines.push_back(mine);
    }
    return view;
}

bool bids(const sim::Content& content, const sim::CompanyView& view) {
    const std::vector<sim::Command> out = decideFor(content, view);
    return std::any_of(out.begin(), out.end(), [](const sim::Command& command) {
        return std::holds_alternative<sim::BidOnPlot>(command);
    });
}

}  // namespace

TEST_CASE("The same plot is bought without a mine and passed over with one", "[ai]") {
    // 200,000 t of iron is worth 6.8M net at full recovery against an outlay
    // of 3M (ask plus mine). The two cases differ only in owning a mine.
    constexpr std::int64_t kTonnes = 200'000;

    // The return multiple: 1 for a first mine (3M needed), 3 otherwise (9M).
    sim::Content byReturn = test::makeContent();
    byReturn.ai.recoverableShare = sim::Fixed::one();
    byReturn.ai.firstMineRecoverableShare = sim::Fixed::one();
    byReturn.ai.firstMineRequiredReturn = sim::Fixed::one();
    byReturn.ai.requiredReturn = sim::Fixed::fromInt(3);
    CHECK(bids(byReturn, prospectView(byReturn, kTonnes, false)));
    CHECK_FALSE(bids(byReturn, prospectView(byReturn, kTonnes, true)));
    // With the general multiple as easy, the mine owner buys too.
    byReturn.ai.requiredReturn = sim::Fixed::one();
    CHECK(bids(byReturn, prospectView(byReturn, kTonnes, true)));

    // The tonnage haircut: all of it for a first mine, a tenth otherwise
    // (0.68M, under the 3M outlay), with both return multiples at 1.
    sim::Content byShare = test::makeContent();
    byShare.ai.requiredReturn = sim::Fixed::one();
    byShare.ai.firstMineRequiredReturn = sim::Fixed::one();
    byShare.ai.firstMineRecoverableShare = sim::Fixed::one();
    byShare.ai.recoverableShare = sim::Fixed::fromRatio(1, 10);
    CHECK(bids(byShare, prospectView(byShare, kTonnes, false)));
    CHECK_FALSE(bids(byShare, prospectView(byShare, kTonnes, true)));
    // And a first mine is held to its own haircut, not waved through.
    byShare.ai.firstMineRecoverableShare = sim::Fixed::fromRatio(1, 10);
    CHECK_FALSE(bids(byShare, prospectView(byShare, kTonnes, false)));
}

TEST_CASE("The AI stops acting once bankrupt", "[ai]") {
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 1, test::setup());
    // Deeper in the red than any loan can cover, so the bootstrap loan rule
    // cannot rescue it and it must go bankrupt after the grace period.
    sim::WorldState edited = simulation.state();
    edited.companies[1].cash = sim::Money::fromUnits(-30'000'000);
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
    sim::HeuristicController ai(content);
    std::uint32_t rejections = 0;
    playYears(simulation, ai, sim::kDaysPerYear, rejections);
    CHECK(simulation.state().companies[1].bankrupt == 1);
    CHECK(rejections == 0);
}

TEST_CASE("The AI's daily commands are jointly affordable", "[ai]") {
    // Rich in opportunities, short on cash: several surveyed listings, an owned
    // plot ready to build on, research available. Everything it issues must be
    // accepted by the sim, which only happens if one budget governs all rules.
    const sim::Content content = test::makeContent();
    sim::Simulation simulation(content, 5, test::setup());
    sim::HeuristicController ai(content);
    std::uint32_t rejections = 0;
    // Let it survey and buy for two years, then squeeze its cash.
    playYears(simulation, ai, sim::kDaysPerYear * 2, rejections);
    CHECK(rejections == 0);
    sim::WorldState squeezed = simulation.state();
    squeezed.companies[1].cash =
        sim::Money::fromUnits(2'400'000) + simulation.view(kRival).reservedCash;
    REQUIRE(simulation.load(sim::saveGame(squeezed, {}, sim::hashContent(content))));
    playYears(simulation, ai, sim::kDaysPerYear, rejections);
    CHECK(rejections == 0);
}

TEST_CASE("The AI does not strand itself on land it cannot develop", "[ai]") {
    const sim::Content content = test::makeContent();
    for (sim::Seed seed = 1; seed <= 4; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        sim::HeuristicController ai(content);
        std::uint32_t rejections = 0;
        playYears(simulation, ai, sim::kDaysPerYear * 6, rejections);
        const sim::CompanyView view = simulation.view(kRival);
        // Either it never bought land, or every plot it owns has a mine or it
        // can still fund one from cash plus borrowing capacity.
        for (const sim::PlotView& plot : view.plots) {
            if (plot.owner != kRival || plot.mine.has_value()) {
                continue;
            }
            CHECK((view.cash - view.reservedCash + view.loanCapacity).cents() >=
                  plot.mineCapexQuote.cents());
        }
    }
}

// Spec 008: what a mine earns is the sim's figure, carried on the view.

namespace {

const sim::ResourceId kIron(0);
const sim::ResourceId kCopper(1);
const sim::ResourceId kLithium(2);  // locked until tech 1 in the fixture

// The fixture with a different running cost per ore: iron 8,000, copper
// 12,000 and (locked) lithium 20,000 a day.
sim::Content costedContent() {
    sim::Content c = test::makeContent();
    c.resources[kIron.index()].mineOpexPerDay = sim::Money::fromUnits(8'000);
    c.resources[kCopper.index()].mineOpexPerDay = sim::Money::fromUnits(12'000);
    c.resources[kLithium.index()].mineOpexPerDay = sim::Money::fromUnits(20'000);
    return c;
}

// A new game in which the rival holds one mine in `state` whose most recent
// operating day produced `output` (iron, copper, lithium), with iron at
// `ironPrice`, copper at $900 and lithium at $14,000.
sim::WorldState worldWithMine(const sim::Content& content, sim::MineState state,
                              const std::vector<std::int64_t>& output,
                              sim::Money ironPrice = sim::Money::fromUnits(60)) {
    const sim::Simulation simulation(content, 1, test::setup());
    sim::WorldState world = simulation.state();
    world.markets[kIron.index()].price = ironPrice;
    world.markets[kCopper.index()].price = sim::Money::fromUnits(900);
    world.markets[kLithium.index()].price = sim::Money::fromUnits(14'000);
    sim::Mine mine;
    mine.plot = sim::PlotId(0);
    mine.owner = kRival;
    mine.state = state;
    mine.extractedTonnes = output;
    mine.lastDayTonnes.assign(content.resources.size(), 0);
    mine.lastOutputTonnes = output;
    world.mines.push_back(mine);
    return world;
}

sim::MineView mineViewOf(const sim::Content& content, const sim::WorldState& world) {
    const sim::CompanyView view = sim::makeCompanyView(content, world, kRival);
    REQUIRE(view.mines.size() == 1);
    return view.mines[0];
}

}  // namespace

TEST_CASE("The view quotes a running cost for each ore", "[ai][view][spec008]") {
    const sim::Content content = costedContent();
    const sim::Simulation simulation(content, 1, test::setup());
    const sim::CompanyView view = simulation.view(kRival);
    // Every ore is quoted, the locked one too: the quote is what a mine
    // producing that ore would cost, whoever can dig it today.
    REQUIRE(view.mineOpexQuotes.size() == content.resources.size());
    CHECK(view.mineOpexQuotes[kIron.index()] == sim::Money::fromUnits(8'000));
    CHECK(view.mineOpexQuotes[kCopper.index()] == sim::Money::fromUnits(12'000));
    CHECK(view.mineOpexQuotes[kLithium.index()] == sim::Money::fromUnits(20'000));
}

TEST_CASE("A single-ore mine's view carries its margin and break-even price",
          "[ai][view][spec008]") {
    const sim::Content content = costedContent();
    // 400 t of iron at $60 is $24,000 against a cost of $8,000.
    const sim::MineView atBase =
        mineViewOf(content, worldWithMine(content, sim::MineState::Operating, {400, 0, 0}));
    CHECK(atBase.hasEarnings);
    CHECK(atBase.dailyOpex == sim::Money::fromUnits(8'000));
    CHECK(atBase.operatingOpex == sim::Money::fromUnits(8'000));
    CHECK(atBase.dailyMargin == sim::Money::fromUnits(16'000));
    CHECK(atBase.breakEvenPrice == sim::Money::fromUnits(20));

    // The margin follows the price the view is built at; the break-even
    // price does not move.
    const sim::MineView atTen = mineViewOf(
        content,
        worldWithMine(content, sim::MineState::Operating, {400, 0, 0}, sim::Money::fromUnits(10)));
    CHECK(atTen.hasEarnings);
    CHECK(atTen.dailyMargin == sim::Money::fromUnits(-4'000));
    CHECK(atTen.breakEvenPrice == sim::Money::fromUnits(20));
}

TEST_CASE("A two-ore mine's view pays the dearer cost and prices its principal ore",
          "[ai][view][spec008]") {
    const sim::Content content = costedContent();
    // 100 t of iron ($6,000) and 60 t of copper ($54,000) against copper's
    // $12,000: copper earns most, and must fetch $100 to cover what iron
    // leaves of the cost.
    const sim::MineView mine =
        mineViewOf(content, worldWithMine(content, sim::MineState::Operating, {100, 60, 0}));
    CHECK(mine.hasEarnings);
    CHECK(mine.dailyOpex == sim::Money::fromUnits(12'000));
    CHECK(mine.dailyMargin == sim::Money::fromUnits(48'000));
    CHECK(mine.breakEvenPrice == sim::Money::fromUnits(100));
}

TEST_CASE("A mothballed mine's view shows its idle cost and what reopening would earn",
          "[ai][view][spec008]") {
    const sim::Content content = costedContent();
    const sim::MineView mine =
        mineViewOf(content, worldWithMine(content, sim::MineState::Mothballed, {400, 0, 0}));
    CHECK(mine.hasEarnings);
    CHECK(mine.dailyOpex ==
          sim::Money::fromUnits(8'000).scaled(content.balance.mineMothballOpexMult));
    CHECK(mine.operatingOpex == sim::Money::fromUnits(8'000));
    CHECK(mine.dailyMargin == sim::Money::fromUnits(16'000));
    CHECK(mine.breakEvenPrice == sim::Money::fromUnits(20));
}

TEST_CASE("A mine with nothing to show presents no margin and no break-even",
          "[ai][view][spec008]") {
    const sim::Content content = costedContent();
    // Running but yet to produce: charged the dearest ore its owner can
    // dig (copper; lithium is locked), with no earnings figure.
    const sim::MineView fresh =
        mineViewOf(content, worldWithMine(content, sim::MineState::Operating, {0, 0, 0}));
    CHECK_FALSE(fresh.hasEarnings);
    CHECK(fresh.dailyOpex == sim::Money::fromUnits(12'000));
    CHECK(fresh.dailyMargin == sim::Money::zero());
    CHECK(fresh.breakEvenPrice == sim::Money::zero());
}

TEST_CASE("A mine that is building, depleted or closed presents no figures",
          "[ai][view][spec008]") {
    // Under construction, and finished for good with output still on record.
    const sim::Content content = costedContent();
    for (const sim::MineState state :
         {sim::MineState::Building, sim::MineState::Depleted, sim::MineState::Closed}) {
        const sim::MineView mine = mineViewOf(content, worldWithMine(content, state, {400, 0, 0}));
        const std::array<sim::Money, 4> figures = {mine.dailyOpex, mine.operatingOpex,
                                                   mine.dailyMargin, mine.breakEvenPrice};
        CHECK_FALSE(mine.hasEarnings);
        CHECK(std::ranges::all_of(figures, [](sim::Money m) { return m == sim::Money::zero(); }));
    }
}

namespace {

// A hand-built view for the rival, long past any grace period: one mine in
// `state` with the figures given, every market at base price and every ore
// quoted at 8,000 a day. The mine's output is whatever the test says, so a
// test can make the tonnage and the margin disagree.
sim::CompanyView mineOwnerView(const sim::Content& content, sim::MineState state, sim::Money margin,
                               bool hasEarnings, std::int64_t ironOutput) {
    sim::CompanyView view;
    view.me = kRival;
    view.day = 1'000;
    view.cash = sim::Money::fromUnits(1'000'000);
    view.inventoryTonnes.assign(content.resources.size(), 0);
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        sim::MarketView market;
        market.resource = sim::ResourceId(r);
        market.price = content.resources[r].basePrice;
        market.basePrice = content.resources[r].basePrice;
        market.baseDemandTonnesPerDay = content.resources[r].baseDemandTonnesPerDay;
        view.markets.push_back(market);
        view.mineOpexQuotes.push_back(sim::Money::fromUnits(8'000));
    }
    view.companies.resize(2);
    sim::MineView mine;
    mine.id = sim::MineId(0);
    mine.plot = sim::PlotId(0);
    mine.state = state;
    mine.operationalDay = 100;
    mine.lastOutputTonnes.assign(content.resources.size(), 0);
    mine.lastOutputTonnes[kIron.index()] = ironOutput;
    mine.operatingOpex = sim::Money::fromUnits(8'000);
    mine.dailyOpex = mine.operatingOpex;
    mine.dailyMargin = margin;
    mine.hasEarnings = hasEarnings;
    view.mines.push_back(mine);
    return view;
}

// The run flag of the order the AI gives its one mine: 0 to idle it, 1 to
// reopen it, nothing when it leaves the mine alone.
std::optional<std::uint8_t> mineOrder(const sim::Content& content, const sim::CompanyView& view) {
    const std::vector<sim::Command> out = decideFor(content, view);
    std::optional<std::uint8_t> order;
    for (const sim::Command& command : out) {
        if (const auto* set = std::get_if<sim::SetMineState>(&command)) {
            REQUIRE_FALSE(order.has_value());
            order = set->operate;
        }
    }
    return order;
}

}  // namespace

TEST_CASE("The AI idles a mine on the view's margin, not on its own sums", "[ai][spec008]") {
    const sim::Content content = test::makeContent();
    const sim::Money cent = sim::Money::fromCents(1);
    // 400 t of iron at $60 would look like $24,000 against $8,000, but the
    // sim says the mine loses money: it is idled.
    CHECK(mineOrder(content, mineOwnerView(content, sim::MineState::Operating,
                                           sim::Money::zero() - cent, true, 400)) == 0);
    // No output on record, but the sim says it earns: it keeps running.
    CHECK_FALSE(mineOrder(content, mineOwnerView(content, sim::MineState::Operating, cent, true, 0))
                    .has_value());
    CHECK_FALSE(mineOrder(content, mineOwnerView(content, sim::MineState::Operating,
                                                 sim::Money::zero(), true, 0))
                    .has_value());
}

TEST_CASE("The AI idles a running mine that has produced nothing", "[ai][spec008]") {
    // Past the grace period with no earnings to show (an empty plot, or ore
    // the company cannot dig yet): the mine only costs money.
    const sim::Content content = test::makeContent();
    CHECK(mineOrder(content, mineOwnerView(content, sim::MineState::Operating, sim::Money::zero(),
                                           false, 0)) == 0);
    // Still inside the grace period it is left alone.
    sim::CompanyView young =
        mineOwnerView(content, sim::MineState::Operating, sim::Money::zero(), false, 0);
    young.mines[0].operationalDay = young.day;
    CHECK_FALSE(mineOrder(content, young).has_value());
}

TEST_CASE("The AI reopens a mine when the view's margin clears its own cost by the knob",
          "[ai][spec008]") {
    // Reopen at revenue of 1.25 x cost: a margin of a quarter of the cost of
    // running this mine, 2,000 on 8,000.
    const sim::Content content = test::makeContent();
    const sim::Money threshold = sim::Money::fromUnits(2'000);
    const sim::Money cent = sim::Money::fromCents(1);
    CHECK(mineOrder(content,
                    mineOwnerView(content, sim::MineState::Mothballed, threshold, true, 0)) == 1);
    CHECK_FALSE(mineOrder(content, mineOwnerView(content, sim::MineState::Mothballed,
                                                 threshold - cent, true, 400))
                    .has_value());

    // The cost is this mine's, not the company-wide quote: a dearer mine
    // needs a quarter of 20,000.
    sim::CompanyView dear =
        mineOwnerView(content, sim::MineState::Mothballed, sim::Money::fromUnits(4'999), true, 0);
    dear.mines[0].operatingOpex = sim::Money::fromUnits(20'000);
    CHECK_FALSE(mineOrder(content, dear).has_value());
    dear.mines[0].dailyMargin = sim::Money::fromUnits(5'000);
    CHECK(mineOrder(content, dear) == 1);

    // A mothballed mine with no earnings figure stays shut.
    CHECK_FALSE(mineOrder(content, mineOwnerView(content, sim::MineState::Mothballed,
                                                 sim::Money::fromUnits(50'000), false, 0))
                    .has_value());
}

TEST_CASE("The AI values a prospect at the running cost of the ores its survey reports",
          "[ai][spec008]") {
    // 200,000 t of iron, worth buying at 8,000 a day (see the test above).
    sim::Content content = test::makeContent();
    content.ai.recoverableShare = sim::Fixed::one();
    content.ai.firstMineRecoverableShare = sim::Fixed::one();
    content.ai.requiredReturn = sim::Fixed::one();
    content.ai.firstMineRequiredReturn = sim::Fixed::one();
    constexpr std::int64_t kTonnes = 200'000;
    const sim::Money dear = sim::Money::fromUnits(30'000);
    REQUIRE(bids(content, prospectView(content, kTonnes, false)));

    // Iron mines cost more than 400 t of iron fetches: not worth buying.
    sim::CompanyView dearIron = prospectView(content, kTonnes, false);
    dearIron.mineOpexQuotes[kIron.index()] = dear;
    CHECK_FALSE(bids(content, dearIron));

    // An ore the survey does not report costs this mine nothing.
    sim::CompanyView dearCopper = prospectView(content, kTonnes, false);
    dearCopper.mineOpexQuotes[kCopper.index()] = dear;
    CHECK(bids(content, dearCopper));

    // Nor does an ore it reports but the company cannot dig.
    sim::CompanyView lockedLithium = prospectView(content, kTonnes, false);
    lockedLithium.mineOpexQuotes[kLithium.index()] = dear;
    lockedLithium.surveys[0].estimates.push_back(
        {kLithium, 1'000, sim::Fixed::fromRatio(1, 2), false});
    CHECK(bids(content, lockedLithium));

    // A survey reporting two ores is costed at the dearer of them.
    sim::CompanyView both = prospectView(content, kTonnes, false);
    both.mineOpexQuotes[kCopper.index()] = dear;
    both.surveys[0].estimates.push_back({kCopper, 60, sim::Fixed::fromRatio(1, 2), false});
    CHECK_FALSE(bids(content, both));
}

// ---------------------------------------------------------------------------
// The race for the program (spec 010, BR-011)
// ---------------------------------------------------------------------------

namespace {

const sim::TechId kSeismic(0);         // the fixture's one ordinary project
const sim::TechId kLithiumProject(1);  // unlocks lithium; needs kSeismic

// Puts the view to work on stage `index` of the fixture's program with
// nothing delivered and the stage's project in the state given. The stages
// before it, and their projects, are complete.
void workOnStage(const sim::Content& content, sim::CompanyView& view, std::uint32_t index,
                 sim::ProgramResearchState research) {
    const sim::ProgramStage& def = content.program.stages[index];
    view.deadlineDay = content.program.deadlineDay;
    view.program.stagesCompleted = index;
    view.program.complete = false;
    sim::ProgramStageView stage;
    stage.index = index;
    stage.cost = def.cost;
    stage.research = def.research;
    stage.researchState = research;
    for (const sim::ProgramRequirement& requirement : def.requirements) {
        stage.requirements.push_back({requirement.resource, requirement.tonnes, 0,
                                      view.inventoryTonnes[requirement.resource.index()]});
    }
    view.program.stage = stage;
    for (std::uint32_t done = 0; done < index; ++done) {
        view.completedTechs.push_back(content.program.stages[done].research);
    }
    if (research == sim::ProgramResearchState::Done) {
        view.completedTechs.push_back(def.research);
    }
}

// Tonnes in stock, on the view and on the stage's requirement alike.
void setStock(sim::CompanyView& view, sim::ResourceId resource, std::int64_t tonnes) {
    view.inventoryTonnes[resource.index()] = tonnes;
    for (sim::ProgramRequirementView& requirement : view.program.stage->requirements) {
        if (requirement.resource == resource) {
            requirement.stockTonnes = tonnes;
        }
    }
}

// Tonnes of the stage's `position`th good already delivered, with the
// stage's progress to match (the mean share over its goods, as the sim has it).
void setDelivered(sim::CompanyView& view, std::size_t position, std::int64_t tonnes) {
    sim::ProgramStageView& stage = *view.program.stage;
    stage.requirements[position].deliveredTonnes = tonnes;
    std::int64_t shares = 0;
    for (const sim::ProgramRequirementView& requirement : stage.requirements) {
        shares += requirement.deliveredTonnes * 1000 / requirement.neededTonnes;
    }
    stage.progressPermille = shares / static_cast<std::int64_t>(stage.requirements.size());
}

// A company with one profitable iron mine and cash to spare, at work on
// stage `index`.
sim::CompanyView racerView(const sim::Content& content, std::uint32_t index,
                           sim::ProgramResearchState research) {
    sim::CompanyView view =
        mineOwnerView(content, sim::MineState::Operating, sim::Money::fromUnits(10'000), true, 400);
    view.cash = sim::Money::fromUnits(20'000'000);  // for anything; tests of cash set their own
    workOnStage(content, view, index, research);
    return view;
}

std::vector<sim::Command> ordersFor(const sim::Content& content, const sim::CompanyView& view) {
    return decideFor(content, view);
}

// Tonnes of `resource` in the orders of type Order (SellOre or
// DeliverToProgram); zero when there is none.
template <typename Order>
std::int64_t tonnesOrdered(const std::vector<sim::Command>& orders, sim::ResourceId resource) {
    std::int64_t tonnes = 0;
    for (const sim::Command& command : orders) {
        if (const auto* order = std::get_if<Order>(&command)) {
            if (order->resource == resource) {
                tonnes += order->tonnes;
            }
        }
    }
    return tonnes;
}

template <typename Order>
std::ptrdiff_t positionOf(const std::vector<sim::Command>& orders) {
    const auto found = std::find_if(orders.begin(), orders.end(), [](const sim::Command& command) {
        return std::holds_alternative<Order>(command);
    });
    return found == orders.end() ? -1 : found - orders.begin();
}

// The project the AI starts today; nothing when it starts none.
std::optional<sim::TechId> researchStarted(const sim::Content& content,
                                           const sim::CompanyView& view) {
    std::optional<sim::TechId> started;
    for (const sim::Command& command : ordersFor(content, view)) {
        if (const auto* start = std::get_if<sim::StartResearch>(&command)) {
            REQUIRE_FALSE(started.has_value());
            started = start->tech;
        }
    }
    return started;
}

}  // namespace

TEST_CASE("The AI delivers what its stage needs and sells only the rest", "[ai][spec010]") {
    // Stage 1 of the fixture needs 1,000 t of iron. Iron is at base price,
    // which the AI's own rule would sell at.
    const sim::Content content = test::makeContent();
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    setStock(view, kIron, 1'500);
    view.inventoryTonnes[kCopper.index()] = 40;  // not needed by stage 1

    std::vector<sim::Command> orders = ordersFor(content, view);
    CHECK(tonnesOrdered<sim::DeliverToProgram>(orders, kIron) == 1'000);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 500);
    CHECK(tonnesOrdered<sim::DeliverToProgram>(orders, kCopper) == 0);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kCopper) == 40);
    // Orders are applied as placed: the delivery must come before the sale.
    CHECK(positionOf<sim::DeliverToProgram>(orders) < positionOf<sim::SellOre>(orders));

    // Less in stock than the stage needs: all of it is kept, none sold.
    setStock(view, kIron, 300);
    orders = ordersFor(content, view);
    CHECK(tonnesOrdered<sim::DeliverToProgram>(orders, kIron) == 300);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 0);

    // Only what is still needed is delivered.
    setStock(view, kIron, 1'500);
    setDelivered(view, 0, 900);
    orders = ordersFor(content, view);
    CHECK(tonnesOrdered<sim::DeliverToProgram>(orders, kIron) == 100);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 1'400);

    // Nothing more is needed: no order at all for the program.
    setDelivered(view, 0, 1'000);
    orders = ordersFor(content, view);
    CHECK(positionOf<sim::DeliverToProgram>(orders) == -1);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 1'500);
}

TEST_CASE("The reserve share sets how much of a needed good is kept from sale", "[ai][spec010]") {
    sim::Content content = test::makeContent();
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    setStock(view, kIron, 600);

    content.ai.programReserveShare = sim::Fixed::fromRatio(1, 2);
    std::vector<sim::Command> orders = ordersFor(content, view);
    CHECK(tonnesOrdered<sim::DeliverToProgram>(orders, kIron) == 300);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 300);

    // Nothing kept: it sells as if there were no program.
    content.ai.programReserveShare = sim::Fixed::zero();
    orders = ordersFor(content, view);
    CHECK(positionOf<sim::DeliverToProgram>(orders) == -1);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 600);

    // The share is of the stock, not of the need: the last tonne a stage
    // needs is delivered like any other.
    content.ai.programReserveShare = sim::Fixed::fromRatio(1, 2);
    setDelivered(view, 0, 999);
    orders = ordersFor(content, view);
    CHECK(tonnesOrdered<sim::DeliverToProgram>(orders, kIron) == 1);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 599);

    // A share of the stock that rounds to less than a tonne still delivers
    // one: rounded away, the tonne would be sold and the stage never fed.
    setStock(view, kIron, 1);
    orders = ordersFor(content, view);
    CHECK(tonnesOrdered<sim::DeliverToProgram>(orders, kIron) == 1);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 0);

    // With no stock there is nothing to deliver, and with nothing kept the
    // one tonne is sold like the rest.
    setStock(view, kIron, 0);
    CHECK(positionOf<sim::DeliverToProgram>(ordersFor(content, view)) == -1);
    setStock(view, kIron, 1);
    content.ai.programReserveShare = sim::Fixed::zero();
    orders = ordersFor(content, view);
    CHECK(positionOf<sim::DeliverToProgram>(orders) == -1);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 1);
}

TEST_CASE("A good that arrives a tonne a day still reaches the program", "[ai][spec010]") {
    // The shipped share is a half. Half of one tonne rounds to none, and
    // what is not delivered is sold the same day, so a slow ore would never
    // be delivered and its stage never finished.
    sim::Content content = test::makeContent();
    content.ai.programReserveShare = sim::Fixed::fromRatio(1, 2);
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    constexpr std::int64_t kDays = 25;
    std::int64_t delivered = 0;
    std::int64_t sold = 0;
    for (std::int64_t day = 0; day < kDays; ++day) {
        setStock(view, kIron, 1);  // the day's output; yesterday's has gone
        const std::vector<sim::Command> orders = ordersFor(content, view);
        delivered += tonnesOrdered<sim::DeliverToProgram>(orders, kIron);
        sold += tonnesOrdered<sim::SellOre>(orders, kIron);
        setDelivered(view, 0, delivered);
    }
    CHECK(delivered == kDays);
    CHECK(sold == 0);
    CHECK(view.program.stage->progressPermille == kDays);  // of the 1,000 t stage 1 needs
}

namespace {

// The standing sell order the AI sets for a resource today; nothing when it
// leaves the policy alone.
std::optional<sim::SetSellPolicy> policySet(const std::vector<sim::Command>& orders,
                                            sim::ResourceId resource) {
    std::optional<sim::SetSellPolicy> found;
    for (const sim::Command& command : orders) {
        if (const auto* set = std::get_if<sim::SetSellPolicy>(&command)) {
            if (set->resource == resource) {
                REQUIRE_FALSE(found.has_value());
                found = *set;
            }
        }
    }
    return found;
}

}  // namespace

TEST_CASE("The AI makes a standing sell order keep what its stage needs", "[ai][spec010]") {
    // A company with standing sell orders (the player's defaults, when the
    // AI plays that company) sells its stock the day it is dug, before the
    // AI can deliver it. The order's reserve is what stops that.
    sim::Content content = test::makeContent();
    const sim::Money floor = sim::Money::fromUnits(7);
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    view.sellPolicies.assign(content.resources.size(), sim::SellPolicy{1, 0, floor});

    const std::vector<sim::Command> orders = ordersFor(content, view);
    const std::optional<sim::SetSellPolicy> iron = policySet(orders, kIron);
    REQUIRE(iron.has_value());
    CHECK(iron->enabled == 1);
    CHECK(iron->reserveTonnes == 1'000);
    CHECK(iron->floorPrice == floor);
    // Copper is not needed by stage 1: its order is left as it is.
    CHECK_FALSE(policySet(orders, kCopper).has_value());

    // A reserve that already covers the need is left alone: no daily orders.
    view.sellPolicies[kIron.index()].reserveTonnes = 1'000;
    setDelivered(view, 0, 400);
    CHECK_FALSE(policySet(ordersFor(content, view), kIron).has_value());

    // The need met: the reserve is released.
    setDelivered(view, 0, 1'000);
    const std::optional<sim::SetSellPolicy> released = policySet(ordersFor(content, view), kIron);
    REQUIRE(released.has_value());
    CHECK(released->enabled == 1);
    CHECK(released->reserveTonnes == 0);
    CHECK(released->floorPrice == floor);

    // A standing order that is switched off sells nothing: nothing to set.
    setDelivered(view, 0, 0);
    view.sellPolicies[kIron.index()] = sim::SellPolicy{0, 0, floor};
    CHECK_FALSE(policySet(ordersFor(content, view), kIron).has_value());

    // With nothing kept for the program there is nothing to reserve.
    view.sellPolicies[kIron.index()] = sim::SellPolicy{1, 0, floor};
    content.ai.programReserveShare = sim::Fixed::zero();
    CHECK_FALSE(policySet(ordersFor(content, view), kIron).has_value());
}

TEST_CASE("The AI races as well from a company with standing sell orders", "[ai][spec010]") {
    // Company 0 is the player's and starts with the default standing orders;
    // here they sell everything, every day. The runner's sweeps play it
    // with the AI.
    sim::Content content = test::makeContent();
    content.balance.defaultSellPolicy = sim::SellPolicy{1, 0, sim::Money::zero()};
    const sim::CompanyId player(0);
    std::uint32_t stages = 0;
    for (sim::Seed seed = 1; seed <= 3; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        REQUIRE(simulation.view(player).sellPolicies[kIron.index()].enabled == 1);
        sim::HeuristicController ai(content);
        for (sim::Day d = 0; d < sim::kDaysPerYear * 20; ++d) {
            simulation.collectCommands(player, ai);
            for (const sim::Notice& n : simulation.tick().notices) {
                CHECK(n.kind != sim::NoticeKind::CommandRejected);
            }
        }
        stages += simulation.state().companies[0].programStage;
    }
    CHECK(stages >= 3);
}

TEST_CASE("A company short of cash sells what its stage needs", "[ai][spec010]") {
    // Every ore unlocked and nothing delivered, so the program wants no
    // project yet. 1,000,000 in the bank less the 500,000 never committed
    // leaves exactly the fixture's buffer: enough to deliver. A cent less
    // and it sells.
    sim::Content content = test::makeContent();
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    view.cash = sim::Money::fromUnits(1'000'000);
    view.completedTechs = {kSeismic, kLithiumProject};
    setStock(view, kIron, 600);
    CHECK(tonnesOrdered<sim::DeliverToProgram>(ordersFor(content, view), kIron) == 600);

    content.ai.programCashBuffer = sim::Money::fromUnits(500'000) + sim::Money::fromCents(1);
    const std::vector<sim::Command> orders = ordersFor(content, view);
    CHECK(positionOf<sim::DeliverToProgram>(orders) == -1);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 600);
}

TEST_CASE("A company saving for a program project sells until it can pay for it", "[ai][spec010]") {
    // The program wants seismic next (600,000, on the way to lithium). A
    // company that went on delivering whenever it had its buffer would sit
    // at the buffer for good and never start the project.
    const sim::Content content = test::makeContent();
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    setStock(view, kIron, 600);
    // Spendable: 1,063,999, which the 36,000 the sale brings in leaves a
    // unit short of the buffer plus the project.
    view.cash = sim::Money::fromUnits(1'563'999);
    std::vector<sim::Command> orders = ordersFor(content, view);
    CHECK(positionOf<sim::DeliverToProgram>(orders) == -1);
    CHECK(positionOf<sim::StartResearch>(orders) == -1);
    CHECK(tonnesOrdered<sim::SellOre>(orders, kIron) == 600);

    // With the unit, the project starts and the stock is delivered.
    view.cash = sim::Money::fromUnits(1'600'000);
    orders = ordersFor(content, view);
    CHECK(tonnesOrdered<sim::DeliverToProgram>(orders, kIron) == 600);
    CHECK(positionOf<sim::StartResearch>(orders) != -1);

    // While the lab is busy there is nothing to save for.
    view.cash = sim::Money::fromUnits(1'000'000);
    view.activeResearch = sim::ResearchView{kSeismic, 990, 1'080};
    CHECK(tonnesOrdered<sim::DeliverToProgram>(ordersFor(content, view), kIron) == 600);
}

TEST_CASE("The AI completes a stage the day it is ready", "[ai][spec010]") {
    const sim::Content content = test::makeContent();
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::Done);
    setDelivered(view, 0, 1'000);
    CHECK(positionOf<sim::CompleteProgramStage>(ordersFor(content, view)) == -1);

    // First in the day's orders, so nothing placed with it can take its cash.
    view.program.stage->ready = true;
    CHECK(positionOf<sim::CompleteProgramStage>(ordersFor(content, view)) == 0);
}

TEST_CASE("The AI gives no other order on the day it founds the program", "[ai][spec010]") {
    // Completing the last stage ends the game, and the sim refuses every
    // order queued behind the one that does.
    const sim::Content content = test::makeContent();
    sim::CompanyView view = racerView(content, 2, sim::ProgramResearchState::Done);
    view.inventoryTonnes[kCopper.index()] = 40;  // would be sold on any other day
    view.program.stage->ready = true;
    const std::vector<sim::Command> orders = ordersFor(content, view);
    REQUIRE(orders.size() == 1);
    CHECK(std::holds_alternative<sim::CompleteProgramStage>(orders[0]));

    // An earlier stage does not end the game: the day's other orders stand.
    sim::CompanyView earlier = racerView(content, 0, sim::ProgramResearchState::Done);
    earlier.inventoryTonnes[kCopper.index()] = 40;
    earlier.program.stage->ready = true;
    CHECK(tonnesOrdered<sim::SellOre>(ordersFor(content, earlier), kCopper) == 40);
}

TEST_CASE("The AI saves for a stage whose goods and project are in", "[ai][spec010]") {
    // A plot worth bidding on and 20,000,000 in the bank, but the stage
    // costs 19,000,000 and can not be completed yet (cash is reserved
    // elsewhere, say): the money is kept for the stage.
    sim::Content content = test::makeContent();
    content.program.stages[0].cost = sim::Money::fromUnits(19'000'000);
    sim::CompanyView view = prospectView(content, 400'000, false);
    workOnStage(content, view, 0, sim::ProgramResearchState::Done);
    REQUIRE(bids(content, view));

    setDelivered(view, 0, 1'000);
    CHECK_FALSE(bids(content, view));

    // Until its project is done the money is still free: the project has
    // to be paid for first, and the company goes on growing while it runs.
    view.completedTechs.clear();
    view.program.stage->researchState = sim::ProgramResearchState::InProgress;
    CHECK(bids(content, view));
    view.program.stage->researchState = sim::ProgramResearchState::NotStarted;
    CHECK(bids(content, view));
}

TEST_CASE("The AI gives no orders once the game has ended", "[ai][spec010]") {
    const sim::Content content = test::makeContent();
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    setStock(view, kIron, 600);
    REQUIRE_FALSE(ordersFor(content, view).empty());
    view.outcome.ended = true;
    CHECK(ordersFor(content, view).empty());
}

TEST_CASE("The AI starts its stage's project once half the deliveries are in", "[ai][spec010]") {
    // Stage 1's project costs 5,000,000; the ordinary rule would pick the
    // cheapest project (seismic, 600,000) and needs cash clear of debt.
    sim::Content content = test::makeContent();
    const sim::TechId project = content.program.stages[0].research;
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    view.cash = sim::Money::fromUnits(6'000'000);
    setDelivered(view, 0, 500);
    CHECK(researchStarted(content, view) == project);

    // A loan does not stop it, as it would an ordinary project.
    view.outstandingPrincipal = sim::Money::fromUnits(5'000'000);
    CHECK(researchStarted(content, view) == project);
    view.outstandingPrincipal = sim::Money::zero();

    // Short of half, the stage's project waits: the road to lithium, which
    // stage 3 needs, comes first.
    setDelivered(view, 0, 499);
    CHECK(researchStarted(content, view) == kSeismic);

    // The knob moves the point.
    content.ai.programResearchProgress = sim::Fixed::fromRatio(499, 1000);
    CHECK(researchStarted(content, view) == project);

    // Nothing is started while research is under way.
    view.activeResearch = sim::ResearchView{kSeismic, 990, 1'080};
    CHECK_FALSE(researchStarted(content, view).has_value());
}

TEST_CASE("The AI starts a program project only with its cash buffer left over", "[ai][spec010]") {
    // 5,999,999 less the 500,000 never committed is a unit short of the
    // project (5,000,000) plus the buffer (500,000). It waits rather than
    // spend the money on another project.
    sim::Content content = test::makeContent();
    const sim::TechId project = content.program.stages[0].research;
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    setDelivered(view, 0, 500);
    view.cash = sim::Money::fromUnits(5'999'999);
    CHECK_FALSE(researchStarted(content, view).has_value());

    content.ai.programCashBuffer = sim::Money::fromUnits(499'999);
    CHECK(researchStarted(content, view) == project);
}

TEST_CASE("The AI heads for the research that unlocks an ore a stage needs", "[ai][spec010]") {
    // Stages 1 and 2 complete; stage 3 needs lithium, which is locked behind
    // a project that itself needs seismic.
    const sim::Content content = test::makeContent();
    sim::CompanyView view = racerView(content, 2, sim::ProgramResearchState::NotStarted);
    view.cash = sim::Money::fromUnits(20'000'000);
    CHECK(researchStarted(content, view) == kSeismic);

    view.completedTechs.push_back(kSeismic);
    CHECK(researchStarted(content, view) == kLithiumProject);

    // Even with the iron in (half the stage's deliveries): without lithium
    // the stage cannot be finished, so its unlock still comes first.
    setDelivered(view, 0, 3'000);
    REQUIRE(view.program.stage->progressPermille == 500);
    CHECK(researchStarted(content, view) == kLithiumProject);

    // Lithium unlocked: now the stage's own project.
    view.completedTechs.push_back(kLithiumProject);
    CHECK(researchStarted(content, view) == content.program.stages[2].research);
}

TEST_CASE("The AI heads for a coming stage's ore ahead of ordinary projects", "[ai][spec010]") {
    // On stage 1, with a cheap ordinary project on offer: the cheapest-first
    // rule would take it, but lithium (stage 3) is two projects away.
    sim::Content content = test::makeContent();
    sim::TechDef cheap;
    cheap.id = "cheap";
    cheap.cost = sim::Money::fromUnits(100'000);
    cheap.durationDays = 30;
    content.techs.push_back(cheap);
    const sim::TechId kCheap(static_cast<std::uint32_t>(content.techs.size() - 1));
    sim::CompanyView view = racerView(content, 0, sim::ProgramResearchState::NotStarted);
    view.cash = sim::Money::fromUnits(20'000'000);
    CHECK(researchStarted(content, view) == kSeismic);
    view.completedTechs.push_back(kSeismic);
    CHECK(researchStarted(content, view) == kLithiumProject);

    // With every needed ore unlocked and deliveries short of half, ordinary
    // research goes on as before.
    view.completedTechs.push_back(kLithiumProject);
    CHECK(researchStarted(content, view) == kCheap);
}

TEST_CASE("Land with an ore the program needs is worth more to the AI", "[ai][spec010]") {
    // 60,000 t of iron: 34 a tonne after running cost, 2,040,000 against a
    // 3,000,000 outlay. At a weight of 1.5 the iron counts for 90 a tonne,
    // 64 after cost: 3,840,000.
    sim::Content content = test::makeContent();
    content.ai.recoverableShare = sim::Fixed::one();
    content.ai.firstMineRecoverableShare = sim::Fixed::one();
    content.ai.requiredReturn = sim::Fixed::one();
    content.ai.firstMineRequiredReturn = sim::Fixed::one();
    constexpr std::int64_t kTonnes = 60'000;
    // No program in the view: valued as before.
    CHECK_FALSE(bids(content, prospectView(content, kTonnes, false)));

    sim::CompanyView racing = prospectView(content, kTonnes, false);
    workOnStage(content, racing, 0, sim::ProgramResearchState::NotStarted);
    CHECK(bids(content, racing));

    // The knob: at a weight of one the ore is worth what it sells for.
    sim::Content plain = content;
    plain.ai.programOreLandWeight = sim::Fixed::one();
    CHECK_FALSE(bids(plain, racing));

    // A stage still to come counts too: stage 3 needs iron.
    sim::CompanyView later = prospectView(content, kTonnes, false);
    workOnStage(content, later, 1, sim::ProgramResearchState::NotStarted);
    setDelivered(later, 0, 2'000);
    CHECK(bids(content, later));

    // A mine already digging 400 t a day will supply the 6,000 t the stages
    // need long before the deadline: no bonus.
    sim::CompanyView supplied = prospectView(content, kTonnes, true);
    workOnStage(content, supplied, 0, sim::ProgramResearchState::NotStarted);
    supplied.mines[0].lastOutputTonnes[kIron.index()] = 400;
    CHECK_FALSE(bids(content, supplied));
    // The same mine with the deadline a week off cannot: the bonus is back.
    supplied.deadlineDay = supplied.day + 7;
    CHECK(bids(content, supplied));

    // Iron the program no longer needs earns nothing extra: the last stage,
    // with its iron delivered.
    sim::CompanyView met = prospectView(content, kTonnes, false);
    workOnStage(content, met, 2, sim::ProgramResearchState::NotStarted);
    setDelivered(met, 0, 3'000);
    CHECK_FALSE(bids(content, met));
}

TEST_CASE("The view offers an unlocking project only when its program stage is reached",
          "[ai][view][spec010]") {
    // The sim refuses a project whose program stage is not complete; the
    // view's "could be started now" flag must agree.
    sim::Content content = test::makeContent();
    content.techs[kLithiumProject.index()].requiresProgramStage = 1;
    sim::Simulation simulation(content, 1, test::setup());
    simulation.submit(sim::StartResearch{sim::CompanyId(0), kSeismic});
    simulation.tick(content.techs[kSeismic.index()].durationDays + 2);
    CHECK(simulation.view(sim::CompanyId(0)).unlockReady[kLithium.index()] == 0);

    sim::WorldState advanced = simulation.state();
    advanced.companies[0].programStage = 1;
    advanced.companies[0].programStageDays.assign(1, 1);
    advanced.companies[0].completedTechs.push_back(content.program.stages[0].research);
    REQUIRE(simulation.load(sim::saveGame(advanced, {}, sim::hashContent(content))));
    CHECK(simulation.view(sim::CompanyId(0)).unlockReady[kLithium.index()] == 1);
}

TEST_CASE("The AI races: it delivers, researches and completes stages in play", "[ai][spec010]") {
    // The fixture's program is small enough to make headway on in twenty
    // years. Every order the AI gives on the way must be accepted.
    const sim::Content content = test::makeContent();
    std::uint32_t stages = 0;
    for (sim::Seed seed = 1; seed <= 3; ++seed) {
        sim::Simulation simulation(content, seed, test::setup());
        sim::HeuristicController ai(content);
        std::uint32_t rejections = 0;
        playYears(simulation, ai, sim::kDaysPerYear * 20, rejections);
        CHECK(rejections == 0);
        stages += simulation.state().companies[1].programStage;
    }
    CHECK(stages >= 3);
}
