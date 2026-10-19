# ADR-F-2026-10-18-reading-the-race-003: Cover costs sells free stock before stock the program needs

**Date:** 2026-10-19
**Status:** Accepted

**Context:** `coverCosts` (`sim/src/finance.cpp`) sells the most valuable stock first to keep cash at zero or above. It takes no account of the program. In playtest 5 it sold the copper held for stage 1 on days 1170, 1230 and 1260 (spec 011, BR-011). The runner's company 0 has cover costs on, so any change reaches the balance sweep.

**Decision:**

- **Two passes.** Over the same eligible resources (unlocked, with stock):
  1. Free stock: stock above what the current stage still needs, `needed − delivered`.
  2. The remaining stock.
- **Order within each pass.** By the value of that tier's tonnes, descending, with ties going to the lower resource id. Each sale is capped by `saleTonnesWithinBounds` and by the tier's tonnes.
- **Unchanged.** With no program need, the free tier is the whole stock and the order is exactly today's. There is no new notice. The Program tab's cover-costs warning says the rule.

**Consequences:**

- **Easier:** routine month-end sales no longer eat the stage's goods while other stock can cover the shortfall.
- **Harder:**
  - A company short of cash with only program stock still sells it, last. BR-011 is an order, not a reserve.
  - It is a change to sim behaviour, so it moves the golden hashes of any run where it fires with program need.
  - The gated sweep is run before and after, with its figures recorded (BR-015).
- **Deferred:** a hard program reserve, which would be a balance change for spec 010 step 4.
