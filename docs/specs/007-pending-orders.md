# 007: Orders show at once, and can be taken back

Status: built 2026-10-10, awaiting review and playtest

## Goal
An order must be visible the moment it is given. Today an order placed
while the clock is stopped changes nothing on screen until the game is
unpaused (`docs/playtests/2026-10-10.md`, finding 1); in playtest 3 the
same survey was ordered twice on two occasions and the second bounced.
We will know this works when a paused turn reads as a list of decisions:
every order given is on screen, its cost is counted, and any of them can
be withdrawn before the clock restarts.

## Player-facing behaviour
- **The control answers.** As soon as an order is given, its button is
  replaced by a pending chip and a Cancel button:
  - Survey: "Survey ordered"
  - Bid: "Bid placed: $X"
  - Build: "Mine ordered"
  - Research: "Queued for the lab"
  - Mothball or reopen: "Mothball ordered" / "Reopening ordered"
  - Loan, repayment, manual sale: a pending line in the Finance or Markets
    sheet with the amount.
  - Sell-policy and cover-costs switches already show their new setting;
    they gain the pending mark only.
- **Money is counted.** The status strip shows "Committed: $X" while any
  pending order costs money, and "Available" is reduced by it. Buttons
  that the remaining money cannot cover are disabled with the usual
  reason, so a paused turn cannot be over-spent.
- **Borrowed money counts.** A pending loan adds its principal to
  "Available", so a loan and the bid it pays for can be given in the
  same paused turn (the sim applies orders in the order given).
  Cancelling the loan takes the money back out: orders it was covering
  stay pending and are flagged "not covered", and the sim will reject
  them if the clock restarts as things stand. A pending manual sale does
  not count, because the price can move before it is made.
- **Pending orders list.** While anything is pending, a line beside the
  speed controls reads "N orders waiting for the clock"; clicking it
  opens the list, each with Cancel, and "Cancel all".
- **Cancel.** Withdraws the order before it reaches the sim. Nothing is
  charged and nothing is logged. Once the clock ticks the order is given
  and Cancel is gone (a placed bid can still be raised as now; taking a
  bid back after the clock has moved is not part of this spec).
- **Running clock.** Orders are given on the next tick, a fraction of a
  second later, so the pending state flashes by; the chip then becomes
  the real state ("Survey crew out: N days"). No behaviour change.
- **Rejections.** If the sim still rejects an order the interface thought
  was fine, the log line appears as now and the control returns to its
  normal state.
- **Auto-pause and saving.** Pending orders survive a pause, a tab
  change and a speed change. Saving while orders are pending gives them
  first (the save is taken after they are applied to the current day's
  order list, as now); loading or starting a new game discards them.

## Sim rules
None. No state, command, notice or balance change; the golden hash and
the content fingerprint do not move, and saves and traces from the
current build still load. Cancel is a session matter: the order is
removed from the session's queue before the sim sees it.

Amended during the build (owner's decision, 2026-10-10): one change was
made under `sim/`, and it is not a rule. The save reader bounded a vector
of commands by the size of the command variant's default alternative
(`BidOnPlot`, 17 bytes with the tag), so a save holding a queued survey,
build, mine switch, loan, research or cover-costs order, all of which are
shorter, could be refused. The bug predates this spec; saving with orders
waiting makes such saves ordinary. `sim/include/sim/archive.h` now bounds
a variant by its smallest alternative plus the tag. The fix is on the
read side only: the bytes written, `kSaveVersion` (1) and the golden hash
(`0x2a5da3177ab7c69d`) are unchanged.

## Session
- `Session::pendingOrders()` returns the queued commands with a stable
  id each; `Session::cancel(id)` and `cancelAll()` remove them.
- As built: the queue is handed to the sim at the top of each tick,
  before the rivals decide, and in `Session::save()`, which is therefore
  no longer const. A successful `load` clears the queue.
- `Session::committed()` sums what the queue will cost at today's
  quotes (survey fee, bid, mine capex, tech cost, repayment),
  `Session::pendingBorrowed()` sums the principal of queued loans, and
  the client uses view cash minus reserved minus committed plus pending
  borrowed for its affordability checks. Amended during the build: a bid
  on a plot where the player already has a standing bid commits only the
  increase, because the standing bid is already in reserved and the sim
  asks a rebid to cover only the difference. `Session::orderCost(command)`
  is the per-order figure that `committed()` sums.
- **Trace.** A `command` record is written when the order is handed to
  the sim, not when the button is pressed, so a cancelled order never
  appears as given and replay needs no new rule. Giving and cancelling
  are recorded as `ui` records (`order_queued`, `order_cancelled`) so a
  playtest still shows what the player did. The record's day is
  unchanged for orders that are not cancelled. As built: the `ui`
  record's detail is `#<id> <command json>`, so a cancellation can be
  matched to the order it withdrew.

## View
Unchanged.

## Client
Plots, Mines, Research, Finance and Markets controls as above; the status
strip's Committed figure; the pending-orders line and list. Pending chip
uses the existing `chip` component in the accent colour; no new
component. New strings through the prose pipeline (chips and buttons
kept as drafted; sentences polished). Also in this spec: the build id is
read at build time so the trace names the build that is running.

## Tests
- Session: queued orders are listed in order; cancel removes exactly one;
  a cancelled order is never applied and never traced as a command;
  committed sums match the quotes; load and new game clear the queue; a
  trace with queue and cancel records replays with matching hashes.
- Probe: paused scenario with a survey, a bid, a build and a research
  order pending, at both frame sizes; then one cancelled.
- Strings parity.

## Out of scope
Withdrawing a bid after the clock has moved (a sim rule). Planning
several days ahead. Undo for orders already given. Any balance change
(spec 008).

## Decisions
- 2026-10-10: drafted from playtest 3. The owner asked for immediate
  feedback during play and chose cancellable pending orders.
- 2026-10-10: approved by the owner with one amendment: a pending loan
  counts toward Available (the draft would have blocked "borrow, then
  bid" within one paused turn). Pending manual sales do not count.

Owner decisions during the build:
- 2026-10-10: a raised bid commits only the increase over the standing
  bid. The standing bid is already reserved and the sim requires only the
  difference (`validateBid` in `sim/src/plot_market.cpp`), so counting the
  whole amount would have counted the standing bid twice. The Session
  section's "bid amount" is amended accordingly.
- 2026-10-10: one exception to "no sim change", the read-side save fix
  described under "Sim rules". Accepted because the bug was already there
  and this spec makes the affected saves common; nothing written, hashed
  or versioned changes.
- 2026-10-10: strings. Chips, buttons and pending lines are kept exactly
  as drafted; of the new strings only `status.order_cancelled` was
  polished through agy. The existing `status.*_queued` lines were not
  reworded.

Build decisions (made by the implementing agents and accepted by the
build conductor, not by the owner):
- 2026-10-10: orders are handed to the sim at the top of each tick,
  before the rivals decide, and in `Session::save()`. `save()` lost its
  const, which the replay tool's `--save` path had to follow. Order ids
  start at 1 and are never reused in a session.
- 2026-10-10: trace. `command` records are written at hand-over with the
  hand-over day; `ui` records `order_queued` and `order_cancelled` carry
  `#<id> <command json>`. `kTraceVersion` stays at 1, and replay, which
  submits each `command` record through the same session queue, needed
  no new rule.
- 2026-10-10: display costs are clamped at zero, and an order naming a
  plot or tech the view does not show costs nothing. Such orders stay
  queued for the sim to reject (R-001).
- 2026-10-10: "not covered" is derived in the UI model by walking the
  pending orders with a running balance (cash less reserved, plus each
  loan as it is reached, less each covered order). An uncovered order
  takes nothing from the orders after it, so the flag does not cascade.
- 2026-10-10: every order in the client goes through one path,
  `App::order()`, which submits and redraws at once.
- 2026-10-10: a second click on the cover-costs switch while its change
  is pending cancels the pending change instead of queueing its opposite.
- 2026-10-10: the header reads "Available" where it read "Spendable",
  and the figure is net of pending orders.
- 2026-10-10: build id. A build-time target `dc_build_id` in the root
  `CMakeLists.txt` runs `cmake/BuildId.cmake`, which writes
  `<build dir>/generated/dc_build_id.h` only when the revision changes.
  The id is `unknown` unless `<source>/.git` exists. Only
  `game/src/main.cpp` includes the header, so only it recompiles.
- 2026-10-10: four catalogue labels beyond the spec's list, for pending
  sell-policy and cover-costs changes: `ui.pending_policy_on`,
  `ui.pending_policy_off`, `ui.pending_cover_on`, `ui.pending_cover_off`.

## Open after build
Known on 2026-10-10 and not resolved. None of this is done.
- The waiting-orders line hangs under the speed buttons with a 16dp hit
  target (18dp on Deck), below the 36/40dp control height. It needs the
  owner's eye on a display.
- `main.rcss` gained off-scale layout dimensions, against rule R-004:
  action column widths of 208dp and 224dp, a 220dp start column, a list
  440dp wide with a max-height of 480dp.
- "Cancel all" reuses the singular status line
  (`status.order_cancelled`).
- Loan credit and the loan field's prefill do not account for a pending
  loan. The bid status line still says the bid is lodged "for tomorrow",
  and the survey, build and research lines still say "tomorrow" as well.
- Other techs' Start buttons stay enabled while a research order is
  pending; the sim rejects the second.
- Two pending bids on the same plot are each costed against the standing
  bid, so Committed overstates in that case.
- Not verified on a display. Not yet run through CI (Windows/MSVC,
  Ninja). Codex review and playtest still to come.
