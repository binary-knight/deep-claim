#include "game/trace.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/simulation.h"
#include "sim/types.h"
#include "sim/view.h"
#include "sim/world_state.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace game {

namespace {

using Json = nlohmann::json;

std::string hex(std::uint64_t value) {
    return std::format("{:016x}", value);
}

bool parseUnsigned(std::string_view text, int base, std::uint64_t& out) {
    const char* end = std::next(text.data(), static_cast<std::ptrdiff_t>(text.size()));
    const auto result = std::from_chars(text.data(), end, out, base);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parseHex(const Json& value, std::uint64_t& out) {
    return value.is_string() && parseUnsigned(value.get<std::string>(), 16, out);
}

// Seeds are 64-bit and JSON readers such as jq lose precision above 2^53,
// so they travel as decimal strings.
bool parseSeed(const Json& value, sim::Seed& out) {
    if (value.is_number_unsigned()) {
        out = value.get<sim::Seed>();
        return true;
    }
    return value.is_string() && parseUnsigned(value.get<std::string>(), 10, out);
}

template <class Id>
Json idJson(Id id) {
    if (!id.valid()) {
        return nullptr;
    }
    return id.value();
}

template <class Id>
bool idFrom(const Json& object, const char* key, Id& out) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return false;
    }
    if (it->is_null()) {
        out = Id();
        return true;
    }
    if (!it->is_number_unsigned() || it->get<std::uint64_t>() > Id::kInvalid) {
        return false;
    }
    out = Id(static_cast<typename Id::Value>(it->get<std::uint64_t>()));
    return true;
}

bool int64From(const Json& object, const char* key, std::int64_t& out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return false;
    }
    // nlohmann counts unsigned values as integers and get<int64_t> does not
    // range-check them, so a huge unsigned value would wrap negative.
    if (it->is_number_unsigned() &&
        it->get<std::uint64_t>() >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    out = it->get<std::int64_t>();
    return true;
}

bool moneyFrom(const Json& object, const char* key, sim::Money& out) {
    std::int64_t cents = 0;
    if (!int64From(object, key, cents)) {
        return false;
    }
    out = sim::Money::fromCents(cents);
    return true;
}

Json commandJson(const sim::Command& command) {
    Json j;
    std::visit(
        [&j](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            j["company"] = idJson(c.company);
            if constexpr (std::is_same_v<T, sim::BidOnPlot>) {
                j["cmd"] = "BidOnPlot";
                j["plot"] = idJson(c.plot);
                j["cents"] = c.amount.cents();
            } else if constexpr (std::is_same_v<T, sim::CommissionSurvey>) {
                j["cmd"] = "CommissionSurvey";
                j["plot"] = idJson(c.plot);
            } else if constexpr (std::is_same_v<T, sim::BuildMine>) {
                j["cmd"] = "BuildMine";
                j["plot"] = idJson(c.plot);
            } else if constexpr (std::is_same_v<T, sim::SetMineState>) {
                j["cmd"] = "SetMineState";
                j["mine"] = idJson(c.mine);
                j["operate"] = c.operate != 0;
            } else if constexpr (std::is_same_v<T, sim::SellOre>) {
                j["cmd"] = "SellOre";
                j["resource"] = idJson(c.resource);
                j["tonnes"] = c.tonnes;
            } else if constexpr (std::is_same_v<T, sim::TakeLoan>) {
                j["cmd"] = "TakeLoan";
                j["cents"] = c.principal.cents();
            } else if constexpr (std::is_same_v<T, sim::RepayLoan>) {
                j["cmd"] = "RepayLoan";
                j["loan"] = idJson(c.loan);
                j["cents"] = c.amount.cents();
            } else if constexpr (std::is_same_v<T, sim::StartResearch>) {
                j["cmd"] = "StartResearch";
                j["tech"] = idJson(c.tech);
            } else if constexpr (std::is_same_v<T, sim::SetSellPolicy>) {
                j["cmd"] = "SetSellPolicy";
                j["resource"] = idJson(c.resource);
                j["enabled"] = c.enabled != 0;
                j["reserve"] = c.reserveTonnes;
                j["floor_cents"] = c.floorPrice.cents();
            } else if constexpr (std::is_same_v<T, sim::SetCoverCosts>) {
                j["cmd"] = "SetCoverCosts";
                j["enabled"] = c.enabled != 0;
            } else if constexpr (std::is_same_v<T, sim::DeliverToProgram>) {
                j["cmd"] = "DeliverToProgram";
                j["resource"] = idJson(c.resource);
                j["tonnes"] = c.tonnes;
            } else {
                static_assert(std::is_same_v<T, sim::CompleteProgramStage>);
                j["cmd"] = "CompleteProgramStage";
            }
        },
        command);
    return j;
}

bool decodeBid(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::BidOnPlot c;
    c.company = company;
    if (!idFrom(j, "plot", c.plot) || !moneyFrom(j, "cents", c.amount)) {
        return false;
    }
    out = c;
    return true;
}

bool decodeSurvey(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::CommissionSurvey c;
    c.company = company;
    if (!idFrom(j, "plot", c.plot)) {
        return false;
    }
    out = c;
    return true;
}

bool decodeBuild(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::BuildMine c;
    c.company = company;
    if (!idFrom(j, "plot", c.plot)) {
        return false;
    }
    out = c;
    return true;
}

bool decodeMineState(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::SetMineState c;
    c.company = company;
    const auto operateIt = j.find("operate");
    if (!idFrom(j, "mine", c.mine) || operateIt == j.end() || !operateIt->is_boolean()) {
        return false;
    }
    c.operate = operateIt->get<bool>() ? std::uint8_t{1} : std::uint8_t{0};
    out = c;
    return true;
}

bool decodeSell(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::SellOre c;
    c.company = company;
    if (!idFrom(j, "resource", c.resource) || !int64From(j, "tonnes", c.tonnes)) {
        return false;
    }
    out = c;
    return true;
}

bool decodeLoan(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::TakeLoan c;
    c.company = company;
    if (!moneyFrom(j, "cents", c.principal)) {
        return false;
    }
    out = c;
    return true;
}

bool decodeRepay(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::RepayLoan c;
    c.company = company;
    if (!idFrom(j, "loan", c.loan) || !moneyFrom(j, "cents", c.amount)) {
        return false;
    }
    out = c;
    return true;
}

bool decodeResearch(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::StartResearch c;
    c.company = company;
    if (!idFrom(j, "tech", c.tech)) {
        return false;
    }
    out = c;
    return true;
}

bool boolFrom(const Json& object, const char* key, std::uint8_t& out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return false;
    }
    out = it->get<bool>() ? std::uint8_t{1} : std::uint8_t{0};
    return true;
}

bool decodeSellPolicy(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::SetSellPolicy c;
    c.company = company;
    if (!idFrom(j, "resource", c.resource) || !boolFrom(j, "enabled", c.enabled) ||
        !int64From(j, "reserve", c.reserveTonnes) || !moneyFrom(j, "floor_cents", c.floorPrice)) {
        return false;
    }
    out = c;
    return true;
}

bool decodeCoverCosts(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::SetCoverCosts c;
    c.company = company;
    if (!boolFrom(j, "enabled", c.enabled)) {
        return false;
    }
    out = c;
    return true;
}

bool decodeDeliver(const Json& j, sim::CompanyId company, sim::Command& out) {
    sim::DeliverToProgram c;
    c.company = company;
    if (!idFrom(j, "resource", c.resource) || !int64From(j, "tonnes", c.tonnes)) {
        return false;
    }
    out = c;
    return true;
}

// A stage completion names nothing but the company.
bool decodeCompleteStage(const Json& /*j*/, sim::CompanyId company, sim::Command& out) {
    out = sim::CompleteProgramStage{company};
    return true;
}

using Decoder = bool (*)(const Json&, sim::CompanyId, sim::Command&);

struct NamedDecoder {
    std::string_view name;
    Decoder decode;
};

constexpr std::array<NamedDecoder, 12> kDecoders = {{
    {"BidOnPlot", &decodeBid},
    {"CommissionSurvey", &decodeSurvey},
    {"BuildMine", &decodeBuild},
    {"SetMineState", &decodeMineState},
    {"SellOre", &decodeSell},
    {"TakeLoan", &decodeLoan},
    {"RepayLoan", &decodeRepay},
    {"StartResearch", &decodeResearch},
    {"SetSellPolicy", &decodeSellPolicy},
    {"SetCoverCosts", &decodeCoverCosts},
    {"DeliverToProgram", &decodeDeliver},
    {"CompleteProgramStage", &decodeCompleteStage},
}};

bool commandFrom(const Json& j, sim::Command& out) {
    if (!j.is_object()) {
        return false;
    }
    const auto cmdIt = j.find("cmd");
    if (cmdIt == j.end() || !cmdIt->is_string()) {
        return false;
    }
    // The session rebinds every order to the player, so hand-written
    // traces may leave the company out.
    sim::CompanyId company;
    if (j.contains("company") && !idFrom(j, "company", company)) {
        return false;
    }
    const std::string name = cmdIt->get<std::string>();
    for (const NamedDecoder& decoder : kDecoders) {
        if (decoder.name == name) {
            return decoder.decode(j, company, out);
        }
    }
    return false;
}

const char* mineStateName(sim::MineState state) {
    switch (state) {
        case sim::MineState::Building:
            return "building";
        case sim::MineState::Operating:
            return "operating";
        case sim::MineState::Mothballed:
            return "mothballed";
        case sim::MineState::Depleted:
            return "depleted";
        case sim::MineState::Closed:
            return "closed";
    }
    return "?";
}

}  // namespace

void TraceWriter::line(const std::string& json) {
    *out_ << json << '\n';
    out_->flush();
}

void TraceWriter::newGame(sim::Seed seed, const sim::GameSetup& setup, std::uint64_t contentHash,
                          std::string_view build) {
    Json j;
    j["t"] = "new_game";
    j["version"] = kTraceVersion;
    j["seed"] = std::to_string(seed);
    j["player"] = setup.playerName;
    j["ais"] = setup.aiNames;
    j["content"] = hex(contentHash);
    j["build"] = std::string(build);
    line(j.dump());
}

void TraceWriter::loaded(sim::Day day, std::string_view file, std::uint64_t stateHash) {
    Json j;
    j["t"] = "load";
    j["day"] = day;
    j["file"] = std::string(file);
    j["hash"] = hex(stateHash);
    line(j.dump());
}

void TraceWriter::saved(sim::Day day, std::string_view file) {
    Json j;
    j["t"] = "save";
    j["day"] = day;
    j["file"] = std::string(file);
    line(j.dump());
}

void TraceWriter::command(sim::Day day, const sim::Command& command, const sim::Content* content) {
    Json j = commandJson(command);
    if (const auto* research = std::get_if<sim::StartResearch>(&command);
        research != nullptr && content != nullptr && research->tech.valid() &&
        research->tech.index() < content->techs.size()) {
        j["tech_id"] = content->techs[research->tech.index()].id;
    }
    j["t"] = "command";
    j["day"] = day;
    line(j.dump());
}

void TraceWriter::dayTicked(sim::Day day, std::uint64_t stateHash) {
    Json j;
    j["t"] = "day";
    j["day"] = day;
    j["hash"] = hex(stateHash);
    line(j.dump());
}

void TraceWriter::notice(sim::Day day, const sim::Notice& notice, std::string_view text) {
    Json j;
    j["t"] = "notice";
    j["day"] = day;
    j["kind"] = sim::noticeKindName(notice.kind);
    if (notice.kind == sim::NoticeKind::CommandRejected) {
        j["reason"] = sim::rejectReasonName(notice.reason);
    }
    j["text"] = std::string(text);
    line(j.dump());
}

void TraceWriter::state(const sim::Content& content, const sim::CompanyView& view) {
    Json j;
    j["t"] = "state";
    j["day"] = view.day;
    j["cash"] = view.cash.cents();
    j["reserved"] = view.reservedCash.cents();
    j["debt"] = view.outstandingPrincipal.cents();
    j["loan_capacity"] = view.loanCapacity.cents();
    j["months_insolvent"] = view.monthsInsolvent;
    std::uint32_t owned = 0;
    for (const sim::PlotView& plot : view.plots) {
        if (plot.listing == sim::PlotListing::Sold && plot.owner == view.me) {
            ++owned;
        }
    }
    j["plots_owned"] = owned;
    Json mines = Json::array();
    for (const sim::MineView& mine : view.mines) {
        Json m;
        m["id"] = idJson(mine.id);
        m["plot"] = idJson(mine.plot);
        m["state"] = mineStateName(mine.state);
        std::int64_t output = 0;
        for (const std::int64_t tonnes : mine.lastOutputTonnes) {
            output += tonnes;
        }
        m["tonnes_per_day"] = output;
        mines.push_back(m);
    }
    j["mines"] = mines;
    Json stock = Json::object();
    Json prices = Json::object();
    for (std::size_t r = 0; r < content.resources.size(); ++r) {
        const std::string& id = content.resources[r].id;
        if (r < view.inventoryTonnes.size()) {
            stock[id] = view.inventoryTonnes[r];
        }
        if (r < view.markets.size()) {
            prices[id] = view.markets[r].price.cents();
        }
    }
    j["stock"] = stock;
    j["prices"] = prices;
    Json rivals = Json::array();
    for (const sim::CompanyPublicView& company : view.companies) {
        if (company.id == view.me) {
            continue;
        }
        Json c;
        c["name"] = company.name;
        c["plots"] = company.plotsOwned;
        c["bankrupt"] = company.bankrupt;
        rivals.push_back(c);
    }
    j["rivals"] = rivals;
    line(j.dump());
}

void TraceWriter::ui(sim::Day day, std::string_view kind, std::string_view detail) {
    Json j;
    j["t"] = "ui";
    j["day"] = day;
    j["kind"] = std::string(kind);
    j["detail"] = std::string(detail);
    line(j.dump());
}

void TraceWriter::marker(sim::Day day, std::string_view note) {
    Json j;
    j["t"] = "marker";
    j["day"] = day;
    j["note"] = std::string(note);
    line(j.dump());
}

void TraceWriter::screenshot(sim::Day day, std::string_view file) {
    Json j;
    j["t"] = "screenshot";
    j["day"] = day;
    j["file"] = std::string(file);
    line(j.dump());
}

void TraceWriter::end(sim::Day day) {
    Json j;
    j["t"] = "end";
    j["day"] = day;
    line(j.dump());
}

namespace {

constexpr std::array<std::string_view, 4> kResultWords = {"founded", "lesser_partner", "lost",
                                                          "bankrupt"};

// The player's result as the end record's word; nothing while the game is
// still being played. Bankruptcy comes first: it is why the player's game
// is over, whatever the race came to.
std::optional<std::string_view> playerResultWord(const sim::CompanyView& view) {
    if (view.bankrupt) {
        return "bankrupt";
    }
    if (!view.outcome.ended) {
        return std::nullopt;
    }
    const std::vector<sim::ProgramResult>& results = view.outcome.results;
    const sim::ProgramResult result = view.me.valid() && view.me.index() < results.size()
                                          ? results[view.me.index()]
                                          : sim::ProgramResult::Lost;
    switch (result) {
        case sim::ProgramResult::Founded:
            return "founded";
        case sim::ProgramResult::LesserPartner:
            return "lesser_partner";
        case sim::ProgramResult::None:  // an ended game gives every company a result
        case sim::ProgramResult::Lost:
            return "lost";
    }
    return "lost";
}

}  // namespace

void TraceWriter::end(sim::Day day, const sim::CompanyView& view) {
    Json j;
    j["t"] = "end";
    j["day"] = day;
    if (const std::optional<std::string_view> result = playerResultWord(view)) {
        Json outcome;
        outcome["ended"] = true;
        outcome["day"] = view.outcome.ended ? view.outcome.day : day;
        outcome["founder"] = idJson(view.outcome.founder);
        outcome["result"] = std::string(*result);
        j["outcome"] = outcome;
    }
    line(j.dump());
}

std::string encodeCommand(const sim::Command& command) {
    return commandJson(command).dump();
}

bool decodeCommand(std::string_view json, sim::Command& out) {
    const Json j = Json::parse(json, nullptr, false);
    if (j.is_discarded()) {
        return false;
    }
    return commandFrom(j, out);
}

namespace {

bool parseNewGame(const Json& j, TraceRecord& out, std::string& error) {
    const auto seedIt = j.find("seed");
    const auto contentIt = j.find("content");
    if (seedIt == j.end() || !parseSeed(*seedIt, out.seed) || contentIt == j.end() ||
        !parseHex(*contentIt, out.contentHash)) {
        error = "bad new_game record";
        return false;
    }
    if (const auto it = j.find("version"); it != j.end()) {
        if (!it->is_number_unsigned() || it->get<std::uint64_t>() > kTraceVersion) {
            error = "unsupported trace version";
            return false;
        }
    }
    if (const auto it = j.find("player"); it != j.end()) {
        if (!it->is_string()) {
            error = "bad player name";
            return false;
        }
        out.player = it->get<std::string>();
    }
    if (const auto it = j.find("ais"); it != j.end()) {
        if (!it->is_array()) {
            error = "bad ai list";
            return false;
        }
        for (const Json& name : *it) {
            if (!name.is_string()) {
                error = "bad ai name";
                return false;
            }
            out.ais.push_back(name.get<std::string>());
        }
    }
    return true;
}

bool dayFrom(const Json& value, sim::Day& out) {
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > sim::kMaxDeadline) {
        return false;
    }
    out = static_cast<sim::Day>(value.get<std::uint64_t>());
    return true;
}

// The outcome of an end record. Traces from before spec 010, and games
// closed while still being played, have none; one that is there is whole.
bool parseOutcome(const Json& j, TraceRecord& out) {
    const auto it = j.find("outcome");
    if (it == j.end()) {
        return true;
    }
    if (!it->is_object()) {
        return false;
    }
    TraceOutcome outcome;
    const auto endedIt = it->find("ended");
    const auto dayIt = it->find("day");
    const auto resultIt = it->find("result");
    if (endedIt == it->end() || !endedIt->is_boolean() || dayIt == it->end() ||
        !dayFrom(*dayIt, outcome.day) || !idFrom(*it, "founder", outcome.founder) ||
        resultIt == it->end() || !resultIt->is_string()) {
        return false;
    }
    outcome.ended = endedIt->get<bool>();
    outcome.result = resultIt->get<std::string>();
    if (std::find(kResultWords.begin(), kResultWords.end(), outcome.result) == kResultWords.end()) {
        return false;
    }
    out.outcome = outcome;
    return true;
}

// Every record but new_game carries the day.
bool parseDay(const Json& j, TraceRecord& out, std::string& error) {
    const auto dayIt = j.find("day");
    if (dayIt == j.end()) {
        if (out.type == "new_game") {
            return true;
        }
        error = "missing day";
        return false;
    }
    if (!dayFrom(*dayIt, out.day)) {
        error = "bad day";
        return false;
    }
    return true;
}

void parseStrings(const Json& j, TraceRecord& out) {
    if (const auto it = j.find("file"); it != j.end() && it->is_string()) {
        out.file = it->get<std::string>();
    }
    for (const char* key : {"text", "detail", "note"}) {
        if (const auto it = j.find(key); it != j.end() && it->is_string()) {
            out.text = it->get<std::string>();
        }
    }
}

}  // namespace

bool parseTraceLine(std::string_view line, TraceRecord& out, std::string& error) {
    out = TraceRecord{};
    error.clear();
    if (line.find_first_not_of(" \t\r") == std::string_view::npos) {
        return true;
    }
    const Json j = Json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = "not a JSON object";
        return false;
    }
    const auto typeIt = j.find("t");
    if (typeIt == j.end() || !typeIt->is_string()) {
        error = "missing record type";
        return false;
    }
    out.type = typeIt->get<std::string>();
    if (!parseDay(j, out, error)) {
        return false;
    }
    if (out.type == "new_game") {
        return parseNewGame(j, out, error);
    }
    if (out.type == "command") {
        sim::Command command;
        if (!commandFrom(j, command)) {
            error = "bad command record";
            return false;
        }
        out.command = command;
        return true;
    }
    if (out.type == "day" || out.type == "load") {
        const auto hashIt = j.find("hash");
        if (hashIt == j.end() || !parseHex(*hashIt, out.hash)) {
            error = "bad hash";
            return false;
        }
    }
    parseStrings(j, out);
    if (out.type == "end" && !parseOutcome(j, out)) {
        error = "bad outcome";
        return false;
    }
    if (out.type == "load" && out.file.empty()) {
        error = "load record without a file";
        return false;
    }
    return true;
}

}  // namespace game
