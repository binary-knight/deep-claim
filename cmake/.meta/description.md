# cmake

**Purpose:** Project-local CMake modules that define the build's compiler-warning policy and its optional AddressSanitizer/UndefinedBehaviorSanitizer instrumentation. The root `CMakeLists.txt` appends this directory to `CMAKE_MODULE_PATH` and runs `include(Warnings)` then `include(Sanitizers)` before any `add_subdirectory()`.

## Key Components
- `Warnings.cmake` - defines the `dc_warnings` INTERFACE library that carries the project warning flags as INTERFACE compile options. MSVC: `/W4 /permissive- /utf-8`, plus `/WX` when `DC_WARNINGS_AS_ERRORS` is ON. Every other compiler (GCC, Clang): `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough`, plus `-Werror` when `DC_WARNINGS_AS_ERRORS` is ON.
- `Sanitizers.cmake` - defines no target and no function; it is a top-level `if(DC_SANITIZE)` block that runs at include time. MSVC: emits `message(WARNING "MSVC supports ASan only; UBSan is unavailable.")` and calls `add_compile_options(/fsanitize=address)`. Every other compiler: `add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)` and `add_link_options(-fsanitize=address,undefined)`. When `DC_SANITIZE` is OFF the file does nothing.

## Dependencies
- CMake 3.25+ (the root `cmake_minimum_required`); uses only built-in commands (`add_library(... INTERFACE)`, `target_compile_options`, `add_compile_options`, `add_link_options`, `message`) and the built-in `MSVC` variable. No `find_package`, no third-party modules.
- Inputs from the root `CMakeLists.txt`: cache options `DC_WARNINGS_AS_ERRORS` (default ON) and `DC_SANITIZE` (default OFF). Both must be declared before the `include()` calls, which the root file does.
- `CMakePresets.json` drives the options: hidden preset `base` sets `DC_WARNINGS_AS_ERRORS=ON` (inherited by `debug`, `release`, `asan`, `debug-game`, `release-game`); only `asan` sets `DC_SANITIZE=ON`, and it also sets `DC_BUILD_GAME=OFF`.
- Consumers of `dc_warnings` (all link it PRIVATE): `sim` (`sim/CMakeLists.txt`), `content` (`content/CMakeLists.txt`), `platform` (`platform/CMakeLists.txt`), `game_session` and `deep_claim_replay` (`game/session/CMakeLists.txt`), `sim_tests` (`tests/CMakeLists.txt`), `sim_runner` (`sim_runner/CMakeLists.txt`), `deep_claim` and `deep_claim_layout_probe` (`game/CMakeLists.txt`). The one target that does not link it is `rmlui_backend` in `game/CMakeLists.txt` (vendored RmlUi sample backend), which instead compiles with `-w` (non-MSVC) or `/w` (MSVC).
- No dependency on any other top-level source directory.

## Patterns
- Warnings are opt-in per target through an INTERFACE library: a target gets the flags only by naming `dc_warnings` in `target_link_libraries(... PRIVATE ...)`. PRIVATE linkage keeps the flags from propagating to dependents, so third-party code (Catch2, nlohmann-json, SDL3, RmlUi, the vendored `rmlui_backend`) is not compiled under the project warning set.
- Sanitizers are the opposite: directory-scoped `add_compile_options` / `add_link_options` issued from the root directory scope, so they apply to every target created afterwards in the root and in all subdirectories, with no per-target call. Dependencies prebuilt by vcpkg are not rebuilt with these flags.
- Compiler selection is a two-way `if(MSVC) ... else()` branch in both files; GCC and Clang share the `else()` branch and there is no separate `CMAKE_CXX_COMPILER_ID` check.
- Naming: the project prefix `DC_` for options and `dc_` for the target; the target has no `dc::` alias, unlike the libraries (`dc::sim`, `dc::content`, `dc::platform`, `dc::game_session`).

## Constraints
- Include order matters: `Warnings.cmake` and `Sanitizers.cmake` must be included from the root before `add_subdirectory()`, because subdirectories reference `dc_warnings` and because directory-scoped options only affect targets created after the call.
- `Warnings.cmake` calls `add_library(dc_warnings INTERFACE)` unconditionally with no include guard; including it twice in one configure fails with a duplicate-target error.
- Both option values are read at configure time (plain `if()`, not generator expressions); changing `DC_SANITIZE` or `DC_WARNINGS_AS_ERRORS` requires a reconfigure.
- On MSVC, `DC_SANITIZE=ON` yields AddressSanitizer only (`/fsanitize=address`), a configure-time warning, and no link option; UndefinedBehaviorSanitizer is never enabled there. `docs/DESIGN.md` ("Sanitizers") and `.github/workflows/ci.yml` run the `asan` preset on `ubuntu-24.04` only; the `windows-2022` legs build `debug` and `release`.
- On non-MSVC, `-fno-sanitize-recover=all` makes every sanitizer report fatal, so a UBSan finding aborts the test process instead of logging and continuing.
- Nothing in this directory prevents `DC_SANITIZE=ON` together with `DC_BUILD_GAME=ON`; the separation of sanitizers from the SDL3/RmlUi client exists only in the `asan` preset (`DC_BUILD_GAME=OFF`).
- With the `/WX` or `-Werror` flag on by default (option default ON and preset `base` ON), any warning from the listed set in a target that links `dc_warnings` fails the build.
