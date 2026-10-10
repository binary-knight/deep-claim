# playtests

**Purpose:** Write-ups of the owner's monitored test plays of the game client, one Markdown file per session, named by date. They are the phase 6 output of `docs/process.md` and the stated source of the specs that follow each session.

## Key Components
- `2026-10-08.md` - "Playtest 1". Build `02929c5`, three games in one sitting. Sections: the three games (table), ten ranked findings, "Proposed order of work", "Decisions (same day)". Source of spec 001 and of the playtest lessons listed in `docs/design/ui-design.md`.
- `2026-10-09.md` - "Playtest 2". Build `ba7b53c` (specs 001 to 003), one game to day 5255. Sections: the game (table), "What the last batch fixed", eight ranked findings, "Proposed order of work" (specs 004, 005, 006), "Decisions (same day)".
- `2026-10-10.md` - "Playtest 3". Build `161b0dc` (specs 004 to 006), one game to day 2974. Sections: the game (table), "What the last three specs fixed", seven ranked findings, "Proposed order of work" (spec 007, spec 008). No Decisions section.

## Dependencies
- Each write-up is derived from a playtest trace file named in its opening paragraph (`playtest/trace-<timestamp>.jsonl`; the `playtest/` directory is gitignored, so the traces are not in the repository) and from `deep_claim_replay` output. The trace format is defined in `game/session/include/game/trace.h`.
- Referenced by `docs/specs/001-sell-policies.md`, `004-playtest-2-fixes.md`, `005-world-and-opening.md`, `006-balance.md`, `007-pending-orders.md`, by `docs/design/ui-design.md`, by `docs/DESIGN.md` ("Monitored playtesting: trace, screenshots, replay") and by `docs/HANDOFF.md`.
- No code, test or CI job reads these files.

## Patterns
- File name is the session date, `YYYY-MM-DD.md`; title `# Playtest N: YYYY-MM-DD`.
- Fixed shape: build id, seed and trace statistics; a table of the game or games; findings numbered and ranked by weight; a proposed order of work; decisions taken the same day.
- Figures are quoted from the trace and the replay; each write-up states whether the replay matched the client's state hash on every day played.

## Constraints
- `docs/process.md` phase 6: the owner plays with the trace on and the session is written up here; the gate is that the spec's player-facing behaviour matches what the trace shows.
- `docs/DESIGN.md` ("Monitored playtesting"): one file per session, with the findings ranked and the order of work they imply.
- `docs/process.md` ("Rules of the road"): playtests record what players did; feature decisions belong in specs and architectural decisions in `docs/DESIGN.md`.
- The write-ups are dated records; later documents supersede their "Proposed order of work" lists (for example `docs/HANDOFF.md` records the owner's instruction not to draft the asteroid design that playtests 2 and 3 list as a later step).
