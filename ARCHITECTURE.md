<!-- generated from .etc_sdlc/architecture-baseline.yaml — edit via /init-project --phase=baseline -->
# Architecture Baseline

**Status:** ratified — ratified by binaryknight on 2026-10-10T11:01:28+00:00
**Confidence:** medium

> This is the ratified, evidence-checked record of how this codebase is
> actually built — the golden registry agents and humans build against.

## Exemplars

The blessed reference implementations. Copy these patterns for new work.

- **Keyed prose catalogue** (`data/strings.json, game/session/src/text.cpp, game/session/include/game/format.h`) — any text C++ shows the player (notices, status lines, labels, chips) _[blessed by binaryknight]_
- **Balance numbers in data** (`data/balance.json, data/ai.json, content/src/loader.cpp`) — any number a designer would tune; reaches the sim through sim::Content _[blessed by binaryknight]_
- **Design tokens pipeline** (`docs/design/tokens.css, tools/design/tokens_to_rcss.py, tests/design_tokens_test.cpp`) — interface colours, type and spacing _[blessed by binaryknight]_

## Do Not Copy

Superseded or anti-pattern code. Never use these as a model.

- `game/src/ui_model.cpp (string literals at L37-59, L420-528, L1132-1143)` — player-facing labels hard-coded in C++; use the strings catalogue. Migrate when touched.
- `game/session/src/session.cpp:127` — 'Game loaded' log text hard-coded; use the strings catalogue.
- `game/src/app.cpp:157` — rival company name hard-coded; belongs in data.
- `sim/src/ai_controller.cpp:188` — 1.3x extraction margin hard-coded; balance knobs belong in data/ai.json. Move when touched.
- `sim/include/sim/mines.h:75` — kRunningLowDays=30 hard-coded; belongs in data/balance.json. Move when touched.
- `sim/src/surveys.cpp:25` — survey shift cap hard-coded; move to data when touched.
- `sim/src/markets.cpp:194,232` — market clamps hard-coded; move to data when touched (spec 008).
- `docs/design/handoff.md` — historical record of the design sprint; six claims do not match the tree. Not a source of rules: use ui-design.md, tokens.css and spec 003.

## Rules

Normative constraints captured at ratification and via `/rule-sweep`.

- **R-001** — The sim is the only authority on game rules: it validates and rejects every command. game/ and game/session may derive display and button-enable state from view fields, but never a rule the sim does not also enforce. _[enforced by human-judgment]_
- **R-002** — Anything C++ shows the player goes through the data/strings.json catalogue via text(); static headings and button labels in game/assets/ui/main.rml are exempt. _[enforced by human-judgment]_
- **R-003** — Numbers a designer would tune live in data/*.json and reach the sim through sim::Content; do not add tuning constants to sim sources. _[enforced by human-judgment]_
- **R-004** — The interface token scale in docs/design/tokens.css is the rule for type, spacing and radius; off-scale values in main.rcss are violations to tidy when touched. _[enforced by human-judgment]_
- **R-005** — files matching sim/** must not contain unordered_ _[enforced by human-judgment]_
- **R-006** — files matching sim/** must not contain std::hash _[enforced by human-judgment]_
- **R-007** — files matching sim/** must not contain <chrono> _[enforced by human-judgment]_

## Boundary Map

Cross-repo / external seams this repo participates in.

_None recorded._

## Confidence

Score: **medium**. The score is computed from the inputs below
and is auditable — never a bare assertion.

Inputs:

- artifacts_claim_checked: README.md, docs/process.md, docs/DESIGN.md, docs/design/handoff.md, docs/design/ui-design.md
- artifacts_not_claim_checked: docs/specs/001-007, tools/prose/README.md, tools/prose/voice.md, docs/HANDOFF.md, .meta/ (generated this run)
- aspirational: 0
- claims_total: 63
- contradicted: 3
- patterns_surveyed: player-facing-strings (competing), rule-and-balance-placement (competing); counts are grep approximations
- sampling: claims capped at 10-20 per artifact; evidence by grep and targeted reads, no builds or tests run
- seams: none (single-repo offline desktop game)
- stale: 10
- verified: 50
