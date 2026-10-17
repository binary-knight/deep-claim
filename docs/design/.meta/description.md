# design

**Purpose:** The locked interface design of the game client: the record of the Design Studio session of 2026-10-09, the mockup bench it was judged on, the final design tokens, and the component hand-off that the client restyle (spec 003) implements. `tokens.css` is also a live input to a generator and a test. Since 2026-10-10 it also holds the two game-direction documents the owner approved: the Earth strategy and the core loop.

## Key Components
- `ui-design.md` - "Deep Claim interface design". Status: locked 2026-10-09 after four Design Studio sprints. Records intake, the banned "anti-slop register", the outcome of each sprint (material plate, information hierarchy, phrasing, spatial calibration), the codified constraints and the "inevitability gate" sign-off.
- `handoff.md` - "Deep Claim interface hand-off" (439 lines). The token block, the component inventory with exact CSS per component (header, status strip, rail, title block, panel, table, chip, button, input, rows, tiles, Steam Deck overrides), the per-screen block order, the component-state table and the RmlUi translation notes.
- `tokens.css` - 71 lines. One `:root` block of CSS custom properties: colours (normal and `--inv-*` inverse scheme), font families, `--fs-*` sizes, `--lh-*` line heights, weights, `--s-*` spacing, row, control and chip sizes, radius, borders, shadow. One declaration per line with a trailing comment. Same text as the token block in `handoff.md`.
- `earth-strategy.md` - "Earth strategy: what makes mining on Earth a game" (188 lines). Status: approved by the owner 2026-10-10. A diagnosis from three playtests, four pillars, a measures table and seven systems in order (1 markets that bite, spec 008; 2 mine economics; 3 land and regions; 4 capital; 5 contracts; 6 rivals; 7 something to aim for), ending in a dated Decisions list. Marked "Superseded in part, 2026-10-11" by `core-loop.md`, which reorders the systems; the diagnosis and pillars stand.
- `core-loop.md` - "Core loop: why the player plays" (168 lines). Status: direction approved by the owner 2026-10-11, after playtest 4. The owner's premise (a deadline, a race between companies to found a space program from what they produce), the loop in layers, what it changes in the existing plan, a proposed first slice (the basis of spec 010, `spec/race-for-the-program.md`) and the owner's Decisions, among them folding spec 009 into the slice and amending `DOMAIN.md`.
- `bench.html` - 1072 lines, title "Deep Claim Studio". Self-contained HTML page with inline CSS and JavaScript that renders the mockup screens (Dashboard, Plots, Market, Research, Finance) in desktop and Steam Deck frames, with toggles for the inverse scheme, a 14px ruler and a Tokens view. Retired directions (A, B, D; H1 to H3) remain in the file behind comments.

## Dependencies
- `tokens.css` is read by `tools/design/tokens_to_rcss.py`, which generates `game/assets/ui/tokens.rcss`, and by `tests/design_tokens_test.cpp` (Catch2), which checks that every token name and value appears in the generated file. `game/src/chart_element.cpp` cites two token values in a comment.
- `handoff.md` is cited as the layout source by `game/assets/ui/main.rcss`, `game/assets/ui/main.rml`, `game/src/ui_model.h` and `game/src/main.cpp` (comments only; nothing parses it).
- `bench.html` loads web fonts from `fonts.googleapis.com`; it has no other external resource and nothing in the repository reads it.
- `ui-design.md` refers to `docs/process.md` (prose pipeline), `docs/playtests/2026-10-08.md` (playtest lessons) and specs 002 and 003; `docs/specs/003-client-restyle.md` and the "Client restyle (spec 003)" section of `docs/DESIGN.md` refer back to this directory.
- `earth-strategy.md` draws on `docs/playtests/2026-10-10.md` and `docs/specs/006-balance.md` and is cited by `spec/markets-that-bite.md` and `spec/mine-economics.md` (systems 1 and 2). `core-loop.md` draws on `docs/playtests/2026-10-11.md` and is cited by `spec/race-for-the-program.md`, `docs/mvp/journeys/J-003-player-races-a-rival-to-found-the-program.md`, `DOMAIN.md`, `PROJECT.md` and `CLAUDE.md` ("Earth first"). No code reads either.

## Patterns
- Design Studio method: several polarised directions built on one bench with identical data and copy, rejected in rounds, with each rejection codified as a `[Constraint: ...]` line in `ui-design.md`.
- Design tokens as CSS custom properties in a single `:root` block, mirrored for RmlUi (which has no custom properties) as a generated lookup table plus utility classes.
- Plain browser stack in `bench.html`: HTML, CSS (flexbox and tables) and inline JavaScript that builds screens from template strings; no framework and no build step.
- `handoff.md` pairs every web CSS feature used in the mockup with its RmlUi 6 fallback in a table.
- The two direction documents are hard-wrapped Markdown prose with a `Status:` line under the title and the owner's Decisions at the end; a later document marks an earlier one superseded in part instead of rewriting it.

## Constraints
- `ui-design.md`: the design is locked; "Code follows this document and `handoff.md`; changes go through a new sprint, not ad hoc."
- `docs/process.md`: interface work has an extra phase before Build, a Design Studio session whose outcome is recorded in this directory; "Code follows the design document, never the other way round." Mockup copy is exempt from the prose polish pipeline until it becomes real copy.
- `tokens.css` and `game/assets/ui/tokens.rcss` must agree; `tests/design_tokens_test.cpp` fails otherwise and requires more than 40 tokens. The generator recognises one `--name: value;` declaration per line with lowercase names.
- Hard legibility constraints stated in `ui-design.md`: Steam Deck 1280x800, body text no smaller than 14px, buttons at least 40px tall, no horizontal scroll.
- `ui-design.md` (Sprint 1) and `handoff.md` name the bench `deep-claim-studio.html`; the tracked file is `bench.html` (named so in `ui-design.md` Sprint 4). No file called `deep-claim-studio.html` is tracked.
- `earth-strategy.md` and `core-loop.md` set direction, not specs: the first says each system still gets its own spec and the numbers belong there; the second says the first slice still gets its own spec, and its approval, before any build.
- `PROJECT.md` calls `handoff.md` historical. It predates spec 003 and differs from what was built in two stated places: it puts the status strip on the Dashboard only (spec 003 and `docs/DESIGN.md` say every tab), and it suggests generating `main.rcss` from `data/ui/tokens.json` (the built pipeline is `tokens.css` to `tokens.rcss`; there is no `data/ui/`).
