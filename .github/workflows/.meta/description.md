# workflows

**Purpose:** GitHub Actions workflow definitions for Deep Claim. The single workflow `ci.yml` ("CI") configures, builds and tests the project on Linux and Windows through the CMake presets, and checks C++ formatting and clang-tidy cleanliness on Linux.

## Key Components
- `ci.yml` — workflow `CI`, triggered on every `push` and every `pull_request` (no branch, tag or path filters). Four jobs, nine job runs per trigger:
  - `build` (matrix, 5 runs, `fail-fast: false`): `ubuntu-24.04` x {`debug`, `release`, `asan`} and `windows-2022` x {`debug`, `release`}. Steps: checkout with submodules, restore vcpkg binary cache, bootstrap vcpkg (`bootstrap-vcpkg.sh` or `.bat`, `-disableMetrics`), `cmake --preset <p>`, `cmake --build --preset <p> --parallel`, `ctest --preset <p>`. With `DC_BUILD_GAME` off this builds `sim`, `content`, `platform`, `game_session`, `sim_tests` and `sim_runner`; ctest runs the Catch2 tests from `tests/` plus `runner_smoke` and `runner_sweep` from `sim_runner/CMakeLists.txt`.
  - `game` (matrix, 2 runs, `fail-fast: false`): `ubuntu-24.04` and `windows-2022`, preset `debug-game` only. Same steps as `build`, plus on Linux an `apt-get install` of the X11/Wayland/EGL/GL/DRM/GBM development packages SDL3 needs. Builds the `deep_claim` client and `deep_claim_layout_probe`; ctest additionally runs `layout_probe` from `game/CMakeLists.txt`.
  - `format` (`ubuntu-24.04`): checkout without submodules, installs `clang-format-18` from apt, runs `git ls-files '*.cpp' '*.h' | grep -v '^game/backends/' | xargs clang-format-18 --dry-run -Werror` against the root `.clang-format`.
  - `tidy` (`ubuntu-24.04`): checkout with submodules, restore vcpkg cache, bootstrap vcpkg, installs `clang-tidy-18` from apt, runs `cmake --preset debug -DDC_BUILD_TESTS=OFF -DCMAKE_CXX_CLANG_TIDY=clang-tidy-18` then `cmake --build --preset debug --parallel`. clang-tidy runs as part of compilation using the root `.clang-tidy`; no ctest step.

## Dependencies
- GitHub-hosted runners `ubuntu-24.04` and `windows-2022`.
- Actions: `actions/checkout@v5`, `actions/cache@v4` (both referenced by major-version tag).
- Repo files read by the jobs: `CMakePresets.json` (presets `debug`, `release`, `asan`, `debug-game`), root `CMakeLists.txt` (options `DC_BUILD_TESTS`, `DC_BUILD_GAME`, `DC_SANITIZE`, `DC_WARNINGS_AS_ERRORS`), `vcpkg.json` and `.gitmodules` (cache key inputs), `.clang-format`, `.clang-tidy`.
- Git submodule `external/vcpkg` (microsoft/vcpkg), used as the CMake toolchain and bootstrapped in every job except `format`.
- vcpkg packages built in CI: `catch2`, `nlohmann-json`; in the `game` job also `sdl3` (features `x11`, `wayland` on Linux) and `rmlui` through the manifest feature `game`.
- Ubuntu apt packages: `clang-format-18`, `clang-tidy-18`, and for the `game` job `libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev pkg-config`.
- The compiler and CMake are whatever the runner image provides; the workflow installs neither and selects no compiler or generator explicitly.

## Patterns
- Preset-driven CI: every configure/build/test command is `cmake --preset`, `cmake --build --preset` or `ctest --preset`, so CI runs the same commands a developer runs locally.
- `strategy.matrix.include` with explicit (os, preset) pairs for `build`; plain `os` matrix for `game`.
- vcpkg binary caching through the `files` backend: `VCPKG_BINARY_SOURCES=clear;files,<workspace>/vcpkg-cache,readwrite`, with `actions/cache` persisting that directory. Cache keys are `vcpkg-<os>-<preset>-<hash>`, `vcpkg-game-<os>-<hash>` and `vcpkg-ubuntu-24.04-tidy-<hash>`, where the hash is `hashFiles('vcpkg.json', '.gitmodules')`; each has a prefix `restore-keys` fallback. A comment in the file records that vcpkg removed its `x-gha` backend.
- OS-specific steps are selected with `if: runner.os == 'Linux'` / `'Windows'`.
- Static analysis is wired in through `CMAKE_CXX_CLANG_TIDY` on a normal build rather than a separate `run-clang-tidy` invocation; `.clang-tidy` sets `WarningsAsErrors: '*'`, so any finding fails the build.
- Warnings are errors in every job: the hidden `base` preset sets `DC_WARNINGS_AS_ERRORS=ON`.

## Constraints
- `asan` (ASan + UBSan, `DC_SANITIZE=ON`, `DC_BUILD_GAME=OFF`) runs on Linux only; there is no Windows sanitizer run.
- The game client is built and tested in Debug only; the `release-game` preset is not used by any job and has no test preset in `CMakePresets.json`.
- clang-format skips `game/backends/` (vendored RmlUi backend sources) and only matches `*.cpp` and `*.h`.
- clang-tidy does not analyze `tests/` (configured with `DC_BUILD_TESTS=OFF`; the in-file comment says Catch2 macros do not survive the checks) and does not analyze `game/src/` or `game/backends/`, because the `debug` preset leaves `DC_BUILD_GAME` off. It covers the targets built by the `debug` preset: `sim`, `content`, `platform`, `game_session`, `sim_runner`.
- The vcpkg cache key does not include `CMakePresets.json` or the vcpkg triplet; it changes only when `vcpkg.json` or `.gitmodules` changes.
- The workflow has no `permissions:` block, no `concurrency:` group, no `timeout-minutes`, no artifact upload, no coverage collection and no release or deploy job. A branch with an open pull request in the same repository triggers both the `push` and the `pull_request` event.
- No macOS runner is used.
