---
journey_id: J-001
title: A player rides out a falling price
actor: The player
actor_role: Owner of a mining company, a few years into a game
trigger: |
  The player notices a price sliding: the chart for an ore has been heading
  down for a couple of months, or the month-end figures show a mine earning
  much less than it did, or the log says a rival has opened another mine in
  the same ore.
outcome: |
  The player made a call and can see that it mattered, and the company came
  through the lean months without missing a loan payment.
status: draft
captured_at: 2026-10-10T16:00:00Z
captured_by: Engineer and Designer (the owner, who has played three monitored playtests); drafted by the facilitator from the playtest write-ups and confirmed section by section
sources: [interactive]
---

# J-001 — A player rides out a falling price

## Actor

A single player running their own mining company, a few years into a game,
with two or three mines working and money in the bank. They like reading the
numbers and making a considered move with the clock stopped. They care most
about growing the company without being caught out, and about feeling that a
good call was theirs.

## Trigger

The player notices a price sliding. The chart for an ore has been heading
down for a couple of months, or the month-end figures show a mine earning
much less than it did, or the log says a rival has opened another mine in
the same ore. Today nothing stops the clock for this, so they notice only if
they happen to look.

## Outcome

The player made a call and can see that it mattered. They mothballed the
weaker mine and held their stock, the price came back over the next few
months because supply had dropped, and they reopened and sold the stockpile
for clearly more than they would have got in the slump. Or they sold early,
moved their next mine into another ore, and watched the rival keep digging
at a loss. Either way the company came through the lean months without
missing a loan payment, and the month-end figures show the difference the
decision made.

## Steps

With the clock stopped:

1. Open the Market tab and look at the falling ore: its price against base,
   the chart, and how much is being supplied against demand.
2. Check which of their mines dig that ore and what each costs to run per
   day, to see which are still earning at this price and which are losing.
3. Look at how much of that ore is in storage and what the storage costs.
4. Decide what to do with the stock: sell now before it falls further, or
   raise the floor on the auto-sell order and hold until the price recovers.
5. Decide about the weakest mine: keep it running, or mothball it to stop
   the losses and take supply off the market.
6. Look at what the rival is doing in that ore, to judge whether the slump
   will last.
7. Reconsider what was planned next: put off the mine they were about to
   build, or switch to a plot with a different ore whose price is holding.
8. Check the cash position and loans, to be sure the company can ride out a
   few lean months.
9. Restart the clock and watch the price over the next weeks, ready to
   reopen the mine when it recovers.

## Failure modes

- **They do not notice in time.** Nothing stops the clock when a price
  falls, so months of losses pile up before the player looks at the Market
  tab.
- **They cannot tell which mine is losing money.** The game shows a mine's
  output and its daily running cost, but not plainly what the mine earns or
  loses per day at today's price, so the sum is done in the player's head.
- **They cannot tell why the price fell or whether it will last.** Their own
  output, the rival's new mines, or a world event that ends next month: the
  supply and demand behind the price are hard to read.
- **Holding stock quietly costs them.** In playtest 3 auto-sell was switched
  off and nine months of storage charges ran up before one manual sale.
- **Mothballing feels like a trap.** It is not obvious what a mothballed
  mine still costs, how long reopening takes, or whether it was worth it.
- **Nothing they do seems to matter.** If the rival keeps flooding the
  market regardless, the player's restraint only helps the rival.

## Tools / Systems touched

Everything is inside the game:

- The Market tab: prices, the price chart, the auto-sell switch with its
  floor and reserve, the manual sale control.
- The Dashboard: the "Needs you" list, the plots-and-mines table, the
  prices panel, the finance row.
- The Plots tab: the mine's panel with Mothball and Reopen, and plots for
  sale when choosing a different ore.
- The Finance tab: cash, loans, monthly storage cost.
- The Log: news of rival mines, sales and world events.
- The clock controls: pause, speed, step a day, and the auto-pause rules.
- Sums done in the player's head to work out whether a mine is worth
  running. This is a gap, not a tool.

## Emotional journey

A jolt of worry on noticing the slide. Absorbed while reading the numbers.
A little tense at the moment of committing to mothball or hold, because it
might be the wrong call. Impatient while the clock runs and the price has
not moved yet. Then satisfied, even a bit smug, when the price recovers and
the decision is shown to have paid. When it goes wrong the feeling is closer
to irritation at the game than at themselves, because they could not see
what they needed.

## Open questions

- Should the game stop the clock and tell the player when a price falls
  sharply or a mine starts losing money?
- Should the game show plainly what each mine earns or loses per day at
  today's price?
- Should it show why a price moved (own output, rival output, world event)?
- What does a mothballed mine cost and how quickly can it reopen, and is
  that shown?
- This journey was captured before prices have ever bitten in play (in
  three playtests no mine was mothballed for price). Revisit it after the
  first playtest of spec 008.
