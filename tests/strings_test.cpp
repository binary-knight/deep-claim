// The prose catalogue (spec 002): template filling, coverage of the shipped
// data, loader validation, and parity between the draft and the catalogue.
#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/view.h"

#include "content/loader.h"
#include "game/format.h"
#include "test_content.h"
#include "test_strings.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
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
    CHECK(text.find(game::rejectReasonText(content, sim::RejectReason::InsufficientFunds)) !=
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
        content::Sources s = good;
        const std::string original =
            game::text(*content::loadFromStrings(good).content, "notice.auction_lost");
        s.strings = replaceOnce(s.strings, original, "Outbid on {plot");
        const auto result = content::loadFromStrings(s);
        CHECK_FALSE(result.ok());
        CHECK(hasError(result, "notice.auction_lost", "malformed"));
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

TEST_CASE("The draft and the shipped catalogue agree on keys and placeholders", "[strings][data]") {
    const content::LoadResult result = content::loadFromDirectory(DC_DATA_DIR);
    REQUIRE(result.ok());
    const std::map<std::string, std::string>& shipped = result.content->strings;

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
