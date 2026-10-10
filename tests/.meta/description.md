# tests

**Purpose:** The project's unit and integration test suite: one Catch2 v3 executable, `sim_tests`, that exercises the deterministic simulation core (`sim/`), the JSON content loader (`content/`) and the headless game session layer (`game/session/`). It also holds three guard tests that read repository files (shipped `data/`, a sim source file, generated UI tokens, the prose draft) instead of calling code.

## Key Components

Build and shared fixtures:
- `CMakeLists.txt` - defines the single target `sim_tests` from the 25 `*_test.cpp` files, links it, bakes in `DC_DATA_DIR` and `DC_SOURCE_DIR`, and registers every test case with CTest through `catch_discover_tests`.
- `test_content.h` - `test::makeContent()` builds a complete `sim::Content` in code (3 resources iron/copper/lithium, 2 techs, 2 regions `north`/`flats`, 1 event `boom`, full balance and AI knobs); `test::setup()` returns a `sim::GameSetup` with one player and one AI named "Rival". Deliberately independent of `data/*.json`.
- `test_strings.h` - prose-catalogue helpers: `test::readTextFile`, `test::fixtureStrings(eventIds, techIds)` (the shipped `data/strings.json` with its `event.*`/`tech.*` keys rebuilt for fixture ids) and `test::addStrings(content)` (copies the shipped strings into fixture content).

Numeric and utility primitives (`sim/`):
- `fixed_test.cpp` - `sim/fixed.h`: `Fixed` construction, truncating multiply/divide, 128-bit intermediates, `compareProducts`, `checkedMulDiv`, `fromDouble` rounding, decimal text.
- `math_test.cpp` - `sim/math.h`: `fixedLn`, `fixedExp`, `fixedPow` against known values, inverse and monotonicity properties, `clampFixed`.
- `money_test.cpp` - `sim/money.h`: `Money` arithmetic, scaling by `Fixed`, ratios, decimal text.
- `ids_test.cpp` - `sim/ids.h`: `StrongId` default-invalid, index wrapping, comparison, tag type distinctness.
- `rng_test.cpp` - `sim/rng.h`: `Pcg32` against the reference sequence, `below` bounds, stream divergence, state restore.
- `random_test.cpp` - `sim/random.h`: `uniformFixed`, `uniformInt`, `approxNormal` distribution checks, deterministic independent `RngStreams`.
- `archive_test.cpp` - `sim/archive.h`, `sim/serialize.h`, `sim/hash.h`, `sim/save.h`: byte layout (little-endian, uint32 length prefixes), FNV-1a hash, truncated/oversized/huge-length rejection, exact save/load round trip, rejection of bad headers, inconsistent worlds and saves made with different content.

Simulation rules (`sim/`, each drives `sim::Simulation` through commands and inspects `sim/world_state.h` and `sim/view.h`):
- `determinism_test.cpp` - `sim/simulation.h`: same seed gives the same state hash, different seeds differ, and the golden hash `kGolden` for seed 42 after one year.
- `worldgen_test.cpp` - `sim/worldgen.h`: region classification by mineability, deposit caps, opening hand and listing queue promises, plot creation order, deposit presence and log-normal size statistics, per-seed determinism.
- `plot_market_test.cpp` - `sim/plot_market.h` (land auctions, tag `[market]`): listings, relisting and ask decay, bid validation and cash reservation, winner selection, public sales record, withdrawal and cooldown of unsold plots, per-viewer filtering of views and reports, save/load mid-auction; also subclasses `sim::Controller` to test `collectCommands`.
- `markets_test.cpp` - `sim/markets.h` (ore commodity prices, tag `[markets]`): base price and history, power-law equilibrium, sale price impact, sell command validation, demand noise bounds, demand growth, save/load validation.
- `sell_policy_test.cpp` - standing sell policies and the month-end cover-costs rule (spec 001) via `sim::SetSellPolicy` in `sim/commands.h`; defaults from the balance data, reserve handling, locked ore, monthly summary, command and save validation.
- `surveys_test.cpp` - `sim/surveys.h`: survey cost and delay, command validation, unbiased log-space noise, "major" estimate flag, save validation, no leakage to other companies.
- `mines_test.cpp` - `sim/mines.h`: capex and build delay, extraction/opex/decline/depletion, mothballing, locked resources, accounting conservation, empty-plot and locked-only notices, survey estimate in the mine view, running-low warnings, save/load.
- `finance_test.cpp` - `sim/finance.h`: loans (capacity, cap on active loans, early repayment), storage charges, insolvency warnings, grace period, bankruptcy effects, save/load validation.
- `research_test.cpp` - `sim/research.h`: research cost and completion, command validation, effect on survey noise and resource unlocks, unlock news, view progress toward locked resources, save validation.
- `events_test.cpp` - `sim/events.h`: event start/duration/end, multipliers on demand, opex and land prices, minimum year and firing rate, order independence, save validation.
- `ai_test.cpp` - `sim/ai_controller.h`: twenty-year run without rejected commands, determinism from a save, AI knobs in the content fingerprint, supply discount, bid jitter fairness, first-mine terms, bankruptcy stop, joint affordability of daily commands.

Content loader and shipped data (`content/`):
- `content_test.cpp` - `content/loader.h` via `content::loadFromStrings` on in-file JSON sources: successful load, and error reporting with paths for bad JSON, unknown/missing keys, wrong types, precision, references, duplicate ids, prerequisite cycles, effect fields, balance and AI knob bounds; all errors collected in one pass.
- `data_files_test.cpp` - `content::loadFromDirectory(DC_DATA_DIR)`: the shipped `data/` files load cleanly, specific shipped balance relationships hold, and worlds generated from shipped data give every one of 200 seeds a fair opening hand.
- `strings_test.cpp` - prose catalogue (spec 002) through `game/format.h` and `content/loader.h`: placeholder filling, plurals, coverage of every notice kind and reject reason in shipped prose, loader rejection of a broken catalogue, fingerprint behaviour, and key/placeholder parity between `tools/prose/draft.tsv` and `data/strings.json`.

Game session layer (`game/session/`):
- `session_test.cpp` - `game/session.h`, `game/format.h`, `game/trace.h`: money/day/tonnage formatting and strict parsers, clock advance at speed, player command binding and log, rivals, save/load, stop on bankruptcy, auto-pause rules and their text round trip, log filtering of relists and unlocks, monthly sales log line.
- `trace_test.cpp` - `game/trace.h`, `game/replay.h`, `game/png.h`: every command through trace JSON, malformed record rejection, replay reproducing a game, stop-at-day and divergence reporting, replay through a load record, and the PNG encoder checked with an in-test stored-deflate reader.

Repository guard tests (no project headers included):
- `view_source_test.cpp` - reads `sim/src/view.cpp` and fails if it contains `hidden`, `Hidden` or `DepositTruth`.
- `design_tokens_test.cpp` - reads `docs/design/tokens.css` and `game/assets/ui/tokens.rcss` and checks every token appears in the generated file with the same value, plus four utility classes.

## Dependencies
- CMake targets linked by `sim_tests` (all PRIVATE): `dc::sim`, `dc::content`, `dc::game_session`, `Catch2::Catch2WithMain`, `dc_warnings`. `dc::platform` is not linked.
- Third party: Catch2 3 (`find_package(Catch2 3 CONFIG REQUIRED)`, headers `catch2/catch_test_macros.hpp`). nlohmann-json is not used directly; it is a private dependency of `game_session`.
- Headers included from other subsystems: `sim/*.h` (25 of the 29 public headers), `content/loader.h`, `game/session.h`, `game/format.h`, `game/trace.h`, `game/replay.h`, `game/png.h`.
- Repository files read at run time through the baked-in paths: `data/` (all content JSON, `data/strings.json`), `sim/src/view.cpp`, `docs/design/tokens.css`, `game/assets/ui/tokens.rcss`, `tools/prose/draft.tsv`.
- Included from the root `CMakeLists.txt` only when `DC_BUILD_TESTS` is ON (the default), after `enable_testing()`.

## Patterns
- Catch2 v3 with `Catch2WithMain`; there is no `main.cpp`. `catch_discover_tests(sim_tests)` makes each `TEST_CASE` its own CTest entry.
- Flat layout, one `<module>_test.cpp` per module under test, file-local helpers in anonymous namespaces (for example `firstListed`, `buyFirstListed`, `tickToNextMonthEnd`, `calmContent`, `certainEventContent`).
- Tags name the module and cross-cutting concern: `[ai]`, `[archive]`, `[content]`, `[data]`, `[design]`, `[determinism]`, `[events]`, `[finance]`, `[fixed]`, `[format]`, `[ids]`, `[market]` (land auctions), `[markets]` (ore prices), `[math]`, `[mines]`, `[money]`, `[opening]`, `[png]`, `[random]`, `[replay]`, `[research]`, `[rng]`, `[save]`, `[sell_policy]`, `[session]`, `[strings]`, `[surveys]`, `[trace]`, `[view]`, `[worldgen]`.
- Two content sources kept apart: the in-code fixture (`test_content.h`) for rule tests, and the shipped `data/` directory for `[data]` tests. Tests that tune a rule copy the fixture and edit one field.
- Save validation is tested by saving, corrupting state or bytes, and asserting the load is refused.
- Statistical tests loop over seeds 1..200 (`worldgen_test.cpp`, `data_files_test.cpp`); `ai_test.cpp` plays twenty simulated years.
- Session and trace tests use in-memory streams (`std::ostringstream`) and byte vectors, not temporary files.
- Source-text guard tests (`view_source_test.cpp`, `design_tokens_test.cpp`, the draft parity case in `strings_test.cpp`) assert on file contents with string search and `std::regex`.

## Constraints
- `kGolden` in `determinism_test.cpp` (`0x2a5da3177ab7c69d`, seed 42, one year, fixture content) is recorded on Linux/GCC and verified on Windows/MSVC in CI; its comment requires a deliberate simulation change to update the constant in the same commit and say so in the commit message.
- `test_content.h` must stay independent of `data/*.json` so balance tuning does not move the golden hash.
- `DC_DATA_DIR` and `DC_SOURCE_DIR` are absolute paths compiled in from `CMAKE_SOURCE_DIR`; the binary depends on the source tree being present at that path, not on the working directory.
- `test::fixtureStrings` relies on `data/strings.json` having one key per line, which `tools/prose/to_json.py` produces.
- `design_tokens_test.cpp` requires more than 40 tokens in `docs/design/tokens.css` and the `GENERATED by tools/design/tokens_to_rcss.py` marker in `tokens.rcss`.
- No test is hidden, skipped, marked may-fail or conditionally compiled; every case runs in every configuration that builds tests.
- Compiled with the project warning set (`dc_warnings`; presets set `DC_WARNINGS_AS_ERRORS=ON`).
- The `runner_smoke` and `runner_sweep` CTest entries are defined in `sim_runner/CMakeLists.txt`, not here.
