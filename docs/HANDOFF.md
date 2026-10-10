# Handoff: 2026-10-10

Written for a new Claude Code session started in `~/Space/deep-claim`
(so the project-scoped etc harness in `.claude/` loads). The previous
sessions ran from `~/Space`; their memory notes are at
`/home/jknight/.claude/projects/-home-jknight-Space/memory/` and are
worth reading once (`MEMORY.md` is the index). Delete this file when it
has been absorbed.

## Where things stand
- `main` is at the playtest 3 write-up (`60a4362`) plus this note and
  the spec 007 draft. CI is green on the spec 006 merge (nine jobs).
  213 tests. Golden hash `0x2a5da3177ab7c69d`. Release client at
  `build/release-game/game/deep_claim`.
- Specs 001 to 006 are merged. Playtests 1 to 3 are written up in
  `docs/playtests/`. Design is locked (`docs/design/`).
- **Spec 007** (`docs/specs/007-pending-orders.md`): drafted, NOT
  approved. Orders show at once and can be cancelled before the clock
  restarts; client and session only, no sim change. The owner asked for
  it during playtest 3 and said yes to cancellation. It also fixes the
  stale build id in traces.
- **etc harness** (github.com/Heavy-Chain-Engineering/etc, `517c5c2`)
  was installed on 2026-10-10 with `--client claude --scope project`
  into `.claude/` (gitignored). `/init-project` has NOT been run; the
  tier-0 hook wants `DOMAIN.md` and `PROJECT.md`. C++ has no language
  profile, so its lint and test-exists hooks skip our sources. An
  earlier attempt at `/init-project` was abandoned and its artifacts
  removed; ask the owner before running it.

## Direction from the owner
- **Earth first.** "I really want to focus on building out the full
  earth side of the game first before even looking at the space side. We
  should make the earth mining fun and challenging and require real
  strategy first." Do not draft the asteroid design; do not park Earth
  problems for a space phase.
- Agreed order after 007: an Earth strategy design document (proposed,
  timing not yet confirmed: before or after the market retune), spec
  008 markets (demand sized to the 120-plot world; prices sat at 0.35 to
  0.5x base all game), save slots with confirmations.
- Open question put to the owner and not answered: whether new work
  goes through etc's `/spec` and `/build` or stays on `docs/process.md`.

## Working agreements
- Plan before coding and wait for approval; ask before adding
  dependencies or changing architecture; small focused commits; balance
  numbers in data files with reasoning in the spec; keep
  `docs/DESIGN.md` current.
- Each built spec is reviewed by the Codex CLI (`codex exec --ephemeral
  -s read-only`, run from inside the repo); findings are applied or
  declined with reasons in the spec's Decisions.
- All player-facing prose is drafted, then polished by the agy CLI
  (`tools/prose/polish.sh`; see `tools/prose/README.md`).
- The owner likes decisions offered as multiple-choice prompts with a
  recommendation.
- Build specs in a worktree; merge only after checking the report.

## Known loose ends
- Live playtest monitor: the notice filter used in playtest 3 matched
  nothing. Take the kinds from `game/session/include/game/trace.h`
  before the next session.
- Spec 006 left two reported targets missed (mean prices, withdrawn
  land) and built no mine wind-down rule (the sim has no close-mine
  order). See its Decisions.
- New client pieces from 006 (Recent sales table, "major deposit" chip)
  have only been checked by the layout probe, not on a display.
- Old worktree directories under `.claude/worktrees/` could not be
  removed from the sandbox ("Device or resource busy").

## Sandbox quirks
- Builds and tests: `XDG_RUNTIME_DIR=$TMPDIR`,
  `VCPKG_DEFAULT_BINARY_CACHE=$TMPDIR/vcpkg-cache`; worktrees configure
  with `-DCMAKE_TOOLCHAIN_FILE=<repo>/external/vcpkg/scripts/buildsystems/vcpkg.cmake`.
- git push and gh need github.com and api.github.com allowed, and
  `XDG_CACHE_HOME=$TMPDIR` for gh.
- Codex needs api.openai.com, chatgpt.com, *.chatgpt.com,
  auth.openai.com, *.oaiusercontent.com and a `CODEX_HOME` in scratch.
- agy needs *.googleapis.com, *.google.com, accounts.google.com,
  *.googleusercontent.com, antigravity-unleash.goog, github.com, and
  `--log-file $TMPDIR/agy.log`.
- clang-format and clang-tidy 18 were installed in a session scratch
  venv; reinstall if it is gone.
