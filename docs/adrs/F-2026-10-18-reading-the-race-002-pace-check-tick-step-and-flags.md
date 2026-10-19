# ADR-F-2026-10-18-reading-the-race-002: Falling behind is checked in tick step 9b, with flags in saved state

**Date:** 2026-10-19
**Status:** Accepted

**Context:** Spec 011 BR-005 has rules about when the notices come:

- A notice comes once on the day a company's earliest founding day passes its rival's.
- It comes again only after the company has drawn level or ahead.
- A notice also comes once on the day the company's earliest day passes the deadline.

Edge case 7 compares the days at the end of the tick. Detecting a change needs the previous day's answer, which no other state holds. The owner chose per-company flags in saved state (2026-10-19). The figures the player sees are built from the state after the day advances.

**Decision:**

- **The step.** A new step, `runProgramPace`, runs as step 9b, after `++state.day`, so it compares exactly the figures the next view shows. It runs only while the game is open, and skips bankrupt companies.
- **The flags.** `Company::programBehind` and `Company::programPastDeadline` (`uint8`) are saved and hashed.
- **The rules, per company in index order:**
  - `behind = own day > leading solvent rival's day`. On a change from 0 to 1 it raises the private `ProgramFellBehind`, naming the rival with both days. On a change from 1 to 0 it clears silently.
  - `past = own day > deadline`. On a change from 0 to 1 it raises `ProgramPastDeadline`.
- **Who gets it.** Every company, not just the player. The AI reads only the view, so its play does not change.
- **On load.** A save is refused unless each solvent company's flags equal their rules on the saved state.

**Consequences:**

- **Easier:**
  - The notice and the displays cannot disagree.
  - "Once per fall" survives save and load.
  - A save's flags are checked against the rule.
- **Harder:**
  - A step now follows the day advance, which breaks the convention that the advance is last. It is recorded in `docs/DESIGN.md` and in the pipeline comment as the final step.
  - `kGolden`, `kGoldenPlayed` and `kGoldenRace` move.
- **Rejected:**
  - Checking inside step 7, which compares a day the player never sees.
  - A stateless check, which cannot see a change from the day before.
