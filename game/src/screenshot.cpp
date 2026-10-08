#include "screenshot.h"

#include "game/png.h"

#include <RmlUi_Include_GL3.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace game {

bool captureFramebufferPng(int width, int height, const std::filesystem::path& file) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    const std::size_t stride = static_cast<std::size_t>(w) * 3;
    std::vector<std::uint8_t> pixels(stride * h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) {
        return false;
    }
    // OpenGL rows run bottom to top; PNG rows run top to bottom.
    for (std::uint32_t y = 0; y < h / 2; ++y) {
        const auto top = pixels.begin() + static_cast<std::ptrdiff_t>(y * stride);
        const auto bottom = pixels.begin() + static_cast<std::ptrdiff_t>((h - 1 - y) * stride);
        std::swap_ranges(top, top + static_cast<std::ptrdiff_t>(stride), bottom);
    }
    const std::vector<std::uint8_t> png = encodePngRgb(w, h, pixels);
    if (png.empty()) {
        return false;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) byte buffer to stream
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

}  // namespace game
