# platform

**Purpose:** The hosting-platform seam for Deep Claim: a small static library (`platform`, alias `dc::platform`) that defines the abstract `platform::Services` interface (save directory, achievements, stats, player name) and ships one local-only implementation. It exists so a storefront backend (the comments name Steamworks) can replace the local one without changes to game or sim code.

## Key Components
- `CMakeLists.txt` — defines `add_library(platform STATIC src/null_services.cpp)`, the alias `dc::platform`, exports `include/` as the PUBLIC include directory, and links `dc_warnings` PRIVATE.
- `include/` — public include root; holds `platform/services.h`, the only public header.
- `src/` — private implementation; holds `null_services.cpp` (the `NullServices` class and the `makeNullServices` factory).

## Dependencies
- Build: `dc_warnings` (interface target from the root `cmake/Warnings.cmake`, linked PRIVATE). Added to the build by the root `CMakeLists.txt` via `add_subdirectory(platform)`.
- Code: C++ standard library only (`<filesystem>`, `<memory>`, `<string_view>`, `<cstdint>`, `<map>`, `<set>`, `<string>`, `<system_error>`, `<utility>`). No third-party libraries and no includes from `sim/`, `content/`, `game/`, `sim_runner/` or `tests/`.
- Consumers: only `game/`. `game/CMakeLists.txt` links `dc::platform` into the `deep_claim` and `deep_claim_layout_probe` executables; `game/src/app.h`, `game/src/app.cpp`, `game/src/main.cpp` and `game/src/layout_probe.cpp` include `platform/services.h`. `sim/`, `content/`, `sim_runner/`, `game/session/` and `tests/` do not reference it.

## Patterns
- Abstract interface plus factory function: callers hold `std::unique_ptr<platform::Services>` or `platform::Services&`; the concrete class is hidden in an anonymous namespace in `src/`.
- Null Object: `NullServices` satisfies the whole interface with local defaults (filesystem saves, in-memory achievement/stat recording).
- Composition root lives in the consumer: `game/src/main.cpp` builds the services with `makeNullServices(saveBase, "Deep Claim Mining")` (save base from SDL's pref path, falling back to `.`) and passes a reference into `App`; `game/src/layout_probe.cpp` uses the system temp directory and the name `"Probe"`.
- Public/private split by directory: `include/platform/` is exported, `src/` is not.

## Constraints
- `Services` is non-copyable and non-movable; it is used only through pointers/references.
- The library contains no OS-specific code: no `#ifdef _WIN32`, `__APPLE__` or `__linux__` branches and no preprocessor conditionals other than `#pragma once`. Portability comes from `std::filesystem`.
- Only one implementation exists (`NullServices`); there is no Steamworks or other storefront backend in the tree.
- Achievements and stats set through `NullServices` are held in memory for the life of the object, are never persisted, and cannot be read back through the interface.
- No test target under `tests/` links `dc::platform`.
- Of the four interface methods, the game currently calls only `saveDirectory()` and `playerName()` (both in `game/src/app.cpp`); `unlockAchievement` and `setStat` have no call sites in the repo.
