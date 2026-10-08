#include "sim/archive.h"

#include "sim/fixed.h"
#include "sim/hash.h"
#include "sim/ids.h"
#include "sim/money.h"
#include "sim/save.h"
#include "sim/serialize.h"
#include "sim/simulation.h"

#include "test_content.h"
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

enum class Kind : std::uint8_t { Alpha = 0, Beta = 1 };

struct Sample {
    bool flag = false;
    std::uint8_t small = 0;
    std::int32_t negative = 0;
    std::int64_t big = 0;
    std::string name;
    std::vector<std::uint32_t> list;
    std::optional<std::int64_t> maybe;
    sim::Fixed ratio;
    sim::Money cash;
    sim::PlotId plot;
    std::vector<sim::Money> ledger;
    Kind kind = Kind::Alpha;
};

template <class Archive>
void serialize(Archive& ar, Sample& s) {
    ar(s.flag, s.small, s.negative, s.big, s.name, s.list, s.maybe, s.ratio, s.cash, s.plot,
       s.ledger, s.kind);
}

Sample makeSample() {
    Sample s;
    s.flag = true;
    s.small = 200;
    s.negative = -123456;
    s.big = -9'000'000'000'000LL;
    s.name = "Northern Shield";
    s.list = {1, 2, 3, 0xFFFFFFFFU};
    s.maybe = 77;
    s.ratio = sim::Fixed::fromRatio(-1, 3);
    s.cash = sim::Money::fromCents(-12345);
    s.plot = sim::PlotId(9);
    s.ledger = {sim::Money::fromUnits(1), sim::Money::fromUnits(-2)};
    s.kind = Kind::Beta;
    return s;
}

}  // namespace

TEST_CASE("Archive round-trips a composite value", "[archive]") {
    const Sample original = makeSample();
    const std::vector<std::uint8_t> bytes = sim::toBytes(original);

    Sample loaded;
    REQUIRE(sim::fromBytes(bytes, loaded));
    CHECK(loaded.flag == original.flag);
    CHECK(loaded.small == original.small);
    CHECK(loaded.negative == original.negative);
    CHECK(loaded.big == original.big);
    CHECK(loaded.name == original.name);
    CHECK(loaded.list == original.list);
    CHECK(loaded.maybe == original.maybe);
    CHECK(loaded.ratio == original.ratio);
    CHECK(loaded.cash == original.cash);
    CHECK(loaded.plot == original.plot);
    CHECK(loaded.ledger == original.ledger);
    CHECK(loaded.kind == original.kind);
    CHECK(sim::hashOf(loaded) == sim::hashOf(original));
}

TEST_CASE("Hash equals FNV-1a over the serialized bytes", "[archive]") {
    const Sample sample = makeSample();
    sim::Fnv1a64 hasher;
    for (const std::uint8_t byte : sim::toBytes(sample)) {
        hasher.addByte(byte);
    }
    CHECK(hasher.value() == sim::hashOf(sample));
}

TEST_CASE("Byte layout is little-endian with uint32 length prefixes", "[archive]") {
    const std::uint32_t word = 0x01020304U;
    CHECK(sim::toBytes(word) == std::vector<std::uint8_t>{4, 3, 2, 1});

    const bool flag = true;
    CHECK(sim::toBytes(flag) == std::vector<std::uint8_t>{1});

    const std::string text = "hi";
    CHECK(sim::toBytes(text) == std::vector<std::uint8_t>{2, 0, 0, 0, 'h', 'i'});

    const std::optional<std::uint8_t> empty;
    CHECK(sim::toBytes(empty) == std::vector<std::uint8_t>{0});

    const std::int16_t negative = -2;
    CHECK(sim::toBytes(negative) == std::vector<std::uint8_t>{0xFE, 0xFF});
}

TEST_CASE("Truncated and oversized streams fail cleanly", "[archive]") {
    std::vector<std::uint8_t> bytes = sim::toBytes(makeSample());
    Sample out;

    std::vector<std::uint8_t> truncated(bytes.begin(), bytes.end() - 1);
    CHECK_FALSE(sim::fromBytes(truncated, out));

    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0);
    CHECK_FALSE(sim::fromBytes(trailing, out));

    CHECK_FALSE(sim::fromBytes(std::vector<std::uint8_t>{}, out));
}

TEST_CASE("Aggregate vectors are bounded by their real serialized size", "[archive]") {
    // 25 bytes per default Plot; a prefix of 2 with only 25 bytes left must fail
    // before any decoding, not after allocating two elements.
    std::vector<std::uint8_t> bytes{2, 0, 0, 0};
    bytes.resize(4 + 25, 0);
    std::vector<sim::Plot> plots;
    CHECK_FALSE(sim::fromBytes(bytes, plots));
    CHECK(plots.empty());
}

TEST_CASE("Huge length prefixes are rejected before allocating", "[archive]") {
    // The byte comes through a volatile so GCC cannot constant-fold the whole
    // parse; with literal bytes GCC 13 -O2 emits a spurious -Warray-bounds on
    // the allocation path that the length check makes unreachable.
    volatile std::uint8_t opaque = 0xFF;
    const std::uint8_t fill = opaque;
    const std::vector<std::uint8_t> bytes(4, fill);
    std::vector<std::uint32_t> list;
    CHECK_FALSE(sim::fromBytes(bytes, list));
    CHECK(list.empty());

    std::string text;
    CHECK_FALSE(sim::fromBytes(bytes, text));
    CHECK(text.empty());
}

TEST_CASE("Simulation save and load reproduce the state exactly", "[archive][save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation original(content, 9);
    original.tick(500);
    const std::vector<std::uint8_t> bytes = original.save();

    sim::Simulation restored(content, 1);
    REQUIRE(restored.load(bytes));
    CHECK(restored.state().day == 500);
    CHECK(restored.stateHash() == original.stateHash());

    original.tick(100);
    restored.tick(100);
    CHECK(restored.stateHash() == original.stateHash());
}

TEST_CASE("Invalid bool bytes and even RNG increments are rejected", "[archive][save]") {
    bool flag = false;
    const std::vector<std::uint8_t> two{2};
    CHECK_FALSE(sim::fromBytes(two, flag));

    sim::Pcg32 rng(1, 1);
    std::vector<std::uint8_t> bytes = sim::toBytes(rng);
    // Layout: 8 bytes state, 8 bytes increment (little-endian); clear bit 0.
    bytes[8] = static_cast<std::uint8_t>(bytes[8] & 0xFEU);
    sim::Pcg32 restored;
    CHECK_FALSE(sim::fromBytes(bytes, restored));
}

TEST_CASE("Saves with a bad header are rejected and leave the state alone", "[save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation original(content, 9);
    original.tick(10);
    const std::vector<std::uint8_t> good = original.save();

    sim::Simulation target(content, 3);
    const std::uint64_t before = target.stateHash();

    std::vector<std::uint8_t> badMagic = good;
    badMagic[0] = 'X';
    CHECK_FALSE(target.load(badMagic));

    std::vector<std::uint8_t> badVersion = good;
    badVersion[4] = static_cast<std::uint8_t>(sim::kSaveVersion + 1);
    CHECK_FALSE(target.load(badVersion));

    std::vector<std::uint8_t> truncated(good.begin(), good.end() - 3);
    CHECK_FALSE(target.load(truncated));

    CHECK(target.stateHash() == before);
}

TEST_CASE("Saves holding an inconsistent world are rejected", "[save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation target(content, 1);
    const std::uint64_t before = target.stateHash();

    sim::WorldState broken = target.state();
    broken.hidden.plotDeposits.pop_back();  // sizes disagree
    CHECK_FALSE(target.load(sim::saveWorld(broken, sim::hashContent(content))));

    broken = target.state();
    broken.plots[0].region = sim::RegionId(99);
    CHECK_FALSE(target.load(sim::saveWorld(broken, sim::hashContent(content))));

    broken = target.state();
    broken.plots[0].owner = sim::CompanyId(0);  // owner without being sold
    CHECK_FALSE(target.load(sim::saveWorld(broken, sim::hashContent(content))));

    broken = target.state();
    bool tampered = false;
    for (sim::PlotDeposits& deposits : broken.hidden.plotDeposits) {
        if (!deposits.deposits.empty()) {
            deposits.deposits[0].tonnesRemaining = deposits.deposits[0].tonnesTotal + 1;
            tampered = true;
            break;
        }
    }
    REQUIRE(tampered);
    CHECK_FALSE(target.load(sim::saveWorld(broken, sim::hashContent(content))));

    CHECK(target.stateHash() == before);
    CHECK(target.load(sim::saveWorld(target.state(), sim::hashContent(content))));
}

TEST_CASE("Saves are bound to the content they were made with", "[save]") {
    const sim::Content content = test::makeContent();
    sim::Simulation original(content, 9);
    const std::vector<std::uint8_t> bytes = original.save();

    sim::Content edited = test::makeContent();
    edited.resources[0].basePrice = sim::Money::fromUnits(61);
    sim::Simulation other(edited, 9);
    CHECK_FALSE(other.load(bytes));

    sim::Simulation same(content, 1);
    CHECK(same.load(bytes));
}
