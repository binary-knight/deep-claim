#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

namespace sim {

// Typed index into the owning vector in WorldState. Ids are never reused and
// entities are never erased from their vectors (dead entities get a flag), so
// an id stays valid for the lifetime of a game.
template <class Tag>
class StrongId {
public:
    using Value = std::uint32_t;
    static constexpr Value kInvalid = 0xFFFFFFFFU;

    constexpr StrongId() = default;
    constexpr explicit StrongId(Value value) : value_(value) {}

    [[nodiscard]] constexpr Value value() const { return value_; }
    [[nodiscard]] constexpr bool valid() const { return value_ != kInvalid; }
    [[nodiscard]] constexpr std::size_t index() const { return static_cast<std::size_t>(value_); }

    friend constexpr auto operator<=>(StrongId, StrongId) = default;

private:
    Value value_ = kInvalid;
};

struct CompanyTag {};
struct RegionTag {};
struct PlotTag {};
struct MineTag {};
struct ResourceTag {};
struct TechTag {};
struct LoanTag {};
struct SurveyTag {};
struct EventTag {};
struct AuctionTag {};

using CompanyId = StrongId<CompanyTag>;
using RegionId = StrongId<RegionTag>;
using PlotId = StrongId<PlotTag>;
using MineId = StrongId<MineTag>;
using ResourceId = StrongId<ResourceTag>;
using TechId = StrongId<TechTag>;
using LoanId = StrongId<LoanTag>;
using SurveyId = StrongId<SurveyTag>;
using EventId = StrongId<EventTag>;
using AuctionId = StrongId<AuctionTag>;

}  // namespace sim
