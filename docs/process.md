# How Deep Claim is built: spec-driven, phased

Every change to the game beyond a bug fix goes through the same six
phases, in order, and leaves a paper trail in the repository. The point is
that a feature is designed before it is coded, reviewed before it is
merged, and judged by play before it is called done.

## Phases

| Phase | Output | Gate |
| --- | --- | --- |
| 1. Spec | Specs 001 to 007: `docs/specs/NNN-name.md`. From spec 008 on: the etc harness spec, copied to `spec/<slug>.md` (see below). | The owner approves the spec. |
| 2. Plan | A short implementation plan in chat or appended to the spec: files touched, state and commands added, tests. From spec 008 on, the design and decision records written by `/architect`. | The owner says go. |
| 3. Build | Small focused commits on a branch or worktree; balance numbers in `data/*.json` with their reasoning in the spec. | Builds in every preset; tests and lint pass locally. |
| 4. Review | Codex review of the diff plus the spec; findings applied or declined with a reason in the spec's Decisions. | No open "real" finding. |
| 5. Verify | The gates, run by hand (see below): every preset touched, `ctest`, clang-format and clang-tidy 18, the sim_runner sweep for balance changes, the layout probe for interface changes. Then CI on Ubuntu and Windows, including the sanitizer leg. | All green. |
| 6. Playtest | The owner plays with the trace on; the session is written up in `docs/playtests/`. | The spec's player-facing behaviour matches what the trace shows. |

Interface work has one extra phase before Build: a Design Studio session
whose outcome is recorded in `docs/design/` (the UI design document and
the chosen direction's tokens). Code follows the design document, never
the other way round.

## Specs through the etc harness (spec 008 on)

From spec 008 on, the first three phases run through the etc harness:

- `/spec` writes `.etc_sdlc/features/active/<feature>/spec.md`, which is
  not tracked. A copy is kept at `spec/<slug>.md`: `spec/markets-that-bite.md`,
  `spec/mine-economics.md` and `spec/race-for-the-program.md`.
- `/architect` writes a design beside the spec and decision records in
  `docs/adrs/`.
- `/build` breaks the work into tasks, dispatches them and verifies them.

These specs use the etc format: Summary, Scope, Requirements, Acceptance
Criteria, Edge Cases and Research Notes, with Decisions and "Open after
build" added once the build is done. The template below is how specs 001
to 007 were written. It is kept as history.

## Verify gates

The etc harness has no C++ profile, so its lint and test hooks skip these
sources. The gates are run by hand before a build is called complete:
build every preset touched, `ctest`, clang-format and clang-tidy 18, the
`sim_runner` sweep for balance changes, and the layout probe at both frame
sizes for interface changes. CI on Ubuntu and Windows then confirms.
clang-format and clang-tidy run in CI on Ubuntu only.

## Spec template (specs 001 to 007)

```
# NNN: Title

## Goal
One paragraph: the player problem this solves and how we will know.

## Player-facing behaviour
What the player sees and does, in plain language. Screens, controls,
messages, defaults.

## Sim rules
State added or changed. Commands and their validation (reject reasons).
Where in the tick pipeline it runs and why. Notices. Balance numbers and
the reasoning behind each. Save-format and determinism consequences.

## View
What the CompanyView exposes for the interface and the AI.

## Client
Which tabs and controls change; what they send; what they show.

## Tests
The cases that prove the rules, named.

## Out of scope
What this spec deliberately does not do.

## Decisions
Dated list of choices made while building, including review findings
declined and why.
```

## Rules of the road

- The sim is the only authority on game rules (R-001 in
  `ARCHITECTURE.md`): it validates and rejects every command. The client
  and session may derive display and button-enable state from the view,
  never a rule the sim does not also enforce. A spec that puts a rule in
  the client is wrong.
- Determinism is non-negotiable: no wall clock, no floats in state, seeded
  RNG streams only. Any change to state updates the golden hash once,
  with the reason in `docs/DESIGN.md`.
- `docs/DESIGN.md` stays the running record of architectural decisions;
  feature-level decision records also live in `docs/adrs/` (spec 008 on);
  specs record feature decisions; playtests record what players did.
- Numbering: specs are numbered in the order they are opened. The first
  is `001-sell-policies`.

## Prose: drafted here, polished by agy

Every sentence a player reads (log notices, status lines, rejection
reasons, how-to-play text, event names and descriptions, research and
tech descriptions, tooltips) goes through two hands:

1. I draft it, plain and correct, with every number, unit and id in place.
2. `tools/prose/polish.sh` sends the draft to the agy CLI with the
   game's voice brief (an operator's desk with in-world flavour: a
   foreman's note, concrete nouns, no buzzwords). agy may rewrite freely
   so the result reads as human writing, with one rule: numbers, units,
   plot and resource names and placeholders such as `{amount}` must
   survive unchanged.
3. The polished text is diffed against the draft and checked for those
   invariants before it lands in `data/strings.json`.

Prose lives in data, not in code, so the pipeline can run on a file
(spec 002). Mockup copy for design sessions is exempt until it becomes
real copy.
