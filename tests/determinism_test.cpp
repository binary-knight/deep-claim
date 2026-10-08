#include "sim/simulation.h"
#include "sim/types.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

TEST_CASE("Same seed produces identical state", "[determinism]") {
    sim::Simulation a(1234);
    sim::Simulation b(1234);
    a.tick(sim::kDaysPerYear * 10);
    b.tick(sim::kDaysPerYear * 10);
    CHECK(a.state().day == b.state().day);
    CHECK(a.stateHash() == b.stateHash());
}

TEST_CASE("Different seeds produce different state", "[determinism]") {
    sim::Simulation a(1);
    sim::Simulation b(2);
    a.tick(sim::kDaysPerYear);
    b.tick(sim::kDaysPerYear);
    CHECK(a.stateHash() != b.stateHash());
}

TEST_CASE("Ticking changes the state hash", "[determinism]") {
    sim::Simulation simulation(1);
    const std::uint64_t before = simulation.stateHash();
    simulation.tick();
    CHECK(simulation.stateHash() != before);
    CHECK(simulation.state().day == 1);
}

TEST_CASE("Golden state hash is stable across platforms", "[determinism]") {
    // This value was recorded on Linux/GCC and is verified on Windows/MSVC in
    // CI. If a deliberate simulation change alters it, update the constant in
    // the same commit and say so in the commit message.
    constexpr std::uint64_t kGolden = 0xe1c8774e2cf22341ULL;
    sim::Simulation simulation(42);
    simulation.tick(sim::kDaysPerYear);
    CHECK(simulation.stateHash() == kGolden);
}
