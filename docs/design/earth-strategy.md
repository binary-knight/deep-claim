# Earth strategy: what makes mining on Earth a game

Status: approved by the owner 2026-10-10 (pillars, order, measures, goal,
length and failure; see Decisions at the end). It sets what to build after
spec 007 and in what order. Each system still gets its own spec, and the
numbers belong there.

**Superseded in part, 2026-10-11.** `docs/design/core-loop.md` changes the
order and the centre of gravity: the goal (system 7) comes first as a
deadline, a race and a space program that consumes goods; processing moves
from "later" to the middle of the game; systems 1 and 2 become parts of
that loop. The diagnosis and the four pillars below still stand.

## Why this document exists

The owner's direction (`DOMAIN.md`): build the whole Earth mining game
first and make it fun, challenging, rewarding and hard to put down, deep
underneath but easy to learn. Space, conflict between companies and
multiplayer come later and are not designed here.

Three playtests say the Earth game is not there yet, and they agree on why.

## Diagnosis: where the decisions go

From `docs/playtests/2026-10-10.md` and `docs/specs/006-balance.md`:

1. **Money stops being scarce.** One iron mine on a major deposit earned
   about $1M a month from year two. Cash went from $3.7M to $177M with 33
   orders in eight years. Nothing the player could buy cost enough to matter.
2. **Prices do not punish anyone.** Every market sat at five times demand
   and 0.35 to 0.5 times base price from year three, and mines were still
   profitable. A flooded market that still pays is not a constraint.
3. **A mine is a single decision.** Build it and it runs until the ore is
   gone, which for a good deposit is never (a 5M t find outlasts the game).
   There is no sizing, no expansion, no decline, no reason to close.
4. **Land is cheap to hold and all alike.** A plot costs its bid and then
   nothing. Regions differ in what is under them, not in what it costs to
   work there. The rival held 44 plots; the player felt no loss.
5. **The rival does not threaten.** It outgrew the player tenfold and the
   player had no reason to care, because the only contact is the price of
   ore, and see point 2.
6. **Nothing to aim at.** There is no goal beyond a bigger number.

A strategy game needs scarce resources, choices that exclude each other, a
rival whose moves change what is best for you, and something to aim for.
Deep Claim currently has the simulation for all four and the tension of
none.

## Pillars

The tests for any Earth feature. A feature that fails all four does
not belong yet.

- **Money is always tight for something.** At every stage there is a thing
  worth wanting that the player cannot yet afford.
- **Every asset can become a liability.** A mine, a plot, a loan or a
  stockpile can turn against its owner, so holding is a decision too.
- **The rival is felt.** What a rival does should regularly change what
  the player ought to do next, and the player should be able to do the
  same to them through the economy.
- **Depth is discovered, not front-loaded.** A new player buys land, digs
  and sells. Each deeper system arrives through research or scale, with
  one new idea at a time, and can be read off the screen.

## The measure

"Fun" is the owner's verdict in play, but the trace gives numbers to watch
between playtests, and `sim_runner --sweep` can report most of them:

| Measure | Playtest 3 | Aim |
| --- | --- | --- |
| Player orders per game year after year 3 | about 3 | 15 or more that are not routine |
| Share of months where a wanted purchase is unaffordable | near 0 after year 2 | 30 to 60% all game |
| Longest stretch with no land or mine decision | 900 days | under 180 days |
| Mean price over base, years 3 to 10 | 0.35 to 0.5 | 0.7 to 1.3, with swings |
| Mines mothballed or closed for economic reasons | 0 | common |
| Leader's mines over the trailer's at year 10 (AI vs AI) | 1.08 | under 2, and against a human too |

These are working aims, accepted by the owner and revised after each
playtest. A game has no fixed length (see Decisions), so sweeps run to 20
years to cover the late game.

## Systems, in order

Each entry says what decision it creates, what it costs to build, and what
it deliberately leaves out. Numbers belong in the specs.

### 1. Markets that bite (spec 008, already agreed as next)
Demand sized to the 120-plot world, so supply from a handful of mines moves
the price and a flooded market falls below the cost of digging.
**Decisions:** when to sell and when to hold; whether to mothball; whether
to open the next mine into a falling price; which resource to move into.
**Cost:** data and the demand model only; no new commands.
**Why first:** every later system assumes price matters.

### 2. Mine economics: size, decline and closing
A mine is built at a chosen scale (throughput against capex), can be
expanded, and gets more expensive to run as it goes deeper: opex per tonne
rises as the deposit is worked out, so a mine has a best life rather than an
endless one. Closing a mine costs money (reclamation) and frees the
overhead.
**Decisions:** how big to build on an uncertain survey; when to expand;
when a mine has stopped paying; whether to run a marginal mine through a
slump. **Cost:** sim state for scale and depth, a close-mine command (spec
006 already noted its absence), golden hash moves. **Leaves out:** shafts,
layouts, crews.
**Why second:** it is the first step toward the deeper engineering
simulation the owner wants, taken where it creates decisions.

### 3. Land that costs and regions that differ
Holding land carries a yearly fee; undeveloped land lapses back to the
market after a term. Each region has a haulage cost per tonne to market,
which shared infrastructure (a rail spur, a port berth) lowers for everyone
working there, built by whoever pays.
**Decisions:** claim early and pay to hold, or wait and risk losing it;
concentrate in one region or spread; build the railway your rival will
also use. **Cost:** holding fees are small; infrastructure is a new
entity. **Leaves out:** a map with routes; transport stays a number per
region.

### 4. Capital under pressure
Overheads grow with the size of the company, borrowing capacity depends on
assets and earnings rather than a flat cap, and interest depends on debt.
Bankruptcy still ends the game, but it is reached through steps the player
can see and fight: a credit freeze, then forced sale of stock, then forced
sale of land or a mine. Losing stays possible; losing by surprise does not.
**Decisions:** grow on debt or on earnings; how much cash to hold against
a slump; what to sell when squeezed. **Cost:** finance rules only.
**Note:** this is the growth brake spec 006 left open for the player, and
it applies to rivals equally.

### 5. Contracts
Buyers offer supply contracts: a fixed price for a tonnage over a term,
with a penalty for shortfall. Spot selling remains.
**Decisions:** lock in a price or ride the market; commit output you do not
yet have; win a contract to deny it to a rival. **Cost:** a new entity,
commands and notices. **Leaves out:** negotiation; offers are take or
leave.

### 6. Rivals with character
Two or three rival temperaments (the land-grabber, the low-cost digger, the
specialist) with knobs in `data/ai.json`, and rivals that respond to the
player: bidding against a plot the player has surveyed, cutting output when
prices fall.
**Decisions:** all of the above become contested. **Cost:** AI work only,
measured by sweeps. **Leaves out:** espionage and sabotage, which belong to
the later conflict layer.

### 7. Something to aim for
Company milestones with a visible ladder (first mine, first region
dominated, first $100M), and a final Earth objective that is expensive and
slow: the capital to found a space program. This document does not design
that program; Earth's end goal is the wealth and capability to start it, so
that money has a purpose all the way up. The game has no year limit: it runs
until the goal is reached or the company fails, and it has to be worth
playing at every stage on the way.
**Decisions:** when to stop reinvesting and start saving. **Cost:** small.

### Later, once the above holds up
- **Geology in depth:** ore grade and depth bands, so two deposits of the
  same tonnage are different propositions; surveys in tiers (a cheap broad
  look, a costly drill programme).
- **Processing:** concentrators and smelters that turn ore into
  higher-value product, with their own capacity and markets.
- **Workforce and safety, environment and permits:** only if they create
  choices rather than chores.

These are the "deep geological, engineering and systems simulation" the
owner wants. They come after 1 to 7 because depth added to a game without
tension only adds reading.

## What this document does not do

No space, asteroid, military, espionage or multiplayer design. No balance
numbers. No change to the rule that every decision can be made with the
clock stopped.

## Decisions

- 2026-10-10: the owner adopted the four pillars as written.
- 2026-10-10: the order of systems stands as written: markets, mine
  economics, land and regions, capital, contracts, rivals, goals.
- 2026-10-10: Earth's objective is to found the space program. Earth has no
  separate ending.
- 2026-10-10: no fixed game length. Balance sweeps move to 20 years.
- 2026-10-10: failure comes in steps before the end; bankruptcy still ends
  the game.
- 2026-10-10: the measures table is accepted as working aims.
