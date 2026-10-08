# Deep Claim design decisions

Short records of decisions and why. Newest at the bottom.

## Architecture split

`sim/` is a pure C++20 static library with every rule and all state, and no
third-party dependencies. `content/` (step 3) parses JSON into the sim's plain
content structs so the sim never sees a JSON library or a file. `sim_runner/`
is a headless CLI for balance testing. `game/` (step 15) is SDL3 + RmlUi and
only forwards player commands. `platform/` will hold a Steam-shaped interface
with a null implementation.

## Dependency manager: vcpkg manifest mode

vcpkg is a git submodule at `external/vcpkg`, pinned by `builtin-baseline`.
Reasons: the RmlUi port wires up FreeType, binary caching (vcpkg `files` backend
persisted with actions/cache) keeps Windows CI fast, and the sim-only side needs only Catch2.
Game-client dependencies live behind the `game` manifest feature so CI does
not build SDL3 and RmlUi until the client exists.

## Determinism rules

- 1 tick = 1 in-game day. Calendar is 12 months of 30 days (360-day year), so
  monthly costs fall on fixed day boundaries and tests stay simple.
- Own RNG (PCG32) with one stream per system. Standard library distributions
  differ between libstdc++ and MSVC.
- Money is int64 cents, quantities are integer tonnes, ratios are fixed-point.
  No floating point in sim state, so state hashes match across compilers.
- State hash is FNV-1a over a canonical little-endian byte stream, never
  `std::hash`. A golden hash recorded on Linux/GCC is checked on Windows/MSVC
  in CI.
- No iteration over `std::unordered_map` during a tick.
- Tick pipeline, fixed order: apply commands (sorted by company id, then
  submission order) -> resolve auctions -> advance surveys -> mines extract ->
  update markets -> month-boundary finance and bankruptcy -> research ->
  events -> advance day and emit tick report.

## Hidden truth vs observable view

True deposits live only in `WorldState::hidden`. A view function builds a
per-company view (public plot data, that company's surveys, market history,
own finances) and never reads `hidden`. AI controllers receive only the view.

## Sanitizers

The asan preset enables ASan and UBSan and builds the sim stack only, because
SDL and font libraries produce leak noise. MSVC has no UBSan, so the sanitizer
CI leg runs on Ubuntu only; Windows CI builds debug and release.

## Tooling

Catch2 v3 for tests. clang-format (Google base, 4-space indent, 100 columns)
and clang-tidy are enforced in CI. Warnings are errors by default
(`DC_WARNINGS_AS_ERRORS`).
