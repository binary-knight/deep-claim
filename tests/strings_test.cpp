// The prose catalogue (spec 002): template filling, coverage of the shipped
// data, loader validation, and parity between the draft and the catalogue.
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/view.h"

#include "content/loader.h"
#include "game/format.h"
#include "test_content.h"
#include "test_strings.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string> placeholdersOf(std::string_view text) {
    std::vector<std::string> names;
    std::size_t pos = 0;
    while ((pos = text.find('{', pos)) != std::string_view::npos) {
        const std::size_t close = text.find('}', pos);
        if (close == std::string_view::npos) {
            break;
        }
        names.emplace_back(text.substr(pos + 1, close - pos - 1));
        pos = close + 1;
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool hasError(const content::LoadResult& result, std::string_view path,
              std::string_view messagePart) {
    return std::any_of(result.errors.begin(), result.errors.end(), [&](const auto& e) {
        return e.file == content::kStringsFile && e.path == path &&
               e.message.find(messagePart) != std::string::npos;
    });
}

content::Sources shippedSources() {
    content::Sources s;
    const std::string dir = std::string(DC_DATA_DIR) + "/";
    s.resources = test::readTextFile(dir + content::kResourcesFile);
    s.techs = test::readTextFile(dir + content::kTechsFile);
    s.regions = test::readTextFile(dir + content::kRegionsFile);
    s.events = test::readTextFile(dir + content::kEventsFile);
    s.balance = test::readTextFile(dir + content::kBalanceFile);
    s.ai = test::readTextFile(dir + content::kAiFile);
    s.program = test::readTextFile(dir + content::kProgramFile);
    s.strings = test::readTextFile(dir + content::kStringsFile);
    return s;
}

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

}  // namespace

TEST_CASE("fill substitutes named placeholders and leaves unknown ones visible", "[strings]") {
    CHECK(game::fill("Sold {tonnes} of {resource} for {amount}",
                     {{"tonnes", "1,200 t"}, {"resource", "Iron ore"}, {"amount", "$60k"}}) ==
          "Sold 1,200 t of Iron ore for $60k");
    CHECK(game::fill("{a}{a} and {b}", {{"a", "x"}}) == "xx and {b}");
    CHECK(game::fill("no placeholders", {}) == "no placeholders");
    CHECK(game::fill("broken {brace", {{"brace", "x"}}) == "broken {brace");
    CHECK(game::fill("{} empty", {{"", "z"}}) == "z empty");
    CHECK(game::fill("", {{"a", "x"}}).empty());
}

TEST_CASE("Missing keys show as the key and plurals pick a variant", "[strings]") {
    sim::Content content = test::makeContent();
    CHECK(game::text(content, "notice.plot_sold") == "[notice.plot_sold]");
    test::addStrings(content);
    CHECK(game::text(content, "notice.plot_sold").find('{') != std::string::npos);
    CHECK(game::textPlural(content, "ui.auction", 1, {{"day", "d"}, {"bids", "1"}}) !=
          game::textPlural(content, "ui.auction", 2, {{"day", "d"}, {"bids", "2"}}));
}

TEST_CASE("Every notice kind and reject reason has shipped prose", "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& content = *result.content;

    sim::CompanyView view;
    sim::CompanyPublicView me;
    me.id = sim::CompanyId(0);
    me.name = "Deep Claim Mining";
    view.companies.push_back(me);
    for (int i = 0;
         std::string_view(sim::noticeKindName(static_cast<sim::NoticeKind>(i))) != "unknown"; ++i) {
        sim::Notice notice;
        notice.kind = static_cast<sim::NoticeKind>(i);
        notice.company = sim::CompanyId(0);
        notice.plot = sim::PlotId(3);
        notice.resource = sim::ResourceId(0);
        notice.tech = sim::TechId(0);
        notice.event = sim::EventId(0);
        notice.amount = sim::Money::fromUnits(1'000);
        notice.tonnes = 5;
        const std::string text = game::describeNotice(content, view, notice);
        INFO(sim::noticeKindName(notice.kind) << ": " << text);
        CHECK_FALSE(text.empty());
        CHECK(text.front() != '[');
        CHECK(text.find('{') == std::string::npos);
    }
    for (int i = 0;
         std::string_view(sim::rejectReasonName(static_cast<sim::RejectReason>(i))) != "unknown";
         ++i) {
        const std::string text = game::rejectReasonText(content, static_cast<sim::RejectReason>(i));
        INFO(sim::rejectReasonName(static_cast<sim::RejectReason>(i)) << ": " << text);
        CHECK_FALSE(text.empty());
        CHECK(text.front() != '[');
    }
    // Rejections name what was ordered.
    sim::Notice rejected;
    rejected.kind = sim::NoticeKind::CommandRejected;
    rejected.company = sim::CompanyId(0);
    rejected.plot = sim::PlotId(3);
    rejected.amount = sim::Money::fromUnits(1'180'000);
    rejected.reason = sim::RejectReason::InsufficientFunds;
    const std::string text = game::describeNotice(content, view, rejected);
    CHECK(text.find("#4") != std::string::npos);
    CHECK(text.find("$1.18M") != std::string::npos);
    CHECK(text.find(game::rejectReasonClause(content, sim::RejectReason::InsufficientFunds)) !=
          std::string::npos);

    for (const sim::EventDef& event : content.events) {
        INFO(event.id);
        CHECK_FALSE(event.name.empty());
        CHECK_FALSE(event.description.empty());
    }
    for (const sim::TechDef& tech : content.techs) {
        INFO(tech.id);
        CHECK_FALSE(tech.name.empty());
        CHECK_FALSE(tech.description.empty());
    }
    CHECK(content.techs[content.findTech("lithium_processing").index()].name ==
          "Lithium Processing");
}

TEST_CASE("The loader rejects a broken catalogue", "[strings][content]") {
    const content::Sources good = shippedSources();
    REQUIRE(content::loadFromStrings(good).ok());

    SECTION("missing key") {
        content::Sources s = good;
        s.strings = replaceOnce(s.strings, "\"notice.plot_sold\"", "\"notice.plot_sold_x\"");
        const auto result = content::loadFromStrings(s);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "notice.plot_sold", "missing"));
        CHECK(hasError(result, "notice.plot_sold_x", "unknown key"));
    }
    SECTION("placeholder the key may not use") {
        content::Sources s = good;
        const std::string original =
            game::text(*content::loadFromStrings(good).content, "notice.auction_lost");
        s.strings = replaceOnce(s.strings, original, "Outbid on {plot} by {company}");
        const auto result = content::loadFromStrings(s);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "notice.auction_lost", "{company}"));
    }
    SECTION("malformed braces") {
        const std::string original =
            game::text(*content::loadFromStrings(good).content, "notice.auction_lost");
        for (const char* broken : {"Outbid on {plot", "Outbid on {plot}}", "stray } here",
                                   "}{plot} first", "empty {} name", "{Plot} upper"}) {
            content::Sources s = good;
            s.strings = replaceOnce(s.strings, original, broken);
            const auto result = content::loadFromStrings(s);
            INFO(broken);
            CHECK_FALSE(result.ok());
            CHECK(hasError(result, "notice.auction_lost", "malformed"));
        }
    }
    SECTION("empty text") {
        content::Sources s = good;
        const std::string original =
            game::text(*content::loadFromStrings(good).content, "ui.no_events");
        s.strings = replaceOnce(s.strings, "\"" + original + "\"", "\"\"");
        const auto result = content::loadFromStrings(s);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "ui.no_events", "empty"));
    }
    SECTION("a tech without a name") {
        content::Sources s = good;
        s.strings = replaceOnce(s.strings, "\"tech.bulk_logistics.name\"", "\"tech.bulk.name\"");
        const auto result = content::loadFromStrings(s);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "tech.bulk_logistics.name", "missing"));
        CHECK(hasError(result, "tech.bulk.name", "unknown key"));
    }
    SECTION("not an object") {
        content::Sources s = good;
        s.strings = "[]";
        CHECK_FALSE(content::loadFromStrings(s).ok());
    }
}

TEST_CASE("Every rejection variant renders with the figures it is given", "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Content& content = *result.content;
    sim::CompanyView view;
    sim::CompanyPublicView me;
    me.id = sim::CompanyId(0);
    me.name = "Deep Claim Mining";
    view.companies.push_back(me);

    struct Variant {
        const char* name;
        sim::PlotId plot;
        sim::ResourceId resource;
        sim::TechId tech;
        sim::LoanId loan;
        sim::Money amount;
        const char* mustContain;
    };
    const sim::Money million = sim::Money::fromUnits(1'000'000);
    const Variant variants[] = {
        {"plot", sim::PlotId(2), {}, {}, {}, sim::Money::zero(), "#3"},
        {"plot_amount", sim::PlotId(2), {}, {}, {}, million, "$1.00M"},
        {"sale",
         {},
         sim::ResourceId(0),
         {},
         {},
         sim::Money::zero(),
         content.resources[0].name.c_str()},
        {"research", {}, {}, sim::TechId(1), {}, sim::Money::zero(), content.techs[1].name.c_str()},
        {"loan", {}, {}, {}, sim::LoanId(0), sim::Money::zero(), ""},
        {"loan_amount", {}, {}, {}, sim::LoanId(0), million, "$1.00M"},
        {"generic", {}, {}, {}, {}, sim::Money::zero(), ""},
        {"amount", {}, {}, {}, {}, million, "$1.00M"},
    };
    for (const Variant& v : variants) {
        sim::Notice n;
        n.kind = sim::NoticeKind::CommandRejected;
        n.company = sim::CompanyId(0);
        n.plot = v.plot;
        n.resource = v.resource;
        n.tech = v.tech;
        n.loan = v.loan;
        n.amount = v.amount;
        n.reason = sim::RejectReason::InsufficientFunds;
        const std::string text = game::describeNotice(content, view, n);
        INFO(v.name << ": " << text);
        CHECK_FALSE(text.empty());
        CHECK(text.front() != '[');
        CHECK(text.find('{') == std::string::npos);
        CHECK(text.find(v.mustContain) != std::string::npos);
        CHECK(text.find(game::rejectReasonClause(content, n.reason)) != std::string::npos);
    }
}

TEST_CASE("Rejections read as one sentence and every notice starts with a capital",
          "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const sim::Content& content = *result.content;
    sim::CompanyView view;
    sim::CompanyPublicView me;
    me.id = sim::CompanyId(0);
    me.name = "Deep Claim Mining";
    view.companies.push_back(me);

    const auto isUpper = [](char c) { return c >= 'A' && c <= 'Z'; };
    for (int r = 1;
         std::string_view(sim::rejectReasonName(static_cast<sim::RejectReason>(r))) != "unknown";
         ++r) {
        sim::Notice n;
        n.kind = sim::NoticeKind::CommandRejected;
        n.company = sim::CompanyId(0);
        n.amount = sim::Money::fromUnits(1'000'000);
        n.reason = static_cast<sim::RejectReason>(r);
        for (int shape = 0; shape < 4; ++shape) {
            n.plot = shape == 1 ? sim::PlotId(2) : sim::PlotId();
            n.loan = shape == 2 ? sim::LoanId(0) : sim::LoanId();
            n.tech = shape == 3 ? sim::TechId(0) : sim::TechId();
            const std::string text = game::describeNotice(content, view, n);
            INFO(text);
            CHECK(text.find("..") == std::string::npos);
            CHECK(text.back() == '.');
            CHECK(isUpper(text.front()));
            const std::size_t colon = text.find(": ");
            REQUIRE(colon != std::string::npos);
            CHECK_FALSE(isUpper(text[colon + 2]));  // one sentence: no capital after the colon
            // Exactly one full stop that ends a sentence.
            CHECK(text.find(". ") == std::string::npos);
        }
    }
    // The clause form of a reason: no full stop, lower-case opening.
    CHECK(game::rejectReasonClause(content, sim::RejectReason::LoanLimitExceeded) ==
          "the bank will not lend that much on our remaining credit");
    CHECK(game::capitalised("plot #3 is up.") == "Plot #3 is up.");
    CHECK(game::capitalised("$5 fee.") == "$5 fee.");
    CHECK(game::capitalised("").empty());

    for (int i = 0;
         std::string_view(sim::noticeKindName(static_cast<sim::NoticeKind>(i))) != "unknown"; ++i) {
        sim::Notice notice;
        notice.kind = static_cast<sim::NoticeKind>(i);
        notice.company = sim::CompanyId(0);
        notice.plot = sim::PlotId(27);
        notice.resource = sim::ResourceId(0);
        notice.tech = sim::TechId(0);
        notice.event = sim::EventId(0);
        notice.amount = sim::Money::fromUnits(1'000);
        notice.tonnes = 5;
        const std::string text = game::describeNotice(content, view, notice);
        INFO(text);
        const char first = text.front();
        CHECK_FALSE((first >= 'a' && first <= 'z'));
    }
    sim::Notice listed;
    listed.kind = sim::NoticeKind::PlotListed;
    listed.plot = sim::PlotId(27);
    listed.amount = sim::Money::fromUnits(1'000'000);
    CHECK(game::describeNotice(content, view, listed).starts_with("Plot #28 is up for auction."));
}

TEST_CASE("Program notices name the stage and the mine notices name the new state",
          "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& content = *result.content;
    sim::CompanyView view;
    sim::CompanyPublicView rival;
    rival.id = sim::CompanyId(0);
    rival.name = "Northern Ore";
    view.companies.push_back(rival);

    // Both notices carry the stage, counting from 1, in `tonnes` (spec 010).
    for (const sim::NoticeKind kind :
         {sim::NoticeKind::ProgramStageComplete, sim::NoticeKind::ProgramResearchStarted}) {
        sim::Notice notice;
        notice.kind = kind;
        notice.company = sim::CompanyId(0);
        notice.tech = content.findTech("program_stage_2");
        notice.amount = sim::Money::fromUnits(15'000'000);
        notice.tonnes = 2;
        const std::string text = game::describeNotice(content, view, notice);
        INFO(text);
        CHECK(text.find("Northern Ore") != std::string::npos);
        CHECK(text.find("stage 2") != std::string::npos);
        CHECK(text.find('{') == std::string::npos);
    }

    // The two keys that will replace the vague "changed operating status".
    const std::string mothballed =
        game::text(content, "notice.mine_mothballed", {{"plot", "plot #4"}});
    const std::string reopened = game::text(content, "notice.mine_reopened", {{"plot", "plot #4"}});
    CHECK(mothballed.find("plot #4") != std::string::npos);
    CHECK(mothballed.find("mothballed") != std::string::npos);
    CHECK(reopened.find("plot #4") != std::string::npos);
    CHECK(mothballed != reopened);
    CHECK(mothballed.front() != '[');
    CHECK(reopened.front() != '[');
}

TEST_CASE("The race strip, the Program tab and the end screen have their text", "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& content = *result.content;

    CHECK(game::text(content, "ui.race_deadline", {{"years", "29"}, {"days", "359"}})
              .find("29 y 359 d") != std::string::npos);
    CHECK(game::text(content, "ui.race_stage", {{"done", "1"}, {"total", "3"}})
              .starts_with("1 of 3"));
    CHECK(game::text(content, "ui.program_stage_heading",
                     {{"number", "3"}, {"name", game::text(content, "ui.program_stage_3_name")}}) ==
          "Stage 3: Deep space payload");
    // The stage project's name shares a 260dp table cell (spec 010).
    for (const char* id : {"program_stage_1", "program_stage_2", "program_stage_3"}) {
        const sim::TechDef& tech = content.techs[content.findTech(id).index()];
        INFO(tech.name);
        CHECK(tech.name.size() <= 39);
        CHECK(tech.description.find("deep-space") == std::string::npos);
    }
    // Every screen step 1 adds has its text in the catalogue.
    for (const char* key :
         {"ui.pause_on.rival_program", "ui.race_ahead", "ui.race_behind", "ui.race_level",
          "ui.program_deliver", "ui.program_complete", "ui.program_cover_costs_warning",
          "ui.end_founded_title", "ui.end_founded_body", "ui.end_partner_title",
          "ui.end_partner_body", "ui.end_lost_title", "ui.end_lost_body", "ui.end_deadline_title",
          "ui.end_deadline_body", "ui.end_bankrupt_title", "ui.end_bankrupt_body",
          "ui.end_new_game"}) {
        INFO(key);
        CHECK(game::text(content, key).front() != '[');
    }
}

TEST_CASE("Time from now reads in years and days, or as today", "[strings][data][format]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& content = *result.content;

    // The owner's form for the status strip (spec 011): a 360-day year.
    CHECK(game::formatTimeFromNow(content, 6 * sim::kDaysPerYear + 120) == "in 6 y 120 d");
    // Whole years keep the day count, as the deadline item does.
    CHECK(game::formatTimeFromNow(content, 2 * sim::kDaysPerYear) == "in 2 y 0 d");
    CHECK(game::formatTimeFromNow(content, sim::kDaysPerYear - 1) == "in 359 d");
    CHECK(game::formatTimeFromNow(content, 1) == "in 1 d");
    CHECK(game::formatTimeFromNow(content, 0) == "today");
}

TEST_CASE("The race notices name the rival and give the days as dates", "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& content = *result.content;
    sim::CompanyView view;
    for (const char* name : {"Deep Claim Mining", "Northfield Resources"}) {
        sim::CompanyPublicView company;
        company.id = sim::CompanyId(static_cast<sim::CompanyId::Value>(view.companies.size()));
        company.name = name;
        view.companies.push_back(company);
    }
    view.deadlineDay = 30 * sim::kDaysPerYear;

    sim::Notice behind;
    behind.kind = sim::NoticeKind::ProgramFellBehind;
    behind.company = sim::CompanyId(0);
    behind.rival = sim::CompanyId(1);
    behind.foundingDay = 16 * sim::kDaysPerYear + 4 * sim::kDaysPerMonth + 2;
    behind.rivalFoundingDay = 12 * sim::kDaysPerYear;
    const std::string fell = game::describeNotice(content, view, behind);
    INFO(fell);
    CHECK(fell.starts_with("Northfield Resources"));
    CHECK(fell.find(game::formatDay(*behind.foundingDay)) != std::string::npos);
    CHECK(fell.find("Y17 M5 D3") != std::string::npos);
    CHECK(fell.find(game::formatDay(*behind.rivalFoundingDay)) != std::string::npos);
    CHECK(fell.find('{') == std::string::npos);

    sim::Notice late;
    late.kind = sim::NoticeKind::ProgramPastDeadline;
    late.company = sim::CompanyId(0);
    late.foundingDay = 31 * sim::kDaysPerYear;
    const std::string past = game::describeNotice(content, view, late);
    INFO(past);
    CHECK(past.find(game::formatDay(*late.foundingDay)) != std::string::npos);
    CHECK(past.find(game::formatDay(view.deadlineDay)) != std::string::npos);
    CHECK(past.find('{') == std::string::npos);

    // A notice without its days still reads, with a mark where a day would be.
    sim::Notice bare;
    bare.kind = sim::NoticeKind::ProgramFellBehind;
    const std::string unknown = game::describeNotice(content, view, bare);
    CHECK(unknown.find('?') != std::string::npos);
    CHECK(unknown.find('{') == std::string::npos);
}

TEST_CASE("The stop panel and the race displays have their text", "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    INFO(content::formatErrors(result.errors));
    REQUIRE(result.ok());
    const sim::Content& content = *result.content;

    CHECK(game::text(content, "ui.stop_more", {{"count", "2"}}) == "+2 more");
    CHECK(game::textPlural(content, "ui.race_behind_by", 1374, {{"days", "1,374"}})
              .find("1,374 days") != std::string::npos);
    CHECK(game::textPlural(content, "ui.race_ahead_by", 1, {{"days", "1"}}).find("1 day") !=
          std::string::npos);
    // Every key spec 011 adds for the session and the client (tasks 009-014).
    for (const char* key : {"ui.pause_on.fell_behind",
                            "ui.stop_heading",
                            "ui.stop_race_heading",
                            "ui.stop_race_days",
                            "ui.stop_more",
                            "ui.stop_go_to",
                            "ui.stop_for_this",
                            "ui.race_behind_by.one",
                            "ui.race_behind_by.many",
                            "ui.race_ahead_by.one",
                            "ui.race_ahead_by.many",
                            "ui.race_cannot_found",
                            "ui.race_founding_day",
                            "ui.race_research_due",
                            "ui.race_rival_could_found",
                            "ui.time_from_now",
                            "ui.time_from_now_days",
                            "ui.time_from_now_today",
                            "ui.program_last_start",
                            "ui.program_last_start_passed",
                            "ui.program_lab_busy",
                            "ui.program_where_from",
                            "ui.program_from_regions",
                            "ui.program_from_region_item",
                            "ui.program_from_research",
                            "ui.program_from_mines",
                            "ui.program_from_mine_item",
                            "ui.program_from_no_mines",
                            "ui.program_from_surveys",
                            "ui.need_pace_title",
                            "ui.need_pace_passed_title",
                            "ui.need_pace_sub",
                            "ui.need_ore_title",
                            "ui.need_ore_sub",
                            "ui.plot_mark_may_hold",
                            "ui.plot_mark_survey_shows",
                            "ui.bid_stands",
                            "ui.bid_replaces"}) {
        INFO(key);
        const std::string line = game::text(content, key);
        CHECK(line.front() != '[');
        CHECK(line.find("deep-space") == std::string::npos);
        CHECK(line.find("deep space") == std::string::npos);
    }
    // The cover-costs warning says program stock is sold last (BR-011), and
    // the end screen counts open mines (BR-012).
    CHECK(game::text(content, "ui.program_cover_costs_warning").find("last") != std::string::npos);
    CHECK(game::text(content, "ui.end_mines") == "Open mines");
}

TEST_CASE("A prose edit keeps the content fingerprint; a name edit moves it", "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    sim::Content edited = *result.content;
    const std::uint64_t before = sim::hashContent(edited);
    edited.strings["notice.plot_sold"] = "Changed wording for {company}, {plot}, {amount}.";
    edited.techs[0].description = "A different description.";
    CHECK(sim::hashContent(edited) == before);
    edited.techs[0].name = "Renamed";
    CHECK(sim::hashContent(edited) != before);
}

TEST_CASE("The draft and the shipped catalogue agree on keys and placeholders", "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const auto& shipped = result.content->strings;

    std::map<std::string, std::string> draft;
    std::ifstream file(std::string(DC_SOURCE_DIR) + "/tools/prose/draft.tsv");
    REQUIRE(file);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        const std::size_t tab = line.find('\t');
        REQUIRE(tab != std::string::npos);
        const std::string key = line.substr(0, tab);
        INFO(key);
        CHECK(draft.emplace(key, line.substr(tab + 1)).second);
    }
    CHECK(draft.size() == shipped.size());
    for (const auto& [key, text] : draft) {
        INFO(key);
        const auto it = shipped.find(key);
        REQUIRE(it != shipped.end());
        CHECK(placeholdersOf(text) == placeholdersOf(it->second));
        CHECK_FALSE(it->second.empty());
    }
    for (const auto& [key, text] : shipped) {
        INFO(key);
        CHECK(draft.contains(key));
    }
}
