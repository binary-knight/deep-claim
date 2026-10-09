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
