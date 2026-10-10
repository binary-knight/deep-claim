# Agent Working Rules — Deep Claim

## First: Read These Two Files

Before taking any action in this repository, read in order:
1. `DOMAIN.md` — the game, its vision, constraints, and design implications
2. `PROJECT.md` — where things live and how the repo is structured

Then read `ARCHITECTURE.md` (rules R-001 onward, exemplars, do-not-copy list)
before touching code. Do not skip this step. Context you skip becomes an
assumption you will regret.

---

## Source Discipline

- Cite sources from `DOMAIN.md`, `docs/DESIGN.md`, a spec, a playtest write-up or `docs/sources/`; do not invent facts.
- If you need to make a factual claim about the game and cannot find a source, say so — do not fabricate.
- If source material is ambiguous or missing, escalate to the user rather than guessing.

---

## Edit Discipline

- Read before you write. Use your read tool; never assume file contents.
- Prefer targeted edits over full rewrites. Change the minimum necessary.
- Do not modify files outside your task's stated scope unless blocking.
- Do not silently delete code. If removing, explain why in your response.

---

## Communication Rules

- State what you are about to do before you do it.
- When you finish a task, summarize what changed and what the caller should do next.
- If you discover a blocker, stop and report it immediately — do not work around it silently.
- Flag unclear requirements as questions, not assumptions.

---

## Boundaries

- Do not make architectural decisions unilaterally. Flag them for the architect or SEM.
- Do not modify `DOMAIN.md` or `PROJECT.md` without explicit user instruction.
- Do not modify test files unless your task explicitly covers testing.
- Do not commit or push to version control unless your task explicitly requires it.
- Never hand-edit `ARCHITECTURE.md` or `.etc_sdlc/architecture-baseline.yaml`; `.claude/scripts/baseline.py` is their only writer.

---

## Project Rules (the owner's standing agreements)

- **Plan first.** Plan before coding and wait for the owner's approval. A spec must be approved before it is built.
- **Ask before** adding a dependency or changing the architecture.
- **Decisions** are put to the owner as multiple-choice prompts with a recommendation.
- **Earth first.** Build the Earth mining game until it is fun, challenging and strategic. Do not draft space, asteroid, conflict or multiplayer designs, and do not park Earth problems for a later phase (see `DOMAIN.md`, "What Deep Claim Is Not").
- **Determinism is non-negotiable.** No wall clock, no floating point in sim state, seeded streams only. A change to sim state moves the golden hash once, with the reason recorded in `docs/DESIGN.md`.
- **Rules live in the sim** (R-001). **Tunable numbers live in `data/*.json`** with the reasoning in the spec (R-003).
- **Prose.** Text the code shows the player goes in `data/strings.json` (R-002). Draft it plainly, then polish it with the agy CLI through `tools/prose/polish.sh`; numbers, units, names and `{placeholders}` must survive unchanged. See `tools/prose/README.md`.
- **Interface work** follows `docs/design/ui-design.md` and `docs/design/tokens.css`, and must pass the layout probe at both frame sizes.
- **Build in a worktree**, in small focused commits. Merge only after the build report has been checked.
- **Review.** Each built spec is reviewed with the Codex CLI (`codex exec --ephemeral -s read-only`, run from the repo root); findings are applied, or declined with a reason recorded with the spec.
- **Done means played.** A feature is finished when a monitored playtest, written up in `docs/playtests/`, shows the behaviour the spec promised.
- **Quality gates run by hand.** The etc harness has no C++ profile, so its lint and test hooks skip these sources. Before calling work complete: build every preset touched, `ctest`, clang-format and clang-tidy 18, the `sim_runner` sweep for balance changes, the layout probe for interface changes.

## Building in the agent sandbox

- Set `XDG_RUNTIME_DIR=$TMPDIR` (Catch2 test discovery) and `VCPKG_DEFAULT_BINARY_CACHE=$TMPDIR/vcpkg-cache`.
- A worktree configures with `-DCMAKE_TOOLCHAIN_FILE=<main repo>/external/vcpkg/scripts/buildsystems/vcpkg.cmake`.
- The sandbox has no display or GL headers: the client can be compile-checked and probed there, not run. CI builds it on Ubuntu and Windows.
