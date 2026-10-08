#pragma once

// Captures the current OpenGL default framebuffer as a PNG. Call from the
// backend's pre-swap callback, when the finished frame is in the back
// buffer and nothing else is bound.

#include <filesystem>

namespace game {

[[nodiscard]] bool captureFramebufferPng(int width, int height, const std::filesystem::path& file);

}  // namespace game
