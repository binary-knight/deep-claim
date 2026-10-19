# ADR-F-2026-10-18-reading-the-race-004: The session keeps the day's stops until the clock restarts

**Date:** 2026-10-19
**Status:** Accepted

**Context:** In playtest 5 the clock stopped 58 times without the player seeing why.

- `Session` keeps a single `pausedBy_` and hands it over once (`takePausedBy`).
- `App` writes "Paused: …" to the status line and clears it after 12 real seconds, or sooner when any other status replaces it.
- Several stops on one day are lost after the first.
- The log does not mark which lines stopped the clock.

Spec 011 BR-009 and BR-010 require four things:

- the reason stays until the clock restarts
- "+N more" for further stops on the same day
- log marks
- race stops set apart

**Decision:**

- **The day's stops.** `Session::stopsToday()` replaces `takePausedBy()`. It returns, in log order, the notices that matched an enabled Stops rule on the day the clock stopped. When the game ends, the ending notice comes first.
- **When they clear.** `setSpeed` clears them when the clock leaves Paused, and so does a load. They are session memory and are not saved.
- **Log marks.** `LogEntry::stoppedClock` marks every logged line whose notice matched an enabled rule.
- **The new rule.** `PauseOn::FellBehind`, with the key `fell_behind`, is appended and is on by default. It covers both new notices.
- **Race stops.** `isRaceStop` is true for `RivalProgram` and `FellBehind`.
- **The client.** It shows a stop panel from `stopsToday()`: the rule's Stops-menu name, the first line, "+N more", a button to the tab where the player acts, and a way to switch the rule off. The real-time timer goes.

**Consequences:**

- **Easier:**
  - The reason cannot vanish at Fastest.
  - The count and the marks come from one list.
  - The client loses its timer.
- **Harder:**
  - An internal API changes, so its callers and tests change in the same commit.
  - A loaded game starts with no stop shown; its clock is stopped anyway.
- **Settings files** without `fell_behind` read the rule as on.
