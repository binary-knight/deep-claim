<!-- generated from .etc_sdlc/architecture-baseline.yaml — edit via /init-project --phase=baseline -->
# Architecture Baseline

**Status:** ratified — ratified by binaryknight on 2026-10-17T05:27:02+00:00
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

- `game/src/ui_model.cpp (short labels and chips: status words, act labels, survey flags, research chips, stage states)` — player-facing labels hard-coded in C++. R-002 as ratified holds (owner, 2026-10-17): the spec 002 comment at ui_model.cpp:34-36 that short labels stay literal is superseded; migrate to the catalogue when touched.
- `game/session/src/session.cpp:306` — 'Game loaded' log text hard-coded; use the strings catalogue.
- `game/src/app.cpp:167` — rival company name hard-coded; belongs in data.
- `sim/src/ai_controller.cpp:247` — 1.3x extraction margin hard-coded; balance knobs belong in data/ai.json. Move when touched.
- `sim/include/sim/mines.h:114` — kRunningLowDays=30 hard-coded; belongs in data/balance.json. Move when touched.
- `sim/src/surveys.cpp:25` — survey shift cap hard-coded; move to data when touched.
- `docs/design/handoff.md` — historical record of the design sprint; six claims do not match the tree. Not a source of rules: use ui-design.md, tokens.css and spec 003.
- `sim/src/plot_market.cpp:31-32` — unsold-ask and ask-floor multipliers (0.9, 0.5) hard-coded; move to data/balance.json when touched (owner, 2026-10-17).
- `sim/src/worldgen.cpp:26` — initial price noise 1/5 hard-coded; move to data when touched (owner, 2026-10-17).
- `sim/src/events.cpp:20-21` — event multiplier bounds hard-coded; move to data when touched (owner, 2026-10-17).
- `sim/src/program.cpp:25` — kLesserPartnerStages = 2 hard-coded; move to data/program.json when touched (owner, 2026-10-17).
- `game/src/ui_model.cpp:781-783` — R-001 violation (owner, 2026-10-17): the client decides a storage-cost warning at a tenth of cash; the threshold and the warning belong in the sim view. Fix in the next client task.

## Rules

Normative constraints captured at ratification and via `/rule-sweep`.

- **R-001** — The sim is the only authority on game rules: it validates and rejects every command. game/ and game/session may derive display and button-enable state from view fields, but never a rule the sim does not also enforce. _[enforced by human-judgment]_
- **R-002** — Anything C++ shows the player goes through the data/strings.json catalogue via text(); static headings and button labels in game/assets/ui/main.rml are exempt. _[enforced by human-judgment]_
- **R-003** — Numbers a designer would tune live in data/*.json and reach the sim through sim::Content; do not add tuning constants to sim sources. _[enforced by human-judgment]_
- **R-004** — The interface token scale in docs/design/tokens.css is the rule for type, spacing and radius; off-scale values in main.rcss are violations to tidy when touched. _[enforced by human-judgment]_
- **R-005** — files matching sim/** must not contain unordered_ (machine-checked by tests/sim_source_rules_test.cpp, comments excluded; owner, 2026-10-17) _[enforced by human-judgment]_
- **R-006** — files matching sim/** must not contain std::hash (machine-checked by tests/sim_source_rules_test.cpp, comments excluded; owner, 2026-10-17) _[enforced by human-judgment]_
- **R-007** — files matching sim/** must not contain <chrono> (machine-checked by tests/sim_source_rules_test.cpp, comments excluded; owner, 2026-10-17) _[enforced by human-judgment]_

## Boundary Map

Cross-repo / external seams this repo participates in.

_None recorded._

## Confidence

Score: **medium**. The score is computed from the inputs below
and is auditable — never a bare assertion.

Inputs:

- artifacts_claim_checked: README.md, docs/process.md, docs/DESIGN.md, docs/design/ui-design.md, DOMAIN.md, PROJECT.md (2026-10-17 review); docs/design/handoff.md (2026-10-10)
- artifacts_not_claim_checked: docs/specs/001-007, spec/*.md, docs/adrs/*, docs/design/core-loop.md, docs/design/earth-strategy.md, tools/prose/*
- aspirational: 1
- claims_total: 90
- contradicted: 3
- patterns_surveyed: player-facing-strings (competing), rule-and-balance-placement (competing); counts are grep approximations
- review: 2026-10-17 drift review after specs 008 and 010 step 1; amendments decided by the owner
- sampling: claims capped at 10-20 per artifact; evidence by grep and targeted reads, no builds or tests run
- seams: none (single-repo offline desktop game)
- stale: 27
- verified: 59
