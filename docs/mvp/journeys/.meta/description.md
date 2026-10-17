# journeys

**Purpose:** Player journeys captured with the owner through the etc `/journey` skill, one file per journey (`J-NNN-<slug>.md`). Each describes, in the player's terms, a situation the game must make into a decision; the specs in `spec/` name the journey they serve.

## Key Components
- `J-001-player-rides-out-a-falling-price.md` - "A player rides out a falling price" (126 lines). Captured 2026-10-10 from the playtest write-ups. A few years into a game, a price slides; the player makes a call that visibly matters and gets through the lean months without missing a loan payment. Served by spec 008 (`spec/markets-that-bite.md`).
- `J-002-player-sizes-works-and-closes-a-mine.md` - "A player sizes, works and closes a mine" (97 lines). Captured 2026-10-11 from the playtest write-ups and `docs/design/earth-strategy.md`. The player chooses a mine's size, enlarges it at a moment that pays back and closes it while still ahead. Served by spec 009 (`spec/mine-economics.md`), now step 3 of spec 010.
- `J-003-player-races-a-rival-to-found-the-program.md` - "A player races a rival to found the program" (74 lines). Captured 2026-10-11, after playtest 4, from `docs/design/core-loop.md`. From day one the deadline and a rival with the same three program stages are on screen; the player founds the program first or loses narrowly and can say why. Served by spec 010 (`spec/race-for-the-program.md`).

## Dependencies
- Drawn from `docs/playtests/` (J-001, J-002), `docs/design/earth-strategy.md` (J-002) and `docs/design/core-loop.md` (J-003), as each file's `captured_by` field states.
- Referenced by the Summary of each spec in `spec/` (and, for spec 008, its Research Notes); no code, test or CI job reads them.
- "Tools / Systems touched" names client screens (Dashboard, Plots, Market, Research, Finance, the log and stop rules, and for J-003 a new Program screen), not source files.

## Patterns
- YAML front matter: `journey_id`, `title`, `actor`, `actor_role`, `trigger`, `outcome`, `status`, `captured_at`, `captured_by`, `sources` (`[interactive]` in all three).
- Then `# J-NNN — <title>` and the sections Actor, Trigger, Outcome, Steps, Failure modes, Tools / Systems touched, Emotional journey, Open questions.
- Plain player-facing language, no implementation detail; open questions are left for a spec to answer (J-001's first two, a stop when a mine loses money and a per-mine daily figure, match what spec 008 added).

## Constraints
- Every journey's `status` is `draft`; none has been marked final, although specs 008 and 010 step 1 that serve J-001 and J-003 are built.
- Journeys describe the Earth game only, in line with `CLAUDE.md` ("Earth first"); J-003's open questions (the threat's reveal, what a lesser partner looks like) are not answered by spec 010, which lists the threat's reveal as out of scope; `docs/design/core-loop.md` leaves the endings in detail for later documents.
