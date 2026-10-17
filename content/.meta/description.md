# content

**Purpose:** The JSON content loader, built as static library `content` (alias `dc::content`). It reads the game's data documents (`data/*.json`), validates them, and produces a `sim::Content` for the simulation. Per its CMake comment it is the only place a JSON library or the filesystem touches game data.

## Key Components
- `CMakeLists.txt` - `find_package(nlohmann_json CONFIG REQUIRED)`; `add_library(content STATIC src/loader.cpp)`; alias `dc::content`; public include dir `include`; links `PUBLIC dc::sim`, `PRIVATE nlohmann_json::nlohmann_json dc_warnings`.
- `include/` - public include root; `include/content/loader.h` is the whole public API (`content::loadFromStrings`, `content::loadFromDirectory`, `content::formatErrors`, `Sources`, `LoadResult`, `LoadError`, file name constants).
- `src/` - `src/loader.cpp`, the single translation unit with all parsing and validation.

## Dependencies
- `sim/` (`dc::sim`, PUBLIC): `sim/content.h`, `sim/commands.h`, `sim/fixed.h`, `sim/ids.h`, `sim/money.h`, `sim/types.h`. The loader fills `sim::Content` and uses `sim::Fixed`, `sim::Money`, `sim::noticeKindName` and `sim::rejectReasonName`.
- `nlohmann_json` (PRIVATE, via vcpkg); not visible to consumers.
- `dc_warnings` (PRIVATE interface target from `cmake/Warnings.cmake`).
- Runtime input: eight files in a data directory (the repo's `data/`): `resources.json`, `techs.json`, `regions.json`, `events.json`, `balance.json`, `ai.json`, `program.json`, `strings.json`.
- Consumers (link `dc::content`): `game/` (`deep_claim`, `deep_claim_layout_probe`), `game/session/` (`deep_claim_replay`), `sim_runner/` (`sim_runner`), `tests/` (`sim_tests`). Added to the build by the root `CMakeLists.txt` via `add_subdirectory(content)`.

## Patterns
- Boundary library: `sim/` has no JSON or filesystem dependency; `content/` converts text into the sim's plain data structs, and callers pass the resulting `sim::Content` on.
- Errors as values: every entry point returns `LoadResult` holding either a `sim::Content` or a list of `LoadError{file, path, message}`; all problems found in one load are reported together.
- Pure string entry point (`loadFromStrings`) plus a directory wrapper (`loadFromDirectory`).
- Strict, bounded schemas: unknown keys, out-of-range numbers, unresolved ids and missing prose keys are all load errors.

## Constraints
- All eight data files are required. `loadFromDirectory` reports `cannot read <path>` for each unreadable file and returns without parsing any document if at least one is missing.
- A `sim::Content` is returned only when there are zero errors; there is no partial or warning-only result.
- Depends only on `sim/` among the repo's targets; it does not depend on `game/` or `platform/`.
- The expected key set of `strings.json` is hard-coded in `src/loader.cpp` (`kStringKeys`) and partly derived from `sim::NoticeKind`, `sim::RejectReason` and the event and tech ids, so the catalogue, the table and the sim enums must agree for a load to succeed.
- The loader does not compute the content fingerprint: `sim::hashContent` (`sim/src/save.cpp`, field lists in `sim/include/sim/serialize.h`) hashes everything the loader fills except `content.strings`, i.e. resources (including `mine_opex_per_day`), techs (including `requires_program_stage`), regions, events, balance, ai and program. A prose edit leaves it unchanged, but event and tech names (filled from `strings.json`) are hashed, so a name edit or any other data change moves it and invalidates saves and traces.
