# game

**Purpose:** The playable Deep Claim client and the session layer beneath it. `game/session` wraps the deterministic sim for any client and is always built; the rest (SDL3 window, OpenGL, RmlUi interface) is the optional desktop client that draws that session and sends the player's orders.

## Key Components
- `CMakeLists.txt` - Client build, included only when `DC_BUILD_GAME=ON`. Defines `rmlui_backend` (static, from `backends/`), the executable `deep_claim` (`src/main.cpp`, `app.cpp`, `chart_element.cpp`, `screenshot.cpp`, `ui_model.cpp`), the always-run `deep_claim_assets` target that copies `data/` and `game/assets/` next to the binary, the executable `deep_claim_layout_probe` and the CTest `layout_probe`. Sets `DC_DATA_DIR`, `DC_ASSETS_DIR`, `DC_PLAYTEST_DIR` and `DC_BUILD_ID`.
- `session/` - Library `dc::game_session` (namespace `game`: `Session`, auto-pause, formatting and prose, playtest trace, replay, PNG encoder) and the headless tool `deep_claim_replay`. Has its own `CMakeLists.txt`, added unconditionally by the root.
- `src/` - Client sources: entry point, `App`, the `UiModel` view-model, the `<price-chart>` element, screenshots, and the headless layout probe.
- `assets/` - Fonts and the RmlUi document, stylesheets and icon sheet.
- `backends/` - Vendored RmlUi SDL + GL3 reference backend (MIT), with two marked local changes.

## Dependencies
- Other top-level directories: `sim` (`dc::sim`, via `game_session` and directly), `content` (`dc::content`), `platform` (`dc::platform`), `data/` at run time, `tools/design/` (scripts that generate `assets/ui/tokens.rcss` from `docs/design/tokens.css` and `assets/ui/icons.tga` with no input file), `playtest/` as the development trace directory.
- Third-party: SDL3 and RmlUi (`find_package` in `CMakeLists.txt`; vcpkg manifest feature `game`, enabled by the `debug-game` and `release-game` presets), nlohmann-json (`game/session` only), glad-generated OpenGL 3.3 loader (bundled in `backends/`).
- Link edges: `deep_claim` -> `dc::game_session`, `dc::content`, `dc::platform`, `rmlui_backend`, `RmlUi::RmlUi`, `RmlUi::Debugger`, `SDL3::SDL3`, `dc_warnings`. `deep_claim_layout_probe` -> the same without `rmlui_backend` and `SDL3::SDL3`. `rmlui_backend` -> `RmlUi::RmlUi`, `SDL3::SDL3`. `game_session` -> PUBLIC `dc::sim`, PRIVATE `nlohmann_json::nlohmann_json`.
- Incoming: `tests/` links `dc::game_session` and reads `game/assets/ui/tokens.rcss`.

## Patterns
- Three layers: sim (rules) -> `game/session` (a client's view of a running game, no UI toolkit) -> `game/src` (RmlUi presentation). Each layer depends only downward.
- Data-bound UI: RML/RCSS documents in `assets/ui` bound to a flat C++ view-model; one custom element for charts.
- Record and replay for playtests: the client writes a JSON Lines trace with per-day state hashes; `deep_claim_replay` rebuilds the game from it without the client's dependencies.
- Headless verification of the interface: `deep_claim_layout_probe` runs the real document with a stub renderer as a CTest.
- Assets located next to the executable in shipped builds, with compile-time repository paths as the development fallback.

## Constraints
- The client never contains game rules and never touches sim world state; it reads `sim::CompanyView` and notices and submits `sim::Command`s through `Session`.
- `game/session` must stay free of SDL and RmlUi so it builds in every preset and is unit-testable.
- The client targets exist only with `DC_BUILD_GAME=ON` (default OFF); `game/session` and `deep_claim_replay` exist in every configuration.
- `backends/` is third-party code: compiled with warnings off, excluded from clang-format and from the clang-tidy header filter (which covers `game/src` and `game/session`).
- Window opens at 1366x820; a Steam Deck layout applies below 1300dp width or with `--deck`.
- Single local save slot (`autosave.dcsave`) in the per-user directory from `SDL_GetPrefPath`; platform services are the null implementation from `platform::makeNullServices`.
