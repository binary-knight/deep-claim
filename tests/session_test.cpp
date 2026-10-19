#include "game/session.h"

#include "sim/ai_controller.h"
#include "sim/commands.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include "game/format.h"
#include "game/trace.h"
#include "test_content.h"
#include "test_strings.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

TEST_CASE("Money, day and tonnage formatting", "[session][format]") {
    CHECK(game::formatMoney(sim::Money::fromUnits(5'000'000)) == "$5.00M");
    CHECK(game::formatMoney(sim::Money::fromUnits(1'234'567)) == "$1.23M");
    CHECK(game::formatMoney(sim::Money::fromUnits(-450'000)) == "-$450k");
    CHECK(game::formatMoney(sim::Money::fromUnits(12'500)) == "$12.5k");
    CHECK(game::formatMoney(sim::Money::fromCents(1'234)) == "$12.34");
    CHECK(game::formatMoney(sim::Money::fromUnits(2'500'000'000LL)) == "$2.50B");
    CHECK(game::formatDay(0) == "Y1 M1 D1");
    CHECK(game::formatDay(sim::kDaysPerYear + 45) == "Y2 M2 D16");
    CHECK(game::formatTonnes(1'234'567) == "1,234,567 t");
    CHECK(game::formatRatio(sim::Fixed::fromRatio(85, 100)) == "x0.85");
}

TEST_CASE("A session advances with real time at the chosen speed", "[session]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, test::setup());
    CHECK(session.speed() == game::Speed::Paused);
    CHECK(session.advance(10.0) == 0);
    CHECK(session.day() == 0);

    session.setSpeed(game::Speed::Normal);
    CHECK(session.advance(0.5) == 0);
    CHECK(session.advance(0.5) == 1);
    CHECK(session.day() == 1);

    session.setSpeed(game::Speed::Fastest);
    CHECK(session.advance(1.0) == 20);
    CHECK(session.day() == 21);
    // A long stall is capped rather than replayed in one frame.
    CHECK(session.advance(100.0) == 40);

    session.togglePause();
    CHECK(session.speed() == game::Speed::Paused);
    session.stepOneDay();
    CHECK(session.day() == 62);
}

TEST_CASE("Player orders are bound to the player and show up in the log", "[session]") {
    sim::Content content = test::makeContent();
    test::addStrings(content);  // the log text comes from the shipped catalogue
    game::Session session(content, 1, test::setup());
    // Day-0 listings are already in the log.
    CHECK(std::any_of(session.log().begin(), session.log().end(), [](const game::LogEntry& e) {
        return e.notice.kind == sim::NoticeKind::PlotListed;
    }));
    sim::PlotId listed;
    for (const sim::PlotView& plot : session.view().plots) {
        if (plot.listing == sim::PlotListing::Listed) {
            listed = plot.id;
            break;
        }
    }
    REQUIRE(listed.valid());
    // Wrong company id on purpose: the session must rebind it to the player.
    session.submit(sim::CommissionSurvey{sim::CompanyId(1), listed});
    session.stepOneDay();
    CHECK(session.view().surveys.size() == 1);
    CHECK(session.view().cash == content.balance.startingCapital - content.balance.surveyCost);
    CHECK(std::any_of(session.log().begin(), session.log().end(), [](const game::LogEntry& e) {
        return e.notice.kind == sim::NoticeKind::SurveyStarted &&
               e.text.find("plot #") != std::string::npos && e.text.front() != '[';
    }));
}

TEST_CASE("Rivals act on their own and the session can be saved and loaded", "[session]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 3, test::setup());
    session.setSpeed(game::Speed::Fastest);
    for (int i = 0; i < 40; ++i) {
        session.advance(1.0);  // 800 days
    }
    const sim::CompanyView view = session.view();
    const bool rivalActive = view.companies[1].plotsOwned > 0 || view.companies[1].bankrupt;
    CHECK(rivalActive);

    game::Session other(content, 9, test::setup());
    REQUIRE(other.load(session.save()));
    CHECK(other.day() == session.day());
    CHECK(other.speed() == game::Speed::Paused);
    CHECK(other.log().size() == 1);
    CHECK(other.log()[0].text == "Game loaded");
    other.stepOneDay();
    session.stepOneDay();
    CHECK(other.stateHash() == session.stateHash());
}

TEST_CASE("The session stops once the player is bankrupt", "[session]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, test::setup());
    sim::WorldState broke;
    {
        // Build a bankrupt starting state through a parallel simulation.
        sim::Simulation twin(content, 1, test::setup());
        broke = twin.state();
    }
    broke.companies[0].cash = sim::Money::fromUnits(-50'000'000);
    REQUIRE(session.load(sim::saveGame(broke, {}, sim::hashContent(content))));
    game::AutoPause never;  // the insolvency warnings would otherwise stop the clock
    never.enabled.fill(false);
    session.setAutoPause(never);
    session.setSpeed(game::Speed::Fastest);
    for (int i = 0; i < 20; ++i) {
        session.advance(1.0);
    }
    CHECK(session.gameOver());
    const sim::Day stopped = session.day();
    CHECK(session.advance(5.0) == 0);
    CHECK(session.day() == stopped);
}

TEST_CASE("Money and tonnage parsers are strict and exact", "[session][format]") {
    sim::Money money;
    CHECK(game::parseMoney("1234567", money));
    CHECK(money == sim::Money::fromUnits(1'234'567));
    CHECK(game::parseMoney("1,234,567.89", money));
    CHECK(money == sim::Money::fromCents(123'456'789));
    CHECK(game::parseMoney("$1.2M", money));
    CHECK(money == sim::Money::fromUnits(1'200'000));
    CHECK(game::parseMoney("450k", money));
    CHECK(money == sim::Money::fromUnits(450'000));
    CHECK(game::parseMoney("2.5B", money));
    CHECK(money == sim::Money::fromUnits(2'500'000'000LL));
    CHECK(game::parseMoney("1.234567M", money));
    CHECK(money == sim::Money::fromUnits(1'234'567));
    CHECK(game::parseMoney("-2M", money));
    CHECK(money == sim::Money::fromUnits(-2'000'000));  // sign kept for the sim to reject
    CHECK_FALSE(game::parseMoney("1e6", money));
    CHECK_FALSE(game::parseMoney("1.2.3M", money));
    CHECK_FALSE(game::parseMoney("abc", money));
    CHECK_FALSE(game::parseMoney("", money));
    CHECK_FALSE(game::parseMoney("1.001", money));  // finer than a cent
    CHECK_FALSE(game::parseMoney("99999999999999999999", money));

    std::int64_t tonnes = 0;
    CHECK(game::parseTonnes("1,000", tonnes));
    CHECK(tonnes == 1000);
    CHECK(game::parseTonnes(" 42 ", tonnes));
    CHECK(tonnes == 42);
    CHECK(game::parseTonnes("-5", tonnes));
    CHECK(tonnes == -5);
    CHECK_FALSE(game::parseTonnes("10.9", tonnes));
    CHECK_FALSE(game::parseTonnes("", tonnes));
    CHECK_FALSE(game::parseTonnes("10t", tonnes));

    CHECK(game::exactMoney(sim::Money::fromCents(123'456'789)) == "1234567.89");
    CHECK(game::parseMoney(game::exactMoney(sim::Money::fromCents(123'456'789)), money));
    CHECK(money == sim::Money::fromCents(123'456'789));
}

// A price per tonne is a threshold the player sells against (spec 008, spec
// 010 BR-014): every dollar and cent, never abbreviated as formatMoney does.
TEST_CASE("An exact price writes every dollar and cent", "[session][format]") {
    SECTION("cents are always two digits") {
        CHECK(game::exactPrice(sim::Money::fromCents(4'120)) == "$41.20");
        CHECK(game::exactPrice(sim::Money::fromCents(5)) == "$0.05");
        CHECK(game::exactPrice(sim::Money::fromCents(99)) == "$0.99");
        CHECK(game::exactPrice(sim::Money::fromUnits(90)) == "$90.00");
    }
    SECTION("thousands are separated, from one thousand up") {
        CHECK(game::exactPrice(sim::Money::fromCents(99'999)) == "$999.99");
        CHECK(game::exactPrice(sim::Money::fromCents(100'000)) == "$1,000.00");
        CHECK(game::exactPrice(sim::Money::fromUnits(1'430)) == "$1,430.00");
        CHECK(game::exactPrice(sim::Money::fromCents(4'461'323)) == "$44,613.23");
        CHECK(game::exactPrice(sim::Money::fromCents(14'000'000)) == "$140,000.00");
    }
    SECTION("a million and above keeps every group") {
        CHECK(game::exactPrice(sim::Money::fromUnits(1'000'000)) == "$1,000,000.00");
        CHECK(game::exactPrice(sim::Money::fromCents(123'456'789)) == "$1,234,567.89");
        CHECK(game::exactPrice(sim::Money::fromCents(100'000'000'001LL)) == "$1,000,000,000.01");
    }
    SECTION("zero") {
        CHECK(game::exactPrice(sim::Money::zero()) == "$0.00");
    }
    SECTION("a negative amount has its sign before the dollar sign") {
        CHECK(game::exactPrice(sim::Money::fromCents(-5)) == "-$0.05");
        CHECK(game::exactPrice(sim::Money::fromUnits(-1'430)) == "-$1,430.00");
        CHECK(game::exactPrice(sim::Money::fromCents(-123'456'789)) == "-$1,234,567.89");
    }
    SECTION("where formatMoney abbreviates, the exact price does not") {
        CHECK(game::formatMoney(sim::Money::fromUnits(1'430)) == "$1.4k");
        CHECK(game::exactPrice(sim::Money::fromUnits(1'430)) == "$1,430.00");
    }
}

TEST_CASE("Auto-pause stops the clock on a configured notice and not on others", "[session]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, test::setup());
    sim::PlotId listed;
    for (const sim::PlotView& plot : session.view().plots) {
        if (plot.listing == sim::PlotListing::Listed) {
            listed = plot.id;
            break;
        }
    }
    REQUIRE(listed.valid());

    SECTION("survey complete pauses when enabled") {
        const game::AutoPause rules = game::AutoPause::defaults();
        CHECK(rules.isEnabled(game::PauseOn::SurveyComplete));  // on by default since spec 004
        session.setAutoPause(rules);
        session.submit(sim::CommissionSurvey{{}, listed});
        session.setSpeed(game::Speed::Fastest);
        int ticked = 0;
        for (int i = 0; i < 10 && session.speed() != game::Speed::Paused; ++i) {
            ticked += session.advance(1.0);
        }
        // Commissioned on day 0, processed on day 0, completes
        // surveyDurationDays later; the clock stops on that day exactly.
        CHECK(session.speed() == game::Speed::Paused);
        CHECK(session.day() == content.balance.surveyDurationDays + 1);
        CHECK(ticked == static_cast<int>(session.day()));
        REQUIRE_FALSE(session.stopsToday().empty());
        const sim::Notice why = session.stopsToday().front();
        CHECK(why.kind == sim::NoticeKind::SurveyComplete);
        // The reason stays while the clock is stopped, however often it is read.
        CHECK(session.stopsToday().size() == 1);
        CHECK(session.advance(5.0) == 0);
        CHECK(session.stopsToday().size() == 1);
        // Resuming clears it and does not re-pause on the same notice.
        session.setSpeed(game::Speed::Fastest);
        CHECK(session.stopsToday().empty());
        CHECK(session.advance(1.0) == 20);
    }
    SECTION("survey complete does not pause when switched off") {
        game::AutoPause rules = game::AutoPause::defaults();
        rules.set(game::PauseOn::SurveyComplete, false);
        session.setAutoPause(rules);
        session.submit(sim::CommissionSurvey{{}, listed});
        session.setSpeed(game::Speed::Fastest);
        for (int i = 0; i < 3; ++i) {
            session.advance(1.0);
        }
        CHECK(session.speed() == game::Speed::Fastest);
        CHECK(session.day() == 60);
        CHECK(session.stopsToday().empty());
    }
    SECTION("a rival's auction win is not the player's") {
        sim::Notice sold = sim::makeNotice(sim::NoticeKind::PlotSold, sim::CompanyId(1), listed);
        CHECK_FALSE(game::AutoPause::defaults().shouldPause(sold, game::Session::kPlayer));
        sold.company = game::Session::kPlayer;
        CHECK(game::AutoPause::defaults().shouldPause(sold, game::Session::kPlayer));
        const sim::Notice listedNotice =
            sim::makeNotice(sim::NoticeKind::PlotListed, sim::CompanyId(), listed);
        CHECK_FALSE(game::AutoPause::defaults().shouldPause(listedNotice, game::Session::kPlayer));
    }
}

TEST_CASE("Auto-pause rules round-trip through their text form", "[session]") {
    game::AutoPause rules = game::AutoPause::defaults();
    rules.set(game::PauseOn::Insolvency, false);
    rules.set(game::PauseOn::MineOperational, true);
    const std::string text = rules.toText();
    CHECK(text.find("insolvency=0\n") != std::string::npos);
    CHECK(text.find("mine_operational=1\n") != std::string::npos);
    CHECK(text.find("survey_complete=1\n") != std::string::npos);
    CHECK(text.find("research_complete=1\n") != std::string::npos);

    game::AutoPause restored = game::AutoPause::defaults();
    CHECK(restored.fromText(text));
    CHECK(restored.enabled == rules.enabled);

    // Unknown keys and comments are ignored; missing keys keep their value;
    // a malformed line is reported but the rest still applies.
    game::AutoPause partial = game::AutoPause::defaults();
    CHECK(partial.fromText("# settings\nfuture_key=1\nauction_lost=0\r\n"));
    CHECK_FALSE(partial.isEnabled(game::PauseOn::AuctionLost));
    CHECK(partial.isEnabled(game::PauseOn::Bankrupt));
    CHECK_FALSE(partial.fromText("bankrupt=maybe\nidle_mine=0\n"));
    CHECK(partial.isEnabled(game::PauseOn::Bankrupt));
    CHECK_FALSE(partial.isEnabled(game::PauseOn::IdleMine));
    CHECK(game::pauseOnKey(game::PauseOn::IdleMine) == "idle_mine");
}

TEST_CASE("Stepping a day onto a pausing notice stops a running clock", "[session]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, test::setup());
    sim::PlotId listed;
    for (const sim::PlotView& plot : session.view().plots) {
        if (plot.listing == sim::PlotListing::Listed) {
            listed = plot.id;
            break;
        }
    }
    REQUIRE(listed.valid());
    game::AutoPause rules;
    rules.enabled.fill(false);
    rules.set(game::PauseOn::SurveyComplete, true);
    session.setAutoPause(rules);
    session.submit(sim::CommissionSurvey{{}, listed});
    session.setSpeed(game::Speed::Normal);
    for (sim::Day i = 0; i < content.balance.surveyDurationDays; ++i) {
        session.stepOneDay();
        REQUIRE(session.speed() == game::Speed::Normal);
    }
    session.stepOneDay();  // the survey completes on this day
    CHECK(session.speed() == game::Speed::Paused);
    CHECK(session.advance(5.0) == 0);  // and the clock stays stopped
    REQUIRE_FALSE(session.stopsToday().empty());
    const sim::Notice why = session.stopsToday().front();
    CHECK(why.kind == sim::NoticeKind::SurveyComplete);
}

namespace {

sim::PlotId firstListedPlot(const game::Session& session) {
    for (const sim::PlotView& plot : session.view().plots) {
        if (plot.listing == sim::PlotListing::Listed) {
            return plot.id;
        }
    }
    return {};
}

int countKind(const game::Session& session, sim::NoticeKind kind) {
    return static_cast<int>(
        std::count_if(session.log().begin(), session.log().end(),
                      [&](const game::LogEntry& e) { return e.notice.kind == kind; }));
}

game::AutoPause neverPause() {
    game::AutoPause rules;
    rules.enabled.fill(false);
    return rules;
}

// A game with no rival, so an auction's outcome is the player's alone.
sim::GameSetup soloSetup() {
    sim::GameSetup setup = test::setup();
    setup.aiNames.clear();
    return setup;
}

// Edits the session's world through a save, as the sim tests do.
template <typename Edit>
void editWorld(game::Session& session, const sim::Content& content, const sim::GameSetup& setup,
               Edit edit) {
    sim::Simulation scratch(content, 1, setup);
    REQUIRE(scratch.load(session.save()));
    sim::WorldState edited = scratch.state();
    edit(edited);
    REQUIRE(session.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

}  // namespace

TEST_CASE("Auto-pause defaults: survey and research complete are on; old files keep their toggles",
          "[session]") {
    const game::AutoPause defaults = game::AutoPause::defaults();
    CHECK(defaults.isEnabled(game::PauseOn::SurveyComplete));
    CHECK(defaults.isEnabled(game::PauseOn::ResearchComplete));
    CHECK_FALSE(defaults.isEnabled(game::PauseOn::MineOperational));
    CHECK(game::pauseOnKey(game::PauseOn::ResearchComplete) == "research_complete");

    // A file written before spec 004: eight keys, no research_complete. The
    // toggles it records are respected; the reason it has never heard of
    // takes the new default.
    const std::string old =
        "insolvency=1\nbankrupt=1\nauction_won=0\nauction_lost=1\n"
        "survey_complete=0\nmine_operational=1\nmine_found_nothing=1\n"
        "idle_mine=1\n";
    game::AutoPause migrated = game::AutoPause::defaults();
    CHECK(migrated.fromText(old));
    CHECK(migrated.isEnabled(game::PauseOn::ResearchComplete));
    CHECK_FALSE(migrated.isEnabled(game::PauseOn::SurveyComplete));
    CHECK_FALSE(migrated.isEnabled(game::PauseOn::AuctionWon));
    CHECK(migrated.isEnabled(game::PauseOn::MineOperational));

    // A file that names neither gets both new defaults.
    game::AutoPause sparse = game::AutoPause::defaults();
    CHECK(sparse.fromText("insolvency=0\n"));
    CHECK(sparse.isEnabled(game::PauseOn::SurveyComplete));
    CHECK(sparse.isEnabled(game::PauseOn::ResearchComplete));

    // Only the player's own research stops the clock.
    sim::Notice done =
        sim::makeNotice(sim::NoticeKind::ResearchComplete, sim::CompanyId(1), sim::PlotId());
    CHECK_FALSE(defaults.shouldPause(done, game::Session::kPlayer));
    done.company = game::Session::kPlayer;
    CHECK(defaults.shouldPause(done, game::Session::kPlayer));
}

TEST_CASE("Finished research stops the clock under the defaults", "[session]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, test::setup());
    session.submit(sim::StartResearch{{}, sim::TechId(0)});
    session.setSpeed(game::Speed::Fastest);
    // A lab busy with other research puts the player behind in the race
    // (spec 011): that stop comes first, on day one.
    CHECK(session.advance(1.0) == 1);
    REQUIRE(session.stopsToday().size() == 1);
    CHECK(session.stopsToday().front().kind == sim::NoticeKind::ProgramFellBehind);
    session.setSpeed(game::Speed::Fastest);
    for (int i = 0; i < 100 && session.speed() != game::Speed::Paused; ++i) {
        session.advance(1.0);
    }
    CHECK(session.speed() == game::Speed::Paused);
    CHECK(session.day() == content.techs[0].durationDays + 1);
    REQUIRE_FALSE(session.stopsToday().empty());
    const sim::Notice why = session.stopsToday().front();
    CHECK(why.kind == sim::NoticeKind::ResearchComplete);
}

TEST_CASE("Relists reach the log only for plots the player surveyed or bid on", "[session]") {
    sim::Content content = test::makeContent();
    test::addStrings(content);
    const int days = static_cast<int>(content.balance.auctionWindowDays) + 5;

    SECTION("no stake: the relist is dropped from the log and the trace") {
        std::ostringstream out;
        game::TraceWriter trace(out);
        game::Session session(content, 1, test::setup());
        session.setTrace(&trace);
        session.setAutoPause(neverPause());
        for (int i = 0; i < days; ++i) {
            session.stepOneDay();
        }
        CHECK(countKind(session, sim::NoticeKind::PlotRelisted) == 0);
        CHECK(countKind(session, sim::NoticeKind::PlotListed) > 0);  // new land is still news
        CHECK(out.str().find("plot_relisted") == std::string::npos);
        CHECK(out.str().find("plot_listed") != std::string::npos);
    }
    SECTION("a survey makes that plot's relist news") {
        game::Session session(content, 1, test::setup());
        session.setAutoPause(neverPause());
        const sim::PlotId plot = firstListedPlot(session);
        REQUIRE(plot.valid());
        session.submit(sim::CommissionSurvey{{}, plot});
        for (int i = 0; i < days; ++i) {
            session.stepOneDay();
        }
        REQUIRE(countKind(session, sim::NoticeKind::PlotRelisted) == 1);
        for (const game::LogEntry& entry : session.log()) {
            if (entry.notice.kind == sim::NoticeKind::PlotRelisted) {
                CHECK(entry.notice.plot == plot);
                CHECK(entry.text.starts_with("Plot #"));  // capitalised
            }
        }
    }
    SECTION("a bid that cannot be paid makes that plot's relist news") {
        game::Session session(content, 1, soloSetup());
        session.setAutoPause(neverPause());
        const sim::PlotId plot = firstListedPlot(session);
        REQUIRE(plot.valid());
        const sim::Money ask = session.view().plots[plot.index()].askPrice;
        session.submit(sim::BidOnPlot{{}, plot, ask});
        session.stepOneDay();
        // Take the cash away before the close: the bid is forfeited and the
        // plot relists. The player never surveyed it; the bid is the stake,
        // and it is read from the view, so it survives the load.
        editWorld(session, content, soloSetup(), [](sim::WorldState& world) {
            world.companies[0].cash = sim::Money::fromUnits(1);
        });
        REQUIRE(session.view().surveys.empty());
        for (int i = 0; i < days; ++i) {
            session.stepOneDay();
        }
        CHECK(countKind(session, sim::NoticeKind::BidForfeited) == 1);
        bool relisted = false;
        for (const game::LogEntry& entry : session.log()) {
            if (entry.notice.kind == sim::NoticeKind::PlotRelisted) {
                CHECK(entry.notice.plot == plot);
                relisted = true;
            }
        }
        CHECK(relisted);
    }
}

TEST_CASE("A rival's unlock is in the player's log; the player's own is not", "[session]") {
    sim::Content content = test::makeContent();
    test::addStrings(content);
    game::Session session(content, 1, test::setup());
    session.setAutoPause(neverPause());
    session.submit(sim::StartResearch{{}, sim::TechId(0)});
    for (sim::Day d = 0; d <= content.techs[0].durationDays + 1; ++d) {
        session.stepOneDay();
    }
    session.submit(sim::StartResearch{{}, sim::TechId(1)});
    for (sim::Day d = 0; d <= content.techs[1].durationDays + 1; ++d) {
        session.stepOneDay();
    }
    REQUIRE(session.view().completedTechs.size() == 2);
    for (const game::LogEntry& entry : session.log()) {
        if (entry.notice.kind == sim::NoticeKind::RivalUnlockedResource) {
            CHECK(entry.notice.company != game::Session::kPlayer);
            CHECK(entry.text.find("Rival") != std::string::npos);
        }
    }
    CHECK(countKind(session, sim::NoticeKind::ResearchComplete) == 2);
}

TEST_CASE("A month's automatic sales are one log line", "[session][format]") {
    sim::Content content = test::makeContent();
    test::addStrings(content);

    // The line itself: one item per resource, joined with semicolons.
    sim::Notice iron =
        sim::makeNotice(sim::NoticeKind::AutoSaleSummary, game::Session::kPlayer, sim::PlotId());
    iron.resource = sim::ResourceId(0);
    iron.tonnes = 33'009;
    iron.amount = sim::Money::fromUnits(1'790'000);
    sim::Notice copper = iron;
    copper.resource = sim::ResourceId(1);
    copper.tonnes = 4'680;
    copper.amount = sim::Money::fromUnits(2'690'000);
    const std::vector<sim::Notice> both = {iron, copper};
    const std::string line = game::describeAutoSales(content, both);
    INFO(line);
    CHECK(line.find(game::formatTonnes(33'009) + " of " + content.resources[0].name +
                    " for $1.79M; " + game::formatTonnes(4'680) + " of " +
                    content.resources[1].name + " for $2.69M.") != std::string::npos);
    CHECK(line.find("..") == std::string::npos);
    CHECK(line.front() != '[');
    const std::string one = game::describeAutoSales(content, std::span(&iron, 1));
    CHECK(one.find(';') == std::string::npos);

    // In play: a mine selling two ores under standing orders logs one line a month.
    game::Session session(content, 1, soloSetup());
    session.setAutoPause(neverPause());
    std::ostringstream out;
    game::TraceWriter trace(out);
    session.setTrace(&trace);
    for (std::uint32_t r = 0; r < content.resources.size(); ++r) {
        sim::SetSellPolicy policy;
        policy.resource = sim::ResourceId(r);
        policy.enabled = 1;
        session.submit(policy);
    }
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    session.submit(sim::BidOnPlot{{}, plot, session.view().plots[plot.index()].askPrice});
    for (sim::Day d = 0; d <= content.balance.auctionWindowDays + 1; ++d) {
        session.stepOneDay();
    }
    REQUIRE(session.view().plots[plot.index()].owner == game::Session::kPlayer);
    // Iron and copper in the ground and the cash to dig them.
    editWorld(session, content, soloSetup(), [&](sim::WorldState& world) {
        world.companies[0].cash = sim::Money::fromUnits(50'000'000);
        auto& deposits = world.hidden.plotDeposits[plot.index()].deposits;
        deposits.clear();
        deposits.push_back({sim::ResourceId(0), 10'000'000'000, 10'000'000'000});
        deposits.push_back({sim::ResourceId(1), 10'000'000'000, 10'000'000'000});
    });
    session.submit(sim::BuildMine{{}, plot});
    for (sim::Day d = 0; d < content.balance.mineBuildDays + 4 * sim::kDaysPerMonth; ++d) {
        session.stepOneDay();
    }
    int lines = 0;
    int multi = 0;
    sim::Day lastDay = 0;
    for (const game::LogEntry& entry : session.log()) {
        if (entry.notice.kind != sim::NoticeKind::AutoSaleSummary) {
            continue;
        }
        ++lines;
        CHECK(entry.day != lastDay);  // never two on one day
        lastDay = entry.day;
        CHECK(entry.text.starts_with("Automatic sales this month: "));
        multi += entry.text.find(';') != std::string::npos ? 1 : 0;
    }
    CHECK(lines >= 3);
    CHECK(multi >= 1);
    // The trace records what the log shows: one notice line per summary.
    std::size_t traced = 0;
    for (std::size_t at = out.str().find("\"auto_sale_summary\""); at != std::string::npos;
         at = out.str().find("\"auto_sale_summary\"", at + 1)) {
        ++traced;
    }
    CHECK(traced == static_cast<std::size_t>(lines));
}

// ---- spec 007: orders wait in the session until the clock moves ----

namespace {

std::size_t countText(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + 1)) {
        ++count;
    }
    return count;
}

const std::string kCommandRecord = "\"t\":\"command\"";
const std::string kQueuedRecord = "\"kind\":\"order_queued\"";
const std::string kCancelledRecord = "\"kind\":\"order_cancelled\"";

// The trace's command records, in file order.
std::vector<game::TraceRecord> commandRecords(const std::string& trace) {
    std::vector<game::TraceRecord> records;
    std::istringstream in(trace);
    std::string line;
    while (std::getline(in, line)) {
        game::TraceRecord record;
        std::string error;
        REQUIRE(game::parseTraceLine(line, record, error));
        if (record.type == "command") {
            records.push_back(record);
        }
    }
    return records;
}

// One day of the game as it ran before spec 007: the player's orders go
// straight to the sim, then the rivals decide, then the day is ticked.
void tickDirect(sim::Simulation& simulation, sim::HeuristicController& ai,
                std::span<const sim::Command> orders) {
    for (sim::Command order : orders) {
        sim::setCommandCompany(order, game::Session::kPlayer);
        simulation.submit(order);
    }
    for (std::uint32_t c = 1; c < simulation.state().companies.size(); ++c) {
        simulation.collectCommands(sim::CompanyId(c), ai);
    }
    simulation.tick();
}

}  // namespace

TEST_CASE("An order waits in the session and reaches the sim on the next tick",
          "[session][pending]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, test::setup());
    CHECK(session.pendingOrders().empty());  // a new game has nothing waiting
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    const std::uint64_t before = session.stateHash();

    session.submit(sim::CommissionSurvey{sim::CompanyId(1), plot});
    REQUIRE(session.pendingOrders().size() == 1);
    const auto* queued = std::get_if<sim::CommissionSurvey>(&session.pendingOrders()[0].command);
    REQUIRE(queued != nullptr);
    CHECK(queued->company == game::Session::kPlayer);  // rebound to the player
    CHECK(queued->plot == plot);
    // The sim has not been told yet: nothing in its save, nothing in its state.
    CHECK(session.stateHash() == before);
    CHECK(session.view().surveys.empty());

    session.stepOneDay();
    CHECK(session.pendingOrders().empty());
    CHECK(session.view().surveys.size() == 1);
    CHECK(session.view().cash == content.balance.startingCapital - content.balance.surveyCost);
}

TEST_CASE("Pending orders are listed in the order given, each with its own id",
          "[session][pending]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, test::setup());
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    session.submit(sim::CommissionSurvey{{}, plot});
    session.submit(sim::TakeLoan{{}, content.balance.loanMinPrincipal});
    session.submit(sim::StartResearch{{}, sim::TechId(0)});

    const std::vector<game::PendingOrder> orders = session.pendingOrders();
    REQUIRE(orders.size() == 3);
    CHECK(std::holds_alternative<sim::CommissionSurvey>(orders[0].command));
    CHECK(std::holds_alternative<sim::TakeLoan>(orders[1].command));
    CHECK(std::holds_alternative<sim::StartResearch>(orders[2].command));
    CHECK(orders[0].id != orders[1].id);
    CHECK(orders[1].id != orders[2].id);
    CHECK(orders[0].id != orders[2].id);

    SECTION("cancel removes exactly the one named and the rest keep their ids") {
        CHECK(session.cancel(orders[1].id));
        REQUIRE(session.pendingOrders().size() == 2);
        CHECK(session.pendingOrders()[0].id == orders[0].id);
        CHECK(session.pendingOrders()[1].id == orders[2].id);
        CHECK_FALSE(session.cancel(orders[1].id));  // already gone
        CHECK(session.pendingOrders().size() == 2);

        // The cancelled loan never reaches the sim; the other two do.
        session.stepOneDay();
        CHECK(session.view().loans.empty());
        CHECK(session.view().surveys.size() == 1);
        CHECK(session.view().activeResearch.has_value());
        CHECK(session.view().cash ==
              content.balance.startingCapital - content.balance.surveyCost - content.techs[0].cost);
        CHECK(countKind(session, sim::NoticeKind::LoanTaken) == 0);
        CHECK(countKind(session, sim::NoticeKind::CommandRejected) == 0);
    }
    SECTION("an id is not handed out again after a cancel") {
        CHECK(session.cancel(orders[2].id));
        session.submit(sim::StartResearch{{}, sim::TechId(0)});
        REQUIRE(session.pendingOrders().size() == 3);
        for (const game::PendingOrder& earlier : orders) {
            CHECK(session.pendingOrders()[2].id != earlier.id);
        }
    }
    SECTION("cancel all removes every order and none is applied") {
        session.cancelAll();
        CHECK(session.pendingOrders().empty());
        const sim::Money cash = session.view().cash;
        session.stepOneDay();
        CHECK(session.view().cash == cash);
        CHECK(session.view().surveys.empty());
        CHECK(session.view().loans.empty());
        CHECK_FALSE(session.view().activeResearch.has_value());
        CHECK(countKind(session, sim::NoticeKind::CommandRejected) == 0);
    }
}

TEST_CASE("A game with no cancellations ticks to the same hashes as orders given directly",
          "[session][pending]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 7, test::setup());
    session.setAutoPause(neverPause());
    sim::Simulation direct(content, 7, test::setup());
    sim::HeuristicController ai(content);
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    const sim::Money ask = session.view().plots[plot.index()].askPrice;

    // Borrow, then bid with the borrowed money: the order given matters.
    const std::vector<sim::Command> dayZero = {
        sim::CommissionSurvey{{}, plot}, sim::TakeLoan{{}, sim::Money::fromUnits(1'000'000)},
        sim::BidOnPlot{{}, plot, ask}, sim::StartResearch{{}, sim::TechId(0)}};
    const std::vector<sim::Command> dayTen = {sim::BidOnPlot{{}, plot, ask + ask / 10},
                                              sim::SetCoverCosts{{}, 1}};
    for (sim::Day day = 0; day < 120; ++day) {
        std::span<const sim::Command> orders;
        if (day == 0) {
            orders = dayZero;
        } else if (day == 10) {
            orders = dayTen;
        }
        for (const sim::Command& order : orders) {
            session.submit(order);
        }
        session.stepOneDay();
        tickDirect(direct, ai, orders);
        REQUIRE(session.stateHash() == direct.stateHash());
    }
    CHECK(session.view().loans.size() == 1);  // the orders did reach the sim
}

TEST_CASE("Pending orders survive the clock controls and are dropped by a load",
          "[session][pending]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, test::setup());
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());

    SECTION("speed changes and pause toggles while stopped keep the queue") {
        session.submit(sim::StartResearch{{}, sim::TechId(0)});
        session.setSpeed(game::Speed::Paused);
        CHECK(session.pendingOrders().size() == 1);
        CHECK(session.advance(5.0) == 0);  // a stopped clock gives no orders
        CHECK(session.pendingOrders().size() == 1);
        session.setSpeed(game::Speed::Fast);
        CHECK(session.pendingOrders().size() == 1);
        session.togglePause();
        CHECK(session.pendingOrders().size() == 1);
        session.togglePause();  // running again, at normal speed
        CHECK(session.pendingOrders().size() == 1);
        CHECK(session.advance(0.5) == 0);  // running, but no whole day yet
        CHECK(session.pendingOrders().size() == 1);
        CHECK(session.advance(0.5) == 1);
        CHECK(session.pendingOrders().empty());
        CHECK(session.view().activeResearch.has_value());
    }
    SECTION("an auto-pause leaves orders given at the stop waiting") {
        session.submit(sim::CommissionSurvey{{}, plot});
        session.setSpeed(game::Speed::Fastest);
        for (int i = 0; i < 10 && session.speed() != game::Speed::Paused; ++i) {
            session.advance(1.0);
        }
        REQUIRE(session.speed() == game::Speed::Paused);  // survey complete
        session.submit(sim::StartResearch{{}, sim::TechId(0)});
        REQUIRE_FALSE(session.stopsToday().empty());
        CHECK(session.advance(5.0) == 0);
        REQUIRE(session.pendingOrders().size() == 1);
        CHECK(std::holds_alternative<sim::StartResearch>(session.pendingOrders()[0].command));
        CHECK_FALSE(session.view().activeResearch.has_value());
    }
    SECTION("an order given while the clock runs survives the auto-pause that follows") {
        game::AutoPause rules = neverPause();
        rules.set(game::PauseOn::SurveyComplete, true);
        session.setAutoPause(rules);
        session.submit(sim::CommissionSurvey{{}, plot});
        session.setSpeed(game::Speed::Normal);
        for (sim::Day i = 0; i <= content.balance.surveyDurationDays; ++i) {
            REQUIRE(session.speed() == game::Speed::Normal);
            if (i == content.balance.surveyDurationDays) {
                // Given during the day whose tick stops the clock: it goes to
                // the sim with that tick, as it did before spec 007.
                session.submit(sim::StartResearch{{}, sim::TechId(0)});
            }
            session.stepOneDay();
        }
        CHECK(session.speed() == game::Speed::Paused);
        CHECK(session.pendingOrders().empty());
        CHECK(session.view().activeResearch.has_value());
    }
    SECTION("a load discards the queue") {
        game::Session other(content, 9, test::setup());
        const std::vector<std::uint8_t> bytes = other.save();
        session.submit(sim::CommissionSurvey{{}, plot});
        session.submit(sim::StartResearch{{}, sim::TechId(0)});
        REQUIRE(session.load(bytes));
        CHECK(session.pendingOrders().empty());
        CHECK(session.committed() == sim::Money::zero());
        const sim::Money cash = session.view().cash;
        session.stepOneDay();
        CHECK(session.view().cash == cash);
        CHECK(session.view().surveys.empty());
        CHECK_FALSE(session.view().activeResearch.has_value());
    }
    SECTION("a save that does not load leaves the queue alone") {
        session.submit(sim::CommissionSurvey{{}, plot});
        const std::vector<std::uint8_t> rubbish = {1, 2, 3};
        CHECK_FALSE(session.load(rubbish));
        CHECK(session.pendingOrders().size() == 1);
    }
}

TEST_CASE("A save carries the pending orders and leaves them pending", "[session][pending]") {
    const sim::Content content = test::makeContent();
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 1, soloSetup());
    session.setTrace(&trace);
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    const sim::Money ask = session.view().plots[plot.index()].askPrice;
    const sim::Money raised = ask + sim::Money::fromUnits(10'000);
    const sim::Money principal = sim::Money::fromUnits(1'000'000);
    // A loan, a bid and its raise: the order they are given in decides what
    // stands.
    session.submit(sim::TakeLoan{{}, principal});
    session.submit(sim::BidOnPlot{{}, plot, ask});
    session.submit(sim::BidOnPlot{{}, plot, raised});
    const std::vector<game::PendingOrder> before = session.pendingOrders();
    const sim::Money committed = session.committed();
    REQUIRE(committed == ask + raised);
    REQUIRE(session.pendingBorrowed() == principal);
    const std::uint64_t hash = session.stateHash();
    const std::string traced = out.str();

    const std::vector<std::uint8_t> bytes = session.save();

    // The live game is as it was: the same orders wait, counted the same.
    REQUIRE(session.pendingOrders().size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(session.pendingOrders()[i].id == before[i].id);
        CHECK(game::encodeCommand(session.pendingOrders()[i].command) ==
              game::encodeCommand(before[i].command));
    }
    CHECK(session.committed() == committed);
    CHECK(session.pendingBorrowed() == principal);
    CHECK(session.stateHash() == hash);
    CHECK(out.str() == traced);  // the save wrote nothing, a command record least of all
    CHECK(commandRecords(out.str()).empty());

    SECTION("the saved bytes hold the orders, in the order given") {
        sim::Simulation saved(content, 9, soloSetup());
        REQUIRE(saved.load(bytes));
        REQUIRE(saved.pendingCommands().size() == before.size());
        for (std::size_t i = 0; i < before.size(); ++i) {
            CHECK(game::encodeCommand(saved.pendingCommands()[i]) ==
                  game::encodeCommand(before[i].command));
        }
    }
    SECTION("the loaded save and the live game tick to the same state") {
        game::Session other(content, 9, soloSetup());
        REQUIRE(other.load(bytes));
        CHECK(other.pendingOrders().empty());
        other.stepOneDay();
        session.stepOneDay();
        CHECK(other.stateHash() == session.stateHash());
        CHECK(other.view().reservedCash == raised);
        CHECK(session.view().reservedCash == raised);
        CHECK(session.view().loans.size() == 1);
        CHECK(session.pendingOrders().empty());
        // Given once, by the tick.
        CHECK(commandRecords(out.str()).size() == before.size());
        session.stepOneDay();
        CHECK(commandRecords(out.str()).size() == before.size());
    }
    SECTION("every order can still be cancelled, and then none is applied") {
        for (const game::PendingOrder& order : before) {
            CHECK(session.cancel(order.id));
        }
        CHECK(session.pendingOrders().empty());
        CHECK(session.committed() == sim::Money::zero());
        CHECK(countText(out.str(), kCancelledRecord) == before.size());
        const sim::Money cash = session.view().cash;
        session.stepOneDay();
        CHECK(session.view().cash == cash);
        CHECK(session.view().reservedCash == sim::Money::zero());
        CHECK(session.view().loans.empty());
        CHECK(commandRecords(out.str()).empty());
    }
    SECTION("a second save holds the orders once, not twice") {
        CHECK(session.save() == bytes);
    }
}

TEST_CASE("A game that saves plays out exactly as one that never does", "[session][pending]") {
    const sim::Content content = test::makeContent();
    std::ostringstream savedOut;
    std::ostringstream plainOut;
    game::TraceWriter savedTrace(savedOut);
    game::TraceWriter plainTrace(plainOut);
    game::Session saver(content, 7, test::setup());
    game::Session plain(content, 7, test::setup());
    saver.setTrace(&savedTrace);
    plain.setTrace(&plainTrace);
    saver.setAutoPause(neverPause());
    plain.setAutoPause(neverPause());
    const sim::PlotId plot = firstListedPlot(saver);
    REQUIRE(plot.valid());
    const sim::Money ask = saver.view().plots[plot.index()].askPrice;

    const std::vector<sim::Command> dayZero = {
        sim::CommissionSurvey{{}, plot}, sim::TakeLoan{{}, sim::Money::fromUnits(1'000'000)},
        sim::BidOnPlot{{}, plot, ask}, sim::StartResearch{{}, sim::TechId(0)}};
    const std::vector<sim::Command> dayTen = {sim::BidOnPlot{{}, plot, ask + ask / 10},
                                              sim::SetCoverCosts{{}, 1}};
    for (sim::Day day = 0; day < 60; ++day) {
        std::span<const sim::Command> orders;
        if (day == 0) {
            orders = dayZero;
        } else if (day == 10) {
            orders = dayTen;
        }
        for (const sim::Command& order : orders) {
            saver.submit(order);
            plain.submit(order);
        }
        // Saved every day, with orders waiting and without.
        const std::vector<std::uint8_t> bytes = saver.save();
        CHECK_FALSE(bytes.empty());
        REQUIRE(saver.pendingOrders().size() == orders.size());
        saver.stepOneDay();
        plain.stepOneDay();
        REQUIRE(saver.stateHash() == plain.stateHash());
    }
    CHECK(saver.view().loans.size() == 1);  // the orders did reach the sim
    CHECK(saver.log().size() == plain.log().size());
    CHECK(savedOut.str() == plainOut.str());  // saving left no mark on the trace
}

TEST_CASE("With nothing pending the session's save is the sim's own", "[session][pending]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 7, test::setup());
    session.setAutoPause(neverPause());
    sim::Simulation direct(content, 7, test::setup());
    sim::HeuristicController ai(content);
    CHECK(session.save() == direct.save());
    for (sim::Day day = 0; day < 5; ++day) {
        session.stepOneDay();
        tickDirect(direct, ai, {});
    }
    CHECK(session.save() == direct.save());

    SECTION("an order queued and withdrawn leaves the bytes as they were") {
        session.submit(sim::StartResearch{{}, sim::TechId(0)});
        CHECK(session.save() != direct.save());
        session.cancelAll();
        CHECK(session.save() == direct.save());
    }
    SECTION("a session that is not to be changed can still be saved") {
        const game::Session& fixed = session;
        CHECK(fixed.save() == direct.save());
    }
}

TEST_CASE("Committed sums what the pending orders will cost at today's quotes",
          "[session][pending]") {
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, soloSetup());
    session.setAutoPause(neverPause());
    CHECK(session.committed() == sim::Money::zero());
    CHECK(session.pendingBorrowed() == sim::Money::zero());
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    const sim::PlotView& plotView = session.view().plots[plot.index()];
    const sim::Money ask = plotView.askPrice;
    const sim::Money capex = plotView.mineCapexQuote;
    REQUIRE(capex > sim::Money::zero());

    SECTION("each kind of order that costs money") {
        session.submit(sim::CommissionSurvey{{}, plot});
        CHECK(session.committed() == content.balance.surveyCost);
        session.submit(sim::BidOnPlot{{}, plot, ask + sim::Money::fromUnits(7)});
        CHECK(session.committed() == content.balance.surveyCost + ask + sim::Money::fromUnits(7));
        session.submit(sim::BuildMine{{}, plot});
        session.submit(sim::StartResearch{{}, sim::TechId(1)});
        const sim::Money expected = content.balance.surveyCost + ask + sim::Money::fromUnits(7) +
                                    capex + content.techs[1].cost;
        CHECK(session.committed() == expected);
        CHECK(session.pendingBorrowed() == sim::Money::zero());

        // Cancelling takes the order's cost back out.
        CHECK(session.cancel(session.pendingOrders()[3].id));
        CHECK(session.committed() == expected - content.techs[1].cost);
        session.cancelAll();
        CHECK(session.committed() == sim::Money::zero());
    }
    SECTION("loans are counted as borrowed, repayments as committed") {
        session.submit(sim::TakeLoan{{}, sim::Money::fromUnits(1'000'000)});
        session.submit(sim::TakeLoan{{}, sim::Money::fromUnits(250'000)});
        CHECK(session.pendingBorrowed() == sim::Money::fromUnits(1'250'000));
        CHECK(session.committed() == sim::Money::zero());
        session.stepOneDay();
        CHECK(session.pendingBorrowed() == sim::Money::zero());  // given: now it is cash
        REQUIRE(session.view().loans.size() == 2);

        session.submit(
            sim::RepayLoan{{}, session.view().loans[0].id, sim::Money::fromUnits(300'000)});
        session.submit(sim::RepayLoan{{}, session.view().loans[1].id, sim::Money::fromUnits(50)});
        CHECK(session.committed() == sim::Money::fromUnits(300'050));
        CHECK(session.pendingBorrowed() == sim::Money::zero());
    }
    SECTION("orders that cost nothing up front count toward neither figure") {
        session.submit(sim::SellOre{{}, sim::ResourceId(0), 500});
        session.submit(sim::SetMineState{{}, sim::MineId(0), 0});
        sim::SetSellPolicy policy;
        policy.resource = sim::ResourceId(0);
        policy.enabled = 1;
        policy.floorPrice = sim::Money::fromUnits(90);
        session.submit(policy);
        session.submit(sim::SetCoverCosts{{}, 1});
        CHECK(session.pendingOrders().size() == 4);
        CHECK(session.committed() == sim::Money::zero());
        CHECK(session.pendingBorrowed() == sim::Money::zero());
    }
    SECTION("orders the sim will turn away are still listed and cost nothing known") {
        // The session never rejects (R-001): it lists them and lets the sim rule.
        session.submit(sim::BuildMine{{}, sim::PlotId(9'999)});
        session.submit(sim::StartResearch{{}, sim::TechId(9'999)});
        CHECK(session.pendingOrders().size() == 2);
        CHECK(session.committed() == sim::Money::zero());
        session.stepOneDay();
        CHECK(countKind(session, sim::NoticeKind::CommandRejected) == 2);
    }
    SECTION("a survey or a bid on a plot that does not exist costs nothing") {
        const sim::CommissionSurvey survey{{}, sim::PlotId(9'999)};
        const sim::BidOnPlot bid{{}, sim::PlotId(9'999), ask};
        CHECK(session.orderCost(survey) == sim::Money::zero());
        CHECK(session.orderCost(bid) == sim::Money::zero());
        CHECK(session.orderCost(sim::CommissionSurvey{}) == sim::Money::zero());  // no plot named
        CHECK(session.orderCost(sim::BidOnPlot{{}, sim::PlotId(), ask}) == sim::Money::zero());
        // The same orders on a plot that does exist still cost what they did.
        CHECK(session.orderCost(sim::CommissionSurvey{{}, plot}) == content.balance.surveyCost);
        CHECK(session.orderCost(sim::BidOnPlot{{}, plot, ask}) == ask);

        session.submit(survey);
        session.submit(bid);
        CHECK(session.pendingOrders().size() == 2);  // listed all the same (R-001)
        CHECK(session.committed() == sim::Money::zero());
        session.stepOneDay();
        CHECK(countKind(session, sim::NoticeKind::CommandRejected) == 2);
    }
    SECTION("raising a standing bid commits only the difference") {
        session.submit(sim::BidOnPlot{{}, plot, ask});
        session.stepOneDay();
        REQUIRE(session.view().reservedCash == ask);  // the standing bid is reserved already
        session.submit(sim::BidOnPlot{{}, plot, ask + sim::Money::fromUnits(40'000)});
        CHECK(session.committed() == sim::Money::fromUnits(40'000));
        session.stepOneDay();
        CHECK(session.view().reservedCash == ask + sim::Money::fromUnits(40'000));
        CHECK(countKind(session, sim::NoticeKind::CommandRejected) == 0);
    }
}

TEST_CASE("Pending sums saturate at the largest amount instead of stopping the game",
          "[session][pending]") {
    // These figures are for display (R-001): an absurd order is the sim's to
    // turn away on the next tick, and must not stop the client before then.
    const sim::Content content = test::makeContent();
    game::Session session(content, 1, soloSetup());
    session.setAutoPause(neverPause());
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    const sim::Money largest = sim::Money::fromCents(std::numeric_limits<std::int64_t>::max());
    const sim::Money lowest = sim::Money::fromCents(std::numeric_limits<std::int64_t>::min());
    // The most game::parseMoney accepts, so the most a player can type.
    sim::Money typed;
    REQUIRE(game::parseMoney("1,000,000,000,000,000", typed));
    REQUIRE(typed == sim::Money::fromCents(sim::kMaxCashCents));
    REQUIRE_FALSE(game::parseMoney("1,000,000,000,000,000.01", typed));

    SECTION("two maximal bids") {
        session.submit(sim::BidOnPlot{{}, plot, largest});
        session.submit(sim::BidOnPlot{{}, plot, largest});
        CHECK(session.committed() == largest);
    }
    SECTION("a maximal bid on top of an ordinary order") {
        session.submit(sim::CommissionSurvey{{}, plot});
        session.submit(sim::BidOnPlot{{}, plot, largest});
        CHECK(session.committed() == largest);
    }
    SECTION("two maximal loans") {
        session.submit(sim::TakeLoan{{}, largest});
        session.submit(sim::TakeLoan{{}, largest});
        CHECK(session.pendingBorrowed() == largest);
        CHECK(session.committed() == sim::Money::zero());
    }
    SECTION("a maximal repayment") {
        const sim::RepayLoan repay{{}, sim::LoanId(0), largest};
        CHECK(session.orderCost(repay) == largest);
        session.submit(repay);
        CHECK(session.committed() == largest);
        session.submit(sim::CommissionSurvey{{}, plot});
        CHECK(session.committed() == largest);
        session.submit(repay);
        CHECK(session.committed() == largest);
    }
    SECTION("as many of the largest typed amounts as it takes") {
        // No single typed amount overflows, and two do not either; a long
        // enough queue of them does.
        for (int i = 0; i < 100; ++i) {
            session.submit(sim::TakeLoan{{}, typed});
            session.submit(sim::BidOnPlot{{}, plot, typed});
            session.submit(sim::RepayLoan{{}, sim::LoanId(0), typed});
        }
        CHECK(session.pendingBorrowed() == largest);
        CHECK(session.committed() == largest);
    }
    SECTION("cancelling brings a saturated sum back down") {
        session.submit(sim::TakeLoan{{}, largest});
        session.submit(sim::TakeLoan{{}, sim::Money::fromUnits(250'000)});
        REQUIRE(session.pendingBorrowed() == largest);
        CHECK(session.cancel(session.pendingOrders()[0].id));
        CHECK(session.pendingBorrowed() == sim::Money::fromUnits(250'000));
    }
    SECTION("zero and negative amounts cost nothing") {
        for (const sim::Money amount :
             {sim::Money::zero(), sim::Money::fromCents(-1), -typed, -largest, lowest}) {
            CHECK(session.orderCost(sim::BidOnPlot{{}, plot, amount}) == sim::Money::zero());
            CHECK(session.orderCost(sim::RepayLoan{{}, sim::LoanId(0), amount}) ==
                  sim::Money::zero());
            session.submit(sim::BidOnPlot{{}, plot, amount});
            session.submit(sim::RepayLoan{{}, sim::LoanId(0), amount});
            session.submit(sim::TakeLoan{{}, amount});
        }
        CHECK(session.committed() == sim::Money::zero());
        CHECK(session.pendingBorrowed() == sim::Money::zero());
    }
    SECTION("a negative raise of a standing bid costs nothing") {
        const sim::Money ask = session.view().plots[plot.index()].askPrice;
        session.submit(sim::BidOnPlot{{}, plot, ask});
        session.stepOneDay();
        REQUIRE(session.view().reservedCash == ask);
        // Less than the standing bid by more than a Money can hold.
        for (const sim::Money amount : {sim::Money::zero(), -largest, lowest}) {
            CHECK(session.orderCost(sim::BidOnPlot{{}, plot, amount}) == sim::Money::zero());
            session.submit(sim::BidOnPlot{{}, plot, amount});
        }
        CHECK(session.committed() == sim::Money::zero());
        // A maximal raise is the difference, as any raise is.
        CHECK(session.orderCost(sim::BidOnPlot{{}, plot, largest}) == largest - ask);
    }
}

TEST_CASE("The trace records an order when it is given to the sim, not when it is queued",
          "[session][pending]") {
    const sim::Content content = test::makeContent();
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 1, test::setup());
    session.setTrace(&trace);
    session.setAutoPause(neverPause());
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    session.stepOneDay();
    session.stepOneDay();  // day 2, so the record's day is not the default
    out.str("");

    SECTION("queued, then given on the tick with the day it was ordered on") {
        session.submit(sim::CommissionSurvey{{}, plot});
        CHECK(countText(out.str(), kQueuedRecord) == 1);
        CHECK(countText(out.str(), kCommandRecord) == 0);
        CHECK(out.str().find("\"day\":2") != std::string::npos);

        session.stepOneDay();
        const std::string text = out.str();
        const std::vector<game::TraceRecord> given = commandRecords(text);
        REQUIRE(given.size() == 1);
        CHECK(given[0].day == 2);  // the day it was ordered on, as before
        REQUIRE(given[0].command.has_value());
        const auto* survey = std::get_if<sim::CommissionSurvey>(&*given[0].command);
        REQUIRE(survey != nullptr);
        CHECK(survey->plot == plot);
        CHECK(survey->company == game::Session::kPlayer);
        // Written before the tick it takes effect in, so a replay submits it
        // ahead of that day's record.
        CHECK(text.find(kCommandRecord) < text.find("\"t\":\"day\""));
        CHECK(countText(text, kQueuedRecord) == 1);
        session.stepOneDay();
        CHECK(commandRecords(out.str()).size() == 1);  // given once
    }
    SECTION("a cancelled order is never a command record") {
        session.submit(sim::CommissionSurvey{{}, plot});
        session.submit(sim::StartResearch{{}, sim::TechId(0)});
        CHECK(session.cancel(session.pendingOrders()[0].id));
        CHECK(countText(out.str(), kQueuedRecord) == 2);
        CHECK(countText(out.str(), kCancelledRecord) == 1);
        CHECK_FALSE(session.cancel(9'999));  // nothing cancelled, nothing recorded
        CHECK(countText(out.str(), kCancelledRecord) == 1);

        session.stepOneDay();
        const std::vector<game::TraceRecord> given = commandRecords(out.str());
        REQUIRE(given.size() == 1);
        CHECK(std::holds_alternative<sim::StartResearch>(*given[0].command));
    }
    SECTION("cancel all records each order it withdraws") {
        session.submit(sim::CommissionSurvey{{}, plot});
        session.submit(sim::TakeLoan{{}, content.balance.loanMinPrincipal});
        session.cancelAll();
        CHECK(countText(out.str(), kCancelledRecord) == 2);
        session.stepOneDay();
        CHECK(commandRecords(out.str()).empty());
    }
    SECTION("without a trace, queueing and cancelling still work") {
        session.setTrace(nullptr);
        session.submit(sim::CommissionSurvey{{}, plot});
        CHECK(session.cancel(session.pendingOrders()[0].id));
        session.submit(sim::CommissionSurvey{{}, plot});
        session.stepOneDay();
        CHECK(session.view().surveys.size() == 1);
        CHECK(out.str().empty());
    }
}

TEST_CASE("A bid given on the day its auction closes still counts as the player's stake",
          "[session][pending]") {
    sim::Content content = test::makeContent();
    test::addStrings(content);
    game::Session session(content, 1, soloSetup());
    session.setAutoPause(neverPause());
    const sim::PlotId plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    REQUIRE(session.view().plots[plot.index()].auction.has_value());
    const sim::Day closes = session.view().plots[plot.index()].auction->closesOnDay;
    while (session.day() < closes) {
        session.stepOneDay();
    }
    REQUIRE(session.view().plots[plot.index()].auction.has_value());
    REQUIRE(session.view().surveys.empty());

    SECTION("no bid: the relist is not news") {
        session.stepOneDay();
        CHECK(countKind(session, sim::NoticeKind::PlotRelisted) == 0);
    }
    SECTION("a bid the sim turns away on the closing day makes the relist news") {
        // The bid is handed over and the auction closes in the same tick, so
        // the view never shows it: the session's own note is the only stake.
        const sim::Money ask = session.view().plots[plot.index()].askPrice;
        session.submit(sim::BidOnPlot{{}, plot, ask - sim::Money::fromUnits(1)});
        session.stepOneDay();
        CHECK(countKind(session, sim::NoticeKind::CommandRejected) == 1);
        REQUIRE(countKind(session, sim::NoticeKind::PlotRelisted) >= 1);
        for (const game::LogEntry& entry : session.log()) {
            if (entry.notice.kind == sim::NoticeKind::PlotRelisted) {
                CHECK(entry.notice.plot == plot);
            }
        }
    }
}

// Spec 008: the losing-mine notice can stop the clock.

namespace {

const sim::ResourceId kIron(0);

// The fixture with iron at 100,000 a day: a mine's 400 t break even at $250
// a tonne, far above the fixture's $60, so an iron mine loses money every day.
sim::Content losingMineContent() {
    sim::Content content = test::makeContent();
    test::addStrings(content);
    content.resources[kIron.index()].mineOpexPerDay = sim::Money::fromUnits(100'000);
    return content;
}

void reload(sim::Simulation& simulation, const sim::Content& content,
            const sim::WorldState& edited) {
    REQUIRE(simulation.load(sim::saveGame(edited, {}, sim::hashContent(content))));
}

// Buys the first listed plot for the player at its asking price.
sim::PlotId buyListedPlot(sim::Simulation& simulation) {
    const sim::WorldState& state = simulation.state();
    const auto listed = std::find_if(state.plots.begin(), state.plots.end(), [](const auto& plot) {
        return plot.listing == sim::PlotListing::Listed;
    });
    REQUIRE(listed != state.plots.end());
    const sim::PlotId plot(static_cast<std::uint32_t>(listed - state.plots.begin()));
    const sim::Day closes = state.auctions[listed->auction.index()].closesOnDay;
    simulation.submit(sim::BidOnPlot{game::Session::kPlayer, plot, listed->askPrice});
    while (simulation.state().day <= closes) {
        simulation.tick();
    }
    REQUIRE(simulation.state().plots[plot.index()].owner == game::Session::kPlayer);
    return plot;
}

// A saved solo game in which the player runs `mines` iron mines at a loss,
// each one day short of its losing-mine notice and none announced yet.
std::vector<std::uint8_t> eveOfLosingNotice(const sim::Content& content, std::size_t mines) {
    sim::Simulation simulation(content, 1, soloSetup());
    sim::WorldState rich = simulation.state();
    rich.companies[game::Session::kPlayer.index()].cash = sim::Money::fromUnits(50'000'000);
    reload(simulation, content, rich);

    std::vector<sim::PlotId> plots;
    for (std::size_t i = 0; i < mines; ++i) {
        plots.push_back(buyListedPlot(simulation));
    }
    sim::WorldState seeded = simulation.state();
    for (const sim::PlotId plot : plots) {
        seeded.hidden.plotDeposits[plot.index()].deposits = {{kIron, 1'000'000, 1'000'000}};
    }
    reload(simulation, content, seeded);
    for (const sim::PlotId plot : plots) {
        simulation.submit(sim::BuildMine{game::Session::kPlayer, plot});
    }
    simulation.tick(content.balance.mineBuildDays + 1);
    REQUIRE(simulation.state().mines.size() == mines);

    REQUIRE(content.balance.mineLossNoticeDays >= 2);
    sim::WorldState eve = simulation.state();
    for (sim::Mine& mine : eve.mines) {
        REQUIRE(mine.state == sim::MineState::Operating);
        REQUIRE(mine.lossDays > 0);  // it really is losing money at the market's price
        mine.lossDays = content.balance.mineLossNoticeDays - 1;
        mine.lossAnnounced = 0;
    }
    return sim::saveGame(eve, {}, sim::hashContent(content));
}

}  // namespace

TEST_CASE("The losing-mine stop rule has a stable key and is on by default", "[session][spec008]") {
    CHECK(game::pauseOnKey(game::PauseOn::MineLosing) == "mine_losing");
    const game::AutoPause defaults = game::AutoPause::defaults();
    CHECK(defaults.isEnabled(game::PauseOn::MineLosing));

    // The player's own losing mine is the reason; nobody else's is.
    sim::Notice losing =
        sim::makeNotice(sim::NoticeKind::MineLosingMoney, game::Session::kPlayer, sim::PlotId(3));
    CHECK(game::AutoPause::reasonFor(losing, game::Session::kPlayer) == game::PauseOn::MineLosing);
    CHECK(defaults.shouldPause(losing, game::Session::kPlayer));
    losing.company = sim::CompanyId(1);
    CHECK_FALSE(game::AutoPause::reasonFor(losing, game::Session::kPlayer).has_value());
    CHECK_FALSE(defaults.shouldPause(losing, game::Session::kPlayer));

    // The rule is written out with the others and comes back switched off.
    game::AutoPause rules = game::AutoPause::defaults();
    CHECK(rules.toText().find("mine_losing=1\n") != std::string::npos);
    rules.set(game::PauseOn::MineLosing, false);
    CHECK(rules.toText().find("mine_losing=0\n") != std::string::npos);
    game::AutoPause restored = game::AutoPause::defaults();
    CHECK(restored.fromText(rules.toText()));
    CHECK_FALSE(restored.isEnabled(game::PauseOn::MineLosing));
    CHECK(restored.enabled == rules.enabled);
}

TEST_CASE("A settings file from before the losing-mine rule gets its default and keeps its toggles",
          "[session][spec008]") {
    // As spec 004 to 007 wrote it: nine keys, no mine_losing.
    const std::string old =
        "insolvency=1\nbankrupt=1\nauction_won=0\nauction_lost=1\n"
        "survey_complete=0\nmine_operational=1\nmine_found_nothing=1\n"
        "idle_mine=0\nresearch_complete=0\n";
    game::AutoPause migrated = game::AutoPause::defaults();
    CHECK(migrated.fromText(old));
    CHECK(migrated.isEnabled(game::PauseOn::MineLosing));

    game::AutoPause expected = game::AutoPause::defaults();
    expected.set(game::PauseOn::AuctionWon, false);
    expected.set(game::PauseOn::SurveyComplete, false);
    expected.set(game::PauseOn::MineOperational, true);
    expected.set(game::PauseOn::IdleMine, false);
    expected.set(game::PauseOn::ResearchComplete, false);
    CHECK(migrated.enabled == expected.enabled);
}

TEST_CASE("A losing mine stops the clock on the day its notice arrives", "[session][spec008]") {
    const sim::Content content = losingMineContent();
    game::Session session(content, 1, soloSetup());
    REQUIRE(session.load(eveOfLosingNotice(content, 1)));
    const sim::Day eve = session.day();
    REQUIRE(countKind(session, sim::NoticeKind::MineLosingMoney) == 0);

    SECTION("with the rule on, as it is by default") {
        session.setAutoPause(game::AutoPause::defaults());
        session.setSpeed(game::Speed::Fastest);
        CHECK(session.advance(1.0) == 1);  // 20 days were due; the first one stopped it
        CHECK(session.speed() == game::Speed::Paused);
        CHECK(session.day() == eve + 1);
        REQUIRE(countKind(session, sim::NoticeKind::MineLosingMoney) == 1);
        REQUIRE_FALSE(session.stopsToday().empty());
        const sim::Notice why = session.stopsToday().front();
        CHECK(why.kind == sim::NoticeKind::MineLosingMoney);
        CHECK(why.company == game::Session::kPlayer);
        CHECK(why.mine == sim::MineId(0));
        // Resuming works: the loss goes on, and nothing more is said about it.
        session.setSpeed(game::Speed::Fastest);
        CHECK(session.advance(1.0) == 20);
        CHECK(countKind(session, sim::NoticeKind::MineLosingMoney) == 1);
    }
    SECTION("with the rule off the log still says so and the clock runs on") {
        game::AutoPause rules = game::AutoPause::defaults();
        rules.set(game::PauseOn::MineLosing, false);
        session.setAutoPause(rules);
        session.setSpeed(game::Speed::Fastest);
        CHECK(session.advance(1.0) == 20);
        CHECK(session.speed() == game::Speed::Fastest);
        CHECK(session.day() == eve + 20);
        CHECK(session.stopsToday().empty());
        REQUIRE(countKind(session, sim::NoticeKind::MineLosingMoney) == 1);
        for (const game::LogEntry& entry : session.log()) {
            if (entry.notice.kind == sim::NoticeKind::MineLosingMoney) {
                CHECK(entry.day == eve);  // logged on the day it was raised
                CHECK(entry.text.find("plot #") != std::string::npos);
                CHECK(entry.text.front() != '[');  // the catalogue's sentence, not a missing key
            }
        }
    }
}

TEST_CASE("Several mines turning to a loss on one day stop the clock once, with every reason kept",
          "[session][spec008]") {
    const sim::Content content = losingMineContent();
    game::Session session(content, 1, soloSetup());
    REQUIRE(session.load(eveOfLosingNotice(content, 2)));
    const sim::Day eve = session.day();
    session.setAutoPause(game::AutoPause::defaults());
    session.setSpeed(game::Speed::Fastest);

    CHECK(session.advance(1.0) == 1);
    CHECK(session.speed() == game::Speed::Paused);
    CHECK(session.day() == eve + 1);
    // One line in the log for each mine; one stop, with both reasons kept
    // in log order (spec 011, BR-009) and both lines marked.
    CHECK(countKind(session, sim::NoticeKind::MineLosingMoney) == 2);
    const std::vector<sim::Notice>& stops = session.stopsToday();
    REQUIRE(stops.size() == 2);
    CHECK(stops[0].kind == sim::NoticeKind::MineLosingMoney);
    CHECK(stops[0].mine == sim::MineId(0));
    CHECK(stops[1].kind == sim::NoticeKind::MineLosingMoney);
    CHECK(stops[1].mine == sim::MineId(1));
    for (const game::LogEntry& entry : session.log()) {
        CHECK(entry.stoppedClock == (entry.notice.kind == sim::NoticeKind::MineLosingMoney));
    }
    // The second mine's notice is not held over to stop the clock again.
    session.setSpeed(game::Speed::Fastest);
    CHECK(session.stopsToday().empty());
    CHECK(session.advance(1.0) == 20);
    CHECK(session.speed() == game::Speed::Fastest);
    CHECK(countKind(session, sim::NoticeKind::MineLosingMoney) == 2);
}

// ---- spec 010: the race ends the game and the rival's progress stops the clock ----

namespace {

const sim::CompanyId kRival(1);

sim::Content raceContent() {
    sim::Content content = test::makeContent();
    test::addStrings(content);
    return content;
}

// A two-company game a couple of days in: an outcome names a day that has
// been played, so nothing here ends on day zero.
sim::WorldState raceWorld(const sim::Content& content) {
    sim::Simulation simulation(content, 7, test::setup());
    simulation.tick(2);
    return simulation.state();
}

// Leaves the company one CompleteProgramStage order from finishing the stage
// with this index: the stages before it done, its research finished, its
// goods delivered and the cash at hand.
void putStageInReach(const sim::Content& content, sim::Company& company, std::uint32_t stage) {
    company.programStage = stage;
    company.programStageDays.assign(stage, 1);
    for (std::uint32_t s = 0; s <= stage; ++s) {
        company.completedTechs.push_back(content.program.stages[s].research);
    }
    std::fill(company.programDelivered.begin(), company.programDelivered.end(), 0);
    for (const sim::ProgramRequirement& need : content.program.stages[stage].requirements) {
        company.programDelivered[need.resource.index()] = need.tonnes;
    }
    company.cash = sim::Money::fromUnits(20'000'000);
}

// The company with this many stages complete as a save holds them: each with
// its day and its research.
void putStagesBehind(const sim::Content& content, sim::Company& company, std::uint32_t stages) {
    company.programStage = stages;
    company.programStageDays.assign(stages, 1);
    for (std::uint32_t s = 0; s < stages; ++s) {
        company.completedTechs.push_back(content.program.stages[s].research);
    }
}

// The save's own orders are applied by the next tick, which is how a test
// has the rival act on a chosen day.
std::vector<std::uint8_t> raceSave(const sim::Content& content, const sim::WorldState& state,
                                   const std::vector<sim::Command>& orders = {}) {
    return sim::saveGame(test::settledPace(content, state), orders, sim::hashContent(content));
}

const game::LogEntry* firstOfKind(const game::Session& session, sim::NoticeKind kind) {
    const auto found = std::find_if(session.log().begin(), session.log().end(),
                                    [&](const game::LogEntry& e) { return e.notice.kind == kind; });
    return found == session.log().end() ? nullptr : &*found;
}

std::string textOfKind(const game::Session& session, sim::NoticeKind kind) {
    const game::LogEntry* entry = firstOfKind(session, kind);
    return entry == nullptr ? std::string("(no such notice)") : entry->text;
}

// An ended game does not move: whatever the player presses, the day, the
// histories, the log and the trace stay as the end left them.
void checkNothingMovesAfterTheEnd(game::Session& session, const std::ostringstream& trace) {
    REQUIRE(session.gameOver());
    const sim::Day day = session.day();
    const std::uint64_t hash = session.stateHash();
    const std::size_t cash = session.cashHistory().size();
    const std::size_t log = session.log().size();
    const std::string traced = trace.str();
    for (int i = 0; i < 3; ++i) {
        session.stepOneDay();
        session.setSpeed(game::Speed::Fastest);
        CHECK(session.advance(5.0) == 0);
        session.togglePause();
    }
    CHECK(session.speed() == game::Speed::Paused);  // the clock cannot be restarted
    CHECK(session.day() == day);
    CHECK(session.stateHash() == hash);
    CHECK(session.cashHistory().size() == cash);
    CHECK(session.log().size() == log);
    CHECK(trace.str() == traced);
}

}  // namespace

TEST_CASE("The session stops on the day the player founds the program", "[session][spec010]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    putStageInReach(content, world.companies[game::Session::kPlayer.index()], 2);
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world)));
    session.setTrace(&trace);
    session.setAutoPause(neverPause());
    const sim::Day eve = session.day();
    REQUIRE_FALSE(session.gameOver());

    session.submit(sim::CompleteProgramStage{});
    session.setSpeed(game::Speed::Fastest);
    CHECK(session.advance(1.0) == 1);  // 20 days were due; the game ended on the first

    CHECK(session.gameOver());
    CHECK(session.day() == eve);  // the world freezes on the day it ends
    CHECK(session.view().outcome.ended);
    CHECK(session.view().outcome.day == eve);
    CHECK(session.cashHistory().size() == 1);  // the day's figure, corrected
    CHECK(session.cashHistory().back() == session.view().cash);
    CHECK(session.view().outcome.founder == game::Session::kPlayer);
    // The end stops the clock whatever the rules say, and says why.
    CHECK(session.speed() == game::Speed::Paused);
    REQUIRE_FALSE(session.stopsToday().empty());
    const sim::Notice why = session.stopsToday().front();
    CHECK(why.kind == sim::NoticeKind::ProgramFounded);
    CHECK(textOfKind(session, sim::NoticeKind::ProgramFounded) ==
          "Player has founded the program. The race is over.");
    CHECK(countText(out.str(), R"("t":"day")") == 1);
    checkNothingMovesAfterTheEnd(session, out);
}

TEST_CASE("The session stops on the day the rival founds the program", "[session][spec010]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    putStageInReach(content, world.companies[kRival.index()], 2);

    SECTION("with the player two stages in, a lesser partner") {
        putStagesBehind(content, world.companies[game::Session::kPlayer.index()], 2);
    }
    SECTION("with the player behind, lost") {
        putStagesBehind(content, world.companies[game::Session::kPlayer.index()], 1);
    }
    const bool partner = world.companies[game::Session::kPlayer.index()].programStage == 2;
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world, {sim::CompleteProgramStage{kRival}})));
    session.setTrace(&trace);
    session.setAutoPause(neverPause());
    const sim::Day eve = session.day();

    session.setSpeed(game::Speed::Fastest);
    CHECK(session.advance(1.0) == 1);

    CHECK(session.gameOver());
    CHECK(session.day() == eve);
    CHECK(session.view().outcome.day == eve);
    CHECK(session.view().outcome.founder == kRival);
    CHECK(session.view().outcome.results.at(game::Session::kPlayer.index()) ==
          (partner ? sim::ProgramResult::LesserPartner : sim::ProgramResult::Lost));
    CHECK_FALSE(session.view().bankrupt);
    CHECK(session.speed() == game::Speed::Paused);
    REQUIRE_FALSE(session.stopsToday().empty());
    const sim::Notice why = session.stopsToday().front();
    CHECK(why.kind == sim::NoticeKind::ProgramFounded);
    CHECK(why.company == kRival);
    CHECK(textOfKind(session, sim::NoticeKind::ProgramFounded) ==
          "Rival has founded the program. The race is over.");
    checkNothingMovesAfterTheEnd(session, out);
}

TEST_CASE("The session stops when the deadline passes with nobody finished", "[session][spec010]") {
    sim::Content content = raceContent();
    content.program.deadlineDay = 6;
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 7, test::setup());
    session.setTrace(&trace);
    session.setAutoPause(neverPause());

    session.setSpeed(game::Speed::Fastest);
    const int ticked = session.advance(1.0);

    CHECK(ticked < 20);  // the deadline cut the run short
    CHECK(session.gameOver());
    CHECK(session.view().outcome.ended);
    CHECK_FALSE(session.view().outcome.founder.valid());
    // Six days close; the tick for the deadline day ends the game on it.
    CHECK(ticked == 7);
    CHECK(session.day() == content.program.deadlineDay);
    CHECK(session.view().outcome.day == session.day());
    CHECK(countText(out.str(), R"("t":"day")") == static_cast<std::size_t>(ticked));
    // The last record repeats the day with the final hash.
    CHECK(countText(out.str(), R"("day":6,"hash")") == 2);
    // One figure a day, the starting day included.
    CHECK(session.cashHistory().size() == static_cast<std::size_t>(session.day()) + 1);
    CHECK(session.speed() == game::Speed::Paused);
    REQUIRE_FALSE(session.stopsToday().empty());
    const sim::Notice why = session.stopsToday().front();
    CHECK(why.kind == sim::NoticeKind::DeadlinePassed);
    CHECK(textOfKind(session, sim::NoticeKind::DeadlinePassed) ==
          game::text(content, "notice.deadline_passed"));
    checkNothingMovesAfterTheEnd(session, out);
}

TEST_CASE("A rival's stage stops the clock under the default rules", "[session][spec010]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    putStageInReach(content, world.companies[kRival.index()], 0);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world, {sim::CompleteProgramStage{kRival}})));
    const sim::Day eve = session.day();

    session.setSpeed(game::Speed::Fastest);
    CHECK(session.advance(1.0) == 1);

    CHECK(session.speed() == game::Speed::Paused);
    CHECK(session.day() == eve + 1);
    CHECK_FALSE(session.gameOver());
    REQUIRE_FALSE(session.stopsToday().empty());
    const sim::Notice why = session.stopsToday().front();
    CHECK(why.kind == sim::NoticeKind::ProgramStageComplete);
    CHECK(why.company == kRival);
}

TEST_CASE("The session reports the outcome the sim set", "[session][spec010]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    putStageInReach(content, world.companies[game::Session::kPlayer.index()], 2);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world)));
    session.setAutoPause(neverPause());
    CHECK_FALSE(session.outcome().ended);
    CHECK_FALSE(session.gameOver());

    session.submit(sim::CompleteProgramStage{});
    session.stepOneDay();

    CHECK(session.gameOver());
    CHECK(session.outcome().ended);
    CHECK(session.outcome().founder == game::Session::kPlayer);
    CHECK(session.outcome().day == session.view().outcome.day);
    CHECK(session.outcome().results.at(game::Session::kPlayer.index()) ==
          sim::ProgramResult::Founded);
    CHECK(session.outcome().results.at(kRival.index()) == sim::ProgramResult::Lost);

    // A finished game loads as finished: over at once, with the clock stopped.
    game::Session other(content, 7, test::setup());
    REQUIRE(other.load(session.save()));
    CHECK(other.gameOver());
    CHECK(other.outcome().founder == game::Session::kPlayer);
    other.setSpeed(game::Speed::Normal);
    CHECK(other.speed() == game::Speed::Paused);
    CHECK(other.advance(3.0) == 0);
}

TEST_CASE("The session takes no order once the game is over", "[session][spec010]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    putStageInReach(content, world.companies[game::Session::kPlayer.index()], 2);
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world)));
    session.setTrace(&trace);
    session.setAutoPause(neverPause());

    // While the game is being played an order is queued, and says so.
    CHECK(session.submit(sim::CompleteProgramStage{}));
    CHECK(session.pendingOrders().size() == 1);
    CHECK(countText(out.str(), "order_queued") == 1);
    session.stepOneDay();
    REQUIRE(session.gameOver());
    REQUIRE(session.pendingOrders().empty());

    // After the end: refused, nothing queued, nothing traced, no id used.
    const std::string traced = out.str();
    const std::uint64_t hash = session.stateHash();
    CHECK_FALSE(session.submit(sim::TakeLoan{{}, sim::Money::fromUnits(500'000)}));
    CHECK_FALSE(session.submit(sim::DeliverToProgram{{}, sim::ResourceId(0), 10}));
    CHECK(session.pendingOrders().empty());
    CHECK(session.committed() == sim::Money::zero());
    CHECK(out.str() == traced);
    session.stepOneDay();
    CHECK(session.stateHash() == hash);
    CHECK(session.log().back().notice.kind != sim::NoticeKind::CommandRejected);
}

TEST_CASE("A bankrupt player's session takes no order", "[session][spec010]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    sim::Company& player = world.companies[game::Session::kPlayer.index()];
    player.bankrupt = 1;
    player.monthsInsolvent = content.balance.bankruptcyGraceMonths + 1;
    player.cash = sim::Money::fromUnits(-1);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world)));
    REQUIRE(session.gameOver());
    REQUIRE_FALSE(session.outcome().ended);

    CHECK_FALSE(session.submit(sim::TakeLoan{{}, sim::Money::fromUnits(500'000)}));
    CHECK(session.pendingOrders().empty());
}

TEST_CASE("The end record of a session's trace carries the outcome", "[session][spec010]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    putStageInReach(content, world.companies[kRival.index()], 2);
    putStagesBehind(content, world.companies[game::Session::kPlayer.index()], 2);
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world, {sim::CompleteProgramStage{kRival}})));
    session.setTrace(&trace);
    session.setAutoPause(neverPause());
    session.stepOneDay();
    REQUIRE(session.gameOver());

    trace.end(session.day(), session.view());

    std::istringstream lines(out.str());
    std::string line;
    std::string last;
    while (std::getline(lines, line)) {
        last = line;
    }
    game::TraceRecord record;
    std::string error;
    REQUIRE(game::parseTraceLine(last, record, error));
    CHECK(record.type == "end");
    CHECK(record.day == session.day());
    REQUIRE(record.outcome.has_value());
    CHECK(record.outcome->ended);
    CHECK(record.outcome->day == session.outcome().day);
    CHECK(record.outcome->founder == kRival);
    CHECK(record.outcome->result == "lesser_partner");
}

TEST_CASE("The rival-program stop rule has a stable key and is on by default",
          "[session][spec010]") {
    CHECK(game::pauseOnKey(game::PauseOn::RivalProgram) == "rival_program");
    const game::AutoPause defaults = game::AutoPause::defaults();
    CHECK(defaults.isEnabled(game::PauseOn::RivalProgram));

    // A rival starting a stage's research or completing a stage is the
    // reason; the player's own progress is not, and nor is anything else
    // the program raises.
    for (const sim::NoticeKind kind :
         {sim::NoticeKind::ProgramResearchStarted, sim::NoticeKind::ProgramStageComplete}) {
        sim::Notice progress = sim::makeNotice(kind, kRival, sim::PlotId());
        CHECK(game::AutoPause::reasonFor(progress, game::Session::kPlayer) ==
              game::PauseOn::RivalProgram);
        CHECK(defaults.shouldPause(progress, game::Session::kPlayer));
        progress.company = game::Session::kPlayer;
        CHECK_FALSE(game::AutoPause::reasonFor(progress, game::Session::kPlayer).has_value());
        CHECK_FALSE(defaults.shouldPause(progress, game::Session::kPlayer));
    }
    for (const sim::NoticeKind kind :
         {sim::NoticeKind::ProgramDelivered, sim::NoticeKind::ProgramFounded,
          sim::NoticeKind::DeadlinePassed}) {
        const sim::Notice other = sim::makeNotice(kind, kRival, sim::PlotId());
        CHECK_FALSE(game::AutoPause::reasonFor(other, game::Session::kPlayer).has_value());
    }

    // The rule is written out with the others and comes back switched off.
    game::AutoPause rules = game::AutoPause::defaults();
    CHECK(rules.toText().find("rival_program=1\n") != std::string::npos);
    rules.set(game::PauseOn::RivalProgram, false);
    CHECK(rules.toText().find("rival_program=0\n") != std::string::npos);
    game::AutoPause restored = game::AutoPause::defaults();
    CHECK(restored.fromText(rules.toText()));
    CHECK_FALSE(restored.isEnabled(game::PauseOn::RivalProgram));
    CHECK(restored.enabled == rules.enabled);
}

TEST_CASE("A settings file from before the rival-program rule keeps its toggles",
          "[session][spec010]") {
    // As spec 008 wrote it: ten keys, no rival_program.
    const std::string old =
        "insolvency=1\nbankrupt=1\nauction_won=0\nauction_lost=1\n"
        "survey_complete=0\nmine_operational=1\nmine_found_nothing=1\n"
        "idle_mine=0\nresearch_complete=0\nmine_losing=0\n";
    game::AutoPause migrated = game::AutoPause::defaults();
    CHECK(migrated.fromText(old));
    CHECK(migrated.isEnabled(game::PauseOn::RivalProgram));

    game::AutoPause expected = game::AutoPause::defaults();
    expected.set(game::PauseOn::AuctionWon, false);
    expected.set(game::PauseOn::SurveyComplete, false);
    expected.set(game::PauseOn::MineOperational, true);
    expected.set(game::PauseOn::IdleMine, false);
    expected.set(game::PauseOn::ResearchComplete, false);
    expected.set(game::PauseOn::MineLosing, false);
    CHECK(migrated.enabled == expected.enabled);
}

namespace {

// Only the rival-program rule, so nothing else the day raises can be what
// stopped the clock.
game::AutoPause onlyRivalProgram(bool on) {
    game::AutoPause rules = neverPause();
    rules.set(game::PauseOn::RivalProgram, on);
    return rules;
}

}  // namespace

TEST_CASE("A rival starting a stage's research stops the clock that day", "[session][spec010]") {
    const sim::Content content = raceContent();
    const sim::TechId project = content.program.stages[0].research;
    sim::WorldState world = raceWorld(content);
    world.companies[kRival.index()].cash = sim::Money::fromUnits(20'000'000);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world, {sim::StartResearch{kRival, project}})));
    const sim::Day eve = session.day();
    session.setSpeed(game::Speed::Fastest);

    SECTION("with the rule on") {
        session.setAutoPause(onlyRivalProgram(true));
        CHECK(session.advance(1.0) == 1);  // 20 days were due; the first one stopped it
        CHECK(session.speed() == game::Speed::Paused);
        CHECK(session.day() == eve + 1);
        REQUIRE_FALSE(session.stopsToday().empty());
        const sim::Notice why = session.stopsToday().front();
        CHECK(why.kind == sim::NoticeKind::ProgramResearchStarted);
        CHECK(why.company == kRival);
        // Resuming works, and the same start is not announced twice.
        session.setSpeed(game::Speed::Fastest);
        CHECK(session.advance(1.0) == 20);
    }
    SECTION("with the rule off the log still says so and the clock runs on") {
        session.setAutoPause(onlyRivalProgram(false));
        CHECK(session.advance(1.0) == 20);
        CHECK(session.speed() == game::Speed::Fastest);
        CHECK(session.day() == eve + 20);
        CHECK(session.stopsToday().empty());
    }
    REQUIRE(countKind(session, sim::NoticeKind::ProgramResearchStarted) == 1);
    const game::LogEntry* line = firstOfKind(session, sim::NoticeKind::ProgramResearchStarted);
    CHECK(line->day == eve);  // logged on the day it was raised
    CHECK(line->text == game::text(content, "notice.program_research_started",
                                   {{"company", "Rival"}, {"stage", "1"}}));
}

TEST_CASE("A rival completing a stage stops the clock that day", "[session][spec010]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    putStageInReach(content, world.companies[kRival.index()], 1);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world, {sim::CompleteProgramStage{kRival}})));
    const sim::Day eve = session.day();
    session.setSpeed(game::Speed::Fastest);

    SECTION("with the rule on") {
        session.setAutoPause(onlyRivalProgram(true));
        CHECK(session.advance(1.0) == 1);
        CHECK(session.speed() == game::Speed::Paused);
        CHECK(session.day() == eve + 1);
        REQUIRE_FALSE(session.stopsToday().empty());
        const sim::Notice why = session.stopsToday().front();
        CHECK(why.kind == sim::NoticeKind::ProgramStageComplete);
        CHECK(why.company == kRival);
    }
    SECTION("with the rule off the log still says so and the clock runs on") {
        session.setAutoPause(onlyRivalProgram(false));
        CHECK(session.advance(0.25) == 5);
        CHECK(session.speed() == game::Speed::Fastest);
        CHECK(session.day() == eve + 5);
        CHECK(session.stopsToday().empty());
    }
    CHECK_FALSE(session.gameOver());
    const game::LogEntry* line = firstOfKind(session, sim::NoticeKind::ProgramStageComplete);
    REQUIRE(line != nullptr);
    CHECK(line->day == eve);
    CHECK(line->text ==
          game::text(content, "notice.program_stage_complete",
                     {{"company", "Rival"},
                      {"stage", "2"},
                      {"amount", game::formatMoney(content.program.stages[1].cost)}}));
}

TEST_CASE("The player's own program progress is logged and does not stop the clock",
          "[session][spec010]") {
    const sim::Content content = raceContent();
    const sim::ResourceId iron(0);
    sim::WorldState world = raceWorld(content);
    sim::Company& player = world.companies[game::Session::kPlayer.index()];
    player.cash = sim::Money::fromUnits(20'000'000);
    player.inventoryTonnes[iron.index()] = 1'500;
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world)));
    session.setAutoPause(onlyRivalProgram(true));
    const sim::Day eve = session.day();

    // The research for the first stage, and a delivery toward it.
    session.submit(sim::StartResearch{{}, content.program.stages[0].research});
    session.submit(sim::DeliverToProgram{{}, iron, 1'000});
    session.setSpeed(game::Speed::Fastest);
    CHECK(session.advance(0.25) == 5);
    CHECK(session.speed() == game::Speed::Fastest);
    CHECK(session.stopsToday().empty());
    const game::LogEntry* started = firstOfKind(session, sim::NoticeKind::ProgramResearchStarted);
    REQUIRE(started != nullptr);
    CHECK(started->notice.company == game::Session::kPlayer);
    CHECK(started->day == eve);
    CHECK(started->text == game::text(content, "notice.program_research_started",
                                      {{"company", "Player"}, {"stage", "1"}}));
    CHECK(textOfKind(session, sim::NoticeKind::ProgramDelivered) ==
          game::text(content, "notice.program_delivered",
                     {{"tonnes", game::formatTonnes(1'000)}, {"resource", "Iron"}}));

    // The stage itself, once its research is in.
    editWorld(session, content, test::setup(), [&](sim::WorldState& edited) {
        edited.companies[game::Session::kPlayer.index()].completedTechs.push_back(
            content.program.stages[0].research);
        edited.research.clear();
    });
    session.setAutoPause(onlyRivalProgram(true));
    const sim::Day stageEve = session.day();
    session.submit(sim::CompleteProgramStage{});
    session.setSpeed(game::Speed::Fastest);
    CHECK(session.advance(0.25) == 5);
    CHECK(session.speed() == game::Speed::Fastest);
    CHECK(session.stopsToday().empty());
    const game::LogEntry* done = firstOfKind(session, sim::NoticeKind::ProgramStageComplete);
    REQUIRE(done != nullptr);
    CHECK(done->notice.company == game::Session::kPlayer);
    CHECK(done->day == stageEve);
    CHECK(done->text ==
          game::text(content, "notice.program_stage_complete",
                     {{"company", "Player"},
                      {"stage", "1"},
                      {"amount", game::formatMoney(content.program.stages[0].cost)}}));
}

TEST_CASE("The player's bankruptcy stops the clock whatever the rules say", "[session][spec010]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    world.companies[game::Session::kPlayer.index()].cash = sim::Money::fromUnits(-50'000'000);
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world)));
    session.setTrace(&trace);
    session.setAutoPause(neverPause());

    session.setSpeed(game::Speed::Fastest);
    for (int i = 0; i < 20 && !session.gameOver(); ++i) {
        session.advance(1.0);
    }

    REQUIRE(session.gameOver());
    CHECK(session.view().bankrupt);
    CHECK_FALSE(session.outcome().ended);  // the race is still open; the player is out of it
    CHECK(session.speed() == game::Speed::Paused);
    REQUIRE_FALSE(session.stopsToday().empty());
    const sim::Notice why = session.stopsToday().front();
    CHECK(why.kind == sim::NoticeKind::Bankrupt);
    CHECK(why.company == game::Session::kPlayer);
    checkNothingMovesAfterTheEnd(session, out);
}

// ---- playtest 4: the log says what actually happened ----

namespace {

// A session whose player owns one working mine; `plot` is the plot it is on.
void openAMine(game::Session& session, const sim::Content& content, sim::PlotId& plot) {
    session.setAutoPause(neverPause());
    plot = firstListedPlot(session);
    REQUIRE(plot.valid());
    session.submit(sim::BidOnPlot{{}, plot, session.view().plots[plot.index()].askPrice});
    for (sim::Day d = 0; d <= content.balance.auctionWindowDays + 1; ++d) {
        session.stepOneDay();
    }
    REQUIRE(session.view().plots[plot.index()].owner == game::Session::kPlayer);
    const sim::PlotId owned = plot;
    editWorld(session, content, soloSetup(), [&](sim::WorldState& world) {
        world.companies[0].cash = sim::Money::fromUnits(50'000'000);
        auto& deposits = world.hidden.plotDeposits[owned.index()].deposits;
        deposits.clear();
        deposits.push_back({sim::ResourceId(0), 10'000'000'000, 10'000'000'000});
    });
    session.submit(sim::BuildMine{{}, plot});
    for (sim::Day d = 0; d <= content.balance.mineBuildDays + 1; ++d) {
        session.stepOneDay();
    }
    REQUIRE(countKind(session, sim::NoticeKind::MineOperational) == 1);
}

// The text of the only rejection in the log.
std::string onlyRejection(const game::Session& session) {
    REQUIRE(countKind(session, sim::NoticeKind::CommandRejected) == 1);
    return textOfKind(session, sim::NoticeKind::CommandRejected);
}

}  // namespace

TEST_CASE("Mothballing and reopening a mine each say what happened and where",
          "[session][playtest4]") {
    const sim::Content content = raceContent();
    game::Session session(content, 1, soloSetup());
    sim::PlotId plot;
    openAMine(session, content, plot);
    const std::string where = "plot #" + std::to_string(plot.value() + 1);

    session.submit(sim::SetMineState{{}, sim::MineId(0), 0});
    session.stepOneDay();
    REQUIRE(countKind(session, sim::NoticeKind::MineMothballed) == 1);
    CHECK(countKind(session, sim::NoticeKind::MineReopened) == 0);
    CHECK(textOfKind(session, sim::NoticeKind::MineMothballed) ==
          game::text(content, "notice.mine_mothballed", {{"plot", where}}));
    CHECK(textOfKind(session, sim::NoticeKind::MineMothballed).find(where) != std::string::npos);

    session.submit(sim::SetMineState{{}, sim::MineId(0), 1});
    session.stepOneDay();
    CHECK(countKind(session, sim::NoticeKind::MineMothballed) == 1);
    REQUIRE(countKind(session, sim::NoticeKind::MineReopened) == 1);
    CHECK(textOfKind(session, sim::NoticeKind::MineReopened) ==
          game::text(content, "notice.mine_reopened", {{"plot", where}}));
    CHECK(textOfKind(session, sim::NoticeKind::MineReopened).find(where) != std::string::npos);
}

TEST_CASE("A refused order is described as the order it was", "[session][playtest4]") {
    const sim::Content content = raceContent();
    game::Session session(content, 7, test::setup());
    session.setAutoPause(neverPause());

    SECTION("a delivery to the program is not called a sale") {
        // Stage 1 needs iron; the company has none to hand over.
        session.submit(sim::DeliverToProgram{{}, sim::ResourceId(0), 500});
        session.stepOneDay();
        const game::LogEntry* entry = firstOfKind(session, sim::NoticeKind::CommandRejected);
        REQUIRE(entry != nullptr);
        CHECK(entry->notice.order == sim::RejectedOrder::Delivery);
        CHECK(entry->notice.resource == sim::ResourceId(0));
        CHECK(onlyRejection(session) ==
              "Couldn't deliver Iron to the program: " +
                  game::rejectReasonClause(content, sim::RejectReason::InsufficientInventory) +
                  ".");
        CHECK(onlyRejection(session).find("sale") == std::string::npos);
    }
    SECTION("an order refused because the game is over reads the same whatever it was") {
        const std::string expected =
            "Couldn't carry out the order: " +
            game::rejectReasonClause(content, sim::RejectReason::GameEnded) + ".";
        sim::Notice refused;
        refused.kind = sim::NoticeKind::CommandRejected;
        refused.company = game::Session::kPlayer;
        refused.reason = sim::RejectReason::GameEnded;
        CHECK(game::describeNotice(content, session.view(), refused) == expected);
        // The fields of the order it was do not change the sentence.
        refused.order = sim::RejectedOrder::Delivery;
        refused.resource = sim::ResourceId(0);
        refused.tonnes = 500;
        CHECK(game::describeNotice(content, session.view(), refused) == expected);
        refused.order = sim::RejectedOrder::StageCompletion;
        CHECK(game::describeNotice(content, session.view(), refused) == expected);
        refused.order = sim::RejectedOrder::Other;
        refused.loan = sim::LoanId(0);
        refused.amount = sim::Money::fromUnits(100);
        refused.plot = sim::PlotId(2);
        CHECK(game::describeNotice(content, session.view(), refused) == expected);
    }
    SECTION("a delivery the stage does not need says so") {
        session.submit(sim::DeliverToProgram{{}, sim::ResourceId(1), 10});
        session.stepOneDay();
        CHECK(onlyRejection(session) ==
              "Couldn't deliver Copper to the program: " +
                  game::rejectReasonClause(content, sim::RejectReason::ProgramNotNeeded) + ".");
    }
    SECTION("completing a stage that is not ready names the stage, not just an order") {
        session.submit(sim::CompleteProgramStage{});
        session.stepOneDay();
        const game::LogEntry* entry = firstOfKind(session, sim::NoticeKind::CommandRejected);
        REQUIRE(entry != nullptr);
        CHECK(entry->notice.order == sim::RejectedOrder::StageCompletion);
        CHECK(entry->notice.reason == sim::RejectReason::ProgramStageNotReady);
        CHECK(onlyRejection(session) ==
              "Couldn't complete the program stage: " +
                  game::rejectReasonClause(content, sim::RejectReason::ProgramStageNotReady) + ".");
    }
    SECTION("a sale is still a sale") {
        session.submit(sim::SellOre{{}, sim::ResourceId(0), 500});
        session.stepOneDay();
        const game::LogEntry* entry = firstOfKind(session, sim::NoticeKind::CommandRejected);
        REQUIRE(entry != nullptr);
        CHECK(entry->notice.order == sim::RejectedOrder::Other);
        CHECK(onlyRejection(session) ==
              "Couldn't carry out the sale of Iron: " +
                  game::rejectReasonClause(content, sim::RejectReason::InsufficientInventory) +
                  ".");
    }
    SECTION("a sell rule the sim turns away keeps the wording it had") {
        sim::SetSellPolicy policy;
        policy.resource = sim::ResourceId(0);
        policy.enabled = 1;
        policy.reserveTonnes = -1;
        session.submit(policy);
        session.stepOneDay();
        CHECK(onlyRejection(session).starts_with("Couldn't carry out the sale of Iron: "));
    }
}

// ---- spec 011: falling behind stops the clock; the day's stops are kept ----

TEST_CASE("The falling-behind stop rule has a stable key and is on by default",
          "[session][spec011]") {
    CHECK(game::pauseOnKey(game::PauseOn::FellBehind) == "fell_behind");
    const game::AutoPause defaults = game::AutoPause::defaults();
    CHECK(defaults.isEnabled(game::PauseOn::FellBehind));

    // Both pace notices are the reason, for the player's own company only.
    for (const sim::NoticeKind kind :
         {sim::NoticeKind::ProgramFellBehind, sim::NoticeKind::ProgramPastDeadline}) {
        sim::Notice pace = sim::makeNotice(kind, game::Session::kPlayer, sim::PlotId());
        CHECK(game::AutoPause::reasonFor(pace, game::Session::kPlayer) ==
              game::PauseOn::FellBehind);
        CHECK(defaults.shouldPause(pace, game::Session::kPlayer));
        pace.company = kRival;
        CHECK_FALSE(game::AutoPause::reasonFor(pace, game::Session::kPlayer).has_value());
        CHECK_FALSE(defaults.shouldPause(pace, game::Session::kPlayer));
    }

    // Race stops are the rival's progress and falling behind; nothing else.
    for (std::size_t i = 0; i < game::kPauseOnCount; ++i) {
        const auto reason = static_cast<game::PauseOn>(i);
        CHECK(game::isRaceStop(reason) ==
              (reason == game::PauseOn::RivalProgram || reason == game::PauseOn::FellBehind));
    }

    // The rule is written out with the others and comes back switched off.
    game::AutoPause rules = game::AutoPause::defaults();
    CHECK(rules.toText().find("fell_behind=1\n") != std::string::npos);
    rules.set(game::PauseOn::FellBehind, false);
    CHECK(rules.toText().find("fell_behind=0\n") != std::string::npos);
    game::AutoPause restored = game::AutoPause::defaults();
    CHECK(restored.fromText(rules.toText()));
    CHECK_FALSE(restored.isEnabled(game::PauseOn::FellBehind));
    CHECK(restored.enabled == rules.enabled);
}

TEST_CASE("A settings file from before the falling-behind rule reads it as on",
          "[session][spec011]") {
    // As spec 010 wrote it: eleven keys, no fell_behind.
    const std::string old =
        "insolvency=1\nbankrupt=1\nauction_won=0\nauction_lost=1\n"
        "survey_complete=0\nmine_operational=1\nmine_found_nothing=1\n"
        "idle_mine=0\nresearch_complete=0\nmine_losing=0\nrival_program=0\n";
    game::AutoPause migrated = game::AutoPause::defaults();
    CHECK(migrated.fromText(old));
    CHECK(migrated.isEnabled(game::PauseOn::FellBehind));

    game::AutoPause expected = game::AutoPause::defaults();
    expected.set(game::PauseOn::AuctionWon, false);
    expected.set(game::PauseOn::SurveyComplete, false);
    expected.set(game::PauseOn::MineOperational, true);
    expected.set(game::PauseOn::IdleMine, false);
    expected.set(game::PauseOn::ResearchComplete, false);
    expected.set(game::PauseOn::MineLosing, false);
    expected.set(game::PauseOn::RivalProgram, false);
    CHECK(migrated.enabled == expected.enabled);
}

namespace {

// Day 2 of a race, level, with the rival about to start stage 1's research:
// the next tick puts the player behind.
std::vector<std::uint8_t> eveOfFallingBehind(const sim::Content& content) {
    sim::WorldState world = raceWorld(content);
    world.companies[kRival.index()].cash = sim::Money::fromUnits(20'000'000);
    return raceSave(content, world,
                    {sim::StartResearch{kRival, content.program.stages[0].research}});
}

std::size_t markedLines(const game::Session& session) {
    return static_cast<std::size_t>(
        std::count_if(session.log().begin(), session.log().end(),
                      [](const game::LogEntry& entry) { return entry.stoppedClock; }));
}

}  // namespace

TEST_CASE("Falling behind stops the clock under the default rules", "[session][spec011]") {
    const sim::Content content = raceContent();
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(eveOfFallingBehind(content)));
    const sim::Day eve = session.day();
    session.setSpeed(game::Speed::Fastest);

    SECTION("under the defaults both stops of the day are kept, in log order, and marked") {
        CHECK(session.advance(1.0) == 1);
        CHECK(session.speed() == game::Speed::Paused);
        CHECK(session.day() == eve + 1);
        const std::vector<sim::Notice>& stops = session.stopsToday();
        REQUIRE(stops.size() == 2);
        CHECK(stops[0].kind == sim::NoticeKind::ProgramResearchStarted);
        CHECK(stops[0].company == kRival);
        CHECK(stops[1].kind == sim::NoticeKind::ProgramFellBehind);
        CHECK(stops[1].company == game::Session::kPlayer);
        CHECK(stops[1].rival == kRival);
        CHECK(markedLines(session) == 2);
        CHECK(firstOfKind(session, sim::NoticeKind::ProgramResearchStarted)->stoppedClock);
        const game::LogEntry* fell = firstOfKind(session, sim::NoticeKind::ProgramFellBehind);
        REQUIRE(fell != nullptr);
        CHECK(fell->stoppedClock);
        CHECK(fell->day == eve);

        // They stay while the clock is stopped and clear when it starts.
        CHECK(session.advance(5.0) == 0);
        CHECK(session.stopsToday().size() == 2);
        session.setSpeed(game::Speed::Fastest);
        CHECK(session.stopsToday().empty());
        CHECK(session.advance(1.0) == 20);
        CHECK(countKind(session, sim::NoticeKind::ProgramFellBehind) == 1);
        CHECK(markedLines(session) == 2);  // the marks stay in the log
    }
    SECTION("under the falling-behind rule alone only its line stopped the clock") {
        game::AutoPause rules = neverPause();
        rules.set(game::PauseOn::FellBehind, true);
        session.setAutoPause(rules);
        CHECK(session.advance(1.0) == 1);
        REQUIRE(session.stopsToday().size() == 1);
        CHECK(session.stopsToday().front().kind == sim::NoticeKind::ProgramFellBehind);
        CHECK(markedLines(session) == 1);
        CHECK_FALSE(firstOfKind(session, sim::NoticeKind::ProgramResearchStarted)->stoppedClock);
    }
    SECTION("with the rule off the log still says so and the clock runs on") {
        game::AutoPause rules = game::AutoPause::defaults();
        rules.set(game::PauseOn::FellBehind, false);
        rules.set(game::PauseOn::RivalProgram, false);
        session.setAutoPause(rules);
        CHECK(session.advance(1.0) == 20);
        CHECK(session.stopsToday().empty());
        CHECK(countKind(session, sim::NoticeKind::ProgramFellBehind) == 1);
        CHECK_FALSE(firstOfKind(session, sim::NoticeKind::ProgramFellBehind)->stoppedClock);
    }
}

TEST_CASE("The day's stops are cleared by a load and by the next day", "[session][spec011]") {
    const sim::Content content = raceContent();
    game::Session session(content, 7, test::setup());
    const std::vector<std::uint8_t> eve = eveOfFallingBehind(content);
    REQUIRE(session.load(eve));
    session.setSpeed(game::Speed::Fastest);
    CHECK(session.advance(1.0) == 1);
    REQUIRE_FALSE(session.stopsToday().empty());

    SECTION("a load") {
        REQUIRE(session.load(eve));
        CHECK(session.stopsToday().empty());
        CHECK(markedLines(session) == 0);
    }
    SECTION("a step onto a day that raises nothing") {
        // Still stopped, but the stops shown are the new day's: none.
        session.stepOneDay();
        CHECK(session.speed() == game::Speed::Paused);
        CHECK(session.stopsToday().empty());
        CHECK(markedLines(session) == 2);
    }
}

TEST_CASE("The day the game ends puts the ending first among the day's stops",
          "[session][spec011]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    putStageInReach(content, world.companies[kRival.index()], 2);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world, {sim::CompleteProgramStage{kRival}})));

    session.setSpeed(game::Speed::Fastest);
    CHECK(session.advance(1.0) == 1);

    REQUIRE(session.gameOver());
    // The rival's last stage matched a rule and came first in the log; the
    // founding ended the game and leads.
    const std::vector<sim::Notice>& stops = session.stopsToday();
    REQUIRE(stops.size() == 2);
    CHECK(stops[0].kind == sim::NoticeKind::ProgramFounded);
    CHECK(stops[0].company == kRival);
    CHECK(stops[1].kind == sim::NoticeKind::ProgramStageComplete);
    // Both lines stopped the clock, so both are marked.
    CHECK(firstOfKind(session, sim::NoticeKind::ProgramFounded)->stoppedClock);
    CHECK(firstOfKind(session, sim::NoticeKind::ProgramStageComplete)->stoppedClock);
    CHECK(markedLines(session) == 2);
    // The finished game's clock cannot be restarted, so the reason stays.
    session.setSpeed(game::Speed::Fastest);
    session.togglePause();
    CHECK(session.stopsToday().size() == 2);
}

TEST_CASE("The player's bankruptcy is one stop, not two, when its rule is on",
          "[session][spec011]") {
    const sim::Content content = raceContent();
    sim::WorldState world = raceWorld(content);
    world.companies[game::Session::kPlayer.index()].cash = sim::Money::fromUnits(-50'000'000);
    game::Session session(content, 7, test::setup());
    REQUIRE(session.load(raceSave(content, world)));
    game::AutoPause rules = neverPause();
    rules.set(game::PauseOn::Bankrupt, true);
    session.setAutoPause(rules);

    session.setSpeed(game::Speed::Fastest);
    for (int i = 0; i < 20 && !session.gameOver(); ++i) {
        session.advance(1.0);
    }

    REQUIRE(session.gameOver());
    REQUIRE(session.stopsToday().size() == 1);
    CHECK(session.stopsToday().front().kind == sim::NoticeKind::Bankrupt);
    CHECK(firstOfKind(session, sim::NoticeKind::Bankrupt)->stoppedClock);
}
