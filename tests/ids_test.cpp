#include "sim/ids.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

TEST_CASE("StrongId defaults to invalid", "[ids]") {
    const sim::PlotId id;
    CHECK_FALSE(id.valid());
    CHECK(id.value() == sim::PlotId::kInvalid);
}

TEST_CASE("StrongId wraps an index", "[ids]") {
    const sim::PlotId id(7);
    CHECK(id.valid());
    CHECK(id.value() == 7U);
    CHECK(id.index() == 7U);
}

TEST_CASE("StrongId compares by value", "[ids]") {
    CHECK(sim::CompanyId(1) == sim::CompanyId(1));
    CHECK(sim::CompanyId(1) != sim::CompanyId(2));
    CHECK(sim::CompanyId(1) < sim::CompanyId(2));
    CHECK(sim::CompanyId(5) < sim::CompanyId());  // invalid sorts last
}

TEST_CASE("StrongIds of different tags are distinct types", "[ids]") {
    STATIC_CHECK_FALSE(std::is_same_v<sim::PlotId, sim::MineId>);
    STATIC_CHECK_FALSE(std::is_convertible_v<sim::PlotId, sim::MineId>);
    STATIC_CHECK_FALSE(std::is_convertible_v<std::uint32_t, sim::PlotId>);
}
