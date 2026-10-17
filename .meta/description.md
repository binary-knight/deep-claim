# deep-claim

**Purpose:** Deep Claim (working title) is a single-player mining company management sim written in C++20. On Earth, the player's company races AI rivals to found a space program before a deadline (`DOMAIN.md`, `docs/design/core-loop.md`). The repository holds a deterministic simulation core, a JSON content pipeline, an optional SDL3/RmlUi desktop client, a headless balance runner, the shipped game data, and the specs and playtest records that drive development.

## Key Components
- `sim/` - static library `dc::sim`: all game rules and dynamic state (plot auctions, surveys, mines, commodity markets, loans, research, world events, the space program with its deadline and outcome, the built-in AI competitor), save/load and state hashing. Standard library only.
- `content/` - static library `dc::content`: `content/loader.h` reads and validates the eight `data/*.json` documents into `sim::Content`. The only JSON and filesystem boundary for game content.
- `platform/` - static library `dc::platform`: abstract `platform::Services` (save directory, achievements, stats, player name) with one local implementation from `makeNullServices`. Used only by the client.
- `game/` - the client. `game/session/` is the UI-independent library `dc::game_session` (time control, the queue of pending player orders, game-over handling, log, prose formatting, JSONL playtest trace, replay, PNG encoder) plus the headless `deep_claim_replay` tool, built in every preset. `game/src/` is the SDL3/RmlUi executable `deep_claim` and the `deep_claim_layout_probe` test tool, built only with `DC_BUILD_GAME=ON`. `game/backends/` is the vendored RmlUi SDL/GL3 backend; `game/assets/` holds fonts and RmlUi documents.
- `sim_runner/` - headless executable `sim_runner`: every company is AI-driven for N days or over a sweep of seeds, each seed stopping when its game ends; emits a JSON summary (including spec 008 bite measures and spec 010 race measures) and optional CSV; the exit code gates sanity and balance checks, including the race gates (founded before the deadline, win balance, bankruptcies).
- `tests/` - one Catch2 v3 binary `sim_tests` (26 test files, 410 test cases; three golden determinism hashes `kGolden`, `kGoldenPlayed`, `kGoldenRace` in `tests/determinism_test.cpp`) covering `sim`, `content` and `game/session`, plus guard tests that read repository files (`data/`, `docs/design/tokens.css`, `game/assets/ui/tokens.rcss`, `tools/prose/draft.tsv`, `sim/src/view.cpp`).
- `data/` - shipped game content: `resources.json`, `techs.json`, `regions.json`, `events.json`, `balance.json`, `ai.json`, `program.json`, `strings.json`.
- `tools/` - hand-run developer scripts, outside the build: `tools/design/` generates `game/assets/ui/tokens.rcss` and `icons.tga`; `tools/prose/` turns `draft.tsv` into `data/strings.json` through an LLM polish step (`agy` CLI).
- `docs/` - `DESIGN.md` (append-only decision log, sections through spec 008 and "The race for the program (spec 010, step 1)"), `process.md` (spec-driven phased process), `specs/` (001 to 007; later specs are in `spec/`), `adrs/` (12 feature decision records for specs 008 to 010), `mvp/journeys/` (player journeys J-001 to J-003), `design/` (locked interface design and tokens, plus `core-loop.md`, the game's premise and core loop, and `earth-strategy.md`), `playtests/` (four dated write-ups, 2026-10-08 to 2026-10-11), and README-only skeleton directories from the etc harness (`contexts/`, `guides/`, `invariants/`, `plans/`, `prds/`, `sources/`, `standards/`).
- `cmake/` - `Warnings.cmake` (interface target `dc_warnings`), `Sanitizers.cmake` (ASan/UBSan flags behind `DC_SANITIZE`) and `BuildId.cmake` (script-mode writer of the generated `dc_build_id.h`).
- `.github/` - `workflows/ci.yml`: preset-driven build and test on ubuntu-24.04 and windows-2022, a game-client job, a Windows `playtest-package` job that uploads the `deep-claim-windows` artifact fetched by `build.sh`, clang-format 18 and clang-tidy 18 jobs.
- `CMakeLists.txt` - root build: project `DeepClaim` 0.1.0, C++20 without extensions, options `DC_BUILD_TESTS` (ON), `DC_BUILD_RUNNER` (ON), `DC_BUILD_GAME` (OFF), `DC_SANITIZE` (OFF), `DC_WARNINGS_AS_ERRORS` (ON); adds the always-run `dc_build_id` target, which runs `cmake/BuildId.cmake` to write the short git revision as `DC_BUILD_ID` into `<build>/generated/dc_build_id.h` (consumed by `deep_claim`); adds `sim`, `content`, `platform`, `game/session` always and `tests`, `sim_runner`, `game` by option.
- `CMakePresets.json` - configure/build presets `debug`, `release`, `asan`, `debug-game`, `release-game` (all into `build/<preset>` with the vcpkg toolchain); test presets for all but `release-game`.
- `vcpkg.json` - manifest: `catch2`, `nlohmann-json`; feature `game` adds `sdl3` (x11 and wayland on Linux) and `rmlui`; pinned by `builtin-baseline`.
- `.gitmodules` - the `external/vcpkg` submodule (not described here).
- `.clang-format`, `.clang-tidy` - Google-based style, 4-space indent, 100 columns; tidy check families with warnings as errors and the naming rules.
- `build.sh` - playtest launcher: by default fetches the `deep-claim-windows` artifact of this commit from the CI `playtest-package` job onto the owner's share (`DEEP_CLAIM_SHARE`, default `~/snorkel/deep-claim`); `--linux` builds the `release-game` client here and runs it (`--no-run` to build only).
- `README.md` - build, play and balance-run instructions.
- `DOMAIN.md` (domain briefing: premise, constraints, risk posture), `PROJECT.md` (where things live, current phase), `ARCHITECTURE.md` (ratified rules R-001 to R-007, exemplars, do-not-copy list; rendered from `.etc_sdlc/architecture-baseline.yaml`, never hand-edited), `CLAUDE.md` (agent working rules).
- `spec/` - feature specs from 008 on, one file per feature slug (`markets-that-bite.md`, `mine-economics.md`, `race-for-the-program.md`).
- `roles/` - etc harness role manifests (`architect`, `backend-dev`, `code-reviewer`, `frontend-dev`, `sem` YAML) declaring each agent role's default context. No description of its own; predates the first survey.
- Not described by this tree: `external/`, `build/`, `vcpkg_installed/`, `playtest/`, `.claude/` (except `.claude/standards/`, which has its own description), `.etc_sdlc/`, editor directories.

## Dependencies
- Toolchain: CMake 3.25 or later, a C++20 compiler (GCC/Clang or MSVC x64; `sim/src/fixed.cpp` needs `__int128` or MSVC `_umul128`), vcpkg from the `external/vcpkg` submodule.
- Third-party libraries: Catch2 3 (tests), nlohmann-json (`content`, and `game/session/src/trace.cpp`), SDL3 and RmlUi (client only, vcpkg feature `game`), plus the vendored RmlUi backend and its glad-generated OpenGL 3.3 loader in `game/backends/`. On Linux the client also needs the X11/Wayland/GL development packages listed in `README.md`.
- Internal link graph (CMake targets):
  - `sim` links nothing but `dc_warnings`.
  - `content` -> `sim` (PUBLIC), nlohmann-json (PRIVATE).
  - `platform` -> nothing internal.
  - `game_session` -> `sim` (PUBLIC), nlohmann-json (PRIVATE).
  - `deep_claim_replay` -> `game_session`, `content`.
  - `sim_runner` -> `sim`, `content`.
  - `sim_tests` -> `sim`, `content`, `game_session`, Catch2.
  - `deep_claim` -> `game_session`, `content`, `platform`, `rmlui_backend`, RmlUi, SDL3.
- Runtime data: executables find content through the compile definition `DC_DATA_DIR` (source-tree `data/`); the client prefers a `data/` and `assets/` copy placed next to the executable by the `deep_claim_assets` target.
- Generated, committed artifacts: `data/strings.json` (from `tools/prose/`), `game/assets/ui/tokens.rcss` (from `docs/design/tokens.css` via `tools/design/tokens_to_rcss.py`), `game/assets/ui/icons.tga` (from `tools/design/icons.py`).

## Patterns
- Layering by library: rules in `sim`, I/O and parsing in `content`, host services in `platform`, client-independent session logic in `game/session`, windowing and UI only in `game/src`. Hosts drive the sim through `sim::Simulation` (submit commands, `tick()` once per in-game day, read `CompanyView` and notices) and never mutate `WorldState`.
- Determinism: integer and 6-digit fixed-point arithmetic only, a local PCG32 with one stream per system, FNV-1a state hashing, commands applied in (company id, submission order). A replay is the seed plus the command log; `game/session` records and replays JSONL traces and compares per-day state hashes.
- One `serialize()` definition drives hashing, saving and loading (`sim/archive.h`, `sim/serialize.h`).
- Data-driven content: all tuning lives in `data/*.json`; the loader collects every error across all files and returns a result type instead of throwing.
- Hidden truth versus view: players and the AI see only `CompanyView`; `sim/src/view.cpp` must not read concealed deposit state and a test greps for it.
- All player-facing sentences come from the string catalogue in `data/strings.json`.
- Spec-driven, phased development recorded in `docs/process.md`, with numbered specs in `docs/specs/` (001 to 007) and `spec/<slug>.md` (008 on, with decision records in `docs/adrs/`) and decisions appended to `docs/DESIGN.md`; balance is checked by `sim_runner` sweeps registered as CTest tests (`runner_smoke`, `runner_outcome`, `runner_bite_measures`, `runner_sweep`), plus `layout_probe` in the game presets.
- Quality gates: warnings as errors through `dc_warnings` on every first-party target, clang-format and clang-tidy in CI, ASan/UBSan preset for the sim stack on Linux.

## Constraints
- `sim` must not depend on SDL, RmlUi, JSON, the filesystem or any platform SDK (stated in `sim/CMakeLists.txt`); no floating point in sim state.
- Save files carry a content fingerprint: changing `resources.json`, `techs.json`, `regions.json`, `events.json`, `balance.json`, `ai.json`, `program.json`, or a tech/event name in `strings.json`, invalidates saves and traces; other prose edits do not. The save version stays at 1 and saves are not compatible across commits until a first public build (`sim/include/sim/save.h`).
- A game ends when a company completes the last program stage or the deadline day in `data/program.json` is reached; from then `WorldState::outcome` is set, ticks change nothing and every order is rejected as `GameEnded` (`sim/src/simulation.cpp`, `docs/DESIGN.md` "The race for the program (spec 010, step 1)").
- `strings.json` must contain exactly the key set the loader expects; unknown keys are load errors in every data file.
- `game/session` must build without SDL or RmlUi so it is covered by tests in every preset; the client itself is built only with the `debug-game` or `release-game` presets.
- `game/backends/` is third-party code (RmlUi 6.3, MIT) with two marked local changes; it is compiled with warnings off and excluded from the clang-format job.
- Generated files (`tokens.rcss`, `icons.tga`, `strings.json`) are committed and are not regenerated by CMake; tests check `tokens.rcss` and `strings.json` against their sources.
- Supported and CI-tested platforms are Linux (ubuntu-24.04) and Windows (windows-2022, MSVC x64); sanitizer runs are Linux-only and exclude the client.
- `DC_BUILD_ID` is re-read on every build (the header is rewritten only when the revision changes) and is `unknown` outside a git checkout.
