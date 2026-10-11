---
journey_id: J-002
title: A player sizes, works and closes a mine
actor: The player
actor_role: Owner of a mining company, at any stage of a game
trigger: |
  A survey comes back on a plot the player owns, giving a range for what is
  in the ground. Later, the mine's monthly figure changes character: it is
  earning well and could earn more, or it costs more to run than it did.
outcome: |
  The player can say why the mine was the size it was, enlarged it at a
  moment that paid back, and closed it while it was still ahead. The mine's
  whole life made more than it cost, and they saw the end coming.
status: draft
captured_at: 2026-10-11T01:30:00Z
captured_by: Engineer and Designer (the owner, who has played three monitored playtests); drafted by the facilitator from the playtest write-ups and the Earth strategy document and confirmed by the owner
sources: [interactive]
---

# J-002 — A player sizes, works and closes a mine

## Actor

The same player as in J-001: someone running their own mining company who
likes reading the numbers and making a considered move with the clock
stopped. Here they are at any stage, from their first mine to their tenth.
They care about not over-building on a guess, and about getting the most out
of a deposit before it turns on them.

## Trigger

A survey comes back on a plot they own. It gives a range for what is in the
ground, not a figure. The player now has to decide what to build there.
Later it starts again each time the mine's monthly figure changes character:
it is earning well and could earn more, or it is costing more to run than it
did.

## Outcome

The player can say why the mine was the size it was, enlarged it at a moment
that paid back, and closed it while it was still ahead. The mine's whole
life made more than it cost, and they saw the end coming rather than being
told afterwards.

## Steps

1. They open the plot and read the survey: which ores, how much, how sure.
2. They look at the price of each ore and what a mine there would cost to
   run.
3. They pick a size for the mine. A big one costs more up front and digs
   faster; a small one is cheap and slow. They weigh that against how sure
   the survey is and how much cash they have.
4. They place the build order and wait for it to open.
5. Over the following years they check the mine's panel now and then: what
   it earns a day, how much ore is left, what it costs to run.
6. When it is earning well and there is plenty left, they pay to enlarge it.
7. As the ore is worked out, the running cost climbs. They watch the daily
   figure shrink.
8. In a slump they mothball it, as in J-001, and reopen it when the price
   recovers.
9. When it no longer pays even at a normal price, they pay to close it for
   good and put the money elsewhere.

## Failure modes

- Today there is nothing to choose: every mine is the same size, never
  changes, and a good deposit never runs out, so the player builds it and
  forgets it.
- They build big on a hopeful survey and the deposit turns out small, so the
  mine never earns back what it cost.
- They build small on a rich deposit and a rival floods the market first.
- They enlarge a mine just before the price falls.
- They cannot tell from the screen how fast the running cost is rising, so
  the mine starts losing money as a surprise.
- They hold a dying mine too long because closing costs money, or close one
  that a price recovery would have saved.

## Tools / Systems touched

- The Plots tab and plot panel (survey, build, enlarge, close)
- The mine rows on the Dashboard
- The Market tab, for prices
- The Finance tab, for cash
- The log and its stop rules
- The pause and speed controls

## Emotional journey

Hopeful and a little unsure when sizing on a range. Pleased when an
enlargement pays back. Uneasy as the daily figure shrinks. Relieved, or
regretful, after closing.

## Open questions

- Should the player be told when a mine's running cost has risen past a
  mark, as they are for a losing mine?
- Can a closed mine's plot be used again?
