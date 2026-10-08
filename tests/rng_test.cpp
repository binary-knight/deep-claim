#include "sim/rng.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

TEST_CASE("Pcg32 matches the reference implementation", "[rng]") {
    // Expected values come from the pcg32-demo program in the PCG reference
    // C library, seeded with pcg32_srandom_r(&rng, 42u, 54u).
    sim::Pcg32 rng(42, 54);
    const std::uint32_t expected[] = {0xa15c02b7U, 0x7b47f409U, 0xba1d3330U,
                                      0x83d2f293U, 0xbfa4784bU, 0xcbed606eU};
    for (const std::uint32_t value : expected) {
        CHECK(rng.next() == value);
    }
}

TEST_CASE("Pcg32::below stays within bounds", "[rng]") {
    sim::Pcg32 rng(7, 1);
    for (int i = 0; i < 10000; ++i) {
        CHECK(rng.below(13) < 13U);
    }
    for (int i = 0; i < 100; ++i) {
        CHECK(rng.below(1) == 0U);
    }
}

TEST_CASE("Different streams from the same seed diverge", "[rng]") {
    sim::Pcg32 a(99, 1);
    sim::Pcg32 b(99, 2);
    CHECK(a.next() != b.next());
}

TEST_CASE("Pcg32 state can be restored", "[rng]") {
    sim::Pcg32 original(5, 3);
    original.next();
    sim::Pcg32 copy;
    copy.restore(original.state(), original.increment());
    CHECK(copy.next() == original.next());
}
