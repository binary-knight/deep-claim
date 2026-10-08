// Headless runner. Plays the simulation for a number of days and reports
// statistics as JSON. Balance reporting arrives in step 14.
#include "sim/simulation.h"
#include "sim/types.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <span>
#include <string_view>

namespace {

struct Options {
    sim::Seed seed = 1;
    sim::Day days = sim::kDaysPerYear * 20;
};

bool parseArgs(std::span<char*> args, Options& out) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if ((arg == "--seed" || arg == "--days") && i + 1 < args.size()) {
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
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(std::span<char*>(argv, static_cast<std::size_t>(argc)), options)) {
        std::cerr << "usage: sim_runner [--seed N] [--days N]\n";
        return 2;
    }

    sim::Simulation simulation(options.seed);
    simulation.tick(options.days);

    std::cout << std::format("{{\"seed\":{},\"days\":{},\"state_hash\":\"{:016x}\"}}\n",
                             options.seed, simulation.state().day, simulation.stateHash());
    return 0;
}
