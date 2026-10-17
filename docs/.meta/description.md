# docs

**Purpose:** The written record of how Deep Claim is designed and built: the running log of architectural decisions, the development process, the feature specs 001 to 007 (later specs are in the top-level `spec/`), per-feature decision records, player journeys, the game's direction documents, the locked interface design and the playtest write-ups. Non-code, except that `design/tokens.css` is an input to a generator and a test.

## Key Components
- `DESIGN.md` - "Deep Claim design decisions" (1776 lines). Short records of decisions and reasons, newest at the bottom: architecture split, dependency manager, determinism rules, hidden truth versus view, sanitizers, tooling, then sections keyed to build steps 2 to 18 (some grouped), monitored playtesting, spec-driven development, and one section per built spec, 001 to 008 and then "The race for the program (spec 010, step 1)" (tick order, the outcome and a finished world, three golden hashes), including each golden-hash change and the sweep result tables. The spec 008 and 010 sections name their spec in `spec/` and their records in `adrs/`.
- `process.md` - "How Deep Claim is built: spec-driven, phased" (88 lines). The six phases with their outputs and gates, the spec template, the "Rules of the road", and the two-hand prose rule (drafted, then polished by the agy CLI).
- `specs/` - feature specs `001` to `007` (`NNN-name.md`) and `001-prose/`, the archived draft and polished strings of spec 001. Specs from 008 on are in the top-level `spec/` (`PROJECT.md`).
- `design/` - the locked interface design (`ui-design.md`, `handoff.md`, `tokens.css`, `bench.html`) and the owner-approved direction documents `earth-strategy.md` (2026-10-10) and `core-loop.md` (the premise and core loop, 2026-10-11).
- `playtests/` - four dated write-ups of monitored test plays (2026-10-08 to 2026-10-11; the last is playtest 4, the first on specs 007 and 008 and the first on Windows).
- `adrs/` - `README.md` and twelve decision records named `F-<date>-<feature>-NNN-<slug>.md`, four each for specs 008, 009 and 010.
- `mvp/` - holds only `journeys/`: player journeys J-001 to J-003, served by specs 008, 009 and 010.
- `contexts/`, `guides/`, `invariants/`, `plans/`, `prds/`, `sources/`, `standards/` - etc skeleton directories, each holding only a `README.md` that states what belongs there; no content yet.

## Dependencies
- Consumed by code: `design/tokens.css` is read by `tools/design/tokens_to_rcss.py` (output `game/assets/ui/tokens.rcss`) and by `tests/design_tokens_test.cpp`.
- Cited in comments, not parsed: `DESIGN.md` by `README.md`, `cmake/Sanitizers.cmake`, `sim/include/sim/worldgen.h`, `sim/include/sim/content.h`, `sim/include/sim/markets.h`, `sim/src/simulation.cpp` and `tests/determinism_test.cpp` (its comments say a golden-hash move is recorded there); `design/handoff.md` by `game/assets/ui/main.rcss`, `game/assets/ui/main.rml`, `game/src/ui_model.h` and `game/src/main.cpp`; specs 005 and 006 by `sim_runner/main.cpp`; spec 002 by `sim/include/sim/content.h` and `tools/prose/README.md`. Specs 008 and 010 (in `spec/`) are cited by number across `sim/`, `content/`, `game/`, `sim_runner/` and `tests/`; the runner's one path citation for spec 008 names `docs/specs/008-markets-that-bite.md`, which does not exist.
- The documents describe and point into every other top-level directory (`sim/`, `content/`, `sim_runner/`, `game/`, `platform/`, `data/`, `tests/`, `tools/`) and name the external tools used in the process: the Codex CLI for review and the agy CLI through `tools/prose/polish.sh`.
- `.github/workflows/ci.yml` and the CMake files do not reference `docs/`.

## Patterns
- Markdown throughout, hard-wrapped (the etc-written `adrs/` records use one long line per field); YAML front matter in `mvp/journeys/`; plain CSS and one self-contained HTML bench under `design/`; TSV under `specs/001-prose/`.
- Kinds of record kept apart: `DESIGN.md` for architecture, `adrs/` for per-feature architecture decisions, `specs/` (and `spec/`) for features, `playtests/` for what players did, `mvp/journeys/` for what a player should be able to do, `design/` for interface and direction.
- Append-only logs: `DESIGN.md` adds sections at the bottom and each spec appends to its dated Decisions list; earlier entries are not rewritten when a later one changes the numbers, but since spec 007 an earlier `DESIGN.md` entry may gain a short added note, such as "(Since spec 008 ...)", pointing to the later section. Accepted ADRs are amended by a new record, not edited.
- Playtests 1 to 3 end in a proposed order of work that names the next specs; after playtest 4 the order came from `design/core-loop.md`. Specs 001 to 007 name the playtest finding they answer; specs from 008 on name a journey and a direction document.

## Constraints
- `process.md`: every change beyond a bug fix goes through six phases in order (Spec, Plan, Build, Review, Verify, Playtest), each with a gate; interface work adds a Design Studio phase before Build, recorded in `docs/design/`.
- `process.md`: specs are `docs/specs/NNN-name.md`, numbered in the order opened, first `001-sell-policies`, and follow the template (Goal, Player-facing behaviour, Sim rules, View, Client, Tests, Out of scope, Decisions). The "Spec-driven development" section of `DESIGN.md` says the same. Both diverge from current practice: `PROJECT.md` puts specs from 008 on in `spec/<slug>.md`, and those files follow the etc PRD format instead of this template.
- `process.md`: Codex review findings are applied or declined with a reason in the spec's Decisions; balance numbers go in `data/*.json` with their reasoning in the spec.
- `process.md`: `docs/DESIGN.md` is the running record of architectural decisions; any change to sim state updates the golden hash once, with the reason written in `docs/DESIGN.md`.
- `process.md`: every player-facing sentence is drafted, polished by `tools/prose/polish.sh`, and checked for unchanged numbers, units, names and placeholders before it lands in `data/strings.json`; mockup copy is exempt.
- `DESIGN.md` ("Spec-driven development"): the spec is committed before the implementation, and decisions made during implementation are appended to the spec while `DESIGN.md` keeps the architectural summary. ("Monitored playtesting"): playtests are written up one file per session under `docs/playtests/`.
- `design/ui-design.md`: the interface design is locked; changes go through a new sprint.
