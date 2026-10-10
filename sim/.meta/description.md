# sim

**Purpose:** The pure, deterministic simulation core of Deep Claim: all game rules and all dynamic game state (plots, auctions, surveys, mines, commodity markets, loans, research, world events, the built-in AI competitor) plus save/load and state hashing. It is a static library with no I/O beyond writing a fatal message to stderr and aborting on invariant violations.

## Key Components
- `CMakeLists.txt` — defines static library `sim` (alias `dc::sim`) from the 21 files in `src/`, exports `include/` as its PUBLIC include directory, links only `dc_warnings` (PRIVATE).
- `include/` — public include root; contains the single directory `sim/` so consumers write `#include "sim/<name>.h"`.
- `src/` — the 21 `.cpp` implementation files (tick pipeline, per-system rules, AI controller, views, world generation, save/validation, fixed-point arithmetic, RNG).

## Dependencies
- Outgoing: C++ standard library only, and the `dc_warnings` INTERFACE target from `cmake/Warnings.cmake` (compiler flags). No includes from `content/`, `platform/`, `game/`, `sim_runner/` or `tests/`; no nlohmann-json, SDL3, RmlUi, Catch2 or filesystem headers.
- One platform-specific spot: `src/fixed.cpp` uses `__int128` (GCC/Clang) or `<intrin.h>` `_umul128`/`_udiv128` (MSVC x64) and `#error`s elsewhere.
- Incoming (who links `dc::sim`): `content` (PUBLIC; fills `sim::Content` from `data/*.json`), `game_session` (PUBLIC), `sim_runner`, `sim_tests` in `tests/`. The `deep_claim` client in `game/` receives it transitively through `dc::game_session` and `dc::content`.

## Patterns
- Contract with hosts: construct `sim::Simulation(content, seed, setup)`; feed it `sim::Command` values via `submit` or `collectCommands(company, controller)`; call `tick()` once per in-game day; read back `CompanyView` (`view(company)`) and per-viewer notices (`reportFor(company)`). Hosts never mutate `WorldState` directly.
- Inputs are plain structs: `sim::Content` (static data, built by the `content` library) and commands. Outputs are plain structs: `CompanyView`, `TickReport`/`Notice`, save bytes, a 64-bit state hash.
- Fixed tick pipeline in `src/simulation.cpp`: commands, plot market, surveys, mines, sell policies, commodity markets, finance, research, events, day increment.
- Each rule system is a header/source pair exposing `validateX` / `applyX` for its commands and a `runX` daily step.
- Information hiding: true deposits live in `WorldState::hidden`; `view.cpp` builds per-company views without reading it; notices are public or private per `NoticeKind`.
- One `serialize()` visitor per type (`archive.h`, `serialize.h`) drives hashing, saving and loading.
- All state arithmetic is integer: `Fixed` (6 decimal digits), `Money` (integer cents), local `Pcg32` RNG with five named streams.

## Constraints
- `CMakeLists.txt` states the library "must never depend on SDL, RmlUi, JSON, the filesystem, or Steam".
- Determinism: same content + seed + command log must give the same state hash on every compiler and OS. No floating point in sim state (`Fixed::fromDouble` is for the content-loading boundary only), no `<random>` distributions, no `std::hash` for state hashes.
- Commands are applied at the start of the next tick in (company id, submission order); validation failures become `CommandRejected` notices, never errors.
- Entities are stored in vectors indexed by `StrongId` and never erased or reused; dead entities carry a flag.
- Save format: magic `DCSV`, `kSaveVersion` (currently 1), content fingerprint, serialized `WorldState`, queued commands. A save loads only against content with an identical hash; `data/strings.json` prose is excluded from that hash.
- Overflow and invariant violations call `sim::fatal`, which aborts in every build type.
- Game length is bounded by `kMaxDay` (10,000 in-game years of 360 days); ticking past it aborts.
