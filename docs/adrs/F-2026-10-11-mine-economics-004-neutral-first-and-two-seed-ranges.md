# ADR-F-2026-10-11-mine-economics-004: Rules land with neutral data first; gates run on two seed ranges

**Date:** 2026-10-11
**Status:** Accepted
**Context:** Spec 008's economy sits close to its gates, and its tuning was fitted to seeds 1 to 50: on seeds 51 to 100 two gates failed by two seeds each. This spec changes output, cost and mine endings, so a failing gate could come from a bug or from the numbers.
**Decision:** The build has two stages. Stage 1 lands every rule with data that changes nothing: the AI limited to the standard size, depth slope zero, closing share zero, and the AI closing a depleted mine the next day. The gated sweep must then match spec 008's recorded figures, and any difference is explained before stage 2. Stage 2 switches the rules on through data and tunes. The gated sweep runs as two release tests, seeds 1 to 50 and seeds 51 to 100, and both must pass every gate in spec BR-016. If they cannot all be met, the owner decides which gate gives way (the spec's blocker).
**Consequences:** A moved gate in stage 2 is known to come from the numbers. The tuning is held to twice the evidence. The build has an extra stage and a second long test, run in parallel with the first, and the tuner has a tighter target. Stage 1 may differ from spec 008 by small amounts if closing shifts a day's cost; that is reported, not hidden.
