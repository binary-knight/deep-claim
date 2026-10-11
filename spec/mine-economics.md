# PRD: Mine economics (spec 009)

## Summary
A mine today is one decision. It has no size, runs at one fixed rate, costs the same to run on its last day as on its first, and on a good deposit never ends. Spec 008 made prices bite; this spec makes the mine itself something to manage.

A mine is built at one of three sizes. A larger mine costs more to build, digs faster and costs less per tonne to run. A mine can later be enlarged one size at a time, for more than it would have cost to build at that size in the first place. As a mine digs, its running cost rises with the total tonnage it has extracted, so every mine, however rich the deposit, eventually stops paying. A mine can be closed for good by paying a reclamation cost, and a mine that has run out keeps costing money until it is closed.

Rival companies size, enlarge and close mines under the same rules. The balance runner reports and gates the result, on two ranges of seeds.

It serves journey J-002 (a player sizes, works and closes a mine) and is system 2 of the Earth strategy (docs/design/earth-strategy.md). Saves and traces from earlier builds stop loading.

## Scope
### In Scope
- Three mine sizes in data, each with an output, build-cost and running-cost multiple.
- Size chosen in the build order; size shown wherever a mine is shown.
- An enlarge order: one size up, costed at the difference plus a premium, taking time, with the mine producing at its old size meanwhile.
- Running cost that rises with tonnes dug so far (normalised per ore and by size), with slope and ceiling in data.
- The existing output fall-off's constant moved from code into data.
- A close order with a reclamation cost; closing is final.
- A depleted mine carries the mothballed keeping cost until closed.
- The mine panel shows size, tonnes dug, today's running cost against its starting cost, and the cost to enlarge and to close.
- AI: chooses a size, enlarges, and closes, with knobs in `data/ai.json`.
- Runner: sizes chosen, enlargements, closures and mine life reported; gates on seeds 1 to 50 and 51 to 100.
- Retuning spec 008's numbers where the new rules require it, with reasons and sweep evidence.
- Golden hashes updated once; older saves refused.

### Out of Scope
- Shafts, layouts, crews; ore grade, depth bands, survey tiers; processing.
- Land holding fees, haulage, infrastructure (system 3); finance changes (system 4).
- Selling a mine or plot; making a mine smaller; mining a closed plot again.
- A new warning as costs rise.
- New research projects.
- Space, conflict, multiplayer.

## Requirements
### BR-001: Sizes in data
`data/balance.json` defines exactly three mine sizes in order (small, standard, large). Each has an output multiple, a build-cost multiple and a running-cost multiple. The loader requires each multiple to be positive, and output and build cost to rise with size. Standard is 1, 1, 1, so a standard mine at zero depth is today's mine.

### BR-002: Economies of scale
Running cost per tonne falls with size and build cost per tonne of daily output does not fall. A data test checks both.

### BR-003: Size at build
`BuildMine` carries a size. Build cost is today's area-based cost times the size's build multiple. A build order without a valid size is rejected.

### BR-004: Output
A mine's daily output per ore is today's formula times its size's output multiple. The fall-off constant (0.5) is read from data.

### BR-005: Depth
A mine's depth is the sum, over ores, of tonnes it has extracted divided by that ore's reference tonnage (in `data/resources.json`) and by its current size's output multiple. Depth uses extracted tonnes only, never the hidden deposit.

### BR-006: Running cost rises with depth
Running cost is the spec 008 cost times the size's running-cost multiple times a depth multiple of `1 + slope × depth`, capped at a ceiling. Slope and ceiling are in data. The mothballed share applies after it. Margin, break-even and the cost quotes all use the same function (spec 008, decision record 002).

### BR-007: Enlarging
An enlarge order moves an Operating or Mothballed mine one size up. It costs the difference in build cost between the two sizes times a premium above 1 (in data), takes a set number of days (in data), and the mine keeps its old size until the works finish. One enlargement at a time; a large mine cannot be enlarged. If the mine is depleted or closed before the works finish, the enlargement lapses without refund.

### BR-008: Closing
A close order ends a Building, Operating, Mothballed or Depleted mine for good. It costs a set fraction (in data) of the mine's build cost at its current size, before research discounts. The order is rejected when the owner cannot pay. A closed mine produces nothing, costs nothing, and its plot cannot be mined again.

### BR-009: Exhausted mines cost money
A Depleted mine pays the mothballed keeping cost each day until it is closed. A mine closed by bankruptcy is charged nothing.

### BR-010: Orders through the queue
Enlarge and close orders are queued, counted in Committed, cancellable until the tick, and recorded in the trace, exactly as spec 007's orders are.

### BR-011: Notices
Starting an enlargement and closing a mine each raise a notice. Closing is public (rivals see it, as they see a mine started); enlargement started and finished are private.

### BR-012: The view carries it
Each mine in the company view has its size, pending enlargement and finish day, depth multiple today, running cost at zero depth, the cost to enlarge and the cost to close. The view offers a per-size quote (build cost, output, running cost) for any plot the company owns. Neither the client nor the AI recomputes these (R-001).

### BR-013: The player can read it
The plot panel offers the three sizes with their quotes when building. An owned mine's panel shows its size, tonnes dug, today's running cost beside its starting cost, and Enlarge and Close with their costs. Mine rows show the size. The layout probe passes at both frame sizes.

### BR-014: The AI plays the same game
The AI chooses the size with the best valued return for its survey and cash, enlarges a mine that would pay back within its horizon, and closes a mine that is depleted or that its valuation says will not pay again. All thresholds are in `data/ai.json`. The hard-coded 1.3 extraction margin in the AI's valuation moves to data, since this spec rewrites that function.

### BR-015: The runner measures it
The sweep reports, per seed: mines built at each size, enlargements, mines closed by order, median mine life (built to closed or depleted), and the longest stretch without a mine order per company.

### BR-016: Gates
The gated sweep passes on seeds 1 to 50 and on seeds 51 to 100:
- every gate of spec 008 (bite, idled mines, cash at year 10, bankruptcy, depletion, first mine day, leader over trailer);
- median mine life between 1,800 and 2,520 days, now gated;
- no size is chosen in more than 70% or fewer than 10% of AI builds;
- at least 40 of 50 seeds have a mine closed by order;
- at least 25 of 50 seeds have an enlargement.

### BR-017: Tuning recorded
Every changed number has its reason and its sweep evidence in the spec's decisions, as spec 008's AC-013 required.

### BR-018: Determinism
New mine state is integer or fixed-point and is saved. The golden hashes are updated once with the reason in `docs/DESIGN.md`. Saves from older content are refused.

### BR-019: Prose
Every new string shown by code is in the catalogue and goes through the prose pipeline. Size names are labels and are not polished.

## Acceptance Criteria
1. **AC-001: Sizes load.** Three sizes load from `data/balance.json`; the loader rejects a missing size, a non-positive multiple, and sizes whose output or build cost do not rise. A data test checks economies of scale (BR-002) and that standard is 1, 1, 1.
2. **AC-002: Size at build.** Sim tests show a mine built at each size pays the area cost times that size's build multiple and produces base output times its output multiple; a build order with an invalid size is rejected.
3. **AC-003: Depth and cost.** Sim tests show: depth is zero for a new mine; it rises with tonnes extracted, normalised per ore and by size; running cost follows `1 + slope × depth` up to the ceiling; margin, break-even and quotes move with it; two mines on deposits of different true size but equal tonnes dug have equal cost.
4. **AC-004: Enlarging.** Sim tests show: cost is the build-cost difference times the premium; the mine produces at its old size until the finish day and at the new size after; a second enlargement, a large mine and a mine that is not Operating or Mothballed are rejected; a mine depleted mid-works loses the enlargement without refund.
5. **AC-005: Closing.** Sim tests show: closing charges the set fraction of build cost; it is rejected when unaffordable; a closed mine produces nothing and costs nothing; its plot rejects a new build; a depleted mine pays the keeping cost daily until closed; a mine closed by bankruptcy is charged nothing.
6. **AC-006: The player chooses a size.** The plot panel offers three sizes, each with build cost, daily output and running cost from the view, and Build places the order at the chosen size. As the player, navigate from the Plots tab via an owned, unmined plot's panel, compare the three sizes and press Build on one, to open a mine sized to the survey and to their cash. Live at release; fully-functional means: the order appears as a waiting chip with the chosen size's cost in Committed, and when the clock runs the mine appears at that size with that cost taken from cash.
7. **AC-007: The player sees a mine age.** An owned mine's panel shows size, tonnes dug, and today's running cost beside its starting cost; mine rows show the size. As the player, navigate from the Dashboard via the plots-and-mines table to an owned mine's panel and read its size, tonnes dug and running cost against its starting cost, to see how far the mine has declined. Live at release; fully-functional means: the figures equal the sim's values for that mine, and the running cost shown rises as the mine digs.
8. **AC-008: The player enlarges a mine.** As the player, navigate from the Plots tab via an owned mine's panel and press Enlarge, which shows its cost and finish time, to raise the mine's output. Live at release; fully-functional means: the order queues with its cost in Committed and can be cancelled; after the tick the panel shows the works and their finish day; on that day the mine's size and output change.
9. **AC-009: The player closes a mine.** As the player, navigate from the Plots tab via an owned mine's panel and press Close, which shows the reclamation cost, to end a mine that no longer pays. Live at release; fully-functional means: the order queues and can be cancelled; after the tick the cost is taken, the mine shows as closed, its daily cost stops, and the log records the closure.
10. **AC-010: The AI plays the same game.** Sim tests show the AI picks different sizes for different surveys and cash, enlarges a paying mine, and closes a depleted mine and one its valuation writes off; a changed knob changes the behaviour.
11. **AC-011: The runner reports it.** The sweep's JSON carries sizes built, enlargements, closures by order, median mine life and the longest stretch without a mine order.
12. **AC-012: Both gated sweeps pass.** `sim_runner --sweep 50 --years 20 --ai 1 --sanity` exits zero, and so does the same command with `--seed 51`, with every gate in BR-016. BLOCKER: If the BR-016 gates cannot all be met on both seed ranges, which gate gives way? — owner: binaryknight.
13. **AC-013: Orders behave like orders.** Session tests show enlarge and close orders queue, count in Committed, cancel, and appear in the trace and replay.
14. **AC-014: Determinism recorded.** `kGoldenPlayed` is updated once (and `kGolden` if it moves); `docs/DESIGN.md` gives the reason; a save from different content is refused.
15. **AC-015: Tuning recorded.** The spec's decisions hold a table of every changed value with its reason and sweep evidence.
16. **AC-016: Prose through the pipeline.** Every new string is in `draft.tsv`, `polished.tsv` and `data/strings.json`, registered in the loader; the parity test passes; sentences are polished by agy.

Sources in play: code, spec, prototype; conflicts resolved per source-of-truth-conflict-rule.

## Edge Cases
1. **A mine closed while Building.** Allowed. The reclamation cost applies to the size ordered; the build cost is not refunded.
2. **A mine mothballed while being enlarged.** The works continue and finish on their day; the mine reopens at the new size.
3. **A mine depleted or closed during the works.** The enlargement lapses, without refund.
4. **Enlarging changes depth.** Depth divides by the current size's output multiple, so depth falls when a mine is enlarged and its running cost per day steps accordingly. This is intended: a bigger operation reaches the same ground more cheaply. The test in AC-004 pins the step.
5. **Depth at the ceiling.** The cost multiple stops rising; the mine may still be profitable at a high price.
6. **An ore unlocked late.** Its tonnes start counting toward depth from the day it is first dug; nothing is back-dated.
7. **A mine that found nothing.** It has zero depth and zero output, pays the running cost of spec 008's fallback rule, and can be closed like any other.
8. **Cannot afford to close.** The order is rejected with the usual insufficient-funds reason; the mine keeps costing its keeping cost. The player can borrow, sell stock, or wait.
9. **Bankruptcy.** Mines are closed as today and no reclamation is charged, pending or not; pending enlargements lapse.
10. **A close order and an enlarge order queued for the same mine in one day.** They are applied in the order placed; whichever comes second is rejected by the sim if the first made it invalid.
11. **Close order for a mine with ore still in stock.** Stock belongs to the company, not the mine, and is unaffected.
12. **The losing-mine count on a depleted mine.** A depleted mine has no output, so spec 008's losing-day count does not run; its cost shows in the panel and in Finance.
13. **Research that changes build cost.** The build discount applies to building and enlarging. Reclamation is a fraction of the undiscounted cost, so research does not make closing cheaper.
14. **An old save.** Refused by the content fingerprint, with the existing message.
15. **The AI and reclamation.** The AI must hold back cash for closing a depleted mine before it buys land, or it accumulates dead mines; a sim test covers a company with a depleted mine and little cash.
16. **Deck frame width.** The size goes in the mine rows as a short label; if it does not fit at 1280x800 it goes in the panel only.

## Research Notes
See `research/codebase.md` in the feature directory. In short: a mine has no size or depth today; build cost is plot area times a rate; output is one fixed rate per ore with a fall-off to half as the hidden deposit empties; there is no close order and a plot holds one mine for ever; spec 008's tuning sits close to its gates, so this spec retunes the economy. Taylor's rule (daily rate grows with about the 0.75 power of reserves) and economies of scale informed the three sizes; closing cost is a game number.
