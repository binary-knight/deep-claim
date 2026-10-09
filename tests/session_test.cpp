#include "game/session.h"

#include "sim/commands.h"
#include "sim/fixed.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include "game/format.h"
#include "test_content.h"
#include "test_strings.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>

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
        game::AutoPause rules = game::AutoPause::defaults();
        CHECK_FALSE(rules.isEnabled(game::PauseOn::SurveyComplete));  // off by default
        rules.set(game::PauseOn::SurveyComplete, true);
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
        const std::optional<sim::Notice> why = session.takePausedBy();
        REQUIRE(why.has_value());
        CHECK(why->kind == sim::NoticeKind::SurveyComplete);
        CHECK_FALSE(session.takePausedBy().has_value());  // handed over once
        // Resuming works and does not re-pause on the same notice.
        session.setSpeed(game::Speed::Fastest);
        CHECK(session.advance(1.0) == 20);
    }
    SECTION("survey complete does not pause under the defaults") {
        session.submit(sim::CommissionSurvey{{}, listed});
        session.setSpeed(game::Speed::Fastest);
        for (int i = 0; i < 3; ++i) {
            session.advance(1.0);
        }
        CHECK(session.speed() == game::Speed::Fastest);
        CHECK(session.day() == 60);
        CHECK_FALSE(session.takePausedBy().has_value());
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
    CHECK(text.find("survey_complete=0\n") != std::string::npos);

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
    const std::optional<sim::Notice> why = session.takePausedBy();
    REQUIRE(why.has_value());
    CHECK(why->kind == sim::NoticeKind::SurveyComplete);
}
