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
- 2026-10-09, build. Choices made beyond the text above:
  - Withdrawn plots wait in their own queue (`WorldState::withdrawnQueue`)
    rather than at the tail of the listing queue. "Behind never-listed
    plots" is then structural, and only the front needs a cooldown check.
  - A withdrawn plot keeps its reduced ask when it returns and starts a new
    count of unsold rounds.
  - New public notice `PlotWithdrawn`, so the log can say land left the
    market. The session filters it exactly like a relist.
  - `max_unsold_rounds` 0 means "relist forever". The in-code test fixture
    uses it, so older sim tests keep their meaning; shipped data uses 3.
  - `MineRunningLow` is said once per survey. The mine records the survey
    it has warned about (`Mine::lowWarnedSurvey`, serialized, hashed and
    validated). The old rule repeated monthly; on the low end it would have
    repeated for the whole second half of a mine's life.
  - The view also carries `withdrawnPlots`, so the Plots tab can say that
    withdrawn land will return when nothing unlisted remains.
  - `unlockDaysRemaining` counts to the day the ore is mineable, the day
    after the research completes. `unlockReady` is false while the
    unlocking tech is in the lab (the countdown says more).
  - The player's own `RivalUnlockedResource` is dropped from their log;
    `ResearchComplete` already says it.
  - "Has a bid on that plot" is read from the view at the start of the day
    plus bids ordered that day, because the auction is gone from the view by
    the time the relist notice arrives.
  - The collapsed auto-sale line stands where the first summary stood. Its
    log entry keeps the `AutoSaleSummary` kind with the month's totals.
  - Auto-pause migration: a toggles file is read over the new defaults. A
    file from before this spec names `survey_complete` (the client always
    wrote every key), so that toggle is respected as written, which for a
    file nobody edited by intent means it stays off; `research_complete` is
    absent from old files and takes the new default, on. The alternative,
    a version line that overrides an old `survey_complete=0`, would discard
    a choice the file cannot distinguish from a default.
  - Rejection lines: the reason is embedded as a clause (closing full stop
    removed, first letter lowered unless the word is an acronym). The
    `reject.*` strings are unchanged and still read as sentences alone.
  - Capitals: `describeNotice` capitalises its result, so templates that
    open with `{plot}` need no edit.
  - The late-survey warning fires when report day >= close day, not only >:
    auctions settle earlier in the day than survey reports arrive, so a
    report on the closing day is already too late.
  - Repay all is disabled when spendable cash does not cover the balance,
    and Take loan also when all loan slots are in use, each a rejection the
    interface can foresee.
  - The loan field follows the remaining credit until the player types
    something else (clearing it counts). A new game, a load or a loan taken
    hands the field back.
  - Log filters classify in the client from the notice kind and company:
    Rivals is another company's purchase, mine start, bankruptcy or unlock;
    Market is listings, relists, withdrawals, ore sales (manual, automatic,
    cover-costs) and events; Mine is everything else.
  - The Loans table header "Months left" became "Months" to make room for
    the second button in the Steam Deck frame.
  - RmlUi logs an out-of-bounds index when a list shrinks under a class
    binding placed on the repeated element itself. The survey-estimate rows
    and the log lines now carry that binding one level down. Found by the
    probe once it selected a second plot and filtered the log.
  - Kept as drafts (chips, buttons, cells, sub-lines, and sentences this
    spec or its brief quotes word for word): the auto-sale line and its
    item, the two needs-you titles, the late-survey line, the unlock chips,
    "No unlisted land remains.", "You can borrow up to X." and the
    auto-pause label. Fifteen sentences went through agy; none were
    rejected by the invariant check.

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
- 2026-10-09, Codex review of the diff and this spec.
  Applied:
  - Running low could repeat (mothball, reopen, and the low-end figure is
    back inside the window) and could stay silent (a survey from before the
    mine opened whose low end was dug out before the first check). The
    first build inferred "already said" from the figure; the mine now
    records the survey it warned about. Tests for both cases.
  - The loan field could overwrite a typed amount once remaining credit
    happened to equal it. Edits are now tracked instead of compared.
  - A Repay all click that outlived its button rechecks spendable cash and
    reports the reason instead of sending an order that would bounce.
  - The auto-sale collapse no longer depends on the sim reporting the
    summaries next to each other, and its totals saturate.
  - Test: monthly batches that straddle the end of the never-listed queue.
  Declined:
  - "A withdrawn plot's return is a relist and should be filtered." A
    return is `PlotListed`: land the player can buy again after a year
    away, at most one line a month (the listing pace), and late in a game
    the only land news there is. The filtered kinds stay `PlotRelisted` and
    `PlotWithdrawn`.
  - Unicode-aware capitalisation. The catalogue's sentences open with ASCII
    letters or a placeholder; `capitalised` leaves anything else untouched.
  - Checked arithmetic on `close day + cooldown` and the client's
    `day + survey duration`. The loader bounds the cooldown at 100 years of
    days and saves bound the day (`kMaxDay`) and `relistNotBefore`
    (`kMaxDeadline`); both sums are far inside 32 bits.
  - Client tests for the loan field and Repay all. The client has no unit
    harness, only the layout probe; the two fixes above are untested code.
