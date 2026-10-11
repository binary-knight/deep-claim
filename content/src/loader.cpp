#include "content/loader.h"

#include "sim/commands.h"
#include "sim/content.h"
#include "sim/fixed.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace content {

namespace {

using nlohmann::json;

constexpr std::int64_t kMaxDays = 36000;      // 100 in-game years
constexpr std::int64_t kMaxProgramStage = 3;  // the program has three stages (spec 010)

// Largest magnitudes accepted from data files. Chosen so that a double keeps
// well over the required resolution (0.01 or 0.000001) at the extreme.
constexpr double kMaxMoneyMagnitude = 1e11;  // currency units
constexpr double kMaxFixedMagnitude = 1e6;

// ---------------------------------------------------------------------------
// Error collection and typed JSON access
// ---------------------------------------------------------------------------

class ErrorSink {
public:
    ErrorSink(std::vector<LoadError>& errors, std::string file)
        : errors_(&errors), file_(std::move(file)) {}

    void add(std::string path, std::string message) {
        errors_->push_back({file_, std::move(path), std::move(message)});
    }

private:
    std::vector<LoadError>* errors_;
    std::string file_;
};

std::string joinPath(const std::string& base, std::string_view key) {
    if (base.empty()) {
        return std::string(key);
    }
    return base + "." + std::string(key);
}

std::string indexPath(const std::string& base, std::size_t index) {
    return base + "[" + std::to_string(index) + "]";
}

std::string_view typeName(const json& value) {
    switch (value.type()) {
        case json::value_t::object:
            return "an object";
        case json::value_t::array:
            return "an array";
        case json::value_t::string:
            return "a string";
        case json::value_t::boolean:
            return "a boolean";
        case json::value_t::number_integer:
        case json::value_t::number_unsigned:
        case json::value_t::number_float:
            return "a number";
        case json::value_t::null:
            return "null";
        default:
            return "an unsupported value";
    }
}

const json& emptyObject() {
    static const json kEmpty = json::object();
    return kEmpty;
}

// A JSON object with its path, offering typed accessors that report problems
// to the sink and return nullopt so parsing can continue.
class Node {
public:
    Node(const json& value, std::string path, ErrorSink& sink)
        : value_(&value), path_(std::move(path)), sink_(&sink) {}

    // Stand-in for an array element that was not an object. It keeps array
    // indices aligned with later diagnostics while reporting nothing itself,
    // because the element has already been reported once.
    static Node placeholder(std::string path, ErrorSink& sink) {
        Node node(emptyObject(), std::move(path), sink);
        node.silent_ = true;
        return node;
    }

    [[nodiscard]] const json& raw() const { return *value_; }
    [[nodiscard]] const std::string& path() const { return path_; }

    void report(std::string_view key, std::string message) const {
        sink_->add(joinPath(path_, key), std::move(message));
    }

    void allowKeys(std::initializer_list<std::string_view> allowed) const {
        for (const auto& item : value_->items()) {
            if (std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end()) {
                sink_->add(joinPath(path_, item.key()), "unknown key");
            }
        }
    }

    [[nodiscard]] std::optional<std::string> string(std::string_view key, bool required) const {
        const json* value = get(key, required);
        if (value == nullptr) {
            return std::nullopt;
        }
        if (!value->is_string()) {
            report(key, "expected a string, got " + std::string(typeName(*value)));
            return std::nullopt;
        }
        auto text = value->get<std::string>();
        if (text.empty()) {
            report(key, "must not be empty");
            return std::nullopt;
        }
        return text;
    }

    [[nodiscard]] std::optional<bool> boolean(std::string_view key, bool required) const {
        const json* value = get(key, required);
        if (value == nullptr) {
            return std::nullopt;
        }
        if (!value->is_boolean()) {
            report(key, "expected true or false, got " + std::string(typeName(*value)));
            return std::nullopt;
        }
        return value->get<bool>();
    }

    [[nodiscard]] std::optional<std::int64_t> integer(std::string_view key, bool required,
                                                      std::int64_t min, std::int64_t max) const {
        const json* value = get(key, required);
        if (value == nullptr) {
            return std::nullopt;
        }
        if (!value->is_number_integer()) {
            report(key, "expected an integer, got " + std::string(typeName(*value)));
            return std::nullopt;
        }
        if (value->is_number_unsigned() &&
            value->get<std::uint64_t>() >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            report(key, "integer is too large");
            return std::nullopt;
        }
        const auto number = value->get<std::int64_t>();
        if (number < min || number > max) {
            report(key, "must be between " + std::to_string(min) + " and " + std::to_string(max));
            return std::nullopt;
        }
        return number;
    }

    [[nodiscard]] std::optional<sim::Fixed> fixed(std::string_view key, bool required,
                                                  sim::Fixed min, sim::Fixed max) const {
        const std::optional<double> number =
            decimal(key, required, static_cast<double>(sim::Fixed::kScale), 6, kMaxFixedMagnitude);
        if (!number) {
            return std::nullopt;
        }
        const sim::Fixed result = sim::Fixed::fromDouble(*number);
        if (result < min || result > max) {
            report(key, "must be between " + sim::toString(min) + " and " + sim::toString(max));
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] std::optional<sim::Money> money(std::string_view key, bool required,
                                                  sim::Money min) const {
        const std::optional<double> number = decimal(
            key, required, static_cast<double>(sim::Money::kCentsPerUnit), 2, kMaxMoneyMagnitude);
        if (!number) {
            return std::nullopt;
        }
        const sim::Money result = sim::Money::fromCents(static_cast<std::int64_t>(
            std::llround(*number * static_cast<double>(sim::Money::kCentsPerUnit))));
        if (result < min) {
            report(key, "must be at least " + sim::toString(min));
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] std::optional<Node> object(std::string_view key, bool required) const {
        const json* value = get(key, required);
        if (value == nullptr) {
            return std::nullopt;
        }
        if (!value->is_object()) {
            report(key, "expected an object, got " + std::string(typeName(*value)));
            return std::nullopt;
        }
        return Node(*value, joinPath(path_, key), *sink_);
    }

    // Array of objects. Non-object elements are reported and skipped.
    [[nodiscard]] std::optional<std::vector<Node>> objectArray(std::string_view key,
                                                               bool required) const {
        const json* value = get(key, required);
        if (value == nullptr) {
            return std::nullopt;
        }
        if (!value->is_array()) {
            report(key, "expected an array, got " + std::string(typeName(*value)));
            return std::nullopt;
        }
        std::vector<Node> nodes;
        const std::string base = joinPath(path_, key);
        for (std::size_t i = 0; i < value->size(); ++i) {
            const json& element = (*value)[i];
            if (!element.is_object()) {
                sink_->add(indexPath(base, i),
                           "expected an object, got " + std::string(typeName(element)));
                continue;
            }
            nodes.emplace_back(element, indexPath(base, i), *sink_);
        }
        return nodes;
    }

    [[nodiscard]] std::optional<std::vector<std::string>> stringArray(std::string_view key,
                                                                      bool required) const {
        const json* value = get(key, required);
        if (value == nullptr) {
            return std::nullopt;
        }
        if (!value->is_array()) {
            report(key, "expected an array, got " + std::string(typeName(*value)));
            return std::nullopt;
        }
        std::vector<std::string> items;
        const std::string base = joinPath(path_, key);
        for (std::size_t i = 0; i < value->size(); ++i) {
            const json& element = (*value)[i];
            if (!element.is_string()) {
                sink_->add(indexPath(base, i),
                           "expected a string, got " + std::string(typeName(element)));
                continue;
            }
            items.push_back(element.get<std::string>());
        }
        return items;
    }

private:
    [[nodiscard]] const json* get(std::string_view key, bool required) const {
        const auto it = value_->find(key);
        if (it == value_->end() || it->is_null()) {
            if (required && !silent_) {
                report(key, "missing required key");
            }
            return nullptr;
        }
        return &*it;
    }

    // Finite number of bounded magnitude representable with `decimals`
    // fractional digits. The magnitude bound keeps the double's spacing far
    // below the smallest unit so the precision check and the final rounding
    // are reliable; excess digits below a thousandth of a unit are rounded
    // away silently, which is the accepted limit of parsing via double.
    [[nodiscard]] std::optional<double> decimal(std::string_view key, bool required, double scale,
                                                int decimals, double maxMagnitude) const {
        const json* value = get(key, required);
        if (value == nullptr) {
            return std::nullopt;
        }
        if (!value->is_number()) {
            report(key, "expected a number, got " + std::string(typeName(*value)));
            return std::nullopt;
        }
        const auto number = value->get<double>();
        if (!std::isfinite(number)) {
            report(key, "must be a finite number");
            return std::nullopt;
        }
        if (std::abs(number) > maxMagnitude) {
            report(key, "magnitude must not exceed " +
                            std::to_string(static_cast<std::int64_t>(maxMagnitude)));
            return std::nullopt;
        }
        const double scaled = number * scale;
        if (std::abs(scaled - std::round(scaled)) > 1e-2) {
            report(key, "at most " + std::to_string(decimals) + " decimal places allowed");
            return std::nullopt;
        }
        return number;
    }

    const json* value_;
    std::string path_;
    ErrorSink* sink_;
    bool silent_ = false;
};

// ---------------------------------------------------------------------------
// Document-level helpers
// ---------------------------------------------------------------------------

std::optional<json> parseDocument(const std::string& text, ErrorSink& sink) {
    try {
        std::optional<json> document;
        document.emplace(json::parse(text));
        return document;
    } catch (const json::exception& error) {
        // parse_error for syntax, out_of_range for numbers such as 1e999.
        sink.add("", std::string("invalid JSON: ") + error.what());
        return std::nullopt;
    }
}

std::optional<std::vector<Node>> topLevelArray(const json& document, ErrorSink& sink) {
    if (!document.is_array()) {
        sink.add("", "expected a top-level array, got " + std::string(typeName(document)));
        return std::nullopt;
    }
    std::vector<Node> nodes;
    for (std::size_t i = 0; i < document.size(); ++i) {
        const json& element = document[i];
        if (!element.is_object()) {
            sink.add(indexPath("", i), "expected an object, got " + std::string(typeName(element)));
            nodes.push_back(Node::placeholder(indexPath("", i), sink));
            continue;
        }
        nodes.emplace_back(element, indexPath("", i), sink);
    }
    return nodes;
}

bool isValidId(std::string_view id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

// Reads and validates an "id" field: lowercase snake_case and unique.
std::string readId(const Node& node, std::vector<std::string>& seen) {
    const std::optional<std::string> id = node.string("id", true);
    if (!id) {
        return "";
    }
    if (!isValidId(*id)) {
        node.report("id", "must contain only a-z, 0-9 and underscore");
        return "";
    }
    if (std::find(seen.begin(), seen.end(), *id) != seen.end()) {
        node.report("id", "duplicate id '" + *id + "'");
        return "";
    }
    seen.push_back(*id);
    return *id;
}

template <class T, class Fallback>
T valueOr(const std::optional<T>& value, Fallback fallback) {
    return value.value_or(static_cast<T>(fallback));
}

// ---------------------------------------------------------------------------
// Per-file parsers
// ---------------------------------------------------------------------------

struct TechEffectName {
    std::string_view name;
    sim::TechEffectType type;
    bool usesValue;
};

constexpr std::array<TechEffectName, 6> kTechEffectNames = {{
    {"survey_noise_mult", sim::TechEffectType::SurveyNoiseMult, true},
    {"extraction_capex_mult", sim::TechEffectType::ExtractionCapexMult, true},
    {"opex_mult", sim::TechEffectType::OpexMult, true},
    {"output_mult", sim::TechEffectType::OutputMult, true},
    {"storage_cost_mult", sim::TechEffectType::StorageCostMult, true},
    {"unlock_resource", sim::TechEffectType::UnlockResource, false},
}};

struct EventEffectName {
    std::string_view name;
    sim::EventEffectType type;
    bool usesResource;
};

constexpr std::array<EventEffectName, 3> kEventEffectNames = {{
    {"demand_mult", sim::EventEffectType::DemandMult, true},
    {"opex_mult", sim::EventEffectType::OpexMult, false},
    {"land_price_mult", sim::EventEffectType::LandPriceMult, false},
}};

// Pointer-returning lookup so the result type is the same on every standard
// library (MSVC's std::array iterator is a class, libstdc++'s is a pointer).
template <class Table>
const typename Table::value_type* findByName(const Table& table, std::string_view name) {
    for (const auto& entry : table) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

// Effect multipliers: a hundredth to a hundredfold keeps every derived
// quantity (capex, opex, output, noise) inside int64 at the loader's bounds.
const sim::Fixed kMultiplierMin = sim::Fixed::fromRatio(1, 100);
const sim::Fixed kMultiplierMax = sim::Fixed::fromInt(100);

// Resources are parsed before techs exist, so unlocked_by names are returned
// for resolution later.
std::vector<std::string> parseResources(const json& document, sim::Content& content,
                                        ErrorSink& sink) {
    std::vector<std::string> unlockedByNames;
    const auto nodes = topLevelArray(document, sink);
    if (!nodes) {
        return unlockedByNames;
    }
    if (nodes->empty()) {
        sink.add("", "at least one resource is required");
    }
    std::vector<std::string> seen;
    for (const Node& node : *nodes) {
        node.allowKeys({"id", "name", "base_price", "base_demand_tonnes_per_day",
                        "base_output_tonnes_per_day", "elasticity", "volatility",
                        "storage_cost_per_tonne_month", "mine_opex_per_day", "unlocked_by"});
        sim::ResourceDef def;
        def.id = readId(node, seen);
        def.name = valueOr(node.string("name", true), std::string());
        def.basePrice =
            valueOr(node.money("base_price", true, sim::Money::fromCents(1)), sim::Money::zero());
        def.baseDemandTonnesPerDay =
            valueOr(node.integer("base_demand_tonnes_per_day", true, 1, 1'000'000'000LL), 0LL);
        def.baseOutputTonnesPerDay =
            valueOr(node.integer("base_output_tonnes_per_day", true, 1, 1'000'000LL), 0LL);
        def.elasticity = valueOr(
            node.fixed("elasticity", true, sim::Fixed::fromRatio(1, 100), sim::Fixed::fromInt(5)),
            sim::Fixed::zero());
        def.volatility =
            valueOr(node.fixed("volatility", true, sim::Fixed::zero(), sim::Fixed::one()),
                    sim::Fixed::zero());
        def.storageCostPerTonneMonth =
            valueOr(node.money("storage_cost_per_tonne_month", true, sim::Money::zero()),
                    sim::Money::zero());
        // Spec 008: a mine that costs nothing to run can never lose money.
        def.mineOpexPerDay = valueOr(
            node.money("mine_opex_per_day", true, sim::Money::fromCents(1)), sim::Money::zero());
        unlockedByNames.push_back(valueOr(node.string("unlocked_by", false), std::string()));
        content.resources.push_back(std::move(def));
    }
    return unlockedByNames;
}

void parseTechEffect(const Node& node, const sim::Content& content, sim::TechDef& tech) {
    node.allowKeys({"type", "value", "resource"});
    const std::optional<std::string> typeName = node.string("type", true);
    if (!typeName) {
        return;
    }
    const TechEffectName* entry = findByName(kTechEffectNames, *typeName);
    if (entry == nullptr) {
        node.report("type", "unknown tech effect type '" + *typeName + "'");
        return;
    }
    sim::TechEffect effect;
    effect.type = entry->type;
    if (entry->usesValue) {
        effect.value =
            valueOr(node.fixed("value", true, kMultiplierMin, kMultiplierMax), sim::Fixed::one());
        if (node.raw().contains("resource")) {
            node.report("resource", "not allowed for this effect type");
        }
    } else {
        if (node.raw().contains("value")) {
            node.report("value", "not allowed for this effect type");
        }
        const std::optional<std::string> resourceName = node.string("resource", true);
        if (resourceName) {
            effect.resource = content.findResource(*resourceName);
            if (!effect.resource.valid()) {
                node.report("resource", "unknown resource '" + *resourceName + "'");
            }
        }
    }
    tech.effects.push_back(effect);
}

// Techs reference each other, so prerequisites are resolved in a second pass.
void parseTechs(const json& document, sim::Content& content, ErrorSink& sink) {
    const auto nodes = topLevelArray(document, sink);
    if (!nodes) {
        return;
    }
    std::vector<std::string> seen;
    std::vector<std::vector<std::string>> requiresNames;
    for (const Node& node : *nodes) {
        node.allowKeys(
            {"id", "cost", "duration_days", "requires_program_stage", "requires", "effects"});
        sim::TechDef def;
        def.id = readId(node, seen);  // name and description come from the catalogue
        def.cost = valueOr(node.money("cost", true, sim::Money::zero()), sim::Money::zero());
        def.durationDays =
            static_cast<sim::Day>(valueOr(node.integer("duration_days", true, 1, kMaxDays), 1LL));
        // Optional: a project outside the program requires no stage.
        def.requiresProgramStage = static_cast<std::uint32_t>(
            valueOr(node.integer("requires_program_stage", false, 0, kMaxProgramStage), 0LL));
        requiresNames.push_back(
            valueOr(node.stringArray("requires", false), std::vector<std::string>()));
        if (const auto effects = node.objectArray("effects", true)) {
            for (const Node& effectNode : *effects) {
                parseTechEffect(effectNode, content, def);
            }
        }
        content.techs.push_back(std::move(def));
    }

    for (std::size_t i = 0; i < content.techs.size(); ++i) {
        const Node& node = (*nodes)[i];
        for (std::size_t j = 0; j < requiresNames[i].size(); ++j) {
            const std::string& name = requiresNames[i][j];
            const sim::TechId id = content.findTech(name);
            const std::string path = indexPath("requires", j);
            if (!id.valid()) {
                node.report(path, "unknown tech '" + name + "'");
            } else if (id.index() == i) {
                node.report(path, "a tech cannot require itself");
            } else {
                content.techs[i].prerequisites.push_back(id);
            }
        }
    }
}

// Depth-first search for prerequisite cycles. 0 = unvisited, 1 = on the
// current path, 2 = finished.
bool hasCycleFrom(const sim::Content& content, std::size_t index, std::vector<int>& state) {
    if (state[index] == 1) {
        return true;
    }
    if (state[index] == 2) {
        return false;
    }
    state[index] = 1;
    for (const sim::TechId prerequisite : content.techs[index].prerequisites) {
        if (hasCycleFrom(content, prerequisite.index(), state)) {
            return true;
        }
    }
    state[index] = 2;
    return false;
}

void checkTechCycles(const sim::Content& content, ErrorSink& sink) {
    std::vector<int> state(content.techs.size(), 0);
    for (std::size_t i = 0; i < content.techs.size(); ++i) {
        std::fill(state.begin(), state.end(), 0);
        if (hasCycleFrom(content, i, state)) {
            sink.add(indexPath("", i) + ".requires",
                     "prerequisite cycle involving '" + content.techs[i].id + "'");
        }
    }
}

// Links resources.unlocked_by with techs' unlock_resource effects and checks
// the two agree.
void resolveUnlocks(const std::vector<std::string>& unlockedByNames, sim::Content& content,
                    ErrorSink& resourceSink, ErrorSink& techSink) {
    for (std::size_t i = 0; i < content.resources.size(); ++i) {
        const std::string& name = unlockedByNames[i];
        if (name.empty()) {
            continue;
        }
        const sim::TechId tech = content.findTech(name);
        if (!tech.valid()) {
            resourceSink.add(indexPath("", i) + ".unlocked_by", "unknown tech '" + name + "'");
            continue;
        }
        content.resources[i].unlockedBy = tech;
        const auto& effects = content.techs[tech.index()].effects;
        const bool unlocks = std::any_of(effects.begin(), effects.end(), [&](const auto& e) {
            return e.type == sim::TechEffectType::UnlockResource &&
                   e.resource == sim::ResourceId(static_cast<std::uint32_t>(i));
        });
        if (!unlocks) {
            resourceSink.add(indexPath("", i) + ".unlocked_by",
                             "tech '" + name + "' has no unlock_resource effect for '" +
                                 content.resources[i].id + "'");
        }
    }
    for (std::size_t t = 0; t < content.techs.size(); ++t) {
        for (std::size_t e = 0; e < content.techs[t].effects.size(); ++e) {
            const sim::TechEffect& effect = content.techs[t].effects[e];
            if (effect.type != sim::TechEffectType::UnlockResource || !effect.resource.valid()) {
                continue;
            }
            const sim::ResourceDef& resource = content.resources[effect.resource.index()];
            if (resource.unlockedBy != sim::TechId(static_cast<std::uint32_t>(t))) {
                techSink.add(indexPath(indexPath("", t) + ".effects", e) + ".resource",
                             "resource '" + resource.id + "' is not marked unlocked_by '" +
                                 content.techs[t].id + "'");
            }
        }
    }
}

void parseRange(const Node& parent, std::string_view key, sim::Money& min, sim::Money& max) {
    const std::optional<Node> range = parent.object(key, true);
    if (!range) {
        return;
    }
    range->allowKeys({"min", "max"});
    min = valueOr(range->money("min", true, sim::Money::fromCents(1)), sim::Money::zero());
    max = valueOr(range->money("max", true, sim::Money::fromCents(1)), sim::Money::zero());
    if (min > max) {
        range->report("max", "must not be below min");
    }
}

void parseRange(const Node& parent, std::string_view key, std::int64_t& min, std::int64_t& max) {
    const std::optional<Node> range = parent.object(key, true);
    if (!range) {
        return;
    }
    range->allowKeys({"min", "max"});
    min = valueOr(range->integer("min", true, 1, 1'000'000), 0LL);
    max = valueOr(range->integer("max", true, 1, 1'000'000), 0LL);
    if (min > max) {
        range->report("max", "must not be below min");
    }
}

void parseDeposits(const Node& region, const sim::Content& content, sim::RegionDef& def,
                   ErrorSink& sink) {
    const std::optional<Node> deposits = region.object("deposits", true);
    if (!deposits) {
        return;
    }
    for (const auto& item : deposits->raw().items()) {
        const std::string path = joinPath(deposits->path(), item.key());
        const sim::ResourceId resource = content.findResource(item.key());
        if (!resource.valid()) {
            sink.add(path, "unknown resource '" + item.key() + "'");
            continue;
        }
        if (!item.value().is_object()) {
            sink.add(path, "expected an object, got " + std::string(typeName(item.value())));
            continue;
        }
        const Node node(item.value(), path, sink);
        node.allowKeys({"presence_prob", "tonnes_median", "tonnes_spread", "tonnes_max_mult"});
        sim::DepositPrior prior;
        prior.resource = resource;
        prior.presenceProb =
            valueOr(node.fixed("presence_prob", true, sim::Fixed::zero(), sim::Fixed::one()),
                    sim::Fixed::zero());
        prior.tonnesMedian =
            valueOr(node.integer("tonnes_median", true, 1, 1'000'000'000'000LL), 0LL);
        prior.tonnesSpread =
            valueOr(node.fixed("tonnes_spread", true, sim::Fixed::one(), sim::Fixed::fromInt(20)),
                    sim::Fixed::one());
        prior.tonnesMaxMult = valueOr(
            node.fixed("tonnes_max_mult", true, sim::Fixed::one(), sim::Fixed::fromInt(1'000'000)),
            sim::Fixed::one());
        def.deposits.push_back(prior);
    }
}

void parseRegions(const json& document, sim::Content& content, ErrorSink& sink) {
    const auto nodes = topLevelArray(document, sink);
    if (!nodes) {
        return;
    }
    if (nodes->empty()) {
        sink.add("", "at least one region is required");
    }
    std::vector<std::string> seen;
    for (const Node& node : *nodes) {
        node.allowKeys(
            {"id", "name", "plot_count", "land_price", "plot_area_hectares", "deposits"});
        sim::RegionDef def;
        def.id = readId(node, seen);
        def.name = valueOr(node.string("name", true), std::string());
        def.plotCount =
            static_cast<std::uint32_t>(valueOr(node.integer("plot_count", true, 1, 1000), 0LL));
        parseRange(node, "land_price", def.landPriceMin, def.landPriceMax);
        parseRange(node, "plot_area_hectares", def.plotAreaMinHectares, def.plotAreaMaxHectares);
        parseDeposits(node, content, def, sink);
        content.regions.push_back(std::move(def));
    }
}

void parseEventEffect(const Node& node, const sim::Content& content, sim::EventDef& event) {
    node.allowKeys({"type", "resource", "value"});
    const std::optional<std::string> typeName = node.string("type", true);
    if (!typeName) {
        return;
    }
    const EventEffectName* entry = findByName(kEventEffectNames, *typeName);
    if (entry == nullptr) {
        node.report("type", "unknown event effect type '" + *typeName + "'");
        return;
    }
    sim::EventEffect effect;
    effect.type = entry->type;
    effect.value =
        valueOr(node.fixed("value", true, kMultiplierMin, kMultiplierMax), sim::Fixed::one());
    if (entry->usesResource) {
        const std::optional<std::string> resourceName = node.string("resource", true);
        if (resourceName) {
            effect.resource = content.findResource(*resourceName);
            if (!effect.resource.valid()) {
                node.report("resource", "unknown resource '" + *resourceName + "'");
            }
        }
    } else if (node.raw().contains("resource")) {
        node.report("resource", "not allowed for this effect type");
    }
    event.effects.push_back(effect);
}

void parseEvents(const json& document, sim::Content& content, ErrorSink& sink) {
    const auto nodes = topLevelArray(document, sink);
    if (!nodes) {
        return;
    }
    std::vector<std::string> seen;
    for (const Node& node : *nodes) {
        node.allowKeys({"id", "daily_weight", "min_year", "duration_days", "effects"});
        sim::EventDef def;
        def.id = readId(node, seen);  // name and description come from the catalogue
        def.dailyWeight =
            valueOr(node.fixed("daily_weight", true, sim::Fixed::zero(), sim::Fixed::one()),
                    sim::Fixed::zero());
        def.minYear =
            static_cast<std::uint32_t>(valueOr(node.integer("min_year", false, 1, 1000), 1LL));
        def.durationDays =
            static_cast<sim::Day>(valueOr(node.integer("duration_days", true, 1, kMaxDays), 1LL));
        if (const auto effects = node.objectArray("effects", true)) {
            for (const Node& effectNode : *effects) {
                parseEventEffect(effectNode, content, def);
            }
        }
        content.events.push_back(std::move(def));
    }
}

// Bounds on the market clamps (spec 008). The price target is base price
// times ratio^elasticity, and the loader allows an elasticity of 5 and a base
// price of 1e11: a ratio of 10 keeps that product inside int64 cents, and a
// ratio of 1/100 keeps elasticity x ln(ratio) inside fixedExp's range. The
// demand factor multiplies a base demand of up to 1e9 tonnes, so 1000 is the
// most that still fits Fixed.
//
// The ratio clamp must also straddle 1: the minimum may not exceed 1 and the
// maximum may not fall below it. Base price is the price at a ratio of 1, so
// a clamp wholly on one side would hold every price above or below base for
// good, and the AI clamps its supply discount between the minimum and 1
// (sim/src/ai_controller.cpp), which a minimum above 1 turns into a premium.
// The demand factor has no such pivot: each end may sit anywhere in its range.
const sim::Fixed kMarketRatioMin = sim::Fixed::fromRatio(1, 100);
const sim::Fixed kMarketRatioMax = sim::Fixed::fromInt(10);
const sim::Fixed kDemandFactorMin = sim::Fixed::fromRaw(1);  // the smallest value above zero
const sim::Fixed kDemandFactorMax = sim::Fixed::fromInt(1000);

// The lower end must lie in [lowest, minCeiling] and the upper end in
// [maxFloor, highest].
struct ClampSpec {
    std::string_view minKey;
    std::string_view maxKey;
    sim::Fixed lowest;
    sim::Fixed minCeiling;
    sim::Fixed maxFloor;
    sim::Fixed highest;
};

// One clamp: each end inside its bounds and the lower strictly below the
// upper. The order is checked only when both ends are usable, so a value
// already rejected is reported once.
void parseClamp(const Node& node, const ClampSpec& spec, sim::Fixed& min, sim::Fixed& max) {
    const std::optional<sim::Fixed> low =
        node.fixed(spec.minKey, true, spec.lowest, spec.minCeiling);
    const std::optional<sim::Fixed> high =
        node.fixed(spec.maxKey, true, spec.maxFloor, spec.highest);
    if (low && high && *low >= *high) {
        node.report(spec.maxKey, "must be greater than " + std::string(spec.minKey));
    }
    min = valueOr(low, spec.lowest);
    max = valueOr(high, spec.highest);
}

void parseMarketClamps(const Node& node, sim::BalanceParams& balance) {
    const ClampSpec ratio{.minKey = "market_ratio_min",
                          .maxKey = "market_ratio_max",
                          .lowest = kMarketRatioMin,
                          .minCeiling = sim::Fixed::one(),
                          .maxFloor = sim::Fixed::one(),
                          .highest = kMarketRatioMax};
    const ClampSpec demand{.minKey = "demand_factor_min",
                           .maxKey = "demand_factor_max",
                           .lowest = kDemandFactorMin,
                           .minCeiling = kDemandFactorMax,
                           .maxFloor = kDemandFactorMin,
                           .highest = kDemandFactorMax};
    parseClamp(node, ratio, balance.marketRatioMin, balance.marketRatioMax);
    parseClamp(node, demand, balance.demandFactorMin, balance.demandFactorMax);
}

void parseBalance(const json& document, sim::BalanceParams& balance, ErrorSink& sink) {
    if (!document.is_object()) {
        sink.add("", "expected a top-level object, got " + std::string(typeName(document)));
        return;
    }
    const Node node(document, "", sink);
    node.allowKeys({"starting_capital",
                    "loan_annual_rate",
                    "loan_term_months",
                    "loan_max_principal",
                    "loan_min_principal",
                    "max_active_loans",
                    "survey_cost",
                    "survey_duration_days",
                    "survey_base_noise",
                    "mine_capex_per_hectare",
                    "mine_build_days",
                    "mine_mothball_opex_mult",
                    "auction_window_days",
                    "plots_listed_at_start",
                    "market_supply_smoothing_days",
                    "market_price_adjust_days",
                    "demand_reversion_rate",
                    "demand_mult_min",
                    "demand_mult_max",
                    "price_floor_mult",
                    "price_cap_mult",
                    "demand_growth_per_year",
                    "market_ratio_min",
                    "market_ratio_max",
                    "demand_factor_min",
                    "demand_factor_max",
                    "mine_loss_notice_days",
                    "mine_loss_notice_min_share",
                    "mine_loss_rearm_days",
                    "plots_listed_per_month",
                    "opening_min_mineable",
                    "opening_max_locked_region",
                    "opening_mineable_presence",
                    "max_unsold_rounds",
                    "relist_cooldown_days",
                    "bankruptcy_grace_months",
                    "default_sell_policy",
                    "default_cover_costs"});
    const sim::Money zero = sim::Money::zero();
    balance.startingCapital =
        valueOr(node.money("starting_capital", true, sim::Money::fromCents(1)), zero);
    balance.loanAnnualRate =
        valueOr(node.fixed("loan_annual_rate", true, sim::Fixed::zero(), sim::Fixed::one()),
                sim::Fixed::zero());
    balance.loanTermMonths =
        static_cast<std::uint32_t>(valueOr(node.integer("loan_term_months", true, 1, 1200), 1LL));
    balance.loanMaxPrincipal = valueOr(node.money("loan_max_principal", true, zero), zero);
    balance.loanMinPrincipal =
        valueOr(node.money("loan_min_principal", true, sim::Money::fromCents(1)), zero);
    if (balance.loanMinPrincipal > balance.loanMaxPrincipal) {
        node.report("loan_min_principal", "must not exceed loan_max_principal");
    }
    balance.maxActiveLoans =
        static_cast<std::uint32_t>(valueOr(node.integer("max_active_loans", true, 1, 100), 1LL));
    balance.surveyCost = valueOr(node.money("survey_cost", true, zero), zero);
    balance.surveyDurationDays = static_cast<sim::Day>(
        valueOr(node.integer("survey_duration_days", true, 1, kMaxDays), 1LL));
    balance.surveyBaseNoise =
        valueOr(node.fixed("survey_base_noise", true, sim::Fixed::zero(), sim::Fixed::fromInt(10)),
                sim::Fixed::zero());
    balance.mineCapexPerHectare = valueOr(node.money("mine_capex_per_hectare", true, zero), zero);
    if (balance.mineCapexPerHectare > sim::Money::fromUnits(100'000'000)) {
        node.report("mine_capex_per_hectare",
                    "must not exceed 100000000 (times plot area and tech)");
    }
    balance.mineBuildDays =
        static_cast<sim::Day>(valueOr(node.integer("mine_build_days", true, 1, kMaxDays), 1LL));
    balance.mineMothballOpexMult =
        valueOr(node.fixed("mine_mothball_opex_mult", true, sim::Fixed::zero(), sim::Fixed::one()),
                sim::Fixed::zero());
    balance.auctionWindowDays = static_cast<sim::Day>(
        valueOr(node.integer("auction_window_days", true, 1, sim::kDaysPerYear), 1LL));
    balance.plotsListedAtStart = static_cast<std::uint32_t>(
        valueOr(node.integer("plots_listed_at_start", true, 0, 1000), 0LL));
    balance.plotsListedPerMonth = static_cast<std::uint32_t>(
        valueOr(node.integer("plots_listed_per_month", true, 0, 1000), 0LL));
    // Spec 005: the opening hand. More mineable plots than the hand holds
    // is a contradiction; world generation relaxes whatever the regions
    // themselves cannot supply.
    balance.openingMinMineable = static_cast<std::uint32_t>(
        valueOr(node.integer("opening_min_mineable", true, 0, 1000), 0LL));
    if (balance.openingMinMineable > balance.plotsListedAtStart) {
        node.report("opening_min_mineable", "must not exceed plots_listed_at_start");
    }
    balance.openingMaxLockedRegion = static_cast<std::uint32_t>(
        valueOr(node.integer("opening_max_locked_region", true, 0, 1000), 0LL));
    balance.openingMineablePresence = valueOr(
        node.fixed("opening_mineable_presence", true, sim::Fixed::zero(), sim::Fixed::one()),
        sim::Fixed::one());
    balance.maxUnsoldRounds =
        static_cast<std::uint32_t>(valueOr(node.integer("max_unsold_rounds", true, 0, 100), 0LL));
    balance.relistCooldownDays = static_cast<sim::Day>(valueOr(
        node.integer("relist_cooldown_days", true, 0, std::int64_t{sim::kDaysPerYear} * 100), 0LL));
    balance.bankruptcyGraceMonths = static_cast<std::uint32_t>(
        valueOr(node.integer("bankruptcy_grace_months", true, 0, 120), 0LL));
    balance.marketSupplySmoothingDays = static_cast<std::uint32_t>(
        valueOr(node.integer("market_supply_smoothing_days", true, 1, 3600), 1LL));
    balance.marketPriceAdjustDays = static_cast<std::uint32_t>(
        valueOr(node.integer("market_price_adjust_days", true, 1, 3600), 1LL));
    balance.demandReversionRate =
        valueOr(node.fixed("demand_reversion_rate", true, sim::Fixed::zero(), sim::Fixed::one()),
                sim::Fixed::zero());
    balance.demandMultMin = valueOr(
        node.fixed("demand_mult_min", true, sim::Fixed::fromRatio(1, 100), sim::Fixed::one()),
        sim::Fixed::one());
    balance.demandMultMax =
        valueOr(node.fixed("demand_mult_max", true, sim::Fixed::one(), sim::Fixed::fromInt(100)),
                sim::Fixed::one());
    balance.priceFloorMult = valueOr(
        node.fixed("price_floor_mult", true, sim::Fixed::fromRatio(1, 100), sim::Fixed::one()),
        sim::Fixed::one());
    balance.priceCapMult =
        valueOr(node.fixed("price_cap_mult", true, sim::Fixed::one(), sim::Fixed::fromInt(100)),
                sim::Fixed::one());
    balance.demandGrowthPerYear = valueOr(
        node.fixed("demand_growth_per_year", true, sim::Fixed::zero(), sim::Fixed::fromRatio(1, 2)),
        sim::Fixed::zero());
    parseMarketClamps(node, balance);
    balance.mineLossNoticeDays = static_cast<std::uint32_t>(
        valueOr(node.integer("mine_loss_notice_days", true, 1, kMaxDays), 1LL));
    // Spec 010 (BR-014): what counts as a losing day, and how long a mine
    // must go without one before the notice can be raised again.
    balance.mineLossNoticeMinShare = valueOr(
        node.fixed("mine_loss_notice_min_share", true, sim::Fixed::zero(), sim::Fixed::one()),
        sim::Fixed::zero());
    constexpr std::int64_t kMaxLossRearmDays = 3600;  // ten in-game years
    balance.mineLossRearmDays = static_cast<std::uint32_t>(
        valueOr(node.integer("mine_loss_rearm_days", true, 1, kMaxLossRearmDays), 1LL));
    // Spec 001: the standing sell order every company starts with.
    if (const std::optional<Node> policy = node.object("default_sell_policy", true)) {
        policy->allowKeys({"enabled", "reserve_tonnes", "floor_price"});
        balance.defaultSellPolicy.enabled =
            valueOr(policy->boolean("enabled", true), false) ? 1 : 0;
        balance.defaultSellPolicy.reserveTonnes =
            valueOr(policy->integer("reserve_tonnes", true, 0, sim::kMaxStockTonnes), 0LL);
        balance.defaultSellPolicy.floorPrice =
            valueOr(policy->money("floor_price", true, zero), zero);
    }
    balance.defaultCoverCosts = valueOr(node.boolean("default_cover_costs", true), false);
}

// The built-in competitor's tuning (ai.json, spec 006). Every key is
// required and bounded: the controller trusts these numbers.
void parseAi(const json& document, sim::AiParams& ai, ErrorSink& sink) {
    if (!document.is_object()) {
        sink.add("", "expected a top-level object, got " + std::string(typeName(document)));
        return;
    }
    const Node node(document, "", sink);
    node.allowKeys({"sell_above_base",
                    "dump_storage_share",
                    "recoverable_share",
                    "required_return",
                    "payback_horizon_days",
                    "bid_premium",
                    "bid_premium_high",
                    "max_open_prospects",
                    "reopen_margin",
                    "idle_mine_grace_days",
                    "opex_reserve_days",
                    "cash_reserve",
                    "research_floor",
                    "loan_floor",
                    "loan_chunk",
                    "bootstrap_loan",
                    "first_mine_recoverable_share",
                    "first_mine_required_return",
                    "supply_discount",
                    "program_reserve_share",
                    "program_cash_buffer",
                    "program_ore_land_weight",
                    "program_research_progress"});
    const sim::Fixed zero = sim::Fixed::zero();
    const sim::Fixed one = sim::Fixed::one();
    const sim::Fixed ten = sim::Fixed::fromInt(10);
    const sim::Money none = sim::Money::zero();
    ai.sellAboveBase = valueOr(node.fixed("sell_above_base", true, zero, ten), one);
    ai.dumpStorageShare = valueOr(node.fixed("dump_storage_share", true, zero, one), one);
    ai.recoverableShare = valueOr(node.fixed("recoverable_share", true, zero, one), one);
    ai.requiredReturn = valueOr(node.fixed("required_return", true, one, ten), one);
    ai.firstMineRecoverableShare =
        valueOr(node.fixed("first_mine_recoverable_share", true, zero, one), one);
    ai.firstMineRequiredReturn =
        valueOr(node.fixed("first_mine_required_return", true, one, ten), one);
    ai.supplyDiscount = valueOr(node.fixed("supply_discount", true, zero, one), zero);
    ai.paybackHorizonDays = valueOr(node.integer("payback_horizon_days", true, 1, kMaxDays), 1LL);
    ai.bidPremium = valueOr(node.fixed("bid_premium", true, one, ten), one);
    ai.bidPremiumHigh = valueOr(node.fixed("bid_premium_high", true, one, ten), one);
    if (ai.bidPremiumHigh < ai.bidPremium) {
        node.report("bid_premium_high", "must not be below bid_premium");
    }
    ai.maxOpenProspects =
        static_cast<std::uint32_t>(valueOr(node.integer("max_open_prospects", true, 1, 1000), 1LL));
    ai.reopenMargin = valueOr(node.fixed("reopen_margin", true, one, ten), one);
    ai.idleMineGraceDays = static_cast<sim::Day>(
        valueOr(node.integer("idle_mine_grace_days", true, 0, kMaxDays), 0LL));
    ai.opexReserveDays = valueOr(node.integer("opex_reserve_days", true, 0, 3600), 0LL);
    ai.cashReserve = valueOr(node.money("cash_reserve", true, none), none);
    ai.researchFloor = valueOr(node.money("research_floor", true, none), none);
    ai.loanFloor = valueOr(node.money("loan_floor", true, none), none);
    ai.loanChunk = valueOr(node.money("loan_chunk", true, sim::Money::fromCents(1)), none);
    ai.bootstrapLoan = valueOr(node.money("bootstrap_loan", true, sim::Money::fromCents(1)), none);
    // Spec 010 (BR-011): how the competitor races for the program.
    ai.programReserveShare = valueOr(node.fixed("program_reserve_share", true, zero, one), one);
    ai.programCashBuffer = valueOr(node.money("program_cash_buffer", true, none), none);
    ai.programOreLandWeight = valueOr(node.fixed("program_ore_land_weight", true, one, ten), one);
    ai.programResearchProgress =
        valueOr(node.fixed("program_research_progress", true, zero, one), one);
}

// ---------------------------------------------------------------------------
// The race (program.json, spec 010)
// ---------------------------------------------------------------------------

// The ore the third stage must need (spec 010, BR-002): it is the one ore
// behind research, so the race cannot be won on day-one ores alone.
constexpr std::string_view kFinalStageResource = "lithium";

void parseProgramRequirement(const Node& node, const sim::Content& content,
                             sim::ProgramStage& stage) {
    node.allowKeys({"resource", "tonnes"});
    sim::ProgramRequirement requirement;
    requirement.tonnes = valueOr(node.integer("tonnes", true, 1, sim::kMaxStockTonnes), 0LL);
    if (const std::optional<std::string> name = node.string("resource", true)) {
        requirement.resource = content.findResource(*name);
        const auto& listed = stage.requirements;
        if (!requirement.resource.valid()) {
            node.report("resource", "unknown resource '" + *name + "'");
        } else if (std::any_of(listed.begin(), listed.end(), [&](const auto& earlier) {
                       return earlier.resource == requirement.resource;
                   })) {
            node.report("resource", "duplicate resource '" + *name + "'");
        }
    }
    stage.requirements.push_back(requirement);
}

// A stage's project must be the tech that waits for exactly the stages
// before this one. `position` is absent when the stage list is malformed and
// positions cannot be trusted.
void parseStageResearch(const Node& node, const sim::Content& content,
                        std::optional<std::size_t> position, sim::ProgramStage& stage) {
    const std::optional<std::string> name = node.string("research", true);
    if (!name) {
        return;
    }
    stage.research = content.findTech(*name);
    if (!stage.research.valid()) {
        node.report("research", "unknown tech '" + *name + "'");
        return;
    }
    if (position && content.techs[stage.research.index()].requiresProgramStage != *position) {
        node.report("research", "tech '" + *name + "' must have requires_program_stage " +
                                    std::to_string(*position));
    }
}

sim::ProgramStage parseProgramStage(const Node& node, const sim::Content& content,
                                    std::optional<std::size_t> position,
                                    std::vector<std::string>& seenIds) {
    node.allowKeys({"id", "cost", "research", "requires"});
    sim::ProgramStage stage;
    stage.id = readId(node, seenIds);
    stage.cost = valueOr(node.money("cost", true, sim::Money::fromCents(1)), sim::Money::zero());
    parseStageResearch(node, content, position, stage);
    const auto requirements = node.objectArray("requires", true);
    if (!requirements) {
        return stage;
    }
    for (const Node& requirement : *requirements) {
        parseProgramRequirement(requirement, content, stage);
    }
    const sim::ResourceId finalResource = content.findResource(kFinalStageResource);
    const bool isLast = position && *position + 1 == static_cast<std::size_t>(kMaxProgramStage);
    const auto& listed = stage.requirements;
    if (isLast && (!finalResource.valid() ||
                   std::none_of(listed.begin(), listed.end(), [&](const auto& requirement) {
                       return requirement.resource == finalResource;
                   }))) {
        node.report("requires", "the third stage must require " + std::string(kFinalStageResource));
    }
    return stage;
}

// Runs after resources and techs: stages name both.
void parseProgram(const json& document, const sim::Content& content, sim::ProgramDef& program,
                  ErrorSink& sink) {
    if (!document.is_object()) {
        sink.add("", "expected a top-level object, got " + std::string(typeName(document)));
        return;
    }
    const Node node(document, "", sink);
    node.allowKeys({"deadline_day", "stages"});
    program.deadlineDay =
        static_cast<sim::Day>(valueOr(node.integer("deadline_day", true, 1, kMaxDays), 0LL));
    const auto stages = node.objectArray("stages", true);
    if (!stages) {
        return;
    }
    // The count is taken from the file, so an element that is not an object
    // is reported once, as itself, and not again as a missing stage.
    const auto expected = static_cast<std::size_t>(kMaxProgramStage);
    const std::size_t written = document["stages"].size();
    if (written != expected) {
        node.report("stages", "the program has exactly " + std::to_string(expected) +
                                  " stages, found " + std::to_string(written));
    }
    const bool positionsKnown = written == expected && stages->size() == expected;
    std::vector<std::string> seenIds;
    for (std::size_t i = 0; i < stages->size(); ++i) {
        const std::optional<std::size_t> position =
            positionsKnown ? std::optional<std::size_t>(i) : std::nullopt;
        program.stages.push_back(parseProgramStage((*stages)[i], content, position, seenIds));
    }
}

// ---------------------------------------------------------------------------
// Prose catalogue (strings.json, spec 002)
// ---------------------------------------------------------------------------

// Every fixed key the client looks up, with the placeholders its template may
// use. A key missing from the file, a key not in this table (other than the
// per-id event and tech keys) or a placeholder outside the allowed set is a
// data error, so a typo in the catalogue fails at load rather than in play.
struct StringKeySpec {
    std::string_view key;
    std::string_view placeholders;  // comma separated, no spaces
};

constexpr auto kStringKeys = std::to_array<StringKeySpec>({
    {"notice.command_rejected", "reason"},
    {"notice.command_rejected.amount", "amount,reason"},
    {"notice.command_rejected.plot", "plot,reason"},
    {"notice.command_rejected.plot_amount", "plot,amount,reason"},
    {"notice.command_rejected.sale", "resource,reason"},
    {"notice.command_rejected.research", "tech,reason"},
    {"notice.command_rejected.loan", "reason"},
    {"notice.command_rejected.loan_amount", "amount,reason"},
    {"notice.command_rejected.delivery", "resource,reason"},
    {"notice.command_rejected.stage", "reason"},
    {"notice.plot_listed", "plot,amount"},
    {"notice.plot_relisted", "plot,amount"},
    {"notice.plot_sold", "company,plot,amount"},
    {"notice.auction_lost", "plot"},
    {"notice.bid_forfeited", "plot"},
    {"notice.survey_started", "plot,amount"},
    {"notice.survey_complete", "plot"},
    {"notice.mine_started", "company,plot"},
    {"notice.mine_operational", "plot"},
    {"notice.mine_depleted", "plot"},
    {"notice.ore_sold", "tonnes,resource,amount"},
    {"notice.cover_costs_sale", "tonnes,resource,amount"},
    {"notice.auto_sale_summary", "tonnes,resource,amount"},
    {"notice.idle_mine_warning", "plot"},
    {"notice.mine_found_nothing", "plot"},
    {"notice.mine_running_low", "plot,days"},
    {"ui.days_in", "days"},
    {"ui.needs_tech", "tech"},
    {"ui.remaining_building", "days"},
    {"ui.remaining_exhausted", ""},
    {"ui.remaining_closed", ""},
    {"ui.remaining_mothballed", "tonnes"},
    {"ui.remaining_mothballed_unknown", ""},
    {"ui.remaining_idle", ""},
    {"ui.remaining_unknown", ""},
    {"ui.remaining_tonnes", "tonnes"},
    {"ui.remaining_est", "tonnes,days"},
    {"ui.remaining_no_mine", ""},
    {"ui.solvency_grace", "months"},
    {"ui.solvency_left", "months"},
    {"ui.strip_no_auction", ""},
    {"ui.strip_next_auction", "plot,day"},
    {"ui.strip_research", "tech,day"},
    {"ui.strip_research_idle", ""},
    {"ui.strip_world", "days"},
    {"ui.need_insolvent_title", "months"},
    {"ui.need_insolvent_sub", ""},
    {"ui.need_storage_title", ""},
    {"ui.need_storage_sub", "amount,cash"},
    {"ui.need_idle_title", "plot"},
    {"ui.need_idle_sub", ""},
    {"ui.need_auction_title", "plot"},
    {"ui.need_auction_sub_contested", "bids,amount"},
    {"ui.need_auction_sub_leading", "amount"},
    {"ui.need_auction_sub_rival", "bids"},
    {"ui.need_auction_sub_open", ""},
    {"ui.need_mine_opens_title", "plot"},
    {"ui.need_mine_opens_sub", ""},
    {"ui.need_event_title", "event"},
    {"ui.need_research_title", ""},
    {"ui.need_none", ""},
    {"ui.rival_line", "company,plots,mines,bidding"},
    {"ui.rival_line_quiet", "company,plots,mines"},
    {"ui.fin_can_borrow", "amount"},
    {"ui.fin_storage_sub", "amount"},
    {"ui.fin_instalment", "amount"},
    {"ui.storage_tonnes.one", "tonnes,count"},
    {"ui.storage_tonnes.many", "tonnes,count"},
    {"ui.storage_rate", "percent"},
    {"ui.storage_note", ""},
    {"ui.survey_blind", "odds"},
    {"ui.flag_bids", "bids"},
    {"ui.survey_title", "day"},
    {"ui.survey_never", ""},
    {"ui.auction_closes", "day,bids"},
    {"ui.notice_unsurveyed", "amount"},
    {"ui.notice_survey_pending", ""},
    {"ui.notice_empty", ""},
    {"ui.notice_locked_only", "needs"},
    {"ui.notice_contested", ""},
    {"ui.survey_again", "amount"},
    {"ui.survey_first", "amount"},
    {"ui.build_label", "amount"},
    {"ui.market_base", "price,ratio"},
    {"ui.market_sub", "demand,supply"},
    {"ui.stock_sub", "tonnes"},
    {"ui.auto_sold_none", ""},
    {"ui.chart_note", "demand,supply"},
    {"ui.tech_until", "day"},
    {"ui.tech_busy", "day"},
    {"ui.tech_bankrupt", ""},
    {"ui.tech_unaffordable", "amount"},
    {"ui.lab_one", ""},
    {"ui.lab_busy", "day,tech"},
    {"ui.lab_idle", ""},
    {"ui.loan_rule", "ratio"},
    {"ui.loan_limits", "amount,min,months,count"},
    {"ui.cover_check", ""},
    {"ui.cover_explain", ""},
    {"ui.cover_last", "amount"},
    {"ui.cover_last_none", ""},
    {"ui.solvency_rule", "count"},
    {"ui.pause_on.insolvency", ""},
    {"ui.pause_on.bankrupt", ""},
    {"ui.pause_on.auction_won", ""},
    {"ui.pause_on.auction_lost", ""},
    {"ui.pause_on.survey_complete", ""},
    {"ui.pause_on.mine_operational", ""},
    {"ui.pause_on.mine_found_nothing", ""},
    {"ui.pause_on.idle_mine", ""},
    {"ui.pause_menu_title", ""},
    {"status.paused_by", "text"},
    {"notice.storage_charged", "amount"},
    {"notice.loan_taken", "amount"},
    {"notice.loan_payment", "amount"},
    {"notice.loan_repaid", ""},
    {"notice.insolvency_warning", "months"},
    {"notice.bankrupt", "company"},
    {"notice.research_started", "tech"},
    {"notice.research_complete", "tech"},
    {"notice.event_started", "event"},
    {"notice.event_ended", "event"},
    {"status.screenshot_failed", ""},
    {"status.screenshot_saved", "name"},
    {"status.reserve_prompt", ""},
    {"status.floor_prompt", ""},
    {"status.policy_starts", "resource"},
    {"status.policy_stops", "resource"},
    {"status.cover_on", ""},
    {"status.cover_off", ""},
    {"status.survey_queued", ""},
    {"status.bid_prompt", ""},
    {"status.bid_queued", "amount"},
    {"status.build_queued", ""},
    {"status.mine_reopen", ""},
    {"status.mine_mothball", ""},
    {"status.sell_prompt", ""},
    {"status.sale_queued", "tonnes"},
    {"status.loan_prompt", ""},
    {"status.loan_requested", "amount"},
    {"status.repay_prompt", ""},
    {"status.repayment_ordered", "amount"},
    {"status.research_queued", ""},
    {"status.delivery_queued", "tonnes,resource"},
    {"status.stage_completion_queued", "amount"},
    {"status.save_write_failed", "name"},
    {"status.save_replace_failed", "name,reason"},
    {"status.saved", "name"},
    {"status.no_save", "name"},
    {"status.save_invalid", ""},
    {"status.loaded_copy_failed", ""},
    {"status.loaded", ""},
    {"status.new_game", ""},
    {"ui.shortcuts", ""},
    {"ui.how_to_1", ""},
    {"ui.how_to_2", "survey_cost,survey_days,days,months"},
    {"ui.how_to_3", ""},
    {"ui.company_terms", "amount,ratio"},
    {"ui.finance_cash", "cash,spendable"},
    {"ui.finance_debt", "debt,amount"},
    {"ui.finance_storage", "amount"},
    {"ui.no_events", ""},
    {"ui.plots_intro", ""},
    {"ui.plot_odds", "region,odds"},
    {"ui.plot_costs", "amount,days,survey_cost,survey_days"},
    {"ui.build_blocked", "amount"},
    {"ui.select_plot", ""},
    {"ui.selected_plot_title", "plot,region,area"},
    {"ui.survey_none", ""},
    {"ui.survey_pending", ""},
    {"ui.survey_pending_again", ""},
    {"ui.auction.one", "day,bids"},
    {"ui.auction.many", "day,bids"},
    {"ui.auction_yours", "amount"},
    {"ui.mine_today", "output"},
    {"ui.mine_opex", "amount"},
    {"ui.policy_manual", ""},
    {"ui.policy_auto", ""},
    {"ui.policy_auto_keep", "reserve"},
    {"ui.policy_auto_floor", "price"},
    {"ui.policy_auto_keep_floor", "reserve,price"},
    {"ui.auto_sold", "tonnes,amount"},
    {"ui.policy_summary_on", "resource,reserve,price"},
    {"ui.policy_summary_off", "resource"},
    {"ui.policy_form_keep", ""},
    {"ui.policy_form_mid", ""},
    {"ui.policy_form_end", ""},
    {"ui.policy_hint", ""},
    {"ui.price_title", "resource"},
    {"ui.chart_hint", ""},
    {"ui.cover_on", ""},
    {"ui.cover_off", ""},
    {"ui.storage_warning", "amount"},
    {"ui.borrow_capacity", "amount"},
    {"ui.loan_terms", "months,ratio,amount,count"},
    {"ui.each_mine_opex", "amount"},
    {"ui.loan_instalment", "amount"},
    {"ui.loan_months", "months"},
    {"ui.insolvency.one", "months,count"},
    {"ui.insolvency.many", "months,count"},
    {"ui.mines_summary.one", "count,operating"},
    {"ui.mines_summary.many", "count,operating"},
    {"ui.plots_summary.one", "count,listed"},
    {"ui.plots_summary.many", "count,listed"},
    {"ui.rival.one", "company,count"},
    {"ui.rival.many", "company,count"},
    {"ui.rival_bankrupt", "company"},
    {"ui.event_active", "event,day"},
    {"ui.research_active", "tech,day"},
    {"ui.research_idle", "known,total"},
    {"ui.tech_known", ""},
    {"ui.tech_in_progress", "day"},
    {"ui.tech_needs", "tech"},
    {"ui.tech_available", ""},
    {"ui.tech_available_busy", ""},
    {"ui.game_over", "company"},
    // Spec 004.
    {"notice.plot_withdrawn", "plot,amount"},
    {"notice.rival_unlocked_resource", "company,resource,tech"},
    {"notice.auto_sale_month", "list"},
    {"notice.auto_sale_item", "tonnes,resource,amount"},
    {"ui.pause_on.research_complete", ""},
    {"ui.need_lab_idle_title", ""},
    {"ui.need_lab_idle_sub.one", "count"},
    {"ui.need_lab_idle_sub.many", "count"},
    {"ui.need_survey_in_title", "plot"},
    {"ui.need_survey_in_sub", "day"},
    {"ui.survey_late", "report,day"},
    {"ui.survey_depleted", ""},
    {"ui.unlock_in_days.one", "days"},
    {"ui.unlock_in_days.many", "days"},
    {"ui.unlock_ready", "tech"},
    {"ui.notice_locked_soon.one", "resource,days"},
    {"ui.notice_locked_soon.many", "resource,days"},
    {"ui.notice_locked_ready", "resource,tech"},
    {"ui.unlisted_plots.one", "count"},
    {"ui.unlisted_plots.many", "count"},
    {"ui.no_unlisted", ""},
    {"ui.no_unlisted_withdrawn.one", "count"},
    {"ui.no_unlisted_withdrawn.many", "count"},
    {"ui.loan_credit", "amount"},
    {"ui.loan_blocked_credit", "amount,min"},
    {"ui.loan_blocked_count", "count"},
    {"ui.log_empty", ""},
    {"ui.sale_over", "percent"},
    {"ui.sale_at_ask", ""},
    {"ui.sale_buyer_you", ""},
    {"ui.no_sales", ""},
    {"ui.panel_sale", "day,paid,over"},
    // Spec 007.
    {"ui.pending_survey", ""},
    {"ui.pending_bid", "amount"},
    {"ui.pending_build", ""},
    {"ui.pending_research", ""},
    {"ui.pending_mothball", ""},
    {"ui.pending_reopen", ""},
    {"ui.pending_loan", "amount"},
    {"ui.pending_repayment", "amount"},
    {"ui.pending_sale", "tonnes,resource,amount"},
    {"ui.pending_policy_on", "resource"},
    {"ui.pending_policy_off", "resource"},
    {"ui.pending_cover_on", ""},
    {"ui.pending_cover_off", ""},
    {"ui.pending_deliver", ""},
    {"ui.pending_complete_stage", ""},
    {"ui.cancel", ""},
    {"ui.cancel_all", ""},
    {"ui.committed", "amount"},
    {"ui.pending_count.one", "count"},
    {"ui.pending_count.many", "count"},
    {"ui.pending_not_covered", ""},
    {"status.order_cancelled", ""},
    {"ui.survey_unaffordable", "amount"},
    // Spec 008.
    {"notice.mine_losing_money", "plot,amount,days"},
    {"ui.pause_on.mine_losing", ""},
    {"ui.mine_margin", "amount"},
    {"ui.mine_break_even", "amount"},
    {"ui.mine_no_figure", ""},
    {"ui.mine_margin_note", ""},
    {"ui.mine_loss", ""},
    {"ui.mine_if_reopened", ""},
    {"ui.mine_earns", ""},
    {"ui.mine_loses", ""},
    {"ui.mine_mothballed", ""},
    {"ui.mine_if_reopened_key", ""},
    // Spec 010.
    {"notice.program_delivered", "tonnes,resource"},
    {"notice.program_stage_complete", "company,stage,amount"},
    {"notice.program_founded", "company"},
    {"notice.deadline_passed", ""},
    {"notice.program_research_started", "company,stage"},
    {"notice.mine_mothballed", "plot"},
    {"notice.mine_reopened", "plot"},
    {"ui.pause_on.rival_program", ""},
    {"ui.race_deadline", "years,days"},
    {"ui.race_stage", "done,total"},
    {"ui.race_progress", "percent"},
    {"ui.race_ahead", ""},
    {"ui.race_behind", ""},
    {"ui.race_level", ""},
    {"ui.race_you", ""},
    {"ui.race_research_not_started", ""},
    {"ui.race_research_in_progress", ""},
    {"ui.race_research_done", ""},
    {"ui.race_program_founded_by", "company"},
    {"ui.program_title", ""},
    {"ui.program_intro", ""},
    {"ui.program_stage_heading", "number,name"},
    {"ui.program_stage_1_name", ""},
    {"ui.program_stage_2_name", ""},
    {"ui.program_stage_3_name", ""},
    {"ui.program_good", ""},
    {"ui.program_needs", ""},
    {"ui.program_delivered", ""},
    {"ui.program_in_stock", ""},
    {"ui.program_still_needed", ""},
    {"ui.program_cost", "amount"},
    {"ui.program_research_not_started", ""},
    {"ui.program_research_in_progress", "day"},
    {"ui.program_research_done", ""},
    {"ui.program_deliver", ""},
    {"ui.program_deliver_amount", "tonnes"},
    {"ui.program_start_research", "amount"},
    {"ui.program_complete_stage", "amount"},
    {"ui.program_stage_ready", ""},
    {"ui.program_blocked_research", ""},
    {"ui.program_blocked_deliveries", ""},
    {"ui.program_blocked_cash", "amount"},
    {"ui.program_blocked_locked", "resource,tech"},
    {"ui.program_nothing_to_deliver", ""},
    {"ui.program_cover_costs_warning", ""},
    {"ui.program_complete", ""},
    {"ui.end_founded_title", ""},
    {"ui.end_founded_body", ""},
    {"ui.end_partner_title", ""},
    {"ui.end_partner_body", "company"},
    {"ui.end_lost_title", ""},
    {"ui.end_lost_body", "company"},
    {"ui.end_deadline_title", ""},
    {"ui.end_deadline_body", ""},
    {"ui.end_bankrupt_title", ""},
    {"ui.end_bankrupt_body", "company"},
    {"ui.end_day", "day"},
    {"ui.end_stage_days", "number,day"},
    {"ui.end_stage_never", "number"},
    {"ui.end_your_figures", ""},
    {"ui.end_cash", ""},
    {"ui.end_mines", ""},
    {"ui.end_stages", ""},
    {"ui.end_new_game", ""},
});

// Names of the {placeholders} a template uses. Braces that do not form a
// placeholder are reported as a malformed template.
std::optional<std::vector<std::string>> templatePlaceholders(std::string_view text) {
    std::vector<std::string> names;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const char c = text[pos];
        if (c == '}') {
            return std::nullopt;  // a closing brace outside a placeholder
        }
        if (c != '{') {
            ++pos;
            continue;
        }
        const std::size_t close = text.find('}', pos + 1);
        if (close == std::string_view::npos || close == pos + 1) {
            return std::nullopt;
        }
        const std::string_view name = text.substr(pos + 1, close - pos - 1);
        for (const char n : name) {
            if (n == '{' || n == ' ' || ((n < 'a' || n > 'z') && n != '_')) {
                return std::nullopt;
            }
        }
        names.emplace_back(name);
        pos = close + 1;
    }
    return names;
}

bool allowsPlaceholder(std::string_view allowed, std::string_view name) {
    std::size_t start = 0;
    while (start <= allowed.size()) {
        const std::size_t comma = allowed.find(',', start);
        const std::string_view item = allowed.substr(
            start, comma == std::string_view::npos ? allowed.size() - start : comma - start);
        if (!item.empty() && item == name) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

using ExpectedKey = std::pair<std::string, std::string_view>;  // key, allowed placeholders

// Every key the catalogue must contain: the fixed table, the per-id event
// and tech keys, and one reject key per RejectReason. Each NoticeKind must
// have a base entry in the fixed table, so a new kind without prose fails.
std::vector<ExpectedKey> expectedStringKeys(const sim::Content& content, ErrorSink& sink) {
    std::vector<ExpectedKey> expected;
    expected.reserve(kStringKeys.size() + 2 * (content.events.size() + content.techs.size()) + 32);
    for (const StringKeySpec& spec : kStringKeys) {
        expected.emplace_back(std::string(spec.key), spec.placeholders);
    }
    for (const sim::EventDef& event : content.events) {
        expected.emplace_back("event." + event.id + ".name", "");
        expected.emplace_back("event." + event.id + ".description", "");
    }
    for (const sim::TechDef& tech : content.techs) {
        expected.emplace_back("tech." + tech.id + ".name", "");
        expected.emplace_back("tech." + tech.id + ".description", "");
    }
    // The enums have no count; their name tables answer "unknown" past the end.
    for (int i = 0; i < 256; ++i) {
        const std::string_view name = sim::noticeKindName(static_cast<sim::NoticeKind>(i));
        if (name == "unknown") {
            break;
        }
        const std::string key = "notice." + std::string(name);
        if (std::none_of(expected.begin(), expected.end(),
                         [&](const ExpectedKey& entry) { return entry.first == key; })) {
            sink.add(key, "no entry in the key table for this notice kind");
        }
    }
    for (int i = 0; i < 256; ++i) {
        const std::string_view name = sim::rejectReasonName(static_cast<sim::RejectReason>(i));
        if (name == "unknown") {
            break;
        }
        expected.emplace_back("reject." + std::string(name), "");
    }
    return expected;
}

// Validates one catalogue entry and stores it when it is usable.
void readStringEntry(const json& document, const ExpectedKey& expected, sim::Content& content,
                     ErrorSink& sink) {
    const auto& [key, placeholders] = expected;
    const auto it = document.find(key);
    if (it == document.end()) {
        sink.add(key, "missing");
        return;
    }
    if (!it->is_string()) {
        sink.add(key, "expected a string, got " + std::string(typeName(*it)));
        return;
    }
    const auto text = it->get<std::string>();
    if (text.empty()) {
        sink.add(key, "must not be empty");
        return;
    }
    const auto used = templatePlaceholders(text);
    if (!used) {
        sink.add(key, "malformed placeholder braces");
        return;
    }
    for (const std::string& name : *used) {
        if (!allowsPlaceholder(placeholders, name)) {
            sink.add(key, "placeholder {" + name + "} is not available for this key");
        }
    }
    content.strings[key] = text;
}

// Reads the flat key -> template object, checks it against the key table,
// and fills in event and tech names and descriptions. Runs after techs and
// events are parsed because their ids define the per-id keys.
void parseStrings(const json& document, sim::Content& content, ErrorSink& sink) {
    if (!document.is_object()) {
        sink.add("", "expected a top-level object, got " + std::string(typeName(document)));
        return;
    }
    const std::vector<ExpectedKey> expected = expectedStringKeys(content, sink);
    for (const ExpectedKey& entry : expected) {
        readStringEntry(document, entry, content, sink);
    }
    for (const auto& item : document.items()) {
        if (std::none_of(expected.begin(), expected.end(),
                         [&](const ExpectedKey& entry) { return entry.first == item.key(); })) {
            sink.add(item.key(), "unknown key");
        }
    }
    for (sim::EventDef& event : content.events) {
        event.name = content.strings["event." + event.id + ".name"];
        event.description = content.strings["event." + event.id + ".description"];
    }
    for (sim::TechDef& tech : content.techs) {
        tech.name = content.strings["tech." + tech.id + ".name"];
        tech.description = content.strings["tech." + tech.id + ".description"];
    }
}

std::optional<std::string> readFile(const std::filesystem::path& path) {
    const std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

}  // namespace

LoadResult loadFromStrings(const Sources& sources) {
    LoadResult result;
    sim::Content content;

    ErrorSink resourceSink(result.errors, kResourcesFile);
    ErrorSink techSink(result.errors, kTechsFile);
    ErrorSink regionSink(result.errors, kRegionsFile);
    ErrorSink eventSink(result.errors, kEventsFile);
    ErrorSink balanceSink(result.errors, kBalanceFile);
    ErrorSink aiSink(result.errors, kAiFile);
    ErrorSink programSink(result.errors, kProgramFile);
    ErrorSink stringsSink(result.errors, kStringsFile);

    std::vector<std::string> unlockedByNames;
    if (const auto document = parseDocument(sources.resources, resourceSink)) {
        unlockedByNames = parseResources(*document, content, resourceSink);
    }
    if (const auto document = parseDocument(sources.techs, techSink)) {
        parseTechs(*document, content, techSink);
        checkTechCycles(content, techSink);
    }
    resolveUnlocks(unlockedByNames, content, resourceSink, techSink);
    if (const auto document = parseDocument(sources.regions, regionSink)) {
        parseRegions(*document, content, regionSink);
    }
    if (const auto document = parseDocument(sources.events, eventSink)) {
        parseEvents(*document, content, eventSink);
    }
    if (const auto document = parseDocument(sources.balance, balanceSink)) {
        parseBalance(*document, content.balance, balanceSink);
    }
    if (const auto document = parseDocument(sources.ai, aiSink)) {
        parseAi(*document, content.ai, aiSink);
    }
    if (const auto document = parseDocument(sources.program, programSink)) {
        parseProgram(*document, content, content.program, programSink);
    }
    // Last: the per-id keys depend on the event and tech ids parsed above.
    if (const auto document = parseDocument(sources.strings, stringsSink)) {
        parseStrings(*document, content, stringsSink);
    }

    if (result.errors.empty()) {
        result.content = std::move(content);
    }
    return result;
}

LoadResult loadFromDirectory(const std::filesystem::path& directory) {
    Sources sources;
    std::vector<LoadError> errors;
    const auto read = [&](const char* file, std::string& target) {
        const std::optional<std::string> text = readFile(directory / file);
        if (!text) {
            errors.push_back({file, "", "cannot read " + (directory / file).string()});
            return;
        }
        target = *text;
    };
    read(kResourcesFile, sources.resources);
    read(kTechsFile, sources.techs);
    read(kRegionsFile, sources.regions);
    read(kEventsFile, sources.events);
    read(kBalanceFile, sources.balance);
    read(kAiFile, sources.ai);
    read(kProgramFile, sources.program);
    read(kStringsFile, sources.strings);
    if (!errors.empty()) {
        LoadResult result;
        result.errors = std::move(errors);
        return result;
    }
    return loadFromStrings(sources);
}

std::string formatErrors(const std::vector<LoadError>& errors) {
    std::string out;
    for (const LoadError& error : errors) {
        out += error.file;
        if (!error.path.empty()) {
            out += ": ";
            out += error.path;
        }
        out += ": ";
        out += error.message;
        out += '\n';
    }
    return out;
}

}  // namespace content
