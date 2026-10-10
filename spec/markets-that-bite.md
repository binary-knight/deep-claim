# PRD: Markets that bite (spec 008)

## Summary
In three playtests the price of ore never forced a decision. Companies supplied five times base demand, prices settled at 0.35 to 0.5 times base, and every mine stayed profitable, because a mine's running cost is only 11 to 22% of base price. Nothing stopped either company building, and the first good mine made money irrelevant.

This feature makes price bite. The price responds more steeply to supply, so a handful of mines visibly moves a market. Each ore gets its own daily mine running cost, set so break-even sits at a chosen share of base price, so a flooded market falls below the cost of digging and oversupply hurts whoever caused it. The AI's knobs are retuned so rivals survive such a market rather than bankrupting themselves, and the opening is held where spec 005 put it.

So the player can play the new market, each mine shows what it earns or loses per day at today's price with its break-even price, and a notice (with an auto-pause rule the player can switch off) fires when one of their mines starts losing money. The balance runner gains the measures needed to judge the result: time each ore spends below break-even, mine-days idled for price, company cash at year 10, and price ranges, over sweeps extended to 20 years.

It serves journey J-001 (a player rides out a falling price) and is system 1 of the Earth strategy (docs/design/earth-strategy.md). Saves and traces from earlier builds stop loading.

## Scope
### In Scope
- Price response: per-ore elasticity retuned upward in `data/resources.json`, and per-ore base demand retuned alongside it.
- Running cost per ore: a new per-ore daily mine running cost in `data/resources.json` replaces the single `mine_opex_per_day` in `data/balance.json`. Research and event multipliers apply as now; the mothballed share is unchanged.
- Clamps to data: the two hard-coded market clamps in `sim/src/markets.cpp` move to `data/balance.json`.
- AI retune: values in `data/ai.json` only, until the bankruptcy gate holds. No new AI logic unless tuning alone cannot meet the gate; if so the build stops and asks.
- The opening: data changes as needed (for example starting capital) to hold the first mine at about day 230 to 270.
- Lithium: its own numbers tuned so its mean sits around 1.0 to 1.4 times base.
- Player information: per-mine daily earn-or-lose figure and break-even price, computed by the sim and exposed on the view, shown in the mine rows and the mine panel; a new notice when one of the player's mines starts losing money, with an auto-pause rule that can be switched off.
- Runner: new measures and checks (time below break-even per ore, mine-days idled, company cash at year 10, yearly price range, bankruptcies over years 1 to 10 gated and 11 to 20 reported). The gated sweep runs 50 seeds for 20 years.
- Records: the golden hash moves once with the reason in `docs/DESIGN.md`; new strings go through the agy polish pass.
### Out of Scope
- Mine size, cost rising with depth, a close-mine order, reclamation (Earth strategy system 2).
- Land holding fees, regional haulage, infrastructure (system 3).
- Capital pressure and the stepped route to bankruptcy (system 4).
- Supply contracts, rival temperaments, milestones and the end goal (systems 5 to 7).
- A display of why a price moved (own output, rivals', the rest of the world).
- New commands, new screens, changes to the sell-policy rules.
- Keeping old saves and traces loadable.
- Anything about space, conflict or multiplayer.

## Requirements
### BR-001: Price responds steeply to supply
Each ore's elasticity and base demand are retuned in `data/resources.json` so that a small number of mines moves its price. The price formula itself is unchanged. The build records each chosen value with the sweep evidence for it.
### BR-002: Running cost follows the ore
Each ore carries a daily mine running cost in `data/resources.json`. A mine pays the cost of the ore it is extracting. A mine extracting more than one ore pays the highest of those ores' costs, not the sum. Research and event cost multipliers and the mothballed share apply to it as they do to today's single figure, which is removed.
### BR-003: Break-even is chosen, not accidental
For each ore, cost per tonne at base output is a stated share of base price: between 0.5 and 0.7 for iron, copper and nickel, and between 0.4 and 0.6 for lithium. The chosen shares are recorded with the reasoning.
### BR-004: Market clamps live in data
The demand-to-supply ratio clamp and the demand-factor clamp move from `sim/src/markets.cpp` to `data/balance.json` with their current values.
### BR-005: The sim says what a mine earns
The company view exposes, for each of the company's mines, its daily margin at today's prices (revenue from its last day's output less its running cost) and a break-even price for its principal ore (the one earning most), holding its other ores at today's price. The client and the AI read these; neither recomputes them.
### BR-006: A losing mine is announced
When an operating mine of the player's has had a negative daily margin for a number of consecutive days set in data (starting value 5), the sim issues one private notice naming the mine and its daily loss. It does not repeat until the mine has returned to a positive margin and turned negative again. An auto-pause rule for this notice exists and is on by default.
### BR-007: Rivals survive
The AI is retuned through `data/ai.json` only. If the bankruptcy gate cannot be met by data alone, the build stops and reports rather than adding AI logic.
### BR-008: The opening holds
The median day on which the AI-played company 0 first has an operating mine stays at or under 270.
### BR-009: The runner measures bite
`sim_runner --sweep` reports, per ore, the share of days in years 3 to 20 on which price was below base-output break-even, and the lowest and highest yearly mean price; per company, mine-days idled and cash at the end of year 10; and bankruptcies split into years 1 to 10 and 11 to 20.
### BR-010: Gates
Over 50 seeds and 20 years with one rival, the sweep passes only if: for each of iron, copper and nickel the median seed has price below break-even on 10 to 50% of days in years 3 to 20; in at least 40 of 50 seeds some mine is idled for at least 30 days; company 0's median cash at the end of year 10 is no more than half of what the pre-008 build gives in the same sweep, measured once and recorded; at most 1 seed has a bankruptcy in years 1 to 10; BR-008 holds; and the existing leader-over-trailer gate still passes. Reported, not gated: mean price over base per ore (aim 0.7 to 1.3, lithium 1.0 to 1.4), bankruptcies in years 11 to 20, withdrawn land.
### BR-011: Determinism and records
No floats in state and no new random streams. The golden hash changes once, with the reason in `docs/DESIGN.md`. Saves and traces from earlier builds are refused cleanly, as today.
### BR-012: Prose
The new notice, the auto-pause rule's label and any new labels go into `data/strings.json`; sentences go through the agy polish pass.

## Acceptance Criteria
1. **AC-001 — Running cost per ore in data.** `data/resources.json` carries a daily mine running cost for each ore and `data/balance.json` no longer has `mine_opex_per_day`. The loader rejects a missing, zero or negative cost. A data test computes each ore's break-even share (cost divided by base output divided by base price) and fails outside 0.5 to 0.7 for iron, copper and nickel or 0.4 to 0.6 for lithium.
2. **AC-002 — A mine pays its ore's cost.** Sim tests show: a single-ore mine pays that ore's cost; a mine extracting two ores pays the higher cost, not the sum; research and event multipliers and the mothballed share apply as before.
3. **AC-003 — Clamps in data.** The two market clamps are read from `data/balance.json`; no numeric clamp literal for them remains in `sim/src/markets.cpp`; a test shows prices are identical to the old code at the old values.
4. **AC-004 — The view carries margin and break-even.** Each mine in the company view has a daily margin and a principal-ore break-even price, tested for single-ore, two-ore, mothballed and not-yet-producing mines. Neither `game/` nor the AI computes them independently.
5. **AC-005 — The player sees what each mine earns.** The mine rows on the Dashboard and Plots tables and the mine panel show the mine's daily earn-or-lose figure and its break-even price, a loss shown distinctly from a gain. The layout probe passes at 1366x820 and 1280x800. As the player, navigate from the Dashboard via the plots-and-mines table (or the Plots tab, selecting an owned plot) and read the mine's row and panel to see what it earns or loses per day at today's price and the price at which it breaks even. Live at release; fully-functional means: with a mine operating, its row and panel show a daily figure that equals the sim's margin for that mine and changes when the ore's price changes, and the break-even price shown is the price at which that figure is zero.
6. **AC-006 — The losing-mine rule.** After the set number of consecutive days of negative margin an operating mine produces exactly one private notice to its owner; none on fewer days; none again until the margin has gone positive and negative again; none for a mothballed mine.
7. **AC-007 — The player is told, and the clock stops.** The notice appears in the log with polished text naming the mine and its daily loss. An auto-pause rule for it is listed with the other stop rules, is on by default, can be switched off, and stops the clock on the day the notice arrives. As the player, navigate from any tab with the clock running via the losing-mine notice that stops the clock, read it in the log, and from the Stops menu beside the speed controls switch the rule off or on, to be told as soon as a mine of theirs starts losing money. Live at release; fully-functional means: when a mine of the player's has lost money for the set number of days, a log line names the mine and its daily loss and the clock stops that day; with the rule switched off the line still appears and the clock keeps running.
8. **AC-008 — The runner reports bite.** The sweep's JSON summary contains, per ore, share of days below break-even in years 3 to 20 and lowest and highest yearly mean price; per company, mine-days idled and cash at end of year 10; bankruptcies for years 1 to 10 and 11 to 20.
9. **AC-009 — The gated sweep passes.** `sim_runner --sweep 50 --years 20 --ai 1 --sanity` exits zero with every check in BR-010 passing, and the `runner_sweep` CTest runs those checks.
10. **AC-010 — The opening and the contest hold.** In that sweep the median first operating mine day is at or under 270 and the leader-over-trailer gate passes.
11. **AC-011 — Determinism recorded.** The golden hash in `tests/determinism_test.cpp` is updated once; `docs/DESIGN.md` gains a section giving the reason; a test shows a save from the previous content fingerprint is refused with the existing message.
12. **AC-012 — Prose through the pipeline.** Every new string is in `tools/prose/draft.tsv`, `polished.tsv` and `data/strings.json` and registered in the loader; the parity test passes.
13. **AC-013 — The numbers are explained.** The feature's decisions record (and `docs/DESIGN.md`) holds a table of every value changed (per-ore elasticity, base demand, running cost, break-even share; AI knobs; any opening change; the measured pre-008 year-10 cash) with the sweep result that justified it.

Sources in play: code, spec, prototype; conflicts resolved per source-of-truth-conflict-rule.

BLOCKER: if the BR-010 gates cannot all be met by data changes alone, which gate gives way? — owner: binaryknight. (Blocks AC-009.)

## Edge Cases
1. A mine digging two ores pays the higher of the two running costs. Its margin counts revenue from both. Its break-even price is for the ore earning most, with the other held at today's price; if the other ore alone covers the cost, the break-even shown is zero.
2. An ore the company cannot yet extract (lithium before the research) contributes neither cost nor revenue; when the research completes, the mine's cost is re-evaluated the next day.
3. When one ore of a two-ore mine runs out, the cost falls back to the remaining ore's cost from the next day.
4. A mine that has not produced yet (under construction, or its first day) shows no margin and no break-even and cannot trigger the losing-mine notice.
5. A mothballed mine shows its mothballed cost as a daily loss and what it would earn if reopened today; it never triggers the notice.
6. Margin hovering around zero: the notice needs the set number of consecutive negative days, so a mine flickering either side of zero does not produce a stream of notices.
7. Several mines turn negative on the same day: one notice per mine; the clock stops once.
8. The player has switched auto-sell off and is holding stock: margin is still worked out at today's market price for the day's output, whether or not it was sold, so a mine can show a gain while cash falls. The figure is labelled as at today's price.
9. A price at its floor or cap: the clamps still apply; a market pinned at the floor counts as below break-even for the measure.
10. A world event that raises costs or cuts demand can push a healthy mine into loss; the notice fires as for any other cause.
11. A rival goes bankrupt anyway in an ungated year or seed: its land returns to the market as today; the game continues.
12. A save or trace from an earlier build is refused with the existing message; nothing is migrated.
13. A data file with a break-even share outside the stated range still loads (the range is a test on shipped data, not a loader rule), so experiments with `--data` remain possible.
14. Tuning cannot meet every gate: the build stops and asks (the blocker above); it does not loosen a gate or add AI logic on its own.

## Research Notes
See `research/codebase.md` in the feature directory: the price model, why prices never reach cost today (break-even 0.11 to 0.22 of base), and eight exploratory 20-seed, 20-year sweeps. Decisions are in `gray-areas-spec.md`. Journey: `docs/mvp/journeys/J-001-player-rides-out-a-falling-price.md`. Strategy: `docs/design/earth-strategy.md`, system 1. Project rules: `ARCHITECTURE.md` R-001 to R-004, `CLAUDE.md`.

## Decisions
Recorded on 2026-10-10, after the build. The requirements and acceptance criteria above are left as approved; where the build differs from their text, the difference is listed under "Departures from the spec's text". The architectural summary, the table of tuned values and the sweep results are in `docs/DESIGN.md`, "Markets that bite (spec 008)".

Owner decisions during the build:
- 2026-10-10: the tuned set of numbers is accepted for the playtest although it is fragile to the seed block. On seeds 1 to 50 every gate passes. On seeds 51 to 100 two gates fail narrowly: 2 seeds have a bankruptcy in years 1 to 10 against a limit of 1, and 2 seeds have no depleted mine by year 8 against a limit of 0. The gates that measure bite (price below break-even on iron, copper and nickel, idled mines, year-10 cash) pass on both blocks. Reason: bankruptcy and depletion will be reshaped by mine economics (Earth strategy system 2). The blocker above did not arise: on the gated seeds (1 to 50) every gate was met by data changes alone and no gate was loosened. What the owner accepted is the fragility on the second seed block.
- 2026-10-10: starting capital is kept at 12M (it was 5M) for the playtest although the first mine does not need it: the median first operating mine is day 228 at 5M and day 227 at 12M. It buys margin against the bankruptcy and depletion failures above. It is the first thing to judge in play.

Build decisions (made by the implementing agents and accepted by the build conductor, not by the owner):
- 2026-10-10: a mine's cost ores are the ores it produced on its most recent operating day; when there are none, every ore its owner has unlocked. The plot's true deposits are never read, so a cost cannot reveal an ore the owner has not found (decision record 001).
- 2026-10-10: `MineView` gained two fields beyond the design: `hasEarnings` (the mine is operating or mothballed and has output on record; otherwise margin and break-even are zero and mean nothing) and `operatingOpex` (what the mine costs, or would cost, as an operating mine, which the reopening decision weighs the margin against).
- 2026-10-10: `CompanyView::mineOpexQuote` became `mineOpexQuotes`, one quote per ore. `sim::mineOpexQuote` is the sim function that prices one ore for one company.
- 2026-10-10: the notice is raised when `lossDays` is at or past the threshold and the run has not been announced, not only on the day it equals the threshold, so a save carrying a longer unannounced run is told on its next losing day.
- 2026-10-10: the runner's bite checks are on the sweep path only and are evaluated only by a sweep that covers 20 years. A shorter sweep prints them as not evaluated and they pass.
- 2026-10-10: the `runner_sweep` CTest runs the gated 50-seed, 20-year sweep in optimized builds without the sanitizers, with a timeout of 180 seconds. Every other build runs 2 seeds (1 under the sanitizers) and is judged by its output, not its exit code, because so few seeds can fail a median by sampling alone.
- 2026-10-10: the loader bounds the demand-to-supply ratio clamps to [0.01, 10], which keeps the price target inside the integer range for any elasticity and base price the loader accepts.
- 2026-10-10: the retune touched `data/resources.json`, `data/ai.json` and `data/balance.json` only. No AI rule was added to meet a gate (BR-007).

Departures from the spec's text (build decisions; the criteria above are not rewritten):
- 2026-10-10, AC-009: the command is `sim_runner --sweep 50 --years 20 --ai 1`, without `--sanity`. The runner refuses `--sanity` together with `--sweep`: a sweep has its own checks and exits non-zero on a failed gate without the flag.
- 2026-10-10, BR-008 and AC-010: the runner's standing first-mine gate from spec 006 is "median under day 250", stricter than this spec's "at or under 270". It was kept and it passes (day 227), so no separate 270 check was added.
- 2026-10-10, BR-011, AC-011 and "Records" in Scope: the golden hash did not move. `kGolden` is still `0x2a5da3177ab7c69d`, because its run gives no orders and builds no mine, so the two new mine fields are hashed in an empty list. Instead a second golden test for a played game was added (`kGoldenPlayed`, three years of AI play on the in-code fixture). `docs/DESIGN.md` records the reason and corrects the design and decision record 002, which say the hash moves. A test shows a save made against different spec 008 content values is refused.
- 2026-10-10, design constraint and decision record 003: the 60-second ceiling on the release sweep became a 180-second timeout. The sweep took 28 to 62 seconds in a release build depending on what else the machine was doing.
- 2026-10-10, BR-009 and decision record 002: the runner's below-break-even measure does not call the sim's margin function. It compares the day's price with the data's own break-even for one mine at base output (running cost over base output, no research or event multiplier), which is what BR-009 names. The margin function serves the tick and the view; the AI and the client read the view.
- 2026-10-10, BR-012 and AC-007 and AC-012: the new strings are in the draft, the polished file and the catalogue and are registered in the loader, but the losing-mine notice sentence did not go through the agy polish pass (see "Open after build"). The labels are kept as drafted, as in earlier specs.

## Open after build
Known on 2026-10-10 and not resolved. None of this is done.
- The losing-mine notice sentence (`notice.mine_losing_money`) is the plain draft: the agy quota was exhausted. It is owed its polish pass before release. AC-007's "polished text" and BR-012 are not met until then.
- Withdrawn land at year 8 is 66 plots at the median (it was 37 after spec 006; spec 005's aim is 25). Reported, not gated.
- Median mine life is 2,550 days against a band of 1,800 to 2,520. Reported, not gated.
- Lithium's best year reaches about 3.1 times base in the median seed. Its mean (1.22) is inside the 1.0 to 1.4 aim.
- The depletion-by-year-8 gate passes with no slack on seeds 1 to 50 and fails on seeds 51 to 100 (owner decision above).
- The idled-mines measure counts any mothballed state, whoever chose it and why, not only mines idled for price.
- The second golden hash (`kGoldenPlayed`, `0xc0980b717725177f`) has been computed on Linux/GCC only. It pins the two new mine fields at zero, because no mine in the fixture's game runs at a loss; a counted value is pinned only by the tests in `tests/mines_test.cpp`.
- The AI's valuation still multiplies extraction time by a hard-coded 13/10 (`sim/src/ai_controller.cpp`). It is on the do-not-copy list and was not moved to data.
- A few labels in `game/assets/ui/main.rml` are literal and not in the catalogue: "loss", "if reopened", "Earns", "Loses", "Mothballed", "If reopened".
- On the Deck frame the dashboard's area figure now sits under the region name.
- A loss is shown in the amber warning colour while the month's net figure uses red.
- Not seen on a display. Not yet run through CI on Windows/MSVC. Codex review and playtest still to come.
