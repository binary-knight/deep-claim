#include "sim/random.h"

#include "sim/fixed.h"
#include "sim/rng.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

TEST_CASE("uniformFixed stays in [0, 1) with a sane mean", "[random]") {
    sim::Pcg32 rng(11, 1);
    constexpr int kSamples = 20000;
    std::int64_t sum = 0;
    for (int i = 0; i < kSamples; ++i) {
        const sim::Fixed value = sim::uniformFixed(rng);
        REQUIRE(value >= sim::Fixed::zero());
        REQUIRE(value < sim::Fixed::one());
        sum += value.raw();
    }
    const std::int64_t mean = sum / kSamples;  // raw units, expect ~500000
    CHECK(mean > 480'000);
    CHECK(mean < 520'000);
}

TEST_CASE("uniformInt covers its inclusive range", "[random]") {
    sim::Pcg32 rng(12, 1);
    std::array<int, 11> counts{};
    for (int i = 0; i < 5000; ++i) {
        const std::int64_t value = sim::uniformInt(rng, -5, 5);
        REQUIRE(value >= -5);
        REQUIRE(value <= 5);
        ++counts[static_cast<std::size_t>(value + 5)];
    }
    for (const int count : counts) {
        CHECK(count > 300);
    }
    CHECK(sim::uniformInt(rng, 42, 42) == 42);
}

TEST_CASE("approxNormal has roughly unit mean and variance", "[random]") {
    sim::Pcg32 rng(13, 1);
    constexpr int kSamples = 20000;
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const sim::Fixed value = sim::approxNormal(rng);
        REQUIRE(value >= sim::Fixed::fromInt(-6));
        REQUIRE(value <= sim::Fixed::fromInt(6));
        const double x = static_cast<double>(value.raw()) / static_cast<double>(sim::Fixed::kScale);
        sum += x;
        sumSquares += x * x;
    }
    const double mean = sum / kSamples;
    const double variance = sumSquares / kSamples - mean * mean;
    CHECK(mean > -0.03);
    CHECK(mean < 0.03);
    CHECK(variance > 0.9);
    CHECK(variance < 1.1);
}

TEST_CASE("RngStreams are seeded deterministically and independently", "[random]") {
    sim::RngStreams a(100);
    sim::RngStreams b(100);
    sim::RngStreams c(101);
    CHECK(a.world.next() == b.world.next());
    CHECK(a.market.next() == b.market.next());
    CHECK(a.survey.next() != c.survey.next());

    sim::RngStreams d(100);
    const std::uint32_t world = d.world.next();
    CHECK(world != d.market.next());
    CHECK(world != d.survey.next());
    CHECK(world != d.event.next());
    CHECK(world != d.ai.next());
}
