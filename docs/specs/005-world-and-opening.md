# 005: A bigger world and a fair opening

Status: approved 2026-10-09 as drafted. Builds after spec 004 (both touch the
plot market and world generation).

## Goal
Land should last through the mid-game, and the first year should always
offer a new company something it can mine. In playtest 2 the world's 30
plots were all listed by day 810, and after the rival took the one nickel
plot every listing for 90 days was Salt Flats, empty or locked. We will
know it works when runner sweeps over 50 seeds show (a) at least two
opening listings with unlocked ore in every seed, (b) unlisted land
remaining at year 8 in every seed, and (c) median time to a player-style
first mine under 250 days.

## Player-facing behaviour
- The opening listings always include at least two plots whose region can
  hold ore the company can mine on day one, and no more than one Salt
  Flats plot.
- New land keeps coming for roughly ten years at default settings; the
  Plots tab shows "N plots not yet listed" so the player can see the
  horizon.
- Listings interleave regions: no region supplies more than two
  consecutive listings.
- Nothing else changes in how auctions work.

## Sim rules
- **World size (data).** `regions.json` `plot_count` rises from 10 to 40
  per region (120 plots). Reasoning: at one listing a month plus the four
  opening plots, 120 plots last 116 months, a little under ten years; with
  spec 004's withdrawal and relist, unsold land recirculates after that.
  (Approved at 30 per region; raised during the build because 90 plots
  run out before year 8. See Decisions.)
- **Listing pace (data).** `balance.plots_listed_per_month` stays 1;
  `balance.plots_listed_at_start` rises from 3 to 4 so two mineable
  plots and one Salt Flats plot fit with one free draw.
- **Opening guarantee.** World generation builds the listing queue in two
  parts: an opening hand of `plots_listed_at_start` plots drawn so that at
  least `balance.opening_min_mineable` (2) come from regions with a
  day-one-unlocked resource at `presence_prob >= 0.5`, and at most
  `balance.opening_max_locked_region` (1) from regions whose only
  deposits are locked; then the rest, shuffled with a constraint of at
  most two consecutive plots from one region (re-draw from the remaining
  pool when violated; deterministic, world RNG stream only).
  The guarantee is on regions and odds, not on hidden deposits: a
  mineable-region plot can still survey empty. That keeps surveys
  meaningful and avoids leaking hidden truth through the queue.
- **View.** `CompanyView::unlistedPlotsRemaining` (added in spec 004) is
  what the Plots tab shows.
- Save format: the queue is already serialized; world size is content, so
  the content fingerprint changes and old saves do not load. The in-code
  test fixture keeps its own small world, so the golden hash moves only
  if queue construction changes its draws (it will; update once).
- AI: unchanged. With three times the land the rival's one-plot-a-quarter
  cadence leaves more for the player; tuning is spec 006.

## View
As above.

## Client
Plots tab: "N plots not yet listed" line; no other change.

## Tests
- Worldgen: over 200 seeds, the opening hand meets both constraints; no
  run of three listings from one region; every plot appears in the queue
  exactly once; same seed gives the same queue.
- Loader: new balance keys validated (min mineable <= listed at start;
  max locked >= 0).
- Runner: a `--sweep N` mode printing, per seed, mineable opening plots,
  day of last new listing, unlisted plots at years 2, 5, 8, and for the
  AI-as-player the day of its first operating mine; CI runs a 10-seed
  sweep as a sanity check with thresholds from the Goal.

## Plan
- `data/regions.json`: `plot_count` 40 per region (see Decisions). `data/balance.json`:
  `plots_listed_at_start` 4, `opening_min_mineable` 2,
  `opening_max_locked_region` 1, `max_unsold_rounds` 6,
  `relist_cooldown_days` 180.
- `sim/content.h`, `sim/serialize.h`: `BalanceParams::openingMinMineable`,
  `openingMaxLockedRegion` (hashed with the rest of the balance).
- `content/src/loader.cpp`: the two keys, with
  `opening_min_mineable <= plots_listed_at_start`.
- `sim/worldgen.h`, `sim/src/worldgen.cpp`: region classes
  (`regionMineableAtStart`, `regionLockedAtStart`), the opening hand, and
  the constrained shuffle of the remainder. World RNG stream only.
- `game/src/ui_model.*`, `main.rml`, catalogue key `ui.unlisted_plots`:
  "N plots not yet listed".
- `sim_runner/main.cpp`: `--sweep N`, per-seed rows, summary, Goal
  thresholds as the exit code; CTest `runner_sweep`.
- Tests: `tests/worldgen_test.cpp` (200-seed property test),
  `tests/content_test.cpp` (keys), fixture values in
  `tests/test_content.h`; layout probe scenario retuned.

## Out of scope
Deposit sizes, iron margin, rival bid premium, AI knobs (spec 006); new
regions or resources; the asteroid phase.

## Decisions
- 2026-10-09: drafted from playtest 2; owner chose this as the next spec.
- 2026-10-09: approved as drafted.
- 2026-10-09: unsold-land knobs change with this spec (owner's choice
  after spec 004 showed 23 of 30 plots withdrawn by year 3 in the probe
  game): `max_unsold_rounds` 3 to 6 (a plot stays for sale 270 days) and
  `relist_cooldown_days` 360 to 180.
- 2026-10-09 (build): **40 plots per region, not 30.** The spec's own
  arithmetic (90 plots last 87 months) contradicts its goal (b), land
  remaining at year 8, and its "roughly ten years": the sweep showed every
  seed out of unlisted land on day 2581 with 30 per region. With four
  opening plots and one listing a month, 120 plots last 116 months
  (day 3481) and leave 21 unlisted at year 8. Plot count is a knob this
  spec owns; listing pace was left alone.
- 2026-10-09 (build): **what counts as a locked region.** The spec's
  wording, "regions whose only deposits are locked", does not describe
  Salt Flats, which also has copper on 20% of plots, yet the player-facing
  promise is "no more than one Salt Flats plot". A region is Mineable when
  an ore that needs no research is present on at least half its plots
  (0.5, a named constant in `worldgen.cpp`), Locked when it is not
  Mineable and holds an ore that needs research, and Other otherwise.
  With the shipped three regions every hand is therefore three or four
  mineable-region plots and at most one Salt Flats plot.
- 2026-10-09 (build): the hand is drawn mineable plots first, then the
  rest under the locked cap, then shuffled. Each rule gives way only when
  no remaining plot can meet it (one-region worlds, a world with a single
  mineable plot, a hand larger than the world), so generation never
  aborts or loops. The "no third in a row" rule counts the last plots of
  the hand as predecessors and is relaxed only when every remaining plot
  is in the blocked region; the hand itself is exempt because it is listed
  together.
- 2026-10-09 (build): **the first-mine goal is reported, not gated.** Over
  50 seeds the AI-played company 0 starts its first mine on day 93 at the
  median and opens it on day 273 (best seed: day 227), against a goal of
  under 250. It is not a property of the land: six opening plots with four
  mineable, or an all-mineable hand of four, leave the median at 272 to
  273. The AI usually buys in the second auction round, and its bidding
  rules belong to spec 006, as does `auction_window_days`. `--sweep`
  prints the check with `"gated": false`; gate it once the AI is tuned.
- 2026-10-09 (build): goals (a) and (b) pass in all 50 seeds and gate the
  exit code (3 on failure). CTest `runner_sweep` runs ten seeds for eight
  years in every preset; about 11 seconds in a debug build.
- 2026-10-09 (build): 50 seeds, 10 years, one rival, release build:

| Per seed | min | median | max |
| --- | --- | --- | --- |
| Opening plots in a mineable region (of 4) | 3 | 3 | 4 |
| Opening plots in the locked region | 0 | 1 | 1 |
| Longest same-region run, monthly listings | 2 | 2 | 3 |
| Day the last unlisted plot was listed | 3481 | 3481 | 3481 |
| Unlisted plots at year 2 / 5 / 8 | 93 / 57 / 21 | 93 / 57 / 21 | 93 / 57 / 21 |
| Withdrawn plots at year 2 | 6 | 10 | 15 |
| Withdrawn plots at year 5 | 23 | 28 | 37 |
| Withdrawn plots at year 8 | 35 | 44 | 57 |
| Day company 0 started its first mine | 47 | 93 | 423 |
| Day company 0's first mine opened | 227 | 273 | 603 |
| Final cash, company 0 | -$0.58M | $187M | $1,086M |
| Final cash, company 1 | -$1.01M | $831M | $1,219M |

- 2026-10-09 (build): two things the sweep shows that this spec does not
  fix. **Withdrawn land:** with six rounds and a 180-day rest, a median of
  44 of 120 plots are sitting withdrawn at year 8 (10 at year 2); the old
  3 and 360 gave 49. Two companies do not buy land as fast as it is
  listed. **AI solvency:** company 0, played by the AI, ends below zero in
  5 of 50 seeds and the rival in 1; a 30-plot world built the old way gave
  0 and 1. The count grows with the size of the world (3 of 50 at 20 plots
  a region), not with the hand rule or the unsold knobs, which points at
  the AI overextending when land is plentiful. Both are spec 006.
- 2026-10-09 (build): the Plots line ships as drafted, "N plots not yet
  listed.", because the spec quotes it; agy's version ("N plots haven't
  been listed yet.") is kept in `tools/prose/agy-raw.tsv`.
- 2026-10-09 (build): the for-sale table binds its selection class on the
  cells, not the row. With ten or more plots for sale the list shrinks
  while laid out and RmlUi logged an out-of-bounds read for a class
  binding on the repeated element (the same fault spec 004 fixed for
  survey estimates and log lines). Found by the layout probe.
- 2026-10-09 (review): Codex reviewed the diff and this spec. It found the
  queue construction correct for every edge case asked about, relaxation
  matching these Decisions, no dependence on hidden deposits and no
  platform nondeterminism. Applied:
  - `classifyRegionAtStart` aborts through `sim::fatal` on a deposit
    naming a resource that does not exist, instead of indexing past the
    table.
  - `--sweep` rejects a seed range that would wrap past the largest seed,
    and rejects `--sanity` (a sweep has its own checks) instead of
    ignoring it.
  - A sample year the run stops short of is printed as `null` (blank in
    the CSV), not as zero.
  - `world_valid` now validates each world after construction and at the
    end of the run as well as every year end, so a short run cannot pass
    without a check.
  - Tests: only-locked land with a locked cap of zero, and a world with no
    plots; the shipped-data run check now starts at the first monthly
    listing, so the boundary with the hand is covered.
  Declined:
  - Distinguishing "the opening hand was the whole world" from "land never
    ran out" in `last_new_listing_day`: both print 0; no shipped or
    planned world is that small. Noted in the code.
  - Escaping control characters in the runner's `jsonString`: the sweep
    prints only fixed strings.
  - Automated runner tests for short runs, seed wrap and parsed output:
    checked by hand (2-year sweep prints nulls; both rejected option
    combinations exit 2). Worth adding if the runner grows.
  - Rejecting worlds of 2^32 plots: the loader caps `plot_count`.
  - Noted, not changed: draws are uniform over plots, so a larger region
    gets more of the hand; the run rule is local, so a forced same-region
    tail can occur where a different earlier order would have avoided it.
