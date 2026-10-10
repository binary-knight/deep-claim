# specs

**Purpose:** The feature specifications of Deep Claim, one Markdown file per feature, numbered in the order they were opened. Each is the phase 1 output of the process in `docs/process.md` and also carries the plan, the decisions made while building and the Codex review findings applied or declined.

## Key Components
- `001-sell-policies.md` - "Spec 001: Standing sell policies". Status line: approved 2026-10-09. Source: `docs/playtests/2026-10-08.md` finding 1.
- `002-prose-catalogue.md` - "002: Prose catalogue". Status line: approved 2026-10-09; builds after spec 001. Defines the `data/strings.json` key scheme and the `tools/prose/` pipeline. Has a Plan section and a second "Decisions (build, 2026-10-09)" section.
- `003-client-restyle.md` - "003: Client restyle (Control Room) with the playtest fixes". Status line: approved 2026-10-09. Implements `docs/design/ui-design.md` and `docs/design/handoff.md`.
- `004-playtest-2-fixes.md` - "004: Playtest 2 fixes". Status line: approved 2026-10-09. Source: `docs/playtests/2026-10-09.md` findings 2, 4, 7 and 8. Plan section at the end.
- `005-world-and-opening.md` - "005: A bigger world and a fair opening". Status line: approved 2026-10-09 as drafted; builds after spec 004. States the sweep goals that `sim_runner` gates on.
- `006-balance.md` - "006: Balance: a contested economy". Status line: approved 2026-10-09; builds on spec 005. The longest spec (445 lines); contains measured sweep tables, targets and a long Decisions log.
- `007-pending-orders.md` - "007: Orders show at once, and can be taken back". Status line: approved 2026-10-10, with the pending-loan amendment. Source: `docs/playtests/2026-10-10.md` finding 1. Adds a Session section; states no sim change. No Plan section and no build decisions recorded in the file.
- `001-prose/` - draft and agy-polished TSVs of the 15 strings added by spec 001 (an archived record; see its own description).

## Dependencies
- Template and rules come from `docs/process.md` ("Spec template", "Rules of the road"); architectural summaries of each built spec are kept in `docs/DESIGN.md` (sections "Standing sell policies (spec 001)" through "Balance: a contested economy (spec 006)").
- Specs cite their inputs: playtest write-ups in `docs/playtests/`, the design documents in `docs/design/` (spec 003), and repository paths they change (`data/*.json`, `tools/prose/*`, `tools/design/*`, `tests/*`, `sim/`, `game/`).
- Cited from code: `sim_runner/main.cpp` (comments at lines 363, 370 and 808 take the sweep goal thresholds from `005-world-and-opening.md` and `006-balance.md`); `sim/include/sim/content.h` line 195 and `tools/prose/README.md` cite `002-prose-catalogue.md` for the string key scheme.
- No build step, test or CI job reads any file in this directory.

## Patterns
- File name `NNN-name.md`; first line `# NNN: Title` (spec 001 uses `# Spec 001: Title`); a `Status:` line directly under the title; then the template sections Goal, Player-facing behaviour, Sim rules, View, Client, Tests, Out of scope, Decisions. Individual specs add sections (Data, Pipeline, Plan, Session) or omit ones that do not apply.
- Decisions are a dated list appended during the build, including Codex review findings with the reason each was applied or declined, and measured results from `sim_runner` sweeps.
- Balance numbers live in `data/*.json`; the reasoning for each number is written in the spec.

## Constraints
- `docs/process.md`: a spec is approved by the owner before any code is written; numbering is in the order specs are opened, starting at `001-sell-policies`.
- `docs/process.md`: "A spec that puts logic in the client is wrong" (the sim holds every rule); feature decisions are recorded in the spec, architectural decisions in `docs/DESIGN.md`.
- `docs/DESIGN.md` ("Spec-driven development"): the spec is committed first, the implementation follows it, and the Codex review runs against the spec as well as the code.
- Every status line in these files says "approved"; none says "merged". The statement that specs 001 to 006 are merged is in `docs/HANDOFF.md`, and `docs/DESIGN.md` has a section for each of 001 to 006 and none for 007.
