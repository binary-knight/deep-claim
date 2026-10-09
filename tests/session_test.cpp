#include "game/session.h"

#include "sim/commands.h"
#include "sim/fixed.h"
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
#include <cstdint>
#include <optional>
#include <span>
#include <sstream>
#include <string>
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
        const std::optional<sim::Notice> why = session.takePausedBy();
        REQUIRE(why.has_value());
        CHECK(why->kind == sim::NoticeKind::SurveyComplete);
        CHECK_FALSE(session.takePausedBy().has_value());  // handed over once
        // Resuming works and does not re-pause on the same notice.
        session.setSpeed(game::Speed::Fastest);
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
    const std::optional<sim::Notice> why = session.takePausedBy();
    REQUIRE(why.has_value());
    CHECK(why->kind == sim::NoticeKind::SurveyComplete);
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
    for (int i = 0; i < 100 && session.speed() != game::Speed::Paused; ++i) {
        session.advance(1.0);
    }
    CHECK(session.speed() == game::Speed::Paused);
    CHECK(session.day() == content.techs[0].durationDays + 1);
    const std::optional<sim::Notice> why = session.takePausedBy();
    REQUIRE(why.has_value());
    CHECK(why->kind == sim::NoticeKind::ResearchComplete);
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
