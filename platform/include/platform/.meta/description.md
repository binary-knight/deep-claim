# platform (include/platform)

**Purpose:** Declares the public API of the `dc::platform` library: the abstract `platform::Services` interface through which the game reaches the hosting platform, and the factory for the local-only implementation.

## Key Components
- `services.h` — namespace `platform`. Declares:
  - `class Services` — abstract base with a virtual destructor, defaulted default constructor, and deleted copy/move operations. Pure virtual methods:
    - `[[nodiscard]] std::filesystem::path saveDirectory()` — directory for save files; documented as "created if missing".
    - `void unlockAchievement(std::string_view id)`
    - `void setStat(std::string_view id, std::int64_t value)`
    - `[[nodiscard]] std::string_view playerName()`
  - `std::unique_ptr<Services> makeNullServices(std::filesystem::path baseDirectory, std::string playerName)` — free factory returning the local-only implementation defined in `platform/src/null_services.cpp`.

## Dependencies
- C++ standard library only: `<cstdint>`, `<filesystem>`, `<memory>`, `<string_view>`.
- No project headers and no third-party headers.
- Included by `game/src/app.h`, `game/src/app.cpp`, `game/src/main.cpp` and `game/src/layout_probe.cpp`; by nothing in `sim/`, `content/`, `sim_runner/` or `tests/`.

## Patterns
- Pure-virtual interface with a factory function; the concrete type is not visible in the header.
- `#pragma once` include guard; `[[nodiscard]]` on the two value-returning methods.
- Identifiers for achievements and stats are passed as `std::string_view` ids; stat values are `std::int64_t`.

## Constraints
- `Services` cannot be copied or moved; hold it by `std::unique_ptr` or reference.
- All four methods are non-const, so a `const Services&` cannot call any of them.
- `playerName()` returns a `std::string_view`; its lifetime is tied to the `Services` object that produced it (callers such as `game/src/app.cpp` copy it into a `std::string`).
- The interface is write-only for achievements and stats: there is no getter for either.
- `makeNullServices` takes a `std::string` parameter but the header does not include `<string>` directly; it relies on a transitive include from the other standard headers.
- No platform-conditional declarations (`_WIN32` etc.) appear in the header.
