# design

**Purpose:** The locked interface design of the game client: the record of the Design Studio session of 2026-10-09, the mockup bench it was judged on, the final design tokens, and the component hand-off that the client restyle (spec 003) implements. `tokens.css` is also a live input to a generator and a test.

## Key Components
- `ui-design.md` - "Deep Claim interface design". Status: locked 2026-10-09 after four Design Studio sprints. Records intake, the banned "anti-slop register", the outcome of each sprint (material plate, information hierarchy, phrasing, spatial calibration), the codified constraints and the "inevitability gate" sign-off.
- `handoff.md` - "Deep Claim interface hand-off" (439 lines). The token block, the component inventory with exact CSS per component (header, status strip, rail, title block, panel, table, chip, button, input, rows, tiles, Steam Deck overrides), the per-screen block order, the component-state table and the RmlUi translation notes.
- `tokens.css` - 71 lines. One `:root` block of CSS custom properties: colours (normal and `--inv-*` inverse scheme), font families, `--fs-*` sizes, `--lh-*` line heights, weights, `--s-*` spacing, row, control and chip sizes, radius, borders, shadow. One declaration per line with a trailing comment. Same text as the token block in `handoff.md`.
- `bench.html` - 1072 lines, title "Deep Claim Studio". Self-contained HTML page with inline CSS and JavaScript that renders the mockup screens (Dashboard, Plots, Market, Research, Finance) in desktop and Steam Deck frames, with toggles for the inverse scheme, a 14px ruler and a Tokens view. Retired directions (A, B, D; H1 to H3) remain in the file behind comments.

## Dependencies
- `tokens.css` is read by `tools/design/tokens_to_rcss.py`, which generates `game/assets/ui/tokens.rcss`, and by `tests/design_tokens_test.cpp` (Catch2), which checks that every token name and value appears in the generated file. `game/src/chart_element.cpp` cites two token values in a comment.
- `handoff.md` is cited as the layout source by `game/assets/ui/main.rcss`, `game/assets/ui/main.rml`, `game/src/ui_model.h` and `game/src/main.cpp` (comments only; nothing parses it).
- `bench.html` loads web fonts from `fonts.googleapis.com`; it has no other external resource and nothing in the repository reads it.
- `ui-design.md` refers to `docs/process.md` (prose pipeline), `docs/playtests/2026-10-08.md` (playtest lessons) and specs 002 and 003; `docs/specs/003-client-restyle.md` and the "Client restyle (spec 003)" section of `docs/DESIGN.md` refer back to this directory.

## Patterns
- Design Studio method: several polarised directions built on one bench with identical data and copy, rejected in rounds, with each rejection codified as a `[Constraint: ...]` line in `ui-design.md`.
- Design tokens as CSS custom properties in a single `:root` block, mirrored for RmlUi (which has no custom properties) as a generated lookup table plus utility classes.
- Plain browser stack in `bench.html`: HTML, CSS (flexbox and tables) and inline JavaScript that builds screens from template strings; no framework and no build step.
- `handoff.md` pairs every web CSS feature used in the mockup with its RmlUi 6 fallback in a table.

## Constraints
- `ui-design.md`: the design is locked; "Code follows this document and `handoff.md`; changes go through a new sprint, not ad hoc."
- `docs/process.md`: interface work has an extra phase before Build, a Design Studio session whose outcome is recorded in this directory; "Code follows the design document, never the other way round." Mockup copy is exempt from the prose polish pipeline until it becomes real copy.
- `tokens.css` and `game/assets/ui/tokens.rcss` must agree; `tests/design_tokens_test.cpp` fails otherwise and requires more than 40 tokens. The generator recognises one `--name: value;` declaration per line with lowercase names.
- Hard legibility constraints stated in `ui-design.md`: Steam Deck 1280x800, body text no smaller than 14px, buttons at least 40px tall, no horizontal scroll.
- `ui-design.md` (Sprint 1) and `handoff.md` name the bench `deep-claim-studio.html`; the tracked file is `bench.html` (named so in `ui-design.md` Sprint 4). No file called `deep-claim-studio.html` is tracked.
- `handoff.md` predates spec 003 and differs from what was built in two stated places: it puts the status strip on the Dashboard only (spec 003 and `docs/DESIGN.md` say every tab), and it suggests generating `main.rcss` from `data/ui/tokens.json` (the built pipeline is `tokens.css` to `tokens.rcss`; there is no `data/ui/`).
