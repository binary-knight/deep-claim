# 007: Orders show at once, and can be taken back

Status: approved 2026-10-10, with the pending-loan amendment (see Decisions).

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

## Session
- `Session::pendingOrders()` returns the queued commands with a stable
  id each; `Session::cancel(id)` and `cancelAll()` remove them.
- `Session::committed()` sums what the queue will cost at today's
  quotes (survey fee, bid amount, mine capex, tech cost, repayment),
  `Session::pendingBorrowed()` sums the principal of queued loans, and
  the client uses view cash minus reserved minus committed plus pending
  borrowed for its affordability checks.
- **Trace.** A `command` record is written when the order is handed to
  the sim, not when the button is pressed, so a cancelled order never
  appears as given and replay needs no new rule. Giving and cancelling
  are recorded as `ui` records (`order_queued`, `order_cancelled`) so a
  playtest still shows what the player did. The record's day is
  unchanged for orders that are not cancelled.

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
