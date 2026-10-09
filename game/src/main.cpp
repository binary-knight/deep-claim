// Deep Claim client entry point: SDL3 window and OpenGL context through
// RmlUi's reference backend, one RmlUi context, and the App that drives the
// game session.
#include "sim/content.h"

#include "RmlUi_Backend.h"
#include "app.h"
#include "chart_element.h"
#include "content/loader.h"
#include "platform/services.h"
#include "screenshot.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

// The backend's key callback is a plain function pointer, so the app is
// reached through this pointer.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
game::App* gApp = nullptr;

bool keyDown(Rml::Context* context, Rml::Input::KeyIdentifier key, int keyModifier,
             float nativeDpRatio, bool priority) {
    (void)nativeDpRatio;
    // Let text fields and other focused elements see keys first; shortcuts
    // apply only when the interface did not consume the key.
    if (priority) {
        if (key == Rml::Input::KI_F8) {
            Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
            return false;
        }
        // Screenshot keys must work even while a text field has focus.
        if ((key == Rml::Input::KI_F9 || key == Rml::Input::KI_F12) && gApp != nullptr) {
            return !gApp->onKey(key);
        }
        return true;
    }
    (void)context;
    (void)keyModifier;
    return gApp == nullptr || !gApp->onKey(key);
}

// Shipped builds keep data and assets next to the executable; development
// builds fall back to the repository.
// SDL returns UTF-8; build filesystem paths from it explicitly so non-ASCII
// install and user directories work on Windows too.
std::filesystem::path utf8Path(const char* text) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) char to char8_t view
    const std::u8string_view view(reinterpret_cast<const char8_t*>(text));
    return {view};
}

std::string utf8String(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

// Where the playtest trace, save copies and screenshots go. The
// DEEP_CLAIM_PLAYTEST environment variable overrides ("off" disables); a
// development build uses the repository's playtest/ directory; a shipped
// build uses the per-user data directory.
std::filesystem::path playtestDirectory(const std::filesystem::path& prefPath) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) read once at startup
    if (const char* env = std::getenv("DEEP_CLAIM_PLAYTEST"); env != nullptr) {
        const std::string_view value(env);
        if (value.empty() || value == "off" || value == "0") {
            return {};
        }
        return utf8Path(env);
    }
    std::filesystem::path dev = DC_PLAYTEST_DIR;
    if (std::filesystem::is_directory(dev.parent_path())) {
        return dev;
    }
    return prefPath / "playtest";
}

// Runs after each frame is drawn and before it is presented.
void preSwap(int width, int height) {
    if (gApp == nullptr) {
        return;
    }
    const std::optional<std::filesystem::path> request = gApp->takeScreenshotRequest();
    if (request.has_value()) {
        gApp->screenshotDone(*request, game::captureFramebufferPng(width, height, *request));
    }
}

std::filesystem::path locate(const char* subdir, const char* devFallback) {
    const char* base = SDL_GetBasePath();
    if (base != nullptr) {
        const std::filesystem::path candidate = utf8Path(base) / subdir;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return devFallback;
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    // Load game data before touching the window so a data problem is
    // reported even where no display is available.
    const std::filesystem::path dataDir = locate("data", DC_DATA_DIR);
    const std::filesystem::path assetsDir = locate("assets", DC_ASSETS_DIR);
    const content::LoadResult loaded = content::loadFromDirectory(dataDir);
    if (!loaded.content.has_value()) {
        std::fputs("Game data failed to load:\n", stderr);
        std::fputs(content::formatErrors(loaded.errors).c_str(), stderr);
        return 1;
    }
    std::cout << std::format("Deep Claim: data from {}, assets from {}\n", utf8String(dataDir),
                             utf8String(assetsDir));

    if (!Backend::Initialize("Deep Claim", 1366, 820, true)) {
        std::fputs("Could not create the window or OpenGL context.\n", stderr);
        return 1;
    }
    Rml::SetSystemInterface(Backend::GetSystemInterface());
    Rml::SetRenderInterface(Backend::GetRenderInterface());
    Rml::Initialise();

    // The design's three faces (docs/design/handoff.md): Barlow for text,
    // Barlow Condensed for display, JetBrains Mono for figures.
    for (const char* face : {"Barlow-Regular.ttf", "Barlow-Medium.ttf", "Barlow-SemiBold.ttf",
                             "BarlowCondensed-SemiBold.ttf", "BarlowCondensed-Bold.ttf",
                             "JetBrainsMono-Regular.ttf", "JetBrainsMono-Medium.ttf"}) {
        Rml::LoadFontFace(utf8String(assetsDir / "fonts" / face));
    }
    game::registerChartElement();

    Rml::Context* context = Rml::CreateContext("main", Rml::Vector2i(1366, 820));
    if (context == nullptr) {
        Rml::Shutdown();
        Backend::Shutdown();
        return 1;
    }
    Rml::Debugger::Initialise(context);

    char* prefPath = SDL_GetPrefPath("DeepClaim", "DeepClaim");
    const std::filesystem::path saveBase = prefPath != nullptr ? utf8Path(prefPath) : ".";
    SDL_free(prefPath);
    std::unique_ptr<platform::Services> services =
        platform::makeNullServices(saveBase, "Deep Claim Mining");
    const std::filesystem::path playtestDir = playtestDirectory(saveBase);

    int exitCode = 0;
    {
        game::App app(*context, *services, *loaded.content, assetsDir, playtestDir, DC_BUILD_ID);
        gApp = &app;
        Backend::SetPreSwapCallback(&preSwap);
        if (!app.tracePath().empty()) {
            std::cout << std::format(
                "Playtest trace: {} (F9 marks a moment and takes a "
                "screenshot, F12 takes a screenshot)\n",
                utf8String(app.tracePath()));
        }
        if (!app.loadDocument()) {
            exitCode = 1;
        }
        auto last = std::chrono::steady_clock::now();
        bool running = exitCode == 0;
        while (running) {
            running = Backend::ProcessEvents(context, &keyDown, false);
            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - last).count();
            last = now;
            app.update(seconds);
            context->Update();
            Backend::BeginFrame();
            context->Render();
            Backend::PresentFrame();
        }
        Backend::SetPreSwapCallback(nullptr);
        gApp = nullptr;
    }

    Rml::Shutdown();
    Backend::Shutdown();
    return exitCode;
}
