# 004: Playtest 2 fixes

Status: approved 2026-10-09.

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

## Plan
State (sim, all serialized and hashed):
- `Plot::unsoldCount` (rounds closed without a sale in the current run of
  listings) and `Plot::relistNotBefore` (first day a withdrawn plot may be
  listed again, 0 otherwise).
- `WorldState::withdrawnQueue`: withdrawn plots in withdrawal order. The
  monthly lister offers never-listed plots first and takes the front of
  this queue only when they have run out and its cooldown has passed.
- `BalanceParams::maxUnsoldRounds`, `relistCooldownDays`.
- Notices `PlotWithdrawn` and `RivalUnlockedResource`, both public.
- `CompanyView`: `unlistedPlotsRemaining`, `withdrawnPlots`,
  `unlockDaysRemaining[]`, `unlockReady[]`.

Data keys:
- `balance.json`: `max_unsold_rounds` 3, `relist_cooldown_days` 360. Three
  rounds is 135 days on the market with the ask already cut by a fifth or
  more: every company has had four months and three prices to want it. A
  year away is long enough for prices, research and cash to have changed
  the answer, and short enough that the land is seen again in a normal
  game. Zero rounds means "relist forever" (the old rule).
- `strings.json`: `notice.plot_withdrawn`, `notice.rival_unlocked_resource`,
  `notice.auto_sale_month`, `notice.auto_sale_item`, `ui.pause_on.research_complete`,
  and the `ui.*` sentences for the rows, warnings and reasons listed under
  Player-facing behaviour.
- Auto-pause file: new key `research_complete`.
- Trace: `StartResearch` command records gain `"tech_id"`.

Files:
- Sim: `world_state.h`, `serialize.h`, `content.h`, `commands.{h,cpp}`,
  `plot_market.cpp`, `research.cpp`, `mines.cpp`, `view.{h,cpp}`, `save.cpp`,
  `worldgen.cpp`; `content/src/loader.cpp`; `data/balance.json`.
- Session: `session.cpp` (log filter, summary collapse), `auto_pause.*`,
  `text.cpp` (rejection join, capitals), `trace.cpp`, `replay_main.cpp`.
- Client: `ui_model.*`, `app.*`, `main.rml`, `main.rcss`, `layout_probe.cpp`.
- Prose: `tools/prose/draft.tsv`, `polished.tsv`, `data/strings.json`.
- Tests beside each.
