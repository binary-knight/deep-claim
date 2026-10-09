# 006: Balance: a contested economy

Status: approved 2026-10-09. Builds on spec 005.

## Goal
A ten-year game should stay a contest. Today it does not: in a 50-seed,
ten-year sweep of the spec 005 build (one rival, both companies played by
the AI) one company ends with 50 to 67 mines and about $1,000M while the
other sits on 3 to 5 mines and, in 6 of 50 seeds, goes bankrupt around
year 7. Prices average 0.6x base for a decade because one company floods
every market. The same snowball is what left playtest 2 without a
decision after year 4. We will know this spec works when the sweep meets
the targets in the table below.

### What the sweep shows now (measured, not assumed)
- **The bankrupt company is the small one, not an overextended one.** In
  every bankrupt seed the loser owned 3 to 5 mines and no debt at the
  end; its cash drained slowly (seed 9: $3.5M, $2.5M, $0.2M at years 2,
  4, 6; bankrupt day 2430) while the leader went from 5 to 67 mines.
  Spec 005 guessed "overextending"; that was wrong.
- **The first mine is bought in the second auction round.** Purchase days
  cluster on auction closes: 11 seeds at day 47 to 48, 5 at day 77, 25 at
  day 92 to 94. The AI's return test (`value >= (ask + capex) x 1.2`
  with half the surveyed tonnage) usually fails at the opening ask and
  passes after the first 10% cut. Median operating day 273.
- **Daily revenue over opex at base price and base output:** iron 3.0,
  copper 6.75, nickel 5.5, lithium 8.75. Iron is the outlier.
- **Deposits outlive the game.** Median deposits last 3 to 5 years at
  base output, but the spread is wide enough that playtest 2 drew 1.59M t
  of copper (73 years of output) and 4.4M t of iron (30 years).
- **Land:** a median of 44 of 120 plots are withdrawn at year 8.

### Targets (50 seeds, 10 years, one rival)

| Measure | Now | Target | In CI |
| --- | --- | --- | --- |
| Median day company 0's first mine operates | 273 | under 250 | gated |
| Seeds with any bankrupt company | 6 | at most 1 | gated |
| Mines owned at year 10, leader over trailer, median seed | about 5x | at most 2.5x | gated at 3x |
| Mean price over ten years, each resource | 0.59 to 0.96x base | 0.75x base or more | reported |
| Median life of a mine before depletion | never (in most) | 5 to 7 years | reported |
| Seeds where no mine has depleted by year 8 | most | none | gated |
| Median withdrawn plots at year 8 | 44 | 25 or fewer | reported |
| Iron revenue over opex at base | 3.0 | 4.5 | by data test |

"Gated" rows fail the `runner_sweep` CTest (ten seeds, with the looser
threshold shown where a ten-seed median is too noisy). The first row
turns on the check spec 005 left reported.

## Player-facing behaviour
- **Winning bids are visible.** The plot panel shows the last five sold
  auctions as "sold to <company> at N% over the ask", and the selected
  plot's history when it has one. The rival's habit of paying 16 to 32%
  over is then something the player can read rather than discover by
  losing.
- **Deposits end.** A typical mine runs five to seven years; very large
  finds are rare and labelled as such in the survey report ("a major
  deposit"). The running-low warning (spec 004) becomes a regular event,
  and buying the next plot before a mine runs out becomes the mid-game
  rhythm.
- **Iron pays.** An iron mine is a modest earner, not a rounding error.
- **The rival is a peer.** It opens its first mine a little sooner, slows
  down when a market is already crowded, and shuts mines that are
  finished instead of bleeding. It should finish a ten-year game within
  reach of an equally careful player, not ten times ahead or bankrupt.
- No new controls. No change to how auctions, loans or research work.

## Sim rules
- **Deposit sizes (data, `regions.json`).** Lower `tonnes_spread` and set
  medians so a median deposit lasts about six years at base output
  including the decline curve; add `tonnes_max_mult` per deposit (a cap
  as a multiple of the median, proposed 4) so no draw exceeds about 25
  years. Starting values to tune by sweep: iron 700k (Northern Shield),
  copper 110k, nickel 36k, lithium 9k, spread 1.6. Reasoning: the mine
  must outlast its payback (two to three years) with room, yet end inside
  a ten-year game so land keeps mattering.
- **Iron (data, `resources.json`).** `base_price` 60 to 90: revenue over
  opex 4.5, below copper so the Copper Belt stays the prize but above the
  point where the Northern Shield plots the opening guarantees are traps.
- **AI valuation.** `surveyedNetValue` prices output at the lower of
  today's price and base (it must not chase a spike), and discounts for
  the supply the company already sells into that market: each mine it
  owns of the same ore lowers the valued price by the market's own
  elasticity rule. A crowded market then stops looking like free money.
  This rule is for the AI only; the player's interface already shows
  price and supply.
- **AI opening.** The return multiple and tonnage haircut are relaxed for
  a company with no mine (proposed 1.1 and 0.6) so the first purchase can
  happen at the opening ask. Later purchases keep 1.2 and 0.5.
- **AI wind-down.** The AI closes a mine that is depleted or has been
  mothballed for a year, ending its opex. The build begins by confirming
  from the runner that mothball opex on finished mines is what drains the
  trailing company (the evidence above fits it but does not prove it); if
  it is something else, the fix changes and the finding is recorded in
  Decisions before any tuning.
- **AI knobs to data.** The sixteen constants at the top of
  `ai_controller.cpp` and the 0.5 "mineable region" threshold in
  `worldgen.cpp` move to data and are loaded into `Content`. They are
  hashed into the content fingerprint: the AI runs inside the
  deterministic sim, so a changed knob must invalidate saves and traces
  exactly as a changed price does.
- **Auction history.** `WorldState` keeps the last 16 settled auctions
  (plot, winner, ask, winning bid, day). Serialized, hashed, validated.
  Winning bids are already public in the purchase notice; this only
  keeps them.
- **Not changed:** market elasticity, demand growth, opex, capex, land
  prices, loan terms, listing pace, world size, unsold-land knobs. If the
  targets cannot be met without one of these, the build stops and asks.
- Save format v1 gains the auction history; the content fingerprint
  changes. Saves and traces from before this spec do not load. The golden
  hash moves once.

## View
`CompanyView::recentSales` (the auction history above, with the premium
as a fixed-point ratio). Survey estimates gain `major` (estimate above
twice the regional median). The AI reads the same view as before plus its
own mine counts per resource, which it can already derive.

## Client
Plots tab: a "Recent sales" block under the for-sale table and a line in
the selected-plot panel; the "major deposit" chip on survey rows. New
strings through the prose pipeline. Layout probe scenario retuned.

## Tests
- Loader: every AI key present and bounded; `tonnes_max_mult` >= 1.
- Worldgen: over 200 seeds no deposit exceeds median x cap; the opening
  guarantee still holds with the threshold read from data.
- AI: with no mine, bids at the opening ask on a plot that passes the
  relaxed test; values an ore lower when it already runs two mines of
  it; closes a depleted mine; a changed AI knob changes the content
  fingerprint.
- Sim: auction history keeps 16, oldest out; save round trip.
- Data: iron revenue over opex at base is 4.5 (computed from the shipped
  files, so the test fails if someone moves one number without the
  other).
- Runner: `--sweep` rows gain mines per company at years 2, 5, 8 and the
  end, mines by state, first depletion day, bankrupt day, mean price
  ratio per resource; the checks in the Targets table.

## Plan
1. Runner reporting first, on the current data, and a baseline table in
   Decisions. No behaviour changes.
2. AI knobs and the worldgen threshold to data, values unchanged; prove
   the sweep output is identical to step 1.
3. Auction history, view, client block, strings.
4. AI rules (valuation, opening, wind-down), each with its test.
5. Data tuning by sweep: deposits, then iron, then AI knobs; one variable
   at a time, each change and its sweep row recorded in Decisions.
6. Codex review, CI, prose pass, layout probe, DESIGN.md.

## Out of scope
A brake on the player's own snowball (overhead, antitrust, diminishing
returns): that is the late game, and belongs to the asteroid phase design
document. More rivals or rival personalities. Plot resale. Save slots.
Market rules.

## Decisions
- 2026-10-09: drafted from playtest 2 findings 5 and 6, the spec 005
  sweep, and a fresh 50-seed sweep that corrected two of spec 005's
  readings (see "What the sweep shows now").
- 2026-10-09: approved. Owner's choices: the AI knobs go in a new
  `data/ai.json`; this spec restrains the rival only, and a brake on the
  player's own growth is left to the asteroid design; a typical mine
  should last five to seven years.
- 2026-10-09 (build, step 1): **baseline.** `--sweep` now reports the
  contest: mines built per company at years 2, 5, 8 and the end, mines by
  state, bankruptcy days, first depletion, the lives of mines opened in
  the first three years, and mean price over base. 50 seeds, 10 years,
  one rival, spec 005 data and rules:

| Measure | Baseline |
| --- | --- |
| Median day company 0's first mine operates | 273 |
| Seeds with a bankrupt company | 6 of 50 |
| Mines built by year 10, leader over trailer, median seed | 3.06x (worst 71x) |
| Mines built by year 10, company 0 / company 1, median | 16 / 49 |
| Mean price over base: iron, copper, nickel, lithium | 0.61, 0.53, 0.66, 0.99 |
| Median life of a mine opened in the first three years | 1,789 days (722 mines) |
| Seeds with no depleted mine by year 8 | 0 |
| Median withdrawn plots at year 8 | 44 |

- 2026-10-09 (build, step 1): **two of the draft's "now" figures were
  wrong.** Depletion already happens: the first mine runs out on day 996
  in the median seed, 16 mines have by year 8, and the median mine of the
  early cohort lasts 4.97 years. What playtest 2 met was the tail, not
  the median: a spread of 2.5 puts one deposit in six above 2.5 times the
  median and has no upper limit. The work on deposits is therefore the
  cap and the spread, not the medians. And the leader's lead over the
  trailer is 3.06x in the median seed, not "about 5x"; the bad seeds
  (71x, 67x, 20x) are the ones that end in bankruptcy.
- 2026-10-09 (build, step 1): **what kills the trailing company.**
  Confirmed from the monthly CSV of all six bankrupt seeds (9, 10, 18,
  28, 37, 40). In every one the company had 1 to 5 mines, no operating
  mine (or one) for the last one to three years and 1 to 4 mothballed,
  with iron at 27 to 38 (0.45 to 0.65x base) and copper at 290 to 450
  (0.32 to 0.5x). Seed 9 loses exactly 1,200 a day from day 1620 to
  bankruptcy: one mothballed mine at 15% of 8,000. In three seeds loan
  instalments, and new loans taken to cover them, sit on top. So the
  mothball charge is the drain, as the draft guessed, but it is the last
  link: the mines are mothballed because the leader's 30 to 60 mines have
  pushed prices under the trailer's opex, and the trailer cannot buy its
  way out because no plot passes its return test at those prices. The
  order of work follows from that: the valuation rule first (the cause),
  then the wind-down rule for whatever bleeding is left.
- 2026-10-09 (build, step 1): **the price target and the land target pull
  against each other.** The market rule is price = base x (demand /
  (demand + company supply)) ^ elasticity. A mean of 0.75x base allows
  company supply of 0.78 of base demand in iron (three mines), 0.51 in
  copper (under two), 0.43 in nickel (just over one) and 0.33 in lithium
  (one): about seven producing mines in the world. Twenty-five or fewer
  withdrawn plots at year 8 needs the two companies to have bought most
  of the hundred plots listed by then. Both rows are reported, not gated;
  the build tunes for the gated rows and reports where these two land.
- 2026-10-09 (build, step 2): the sixteen constants load from
  `data/ai.json` into `Content::ai`, and the presence threshold from
  `balance.json` (`opening_mineable_presence`: it is a rule of world
  generation, not of the competitor). Both are in the content
  fingerprint. With the values unchanged the 50-seed sweep is
  byte-identical to the baseline.
- 2026-10-09 (build, step 3): **no separate list of settled auctions.**
  `WorldState::auctions` is never erased and already records the winner
  and the winning bid, so the history exists; what it lacked was the ask.
  `Auction::askPrice` (the minimum bid the auction opened with) is the one
  field added: serialized, hashed, validated (an open auction stands at
  its plot's ask; a sale is at or above it). The view derives
  `recentSales` (the sixteen newest, newest first) and each sold plot's
  own `sale` from that. Golden hash 0x118a72ce0f57d9f7 to
  0x2a5da3177ab7c69d, the only move in this spec.
- 2026-10-09 (build, step 4): **the rival's bids were rigged, by
  accident.** The offset added to every AI bid "so identical competitors
  do not tie" was `(plot * 7 + company * 13) % 7`. The plot term is a
  multiple of seven, so the offset was 0 for company 0 and 6 for company
  1 on every plot: the rival outbid the AI-played company by six points
  in every auction they both wanted. That is why the rival's winning bids
  in both playtests were 16%, 16%, 16% and 32% over the ask (10 + 6, 25 +
  6), why company 1 built 49 mines to company 0's 16, and why company 0
  bought its first plot in the second round. The offset is now a hash of
  plot and company (`aiBidJitterPercent`, 0 to 6): over 120 plots each of
  two companies is ahead on at least 35. Alone, with every knob as it
  was:

| 50 seeds, 10 years | Baseline | Offset fixed |
| --- | --- | --- |
| Median day company 0's first mine operates | 273 | 228 |
| Seeds with a bankrupt company | 6 | 4 |
| Mines built, leader over trailer, median seed | 3.06x | 1.27x |
| Mines built, company 0 / company 1, median | 16 / 49 | 32 / 33 |

  The first-mine target is met by this alone. This is not one of the
  sixteen knobs and the spec did not ask for it; it is a defect the
  tuning exposed, fixed in code, and it replaces the draft's diagnosis of
  the opening ("the return test fails at the opening ask").
- 2026-10-09 (build, step 4): **valuation.** `aiValuedPrice`: today's
  price, never above base; then the market's own rule applied to the new
  mine's output, `(before / (before + added)) ^ elasticity` with `before`
  = base demand + recent supply, and the same price drop charged on the
  ore the company's operating mines already sell, per tonne of the new
  mine. That is the marginal revenue of one more mine, which is what the
  draft's "each mine it owns lowers the valued price" was reaching for:
  a company with four copper mines values a fifth well below a newcomer.
  `supply_discount` (0 to 1) scales the adjustment; shipped at 1. Capping
  at base alone changes nothing measurable; the supply rule on top of the
  fixed offset takes bankrupt seeds from 4 to 1 and the ratio from 1.27x
  to 1.13x.
- 2026-10-09 (build, step 4): **first-mine terms** exist as two knobs
  (`first_mine_required_return`, `first_mine_recoverable_share`) and ship
  at the proposed 1.1 and 0.6. They do not move the median first mine
  (228 with or without them: the purchase already happens in the first
  round once the bids are fair). Kept because the rule is cheap and the
  owner approved it; step 5 checks they do no harm on the final data.
- 2026-10-09 (build, step 4): **the wind-down rule is not built.** The
  sim has no order that closes a mine: `SetMineState` switches between
  operating and mothballed, a depleted mine already costs nothing, and
  `Closed` is what bankruptcy does. "The AI closes a mine mothballed for
  a year" would need a new command, a since-day on the mine, and either a
  control the player does not get (the spec says no new controls) or a
  player-facing feature this spec does not design. The spec made the
  rule conditional on the diagnosis, and the diagnosis is that the
  mothball charge is the last link, not the cause. Step 5 shows whether
  any bankruptcy is left for it to prevent.
