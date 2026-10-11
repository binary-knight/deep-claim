#include "game/trace.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/simulation.h"
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
    // The script counts days; nothing may stop the clock under it.
    game::AutoPause never;
    never.enabled.fill(false);
    session.setAutoPause(never);
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

std::vector<std::string> linesWith(const std::string& text, const std::string& needle) {
    std::vector<std::string> found;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find(needle) != std::string::npos) {
            found.push_back(line);
        }
    }
    return found;
}

std::vector<game::TraceRecord> parseTrace(const std::string& text) {
    std::vector<game::TraceRecord> records;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        game::TraceRecord record;
        std::string error;
        REQUIRE(game::parseTraceLine(line, record, error));
        records.push_back(record);
    }
    return records;
}

std::vector<game::TraceRecord> commandRecords(const std::string& text) {
    std::vector<game::TraceRecord> commands;
    for (const game::TraceRecord& record : parseTrace(text)) {
        if (record.type == "command") {
            commands.push_back(record);
        }
    }
    return commands;
}

const std::string kQueued = R"("kind":"order_queued")";
const std::string kCancelled = R"("kind":"order_cancelled")";

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
        sim::SetSellPolicy{sim::CompanyId(0), sim::ResourceId(1), 1, 250,
                           sim::Money::fromUnits(950)},
        sim::SetCoverCosts{sim::CompanyId(0), 0},
        sim::DeliverToProgram{sim::CompanyId(0), sim::ResourceId(1), 250},
        sim::CompleteProgramStage{sim::CompanyId(0)},
    };
    REQUIRE(commands.size() == std::variant_size_v<sim::Command>);
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
    CHECK_FALSE(
        game::decodeCommand(R"({"cmd":"SetSellPolicy","resource":0,"enabled":1})", decoded));
    CHECK_FALSE(game::decodeCommand(R"({"cmd":"SetCoverCosts"})", decoded));
    // The program's orders go by name: a delivery needs its good and its
    // tonnes, a stage completion names nothing but the order.
    REQUIRE(game::decodeCommand(R"({"cmd":"DeliverToProgram","resource":2,"tonnes":40})", decoded));
    CHECK(std::get<sim::DeliverToProgram>(decoded).resource == sim::ResourceId(2));
    CHECK(std::get<sim::DeliverToProgram>(decoded).tonnes == 40);
    CHECK_FALSE(game::decodeCommand(R"({"cmd":"DeliverToProgram","resource":2})", decoded));
    CHECK_FALSE(game::decodeCommand(R"({"cmd":"DeliverToProgram","tonnes":40})", decoded));
    REQUIRE(game::decodeCommand(R"({"cmd":"CompleteProgramStage"})", decoded));
    CHECK(std::holds_alternative<sim::CompleteProgramStage>(decoded));
    CHECK(game::decodeCommand(
        R"({"cmd":"SetSellPolicy","resource":0,"enabled":true,"reserve":5,"floor_cents":100})",
        decoded));
    CHECK(std::get<sim::SetSellPolicy>(decoded).reserveTonnes == 5);
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

TEST_CASE("StartResearch records carry the tech id and still decode by index", "[trace]") {
    const sim::Content content = test::makeContent();
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 1, test::setup());
    session.setTrace(&trace);
    session.submit(sim::StartResearch{{}, sim::TechId(1)});
    session.submit(sim::TakeLoan{{}, sim::Money::fromUnits(1'000'000)});
    // Orders are recorded as commands when the clock hands them to the sim.
    session.stepOneDay();

    std::istringstream lines(out.str());
    std::string line;
    int research = 0;
    while (std::getline(lines, line)) {
        if (line.find("\"StartResearch\"") == std::string::npos) {
            CHECK(line.find("tech_id") == std::string::npos);
            continue;
        }
        ++research;
        CHECK(line.find("\"tech_id\":\"" + content.techs[1].id + "\"") != std::string::npos);
        game::TraceRecord record;
        std::string error;
        REQUIRE(game::parseTraceLine(line, record, error));
        REQUIRE(record.command.has_value());
        const auto* decoded = std::get_if<sim::StartResearch>(&*record.command);
        REQUIRE(decoded != nullptr);
        CHECK(decoded->tech == sim::TechId(1));
    }
    CHECK(research == 1);
    // Without content (a bare writer) the record is as before.
    std::ostringstream bare;
    game::TraceWriter plain(bare);
    plain.command(3, sim::StartResearch{{}, sim::TechId(0)});
    CHECK(bare.str().find("tech_id") == std::string::npos);
}

TEST_CASE("A cancelled order is traced as queued and cancelled, never as a command", "[trace]") {
    const sim::Content content = test::makeContent();
    const sim::Seed seed = 3;
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, seed, test::setup());
    session.setTrace(&trace);
    const sim::PlotId plot = firstListed(session);
    REQUIRE(plot.valid());
    const sim::Command loan = sim::TakeLoan{{}, sim::Money::fromUnits(1'000'000)};

    SECTION("one order withdrawn, the other given") {
        session.submit(sim::CommissionSurvey{{}, plot});
        session.submit(loan);
        const std::uint64_t surveyId = session.pendingOrders().front().id;
        REQUIRE(session.cancel(surveyId));
        session.stepOneDay();

        CHECK(linesWith(out.str(), kQueued).size() == 2);
        const std::vector<std::string> cancelled = linesWith(out.str(), kCancelled);
        REQUIRE(cancelled.size() == 1);
        CHECK(cancelled.front().find("\"t\":\"ui\"") != std::string::npos);
        CHECK(cancelled.front().find("#" + std::to_string(surveyId) + " ") != std::string::npos);
        CHECK(cancelled.front().find("CommissionSurvey") != std::string::npos);
        // The queued record of the same order names the same id.
        const std::vector<std::string> queued = linesWith(out.str(), kQueued);
        CHECK(queued.front().find("#" + std::to_string(surveyId) + " ") != std::string::npos);
        CHECK(queued.front().find("CommissionSurvey") != std::string::npos);

        const std::vector<game::TraceRecord> commands = commandRecords(out.str());
        REQUIRE(commands.size() == 1);
        CHECK(std::holds_alternative<sim::TakeLoan>(commands.front().command.value()));

        // The game is the one in which the survey was never ordered, and not
        // the one in which it was.
        game::Session never(content, seed, test::setup());
        never.submit(loan);
        never.stepOneDay();
        CHECK(session.stateHash() == never.stateHash());
        game::Session given(content, seed, test::setup());
        given.submit(sim::CommissionSurvey{{}, plot});
        given.submit(loan);
        given.stepOneDay();
        CHECK(session.stateHash() != given.stateHash());
    }
    SECTION("everything withdrawn at once") {
        session.submit(sim::CommissionSurvey{{}, plot});
        session.submit(loan);
        session.cancelAll();
        session.stepOneDay();

        CHECK(linesWith(out.str(), kQueued).size() == 2);
        CHECK(linesWith(out.str(), kCancelled).size() == 2);
        CHECK(commandRecords(out.str()).empty());
        CHECK(linesWith(out.str(), "\"t\":\"command\"").empty());
    }
}

TEST_CASE("A command record carries the day its order was given", "[trace]") {
    const sim::Content content = test::makeContent();
    std::ostringstream out;
    game::TraceWriter trace(out);
    game::Session session(content, 7, test::setup());
    session.setTrace(&trace);
    game::AutoPause never;
    never.enabled.fill(false);
    session.setAutoPause(never);

    std::vector<sim::Day> given;
    const auto borrow = [&session, &given] {
        given.push_back(session.day());
        session.submit(sim::TakeLoan{{}, sim::Money::fromUnits(100'000)});
    };
    borrow();  // day 0, clock stopped
    session.stepOneDay();
    session.stepOneDay();
    session.stepOneDay();
    borrow();  // day 3, clock stopped
    session.setSpeed(game::Speed::Fastest);
    REQUIRE(session.advance(1.0) == 20);
    borrow();  // day 23, clock running
    REQUIRE(session.advance(1.0) == 20);
    REQUIRE(given == std::vector<sim::Day>{0, 3, 23});

    // Each order is recorded on the day it was given, after that day's tick
    // record and before the next one: where it stood before orders waited.
    std::vector<sim::Day> recorded;
    sim::Day lastTick = 0;
    for (const game::TraceRecord& record : parseTrace(out.str())) {
        if (record.type == "day") {
            lastTick = record.day;
        } else if (record.type == "command") {
            CHECK(record.day == lastTick);
            recorded.push_back(record.day);
        }
    }
    CHECK(recorded == given);
    // The record of the order being given carries that day too.
    std::vector<sim::Day> queued;
    for (const std::string& line : linesWith(out.str(), kQueued)) {
        game::TraceRecord record;
        std::string error;
        REQUIRE(game::parseTraceLine(line, record, error));
        queued.push_back(record.day);
    }
    CHECK(queued == given);
}

TEST_CASE("A trace with queued and cancelled orders replays to the same game", "[trace][replay]") {
    const sim::Content content = test::makeContent();
    const sim::Seed seed = 17;
    std::ostringstream out;
    game::TraceWriter trace(out);
    trace.newGame(seed, test::setup(), sim::hashContent(content), "test");
    game::Session live(content, seed, test::setup());
    live.setTrace(&trace);
    game::AutoPause never;
    never.enabled.fill(false);
    live.setAutoPause(never);
    const sim::PlotId plot = firstListed(live);
    REQUIRE(plot.valid());

    // Day 0: a loan that stays and a survey that is taken back.
    live.submit(sim::TakeLoan{{}, sim::Money::fromUnits(1'000'000)});
    live.submit(sim::CommissionSurvey{{}, plot});
    REQUIRE(live.cancel(live.pendingOrders().back().id));
    live.stepOneDay();
    // Day 1: two orders withdrawn together, then a bid that stays.
    live.submit(sim::TakeLoan{{}, sim::Money::fromUnits(2'000'000)});
    live.submit(sim::StartResearch{{}, sim::TechId(0)});
    live.cancelAll();
    live.submit(sim::BidOnPlot{{}, plot, live.view().plots[plot.index()].askPrice});
    live.setSpeed(game::Speed::Fastest);
    for (int i = 0; i < 3; ++i) {
        live.advance(1.0);
    }
    // Clock running: one more order that stays.
    live.submit(sim::StartResearch{{}, sim::TechId(0)});
    live.advance(1.0);
    trace.end(live.day());

    REQUIRE(linesWith(out.str(), kQueued).size() == 6);
    REQUIRE(linesWith(out.str(), kCancelled).size() == 3);

    std::istringstream in(out.str());
    const game::ReplayResult result = game::replayTrace(content, in, {});
    REQUIRE(result.ok());
    REQUIRE(result.session != nullptr);
    CHECK_FALSE(result.firstDivergence.has_value());
    CHECK(result.commands == 3);  // the loan, the bid and the research; nothing withdrawn
    CHECK(result.daysTicked == live.day());
    CHECK(result.session->day() == live.day());
    CHECK(result.session->stateHash() == live.stateHash());
    CHECK(result.session->pendingOrders().empty());
    CHECK(result.session->view().loans.size() == 1);
    CHECK(result.session->log().size() == live.log().size());
}

namespace {

// A save of a fresh game in which the player's first program stage is one
// delivery and one order from done: its research complete, the iron it needs
// in stock and plenty of cash. The fixture's first stage costs 1,000,000 and
// needs 1,000 t of iron (resource 0).
std::vector<std::uint8_t> saveWithStageInReach(const sim::Content& content, sim::Seed seed) {
    const sim::Simulation fresh(content, seed, test::setup());
    sim::WorldState state = fresh.state();
    sim::Company& player = state.companies[game::Session::kPlayer.index()];
    player.completedTechs.push_back(content.program.stages[0].research);
    player.inventoryTonnes[0] = 1'500;
    player.cash = sim::Money::fromUnits(5'000'000);
    return sim::saveGame(state, {}, sim::hashContent(content));
}

sim::Company playerOf(const sim::Content& content, const game::Session& session) {
    sim::WorldState state;
    std::vector<sim::Command> pending;
    REQUIRE(sim::loadGame(session.save(), content, state, pending));
    return state.companies[game::Session::kPlayer.index()];
}

}  // namespace

TEST_CASE("The program's orders are priced, applied by a tick and replayed", "[trace][replay]") {
    const sim::Content content = test::makeContent();
    const sim::Seed seed = 31;
    const std::vector<std::uint8_t> saveBytes = saveWithStageInReach(content, seed);
    const sim::Money stageCost = content.program.stages[0].cost;

    std::ostringstream out;
    game::TraceWriter trace(out);
    trace.newGame(seed, test::setup(), sim::hashContent(content), "test");
    game::Session live(content, seed, test::setup());
    live.setTrace(&trace);
    game::AutoPause never;
    never.enabled.fill(false);
    live.setAutoPause(never);
    REQUIRE(live.load(saveBytes));
    trace.loaded(live.day(), "stage.dcsave", live.stateHash());

    // A delivery costs nothing up front; completing a stage costs the stage.
    CHECK(live.orderCost(sim::DeliverToProgram{{}, sim::ResourceId(0), 1'000}) ==
          sim::Money::zero());
    CHECK(live.orderCost(sim::CompleteProgramStage{}) == stageCost);

    // Both orders on one day: the sim takes them in the order given, so the
    // delivery lands before the stage is checked.
    live.submit(sim::DeliverToProgram{{}, sim::ResourceId(0), 1'000});
    live.submit(sim::CompleteProgramStage{});
    CHECK(live.committed() == stageCost);
    const sim::Money cashBefore = playerOf(content, live).cash;
    live.stepOneDay();

    const sim::Company after = playerOf(content, live);
    CHECK(after.programStage == 1);
    CHECK(after.inventoryTonnes[0] == 500);
    CHECK(after.programDelivered[0] == 0);  // cleared for the next stage
    CHECK(after.cash <= cashBefore - stageCost);
    // The next completion is priced at the second stage's cost.
    CHECK(live.orderCost(sim::CompleteProgramStage{}) == content.program.stages[1].cost);

    // A delivery alone moves stock to the program and stays there.
    live.submit(sim::DeliverToProgram{{}, sim::ResourceId(0), 300});
    live.stepOneDay();
    CHECK(playerOf(content, live).programDelivered[0] == 300);
    CHECK(playerOf(content, live).programStage == 1);
    trace.end(live.day());

    const std::vector<game::TraceRecord> commands = commandRecords(out.str());
    REQUIRE(commands.size() == 3);
    CHECK(std::holds_alternative<sim::DeliverToProgram>(commands[0].command.value()));
    CHECK(std::holds_alternative<sim::CompleteProgramStage>(commands[1].command.value()));
    CHECK(out.str().find(R"("cmd":"DeliverToProgram")") != std::string::npos);
    CHECK(out.str().find(R"("cmd":"CompleteProgramStage")") != std::string::npos);

    game::ReplayOptions options;
    options.readSave = [&saveBytes](const std::string& file) {
        return file == "stage.dcsave" ? saveBytes : std::vector<std::uint8_t>{};
    };
    std::istringstream in(out.str());
    const game::ReplayResult result = game::replayTrace(content, in, options);
    REQUIRE(result.ok());
    REQUIRE(result.session != nullptr);
    CHECK_FALSE(result.firstDivergence.has_value());
    CHECK(result.commands == 3);
    CHECK(result.session->day() == live.day());
    CHECK(result.session->stateHash() == live.stateHash());
    CHECK(playerOf(content, *result.session).programStage == 1);
}

// ---- spec 010: the end record carries the outcome; a replay stops at the end ----

namespace {

// The view of a two-company game as the trace's end record reads it.
sim::CompanyView endedView(sim::CompanyId founder, sim::ProgramResult playerResult) {
    sim::CompanyView view;
    view.me = game::Session::kPlayer;
    view.day = 412;
    view.outcome.ended = true;
    view.outcome.day = 411;
    view.outcome.founder = founder;
    view.outcome.results = {playerResult, sim::ProgramResult::Lost};
    return view;
}

game::TraceRecord endRecordFor(const sim::CompanyView& view) {
    std::ostringstream out;
    game::TraceWriter trace(out);
    trace.end(view.day, view);
    game::TraceRecord record;
    std::string error;
    REQUIRE(game::parseTraceLine(out.str(), record, error));
    REQUIRE(record.type == "end");
    REQUIRE(record.day == view.day);
    return record;
}

}  // namespace

TEST_CASE("The end record says how the game ended for the player", "[trace][spec010]") {
    SECTION("the player founded the program") {
        const game::TraceRecord record =
            endRecordFor(endedView(game::Session::kPlayer, sim::ProgramResult::Founded));
        REQUIRE(record.outcome.has_value());
        CHECK(record.outcome->ended);
        CHECK(record.outcome->day == 411);
        CHECK(record.outcome->founder == game::Session::kPlayer);
        CHECK(record.outcome->result == "founded");
    }
    SECTION("the rival founded it and the player joins as a lesser partner") {
        const game::TraceRecord record =
            endRecordFor(endedView(sim::CompanyId(1), sim::ProgramResult::LesserPartner));
        REQUIRE(record.outcome.has_value());
        CHECK(record.outcome->founder == sim::CompanyId(1));
        CHECK(record.outcome->result == "lesser_partner");
    }
    SECTION("the rival founded it and the player lost") {
        const game::TraceRecord record =
            endRecordFor(endedView(sim::CompanyId(1), sim::ProgramResult::Lost));
        REQUIRE(record.outcome.has_value());
        CHECK(record.outcome->result == "lost");
    }
    SECTION("the deadline passed: nobody founded it") {
        const game::TraceRecord record =
            endRecordFor(endedView(sim::CompanyId(), sim::ProgramResult::Lost));
        REQUIRE(record.outcome.has_value());
        CHECK(record.outcome->ended);
        CHECK_FALSE(record.outcome->founder.valid());
        CHECK(record.outcome->result == "lost");
    }
    SECTION("the player went bankrupt with the race still open") {
        sim::CompanyView view;
        view.me = game::Session::kPlayer;
        view.day = 95;
        view.bankrupt = true;
        view.outcome.results = {sim::ProgramResult::None, sim::ProgramResult::None};
        const game::TraceRecord record = endRecordFor(view);
        REQUIRE(record.outcome.has_value());
        CHECK(record.outcome->ended);  // over for the player, though nobody won
        CHECK(record.outcome->day == 95);
        CHECK_FALSE(record.outcome->founder.valid());
        CHECK(record.outcome->result == "bankrupt");
    }
    SECTION("a bankrupt player is bankrupt even when the race ends the same day") {
        sim::CompanyView view = endedView(sim::CompanyId(1), sim::ProgramResult::Lost);
        view.bankrupt = true;
        const game::TraceRecord record = endRecordFor(view);
        REQUIRE(record.outcome.has_value());
        CHECK(record.outcome->day == 411);
        CHECK(record.outcome->founder == sim::CompanyId(1));
        CHECK(record.outcome->result == "bankrupt");
    }
    SECTION("a game closed while still being played has no outcome") {
        sim::CompanyView view;
        view.me = game::Session::kPlayer;
        view.day = 30;
        view.outcome.results = {sim::ProgramResult::None, sim::ProgramResult::None};
        CHECK_FALSE(endRecordFor(view).outcome.has_value());
    }
}

TEST_CASE("End records read back with and without an outcome", "[trace][spec010]") {
    game::TraceRecord record;
    std::string error;

    // As every build before spec 010 wrote it, and as TraceWriter::end(day) still does.
    REQUIRE(game::parseTraceLine(R"({"t":"end","day":5})", record, error));
    CHECK(record.type == "end");
    CHECK(record.day == 5);
    CHECK_FALSE(record.outcome.has_value());
    std::ostringstream out;
    game::TraceWriter trace(out);
    trace.end(9);
    REQUIRE(game::parseTraceLine(out.str(), record, error));
    CHECK(record.day == 9);
    CHECK_FALSE(record.outcome.has_value());

    REQUIRE(game::parseTraceLine(
        R"({"t":"end","day":8,"outcome":{"ended":true,"day":7,"founder":1,"result":"lost"}})",
        record, error));
    REQUIRE(record.outcome.has_value());
    CHECK(record.outcome->day == 7);
    CHECK(record.outcome->founder == sim::CompanyId(1));
    CHECK(record.outcome->result == "lost");
    REQUIRE(game::parseTraceLine(
        R"({"t":"end","day":8,"outcome":{"ended":true,"day":7,"founder":null,"result":"lost"}})",
        record, error));
    REQUIRE(record.outcome.has_value());
    CHECK_FALSE(record.outcome->founder.valid());

    // An outcome that is there must be whole.
    for (
        const char* bad : {
            R"({"t":"end","day":8,"outcome":7})",
            R"({"t":"end","day":8,"outcome":{"day":7,"founder":1,"result":"lost"}})",
            R"({"t":"end","day":8,"outcome":{"ended":1,"day":7,"founder":1,"result":"lost"}})",
            R"({"t":"end","day":8,"outcome":{"ended":true,"founder":1,"result":"lost"}})",
            R"({"t":"end","day":8,"outcome":{"ended":true,"day":-1,"founder":1,"result":"lost"}})",
            R"({"t":"end","day":8,"outcome":{"ended":true,"day":7,"result":"lost"}})",
            R"({"t":"end","day":8,"outcome":{"ended":true,"day":7,"founder":"x","result":"lost"}})",
            R"({"t":"end","day":8,"outcome":{"ended":true,"day":7,"founder":1}})",
            R"({"t":"end","day":8,"outcome":{"ended":true,"day":7,"founder":1,"result":"won"}})",
        }) {
        CAPTURE(bad);
        CHECK_FALSE(game::parseTraceLine(bad, record, error));
        CHECK(error == "bad outcome");
    }
}

namespace {

// A save, two days in, in which the player's last program stage is one order
// from done: completing it founds the program and ends the game.
std::vector<std::uint8_t> saveWithFoundingInReach(const sim::Content& content, sim::Seed seed) {
    sim::Simulation simulation(content, seed, test::setup());
    simulation.tick(2);
    sim::WorldState state = simulation.state();
    sim::Company& player = state.companies[game::Session::kPlayer.index()];
    const std::size_t last = content.program.stages.size() - 1;
    player.programStage = static_cast<std::uint32_t>(last);
    for (const sim::ProgramStage& stage : content.program.stages) {
        player.completedTechs.push_back(stage.research);
    }
    for (const sim::ProgramRequirement& need : content.program.stages[last].requirements) {
        player.programDelivered[need.resource.index()] = need.tonnes;
    }
    player.cash = sim::Money::fromUnits(20'000'000);
    return sim::saveGame(state, {}, sim::hashContent(content));
}

}  // namespace

TEST_CASE("A trace of a game played to its end replays to the same end", "[trace][replay]") {
    const sim::Content content = test::makeContent();
    const sim::Seed seed = 31;
    const std::vector<std::uint8_t> saveBytes = saveWithFoundingInReach(content, seed);

    std::ostringstream out;
    game::TraceWriter trace(out);
    trace.newGame(seed, test::setup(), sim::hashContent(content), "test");
    game::Session live(content, seed, test::setup());
    live.setTrace(&trace);
    game::AutoPause never;
    never.enabled.fill(false);
    live.setAutoPause(never);
    REQUIRE(live.load(saveBytes));
    trace.loaded(live.day(), "last.dcsave", live.stateHash());
    live.stepOneDay();
    live.submit(sim::CompleteProgramStage{});
    live.stepOneDay();
    REQUIRE(live.gameOver());
    // The player keeps pressing after the end: nothing more is recorded.
    live.stepOneDay();
    live.setSpeed(game::Speed::Fastest);
    CHECK(live.advance(2.0) == 0);
    trace.end(live.day(), live.view());
    CHECK(linesWith(out.str(), R"("t":"day")").size() == 2);

    game::ReplayOptions options;
    options.readSave = [&saveBytes](const std::string& file) {
        return file == "last.dcsave" ? saveBytes : std::vector<std::uint8_t>{};
    };
    SECTION("as recorded") {
        std::istringstream in(out.str());
        const game::ReplayResult result = game::replayTrace(content, in, options);
        REQUIRE(result.ok());
        REQUIRE(result.session != nullptr);
        CHECK_FALSE(result.firstDivergence.has_value());
        CHECK(result.daysTicked == 2);
        CHECK(result.session->stateHash() == live.stateHash());
        CHECK(result.session->gameOver());
        CHECK(result.session->outcome().founder == game::Session::kPlayer);
        CHECK(result.session->outcome().day == live.outcome().day);
    }
    SECTION("with day records after the end, as a build before the fix wrote them") {
        // The same day and hash again and again: the session used to tick a
        // finished world and record it each time.
        const std::vector<std::string> days = linesWith(out.str(), R"("t":"day")");
        std::string padded;
        std::istringstream lines(out.str());
        std::string line;
        while (std::getline(lines, line)) {
            if (line.find(R"("t":"end")") != std::string::npos) {
                padded += days.back() + "\n" + days.back() + "\n";
            }
            padded += line + "\n";
        }
        std::istringstream in(padded);
        const game::ReplayResult result = game::replayTrace(content, in, options);
        REQUIRE(result.ok());
        REQUIRE(result.session != nullptr);
        CHECK_FALSE(result.firstDivergence.has_value());
        CHECK(result.daysTicked == 2);  // the days that were played, no more
        CHECK(result.session->day() == live.day());
        CHECK(result.session->stateHash() == live.stateHash());
        CHECK(result.session->gameOver());
    }
}
