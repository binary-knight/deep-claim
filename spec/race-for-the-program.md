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

## Decisions
Recorded on 2026-10-11, after the step 1 build. Steps 2 to 4 are not built. The requirements and acceptance criteria above are left as approved; where the build differs from their text or from the design, the difference is listed under "Departures". The architectural summary, the tick order and the reason for each golden hash move are in `docs/DESIGN.md`, "The race for the program (spec 010, step 1)". The owner played step 1 on 2026-10-18 (playtest 5) and decided under "Owner decisions" at the end of this section. The code review and the Codex review are under "Reviews (2026-10-11, step 1)" below; entries marked "after review" were changed in that fix round. The security review and the spec-compliance check followed on 2026-10-18.

Build decisions (made by the implementing agents and accepted by the build conductor, not by the owner):
- 2026-10-11: the program and the deadline are one content file, `data/program.json`, loaded into `Content::program` after resources and techs. The loader requires exactly three stages, and each stage's project must be the tech whose `requires_program_stage` equals the stage's position counting from zero.
- 2026-10-11: one private function in `sim/src/program.cpp` decides whether a stage may be completed. The order's validation and the view's `ready` flag both answer from it.
- 2026-10-11: a company's delivery progress is the mean over the stage's requirements of the share of each delivered, in thousandths. Each good counts by its own need, so a small requirement weighs as much as a large one. Research and cash are not part of it.
- 2026-10-11: a company that is bankrupt when another founds the program has lost, whatever it had completed.
- 2026-10-11, after review: the day a game ends on is not played out. The world freezes straight after that day's orders (tick step 1, where an order completing the last stage founds the program and the orders queued behind it are refused) or after the deadline check that follows them (step 1b). No later step of that tick runs and the day does not advance, so the outcome's day always equals the state's day. The tick order is: commands, program (deadline check), auctions, surveys, mines, sell policies, markets, finance, research, events, day. As first built the rest of the day was played and the outcome's day was one less than the state's.
- 2026-10-11: a finished world refuses every order as `GameEnded`, checked per order, so orders queued behind the one that founds the program are refused the same day. A tick on a finished world advances nothing.
- 2026-10-11, after review: the rule for a company that did not found the program is one function, `programResultBesideFounder` in `sim/include/sim/program.h`. The ending applies it and save validation checks against it.
- 2026-10-11, after review: save validation checks the outcome against company state. The founder has every stage complete, is solvent, is the only company complete, founded on or before the deadline, and its last stage day equals the outcome's day. Other companies' results follow the rule. A no-founder ending has every company lost on the deadline day. A world still being played has no complete company and a day at or before the deadline. Every completed stage has its research among the company's completed techs. Stage days are at least 1. An ended world may hold records from its last day's orders, and validation allows them only when the outcome is ended.
- 2026-10-11, after review: `Session::submit` returns a bool and refuses orders once the game is over.
- 2026-10-11, after review: the AI delivers at least one tonne when it keeps any share, holds stock and the stage needs it, so a share that rounds down to nothing does not starve the program.
- 2026-10-11, after review: the trace writes one `end` record per game: at the moment the game ends, when a new game replaces it, or on close.
- 2026-10-11, after review: every `GameEnded` rejection reads the same, whatever the order was: `notice.command_rejected` with the `reject.game_ended` reason ("Couldn't carry out the order: the game is over and no more orders are taken." as drafted; polished on 2026-10-18).
- 2026-10-11, after review: the loader refuses a stage with no requirements.
- 2026-10-11: `sim::researchOpenTo` is the one rule for whether a project can be started, money and a busy lab aside. The view carries its answer per project (`CompanyView::researchOpen`); the AI and the client read it and keep no copy of the rule.
- 2026-10-11: a company's stage days are saved state (`Company::programStageDays`) and are in the view for every company, so the end screen is right in a long game and after a load.
- 2026-10-11: the AI keeps stock for a stage by setting the reserve of a standing sell order to what the stage still needs, where the company has such orders, and by delivering before it sells. It places its program orders ahead of every other rule.
- 2026-10-11: the runner stops a seed when its game ends and prints days counted from 1. After review, `founded_day` and `end_day` are the outcome's day plus one, like `stage_days`, and the ending tick is not sampled. A run of exactly the deadline's length makes one tick more, so `--years 30` always reaches an outcome. Orders refused because the game had just ended do not count against the AI.
- 2026-10-11: a third golden test pins a scripted race (`kGoldenRace`), because neither older golden run is certain to reach the program's rules.
- 2026-10-11: the player's own bankruptcy ends the game in the session with the outcome still open. The trace's `end` record then says `bankrupt`.

Departures (build decisions; the requirements and criteria above are not rewritten):
- 2026-10-11, design: `deadline_day` lives in `data/program.json`, not `data/balance.json`. BR-001 asks only that it is set in data.
- 2026-10-11, design: the lesser-partner threshold is a named constant, `kLesserPartnerStages = 2` in `sim/src/program.cpp`, not data. BR-006 fixes it at two stages, so it is part of the rule and not a tuning number.
- 2026-10-11, design: the mine field for the losing-mine notice is `lossClearDays`, not `profitDays`. It counts consecutive days that are not losing days, which includes a day at break-even and a day with a loss under the set share.
- 2026-10-11, design: the single `MineSwitched` notice was replaced by two kinds, `MineMothballed` and `MineReopened` (BR-014).
- 2026-10-11, design: a rejected order's notice carries which kind of order it was (`RejectedOrder`: other, delivery, stage completion), so the log can word a refused delivery or stage completion.
- 2026-10-11, design: `programStageReady` and `programProgressPermille` take the world state and a company id.
- 2026-10-11, design: the deadline is shown in the status strip under the header, on every tab, not in the header row. It did not fit there. BR-001 is met: the time left is on screen on every tab.
- 2026-10-11, design: stage days were added to saved state and the view after the end screen needed them (BR-009).
- 2026-10-11, design: the AI has a fourth knob, `program_research_progress`. The design listed three.
- 2026-10-11, design and spec 008: the runner gates only the opening, validity and race checks (`opening_has_mineable_plots`, `median_first_mine_day`, `world_valid`, `ai_commands_valid`, `founded_before_deadline`, `win_balance`, `bankruptcies`). The checks of specs 005 to 008 that assumed a 20-year open-ended game are reported and no longer gated, spec 008's bite checks among them. Spec 008's decision record 003 no longer describes the gates in force. The record is not edited.
- 2026-10-11, design: a delivery above the stage's remaining need is refused, as AC-002 asks, where the design's layer table says it is capped. The AI and the Program tab's Deliver button cut the order to the need before giving it.
- 2026-10-11, design: the runner's own default is still 20 years. The gated CTest passes `--years 30`.
- 2026-10-11, edge case 1: a same-day finish is not recorded in the outcome. `Outcome` has no field for it. The lower company index is applied first and founds the program; the other's order is refused as `GameEnded`. Raised by the Codex review and declined: the first order applied wins (see "Reviews", Codex 2).
- 2026-10-11, design, after review: the program step (the deadline check) follows the day's orders. The design put it after events. The reason is that save validation reads the state's day as a between-ticks day, so a world frozen after a full pipeline would not load.
- 2026-10-11, BR-026 and AC-019: the golden hashes moved more than once in the step. `kGolden` moved twice and `kGoldenPlayed` four times, once for each change to state or behaviour as the tasks landed. `kGoldenRace` moved once after review, from `0x588363dbb9c693f3` to `0xae14b05ab3e56760`, when the world began to freeze on the day it ends. Each move and its reason is in `docs/DESIGN.md`.
- 2026-10-11, BR-027 and AC-021: the new strings are in the catalogue and registered in the loader, but about 30 sentences did not go through the agy polish pass (see "Open after step 1"). Neither is met until they have. Resolved on 2026-10-18: see "Owner decisions" below.
- 2026-10-11, BR-011: the AI's `program_ore_land_weight` ships at 1, which is neutral, so the shipped AI does not prefer land holding an ore its coming stages need. The rule is built and tested at 1.5 (AC-009); any value above 1 sent both companies after the same opening plot in seed 1 and delayed the first mine from day 47 to day 92 (F8). Listed here on 2026-10-18 after the spec-compliance check found it missing from this block.

Tuned and provisional values (2026-10-11; BR-024, AC-020). Every value changed or added in step 1. All of them are provisional: step 4 sets the final market depth and gates.

The evidence is from the tuning task's report (task 016): 41 variants, each run on seeds 1 to 50 and on seeds 51 to 100 with the release runner, `sim_runner --sweep 50 --years 30 --ai 1 --sanity`. The table's last column cites these findings by number. The report gives findings, not a reason for each value; where the Reason column goes beyond a finding it is this record's reading of the report. Nothing in the repository reproduces the intermediate variants.
- **F1.** A deeper market alone shortens the race: the median founding fell to about day 2,490 from about 3,340, with 19 to 21 mines per company.
- **F2.** Scaling the whole program by k barely slows it. k=2 gave a median of 2,785, k=4 gave 3,340, and k=8 gave about 4,700 to 4,800 with 41 and 46 seeds founded and a long tail.
- **F3.** Cash never binds the AI. It ends with 2bn to 6bn dollars, so stage costs do not move it: costs of 150M and 500M gave the same race. The costs were sized for a human.
- **F4.** Nickel was the bottleneck, not lithium. The world holds about 0.78M t of nickel against 42M t of iron. Cutting nickel to 0.4 of the scaled amount at k=8 took deadline seeds from 9 and 4 to 1 and 0. Changing lithium did nothing.
- **F5.** Tonnage alone cannot reach the target window: at k=16 with reduced nickel the median was about 4,970 to 4,990 with 42 and 46 seeds founded.
- **F6.** Ore supply is finite: land is all listed by year 10 and unsold plots are withdrawn after six rounds.
- **F7.** Research durations move the median founding almost one for one, with a tight spread.
- **F8.** The AI knobs did little.
- **F9.** The same few seeds stall at stage 3 on a scarce ore. With the final values only seed 26 does.

"Old" is the value before spec 010 for `resources`, and the first provisional value set earlier in this step for `program` and `techs`. Own-price impact is 1 - (D / (D + q))^e, with D the base demand, q one standard mine's base output and e the elasticity.

| File | Key | Old | New | Reason | Sweep evidence |
| --- | --- | --- | --- | --- | --- |
| resources | iron `base_demand_tonnes_per_day` | 4,000 | 12,000 | BR-012: one standard mine's own-price impact falls from 13.3 to 4.80 percent | F1. A data test limits the impact to 6 percent |
| resources | copper `base_demand_tonnes_per_day` | 540 | 1,800 | as iron: 14.6 to 4.80 percent | F1; the data test |
| resources | nickel `base_demand_tonnes_per_day` | 90 | 600 | as iron: 26.0 to 4.80 percent | F1; the data test |
| resources | lithium `base_demand_tonnes_per_day` | 20 | 200 | as iron: 36.0 to 4.82 percent | F1; the data test |
| program | `deadline_day` | none | 10,800 | new; the spec's value (BR-001) | not tuned |
| program | stage 1 `cost` | 5M | 10M | sized for a human, since cash does not bind the AI | F3 |
| program | stage 1 iron, copper tonnes | 60k, 8k | 150k, 20k | a deeper market shortens the race, so the program asks for more | F1, F2 |
| program | stage 2 `cost` | 15M | 60M | as stage 1 | F3 |
| program | stage 2 iron, copper, nickel tonnes | 150k, 20k, 6k | 600k, 80k, 20k | as stage 1; nickel is raised less than iron and copper because it is the scarce ore | F2, F4 |
| program | stage 3 `cost` | 30M | 150M | as stage 1 | F3: 150M and 500M gave the same race |
| program | stage 3 iron, copper, nickel, lithium tonnes | 200k, 30k, 12k, 2k | 800k, 120k, 36k, 8k | as stage 2 | F2, F4, F5, F9 |
| techs | `program_stage_1` cost / days | 2M / 240 | 3M / 480 | tonnage alone cannot set the pace; the research clock does | F5, F7; cost: F3 |
| techs | `program_stage_2` cost / days | 6M / 360 | 10M / 1,680 | as `program_stage_1` | F5, F7; cost: F3 |
| techs | `program_stage_3` cost / days | 12M / 480 | 25M / 2,520 | as `program_stage_1` | F5, F7; cost: F3 |
| ai | `program_reserve_share` | none | 0.5 | new; chosen with the buffer below for no bankruptcies | a 50-seed, 20-year sweep had bankruptcies in 5 seeds at share 0.25 and buffer 1M, 1 at 0.5 and 3M, 0 at 0.5 and 6M, and 11 at share 1 and buffer 1M |
| ai | `program_cash_buffer` | none | 6,000,000 | as above | as above |
| ai | `program_ore_land_weight` | none | 1 | new; 1 is neutral | any value above 1 made both companies go for the same opening plot in seed 1 and delayed the first mine from day 47 to day 92. F8 |
| ai | `program_research_progress` | none | 0.5 | new | F8; no finding specific to this value was recorded |
| balance | `mine_loss_notice_min_share` | none | 0.05 | new (BR-014) | not tuned |
| balance | `mine_loss_rearm_days` | none | 30 | new (BR-014) | not tuned |

Mine running costs, elasticity, starting capital and loans are unchanged.

The final sweeps, as reported by the tuning task, on the shipped data. Both exit 0 and took 38 to 39 seconds each. Days are the runner's, counted from 1.

| Measure | Seeds 1 to 50 | Seeds 51 to 100 | Gate |
| --- | --- | --- | --- |
| Seeds ended by a founding | 49 | 50 | at least 40 |
| Wins, company 0 and company 1 | 20 and 29 | 25 and 25 | at most 35 each |
| Seeds with a bankruptcy | 0 | 0 | at most 1 |
| Seeds that reached the deadline | 1 (seed 26) | 0 | |
| Founding day: min, median, max | 6,101 / 6,285 / 6,864 | 6,085 / 6,318.5 / 6,998 | |
| Winner's stage days, median: stage 1, 2, 3 | 1,288 / 3,383 / 6,285 | 1,331 / 3,425 / 6,318.5 | |
| Median day company 0's first mine operates | 227 | 227 | under 250 |
| Rejected commands | 0 | figure not reported; the gated check passed (exit 0) | 0 |
| Mean price over base: iron, copper, nickel, lithium | 1.03, 1.14, 1.47, 2.08 | 1.01, 1.11, 1.50, 2.06 | reported |
| Days below break-even, permille: iron, copper, nickel | 19, 16, 72 | 24, 20, 60 | reported (spec 008 asked for 100 to 500) |
| Mines built per company | 43 and 44 | 44 and 45 | |
| Company 0's cash at year 10, median | 851M | 1,007M | reported |

Foundings fall in years 17 to 20. In all 99 founded seeds the loser had completed two stages, so it finished as a lesser partner; it was a median 149 days behind at stage 2 on seeds 1 to 50 and 194 on seeds 51 to 100. Before tuning, on the first provisional values, the median founding was day 3,337 and 3,341, with wins of 19 and 31 and of 28 and 22.

The table above is from the tuning task, before the review fix round. After that round both release sweeps were run again and exit 0. Seeds 1 to 50: 49 founded, wins 20 and 29, median founding day 6,285, 1 deadline seed, 0 bankruptcies. Seeds 51 to 100: 50 founded, wins 25 and 25, median founding day 6,313, 0 deadline seeds, 0 bankruptcies. The other figures in the table were not reported again.

Reviews (2026-10-11, step 1; outcomes accepted by the build conductor, not by the owner). After the fix round 415 tests pass in the client preset, the layout probe among them, and both release sweeps exit 0 as above.

Code review (gate PASS; 4 medium, 3 low):
- The outcome was not validated against company state on load. Applied: the checks listed under the build decisions above.
- The runner's days were off by one. Applied: `founded_day` and `end_day` are the outcome's day plus one.
- A lesser-partner result could be contradicted by a bankruptcy later the same day. Applied: the world freezes on the day it ends.
- `Session::submit` did not refuse orders after the end. Applied.
- Stage days accepted day 0. Applied: a stage day is at least 1.
- A second click on Deliver queues a delivery the sim rejects. Declined for now: the sim stays the authority. Kept under "Open after step 1".
- The loader accepts a program that cannot be won. Applied for a stage with no requirements only. The rest is kept under "Open after step 1".

Codex (7 findings and 1 minor):
- Codex 1, finance ran after the founding. Applied: the world freezes.
- Codex 2, a second finish on the same day is discarded and not recorded. Declined: the first order applied wins. Recorded as a departure from edge case 1.
- Codex 3, the end screen and the runner disagreed on the day. Applied.
- Codex 4 and 5, save validation. Applied.
- Codex 6, the AI's fractional deliveries. Applied: at least one tonne.
- Codex 7, the trace's outcome was lost on New game. Applied: one `end` record per game, written when the game ends.
- Codex minor, the wording of a rejection after the end. Applied.

Security review (2026-10-18, at `90261e3`; PASS, no critical, high, medium or low findings, 3 informational):
- `exactPrice` in `game/session/src/format.cpp` negates the cents without a guard, as `exactMoney` does. Unreachable: cash is bounded on load. Noted, no change.
- The loader accepts programs that cannot be won (already open below). It is not a memory-safety issue: every use stays inside int64.
- No new third-party dependency in step 1.
Save loading, the two program orders, the content loader, trace and replay parsing and string substitution were traced end to end and found clean.

Spec-compliance check (2026-10-18, at `90261e3`; NON-COMPLIANT on four points, each since resolved or decided):
- AC-021, unpolished prose. Applied: polished through agy (`534d08b`).
- AC-019, no recorded green run at the head (one test changed after `9f3a7e0`). Applied: rerun on 2026-10-18 after the prose polish. debug 422 of 422, debug-game 423 of 423 (the layout probe among them), release 422 of 422 (the gated sweep among them), asan 422 of 422; clang-format 18 clean on every source. No shipped source changed after `9f3a7e0`, where clang-tidy was clean; tests are outside the tidy gate, as in CI. Golden hashes unchanged.
- The polish broke eight tests that compared notices with the drafted sentences. Applied, with the owner's approval: those checks now take the wording from the catalogue (`game::text`, `game::rejectReasonClause`) and still check the assembled line; the mothball notice keeps the word "mothballed", and the stage 3 description keeps "deep space" unhyphenated, as their string tests require.
- BR-011, the neutral land weight was not listed as a departure. Applied: listed above; the owner keeps it neutral.
- EC-001 and the build's departures were accepted by the conductor only. The owner accepted them on 2026-10-18.
- Not blocking, kept under "Open after step 1": AC-002 has no assertion that delivered goods pay no storage (it holds because they leave stock); edge cases 2 (delivery and sale of the same stock on one day) and 5 (stage 3 while insolvent) have no test; the BR-012 data test allows 6% against "about 5%". The check reported edge case 3 (the Program tab warns when cover costs is on) as unverified; it is built (`game/src/ui_model.cpp`, `ui.program_cover_costs_warning`) and checked by the layout probe.

Owner decisions (2026-10-18, after playtest 5, `docs/playtests/2026-10-18.md`):
- The build decisions and departures above, edge case 1 and the repeated golden-hash moves among them, are accepted.
- BR-011: the land weight stays neutral for now and is revisited with step 4's tuning.
- AC-021: the step 1 prose is polished before the merge. 36 sentences went through `tools/prose/polish.sh`; where agy changed the game's terms (a "contract" or "charter" for the program, "drawings" for research, "junior" for lesser partner, a mothball notice without the word) the line was corrected by hand to match the buttons and tabs.
- Step 2 does not start yet (BR-025). The playtest showed the race is not felt: the player did not know how far behind they were, could not find nickel, and could not tell why the clock stopped. A follow-up for step 1 comes first.
- `spec-010-race-step-1` is merged to `main` once these reviews are recorded.

## Open after step 1
Known on 2026-10-11 and not resolved, except where an item says it was resolved on 2026-10-18.

Prose:
- Resolved 2026-10-18 (`534d08b`): about 30 sentences were unpolished drafts, because the agy quota was exhausted, and were owed their polish pass (BR-027, AC-021): the notices `notice.program_*`, `notice.deadline_passed`, `notice.mine_mothballed`, `notice.mine_reopened`, `notice.command_rejected.delivery` and `notice.command_rejected.stage`; the `reject.*` reasons for the program and for a game that has ended; the three `tech.program_stage_N.description`; the `ui.program_*` sentences; the `ui.end_*_body` sentences and `ui.end_day`; `status.delivery_queued` and `status.stage_completion_queued`.
- `ui.game_over` is now unused. It is still in the catalogue and registered in the loader.

Numbers:
- Every number in the table above is provisional. The market-depth target of 3 percent (BR-021) is step 4's; step 1 ships at about 4.8 percent against BR-012's "about 5% or less".
- Seed 26 stalls at stage 3 on a scarce ore and reaches the deadline (F9).

Design consequences the tuning task flagged for the owner. These are questions, not decisions:
- The race is paced by the stage research clock. The stage 3 project alone is 2,520 days. That gives a narrow founding spread, years 17 to 20, and means the deadline hardly bites: 1 seed in 100 reaches it.
- A human who completes stage 1 late cannot catch an AI that makes no mistakes. The AI does give away about 700 days by waiting for half its deliveries before starting each project.
- The AI is rich and takes the land: about 88 mines between two AIs, and 0.85bn to 1bn dollars each at year 10. Spec 008's bite is largely gone at this market depth (16 to 72 permille of days below break-even against that spec's 100 to 500).
- Nothing about a human's pace is measured. There is only arithmetic: one iron and one copper mine clear about 16.2M a year at base prices, and stage 1 needs 13M in cash and research plus goods worth about 31.5M at base, so a two-mine company reaches stage 1 in roughly years 5 to 7.

Client:
- The bankruptcy day on the end screen still comes from the session log, and falls back to today after a load. A race's ending day and the stage days come from the sim and survive a load.
- Loading a save makes RmlUi log data-binding warnings for the Dashboard's Recent list as its rows are removed. The layout probe sets those warnings, and only those, aside.
- The client has not been seen on a display. The sandbox can compile and probe it, not run it.

Verification and records:
- The golden values (`kGolden` `0x059a7c79213dd307`, `kGoldenPlayed` `0x41192679894c4afc`, `kGoldenRace` `0xae14b05ab3e56760`) were recorded on Linux/GCC and are unconfirmed on Windows/MSVC until CI runs.
- Resolved 2026-10-18: the security review and the spec-compliance check have been run (see "Reviews"). Their non-blocking gaps stay open: AC-002 has no storage assertion, edge cases 2 and 5 have no test, and the BR-012 data test allows 6%.
- A second click on Deliver, before the clock moves, queues a delivery the sim will reject (code review; declined for now, the sim stays the authority).
- The loader still accepts a program that cannot be won, other than a stage with no requirements (code review).
- The `.meta/description.md` files are stale. For one, `tests/.meta/description.md` still gives `kGolden` as `0x2a5da3177ab7c69d`.
- Spec 008's decision record 003 is superseded in effect for gating. Accepted records are not edited. Whether an amending record is written, as record 004 did for spec 008, is a question for the architect.
- The comment in `sim_runner/CMakeLists.txt` above `runner_sweep` says the gated sweep took 8 to 9 seconds with every game ending in years 8 to 12. That was before the tuning; the tuning task reports 38 to 39 seconds and foundings in years 17 to 20.
- Resolved 2026-10-18: step 1 was playtested (playtest 5, `docs/playtests/2026-10-18.md`). The owner held step 2 for a step 1 follow-up.
- The end screen's "Mines" figure counts worked-out mines (playtest 5).
