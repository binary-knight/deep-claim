# PRD: The race for the program (spec 010, first slice of the core loop)

## Summary
Deep Claim gets an ending and a reason to hurry. Humanity has a deadline, 30 game years away, and two companies race to found a space program before it. The program has three stages; each needs cash, a research project, and goods the company has produced itself. The first company to complete the third stage founds the program and wins. If the rival gets there first the player loses, or finishes as a lesser partner if they had completed two stages. If nobody does by the deadline, everyone loses.

Ore gains two uses beyond selling it: delivering it to the program, and feeding plants that turn it into steel and launch structures the later stages need. Mines gain a life (spec 009's sizes, decline, enlarging and closing). The market is resized so one mine no longer floods it.

It is built in four playable steps, with a playtest after the first: the race; plants; mine life; tuning. It serves journey J-003 and implements the first slice of `docs/design/core-loop.md`. Saves and traces from earlier builds stop loading.

## Scope
### In Scope
- Step 1, the race: deadline, three-stage program, deliveries, stage research, win and lose, rival progress on screen, a Program screen, an end screen, the AI racing, a provisional market resize, runner measures, the small playtest 4 fixes.
- Step 2, plants: steel and launch structures as goods, a smelter and a fabrication plant as nodes with flows, a Production screen, the AI building and running them.
- Step 3, mine life: spec 009 (`spec/mine-economics.md`) as approved.
- Step 4, tuning: market depth (one mine moves its price 3% or less), a limit on sales per day, race gates on two seed ranges.

### Out of Scope
- Crews and equipment; regional demand; more than one rival or ending; the threat's reveal; the AI relationship.
- Buying goods for the program; contracts; land fees; conflict; save slots.
- Anything after the program is founded.

## Requirements
Step 1: the race.

### BR-001: Deadline
A game ends on a deadline day set in data (day 10,800). The days remaining are always on screen.

### BR-002: Program
`data/program.json` defines three stages in order. Each has a cash cost, one research project and required tonnes of named goods. Stage 3 requires lithium.

### BR-003: Deliveries
A deliver order moves tonnes of a good from company stock to the company's program, up to what the current stage still needs. It cannot be undone. Delivered goods pay no storage and are not market supply. A company cannot buy goods for its program.

### BR-004: Stage research
Each stage's project can start only when the previous stage is complete. Starting it raises a public notice.

### BR-005: Completing a stage
A complete-stage order succeeds when the research is done, the deliveries are met and the cash is available. It charges the cost and raises a public notice.

### BR-006: Ending
The first company to complete stage 3 founds the program and the game ends. The other company finishes as a lesser partner if it has completed two stages, and has lost otherwise. If the deadline arrives first, both lose. A bankrupt player loses as today; if the rival goes bankrupt the player still races the deadline.

### BR-007: The race is public
Each company's current stage, share of deliveries met and research state are in every view. The Dashboard shows the deadline and both companies' progress.

### BR-008: Program screen
A new tab shows each stage's needs, what has been delivered, what is in stock, and Deliver, Start research and Complete stage. Orders go through the pending queue.

### BR-009: End screen
When the game ends the player sees the result, both companies' stage dates and the key figures. The trace records the outcome.

### BR-010: Stops
The clock stops, by default, when the rival starts a stage's research or completes a stage. The rule can be switched off.

### BR-011: The AI races
The AI holds back what its current stage needs, starts research, completes stages, and favours land holding ores its coming stages need. Its knobs are in `data/ai.json`.

### BR-012: Provisional market size
Demand is raised so one standard mine moves its own price by about 5% or less, recorded as provisional.

### BR-013: Runner
The sweep reports, per seed: who founded the program and when, each stage's day for each company, and how the game ended. Step 1 gates: the game ends by a founding in at least 80% of seeds; neither company wins more than 70%; at most one bankruptcy in 50 seeds.

### BR-014: Playtest 4 fixes
The losing-mine notice needs a loss above a set share of running cost and is not re-armed by a single profitable day (both in data). The mothball notice says whether the mine was mothballed or reopened. The Market tab shows exact prices.

Step 2: plants.

### BR-015: Goods
Steel and launch structures are goods with stock, a market and a price, defined in data like ores.

### BR-016: Plants
A plant type in data has inputs and outputs per day, build cost and time, and running cost. A plant is built in a region by order, without a plot. It draws inputs from company stock and adds outputs to it, runs only when its inputs are there, and can be idled and closed.

### BR-017: The chain
A smelter makes steel from iron ore. A fabrication plant makes launch structures from steel and nickel. Stage 2 requires steel; stage 3 requires launch structures as well as lithium.

### BR-018: Production screen
A new tab shows mines, plants, stock and outlets as nodes with daily flows between them.

### BR-019: The AI builds and runs plants
The AI builds and runs the plants its coming stages need.

Step 3: mine life.

### BR-020: Mine life
Requirements BR-001 to BR-014 of `spec/mine-economics.md` apply as approved, with its design and decision records.

Step 4: tuning.

### BR-021: Market depth
One standard mine moves its own price by 3% or less at steady demand; the runner measures and gates it.

### BR-022: Sales per day
A company can sell at most a set share of a good's base demand in one day, by any route; the rest stays in stock.

### BR-023: Final gates
On seeds 1 to 50 and 51 to 100: BR-013's race gates; the median founding falls in a set window before the deadline; mines are still idled for price in most seeds; a typical mine is profitable for most of its life.

### BR-024: Tuning recorded
Every tuned value has its reason and sweep evidence in the spec's decisions.

Throughout.

### BR-025: Playable steps
Each step ends green and playable. Step 2 does not begin until the owner has played step 1 and said to go on.

### BR-026: Determinism
Saved state is integer or fixed-point; golden hashes move once per step with the reason recorded; older saves are refused.

### BR-027: Prose
Every new string goes through the prose pipeline.

## Acceptance Criteria
1. **AC-001 Program data.** `data/program.json` loads three stages; the loader rejects a missing stage, a non-positive cost or tonnage, an unknown good or research project, and a stage 3 without lithium.
2. **AC-002 Deliveries.** Sim tests: a delivery moves stock to the program up to the stage's remaining need; excess, a good the stage does not need, and more than is in stock are rejected; delivered goods pay no storage and do not move the market.
3. **AC-003 Stages.** Sim tests: stage research cannot start before the previous stage is complete; a stage cannot be completed without research, deliveries and cash; completing charges the cost once and raises a public notice.
4. **AC-004 Endings.** Sim tests cover each ending: founding first; the rival founding first with the player at two stages (lesser partner) and at fewer (lost); the deadline with nobody finished; bankruptcy of either side.
5. **AC-005 The player sees the race.** As the player, navigate from any tab via the Dashboard's deadline and progress bars to see days remaining and both companies' stage and progress, to know whether they are ahead. Live at step-1; fully-functional means: the figures equal the sim's for both companies and change the day a delivery, research start or stage completion is applied.
6. **AC-006 The player advances the program.** As the player, navigate from the Dashboard via the Program tab, read what the current stage needs against stock, and use Deliver, Start research and Complete stage, to move their program forward. Live at step-1; fully-functional means: each order queues with its cost in Committed and can be cancelled; after the tick the stock, delivered amounts, research and stage shown match the sim; a stage completes only when all three needs are met.
7. **AC-007 The player is told about the rival.** As the player, navigate from any tab with the clock running via the notice that stops the clock when the rival starts a stage's research or completes a stage, and from the Stops menu switch that rule off or on, to react to the rival in time. Live at step-1; fully-functional means: the log line names the rival and the stage and the clock stops that day; with the rule off the line still appears and the clock runs on.
8. **AC-008 The game ends.** As the player, navigate from the final day's notice via the end screen to read the result, both companies' stage dates and their own key figures, to understand how the race was decided. Live at step-1; fully-functional means: the end screen appears on the day the sim ends the game, shows the same outcome the trace records, and no further day can be played.
9. **AC-009 The AI races.** Sim tests: the AI holds back stage goods from sale, starts research, completes stages, and values land with a needed ore above the same land without it; a changed knob changes the behaviour. In a sweep the AI founds the program before the deadline in at least 80% of seeds.
10. **AC-010 Step 1 sweep.** The runner's JSON carries winner, founding day, stage days and outcome per seed; `sim_runner --sweep 50 --years 30 --ai 1 --sanity` exits zero with BR-013's gates.
11. **AC-011 Playtest 4 fixes.** Tests show the losing-mine notice is not raised for a loss under the set share nor re-armed by one profitable day; the mothball notice names the new state; the probe shows exact prices on the Market tab.
12. **AC-012 Goods and plants.** Loader and sim tests: steel and launch structures load as goods; a plant consumes and produces at its rates only when inputs are in stock; idled and closed plants do neither; building charges the cost and takes the build time.
13. **AC-013 The player builds a chain.** As the player, navigate from the Dashboard via the Production tab, build a smelter and a fabrication plant in a region, and read the nodes and daily flows, to turn ore into goods the program needs. Live at step-2; fully-functional means: the build orders queue and cost what the screen says; once built, the flows shown equal what the sim moved that day; stock of steel and launch structures rises and can be delivered or sold. BLOCKER: Has the owner played step 1 and agreed to go on? — owner: binaryknight.
14. **AC-014 Stages need the chain.** With step 2's data, stage 2 cannot be completed without steel nor stage 3 without launch structures; the AI builds both plants and still founds the program in at least 80% of seeds.
15. **AC-015 Mine life.** Acceptance criteria 1 to 11 and 13 of `spec/mine-economics.md` hold. Its player-facing criteria are live at step-3.
16. **AC-016 Depth.** The runner reports one standard mine's own-price impact per good; all are 3% or less.
17. **AC-017 Sales per day.** Sim tests: manual sales, sell policies and cover-costs sales together never exceed the daily share; the remainder stays in stock.
18. **AC-018 Final sweeps.** The gated sweep exits zero on seeds 1 to 50 and 51 to 100 with BR-023's gates.
19. **AC-019 Each step is green.** All presets pass, format and tidy are clean, the layout probe passes at both frames, and golden hashes are updated once with the reason in `docs/DESIGN.md`.
20. **AC-020 Tuning recorded.** The spec's decisions hold every tuned value with its reason and sweep evidence, per step.
21. **AC-021 Prose through the pipeline.** Every new string is in the catalogue through the pipeline; the parity test passes.

Sources in play: code, spec, prototype; conflicts resolved per source-of-truth-conflict-rule.

## Edge Cases
1. Both companies complete stage 3 on the same day: the lower company index is applied first, as with all orders; recorded in the outcome as a same-day finish.
2. A delivery and a sale of the same stock queued the same day: applied in the order placed; the second is cut to what is left or rejected.
3. Stock held for the program is still stock: it pays storage until delivered, and the cover-costs rule can sell it. The Program screen warns when cover-costs is on.
4. The rival goes bankrupt: its program stops; the player races the deadline alone.
5. The player completes stage 3 while insolvent: the stage's cost must be available cash; borrowing counts.
6. The deadline falls on the day a stage-3 order is applied: the order is applied first.
7. A stage needs a good the company cannot yet produce (lithium locked, no plant): the Program screen says what is missing and what unlocks it.
8. A plant with inputs for part of a day's run produces in proportion, in whole tonnes.
9. A plant closed with goods in stock: stock is unaffected.
10. An old save or trace: refused by the content fingerprint.
11. A game left running after its end: the clock cannot be restarted; a new game can be started.
12. Sweeps now run 30 years: the gated sweep's time is checked against the CTest ceiling at each step.

## Research Notes
See `research/market-depth.md` (price arithmetic and sweep experiments), `docs/playtests/2026-10-11.md` and `docs/design/core-loop.md`. A game today ends only on bankruptcy; a company sees only a rival's name, plot count and bankruptcy; there is no way to set stock aside or give it away; research is ten one-off projects with lithium unlocked by the dearest; at spec 008's market sizes one mine is 10 to 25% of demand.
