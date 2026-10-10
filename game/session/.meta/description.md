# session

**Purpose:** The UI-independent game session library (`game_session`, alias `dc::game_session`) and the headless `deep_claim_replay` tool. It is the boundary between the deterministic sim and any client: time control, the player's command queue, AI competitors, the notice log, text formatting, the playtest trace and its replay.

## Key Components
- `CMakeLists.txt` - Defines static library `game_session` from the seven files in `src/`, with `include/` as PUBLIC include directory, and the executable `deep_claim_replay` from `replay_main.cpp` with `DC_DATA_DIR` defined to `<repo>/data`.
- `include/` - Public include root; headers live in `include/game/` (`session.h`, `auto_pause.h`, `format.h`, `trace.h`, `replay.h`, `png.h`).
- `src/` - Implementation: `session.cpp`, `auto_pause.cpp`, `format.cpp`, `text.cpp`, `trace.cpp`, `replay.cpp`, `png.cpp`.
- `replay_main.cpp` - `deep_claim_replay <trace.jsonl> [--day N] [--save out.dcsave] [--data dir] [--log]`: loads content, replays the trace, reports the first day whose state hash differs from the recorded one, prints the player's view (plots, surveys, mines, markets, finance and research) at the stop day, and can write a save. Save files named by the trace are read from the trace's directory.

## Dependencies
- `game_session` links PUBLIC `dc::sim`; PRIVATE `nlohmann_json::nlohmann_json` and `dc_warnings`.
- `deep_claim_replay` links PRIVATE `dc::game_session`, `dc::content` (for `content/loader.h`) and `dc_warnings`.
- Consumers: `game/CMakeLists.txt` targets `deep_claim` and `deep_claim_layout_probe`; `tests/CMakeLists.txt` target `sim_tests`.
- Added by the root `CMakeLists.txt` with `add_subdirectory(game/session)` unconditionally, independent of `DC_BUILD_GAME`.

## Patterns
- Library plus thin CLI: all replay logic is in the library (`game/replay.h`); `replay_main.cpp` only parses arguments and prints.
- Facade over the sim: clients hold a `Session` and see only `sim::CompanyView`, `sim::Notice` and `sim::Command`.
- Record and replay: the client writes a JSON Lines trace as it runs; replay drives the same `Session` class with the recorded seed and orders.
- Namespace `game`; headers under `include/game/` mirror it.

## Constraints
- No SDL or RmlUi here, so the library builds in every preset and can be unit tested (stated in `CMakeLists.txt`).
- The session never contains game rules; those stay in `sim`. It decides only presentation concerns: speed, which notices to log or pause on, and wording.
- The player is always company 0 (`Session::kPlayer`); `Session::submit` rebinds every command to it.
- Replay needs the same data files the trace was recorded with (content hash check).
- Covered by clang-format and by the `.clang-tidy` `HeaderFilterRegex` (`game/session`).
