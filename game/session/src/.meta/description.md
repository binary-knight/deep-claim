# src (game_session implementation)

**Purpose:** Implementation files of the `game_session` static library. They wrap the deterministic sim for a client: ticking, logging, text, the playtest trace and its replay, with no window or UI toolkit code.

## Key Components
- `session.cpp` - `Session`. `advance` accumulates real seconds into days at the current speed, caps catch-up at twice the per-second day rate per call, and stops on an auto-pause notice or when the game is over (bankruptcy, or the race founded or its deadline passed). `submit`/`cancel`/`cancelAll` keep the pending-order queue and trace each change as a `ui` record (`order_queued`, `order_cancelled`, with the order id); `giveOrders` hands the queue to the sim at the start of `tickOnce` and traces each as a `command`. `orderCost` (a `std::visit` over every command type, saturating arithmetic), `committed` and `pendingBorrowed` sum the queue for display. `save` saves a copy of the sim that has been given the queue. `tickOnce` does nothing once the game is over; otherwise it gives the orders, collects AI commands for companies 1..n via `sim::Simulation::collectCommands`, ticks, refreshes the player view, appends cash history (or corrects the last point on the day the game ends, since the world freezes on it), writes trace `day` records and a monthly `state` record, and sets the game-ending notice as the pause reason. `logNotices`/`showsInLog` filter notices to those the player has a stake in and fold a month's automatic sales into one line. `load` clears the queue, resets the log to a single "Game loaded" entry and pauses.
- `auto_pause.cpp` - `pauseOnKey` (eleven keys, ending `mine_losing`, `rival_program`), `AutoPause::defaults` (all on except mine operational), `reasonFor` (maps a `sim::Notice` to a `PauseOn`; `MineLosingMoney` for the player's own mine, `ProgramResearchStarted`/`ProgramStageComplete` for another company's), `shouldPause`, `toText`, `fromText`.
- `format.cpp` - Number formatting and parsing declared in `game/format.h`: `formatMoney`, `formatDay`, `formatTonnes`, `formatRatio`, `parseMoney` (accepts `$`, sign, thousands separators, decimals and k/M/B suffixes, exact to the cent), `parseTonnes`, `exactMoney`, `exactPrice`.
- `text.cpp` - The prose-catalogue half of `game/format.h` (it has no header of its own): `fill`, `text`, `textPlural`, `rejectReasonText`, `rejectReasonClause`, `capitalised`, `describeNotice`, `describeAutoSales`. Chooses keys in `data/strings.json` and supplies figures and names; rejected program orders and orders refused after the end (`RejectReason::GameEnded`) have their own keys.
- `trace.cpp` - `TraceWriter` methods, command to/from JSON (`encodeCommand`, `decodeCommand`, one decoder per command type: twelve, including `DeliverToProgram` and `CompleteProgramStage`), the end record's outcome (`playerResultWord`, `parseOutcome`), and `parseTraceLine`. The only file that includes `<nlohmann/json.hpp>`.
- `replay.cpp` - `replayTrace`, implemented by a file-local `Replayer` class that handles `new_game`, `load`, `command` and `day` records and treats the rest as informational. It stops at the end of the game; the tick that ends a game is recorded with the same day number as the day before.
- `png.cpp` - `encodePngRgb`: CRC-32 table for chunk checksums, a zlib stream of stored deflate blocks (`zlibStored`), filter type 0 on every row, and chunk writing (`putChunk`).

## Dependencies
- `dc::sim` headers: `sim/simulation.h`, `sim/commands.h`, `sim/content.h`, `sim/view.h`, `sim/ids.h`, `sim/money.h`, `sim/fixed.h`, `sim/types.h`, `sim/save.h`, `sim/world_state.h`.
- `nlohmann_json::nlohmann_json` (PRIVATE link; `trace.cpp` only).
- Own headers under `game/session/include/game/`.
- C++20 standard library (`<format>`, `<span>`, `<charconv>`, `<variant>`).

## Patterns
- Pure, locale-free free functions for text and numbers; state lives only in `Session`.
- Anonymous-namespace helpers per file; no cross-file private headers.
- JSON Lines for the trace: 64-bit seeds travel as decimal strings and hashes as hex strings because JSON readers lose precision above 2^53.
- Replay validates before mutating: a save is loaded into a scratch `sim::WorldState` and its hash compared with the recorded one before `Session::load` is called.
- Every sentence shown to the player comes from the content's string catalogue; missing keys render as `[key]` and unknown placeholders stay visible as `{name}`.

## Constraints
- No SDL or RmlUi includes; the library must build in every preset and be unit-testable.
- Determinism contract: the same content, seed and recorded commands must reproduce the recorded per-day state hashes; replay reports the first day that differs.
- Replay refuses a trace whose `new_game` content hash differs from `sim::hashContent` of the loaded data.
- A `command` record must carry the day the replay is currently on, and `day` records must be consecutive (or repeat the last day for the tick that ended the game); otherwise replay stops with a line-numbered error.
- Pending-order sums never use `sim::Money`'s checked arithmetic (which aborts on overflow); they saturate, because an order may carry any amount until the sim rejects it.
- An end record's outcome, when present, must be whole and carry a known result word, or `parseTraceLine` fails with "bad outcome"; traces from before spec 010 have none.
- `png.cpp` keeps the single IDAT chunk under the PNG chunk size limit (2^31 - 1 bytes).
- `trace.cpp` is 739 lines and `session.cpp` 480; `TraceWriter::state` is the longest function here.
