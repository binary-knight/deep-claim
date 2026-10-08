// Headless runner. Loads content, plays the simulation for a number of days
// and reports statistics as JSON. Balance reporting arrives in step 14.
#include "sim/simulation.h"
#include "sim/types.h"

#include "content/loader.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
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
            const std::string_view text = args[++i];
            const bool digitsOnly =
                !text.empty() &&
                std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
            if (!digitsOnly) {
                return false;
            }
            errno = 0;
            const unsigned long long value = std::strtoull(text.data(), nullptr, 10);
            if (errno == ERANGE) {
                return false;
            }
            if (arg == "--seed") {
                out.seed = value;
            } else {
                if (value > std::numeric_limits<sim::Day>::max()) {
                    return false;
                }
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

    sim::GameSetup setup;
    setup.aiNames = {"Rival Mining Co"};
    sim::Simulation simulation(gameContent, options.seed, setup);
    simulation.tick(options.days);

    std::cout << std::format(
        "{{\"seed\":{},\"days\":{},\"plots\":{},\"state_hash\":\"{:016x}\"}}\n", options.seed,
        simulation.state().day, simulation.state().plots.size(), simulation.stateHash());
    return 0;
}
