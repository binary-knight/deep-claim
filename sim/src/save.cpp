#include "sim/save.h"

#include "sim/archive.h"
#include "sim/serialize.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sim {

namespace {
constexpr std::array<std::uint8_t, 4> kMagic = {'D', 'C', 'S', 'V'};
}  // namespace

std::uint64_t hashWorld(const WorldState& state) {
    return hashOf(state);
}

std::uint64_t hashContent(const Content& content) {
    return hashOf(content);
}

bool validateWorld(const Content& content, const WorldState& state) {
    if (state.plots.size() != state.hidden.plotDeposits.size()) {
        return false;
    }
    for (const Plot& plot : state.plots) {
        if (!plot.region.valid() || plot.region.index() >= content.regions.size()) {
            return false;
        }
        if (plot.areaHectares < 1 || plot.askPrice < Money::zero()) {
            return false;
        }
        if (plot.listing != PlotListing::Unlisted && plot.listing != PlotListing::Listed &&
            plot.listing != PlotListing::Sold) {
            return false;
        }
        // Companies arrive in step 5; until then no plot may have an owner.
        if ((plot.listing == PlotListing::Sold) != plot.owner.valid()) {
            return false;
        }
    }
    for (const PlotDeposits& deposits : state.hidden.plotDeposits) {
        for (const DepositTruth& truth : deposits.deposits) {
            if (!truth.resource.valid() || truth.resource.index() >= content.resources.size()) {
                return false;
            }
            if (truth.tonnesTotal < 0 || truth.tonnesRemaining < 0 ||
                truth.tonnesRemaining > truth.tonnesTotal) {
                return false;
            }
        }
    }
    return true;
}

std::vector<std::uint8_t> saveWorld(const WorldState& state, std::uint64_t contentHash) {
    WriteArchive archive;
    for (const std::uint8_t byte : kMagic) {
        archive(byte);
    }
    std::uint32_t version = kSaveVersion;
    archive(version);
    archive(contentHash);
    archive(const_cast<WorldState&>(state));  // NOLINT(cppcoreguidelines-pro-type-const-cast)
    return archive.release();
}

bool loadWorld(std::span<const std::uint8_t> bytes, const Content& content, WorldState& out) {
    const std::uint64_t contentHash = hashContent(content);
    ReadArchive archive(bytes);
    for (const std::uint8_t expected : kMagic) {
        std::uint8_t byte = 0;
        archive(byte);
        if (byte != expected) {
            return false;
        }
    }
    std::uint32_t version = 0;
    archive(version);
    if (archive.failed() || version != kSaveVersion) {
        return false;
    }
    std::uint64_t savedContentHash = 0;
    archive(savedContentHash);
    if (archive.failed() || savedContentHash != contentHash) {
        return false;
    }
    archive(out);
    return !archive.failed() && archive.atEnd() && validateWorld(content, out);
}

}  // namespace sim
