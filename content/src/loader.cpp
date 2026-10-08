#include "content/loader.h"

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

constexpr std::int64_t kMaxDays = 36000;  // 100 in-game years

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

const sim::Fixed kMultiplierMin = sim::Fixed::fromRaw(1);
const sim::Fixed kMultiplierMax = sim::Fixed::fromInt(1000);

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
                        "storage_cost_per_tonne_month", "unlocked_by"});
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
            node.fixed("elasticity", true, sim::Fixed::fromRaw(1), sim::Fixed::fromInt(100)),
            sim::Fixed::zero());
        def.volatility =
            valueOr(node.fixed("volatility", true, sim::Fixed::zero(), sim::Fixed::one()),
                    sim::Fixed::zero());
        def.storageCostPerTonneMonth =
            valueOr(node.money("storage_cost_per_tonne_month", true, sim::Money::zero()),
                    sim::Money::zero());
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
        node.allowKeys({"id", "name", "cost", "duration_days", "requires", "effects"});
        sim::TechDef def;
        def.id = readId(node, seen);
        def.name = valueOr(node.string("name", true), std::string());
        def.cost = valueOr(node.money("cost", true, sim::Money::zero()), sim::Money::zero());
        def.durationDays =
            static_cast<sim::Day>(valueOr(node.integer("duration_days", true, 1, kMaxDays), 1LL));
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
        node.allowKeys({"presence_prob", "tonnes_median", "tonnes_spread"});
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
        node.allowKeys({"id", "name", "daily_weight", "min_year", "duration_days", "effects"});
        sim::EventDef def;
        def.id = readId(node, seen);
        def.name = valueOr(node.string("name", true), std::string());
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

void parseBalance(const json& document, sim::BalanceParams& balance, ErrorSink& sink) {
    if (!document.is_object()) {
        sink.add("", "expected a top-level object, got " + std::string(typeName(document)));
        return;
    }
    const Node node(document, "", sink);
    node.allowKeys({"starting_capital", "loan_annual_rate", "loan_term_months",
                    "loan_max_principal", "survey_cost", "survey_duration_days",
                    "survey_base_noise", "mine_capex_per_hectare", "mine_build_days",
                    "mine_opex_per_day", "auction_window_days", "plots_listed_at_start",
                    "plots_listed_per_month", "bankruptcy_grace_months"});
    const sim::Money zero = sim::Money::zero();
    balance.startingCapital =
        valueOr(node.money("starting_capital", true, sim::Money::fromCents(1)), zero);
    balance.loanAnnualRate =
        valueOr(node.fixed("loan_annual_rate", true, sim::Fixed::zero(), sim::Fixed::one()),
                sim::Fixed::zero());
    balance.loanTermMonths =
        static_cast<std::uint32_t>(valueOr(node.integer("loan_term_months", true, 1, 1200), 1LL));
    balance.loanMaxPrincipal = valueOr(node.money("loan_max_principal", true, zero), zero);
    balance.surveyCost = valueOr(node.money("survey_cost", true, zero), zero);
    balance.surveyDurationDays = static_cast<sim::Day>(
        valueOr(node.integer("survey_duration_days", true, 1, kMaxDays), 1LL));
    balance.surveyBaseNoise =
        valueOr(node.fixed("survey_base_noise", true, sim::Fixed::zero(), sim::Fixed::fromInt(10)),
                sim::Fixed::zero());
    balance.mineCapexPerHectare = valueOr(node.money("mine_capex_per_hectare", true, zero), zero);
    balance.mineBuildDays =
        static_cast<sim::Day>(valueOr(node.integer("mine_build_days", true, 1, kMaxDays), 1LL));
    balance.mineOpexPerDay = valueOr(node.money("mine_opex_per_day", true, zero), zero);
    balance.auctionWindowDays = static_cast<sim::Day>(
        valueOr(node.integer("auction_window_days", true, 1, sim::kDaysPerYear), 1LL));
    balance.plotsListedAtStart = static_cast<std::uint32_t>(
        valueOr(node.integer("plots_listed_at_start", true, 0, 1000), 0LL));
    balance.plotsListedPerMonth = static_cast<std::uint32_t>(
        valueOr(node.integer("plots_listed_per_month", true, 0, 1000), 0LL));
    balance.bankruptcyGraceMonths = static_cast<std::uint32_t>(
        valueOr(node.integer("bankruptcy_grace_months", true, 0, 120), 0LL));
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
