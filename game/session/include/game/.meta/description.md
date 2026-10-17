# game (session public headers)

**Purpose:** The public API of the `game_session` library, namespace `game`: a running game as a client sees it, plus the text formatting, playtest trace, replay, auto-pause rules and PNG encoding that go with it. Included as `"game/<name>.h"`.

## Key Components
- `session.h` - `class Session`: owns a `sim::Simulation` and a `sim::HeuristicController` for the rivals; time control (`Speed`, `daysPerSecond` = 0/1/5/20, `advance`, `stepOneDay`, `togglePause`); the player's order queue (spec 007): `submit` queues a `PendingOrder` (id plus command, always bound to `kPlayer` = company 0) that reaches the sim at the start of the next tick, `pendingOrders`, `cancel`, `cancelAll`, and the display-only sums `committed`, `pendingBorrowed`, `orderCost`; the player's `sim::CompanyView`, `gameOver` (bankrupt or `view.outcome.ended`) and `outcome` (`sim::OutcomeView`, spec 010); the notice log (`LogEntry`, capped at `kMaxLogEntries` = 500), `priceHistory`, `cashHistory`, `save`/`load`, `setAutoPause`/`takePausedBy`, `setTrace`, `stateHash`.
- `auto_pause.h` - `enum class PauseOn` (eleven notice-driven reasons, including `MineLosing` from spec 008 and `RivalProgram` from spec 010) and `struct AutoPause`: which notices stop the clock, `defaults()`, `reasonFor`, `shouldPause`, and the `key=0/1` text form (`toText`, `fromText`).
- `format.h` - Locale-free formatters (`formatMoney`, `formatDay`, `formatTonnes`, `formatRatio`, `exactMoney`, `exactPrice` for an unabbreviated per-tonne price), strict input parsers (`parseMoney`, `parseTonnes`), and the prose-catalogue functions (`fill`, `text`, `textPlural`, `rejectReasonText`, `rejectReasonClause`, `capitalised`, `describeNotice`, `describeAutoSales`). Implemented across `src/format.cpp` and `src/text.cpp`.
- `trace.h` - `class TraceWriter` (one JSON object per line, flushed per record; record types new_game, load, save, command, day, notice, state, ui, marker, screenshot, end; `end(day, view)` adds the outcome when the game is over, and only one end record is written per game), `struct TraceOutcome` (ended, day, founder, result word "founded"/"lesser_partner"/"lost"/"bankrupt"), `struct TraceRecord`, `encodeCommand`/`decodeCommand`, `parseTraceLine`, `kTraceVersion` = 1.
- `replay.h` - `replayTrace(content, istream, ReplayOptions)` returning `ReplayResult` (the rebuilt `Session`, counts, `firstDivergence` day, `error`).
- `png.h` - `encodePngRgb(width, height, rgb)`: 8-bit RGB PNG using stored deflate blocks.

## Dependencies
- `sim` headers only: `sim/commands.h`, `sim/content.h`, `sim/ids.h`, `sim/money.h`, `sim/simulation.h`, `sim/types.h`, `sim/view.h` (including `sim::OutcomeView`), `sim/ai_controller.h`.
- The C++ standard library. No SDL, RmlUi or nlohmann-json type appears in these headers.
- Included by `game/src/*` (the client), and `game/session/replay_main.cpp`; available to `tests/` because `sim_tests` links `dc::game_session`.

## Patterns
- Every header opens with a comment block stating its role and contract.
- `[[nodiscard]]` on query and parse functions; parse functions return `bool` with out-parameters rather than throwing.
- `Session` holds non-owning pointers (`const sim::Content*`, `TraceWriter*`); the host owns content and trace.
- Strongly typed sim ids and `sim::Money` throughout; no raw world-state access is exposed.

## Constraints
- The UI reads views and the log and sends commands; it never touches the sim's world state (stated in `session.h`).
- `TraceWriter`'s `std::ostream` must outlive the writer; a null trace turns tracing off.
- `AutoPause::fromText` ignores unknown keys and keeps current values for missing keys; it returns false only for a malformed line.
- `cashHistory` is session memory, not sim state: a load restarts it from the loaded day's cash.
- Pending orders live in the session, not the sim, until the next tick; `save` writes them into the save as the current day's orders while keeping them queued, and a successful `load` discards them. `committed`/`orderCost` are for display and button state only (R-001).
- Once `gameOver()` is true the clock stays stopped (`setSpeed` forces Paused), `submit` returns false and queues nothing, and no further day, log line, history point or trace record is added.
- `encodePngRgb` returns an empty vector for invalid dimensions or a short buffer.
