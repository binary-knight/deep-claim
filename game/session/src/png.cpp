#include "game/png.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace game {

namespace {

constexpr std::uint32_t kMaxDimension = 1U << 15U;
constexpr std::size_t kMaxStoredBlock = 65535;

std::array<std::uint32_t, 256> makeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < table.size(); ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) != 0 ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
        }
        table.at(n) = c;
    }
    return table;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    static const std::array<std::uint32_t, 256> table = makeCrcTable();
    std::uint32_t c = 0xFFFFFFFFU;
    for (const std::uint8_t b : bytes) {
        c = table.at((c ^ b) & 0xFFU) ^ (c >> 8U);
    }
    return c ^ 0xFFFFFFFFU;
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void putChunk(std::vector<std::uint8_t>& out, std::string_view type,
              std::span<const std::uint8_t> data) {
    putU32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type.begin(), type.end());
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    putU32(out, crc32(body));
}

// zlib stream of stored deflate blocks around `raw`.
std::vector<std::uint8_t> zlibStored(std::span<const std::uint8_t> raw) {
    std::vector<std::uint8_t> z;
    z.reserve(raw.size() + raw.size() / kMaxStoredBlock * 5 + 16);
    z.push_back(0x78);  // CM=8, CINFO=7
    z.push_back(0x01);  // no preset dictionary, fastest compression, FCHECK ok
    std::size_t offset = 0;
    do {
        const std::size_t len = std::min(kMaxStoredBlock, raw.size() - offset);
        const bool last = offset + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(len & 0xFFU));
        z.push_back(static_cast<std::uint8_t>(len >> 8U));
        z.push_back(static_cast<std::uint8_t>(~len & 0xFFU));
        z.push_back(static_cast<std::uint8_t>((~len >> 8U) & 0xFFU));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                 raw.begin() + static_cast<std::ptrdiff_t>(offset + len));
        offset += len;
    } while (offset < raw.size());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    putU32(z, (b << 16U) | a);
    return z;
}

}  // namespace

std::vector<std::uint8_t> encodePngRgb(std::uint32_t width, std::uint32_t height,
                                       std::span<const std::uint8_t> rgb) {
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension ||
        rgb.size() < static_cast<std::size_t>(width) * height * 3) {
        return {};
    }
    const std::size_t stride = static_cast<std::size_t>(width) * 3;
    std::vector<std::uint8_t> raw;
    raw.reserve((stride + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);  // filter type: none
        const auto row = rgb.subspan(static_cast<std::size_t>(y) * stride, stride);
        raw.insert(raw.end(), row.begin(), row.end());
    }

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    putU32(ihdr, width);
    putU32(ihdr, height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // colour type: RGB
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    putChunk(out, "IHDR", ihdr);
    putChunk(out, "IDAT", zlibStored(raw));
    putChunk(out, "IEND", {});
    return out;
}

}  // namespace game
