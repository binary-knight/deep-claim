# playtests

**Purpose:** Write-ups of the owner's monitored test plays of the game client, one Markdown file per session, named by date. They are the phase 6 output of `docs/process.md` and the stated source of the specs that follow each session.

## Key Components
- `2026-10-08.md` - "Playtest 1". Build `02929c5`, three games in one sitting. Sections: the three games (table), ten ranked findings, "Proposed order of work", "Decisions (same day)". Source of spec 001 and of the playtest lessons listed in `docs/design/ui-design.md`.
- `2026-10-09.md` - "Playtest 2". Build `ba7b53c` (specs 001 to 003), one game to day 5255. Sections: the game (table), "What the last batch fixed", eight ranked findings, "Proposed order of work" (specs 004, 005, 006), "Decisions (same day)".
- `2026-10-10.md` - "Playtest 3". Build `161b0dc` (specs 004 to 006), one game to day 2974. Sections: the game (table), "What the last three specs fixed", seven ranked findings, "Proposed order of work" (spec 007, spec 008). No Decisions section.
- `2026-10-11.md` - "Playtest 4". Build `bb3ae2a`, the first on specs 007 and 008 and the first played on Windows (the CI playtest package, from the share). One game to day 1275; the Windows trace replayed on Linux matched the client's hash on every day. Read from the trace; it states the owner's own impressions are not yet recorded. Sections: the game (table), "What the last two specs changed", seven findings by weight (the seventh "Smaller things"), "Spec 008's prediction", "Tooling". No order of work and no Decisions section; the direction that followed is in `docs/design/core-loop.md`.

## Dependencies
- Each write-up is derived from a playtest trace file named in its opening paragraph (`playtest/trace-<timestamp>.jsonl`; the `playtest/` directory is gitignored, so the traces are not in the repository) and from `deep_claim_replay` output. The trace format is defined in `game/session/include/game/trace.h`.
- Referenced by `docs/specs/001-sell-policies.md`, `004-playtest-2-fixes.md`, `005-world-and-opening.md`, `006-balance.md`, `007-pending-orders.md`, by `spec/race-for-the-program.md` (playtest 4), by `docs/design/ui-design.md`, `docs/design/earth-strategy.md` (playtest 3) and `docs/design/core-loop.md` (playtest 4), and by `docs/DESIGN.md` ("Monitored playtesting: trace, screenshots, replay").
- No code, test or CI job reads these files.

## Patterns
- File name is the session date, `YYYY-MM-DD.md`; title `# Playtest N: YYYY-MM-DD`.
- Fixed shape: build id, seed and trace statistics; a table of the game or games; findings numbered and ranked by weight; a proposed order of work; decisions taken the same day. Playtest 3 has no decisions and playtest 4 has neither of the last two.
- Figures are quoted from the trace and the replay; each write-up states whether the replay matched the client's state hash on every day played.

## Constraints
- `docs/process.md` phase 6: the owner plays with the trace on and the session is written up here; the gate is that the spec's player-facing behaviour matches what the trace shows.
- `docs/DESIGN.md` ("Monitored playtesting"): one file per session, with the findings ranked and the order of work they imply.
- `docs/process.md` ("Rules of the road"): playtests record what players did; feature decisions belong in specs and architectural decisions in `docs/DESIGN.md`.
- The write-ups are dated records; later documents supersede their "Proposed order of work" lists (for example `docs/design/earth-strategy.md` and then `docs/design/core-loop.md` set the order of work after playtests 3 and 4, and `CLAUDE.md` ("Earth first") rules out detailed design of space operations, so the asteroid phase design document that playtests 2 and 3 list as a later step is not drafted).
