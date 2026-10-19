# ADR-F-2026-10-18-reading-the-race-001: The earliest founding day is a sim rule carried in the view

**Date:** 2026-10-19
**Status:** Accepted

**Context:** Playtest 5 lost the race without the player seeing it: research time had decided it by day 2406, but the verdict ranked only by stages and deliveries (spec 011, BR-001 to BR-004). The figure that paces the race could be derived in the client from the view and content, since it is not a rule the sim enforces. But the AI, the runner and the sim's own BR-005 notice also need it. The owner has also ruled that a threshold and the warning it decides belong in the sim view (ARCHITECTURE.md, do-not-copy `ui_model.cpp:781-783`).

**Decision:**

- **The rule.** `sim::earliestFoundingDay(content, state, company)` in `sim/program.h` is a pure function. It counts from `state.day`:
  - A stage-completion order applies the day after its project finishes.
  - A busy lab takes a new project the day after its current one ends.
  - It reads only research records, stage state and bankruptcy, never cash or deliveries.
- **Shared arithmetic.** `foundingDayIfStartedOn` serves both this and the last day to start.
- **What the view carries:**
  - every company's earliest day, its stage project's due day, and whether its day is past the deadline (the same predicate as the deadline notice)
  - the viewer's own last day to start, whether that day has passed, its lab-free day and the pace warning
  - per-ore `MissingOre`
  - `storageWarning`
  - The client only formats them.
- **What the rule counts.** A rival's busy lab is counted, which discloses when its non-program project ends (owner, GA-008).
- **What the loader refuses.** A stage project with ordinary prerequisites, so the rule stays exact for any content that loads (owner, GA-009).

**Consequences:**

- **Easier:**
  - The client, the notice and any later AI use read one rule.
  - The "never moves earlier" and "ignores cash and deliveries" properties are tested at the sim level.
- **Harder:**
  - The view grows, and every change to the rule is a sim change.
  - A rival's private research end day is now public.
  - Content that wants prerequisites on a stage project must first extend the rule.
