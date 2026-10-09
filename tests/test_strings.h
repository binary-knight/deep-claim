#pragma once

// Prose catalogue helpers for tests. The loader requires a complete
// catalogue, including a name and description for every event and tech id,
// so fixtures with their own ids rebuild the per-id keys from the shipped
// file: every other key is taken verbatim from data/strings.json.

#include "sim/content.h"

#include "content/loader.h"

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace test {

inline std::string readTextFile(const std::string& path) {
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// The shipped catalogue with its event.* and tech.* keys replaced by keys
// for the given ids. Relies on the file having one key per line, which
// tools/prose/to_json.py guarantees.
inline std::string fixtureStrings(std::initializer_list<std::string_view> eventIds,
                                  std::initializer_list<std::string_view> techIds) {
    const std::string shipped = readTextFile(std::string(DC_DATA_DIR) + "/strings.json");
    std::string out = "{\n";
    for (const std::string_view id : eventIds) {
        out += "  \"event." + std::string(id) + ".name\": \"Event " + std::string(id) + "\",\n";
        out += "  \"event." + std::string(id) + ".description\": \"About " + std::string(id) +
               ".\",\n";
    }
    for (const std::string_view id : techIds) {
        out += "  \"tech." + std::string(id) + ".name\": \"Tech " + std::string(id) + "\",\n";
        out +=
            "  \"tech." + std::string(id) + ".description\": \"About " + std::string(id) + ".\",\n";
    }
    std::istringstream lines(shipped);
    std::string line;
    while (std::getline(lines, line)) {
        if (line == "{" || line == "}" || line.starts_with("  \"event.") ||
            line.starts_with("  \"tech.")) {
            continue;
        }
        out += line + "\n";
    }
    out += "}\n";
    return out;
}

// Gives in-code fixture content the shipped prose, so text produced from it
// reads as it does in the game. Event and tech names stay as the fixture
// set them.
inline void addStrings(sim::Content& content) {
    const content::LoadResult shipped = content::loadFromDirectory(DC_DATA_DIR);
    if (!shipped.content) {
        throw std::runtime_error("shipped data does not load: " +
                                 content::formatErrors(shipped.errors));
    }
    content.strings = shipped.content->strings;
}

}  // namespace test
