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
- **World size (data).** `regions.json` `plot_count` rises from 10 to 30
  per region (90 plots). Reasoning: at one listing a month plus the three
  opening plots, 90 plots last 87 months, a little over seven years; with
  spec 004's withdrawal and relist, unsold land recirculates after that.
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

## Out of scope
Deposit sizes, iron margin, rival bid premium, AI knobs (spec 006); new
regions or resources; the asteroid phase.

## Decisions
- 2026-10-09: drafted from playtest 2; owner chose this as the next spec.
