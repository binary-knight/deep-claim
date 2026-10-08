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
    // Unsigned values past INT64_MAX must not wrap into negative money or tonnes.
    CHECK_FALSE(game::decodeCommand(R"({"cmd":"TakeLoan","cents":9223372036854775808})", decoded));
    CHECK_FALSE(game::decodeCommand(
        R"({"cmd":"SellOre","resource":0,"tonnes":18446744073709551615})", decoded));
    CHECK(game::decodeCommand(R"({"cmd":"TakeLoan","cents":9223372036854775807})", decoded));
    CHECK(game::decodeCommand(R"({"cmd":"SellOre","resource":0,"tonnes":-5})", decoded));
    // A null id is the invalid id; a bad type or an out-of-range id is an error.
    CHECK(game::decodeCommand(R"({"cmd":"BuildMine","plot":null})", decoded));
    CHECK_FALSE(std::get<sim::BuildMine>(decoded).plot.valid());
    CHECK_FALSE(game::decodeCommand(R"({"cmd":"BuildMine","plot":"3"})", decoded));
    CHECK_FALSE(game::decodeCommand(R"({"cmd":"BuildMine","plot":4294967296})", decoded));
    CHECK_FALSE(game::decodeCommand(R"({"cmd":"BuildMine","plot":-1})", decoded));
    CHECK_FALSE(game::decodeCommand(R"({"cmd":"SetMineState","mine":1,"operate":1})", decoded));
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
    CHECK_FALSE(game::parseTraceLine(R"({"t":"day","hash":"00"})", record, error));
    CHECK_FALSE(game::parseTraceLine(R"({"t":"load","day":3,"hash":"00"})", record, error));
    CHECK_FALSE(game::parseTraceLine(R"({"t":"new_game","seed":"1","content":"00","ais":"x"})",
                                     record, error));
    CHECK_FALSE(game::parseTraceLine(R"({"t":"new_game","seed":"1","content":"00","player":1})",
                                     record, error));
    CHECK_FALSE(game::parseTraceLine(R"({"t":"new_game","seed":"1","content":"00","version":99})",
                                     record, error));
    CHECK(game::parseTraceLine(R"({"t":"new_game","seed":"1","content":"00"})", record, error));
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
    SECTION("nothing after the stop day is applied, not even a new game") {
        std::string text = out.str();
        const std::size_t pos = text.find(R"({"day":40,"hash":")");
        REQUIRE(pos != std::string::npos);
        const std::size_t lineEnd = text.find('\n', pos);
        // Right after day 40: an order, then a whole new game.
        std::ostringstream extra;
        game::TraceWriter more(extra);
        more.command(40, sim::TakeLoan{{}, sim::Money::fromUnits(2'000'000)});
        more.newGame(99, test::setup(), sim::hashContent(content), "test");
        text.insert(lineEnd + 1, extra.str());
        std::istringstream in(text);
        game::ReplayOptions options;
        options.stopDay = 40;
        const game::ReplayResult result = game::replayTrace(content, in, options);
        REQUIRE(result.ok());
        CHECK(result.session->day() == 40);
        CHECK(result.commands == 3);  // the two day-0 orders and the day-1 bid only
        CHECK(result.session->view().loans.size() == 1);  // the day-0 loan, not the day-40 one
        game::Session fresh(content, 99, test::setup());
        CHECK(result.session->stateHash() != fresh.stateHash());
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

namespace {

std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 24U) |
           (static_cast<std::uint32_t>(b[at + 1]) << 16U) |
           (static_cast<std::uint32_t>(b[at + 2]) << 8U) | static_cast<std::uint32_t>(b[at + 3]);
}

std::uint32_t le16(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8U);
}

// Reference CRC-32 (bitwise, no table) and Adler-32, independent of the encoder.
std::uint32_t refCrc32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t c = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < len; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) != 0 ? (c >> 1U) ^ 0xEDB88320U : c >> 1U;
        }
    }
    return ~c;
}

std::uint32_t refAdler32(const std::vector<std::uint8_t>& data) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : data) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16U) | a;
}

// Inflates a zlib stream made only of stored blocks, as a real decoder would
// see it; returns false on any structural problem.
bool inflateStored(const std::vector<std::uint8_t>& z, std::vector<std::uint8_t>& out) {
    if (z.size() < 6 || (z[0] & 0x0FU) != 8 || ((z[0] << 8U) | z[1]) % 31 != 0) {
        return false;
    }
    std::size_t pos = 2;
    bool final = false;
    while (!final) {
        if (pos + 5 > z.size() || (z[pos] & 0x06U) != 0) {
            return false;  // not a stored block
        }
        final = (z[pos] & 1U) != 0;
        const std::uint32_t len = le16(z, pos + 1);
        const std::uint32_t nlen = le16(z, pos + 3);
        if ((len ^ nlen) != 0xFFFFU || pos + 5 + len > z.size()) {
            return false;
        }
        out.insert(out.end(), z.begin() + static_cast<std::ptrdiff_t>(pos + 5),
                   z.begin() + static_cast<std::ptrdiff_t>(pos + 5 + len));
        pos += 5 + len;
    }
    return pos + 4 == z.size() && be32(z, pos) == refAdler32(out);
}

}  // namespace

TEST_CASE("PNG encoder output decodes with an independent stored-deflate reader", "[png]") {
    // 300x100 RGB: the filtered rows are 90,100 bytes, past the 65,535-byte
    // stored block limit, so the stream must hold two blocks.
    const std::uint32_t width = 300;
    const std::uint32_t height = 100;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        rgb[i] = static_cast<std::uint8_t>((i * 7 + i / 301) & 0xFFU);
    }
    const std::vector<std::uint8_t> png = game::encodePngRgb(width, height, rgb);
    REQUIRE(png.size() > 8);
    const std::vector<std::uint8_t> signature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    CHECK(std::vector<std::uint8_t>(png.begin(), png.begin() + 8) == signature);

    // Walk the chunks, checking each CRC over type + data.
    std::size_t pos = 8;
    std::vector<std::string> types;
    std::vector<std::uint8_t> idat;
    while (pos + 12 <= png.size()) {
        const std::uint32_t len = be32(png, pos);
        REQUIRE(pos + 12 + len <= png.size());
        const std::string type(png.begin() + static_cast<std::ptrdiff_t>(pos + 4),
                               png.begin() + static_cast<std::ptrdiff_t>(pos + 8));
        CHECK(refCrc32(&png[pos + 4], len + 4) == be32(png, pos + 8 + len));
        if (type == "IHDR") {
            CHECK(len == 13);
            CHECK(be32(png, pos + 8) == width);
            CHECK(be32(png, pos + 12) == height);
            CHECK(png[pos + 16] == 8);  // bit depth
            CHECK(png[pos + 17] == 2);  // RGB
            CHECK(png[pos + 18] == 0);
            CHECK(png[pos + 19] == 0);
            CHECK(png[pos + 20] == 0);
        } else if (type == "IDAT") {
            idat.insert(idat.end(), png.begin() + static_cast<std::ptrdiff_t>(pos + 8),
                        png.begin() + static_cast<std::ptrdiff_t>(pos + 8 + len));
        }
        types.push_back(type);
        pos += 12 + len;
    }
    CHECK(pos == png.size());
    CHECK(types == std::vector<std::string>{"IHDR", "IDAT", "IEND"});

    std::vector<std::uint8_t> raw;
    REQUIRE(inflateStored(idat, raw));
    const std::size_t stride = static_cast<std::size_t>(width) * 3;
    REQUIRE(raw.size() == (stride + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::size_t row = y * (stride + 1);
        REQUIRE(raw[row] == 0);  // filter: none
        REQUIRE(std::equal(raw.begin() + static_cast<std::ptrdiff_t>(row + 1),
                           raw.begin() + static_cast<std::ptrdiff_t>(row + 1 + stride),
                           rgb.begin() + static_cast<std::ptrdiff_t>(y * stride)));
    }
    CHECK(idat[2] == 0);  // first block is not final
    CHECK(le16(idat, 3) == 65535);

    CHECK(game::encodePngRgb(0, 1, rgb).empty());
    CHECK(game::encodePngRgb(400, 100, rgb).empty());
    CHECK(game::encodePngRgb(40'000, 1, std::span<const std::uint8_t>{}).empty());
}
