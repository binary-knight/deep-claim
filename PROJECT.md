# Deep Claim — Project Orientation

**What this is:** A deterministic C++20 mining-company management game: a rules library (`sim/`), a headless balance runner, and an SDL3 + RmlUi desktop client.

**Read order for new agents:**
1. `DOMAIN.md` — what Deep Claim does and why (the game, its constraints, design implications)
2. This file — where to find things in the repo
3. `ARCHITECTURE.md` — the ratified architecture baseline: rules R-001 onward, exemplars, do-not-copy list
4. The spec for your task (`docs/specs/` for 001 to 007; from 008 on, `spec/<slug>.md`, with the full feature record under `.etc_sdlc/features/active/<feature>/`)
5. `docs/design/core-loop.md` — the game's premise and the order of work
6. `docs/playtests/` — ground truth on how the game actually plays

---

## Current Phase

**Earth game, pre-release; the core loop is being built.** Specs 001 to 008
are merged; four playtests are written up. After playtest 4 the owner set the
game's premise and direction (`docs/design/core-loop.md`): a deadline, a race
against a rival to found a space program, and production chains as nodes and
flows. Spec 010 (the race for the program) is the first slice of that loop and
is built in four steps. Step 1 (the deadline, the three-stage program, the
ending, a deeper market) is built on branch `spec-010-race-step-1` and waits
for the owner's playtest; steps 2 to 4 (plants, mine life from spec 009,
tuning) follow only after it. Spec 009 (mine economics) is folded into spec
010 and not built on its own. New work runs through the etc harness (`/spec`,
`/architect`, `/build`); the gates in `docs/process.md` (Codex review, CI,
layout probe, sweep, playtest) still apply and are run by hand, because etc
has no C++ profile. Playtest builds for Windows are fetched from CI onto the
owner's share with `./build.sh`.

---

## Where Things Live

| What you need | Where to find it |
|---|---|
| Game domain, vision and constraints | `DOMAIN.md` |
| Architecture rules, exemplars, do-not-copy | `ARCHITECTURE.md` (rendered from `.etc_sdlc/architecture-baseline.yaml`; never edit either by hand) |
| Architecture decision log (running, append-only) | `docs/DESIGN.md` |
| Feature specs 001 to 007 | `docs/specs/NNN-name.md` |
| Feature specs 008 onward | `spec/<slug>.md` |
| Designs, tasks and reviews written through etc (008 onward) | `.etc_sdlc/features/active/<feature>/` (untracked) |
| Decision records (ADRs) | `docs/adrs/` |
| Customer journeys | `docs/mvp/journeys/` |
| Game premise, core loop, Earth strategy | `docs/design/core-loop.md`, `docs/design/earth-strategy.md` |
| Product requirement documents | `docs/prds/` |
| Playtest write-ups | `docs/playtests/YYYY-MM-DD.md` |
| Locked interface design and tokens | `docs/design/ui-design.md`, `docs/design/tokens.css` (`handoff.md` is historical) |
| Development process and quality gates | `docs/process.md` |
| Ground truth research | `docs/sources/` |
| Implementation plans (dated) | `docs/plans/` |
| Engineering standards | `docs/standards/` |
| Operational guides | `docs/guides/` |
| Per-directory descriptions | `<dir>/.meta/description.md` |
| Rules and state (no third-party dependencies) | `sim/` |
| JSON content loader | `content/` |
| Balance numbers and the strings catalogue | `data/*.json` |
| Session layer, trace, replay tool | `game/session/` |
| Client (SDL3 + RmlUi), layout probe | `game/src/`, `game/assets/ui/` |
| Storefront-shaped services interface | `platform/` |
| Headless balance runner and sweeps | `sim_runner/` |
| Tests (one Catch2 binary) | `tests/` |
| Prose pipeline (draft, agy polish, catalogue) | `tools/prose/` |
| Token and icon generators | `tools/design/` |
| Playtest traces and screenshots (gitignored) | `playtest/`; Windows playtests write to the share's `playtest/` folder |
| Playtest build onto the owner's share | `build.sh` (`--linux` builds and runs here) |

**Anti-fabrication rule:** If an agent needs to make a factual claim about the
game or the system, that claim must cite `DOMAIN.md`, `docs/DESIGN.md`, a
spec, a playtest write-up or a file in `docs/sources/`. No invented facts.

---

## Tech Stack Anchors

- **Language and build:** C++20, CMake presets (`debug`, `release`, `asan`, `debug-game`, `release-game`) in `CMakePresets.json`
- **Dependencies:** vcpkg manifest mode, submodule at `external/vcpkg`; Catch2 and nlohmann-json always, SDL3 and RmlUi behind the `game` feature (`vcpkg.json`)
- **Simulation:** fixed-point and integer state, PCG32 streams, FNV-1a state hash (`sim/`)
- **Client:** SDL3 window, RmlUi 6.3 with a vendored SDL + GL3 backend (`game/backends/`, not ours to restyle)
- **Local dev:** `git submodule update --init && ./external/vcpkg/bootstrap-vcpkg.sh -disableMetrics && cmake --preset debug && cmake --build --preset debug`
- **Testing:** `ctest --preset debug` (Catch2 binary `sim_tests`, plus CTests `runner_smoke`, `runner_outcome`, `runner_bite_measures`, `runner_sweep` and, in game presets, `layout_probe`); three golden determinism hashes in `tests/determinism_test.cpp`
- **Lint:** clang-format 18 and clang-tidy 18, warnings as errors; enforced in `.github/workflows/ci.yml` on Ubuntu (Windows CI builds and tests); rules R-005 to R-007 are machine-checked by `tests/sim_source_rules_test.cpp`

**Key decisions to read before touching architecture** (sections of `docs/DESIGN.md`; feature-level decision records are in `docs/adrs/`):
- "Determinism rules" and "Hidden truth vs observable view"
- "Architecture split" together with rule R-001 in `ARCHITECTURE.md`
- "The race for the program (spec 010, step 1)": the tick order, the outcome and a finished world

---

## Role Manifests

Agents working in this repo should load context through their role manifest in
`roles/`. Manifests declare which slices of this tree a role consumes.
See `roles/` for the available manifests.
