# src (game client)

**Purpose:** Source of the playable client `deep_claim` and of the headless `deep_claim_layout_probe`. It hosts an RmlUi context, turns a `game::Session` into bindable strings, and turns interface events into `sim::Command`s; it contains no game rules.

## Key Components
- `main.cpp` - Entry point of `deep_claim`. Parses `--deck`; locates `data/` and `assets/` next to the executable (`SDL_GetBasePath`) or falls back to `DC_DATA_DIR`/`DC_ASSETS_DIR`; loads content with `content::loadFromDirectory` before opening the window; `Backend::Initialize("Deep Claim", 1366, 820, true)`; loads the seven font faces; registers `<price-chart>`; builds `platform::makeNullServices` on `SDL_GetPrefPath("DeepClaim", "DeepClaim")`; resolves the playtest directory (`DEEP_CLAIM_PLAYTEST` env var, else `DC_PLAYTEST_DIR`, else `<prefPath>/playtest`); runs the frame loop. `keyDown` routes F8 to the RmlUi debugger and F9/F12 and unconsumed keys to `App::onKey`; `preSwap` services screenshot requests.
- `app.h` / `app.cpp` - `class App`: owns the `Session`, the `UiModel`, the RmlUi data model "game" and the document. `bindModel` registers every struct, variable and event callback used by `main.rml`. `update` advances the session, shows the auto-pause reason, expires the status line after 6 s, toggles the deck class below 1300dp and refreshes on a new day. The `on*` handlers parse the text inputs with `parseMoney`/`parseTonnes` and submit commands. Also: save/load of `autosave.dcsave` (written to `.tmp` then renamed), auto-pause rules in `autopause.txt`, the playtest trace (`trace-YYYYMMDD-HHMMSS.jsonl`, save copies, up to 200 screenshots), keyboard shortcuts (space, 1/2/3, period, F9, F12), and public hooks used only by the layout probe (`setTab`, `startGame`, `selectPlot`, `showPauseMenu`, `setLogFilter`, `refreshNow`, `session`).
- `ui_model.h` / `ui_model.cpp` - `struct UiModel` and its row structs (`NeedRow`, `SaleRow`, `SoldRow`, `OwnedRow`, `EstimateRow`, `PlotPanel`, `ResourceRow`, `TechRow`, `LoanRow`, `LogRow`, `PauseRow`): flat, pre-formatted strings and flags. `refreshModel(session, model)` rebuilds everything through file-local `fill*` functions (`fillStrip`, `fillNeeds`, `fillOwned`, `fillFinanceRow`, `fillSale`, `fillRecentSales`, `fillPanel`, `fillResources`, `fillTechs`, `fillFinance`, `fillLog`, `fillPauseRows`).
- `chart_element.h` / `chart_element.cpp` - `PriceChartElement`, the custom RmlUi element `<price-chart resource days mode series>` drawn with `Rml::Geometry` (line sparkline with area, dashed baseline and endpoint mark, or absolute columns). Data comes from static `ChartProvider`/`CashProvider` callbacks installed by `App`; `registerChartElement` installs the instancer.
- `screenshot.h` / `screenshot.cpp` - `captureFramebufferPng`: `glReadPixels` of the back buffer, row flip, `game::encodePngRgb`, write to file. Compiled into `deep_claim` only.
- `layout_probe.cpp` - Entry point of `deep_claim_layout_probe`, registered as CTest `layout_probe`. Runs the real `App` and `main.rml` with a stub `Rml::RenderInterface` and a logging `Rml::SystemInterface`, lays out every tab at 1366x820 and 1280x800 on a fresh game and on fixed-seed scripted games (`kScenarioSeed` = 20261009), and exits non-zero on layout faults, RmlUi warnings, scenario failures or a missing trace header.

## Dependencies
- `dc::game_session` (`game/session.h`, `game/format.h`, `game/trace.h`, `game/auto_pause.h`, `game/png.h`).
- `dc::sim` headers directly (`sim/commands.h`, `sim/content.h`, `sim/view.h`, `sim/save.h`, `sim/money.h`, `sim/ids.h`, `sim/fixed.h`, `sim/types.h`).
- `dc::content` (`content/loader.h`) and `dc::platform` (`platform/services.h`).
- RmlUi (`RmlUi::RmlUi`, `RmlUi::Debugger`), SDL3 (`main.cpp` only), and the vendored `rmlui_backend` (`RmlUi_Backend.h` in `main.cpp`, `RmlUi_Include_GL3.h` in `screenshot.cpp`).
- Runtime files: `game/assets/ui/main.rml`, `game/assets/fonts/*.ttf`, `data/` (including `data/strings.json`).

## Patterns
- View-model: `UiModel` is a snapshot rebuilt from `Session::view()` on each refresh, followed by `DirtyAllVariables`; the RML never touches sim types.
- RmlUi data binding with `BindEventCallback` lambdas that forward to `App::on*` member functions.
- Global function-pointer bridge: the backend callbacks are plain function pointers, so `main.cpp` reaches the app through a file-static `gApp`.
- Provider callbacks decouple the chart element from the session type.
- Screenshots are a request/acknowledge handshake: `App` queues a path, the host captures in the pre-swap callback and calls `screenshotDone`.
- All player-visible sentences come from `game::text(content, key, ...)`; C++ holds keys, not prose.
- `NOLINT` comments with a reason where clang-tidy checks are suppressed.

## Constraints
- No game rules here (stated in `game/CMakeLists.txt`); state changes only through `Session::submit`, `Session::load` and new games.
- `deep_claim_layout_probe` compiles `app.cpp`, `ui_model.cpp` and `chart_element.cpp` without SDL or the backend, so those three files must not include SDL or GL headers.
- `kDeckWidth` (1300) is defined separately in `app.cpp` and `layout_probe.cpp`; the font list is duplicated in `main.cpp` and `layout_probe.cpp`.
- `App` is non-copyable and non-movable; it must be destroyed before `Rml::Shutdown`.
- A new game uses a clock-derived seed and one AI rival named "Northfield Minerals".
- The trace is optional: an empty playtest directory disables trace, save copies and screenshots.
- Large files: `ui_model.cpp` 1360 lines, `app.cpp` 1046, `layout_probe.cpp` 647; `App::bindModel` is about 320 lines.
