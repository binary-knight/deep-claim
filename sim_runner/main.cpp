// Headless runner. Loads content, plays the simulation for a number of days
// and reports statistics as JSON. Balance reporting arrives in step 14.
#include "sim/simulation.h"
#include "sim/types.h"

#include "content/loader.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

struct Options {
    sim::Seed seed = 1;
    sim::Day days = sim::kDaysPerYear * 20;
    std::string dataDir = DC_DATA_DIR;
};

bool parseArgs(std::span<char*> args, Options& out) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (i + 1 >= args.size()) {
            return false;
        }
        if (arg == "--data") {
            out.dataDir = args[++i];
            continue;
        }
        if (arg == "--seed" || arg == "--days") {
            char* end = nullptr;
            const unsigned long long value = std::strtoull(args[++i], &end, 10);
            if (end == nullptr || *end != '\0') {
                return false;
            }
            if (arg == "--seed") {
                out.seed = value;
            } else {
                out.days = static_cast<sim::Day>(value);
            }
            continue;
        }
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(std::span<char*>(argv, static_cast<std::size_t>(argc)), options)) {
        std::cerr << "usage: sim_runner [--seed N] [--days N] [--data DIR]\n";
        return 2;
    }

    const content::LoadResult loaded = content::loadFromDirectory(options.dataDir);
    if (!loaded.content.has_value()) {
        std::cerr << "content errors:\n" << content::formatErrors(loaded.errors);
        return 1;
    }
    const sim::Content& gameContent = *loaded.content;

    sim::Simulation simulation(options.seed);
    simulation.tick(options.days);

    std::cout << std::format(
        "{{\"seed\":{},\"days\":{},\"resources\":{},\"state_hash\":\"{:016x}\"}}\n", options.seed,
        simulation.state().day, gameContent.resources.size(), simulation.stateHash());
    return 0;
}
