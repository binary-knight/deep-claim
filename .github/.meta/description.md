# .github

**Purpose:** GitHub-specific repository configuration for Deep Claim. It currently holds only the continuous integration workflow that builds, tests, format-checks and lints the C++ project on every push and pull request.

## Key Components
- `workflows/` — GitHub Actions workflows; contains the single file `ci.yml` (workflow `CI`) with four jobs: `build` (Linux and Windows, presets `debug`/`release`/`asan`), `game` (Linux and Windows, preset `debug-game`), `format` (clang-format 18) and `tidy` (clang-tidy 18).

## Dependencies
- Consumes, from the repository root: `CMakePresets.json`, `CMakeLists.txt`, `vcpkg.json`, `.gitmodules`, `.clang-format`, `.clang-tidy`, and the `external/vcpkg` submodule.
- External: GitHub-hosted runners `ubuntu-24.04` and `windows-2022`; `actions/checkout@v5` and `actions/cache@v4`; Ubuntu apt packages `clang-format-18`, `clang-tidy-18` and the SDL3 system development libraries.
- Nothing in the repository's build (CMake targets, sources, tools) depends on this directory.

## Patterns
- One workflow file is the whole CI definition; it calls the CMake configure/build/test presets instead of duplicating compiler flags, so the presets remain the single description of each build flavor.
- Contract with the rest of the repo: a preset name used in `ci.yml` must exist as a configure, build and (where ctest is run) test preset in `CMakePresets.json`; tests registered with `catch_discover_tests` in `tests/` and with `add_test` in `sim_runner/` and `game/` are picked up automatically by `ctest --preset`.
- vcpkg dependencies are cached per job flavor with `actions/cache` over a vcpkg `files` binary source.

## Constraints
- Only `workflows/ci.yml` is tracked here. There is no `dependabot.yml`, no CodeQL or secret-scanning workflow, no `CODEOWNERS`, no issue or pull request templates, and no composite actions or reusable workflows.
- The workflow declares no `permissions:` or `concurrency:` settings and uses no repository secrets.
- CI coverage boundaries (details in `workflows/.meta/description.md`): sanitizers on Linux only, game client in Debug only, `game/backends/` excluded from clang-format, `tests/` and the game client excluded from clang-tidy, no coverage measurement.
