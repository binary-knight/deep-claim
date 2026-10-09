#pragma once

#include "sim/content.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace content {

inline constexpr const char* kResourcesFile = "resources.json";
inline constexpr const char* kTechsFile = "techs.json";
inline constexpr const char* kRegionsFile = "regions.json";
inline constexpr const char* kEventsFile = "events.json";
inline constexpr const char* kBalanceFile = "balance.json";
inline constexpr const char* kAiFile = "ai.json";
inline constexpr const char* kStringsFile = "strings.json";

struct LoadError {
    std::string file;     // e.g. "resources.json"
    std::string path;     // e.g. "[2].base_price"
    std::string message;  // e.g. "expected a number"
};

// JSON text of each data file.
struct Sources {
    std::string resources;
    std::string techs;
    std::string regions;
    std::string events;
    std::string balance;
    std::string ai;       // the built-in competitor's tuning (spec 006)
    std::string strings;  // the prose catalogue (spec 002)
};

struct LoadResult {
    std::optional<sim::Content> content;  // present only when errors is empty
    std::vector<LoadError> errors;

    [[nodiscard]] bool ok() const { return content.has_value(); }
};

// Parses and validates all seven documents. Every problem found is reported,
// not just the first. Decimal values are parsed as doubles, rejected if not
// finite or if they carry more precision than Fixed or Money can hold, then
// rounded to nearest.
[[nodiscard]] LoadResult loadFromStrings(const Sources& sources);

// Reads the seven files from `directory` and calls loadFromStrings.
[[nodiscard]] LoadResult loadFromDirectory(const std::filesystem::path& directory);

// One line per error, for logs and CLI output.
[[nodiscard]] std::string formatErrors(const std::vector<LoadError>& errors);

}  // namespace content
