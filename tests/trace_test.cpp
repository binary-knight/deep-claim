#include "game/trace.h"

#include "sim/commands.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/types.h"
#include "sim/world_state.h"

#include "game/png.h"
#include "game/replay.h"
#include "game/session.h"
#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace {

sim::PlotId firstListed(const game::Session& session) {
    for (const sim::PlotView& plot : session.view().plots) {
        if (plot.listing == sim::PlotListing::Listed) {
            return plot.id;
        }
    }
    return {};
}

// A short game with a few orders at known days, traced into `out`.
void playTraced(game::Session& session, game::TraceWriter& trace) {
    session.setTrace(&trace);
    const sim::PlotId plot = firstListed(session);
    REQUIRE(plot.valid());
    session.submit(sim::CommissionSurvey{{}, plot});
    session.submit(sim::TakeLoan{{}, sim::Money::fromUnits(1'000'000)});
    session.stepOneDay();
    session.submit(sim::BidOnPlot{{}, plot, session.view().plots[plot.index()].askPrice});
    session.setSpeed(game::Speed::Fastest);
    for (int i = 0; i < 6; ++i) {
        session.advance(1.0);  // 120 days
    }
    session.submit(sim::BuildMine{{}, plot});
    for (int i = 0; i < 10; ++i) {
        session.advance(1.0);
    }
    session.submit(sim::StartResearch{{}, sim::TechId(0)});
    session.stepOneDay();
}

}  // namespace

TEST_CASE("Every command survives a trip through trace JSON", "[trace]") {
    const std::vector<sim::Command> commands = {
        sim::BidOnPlot{sim::CompanyId(0), sim::PlotId(7), sim::Money::fromCents(123'456'789)},
        sim::CommissionSurvey{sim::CompanyId(0), sim::PlotId(3)},
        sim::BuildMine{sim::CompanyId(0), sim::PlotId(2)},
        sim::SetMineState{sim::CompanyId(0), sim::MineId(1), 0},
        sim::SellOre{sim::CompanyId(0), sim::ResourceId(1), 1'500},
        sim::TakeLoan{sim::CompanyId(0), sim::Money::fromUnits(-5)},
        sim::RepayLoan{sim::CompanyId(0), sim::LoanId(4), sim::Money::fromUnits(250'000)},
        sim::StartResearch{sim::CompanyId(0), sim::TechId(2)},
    };
    for (const sim::Command& command : commands) {
        const std::string json = game::encodeCommand(command);
        sim::Command decoded;
        REQUIRE(game::decodeCommand(json, decoded));
        CHECK(decoded.index() == command.index());
        CHECK(game::encodeCommand(decoded) == json);
    }
    sim::Command decoded;
    CHECK_FALSE(game::decodeCommand("{\"cmd\":\"Teleport\"}", decoded));
    CHECK_FALSE(game::decodeCommand("{\"cmd\":\"BidOnPlot\",\"plot\":1}", decoded));
    CHECK_FALSE(game::decodeCommand("not json", decoded));
}

TEST_CASE("Trace lines parse and reject malformed records", "[trace]") {
    game::TraceRecord record;
    std::string error;
    CHECK(game::parseTraceLine("", record, error));
    CHECK(record.type.empty());
    CHECK(game::parseTraceLine(R"({"t":"new_game","seed":"18446744073709551615","content":"00ff",)"
                               R"("player":"Me","ais":["A","B"]})",
                               record, error));
    CHECK(record.seed == 18446744073709551615ULL);
    CHECK(record.contentHash == 0xFFU);
    CHECK(record.player == "Me");
    CHECK(record.ais == std::vector<std::string>{"A", "B"});
    CHECK(game::parseTraceLine(R"({"t":"day","day":12,"hash":"deadbeef"})", record, error));
    CHECK(record.day == 12);
    CHECK(record.hash == 0xDEADBEEFU);
    CHECK_FALSE(game::parseTraceLine(R"({"t":"day","day":12})", record, error));
    CHECK_FALSE(game::parseTraceLine(R"({"t":"day","day":-1,"hash":"00"})", record, error));
    CHECK_FALSE(game::parseTraceLine(R"({"day":1})", record, error));
    CHECK_FALSE(game::parseTraceLine("[1,2]", record, error));
    CHECK_FALSE(
        game::parseTraceLine(R"({"t":"command","day":0,"cmd":"BuildMine"})", record, error));
    CHECK(game::parseTraceLine(R"({"t":"command","day":0,"cmd":"BuildMine","plot":2})", record,
                               error));
    REQUIRE(record.command.has_value());
    CHECK(std::holds_alternative<sim::BuildMine>(record.command.value()));
}

TEST_CASE("A replayed trace rebuilds the same game", "[trace][replay]") {
    const sim::Content content = test::makeContent();
    std::ostringstream out;
    game::TraceWriter trace(out);
    const sim::Seed seed = 11;
    trace.newGame(seed, test::setup(), sim::hashContent(content), "test");
    game::Session live(content, seed, test::setup());
    playTraced(live, trace);
    trace.end(live.day());

    std::istringstream in(out.str());
    const game::ReplayResult result = game::replayTrace(content, in, {});
    REQUIRE(result.ok());
    REQUIRE(result.session != nullptr);
    CHECK_FALSE(result.firstDivergence.has_value());
    CHECK(result.commands == 5);
    CHECK(result.daysTicked == live.day());
    CHECK(result.session->day() == live.day());
    CHECK(result.session->stateHash() == live.stateHash());
    CHECK(result.session->log().size() == live.log().size());
    // Day-0 listings are logged before any trace is attached, so the trace
    // starts with the orders.
    CHECK(out.str().find("\"t\":\"command\"") != std::string::npos);
    CHECK(out.str().find("\"t\":\"state\"") != std::string::npos);
}

TEST_CASE("Replay stops at the requested day and reports divergence", "[trace][replay]") {
    const sim::Content content = test::makeContent();
    std::ostringstream out;
    game::TraceWriter trace(out);
    trace.newGame(5, test::setup(), sim::hashContent(content), "test");
    game::Session live(content, 5, test::setup());
    playTraced(live, trace);

    SECTION("stop day") {
        std::istringstream in(out.str());
        game::ReplayOptions options;
        options.stopDay = 40;
        const game::ReplayResult result = game::replayTrace(content, in, options);
        REQUIRE(result.ok());
        CHECK(result.session->day() == 40);
        CHECK(result.daysTicked == 40);
    }
    SECTION("a tampered hash is reported as a divergence") {
        std::string text = out.str();
        const std::size_t pos = text.find(R"({"day":30,"hash":")");
        REQUIRE(pos != std::string::npos);
        const std::size_t hashPos = pos + std::string(R"({"day":30,"hash":")").size();
        text[hashPos] = text[hashPos] == '0' ? '1' : '0';
        std::istringstream in(text);
        const game::ReplayResult result = game::replayTrace(content, in, {});
        REQUIRE(result.ok());
        REQUIRE(result.firstDivergence.has_value());
        CHECK(result.firstDivergence.value() == 30);
    }
    SECTION("different game data is refused") {
        sim::Content other = content;
        other.balance.surveyCost = other.balance.surveyCost + sim::Money::fromUnits(1);
        std::istringstream in(out.str());
        const game::ReplayResult result = game::replayTrace(other, in, {});
        CHECK_FALSE(result.ok());
        CHECK(result.session == nullptr);
    }
    SECTION("a malformed line is reported with its number") {
        std::istringstream in(out.str() + "garbage\n");
        const game::ReplayResult result = game::replayTrace(content, in, {});
        CHECK_FALSE(result.ok());
        CHECK(result.error.find("garbage") == std::string::npos);
        CHECK(result.error.find("line ") == 0);
    }
}

TEST_CASE("Replay follows a load record through its save file", "[trace][replay]") {
    const sim::Content content = test::makeContent();
    // Game A: play a while and save.
    game::Session a(content, 21, test::setup());
    a.setSpeed(game::Speed::Fastest);
    for (int i = 0; i < 5; ++i) {
        a.advance(1.0);
    }
    const std::vector<std::uint8_t> saveBytes = a.save();

    // Game B: new game, load A's save, keep playing with a trace.
    std::ostringstream out;
    game::TraceWriter trace(out);
    trace.newGame(22, test::setup(), sim::hashContent(content), "test");
    game::Session b(content, 22, test::setup());
    b.setTrace(&trace);
    b.stepOneDay();
    REQUIRE(b.load(saveBytes));
    trace.loaded(b.day(), "load-1.dcsave", b.stateHash());
    b.submit(sim::TakeLoan{{}, sim::Money::fromUnits(500'000)});
    b.setSpeed(game::Speed::Fastest);
    for (int i = 0; i < 3; ++i) {
        b.advance(1.0);
    }

    game::ReplayOptions options;
    options.readSave = [&saveBytes](const std::string& file) {
        return file == "load-1.dcsave" ? saveBytes : std::vector<std::uint8_t>{};
    };
    std::istringstream in(out.str());
    const game::ReplayResult result = game::replayTrace(content, in, options);
    REQUIRE(result.ok());
    CHECK_FALSE(result.firstDivergence.has_value());
    CHECK(result.session->day() == b.day());
    CHECK(result.session->stateHash() == b.stateHash());

    std::istringstream again(out.str());
    const game::ReplayResult noReader = game::replayTrace(content, again, {});
    CHECK_FALSE(noReader.ok());
}

TEST_CASE("PNG encoder writes a valid stored-deflate image", "[png]") {
    const std::uint32_t width = 3;
    const std::uint32_t height = 2;
    const std::vector<std::uint8_t> rgb = {255, 0,  0,  0,  255, 0,  0,  0,  255,
                                           10,  20, 30, 40, 50,  60, 70, 80, 90};
    const std::vector<std::uint8_t> png = game::encodePngRgb(width, height, rgb);
    REQUIRE(png.size() > 8 + 25 + 12);
    const std::vector<std::uint8_t> signature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    CHECK(std::vector<std::uint8_t>(png.begin(), png.begin() + 8) == signature);
    auto u32 = [&png](std::size_t at) {
        return (static_cast<std::uint32_t>(png[at]) << 24U) |
               (static_cast<std::uint32_t>(png[at + 1]) << 16U) |
               (static_cast<std::uint32_t>(png[at + 2]) << 8U) | png[at + 3];
    };
    // IHDR
    CHECK(u32(8) == 13);
    CHECK(std::string(png.begin() + 12, png.begin() + 16) == "IHDR");
    CHECK(u32(16) == width);
    CHECK(u32(20) == height);
    CHECK(png[24] == 8);
    CHECK(png[25] == 2);
    // IDAT: zlib header, one stored block holding the filtered rows.
    const std::size_t idat = 8 + 25;
    const std::uint32_t idatLen = u32(idat);
    CHECK(std::string(png.begin() + static_cast<std::ptrdiff_t>(idat) + 4,
                      png.begin() + static_cast<std::ptrdiff_t>(idat) + 8) == "IDAT");
    const std::size_t z = idat + 8;
    CHECK(png[z] == 0x78);
    CHECK(((png[z] << 8U) | png[z + 1]) % 31 == 0);
    CHECK(png[z + 2] == 1);  // final stored block
    const std::size_t rawLen = (static_cast<std::size_t>(width) * 3 + 1) * height;
    CHECK((png[z + 3] | (png[z + 4] << 8U)) == rawLen);
    CHECK(((png[z + 5] | (png[z + 6] << 8U)) & 0xFFFFU) == (~rawLen & 0xFFFFU));
    std::vector<std::uint8_t> expected;
    for (std::uint32_t y = 0; y < height; ++y) {
        expected.push_back(0);
        const auto rowStart = static_cast<std::ptrdiff_t>(y) * width * 3;
        expected.insert(expected.end(), rgb.begin() + rowStart, rgb.begin() + rowStart + width * 3);
    }
    const std::vector<std::uint8_t> stored(
        png.begin() + static_cast<std::ptrdiff_t>(z + 7),
        png.begin() + static_cast<std::ptrdiff_t>(z + 7 + rawLen));
    CHECK(stored == expected);
    CHECK(idatLen == 2 + 5 + rawLen + 4);
    // IEND last.
    CHECK(std::string(png.end() - 8, png.end() - 4) == "IEND");

    CHECK(game::encodePngRgb(0, 1, rgb).empty());
    CHECK(game::encodePngRgb(4, 2, rgb).empty());
    CHECK(game::encodePngRgb(40'000, 1, std::span<const std::uint8_t>{}).empty());
}
