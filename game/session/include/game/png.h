#pragma once

// Minimal PNG encoder for screenshots: 8-bit RGB, uncompressed (deflate
// "stored" blocks), so there is no image or compression library to depend
// on. A 1366x820 frame is about 3.4 MB.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace game {

// `rgb` holds width*height*3 bytes, rows top to bottom. Returns the file
// bytes, or an empty vector when the dimensions are invalid or the buffer is
// too small.
[[nodiscard]] std::vector<std::uint8_t> encodePngRgb(std::uint32_t width, std::uint32_t height,
                                                     std::span<const std::uint8_t> rgb);

}  // namespace game
