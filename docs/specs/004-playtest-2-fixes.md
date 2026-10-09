# 004: Playtest 2 fixes

Status: draft, awaiting approval.

## Goal
Remove the friction and the noise the second playtest exposed
(`docs/playtests/2026-10-09.md`, findings 2, 4, 7 and 8) without changing
balance or world size. We will know it works when a full game's log is
readable at any point, no order can be placed that the interface knows
will bounce, and the lab is never idle without the player being told.

## Player-facing behaviour
- **Loans.** Each loan row has **Repay all**. The Take-a-loan form shows
  "You can borrow up to X" beside the field, prefills X, and disables Take
  loan with a reason when X is under the minimum.
- **Auto-pause.** Survey complete and research complete join the list and
  default to on. Existing saved toggles are respected.
- **Needs you.** New rows: "The lab is idle" (button: Research) when no
  project is running and one is available; "Survey report on plot #N is
  in" for 15 days after it lands while the plot is still listed.
- **Surveys.** The Survey button warns when the report would arrive after
  the auction closes ("Report lands day A; auction closes day B") and is
  disabled on plots the player owns with a depleted mine.
- **Locked ore.** When the unlocking tech is being researched or all its
  prerequisites are done, the locked-ore chip and warning change from
  "needs <tech>" to "mineable in N days" or "one project away", and the
  build warning becomes advice to buy ahead.
- **Rival news.** A public log line when a rival completes a tech that
  unlocks a resource ("Northfield Minerals can now process lithium.").
- **Log.** Filter buttons on the Log tab: All, Mine (own orders, mines,
  finance), Market (sales, prices, events), Rivals. Relists of plots the
  player has neither surveyed nor bid on are not written to the player's
  log. Monthly auto-sale summaries collapse into one line per month.
- **Unsold land.** A plot that goes unsold three times is withdrawn from
  sale and returns to the queue; it relists no sooner than a year later.
  When the queue is empty the Plots tab says "No unlisted land remains."
- **Mine running low** triggers on the lower bound of the survey estimate.
- **Prose.** The rejection line reads as one sentence; every notice starts
  with a capital. New strings go through the prose pipeline.

## Sim rules
- `Plot` gains `unsoldCount` and `relistNotBefore`; `runPlotMarket`
  withdraws at three unsold rounds (`balance.max_unsold_rounds`, 3) and
  requeues with `balance.relist_cooldown_days` (360). Serialized, hashed,
  validated; the golden hash moves once.
- New public notice `RivalUnlockedResource` (company, tech, resource)
  emitted when any company completes an unlocking tech; the owner still
  gets ResearchComplete.
- `PlotRelisted` stays public in the sim; the session filters it from the
  player's log unless the player has a survey or bid on the plot.
- `CompanyView` gains `unlistedPlotsRemaining` and, per locked resource,
  `unlockDaysRemaining` (0 when not in progress) and `unlockReady`
  (prerequisites complete).
- `MineRunningLow` uses estimate times (1 - relative error).
- No balance numbers change except the two new knobs above.

## Client
Finance, Research, Plots, Dashboard and Log changes as above; all
presentation of view data. The trace records the tech id on
`StartResearch`. `deep_claim_replay` words building and depleted mines
correctly.

## Tests
Sim: withdrawal after three rounds and cooldown; rival unlock notice is
public and fires once; running-low on the lower bound; save round trip.
Session: relist filter; summary collapse; new auto-pause defaults and
migration of an old toggles file. Client: probe covers the Log filters and
the new rows; strings parity. Golden hash updated with the reason.

## Out of scope
World size and listing pace (spec 005), balance (spec 006), save slots.

## Decisions
- 2026-10-09: drafted from playtest 2.
