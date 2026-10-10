# docs

**Purpose:** The written record of how Deep Claim is designed and built: the running log of architectural decisions, the development process, the numbered feature specs, the locked interface design and the playtest write-ups. Non-code, except that `design/tokens.css` is an input to a generator and a test.

## Key Components
- `DESIGN.md` - "Deep Claim design decisions" (910 lines). Short records of decisions and reasons, newest at the bottom: architecture split, dependency manager, determinism rules, hidden truth versus view, sanitizers, tooling, then sections keyed to build steps 2 to 18 (some grouped), monitored playtesting, spec-driven development, and one section per built spec (001 to 006) including each golden-hash change and the sweep result tables.
- `process.md` - "How Deep Claim is built: spec-driven, phased" (88 lines). The six phases with their outputs and gates, the spec template, the "Rules of the road", and the two-hand prose rule (drafted, then polished by the agy CLI).
- `HANDOFF.md` - "Handoff: 2026-10-10" (81 lines). A session-to-session note: where things stand, direction from the owner, working agreements, loose ends, sandbox quirks. States of itself "Delete this file when it has been absorbed".
- `specs/` - feature specs `001` to `007` (`NNN-name.md`) and `001-prose/`, the archived draft and polished strings of spec 001.
- `design/` - the locked interface design: `ui-design.md`, `handoff.md`, `tokens.css`, `bench.html`.
- `playtests/` - three dated write-ups of monitored test plays (2026-10-08, 2026-10-09, 2026-10-10).

## Dependencies
- Consumed by code: `design/tokens.css` is read by `tools/design/tokens_to_rcss.py` (output `game/assets/ui/tokens.rcss`) and by `tests/design_tokens_test.cpp`.
- Cited in comments, not parsed: `DESIGN.md` by `README.md`, `cmake/Sanitizers.cmake`, `sim/include/sim/worldgen.h`, `sim/include/sim/content.h`, `sim/include/sim/markets.h` and `sim/src/simulation.cpp`; `design/handoff.md` by `game/assets/ui/main.rcss`, `game/assets/ui/main.rml`, `game/src/ui_model.h` and `game/src/main.cpp`; specs 005 and 006 by `sim_runner/main.cpp`; spec 002 by `sim/include/sim/content.h` and `tools/prose/README.md`.
- The documents describe and point into every other top-level directory (`sim/`, `content/`, `sim_runner/`, `game/`, `platform/`, `data/`, `tests/`, `tools/`) and name the external tools used in the process: the Codex CLI for review and the agy CLI through `tools/prose/polish.sh`.
- `.github/workflows/ci.yml` and the CMake files do not reference `docs/`.

## Patterns
- Markdown throughout, hard-wrapped; plain CSS and one self-contained HTML bench under `design/`; TSV under `specs/001-prose/`.
- Three kinds of record, kept apart: `DESIGN.md` for architecture, `specs/` for features, `playtests/` for what players did.
- Append-only logs: `DESIGN.md` adds sections at the bottom and each spec appends to its dated Decisions list; earlier entries are left as written even when a later one changes the numbers.
- Each playtest ends in a proposed order of work that names the next specs; each spec names the playtest finding it answers.

## Constraints
- `process.md`: every change beyond a bug fix goes through six phases in order (Spec, Plan, Build, Review, Verify, Playtest), each with a gate; interface work adds a Design Studio phase before Build, recorded in `docs/design/`.
- `process.md`: specs are `docs/specs/NNN-name.md`, numbered in the order opened, first `001-sell-policies`, and follow the template (Goal, Player-facing behaviour, Sim rules, View, Client, Tests, Out of scope, Decisions).
- `process.md`: Codex review findings are applied or declined with a reason in the spec's Decisions; balance numbers go in `data/*.json` with their reasoning in the spec.
- `process.md`: `docs/DESIGN.md` is the running record of architectural decisions; any change to sim state updates the golden hash once, with the reason written in `docs/DESIGN.md`.
- `process.md`: every player-facing sentence is drafted, polished by `tools/prose/polish.sh`, and checked for unchanged numbers, units, names and placeholders before it lands in `data/strings.json`; mockup copy is exempt.
- `DESIGN.md` ("Spec-driven development"): the spec is committed before the implementation, and decisions made during implementation are appended to the spec while `DESIGN.md` keeps the architectural summary. ("Monitored playtesting"): playtests are written up one file per session under `docs/playtests/`.
- `design/ui-design.md`: the interface design is locked; changes go through a new sprint.
- `HANDOFF.md` is a point-in-time note and is behind the repository on one point: it says spec 007 is drafted and not approved, while `specs/007-pending-orders.md` says approved 2026-10-10.
