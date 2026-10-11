# ADR-F-2026-10-11-first-slice-race-for-the-program-004: The slice is built in four playable steps; the owner's playtest gates step 2

**Date:** 2026-10-11
**Status:** Accepted
**Context:** Specs 008 and 009 were tuned to sweep gates before the owner could play them, and playtest 4 found a game that passed its gates and could not be won. The slice is large: a race, production chains, mine life and a market retune.
**Decision:** The build has four steps, each ending green and playable: (1) the race, with a provisional market resize; (2) plants; (3) mine life, which is spec 009 with its design and decision records; (4) tuning, where market depth and the final gates are met on seeds 1 to 50 and 51 to 100. After step 1 the build stops for a monitored playtest. Step 2 begins only when the owner clears the spec's blocker. Steps 2 to 4 are confirmed with the owner at that point and may change. Sweep gates remain necessary at every step but are no longer treated as evidence that the game is fun.
**Consequences:** The race itself is played within the first build, before anything is stacked on it. The golden hashes and the content fingerprint move once per step, so saves break four times. The runner and its gates are extended step by step, and step 1 ships with numbers known to be provisional. If the playtest shows the race has no tension, steps 2 to 4 are redesigned instead of built.
